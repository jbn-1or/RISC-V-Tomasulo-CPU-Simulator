#pragma once

#include <cstdint>
#include <map>

namespace riscv {

// 内存按小端序读取 PC 处的 32 位指令。若不存在于 memory 返回 0。
uint32_t fetch_instruction(const std::map<uint32_t, uint8_t>& memory,
                           uint32_t pc);

}  // namespace riscv