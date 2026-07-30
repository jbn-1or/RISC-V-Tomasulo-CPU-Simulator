#pragma once

#include "../include/command.hpp"

namespace riscv {

/// 解码一条 32 位 RISC-V 指令，返回填充好的 Command 结构体。
Command decode_instruction(uint32_t raw, uint32_t pc);

}  // namespace riscv