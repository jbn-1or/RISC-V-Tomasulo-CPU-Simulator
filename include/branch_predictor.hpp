#pragma once

#include <cstdint>

namespace riscv {

constexpr int BP_TABLE_SIZE = 64;

struct BranchPredictor {
    uint8_t table[BP_TABLE_SIZE] = {0};
    uint64_t total_branches = 0;
    uint64_t correct_branches = 0;

    bool predict(uint32_t pc) const {
        int idx = (pc >> 2) & (BP_TABLE_SIZE - 1);
        return (table[idx] >> 1) != 0;
    }

    void update(uint32_t pc, bool taken) {
        int idx = (pc >> 2) & (BP_TABLE_SIZE - 1);
        total_branches++;
        if (predict(pc) == taken) correct_branches++;
        if (taken) { if (table[idx] < 3) table[idx]++; }
        else       { if (table[idx] > 0) table[idx]--; }
    }
};

}  // namespace riscv
