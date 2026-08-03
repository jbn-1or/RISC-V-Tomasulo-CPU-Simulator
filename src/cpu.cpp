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

/// 发射一条指令（取指+解码+分配 RS/ROB）
// 取指、终止哨兵检查、解码、资源检查、分支预测、分配 RS/ROB、更新 reg_status、更新 PC
void CPU::issue(const CPUState& /*c*/, CPUState& /*n*/) {
}

/// 执行阶段：RS 就绪项执行、LSU 推进
// RS 就绪项路由到 ALU / LSU，LSU 流水线推进
void CPU::execute(const CPUState& /*c*/, CPUState& /*n*/) {
}

/// CDB ：从完成结果中选一个广播（优先级 RS > LSU）
// 扫描 RS done 项与 LSU 完成项，选一个填入 cdb
void CPU::cdb_arbitrate(const CPUState& /*c*/, CPUState& /*n*/) {
}

/// CDB ：所有 RS 槽位 + ROB 捕获 CDB 结果
// q1/q2 匹配捕获 value；ROB rs_tag 匹配 → ready
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