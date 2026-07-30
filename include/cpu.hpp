#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace riscv {

/// CPU 核心：持有寄存器文件、内存、PC，并串联主执行循环。
class CPU {
public:
    uint32_t regs[32] = {0};           // 寄存器文件（x0 恒为 0）
    std::map<uint32_t, uint8_t> memory; // 字节寻址内存
    uint32_t pc = 0;                    // 程序计数器

    /// 从 .data 文件加载程序到内存。
    /// @return true 表示加载成功
    bool load_program(const std::string& path);

    /// 运行主循环：取指 → 解码 → 执行 → 更新 PC，直到遇见停止指令。
    /// @return a0 的低 8 位作为程序返回值
    uint8_t run();
};

}  // namespace riscv