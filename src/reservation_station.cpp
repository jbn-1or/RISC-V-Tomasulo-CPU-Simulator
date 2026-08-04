#include "reservation_station.hpp"

#include "cpu_state.hpp"

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
    // 调用方（issue 阶段）保证 cur 中存在空闲槽（!rs_has_free(c)）
    for (int i = 0; i < RS_SIZE; ++i) {
        if (!c.rs[i].busy) {
            RsEntry& slot = n.rs[i];
            slot.busy = true;
            slot.cmd = cmd;
            // 操作数：读 regs 取当前值，读 reg_status 取等待的 ROB 条目索引（-1 就绪）。
            // 注意：q 是 ROB 条目索引而非 RS tag（见 DESIGN.md §2 顶部说明）；
            // 值就绪与否由 q 判定，v 在 q!=-1 时是占位（CDB 唤醒时被覆盖）。
            slot.v1 = c.regs[cmd.rs1];
            slot.q1 = c.reg_status[cmd.rs1];
            slot.v2 = c.regs[cmd.rs2];
            slot.q2 = c.reg_status[cmd.rs2];
            slot.rob_idx = rob_idx;
            slot.done = false;
            slot.result = 0;
            // 控制流执行结果由 ALU 执行时回填（Execute 阶段），分配时复位
            slot.is_branch = false;
            slot.branch_taken = false;
            slot.branch_target = 0;
            return rs_tag(i);
        }
    }
    // 正常调用路径不会到达（调用方已确认存在自由槽）
    return -1;
}

void rs_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig) {
    if (!sig.valid) {
        return;
    }
    // 唤醒/捕获 tag 统一为 rob_idx：所有槽位 q1/q2 匹配 sig.rob_idx → 捕获 value 并清 q
    // 读 cur、写 next（双缓冲：capture 对下一周期生效）
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

}  // namespace riscv

