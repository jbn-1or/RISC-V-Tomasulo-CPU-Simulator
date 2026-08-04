#pragma once

#include <cstdint>

namespace riscv {

/// CDB 广播信号（每周期至多一条）
struct CdbSignal {
    bool valid = false;
    int32_t rs_tag = -1;       // 生产者 RS 槽位 tag（用于广播后释放）
    int32_t rob_idx = -1;      // tag：RS/LSQ 的 q1/q2 与 ROB 按此匹配
    uint32_t value = 0;        // 计算结果

    // B/J/JALR,J/JALR 恒 taken
    bool is_branch = false;
    bool branch_taken = false;
    uint32_t branch_target = 0;
};

struct CPUState;

/// 从 RS.done/LSQ load 完成项中选 ROB 程序序最早
CdbSignal cdb_arbitrate(const CPUState& c, CPUState& n);

/// CDB 广播:广播到 RS / LSQ / ROB
void cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig);

}  // namespace riscv
