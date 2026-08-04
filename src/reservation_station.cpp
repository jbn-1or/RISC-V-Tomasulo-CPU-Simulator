#include "reservation_station.hpp"

#include "alu.hpp"
#include "cpu_state.hpp"
#include "rob.hpp"

namespace riscv {

bool rs_has_free(const CPUState& s) {
    for (int i = 0; i < RS_SIZE; ++i) {
        if (!s.rs[i].busy) {
            return true;
        }
    }
    return false;
}

bool rs_entry_ready(const RsEntry& e) {
    return e.busy && !e.done && e.q1 == -1 && e.q2 == -1;
}

int32_t rs_allocate(const CPUState& c, CPUState& n, const Command& cmd, int32_t rob_idx) {
    for (int i = 0; i < RS_SIZE; ++i) {
        if (!c.rs[i].busy) {
            RsEntry& slot = n.rs[i];
            slot.busy = true;
            slot.cmd = cmd;
            // 读 regs 取当前值，读 reg_status 取等待的 ROB 条目索引
            slot.v1 = c.regs[cmd.rs1];
            slot.q1 = c.reg_status[cmd.rs1];
            slot.v2 = c.regs[cmd.rs2];
            slot.q2 = c.reg_status[cmd.rs2];
            slot.rob_idx = rob_idx;
            slot.done = false;
            slot.result = 0;
            // 由 ALU 执行时回填
            slot.is_branch = false;
            slot.branch_taken = false;
            slot.branch_target = 0;
            return rs_tag(i);
        }
    }
    return -1;
}

void rs_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig) {
    if (!sig.valid) {
        return;
    }
    // 所有槽位 q1/q2 匹配 sig.rob_idx → 捕获 value 并清 q
    for (int i = 0; i < RS_SIZE; ++i) {
        if (!c.rs[i].busy) {
            continue;
        }
        RsEntry& slot = n.rs[i];
        if (c.rs[i].q1 == sig.rob_idx) {
            slot.v1 = sig.value;
            slot.q1 = -1;
        }
        if (c.rs[i].q2 == sig.rob_idx) {
            slot.v2 = sig.value;
            slot.q2 = -1;
        }
    }
}

void rs_execute(const CPUState& c, CPUState& n) {
    int32_t best = -1;
    uint32_t best_age = ROB_SIZE;  // 无效索引返回 ROB_SIZE
    for (int i = 0; i < RS_SIZE; ++i) {
        const RsEntry& e = c.rs[i];
        if (!rs_entry_ready(e) || e.cmd.is_mem()) {
            continue;
        }
        const uint32_t age = rob_age(c, e.rob_idx);
        if (age < best_age) {
            best_age = age;
            best = i;
        }
    }
    if (best < 0) {
        return;
    }

    // 执行：v1/v2 就绪即可计算
    const RsEntry& e = c.rs[best];
    const AluResult res = alu_execute(AluInput{e.cmd, e.v1, e.v2, e.cmd.pc});
    RsEntry& slot = n.rs[best];
    slot.done = true;
    slot.result = res.value;
    // B/J/JALR
    slot.is_branch = res.is_branch;
    slot.branch_taken = res.branch_taken;
    slot.branch_target = res.next_pc;
}

}  // namespace riscv

