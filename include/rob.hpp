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

/// 环形 ROB 年龄：age = (rob_idx - rob_head + ROB_SIZE) % ROB_SIZE
/// age 越小越靠前（程序序越早）。用于分支边界判定 / store-to-load 内存序 /
/// CDB 仲裁等所有需要"程序序先后"比较的场合（见 DESIGN.md §5.3，不可用裸 rob_idx）
uint32_t rob_age(const CPUState& s, int32_t rob_idx);

// --- port-helper 方法（读 cur、写 next） ---

/// 发射阶段：在 tail 处分配一个 ROB 条目
/// 返回分配的 rob 索引；调用方需先确认 !rob_is_full(c)
/// is_branch 覆盖 B/J/JALR；J/JALR 传 predicted_target=pc+imm（B 传 0）
int32_t rob_allocate(const CPUState& c, CPUState& n, const Command& cmd,
                     bool is_branch, bool branch_predicted,
                     uint32_t branch_target, uint32_t predicted_target,
                     uint32_t next_pc);

/// CDB 捕获阶段：按 sig.rob_idx 匹配 ROB 条目标记 ready 并接收 value；
/// 分支指令回填 branch_taken / branch_target
void rob_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig);

/// 提交阶段：head 处条目就绪则顺序提交
/// （写 regs / 写 memory / 更新 predictor / 检测终止哨兵，head++）
/// memory 为 CPU 类持有的单实例内存（不在 CPUState 内，见 cpu_state.hpp）；
/// store 提交时直接写入 memory（按序提交，无并发写冲突）
void rob_commit(const CPUState& c, CPUState& n,
                std::map<uint32_t, uint8_t>& memory);

}  // namespace riscv