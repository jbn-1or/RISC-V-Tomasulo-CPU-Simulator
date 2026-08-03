#pragma once

#include <cstdint>
#include "command.hpp"

namespace riscv {

constexpr int ROB_SIZE = 16;

struct RobEntry {
    bool busy = false;
    Command cmd;

    int32_t rs_tag = -1;

    bool ready = false;
    uint32_t value = 0;

    // store 指令提交时写内存的目标地址（由 Execute/LSU 阶段填充）
    uint32_t store_address = 0;

    bool is_branch = false;
    bool branch_taken = false;
    bool branch_predicted = false;
    uint32_t branch_target = 0;
    uint32_t next_pc = 0;
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
int32_t rob_allocate(const CPUState& c, CPUState& n, const Command& cmd,
                     int32_t rs_tag, bool is_branch, bool branch_predicted,
                     uint32_t branch_target, uint32_t next_pc);

/// CDB 捕获阶段：rs_tag 匹配 cdb 的 ROB 条目标记 ready 并接收 value；
/// 分支指令回填 branch_taken / branch_target
void rob_cdb_capture(const CPUState& c, CPUState& n);

/// 提交阶段：head 处条目就绪则顺序提交
/// （写 regs / 写 memory / 更新 predictor / 检测终止哨兵，head++）
void rob_commit(const CPUState& c, CPUState& n);

}  // namespace riscv