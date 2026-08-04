#include "alu.hpp"

#include <cstdint>
#include <string>

namespace riscv {

AluResult alu_execute(const AluInput& in) {
    AluResult res;

    const Command& cmd = in.cmd;
    const std::string& op = cmd.cmdname;

    
    // R-type
    if (op == "add") {
        res.value = in.rs1_val + in.rs2_val;
    } else if (op == "sub") {
        res.value = in.rs1_val - in.rs2_val;
    } else if (op == "sll") {
        res.value = in.rs1_val << (in.rs2_val & 0x1Fu);
    } else if (op == "slt") {
        res.value = (static_cast<int32_t>(in.rs1_val) <
                     static_cast<int32_t>(in.rs2_val)) ? 1u : 0u;
    } else if (op == "sltu") {
        res.value = (in.rs1_val < in.rs2_val) ? 1u : 0u;
    } else if (op == "xor") {
        res.value = in.rs1_val ^ in.rs2_val;
    } else if (op == "srl") {
        res.value = in.rs1_val >> (in.rs2_val & 0x1Fu);
    } else if (op == "sra") {
        res.value = static_cast<uint32_t>(
            static_cast<int32_t>(in.rs1_val) >>
            (static_cast<int32_t>(in.rs2_val) & 0x1F));
    } else if (op == "or") {
        res.value = in.rs1_val | in.rs2_val;
    } else if (op == "and") {
        res.value = in.rs1_val & in.rs2_val;
    }
    
    // I-type
    else if (op == "addi") {
        res.value = in.rs1_val + static_cast<uint32_t>(cmd.imm);
    } else if (op == "slli") {
        uint32_t shamt = static_cast<uint32_t>(cmd.imm) & 0x1Fu;
        res.value = in.rs1_val << shamt;
    } else if (op == "srli") {
        uint32_t shamt = static_cast<uint32_t>(cmd.imm) & 0x1Fu;
        res.value = in.rs1_val >> shamt;
    } else if (op == "srai") {
        uint32_t shamt = static_cast<uint32_t>(cmd.imm) & 0x1Fu;
        res.value = static_cast<uint32_t>(
            static_cast<int32_t>(in.rs1_val) >> shamt);
    } else if (op == "slti") {
        res.value = (static_cast<int32_t>(in.rs1_val) < cmd.imm) ? 1u : 0u;
    } else if (op == "sltiu") {
        res.value = (in.rs1_val < static_cast<uint32_t>(cmd.imm)) ? 1u : 0u;
    } else if (op == "xori") {
        res.value = in.rs1_val ^ static_cast<uint32_t>(cmd.imm);
    } else if (op == "ori") {
        res.value = in.rs1_val | static_cast<uint32_t>(cmd.imm);
    } else if (op == "andi") {
        res.value = in.rs1_val & static_cast<uint32_t>(cmd.imm);
    }
    
    // I-type Load / S-type Store
    else if (op == "lb" || op == "lh" || op == "lw" ||
             op == "lbu" || op == "lhu" ||
             op == "sb" || op == "sh" || op == "sw") {
        res.value = in.rs1_val + static_cast<uint32_t>(cmd.imm);
    }
    
    // B-type
    else if (op == "beq") {
        res.is_branch = true;
        res.branch_taken = (in.rs1_val == in.rs2_val);
        res.next_pc = in.pc + static_cast<uint32_t>(cmd.imm);
    } else if (op == "bne") {
        res.is_branch = true;
        res.branch_taken = (in.rs1_val != in.rs2_val);
        res.next_pc = in.pc + static_cast<uint32_t>(cmd.imm);
    } else if (op == "blt") {
        res.is_branch = true;
        res.branch_taken = (static_cast<int32_t>(in.rs1_val) <
                            static_cast<int32_t>(in.rs2_val));
        res.next_pc = in.pc + static_cast<uint32_t>(cmd.imm);
    } else if (op == "bge") {
        res.is_branch = true;
        res.branch_taken = (static_cast<int32_t>(in.rs1_val) >=
                            static_cast<int32_t>(in.rs2_val));
        res.next_pc = in.pc + static_cast<uint32_t>(cmd.imm);
    } else if (op == "bltu") {
        res.is_branch = true;
        res.branch_taken = (in.rs1_val < in.rs2_val);
        res.next_pc = in.pc + static_cast<uint32_t>(cmd.imm);
    } else if (op == "bgeu") {
        res.is_branch = true;
        res.branch_taken = (in.rs1_val >= in.rs2_val);
        res.next_pc = in.pc + static_cast<uint32_t>(cmd.imm);
    }
    
    // U-type / J-type / JALR
    else if (op == "lui") {
        res.value = static_cast<uint32_t>(cmd.imm);
    } else if (op == "auipc") {
        res.value = in.pc + static_cast<uint32_t>(cmd.imm);
    } else if (op == "jal") {
        // 使经 CDB 广播时 ROB 能按 is_branch 捕获真实目标（next_pc）。
        res.is_branch = true;
        res.branch_taken = true;
        res.is_jump = true;
        res.value = in.pc + 4u;
        res.next_pc = in.pc + static_cast<uint32_t>(cmd.imm);
    } else if (op == "jalr") {
        // 真实目标（=rs1+imm 并对齐）写入 next_pc 供 ROB 捕获，
        // commit 阶段与 predicted_target（pc+imm）比较检测误预测。
        res.is_branch = true;
        res.branch_taken = true;
        res.is_jump = true;
        res.value = in.pc + 4u;
        uint32_t target = in.rs1_val + static_cast<uint32_t>(cmd.imm);
        res.next_pc = target & ~1u;
    }
    // ecall / ebreak / unknown
    return res;
}

}  // namespace riscv

