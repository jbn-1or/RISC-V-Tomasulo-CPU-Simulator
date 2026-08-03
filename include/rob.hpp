#pragma once

#include <cstdint>
#include "command.hpp"

namespace riscv {

constexpr int ROB_SIZE = 16;

struct RobEntry {
    bool busy = false;
    Command cmd;

    bool ready = false;
    uint32_t value = 0;

    // store 指令提交时写内存的目标地址（由 LSQ 完成回填，commit 写内存用）
    uint32_t store_address = 0;

    // 控制流指令（B/J/JALR）相关。
    // is_branch 覆盖全部控制流指令；JAL/JALR 恒 taken（branch_predicted=true）。
    bool is_branch = false;
    bool branch_taken = false;
    bool branch_predicted = false;
    uint32_t branch_target = 0;      // 实际目标（B: pc+imm；J/JALR: ALU 计算的真实目标）
    uint32_t predicted_target = 0;   // 发射时的预测目标（B 无预测目标恒 0；J: pc+imm 精确；JALR: pc+imm 猜测）
    uint32_t next_pc = 0;            // 不跳转顺序流时下一 PC（=pc+4）
};

struct CPUState;

// --- wire 方法（只读状态、不修改） ---

/// ROB 是否满（环形缓冲，(tail+1)%ROB_SIZE == head）
bool rob_is_full(const CPUState& s);

/// ROB 是否为空（head == tail）
bool rob_empty(const CPUState& s);

// --- port-helper 方法（读 cur、写 next） ---

/// 发射阶段：在 tail 处分配一个 ROB 条目
/// 返回分配的 rob 索引；调用方需先确认 !rob_is_full(c)
/// is_branch 覆盖 B/J/JALR；J/JALR 传 predicted_target=pc+imm（B 传 0）
int32_t rob_allocate(const CPUState& c, CPUState& n, const Command& cmd,
                     bool is_branch, bool branch_predicted,
                     uint32_t branch_target, uint32_t predicted_target,
                     uint32_t next_pc);

/// CDB 捕获阶段：按 cdb.rob_idx 匹配 ROB 条目标记 ready 并接收 value；
/// 分支指令回填 branch_taken / branch_target
void rob_cdb_capture(const CPUState& c, CPUState& n);

/// 提交阶段：head 处条目就绪则顺序提交
/// （写 regs / 写 memory / 更新 predictor / 检测终止哨兵，head++）
void rob_commit(const CPUState& c, CPUState& n);

}  // namespace riscv