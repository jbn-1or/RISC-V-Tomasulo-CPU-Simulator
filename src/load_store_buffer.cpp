#include "load_store_buffer.hpp"

#include <map>

#include "cpu_state.hpp"
#include "rob.hpp"

namespace riscv {

// funct3 → 字节宽度
inline uint32_t mem_width(uint32_t funct3) {
    switch (funct3) {
        case 0b000: return 1;
        case 0b001: return 2;
        case 0b010: return 4;
        case 0b100: return 1;
        case 0b101: return 2;
        default:    return 4;
    }
}

// 对 load 读回值做最终符号/无符号扩展
inline uint32_t ext_load(uint32_t raw, uint32_t funct3) {
    switch (funct3) {
        case 0b000:  // lb：符号扩展
            return static_cast<uint32_t>(
                static_cast<int32_t>(static_cast<int8_t>(raw & 0xFF)));
        case 0b001: {  // lh：符号扩展
            uint32_t v = raw & 0xFFFF;
            return static_cast<uint32_t>(
                static_cast<int32_t>(static_cast<int16_t>(v)));
        }
        case 0b010:  // lw：无需扩展
            return raw;
        case 0b100:  // lbu：无符号扩展
            return raw & 0xFF;
        case 0b101:  // lhu：无符号扩展
            return raw & 0xFFFF;
        default:
            return raw;
    }
}

inline uint8_t mem_read_byte(const std::map<uint32_t, uint8_t>& memory,
                             uint32_t addr) {
    auto it = memory.find(addr);
    return (it == memory.end()) ? 0 : it->second;
}

bool lsq_has_free(const CPUState& s) {
    for (int i = 0; i < LSQ_COUNT; ++i) {
        if (!s.lsq[i].busy) {
            return true;
        }
    }
    return false;
}

int32_t lsq_allocate(const CPUState& c, CPUState& n, const Command& cmd,
                     int32_t rob_idx) {
    // issue 阶段保证 cur 中存在空闲槽
    for (int i = 0; i < LSQ_COUNT; ++i) {
        if (!c.lsq[i].busy) {
            LsqEntry& slot = n.lsq[i];
            slot.busy = true;
            slot.cmd = cmd;
            slot.rs_tag = -1;
            slot.rob_idx = rob_idx;
            // 操作数：读 regs 取当前值，读 reg_status 取等待的 ROB 条目索引（-1 就绪）。
            slot.v1 = c.regs[cmd.rs1];
            slot.q1 = c.reg_status[cmd.rs1];
            slot.v2 = c.regs[cmd.rs2];
            slot.q2 = c.reg_status[cmd.rs2];
            slot.address = 0;
            slot.store_data = 0;
            slot.result = 0;
            slot.address_ready = false;
            slot.done = false;
            slot.cycle = 0;
            return i;
        }
    }
    return -1;
}

void lsq_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig) {
    if (!sig.valid) {
        return;
    }
    // 唤醒/捕获 tag 统一为 rob_idx
    for (int i = 0; i < LSQ_COUNT; ++i) {
        if (!c.lsq[i].busy) {
            continue;
        }
        LsqEntry& slot = n.lsq[i];
        if (c.lsq[i].q1 == sig.rob_idx) {
            slot.v1 = sig.value;
            slot.q1 = -1;
        }
        if (c.lsq[i].q2 == sig.rob_idx) {
            slot.v2 = sig.value;
            slot.q2 = -1;
        }
    }
}

void lsq_advance(const CPUState& c, CPUState& n,
                 std::map<uint32_t, uint8_t>& memory) {
    for (int i = 0; i < LSQ_COUNT; ++i) {
        const LsqEntry& cs = c.lsq[i];
        if (!cs.busy) {
            continue;
        }
        LsqEntry& slot = n.lsq[i];

        // 已完成的 load 等待 CDB 仲裁广播
        if (cs.done) {
            continue;
        }

        // 仅访存指令进入 LSQ
        const bool is_load = (cs.cmd.type == InstrType::I);

        // 1) 启动：load 仅需基址 q1；store 需基址 q1 与数据 q2 均就绪
        if (!cs.address_ready) {
            const bool can_start =
                is_load ? (cs.q1 == -1) : (cs.q1 == -1 && cs.q2 == -1);
            if (can_start) {
                slot.address = cs.v1 + static_cast<uint32_t>(cs.cmd.imm);
                if (!is_load) {
                    slot.store_data = cs.v2;
                }
                slot.address_ready = true;
                slot.cycle = 0;
            }
            continue;
        }

        // cycle+1，达 LSQ_PIPE_DEPTH = 3 即进入完成阶段
        slot.cycle = (cs.cycle < LSQ_PIPE_DEPTH) ? (cs.cycle + 1) : cs.cycle;
        if (slot.cycle < LSQ_PIPE_DEPTH) {
            continue;
        }

        // 3) 完成阶段
        if (is_load) {
            // 未回填（ready==false）→ 无法判定地址重叠 → stall（暂停流水）
            // 已回填且地址重叠 → 转发 store_data
            // 已回填且不重叠 → 安全读内存
            const uint32_t load_age = rob_age(c, cs.rob_idx);
            const uint32_t load_w = mem_width(cs.cmd.funct3);
            bool stalled = false;
            for (uint32_t k = 0; k < ROB_SIZE; ++k) {
                const uint32_t idx = (c.rob_head + k) % ROB_SIZE;
                if (idx == c.rob_tail) {
                    break;  // 已遍历完 ROB 内全部条目
                }
                const RobEntry& e = c.rob[idx];
                if (!e.busy || e.cmd.type != InstrType::S) {
                    continue;
                }
                if (rob_age(c, static_cast<int32_t>(idx)) >= load_age) {
                    continue;  // 不比本 load 早
                }
                if (!e.ready) {
                    stalled = true;  // 更早的 store 尚未回填 → 无法判定
                    break;
                }
            }
            if (stalled) {
                continue;
            }

            // load 的每个字节取覆盖它的 age 最大的已回填 store 的值，无 store 取内存。
            uint32_t raw = 0;
            for (uint32_t b = 0; b < load_w; ++b) {
                const uint32_t byte_addr = cs.address + b;
                uint8_t byte = mem_read_byte(memory, byte_addr);
                for (uint32_t k = 0; k < ROB_SIZE; ++k) {
                    const uint32_t idx = (c.rob_head + k) % ROB_SIZE;
                    if (idx == c.rob_tail) {
                        break;
                    }
                    const RobEntry& e = c.rob[idx];
                    if (!e.busy || e.cmd.type != InstrType::S || !e.ready) {
                        continue;
                    }
                    if (rob_age(c, static_cast<int32_t>(idx)) >= load_age) {
                        continue;
                    }
                    const uint32_t sw = mem_width(e.cmd.funct3);
                    if (byte_addr >= e.store_address &&
                        byte_addr < e.store_address + sw) {
                        const uint32_t shift =
                            (byte_addr - e.store_address) * 8;
                        byte = static_cast<uint8_t>((e.value >> shift) & 0xFF);
                    }
                }
                raw |= static_cast<uint32_t>(byte) << (b * 8);
            }

            // 直通 CDB：置 done=true、result=读回值（不写 ROB、不写关联 RS 槽的 result）
            slot.result = ext_load(raw, cs.cmd.funct3);
            slot.done = true;
        } else {
            // store：回填 ROB
            RobEntry& rob = n.rob[cs.rob_idx];
            rob.value = cs.store_data;
            rob.store_address = cs.address;
            rob.ready = true;
            // 释放关联 RS 槽
            if (cs.rs_tag >= 0 && cs.rs_tag < RS_SIZE) {
                n.rs[cs.rs_tag].busy = false;
            }
            // 释放 LSQ 槽
            slot.busy = false;
        }
    }
}

}  // namespace riscv

