#include "command.hpp"
#include "instruction_decoder.hpp"
#include "instruction_fetch.hpp"
#include "memory_loader.hpp"
#include <cstdint>
#include <iostream>
#include <map>
#include <string>

std::map<uint32_t, uint8_t> memory;
uint32_t max_addr = UINT32_MAX;
const int MAXT = 1e6;
Command cmd;
uint32_t regs[32] = {0};

uint8_t run() {
    int i = 0;
    uint32_t pc = 0;
    while (i++ < MAXT) {
        uint32_t ins = riscv::fetch_instruction(memory, pc);
        cmd = riscv::decode_instruction(ins, pc);
    }
    
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <program.data>" << std::endl;
        return 1;
    }

    std::string path = argv[1];

    if (riscv::load_program_from_data(path, memory, max_addr)) {
        std::cerr << "Failed to load program: " << path << std::endl;
        return 1;
    }

    uint8_t result = run();
    std::cout << static_cast<int>(result) << std::endl;
    return result;
}