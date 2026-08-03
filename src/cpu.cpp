#include "cpu.hpp"

#include "instruction_fetch.hpp"
#include "instruction_decoder.hpp"
#include "memory_loader.hpp"

namespace riscv {

CPU::CPU() {
    next = cur;
}

bool CPU::load_program(const std::string& path) {
    uint32_t max_addr;
    if (!load_program_from_data(path, cur.memory, max_addr))
        return false;
    cur.pc = 0;  // 从地址 0 开始取指
    return true;
}

// 双状态重构后的 port / wire 骨架


/// 是否有空闲 RS 槽位
// 遍历 rs[] 检查是否存在 busy == false
bool CPU::rs_has_free(const CPUState& /*s*/) const {
    return false;
}

/// ROB 是否满（环形缓冲，(tail+1)%ROB_SIZE == head）
bool CPU::rob_is_full(const CPUState& s) const {
    return ::riscv::rob_is_full(s);
}

/// LSU 是否有空闲槽位接受新访存
bool CPU::lsu_has_free(const CPUState& /*s*/) const {
    return false;
}

/// ROB 是否为空（head == tail）
bool CPU::rob_empty(const CPUState& s) const {
    return ::riscv::rob_empty(s);
}

// --- port 方法（每周期至多调用一次） ---

/// 发射一条指令（取指+解码+分配 RS/ROB/LSQ）
// 取指、解码、资源检查、分支预测、分配 ROB→LSQ（访存类）→RS、回填 LSQ.rs_tag、
// 更新 reg_status（指向 ROB 条目索引）、更新 PC。
// 终止哨兵 0x0ff00513 作为普通指令发射，由 commit 阶段停机（见 DESIGN.md §4.1）。
void CPU::issue(const CPUState& /*c*/, CPUState& /*n*/) {
}

/// 执行阶段：RS 就绪项执行、LSU 推进
// RS 就绪项路由到 ALU / LSU，LSU 流水线推进
void CPU::execute(const CPUState& /*c*/, CPUState& /*n*/) {
}

/// CDB ：从完成结果中选一个广播（扫描 RS.done 与 LSQ load 完成项，选 rob_idx 最小者）
// 算术/分支来自 RS.done（rob_idx=rs.rob_idx）；load 由 LSQ 完成项直通 CDB（rob_idx=lsq.rob_idx）
// 广播后释放生产者：算术/分支释放自身 RS 槽；load 释放关联 RS 槽 rs[lsq.rs_tag] + LSQ 槽；
// 释放的槽位下一周期才对 issue 可见（issue 只读 cur）
void CPU::cdb_arbitrate(const CPUState& /*c*/, CPUState& /*n*/) {
}

/// CDB ：所有 RS 槽位 + LSQ 槽位 + ROB 捕获 CDB 结果
// RS/LSQ 按 cdb.rob_idx 匹配 q1/q2 捕获 value；ROB 按 cdb.rob_idx 捕获 → ready。
// （唤醒/捕获 tag 统一为 rob_idx，见 DESIGN.md §2 顶部说明）
void CPU::cdb_capture(const CPUState& c, CPUState& n) {
    ::riscv::rob_cdb_capture(c, n);
}

/// ROB 提交：顺序提交一条指令
// head 若 ready → 写 regs/写 memory、更新 predictor、检测终止哨兵、释放条目、head++
void CPU::commit(const CPUState& c, CPUState& n) {
    ::riscv::rob_commit(c, n);
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