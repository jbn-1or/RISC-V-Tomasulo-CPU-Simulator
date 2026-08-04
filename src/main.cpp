#include <cstdint>
#include <iostream>

#include "cpu.hpp"

int main() {
    riscv::CPU cpu;

    // 从标准输入读入 .data 格式机器指令（与 issue.pdf 要求一致）
    if (!cpu.load_program(std::cin)) {
        std::cerr << "Failed to load program from stdin" << std::endl;
        return 1;
    }

    uint8_t result = cpu.run();
    std::cout << static_cast<int>(result) << std::endl;
    return result;
}
