#include "cpu.hpp"

#include "cdb.hpp"
#include "instruction_fetch.hpp"
#include "instruction_decoder.hpp"
#include "load_store_buffer.hpp"
#include "memory_loader.hpp"
#include "reservation_station.hpp"
#include "rob.hpp"

namespace riscv {

CPU::CPU() {
    next = cur;
}

bool CPU::load_program(std::istream& input) {
    uint32_t max_addr;
    if (!load_program_from_stream(input, memory, max_addr))
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

    const bool is_load = cmd.is_load();
    const bool is_store = cmd.is_store();
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

    // 9.5 操作数旁路（同周期广播竞态的必要修正，见 DESIGN.md §3 时序）：
    // 双状态模型下 CDB capture 只读 cur 且先于 issue 执行，看不到本周期才诞生的
    // 消费者——若生产者的广播恰好发生在消费指令被发射的同一周期，其 q 指向的
    // ROB 条目将**不会再有第二次广播**，导致永久等待（死锁）。
    // 因此：reg_status 指向的生产者结果若已可用，直接旁路取值、q 置 -1。
    //   ① ROB 条目已捕获（广播发生在更早周期）→ 用 rob.value
    //   ② RS 生产者已 done（结果就绪，本周期已/待广播）→ 用 rs.result
    //   ③ LSQ load 生产者已 done（读回值就绪）→ 用 lsu.result
    // 既有的"q 等待未就绪生产者"路径不变（由 CDB 广播唤醒，见 §2 顶部说明）。
    auto operand_bypass = [&](int32_t& q, uint32_t& v) {
        if (q < 0 || q >= ROB_SIZE) {
            return;
        }
        if (c.rob[q].ready) {           // ① 广播已发生在更早周期
            v = c.rob[q].value;
            q = -1;
            return;
        }
        for (int i = 0; i < RS_SIZE; ++i) {          // ② 算术/分支生产者已执行
            if (c.rs[i].busy && c.rs[i].done && c.rs[i].rob_idx == q) {
                v = c.rs[i].result;
                q = -1;
                return;
            }
        }
        for (int i = 0; i < LSU_COUNT; ++i) {        // ③ load 生产者读回值已就绪
            if (c.lsu_slots[i].busy && c.lsu_slots[i].done &&
                c.lsu_slots[i].rob_idx == q) {
                v = c.lsu_slots[i].result;
                q = -1;
                return;
            }
        }
    };
    operand_bypass(n.rs[rs_tag].q1, n.rs[rs_tag].v1);
    operand_bypass(n.rs[rs_tag].q2, n.rs[rs_tag].v2);
    if (lsq_idx >= 0) {
        operand_bypass(n.lsu_slots[lsq_idx].q1, n.lsu_slots[lsq_idx].v1);
        operand_bypass(n.lsu_slots[lsq_idx].q2, n.lsu_slots[lsq_idx].v2);
    }

    // 10. 更新 reg_status：指向 ROB 条目（唤醒 tag 统一为 rob_idx，见 §2 顶部说明）
    if (cmd.writes_rd() && cmd.rd != 0) {
        n.reg_status[cmd.rd] = static_cast<int32_t>(rob_idx);
    }

    // 11. 更新 PC（控制流指令在步骤 5 已设预测目标；其余顺序流 pc+4）
    n.pc = next_pc;
}

/// 执行阶段：RS 就绪项执行、LSQ 流水推进（DESIGN.md §3 步骤 3、§5.1、§5.3）
// 1) rs_execute：单 ALU 仲裁——就绪非访存项选 age 最小者执行，结果写回
//    done/result + 分支元数据（is_branch/taken/target），下一周期经 CDB 广播 → ROB 捕获。
// 2) lsq_advance：load/store 流水推进（启动/推进/完成，含 store-to-load 内存序约束）。
// 时序：execute 只读 cur（CDB 捕获写 next，本周期的唤醒下一周期才对 execute 可见）。
void CPU::execute(const CPUState& c, CPUState& n) {
    ::riscv::rs_execute(c, n);
    ::riscv::lsq_advance(c, n, memory);
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

/// Flush 流水线（分支预测失败时，DESIGN.md §10 / §16.2 阶段 7）
// 触发条件：commit 阶段检测到 mispredict（rob_commit 置 need_flush，见 rob.cpp）。
// 执行时机：每周期最后、issue 之后（见 §9）——本周期 issue 新分配的分支后条目一并被清，
//          reg_status 重建基于 next 状态，天然覆盖 issue 的同周期映射。
// 分支条目索引 = c.rob_head（mispredict 由 commit 检测，分支正是本周期刚提交的 head 条目）。
// 步骤（§10）：
//   1. next.pc = target_pc（正确目标，调用方按 §10 步骤 1 由分支条目算出）
//   2. 清 RS：按 age 比较（rob_age(c, rob_idx) > branch_age；c.rob_head 即分支索引、
//      age(分支)=0）清掉分支之后的条目，保留 age <= 分支 age 的合法条目。
//      不可用裸 rob_idx 比较：ROB 回绕时数值大小不代表程序序，会漏清（见 §5.3）。
//   3. 清 LSQ：同上 age 边界
//   4. 清 ROB：遍历分支后 [branch_idx+1, n.rob_tail) 的条目标记 busy=false，回退 tail；
//      reg_status 用"扫描残余 ROB 重建法"恢复（P0-3，见 §10 步骤 4）——对每个 rd 在
//      仍 busy 的残余 ROB 条目中找最近写入者（age 最大），找到指回、否则 -1。
//      不用旧"置 -1"法：分支前未提交指令写同一 rd 时会错误破坏依赖。
//   5. need_flush = false
void CPU::flush_pipeline(const CPUState& c, CPUState& n, uint32_t target_pc) {
    // 分支条目索引 = 本周期刚被 commit 的 head（分支的 branch_taken/target 等
    // 元数据仍可从 c.rob[branch_idx] 读取，commit 只写了 next）
    const int32_t branch_idx = static_cast<int32_t>(c.rob_head);
    const uint32_t branch_age = rob_age(c, branch_idx);  // == 0

    // 1. 恢复 PC 为正确目标
    n.pc = target_pc;

    // 2. 清空 RS 中分支之后的条目（保留 age <= 分支 age 的合法条目）
    for (int i = 0; i < RS_SIZE; ++i) {
        if (n.rs[i].busy && rob_age(c, n.rs[i].rob_idx) > branch_age) {
            n.rs[i].busy = false;
            n.rs[i].done = false;
        }
    }

    // 3. 清空 LSQ 中分支之后的条目（同上 age 边界）
    for (int i = 0; i < LSU_COUNT; ++i) {
        if (n.lsu_slots[i].busy && rob_age(c, n.lsu_slots[i].rob_idx) > branch_age) {
            n.lsu_slots[i].busy = false;
            n.lsu_slots[i].done = false;
            n.lsu_slots[i].address_ready = false;
        }
    }

    // 4. 清空 ROB 中分支之后的条目并回退 tail
    //    新 tail = 分支条目之后一槽（分支条目自身已由 commit 释放并推进 head）
    const uint32_t new_tail = (static_cast<uint32_t>(branch_idx) + 1) % ROB_SIZE;
    for (uint32_t i = new_tail; i != n.rob_tail; i = (i + 1) % ROB_SIZE) {
        if (n.rob[i].busy) {
            n.rob[i].busy = false;
            n.rob[i].ready = false;
        }
    }
    n.rob_tail = new_tail;

    // 恢复 reg_status：扫描残余 ROB 重建法（全量扫描仍 busy 的条目；
    // 每个 rd 找最近写入者——正常流水执行下分支后条目全清 → 全部恢复 -1）
    n.reg_status[0] = -1;  // x0 恒就绪
    for (int rd = 1; rd < 32; ++rd) {
        int32_t found = -1;
        uint32_t best_age = 0;  // age 最大 = 程序序最近
        for (int i = 0; i < ROB_SIZE; ++i) {
            const RobEntry& e = n.rob[i];
            if (!e.busy || !e.cmd.writes_rd() ||
                static_cast<int32_t>(e.cmd.rd) != rd) {
                continue;
            }
            const uint32_t a = rob_age(n, i);
            if (a >= best_age) {
                best_age = a;
                found = i;
            }
        }
        n.reg_status[rd] = found;
    }

    // 5. 清除 flush 标志（CDB 为组合线信号，无跨周期 valid 需清，见 §5.4）
    n.need_flush = false;
}

/// 主循环（串联全部阶段，DESIGN.md §9 / §16.2 阶段 8）
// 每周期顺序：x0 置零 → CDB 仲裁 → CDB 捕获 → commit → execute → issue → flush。
// 顺序依据（吞吐量最大化，见 DESIGN.md §2/§9）：CDB 先广播（释放生产者槽位）→ commit
// 提交（释放 ROB head、清 reg_status）→ execute 执行 → issue 发射（复用被释放的槽位）
// → flush 冲刷（在 issue 之后，按 §10 重建 reg_status，天然覆盖 issue 同周期的映射）。
// commit 检测终止哨兵 0x0ff00513 时返回 return_value（哨兵走完整流水线，见 §4.1）。
uint8_t CPU::run() {
    constexpr uint64_t MAX_CYCLES = 500000000;

    for (;;) {
        next.regs[0] = 0;                         // x0 每周期强制置 0
        CdbSignal sig = cdb_arbitrate(cur, next); // CDB 仲裁（组合线，返回广播信号）
        cdb_capture(cur, next, sig);              // CDB 广播（同周期捕获）
        commit(cur, next);                        // 提交（先于 issue，见 §9 注）
        if (next.stopped) {
            return next.return_value;             // 终止哨兵已提交 → 停机返回
        }
        execute(cur, next);                       // 执行（RS 就绪项 + LSQ 推进）
        if (!cur.stopped) {
            issue(cur, next);                     // 发射
        }
        if (next.need_flush) {
            // 正确目标由分支条目计算（§10 步骤 1）：
            //   B 型：taken → branch_target；not taken → next_pc（=pc+4）
            //   JALR：branch_target（ALU 真实目标）
            // 不能直接传 next.pc：同周期 issue 已按预测流覆盖了它
            const RobEntry& br = cur.rob[cur.rob_head];
            const uint32_t target =
                (br.cmd.type == InstrType::B)
                    ? (br.branch_taken ? br.branch_target : br.next_pc)
                    : br.branch_target;
            flush_pipeline(cur, next, target);    // 冲刷（分支后指令全清 + reg_status 重建）
        }
        next.cycles = cur.cycles + 1;             // 周期推进
        cur = next;
        if (cur.cycles > MAX_CYCLES) {
            return cur.regs[10] & 0xFF;           // 超上限兜底返回
        }
    }
}

}  // namespace riscv