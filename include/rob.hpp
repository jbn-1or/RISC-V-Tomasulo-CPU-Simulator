#pragma once

#include <cstdint>
#include <map>
#include "cdb.hpp"
#include "command.hpp"

namespace riscv {

constexpr int ROB_SIZE = 16;

struct RobEntry {
    bool busy = false;
    Command cmd;

    bool ready = false;
    uint32_t value = 0;

    // store 指令提交时写内存的目标地址（由 LSQ 回填）
    uint32_t store_address = 0;

    // B/J/JALR
    bool is_branch = false;
    bool branch_taken = false;
    bool branch_predicted = false;
    uint32_t branch_target = 0;      // 实际目标（B: pc+imm；J/JALR: ALU 计算的真实目标）
    uint32_t predicted_target = 0;   // 发射时的预测目标（B 无预测目标恒 0；J: pc+imm 精确；JALR: pc+4 not-taken 预测）
    uint32_t next_pc = 0;            // 不跳转顺序流时下一 PC=pc+4
};

struct CPUState;

/// ROB 是否满 (tail+1)%ROB_SIZE == head
bool rob_is_full(const CPUState& s);

/// ROB 是否为空（head == tail）
bool rob_empty(const CPUState& s);

/// ROB age = (rob_idx - rob_head + ROB_SIZE) % ROB_SIZE，越小越靠前（程序序越早）
uint32_t rob_age(const CPUState& s, int32_t rob_idx);

/// 发射阶段：在 tail 处分配一个 ROB 条目
int32_t rob_allocate(const CPUState& c, CPUState& n, const Command& cmd,
                     bool is_branch, bool branch_predicted,
                     uint32_t branch_target, uint32_t predicted_target,
                     uint32_t next_pc);

/// CDB 捕获阶段：按 sig.rob_idx 匹配 ROB 条目标记 ready 并接收 value；
void rob_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig);

/// 提交阶段：head 处条目就绪则顺序提交
void rob_commit(const CPUState& c, CPUState& n,
                std::map<uint32_t, uint8_t>& memory);

}  // namespace riscv