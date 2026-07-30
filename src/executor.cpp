#include "executor.hpp"

namespace riscv {

bool Executor::execute(const Command& cmd, uint32_t regs[32],
                       std::map<uint32_t, uint8_t>& memory, uint32_t& pc) {
    // TODO: 根据 cmd.cmdname 分发到各指令实现
    (void)cmd;
    (void)regs;
    (void)memory;
    (void)pc;
    return false;
}

}  // namespace riscv