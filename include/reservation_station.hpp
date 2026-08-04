#pragma once

#include <cstdint>
#include "command.hpp"

namespace riscv {

struct RsEntry {
    bool busy = false;
    Command cmd;

    // 操作数：值 + 等待的 ROB 条目索引（-1 表示就绪）。
    uint32_t v1 = 0;
    uint32_t v2 = 0;
    int32_t q1 = -1;
    int32_t q2 = -1;

    int32_t rob_idx = -1;

    bool done = false;
    uint32_t result = 0;

    // B/J/JALR执行结果（ALU 写回 → 经 CDB 广播由 ROB 按 rob_idx 捕获）。
    bool is_branch = false;
    bool branch_taken = false;
    uint32_t branch_target = 0;
};

constexpr int RS_SIZE = 8;

inline int rs_tag(int idx) { return idx; }

struct CPUState;
struct CdbSignal;

// 是否有空闲 RS 槽位
bool rs_has_free(const CPUState& s);

// RS 条目是否就绪
bool rs_entry_ready(const RsEntry& e);


// 发射阶段：分配 RS，返回 rs_tag
int32_t rs_allocate(const CPUState& c, CPUState& n, const Command& cmd, int32_t rob_idx);

// CDB 捕获：q1/q2 匹配 sig.rob_idx → 捕获 value 并清 q
void rs_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig);

// 执行阶段：从所有就绪且非访存的 RS 条目中，选 ROB 程序序最早
void rs_execute(const CPUState& c, CPUState& n);

}  // namespace riscv