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

}  // namespace riscv
