#include "cpu.hpp"

#include "cdb.hpp"
#include "instruction_fetch.hpp"
#include "instruction_decoder.hpp"
#include "load_store_buffer.hpp"
#include "memory_loader.hpp"
#include "reservation_station.hpp"
#include "rob.hpp"

namespace riscv {

namespace {

/// 访存类指令判定：load（lb/lh/lw/lbu/lhu）与 store（sb/sh/sw）。
// 后续 Execute 阶段路由 RS 就绪项到 ALU / LSQ 时复用，见 DESIGN.md §5.1。
inline bool is_load_op(const std::string& name) {
    return name == "lb" || name == "lh" || name == "lw" ||
           name == "lbu" || name == "lhu";
}

inline bool is_store_op(const std::string& name) {
    return name == "sb" || name == "sh" || name == "sw";
}

}  // namespace

CPU::CPU() {
    next = cur;
}

bool CPU::load_program(const std::string& path) {
    uint32_t max_addr;
    if (!load_program_from_data(path, memory, max_addr))
        return false;
    cur.pc = 0;  // 从地址 0 开始取指
    return true;
}

// 双状态重构后的 port / wire 骨架


/// 是否有空闲 RS 槽位
// 遍历 rs[] 检查是否存在 busy == false
bool CPU::rs_has_free(const CPUState& s) const {
    return ::riscv::rs_has_free(s);
}

/// ROB 是否满（环形缓冲，(tail+1)%ROB_SIZE == head）
bool CPU::rob_is_full(const CPUState& s) const {
    return ::riscv::rob_is_full(s);
}

/// LSU 是否有空闲槽位接受新访存
bool CPU::lsu_has_free(const CPUState& s) const {
    return ::riscv::lsq_has_free(s);
}

/// ROB 是否为空（head == tail）
bool CPU::rob_empty(const CPUState& s) const {
    return ::riscv::rob_empty(s);
}

// --- port 方法（每周期至多调用一次） ---

/// 发射一条指令（取指+解码+分配 RS/ROB/LSQ）
// 取指、解码、资源检查、分支预测、分配 ROB→LSQ（访存类）→RS、回填 LSQ.rs_tag、
// 更新 reg_status（指向 ROB 条目索引）、更新 PC。
// 取指读 CPU::memory（单实例，非 CPUState）；终止哨兵 0x0ff00513 作为普通指令发射，
// 由 commit 阶段停机（见 DESIGN.md §4.1）。
//
// 实现遵循 DESIGN.md §6 的 11 步流程：
//   1. cur.stopped → 直接返回
//   2. 取指 fetch_instruction(memory, pc)
//   3. 解码 decode_instruction(ins, pc)；终止哨兵正常发射
//   4. 资源检查 ROB/RS/LSQ（访存类）→ 不足则 stall（PC 不推进，下周期重取同一指令）
//   5. 分支预测：B 用 2-bit 预测器；JAL 目标精确（永不 flush）；JALR not-taken 基线
//   6. 分配 ROB（is_branch = B||J||JALR；branch_target 由 ALU 执行回填，此处恒 0）
//   7. 访存类分配 LSQ（先占槽、填 rob_idx，rs_tag 待步骤 9 回填）
//   8. 分配 RS（访存类同样占 RS 槽，由 LSQ 显式释放，见 §5.1/§5.3）
//   9. 同周期回填 LSQ.rs_tag
//  10. 更新 reg_status[rd] = rob_idx（指向 ROB 条目，唤醒 tag 统一为 rob_idx）
//  11. 更新 PC（控制流在步骤 5 已设预测目标；其余 pc+4）
void CPU::issue(const CPUState& c, CPUState& n) {
    // 1. 停机后不再发射
    if (c.stopped) {
        return;
    }

    // 2. 取指（读 CPU::memory 单实例，非 CPUState）
    const uint32_t ins = fetch_instruction(memory, c.pc);

    // 3. 解码；终止哨兵 0x0ff00513 作为普通指令走完整生命周期，由 commit 阶段停机
    const Command cmd = decode_instruction(ins, c.pc);

    const bool is_load = is_load_op(cmd.cmdname);
    const bool is_store = is_store_op(cmd.cmdname);
    const bool is_mem = is_load || is_store;

    // 4. 资源检查：ROB / RS / LSQ（仅访存类）任一不足 → stall
    if (rob_is_full(c) || !rs_has_free(c) || (is_mem && !lsu_has_free(c))) {
        return;
    }

    // 5. 分支预测
    bool predicted_taken = false;   // B: 预测器结果；J/JALR: 恒 true
    uint32_t predicted_target = 0;  // J: pc+imm 精确；JALR: pc+4 猜测；B 无预测目标恒 0
    uint32_t next_pc = c.pc + 4;    // 默认顺序流（步骤 11 兜底）

    const bool is_control = (cmd.type == InstrType::B) ||
                            (cmd.type == InstrType::J) ||
                            (cmd.type == InstrType::I && cmd.cmdname == "jalr");

    if (cmd.type == InstrType::B) {
        // B 型：2-bit 预测器决定预测方向
        predicted_taken = c.predictor.predict(cmd.pc);
        next_pc = predicted_taken ? (cmd.pc + static_cast<uint32_t>(cmd.imm))
                                  : (cmd.pc + 4);
    } else if (cmd.type == InstrType::J) {
        // JAL：目标 = pc + imm 发射时精确已知，预测精确、永不 flush
        predicted_taken = true;
        predicted_target = cmd.pc + static_cast<uint32_t>(cmd.imm);
        next_pc = predicted_target;
    } else if (cmd.type == InstrType::I && cmd.cmdname == "jalr") {
        // JALR：目标依赖 rs1，采用 not-taken 预测（predicted_target = pc + 4）；
        // commit 比较真实目标 != predicted_target → flush
        predicted_taken = true;
        predicted_target = cmd.pc + 4;
        next_pc = predicted_target;
    }

    // 6. 分配 ROB
    //    branch_predicted = B ? predicted_taken : true（J/JALR 恒 taken）
    //    branch_target 恒 0：B/J/JALR 真实目标由 ALU 执行回填、经 CDB 捕获
    const uint32_t rob_idx = static_cast<uint32_t>(rob_allocate(
        c, n, cmd,
        is_control,
        (cmd.type == InstrType::B) ? predicted_taken : true,
        0,                    // branch_target（ALU 回填）
        predicted_target,
        c.pc + 4));           // next_pc（顺序流 / J/JALR 返回地址）

    // 7. 访存类分配 LSQ（先占 LSQ 槽、填 rob_idx；rs_tag 由步骤 9 同周期回填）
    int32_t lsq_idx = -1;
    if (is_mem) {
        lsq_idx = lsq_allocate(c, n, cmd, static_cast<int32_t>(rob_idx));
    }

    // 8. 分配 RS（访存类同样占 RS 槽——地址基址/数据经 RS 捕获，槽由 LSQ 显式释放）
    const int32_t rs_tag = rs_allocate(c, n, cmd, static_cast<int32_t>(rob_idx));

    // 9. 同周期回填关联：LSQ 记录关联 RS 槽（load 经 CDB 广播后 / store 完成回填时释放）
    if (lsq_idx >= 0) {
        n.lsu_slots[lsq_idx].rs_tag = rs_tag;
    }

    // 10. 更新 reg_status：指向 ROB 条目（唤醒 tag 统一为 rob_idx，见 §2 顶部说明）
    if (cmd.writes_rd() && cmd.rd != 0) {
        n.reg_status[cmd.rd] = static_cast<int32_t>(rob_idx);
    }

    // 11. 更新 PC（控制流指令在步骤 5 已设预测目标；其余顺序流 pc+4）
    n.pc = next_pc;
}

/// 执行阶段：RS 就绪项执行、LSU 推进
// RS 就绪项路由到 ALU / LSU，LSU 流水线推进
void CPU::execute(const CPUState& /*c*/, CPUState& /*n*/) {
}

/// CDB 仲裁（组合线）：从完成结果中选一个广播（扫描 RS.done 与 LSQ load 完成项，选 rob_idx 最小者）
// 算术/分支来自 RS.done（rob_idx=rs.rob_idx）；load 由 LSQ 完成项直通 CDB（rob_idx=lsq.rob_idx）
// 广播后释放生产者：算术/分支释放自身 RS 槽；load 释放关联 RS 槽 rs[lsq.rs_tag] + LSQ 槽；
// 释放的槽位下一周期才对 issue 可见（issue 只读 cur）
CdbSignal CPU::cdb_arbitrate(const CPUState& c, CPUState& n) {
    return ::riscv::cdb_arbitrate(c, n);
}

/// CDB 广播捕获（组合线）：以仲裁信号为输入，同周期广播到 RS/LSQ/ROB
// RS/LSQ 按 sig.rob_idx 匹配 q1/q2 捕获 value；ROB 按 sig.rob_idx 捕获 → ready。
// （唤醒/捕获 tag 统一为 rob_idx，见 DESIGN.md §2 顶部说明）
void CPU::cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig) {
    ::riscv::cdb_capture(c, n, sig);
}

/// ROB 提交：顺序提交一条指令
// head 若 ready → 写 regs/写 memory、更新 predictor、检测终止哨兵、释放条目、head++
// 写 store 内存时使用 CPU::memory（单实例，按序提交无并发写冲突）
void CPU::commit(const CPUState& c, CPUState& n) {
    ::riscv::rob_commit(c, n, memory);
}

/// Flush 流水线（分支预测失败时）
// 清空 RS/LSU、回退 ROB tail、恢复 reg_status、重置 PC
void CPU::flush_pipeline(const CPUState& /*c*/, CPUState& /*n*/,
                         uint32_t /*target_pc*/) {
}

/// 主循环（串联全部阶段）
// x0 置零 → cdb_arbitrate → cdb_capture → commit → execute → issue → flush → 周期推进 + MAX_CYCLES 上限
uint8_t CPU::run() {
    return 0;
}

}  // namespace riscv