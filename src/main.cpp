#include <cstdint>
#include <iostream>

#include "cpu.hpp"

int main() {
    riscv::CPU cpu;

    if (!cpu.load_program(std::cin)) {
        std::cerr << "Failed to load program from stdin" << std::endl;
        return 1;
    }

    uint8_t result = cpu.run();
    std::cout << static_cast<int>(result) << std::endl;
    return 0;
}

// cd /home/jbn/PPCA/RISC-V-Tomasulo-CPU-Simulator && ./build/shuffle_test data/testcases/*.data
// cd /home/jbn/PPCA/RISC-V-Tomasulo-CPU-Simulator && cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build --target shuffle_test -j$(nproc) && ./build/shuffle_test data/testcases/*.data
// cd /home/jbn/PPCA/RISC-V-Tomasulo-CPU-Simulator && ./build/shuffle_test data/testcases/naive.data

