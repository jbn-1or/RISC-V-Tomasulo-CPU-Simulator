#include "../include/instruction_fetch.hpp"

namespace riscv {

uint32_t fetch_instruction(const std::map<uint32_t, uint8_t>& memory,
                           uint32_t pc) {
    uint32_t instruction = 0;
    for (int i = 0; i < 4; ++i) {
        auto it = memory.find(pc + i);
        if (it == memory.end()) {
            return 0;
        }
        instruction |= static_cast<uint32_t>(it->second) << (8 * i);
    }
    return instruction;
}

}  // namespace riscv