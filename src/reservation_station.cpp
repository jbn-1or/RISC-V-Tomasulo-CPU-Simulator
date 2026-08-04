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

void rs_execute(const CPUState& c, CPUState& n) {
    // 单 ALU：从就绪非访存条目中选 ROB 程序序最早（age 最小）者执行（§5.1）。
    //   就绪 = busy && !done && q1==-1 && q2==-1（rs_entry_ready）。
    //   访存类（load/store）不进入 ALU——地址计算/数据在 LSQ 内独立完成（§5.1/§8），
    //   其 RS 槽由 LSQ 显式释放（load 经 CDB 广播、store 完成回填时释放）。
    int32_t best = -1;
    uint32_t best_age = ROB_SIZE;  // rob_age 对无效索引返回 ROB_SIZE（rob.cpp 防御）
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
        return;  // 无就绪非访存项（或全为访存项）→ 本周期 ALU 空闲
    }

    // 执行：ALU 为组合逻辑，v1/v2 就绪即可计算（读 cur、写 next）
    const RsEntry& e = c.rs[best];
    const AluResult res = alu_execute(AluInput{e.cmd, e.v1, e.v2, e.cmd.pc});
    RsEntry& slot = n.rs[best];
    slot.done = true;
    slot.result = res.value;
    // 控制流元数据（B/J/JALR 有效；J/JALR 恒 taken）写回，供 CDB 广播 → ROB 捕获
    slot.is_branch = res.is_branch;
    slot.branch_taken = res.branch_taken;
    slot.branch_target = res.next_pc;
}

}  // namespace riscv

