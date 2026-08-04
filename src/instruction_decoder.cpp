#include "instruction_decoder.hpp"

#include <cstdint>
#include <string>

namespace riscv {

inline uint32_t extract_opcode(uint32_t raw) {
    return raw & 0x7F;                     // bits [6:0]
}

inline uint32_t extract_rd(uint32_t raw) {
    return (raw >> 7) & 0x1F;              // bits [11:7]
}

inline uint32_t extract_funct3(uint32_t raw) {
    return (raw >> 12) & 0x7;              // bits [14:12]
}

inline uint32_t extract_rs1(uint32_t raw) {
    return (raw >> 15) & 0x1F;             // bits [19:15]
}

inline uint32_t extract_rs2(uint32_t raw) {
    return (raw >> 20) & 0x1F;             // bits [24:20]
}

inline uint32_t extract_funct7(uint32_t raw) {
    return (raw >> 25) & 0x7F;             // bits [31:25]
}

// I 型立即数: bits [31:20]，12 位符号扩展到 32 位
int32_t extract_imm_i(uint32_t raw) {
    uint32_t imm = (raw >> 20) & 0xFFF;
    // 符号扩展：算术左移 20 位再算术右移 20 位
    return static_cast<int32_t>(imm << 20) >> 20;
}

// I* 型移位量: bits [24:20]，5 位无符号（不需要符号扩展）
uint32_t extract_shamt(uint32_t raw) {
    return (raw >> 20) & 0x1F;
}

// S 型立即数: imm[11:5]=bits[31:25], imm[4:0]=bits[11:7]，12 位符号扩展
int32_t extract_imm_s(uint32_t raw) {
    uint32_t imm = ((raw >> 25) << 5) | ((raw >> 7) & 0x1F);
    return static_cast<int32_t>(imm << 20) >> 20;
}

// B 型立即数: 13 位立即数（bit 0 恒为 0，省略不编码），符号扩展
// imm = {raw[31], raw[7], raw[30:25], raw[11:8], 0}
int32_t extract_imm_b(uint32_t raw) {
    uint32_t imm =
        ((raw >> 31) << 12)              // imm[12]
        | (((raw >> 7) & 0x1) << 11)      // imm[11]
        | (((raw >> 25) & 0x3F) << 5)     // imm[10:5]
        | (((raw >> 8) & 0xF) << 1);      // imm[4:1]
    // 符号扩展：bit 12 是符号位，扩展到 32 位
    return static_cast<int32_t>(imm << 19) >> 19;
}

// U 型立即数: bits [31:12]，低 12 位补 0
int32_t extract_imm_u(uint32_t raw) {
    return static_cast<int32_t>(raw & 0xFFFFF000);
}

// J 型立即数: 21 位立即数（bit 0 恒为 0），符号扩展
// imm = {raw[31], raw[19:12], raw[20], raw[30:21], 0}
int32_t extract_imm_j(uint32_t raw) {
    uint32_t imm =
        ((raw >> 31) << 20)               // imm[20]
        | (((raw >> 12) & 0xFF) << 12)    // imm[19:12]
        | (((raw >> 20) & 0x1) << 11)     // imm[11]
        | (((raw >> 21) & 0x3FF) << 1);   // imm[10:1]
    // 符号扩展：bit 20 是符号位
    return static_cast<int32_t>(imm << 11) >> 11;
}

InstrType determine_type(uint32_t opcode) {
    switch (opcode) {
        case 0b0110011: return InstrType::R;
        case 0b0010011: return InstrType::I;
        case 0b0000011: return InstrType::I;
        case 0b1100111: return InstrType::I;
        case 0b1110011: return InstrType::I;
        case 0b0100011: return InstrType::S;
        case 0b1100011: return InstrType::B;
        case 0b0110111: return InstrType::U; 
        case 0b0010111: return InstrType::U;
        case 0b1101111: return InstrType::J;
        default: return InstrType::Unknown;
    }
}

const char* get_cmdname(uint32_t opcode, uint32_t funct3, uint32_t funct7,
                          uint32_t raw) {
    switch (opcode) {
        // R 型
        case 0b0110011:
            switch (funct3) {
                case 0b000:
                    switch (funct7) {
                        case 0b0000000: return "add";
                        case 0b0100000: return "sub";
                        default: return "unknown_r";
                    }
                case 0b001: return "sll";
                case 0b010: return "slt";
                case 0b011: return "sltu";
                case 0b100: return "xor";
                case 0b101:
                    switch (funct7) {
                        case 0b0000000: return "srl";
                        case 0b0100000: return "sra";
                        default: return "unknown_r";
                    }
                case 0b110: return "or";
                case 0b111: return "and";
                default: return "unknown_r";
            }

        // I 型
        case 0b0010011:
            switch (funct3) {
                case 0b000: return "addi";
                case 0b001: return "slli";
                case 0b010: return "slti";
                case 0b011: return "sltiu";
                case 0b100: return "xori";
                case 0b101:
                    switch (funct7) {
                        case 0b0000000: return "srli";
                        case 0b0100000: return "srai";
                        default: return "unknown_i";
                    }
                case 0b110: return "ori";
                case 0b111: return "andi";
                default: return "unknown_i";
            }

        // l 型 — load
        case 0b0000011:
            switch (funct3) {
                case 0b000: return "lb";
                case 0b001: return "lh";
                case 0b010: return "lw";
                case 0b100: return "lbu";
                case 0b101: return "lhu";
                default: return "unknown_load";
            }

        // I 型 — jalr
        case 0b1100111:
            return "jalr";

        // I 型 — ecall / ebreak
        case 0b1110011:
            if (funct3 == 0b000) {
                // RISC-V 规范: ecall = 0x00000073 → raw[20]=0；ebreak = 0x00100073 → raw[20]=1
                if (raw & (1u << 20))
                    return "ebreak";
                else
                    return "ecall";
            }
            return "unknown_env";

        // S 型
        case 0b0100011:
            switch (funct3) {
                case 0b000: return "sb";
                case 0b001: return "sh";
                case 0b010: return "sw";
                default: return "unknown_s";
            }

        // B 型
        case 0b1100011:
            switch (funct3) {
                case 0b000: return "beq";
                case 0b001: return "bne";
                case 0b100: return "blt";
                case 0b101: return "bge";
                case 0b110: return "bltu";
                case 0b111: return "bgeu";
                default: return "unknown_b";
            }

        // U 型
        case 0b0110111: return "lui";
        case 0b0010111: return "auipc";

        // J 型
        case 0b1101111: return "jal";

        default: return "unknown";
    }
}

Command decode_instruction(uint32_t raw, uint32_t pc) {
    Command cmd;
    cmd.pc = pc;
    cmd.raw = raw;

    uint32_t opcode = extract_opcode(raw);
    uint32_t funct3 = extract_funct3(raw);

    cmd.opcode = opcode;
    cmd.funct3 = funct3;
    cmd.type = determine_type(opcode);

    // 只有 R 型和 I* 型移位指令才有 funct7，其余类型 bits[31:25] 是立即数的一部分
    if (cmd.type == InstrType::R ||
        (opcode == 0b0010011 && (funct3 == 0b001 || funct3 == 0b101))) {
        cmd.funct7 = extract_funct7(raw);
    } else {
        cmd.funct7 = 0;
    }

    // 提取通用寄存器字段
    cmd.rd  = extract_rd(raw);
    cmd.rs1 = extract_rs1(raw);
    cmd.rs2 = extract_rs2(raw);

    // 提取立即数（按类型）
    switch (cmd.type) {
        case InstrType::R:
            cmd.imm = 0;
            break;

        case InstrType::I:
            // 移位指令 (opcode 0010011, funct3=001 或 101) 使用无符号 shamt
            if (opcode == 0b0010011 && (funct3 == 0b001 || funct3 == 0b101)) {
                cmd.imm = static_cast<int32_t>(extract_shamt(raw));
            } else {
                cmd.imm = extract_imm_i(raw);
            }
            break;

        case InstrType::S:
            cmd.imm = extract_imm_s(raw);
            break;

        case InstrType::B:
            cmd.imm = extract_imm_b(raw);
            break;

        case InstrType::U:
            cmd.imm = extract_imm_u(raw);
            break;

        case InstrType::J:
            cmd.imm = extract_imm_j(raw);
            break;

        case InstrType::Unknown:
            cmd.imm = 0;
            break;
    }

    cmd.cmdname = get_cmdname(opcode, funct3, cmd.funct7, raw);

    return cmd;
}

}  // namespace riscv