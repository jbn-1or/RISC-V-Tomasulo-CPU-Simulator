#pragma once

#include <cstdint>
#include "command.hpp"

namespace riscv {

struct RsEntry {
    bool busy = false;
    Command cmd;

    // 操作数：值 + 等待的 ROB 条目索引（-1 表示就绪）。
    // 唤醒统一按 rob_idx 匹配 CDB（见 DESIGN.md §2 顶部说明）。
    uint32_t v1 = 0;
    uint32_t v2 = 0;
    int32_t q1 = -1;
    int32_t q2 = -1;

    int32_t rob_idx = -1;

    bool done = false;
    uint32_t result = 0;

    // 控制流指令（B/J/JALR）执行结果（ALU 写回 → 经 CDB 广播由 ROB 按 rob_idx 捕获）。
    // is_branch 覆盖 B/J/JALR；JAL/JALR 恒 taken。
    bool is_branch = false;
    bool branch_taken = false;
    uint32_t branch_target = 0;
};

constexpr int RS_SIZE = 8;

inline int rs_tag(int idx) { return idx; }

struct CPUState;
struct CdbSignal;

// --- wire 方法（只读状态、不修改） ---

/// 是否有空闲 RS 槽位（遍历 8 槽，busy == false 即空闲）
bool rs_has_free(const CPUState& s);

/// RS 条目就绪判定：busy && !done && q1==-1 && q2==-1（操作数齐备、尚未执行）
bool rs_entry_ready(const RsEntry& e);

// --- port 方法（读 cur、写 next） ---

/// 发射阶段：在任意空闲槽分配 RS 条目，返回 rs_tag（槽位索引 0..RS_SIZE-1）
/// 读 c.regs / c.reg_status 填充 v1/v2/q1/q2（q = reg_status[rs] = 等待的 ROB 条目索引，-1 就绪）
/// 调用方需先确认 rs_has_free(c)
int32_t rs_allocate(const CPUState& c, CPUState& n, const Command& cmd, int32_t rob_idx);

/// CDB 捕获阶段：所有槽位 q1/q2 匹配 sig.rob_idx → 捕获 value 并清 q
/// （唤醒/捕获 tag 统一为 rob_idx，见 DESIGN.md §2 顶部说明）
void rs_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig);

}  // namespace riscv