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

constexpr int RS_ALU_COUNT = 3;
constexpr int RS_BR_COUNT  = 1;
constexpr int RS_MEM_COUNT = 2;

inline int get_rs_tag(int group, int idx) {
    if (group == 0) return idx;                         // ALU: 0..2
    if (group == 1) return RS_ALU_COUNT + idx;          // BR: 3
    return RS_ALU_COUNT + RS_BR_COUNT + idx;            // MEM: 4..5
}

}  // namespace riscv