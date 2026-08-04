#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>

namespace riscv {

// 将 .data 格式输入从给定输入流加载到字节寻址的内存映射中。
// 适用于 std::cin（OJ 标准输入）或任何 std::istream。
bool load_program_from_stream(std::istream& input,
                              std::map<uint32_t, uint8_t>& memory,
                              uint32_t& max_addr);

}  // namespace riscv
