#pragma once

#include <cstdint>

namespace riscv {

/// CDB 广播信号（每周期至多一条）
struct CdbSignal {
    bool valid = false;
    int32_t rs_tag = -1;       // 生产者 RS 槽位 tag（仅用于广播后释放该槽位）
    int32_t rob_idx = -1;      // 唤醒 / 捕获 tag：RS/LSQ 的 q1/q2 与 ROB 均按此匹配
    uint32_t value = 0;        // 计算结果

    // 控制流指令相关（B/J/JALR 有效；J/JALR 恒 taken）
    bool is_branch = false;
    bool branch_taken = false;
    uint32_t branch_target = 0;
};

}  // namespace riscv
