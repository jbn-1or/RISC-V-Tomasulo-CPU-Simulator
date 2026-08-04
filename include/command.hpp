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

    /// 访存类判定：load（lb/lh/lw/lbu/lhu）
    bool is_load() const {
        return cmdname == "lb" || cmdname == "lh" || cmdname == "lw" ||
               cmdname == "lbu" || cmdname == "lhu";
    }
    /// 访存类判定：store（sb/sh/sw）
    bool is_store() const {
        return cmdname == "sb" || cmdname == "sh" || cmdname == "sw";
    }
    /// 是否为访存类指令（load / store）
    // Execute 阶段据此把访存指令排除在 ALU 之外（由 LSQ 独立执行，见 DESIGN.md §5.1/§8）
    bool is_mem() const {
        return is_load() || is_store();
    }
};
