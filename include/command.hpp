#pragma once

#include <cstdint>
#include <string>

enum class InstrType {
    R,
    I,
    S,
    B,
    U,
    J,
    Unknown
};

struct Command {
    uint32_t pc = 0;          // 当前指令所在地址
    uint32_t raw = 0;         // 原始 32 位机器码
    InstrType type = InstrType::Unknown;

    uint32_t opcode = 0;      // 低 7 位
    uint32_t rd = 0;          // 目标寄存器
    uint32_t rs1 = 0;         // 源寄存器 1
    uint32_t rs2 = 0;         // 源寄存器 2
    uint32_t funct3 = 0;      // funct3
    uint32_t funct7 = 0;      // funct7

    int32_t imm = 0;          // 立即数，已经做完符号扩展后的值
    std::string cmdname;     // 指令名，如 add, addi, lw

    bool is_branch() const { return type == InstrType::B; }
    bool is_jump() const { return type == InstrType::J; }
    bool writes_rd() const {
        return type != InstrType::S && type != InstrType::B && type != InstrType::Unknown;
    }
};
