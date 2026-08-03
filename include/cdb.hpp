#pragma once

#include <cstdint>

namespace riscv {

/// CDB 广播信号（每周期至多一条）
struct CdbSignal {
    bool valid = false;
    int32_t rs_tag = -1;       // 生产者 RS 槽位 tag
    int32_t rob_idx = -1;      // 对应的 ROB 项索引
    uint32_t value = 0;        // 计算结果

    // 分支相关（仅分支指令有效）
    bool is_branch = false;
    bool branch_taken = false;
    uint32_t branch_target = 0;
};

}  // namespace riscv
