#pragma once

#include <cstdint>
#include "command.hpp"

namespace riscv {

constexpr int LSU_PIPE_DEPTH = 3;

struct LsuSlot {
    bool busy = false;
    Command cmd;
    uint32_t address = 0;
    uint32_t store_data = 0;
    int32_t rs_tag = -1;
    int32_t rob_idx = -1;
    int cycle = 0;  // 0=地址, 1=访存, 2=完成
};

constexpr int LSU_COUNT = 4;

}  // namespace riscv