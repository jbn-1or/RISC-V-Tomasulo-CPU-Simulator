#pragma once

#include <cstdint>
#include "command.hpp"

namespace riscv {

struct RsEntry {
    bool busy = false;
    Command cmd;

    uint32_t v1 = 0;
    uint32_t v2 = 0;
    int32_t q1 = -1;
    int32_t q2 = -1;

    int32_t rob_idx = -1;

    bool done = false;
    uint32_t result = 0;
};

constexpr int RS_SIZE = 8;

inline int rs_tag(int idx) { return idx; }

}  // namespace riscv
