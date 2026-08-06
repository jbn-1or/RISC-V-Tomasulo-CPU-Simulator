#include "alu.hpp"
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
const int MAXT = 500000000;
uint32_t regs[32] = {0};

// 从字节寻址内存中读取 val（小端序，最多 4 字节）
uint32_t read_mem(uint32_t addr, int bytes) {
    uint32_t val = 0;
    for (int i = 0; i < bytes; ++i) {
        auto it = memory.find(addr + i);
        if (it != memory.end())
            val |= static_cast<uint32_t>(it->second) << (8 * i);
    }
    return val;
}

// 向字节寻址内存写入 val（小端序，最多 4 字节）
void write_mem(uint32_t addr, uint32_t val, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        memory[addr + i] = static_cast<uint8_t>((val >> (8 * i)) & 0xFFu);
    }
}

uint8_t run() {
    uint32_t pc = 0;
    for (int i = 0; i < MAXT; ++i) {
        regs[0] = 0;

        uint32_t ins = riscv::fetch_instruction(memory, pc);

        // 终止条件：li a0, 255 = 0x0ff00513
        if (ins == 0x0ff00513u) {
            return static_cast<uint8_t>(regs[10] & 0xFFu);
        }

        Command cmd = riscv::decode_instruction(ins, pc);

        // 读取寄存器操作数
        uint32_t rs1_val = regs[cmd.rs1];
        uint32_t rs2_val = regs[cmd.rs2];

        // 通过 ALU 执行
        riscv::AluInput alu_in{cmd, rs1_val, rs2_val, pc};
        riscv::AluResult res = riscv::alu_execute(alu_in);

        const std::string& op = cmd.cmdname;

        // ecall / ebreak
        if (op == "ecall" || op == "ebreak") {
            pc += 4;
            continue;
        }

        // 分支指令（B 型）：不写回 rd，直接更新 PC
        if (res.is_branch && !res.is_jump) {
            pc = res.branch_taken ? res.next_pc : pc + 4;
            continue;
        }

        // 写回 rd（非 x0）
        if (cmd.writes_rd() && cmd.rd != 0) {
            if (op == "lb") {
                int32_t v = static_cast<int32_t>(
                    read_mem(res.value, 1) << 24) >> 24;
                regs[cmd.rd] = static_cast<uint32_t>(v);
            } else if (op == "lh") {
                int32_t v = static_cast<int32_t>(
                    read_mem(res.value, 2) << 16) >> 16;
                regs[cmd.rd] = static_cast<uint32_t>(v);
            } else if (op == "lw") {
                regs[cmd.rd] = read_mem(res.value, 4);
            } else if (op == "lbu") {
                regs[cmd.rd] = read_mem(res.value, 1);
            } else if (op == "lhu") {
                regs[cmd.rd] = read_mem(res.value, 2);
            } else {
                // R/I/U/J 型：直接使用 ALU 结果
                regs[cmd.rd] = res.value;
            }
        }

        // 存储指令
        if (cmd.type == InstrType::S) {
            if (op == "sb") {
                write_mem(res.value, rs2_val, 1);
            } else if (op == "sh") {
                write_mem(res.value, rs2_val, 2);
            } else if (op == "sw") {
                write_mem(res.value, rs2_val, 4);
            }
        }

        // 更新 PC
        if (res.is_jump) {
            pc = res.next_pc;
        } else {
            pc += 4;
        }
    }

    // 若超时返回 a0 的低 8 位
    return static_cast<uint8_t>(regs[10] & 0xFFu);
}

int main() {
    if (!riscv::load_program_from_stream(std::cin, memory, max_addr)) {
        std::cerr << "Failed to load program from stdin" << std::endl;
        return 1;
    }

    uint8_t result = run();
    std::cout << static_cast<int>(result) << std::endl;
    return result;
}