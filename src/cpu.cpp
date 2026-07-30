#include "cpu.hpp"

#include "instruction_fetch.hpp"
#include "instruction_decoder.hpp"
#include "memory_loader.hpp"
#include "executor.hpp"

namespace riscv {

bool CPU::load_program(const std::string& path) {
    // TODO: 委托 memory_loader 加载 .data 文件
    (void)path;
    return false;
}

uint8_t CPU::run() {
    // TODO: 主循环：取指 → 解码 → 执行 → 更新 PC
    return 0;
}

}  // namespace riscv