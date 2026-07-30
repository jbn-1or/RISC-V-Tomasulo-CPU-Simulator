#pragma once

#include <cstdint>
#include <map>

namespace riscv {

// 从字节寻址的内存映射中，按小端序读取 PC 处的 32 位 RISC-V 指令。
// 若 PC 地址范围内的任何字节不存在于 memory 中，返回 0。
uint32_t fetch_instruction(const std::map<uint32_t, uint8_t>& memory,
                           uint32_t pc);

}  // namespace riscv