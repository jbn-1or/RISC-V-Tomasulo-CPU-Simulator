#pragma once

#include <cstdint>
#include "command.hpp"

namespace riscv {

// ALU input
struct AluInput {
    const Command& cmd;   // 解码后的指令
    uint32_t rs1_val;     // rs1 的实际值
    uint32_t rs2_val;     // rs2 的实际值
    uint32_t pc;          // 当前指令的 PC
};

// ALU output
struct AluResult {
    uint32_t value = 0;        // 数据通路输出

    uint32_t next_pc = 0;      // 控制通路输出（B: 分支目标；J/JALR: 跳转目标）

    bool branch_taken = false; // 分支条件是否成立（J/JALR 恒 true）
    bool is_branch = false;    // 是否为控制流指令（B / JAL / JALR）
    bool is_jump = false;      // 是否为 JAL / JALR
};

// ALU function
AluResult alu_execute(const AluInput& in);

}  // namespace riscv
