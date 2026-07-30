#include <cstdint>
#include <iostream>
#include <string>

#include "cpu.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <program.data>" << std::endl;
        return 1;
    }

    std::string path = argv[1];
    riscv::CPU cpu;

    if (!cpu.load_program(path)) {
        std::cerr << "Failed to load program: " << path << std::endl;
        return 1;
    }

    uint8_t result = cpu.run();
    std::cout << static_cast<int>(result) << std::endl;
    return result;
}