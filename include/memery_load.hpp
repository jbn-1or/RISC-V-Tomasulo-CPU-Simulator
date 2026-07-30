#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace riscv {

// 将 .data 文件加载到字节寻址的内存映射中。
bool load_program_from_data(const std::string& path,
                            std::map<uint32_t, uint8_t>& memory,
                            uint32_t& max_addr);

}  // namespace riscv