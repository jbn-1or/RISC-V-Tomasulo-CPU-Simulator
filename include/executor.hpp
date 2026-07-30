#pragma once

#include <cstdint>
#include <map>

#include "command.hpp"

namespace riscv {

/// 执行器：负责根据已解码的 Command 执行 RISC-V 指令的实际语义。
class Executor {
public:
    /// cmd:指令 regs:寄存器 memory:字节寻址的内存映射 
    /// true 表示继续执行下一条指令，false 表示遇到停止指令
    bool execute(const Command& cmd, uint32_t regs[32],
                 std::map<uint32_t, uint8_t>& memory, uint32_t& pc);
};

}  // namespace riscv