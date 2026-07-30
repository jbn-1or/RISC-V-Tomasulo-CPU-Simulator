#include "cpu.hpp"

#include "instruction_fetch.hpp"
#include "instruction_decoder.hpp"
#include "memory_loader.hpp"
#include "executor.hpp"

namespace riscv {

bool CPU::load_program(const std::string& path) {
    uint32_t max_addr;
    if (!load_program_from_data(path, memory, max_addr))
        return false;
    pc = 0;  // 从地址 0 开始取指
    return true;
}

uint8_t CPU::run() {
    // 主循环：取指 → 解码 → 执行 → 更新 PC
    return 0;
}

}  // namespace riscv