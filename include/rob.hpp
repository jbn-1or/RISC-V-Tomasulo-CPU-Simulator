#pragma once

#include <cstdint>
#include "command.hpp"

namespace riscv {

constexpr int ROB_SIZE = 32;

struct RobEntry {
    bool busy = false;
    Command cmd;

    int32_t rs_tag = -1;

    bool ready = false;
    uint32_t value = 0;

    bool is_branch = false;
    bool branch_taken = false;
    bool branch_predicted = false;
    uint32_t branch_target = 0;
    uint32_t next_pc = 0;
};

}  // namespace riscv
