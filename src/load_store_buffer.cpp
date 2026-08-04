#include "load_store_buffer.hpp"

#include <map>

#include "cpu_state.hpp"
#include "rob.hpp"

namespace riscv {

// 访存指令粒度：funct3 → 字节宽度（load / store 共用同一编码）
// lb/sb=000(1B), lh/sh=001(2B), lw/sw=010(4B), lbu=100(1B), lhu=101(2B)
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

// 对 load 读回值做最终符号/无符号扩展（在逐字节覆盖转发后统一处理）
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
    for (int i = 0; i < LSU_COUNT; ++i) {
        if (!s.lsu_slots[i].busy) {
            return true;
        }
    }
    return false;
}

int32_t lsq_allocate(const CPUState& c, CPUState& n, const Command& cmd,
                     int32_t rob_idx) {
    // 调用方（issue 阶段）保证 cur 中存在空闲槽（!lsq_has_free(c)）
    for (int i = 0; i < LSU_COUNT; ++i) {
        if (!c.lsu_slots[i].busy) {
            LsuSlot& slot = n.lsu_slots[i];
            slot.busy = true;
            slot.cmd = cmd;
            slot.rs_tag = -1;  // 由 issue 阶段同周期回填（DESIGN.md §6 步骤 9）
            slot.rob_idx = rob_idx;
            // 操作数：读 regs 取当前值，读 reg_status 取等待的 ROB 条目索引（-1 就绪）。
            // 注意：q 是 ROB 条目索引而非 RS tag（见 DESIGN.md §2 顶部说明）
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
    // 正常调用路径不会到达（调用方已确认存在自由槽）
    return -1;
}

void lsq_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig) {
    if (!sig.valid) {
        return;
    }
    // 唤醒/捕获 tag 统一为 rob_idx（见 DESIGN.md §2 顶部说明）
    for (int i = 0; i < LSU_COUNT; ++i) {
        if (!c.lsu_slots[i].busy) {
            continue;
        }
        LsuSlot& slot = n.lsu_slots[i];
        if (c.lsu_slots[i].q1 == sig.rob_idx) {
            slot.v1 = sig.value;
            slot.q1 = -1;
        }
        if (c.lsu_slots[i].q2 == sig.rob_idx) {
            slot.v2 = sig.value;
            slot.q2 = -1;
        }
    }
}

void lsq_advance(const CPUState& c, CPUState& n,
                 std::map<uint32_t, uint8_t>& memory) {
    for (int i = 0; i < LSU_COUNT; ++i) {
        const LsuSlot& cs = c.lsu_slots[i];
        if (!cs.busy) {
            continue;
        }
        LsuSlot& slot = n.lsu_slots[i];

        // 已完成的 load 等待 CDB 仲裁广播（槽位释放由 CDB 阶段负责，见 §5.3）
        if (cs.done) {
            continue;
        }

        // load / store 区分：仅访存指令进入 LSQ（load 为 I 型，store 为 S 型）
        const bool is_load = (cs.cmd.type == InstrType::I);

        // 1) 启动：操作数就绪 → 计算地址、进入流水（cycle 置 0，启动周期不推进）
        //    load 仅需基址 q1；store 需基址 q1 与数据 q2 均就绪（见 §5.3）
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

        // 2) 推进：cycle+1，达 LSU_PIPE_DEPTH 即进入完成阶段
        //    （stall 时保持 cycle 于 DEPTH，下周期重查）
        slot.cycle = (cs.cycle < LSU_PIPE_DEPTH) ? (cs.cycle + 1) : cs.cycle;
        if (slot.cycle < LSU_PIPE_DEPTH) {
            continue;
        }

        // 3) 完成阶段
        if (is_load) {
            // ---- load：store-to-load 内存序检查（§5.3）----
            // 统一扫描 ROB（而非仅 LSQ）：比本 load 更早（age 更小）、尚未提交
            // （busy）的 store——
            //   · 未回填（ready==false）→ 无法判定地址重叠 → stall（暂停流水）
            //   · 已回填且地址重叠 → 转发 store_data
            //   · 已回填且不重叠 → 安全读内存
            // （已提交的 store 已写入 memory，与"不重叠"等价，无需检查）
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
                continue;  // 暂停流水：cycle 保持 LSU_PIPE_DEPTH，下周期重查
            }

            // 读内存 + 逐字节覆盖转发：load 的每个字节取覆盖它的、最年轻（age 最大、
            // 最后扫描到）的已回填 store 的值；无 store 覆盖则取内存。
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

            // 直通 CDB：置 done=true、result=读回值（不写 ROB、不写关联 RS 槽的
            // result；槽位由 CDB 仲裁选中后释放，见 §5.3）
            slot.result = ext_load(raw, cs.cmd.funct3);
            slot.done = true;
        } else {
            // ---- store：回填 ROB（LSQ 回填即 ROB 捕获，供 commit 按序提交）----
            RobEntry& rob = n.rob[cs.rob_idx];
            rob.value = cs.store_data;
            rob.store_address = cs.address;
            rob.ready = true;
            // 释放关联 RS 槽（store 不走 CDB，关联 RS 槽依赖此显式释放，见 §5.3）
            if (cs.rs_tag >= 0 && cs.rs_tag < RS_SIZE) {
                n.rs[cs.rs_tag].busy = false;
            }
            // 释放 LSQ 槽（下一周期对 issue 可见）
            slot.busy = false;
        }
    }
}

}  // namespace riscv

