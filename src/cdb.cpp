#include "cdb.hpp"

#include "cpu_state.hpp"
#include "load_store_buffer.hpp"
#include "reservation_station.hpp"
#include "rob.hpp"

namespace riscv {

CdbSignal cdb_arbitrate(const CPUState& c, CPUState& n) {
    // 组合线：只读 cur、写 next（生产者槽位释放）。返回本周期广播信号。
    // 仲裁源 = RS.done（算术/分支/JAL/JALR）∪ LSQ load 完成项（done=true，直通 CDB）。
    // 选 ROB 程序序最早（age 最小）者——环形 ROB 年龄约定见 §5.3，
    // 不可用裸 rob_idx 比较（回绕时数值最小者未必最靠前）。
    CdbSignal sig;  // valid = false 默认；无候选时原样返回

    uint32_t best_age = ROB_SIZE;  // 比任何合法 age（0..ROB_SIZE-1）都大
    int32_t best_rob = -1;         // 选中候选的 rob_idx
    bool best_is_load = false;     // 候选来源：false = RS，true = LSQ load
    int32_t best_idx = -1;         // 选中的槽位索引（RS 槽 或 LSQ 槽）

    // 1) RS 候选
    for (int i = 0; i < RS_SIZE; ++i) {
        const RsEntry& e = c.rs[i];
        if (!e.busy || !e.done) {
            continue;
        }
        const uint32_t age = rob_age(c, e.rob_idx);
        if (age < best_age) {
            best_age = age;
            best_rob = e.rob_idx;
            best_is_load = false;
            best_idx = i;
        }
    }

    // 2) LSQ load 候选（直通 CDB）
    for (int i = 0; i < LSU_COUNT; ++i) {
        const LsuSlot& s = c.lsu_slots[i];
        if (!s.busy || !s.done) {
            continue;
        }
        const uint32_t age = rob_age(c, s.rob_idx);
        if (age < best_age) {
            best_age = age;
            best_rob = s.rob_idx;
            best_is_load = true;
            best_idx = i;
        }
    }

    if (best_idx < 0) {
        return sig;  // 无候选：invalid
    }

    sig.valid = true;
    if (best_is_load) {
        // load 直通：value + rob_idx（唤醒 tag）+ 关联 rs_tag（释放用）
        const LsuSlot& s = c.lsu_slots[best_idx];
        sig.rs_tag = s.rs_tag;
        sig.rob_idx = s.rob_idx;
        sig.value = s.result;
        // 释放 load 生产者：关联 RS 槽 + LSQ 槽（下一周期才对 issue 可见）
        if (s.rs_tag >= 0 && s.rs_tag < RS_SIZE) {
            n.rs[s.rs_tag].busy = false;
        }
        n.lsu_slots[best_idx].busy = false;
    } else {
        // 算术 / 分支 / JAL / JALR：value + rob_idx（唤醒 tag）+ 自身 rs_tag（释放用）
        const RsEntry& e = c.rs[best_idx];
        sig.rs_tag = best_idx;
        sig.rob_idx = e.rob_idx;
        sig.value = e.result;
        // 分支 / JAL / JALR 元数据（供 ROB 捕获）
        sig.is_branch = e.is_branch;
        sig.branch_taken = e.branch_taken;
        sig.branch_target = e.branch_target;
        // 释放算术 / 分支生产者：自身 RS 槽
        n.rs[best_idx].busy = false;
    }

    return sig;
}

void cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig) {
    // 三端捕获：RS / LSQ 按 sig.rob_idx 匹配 q1/q2 唤醒；ROB 按 rob_idx 捕获。
    // 各自只读 cur、可交换（见 DESIGN.md §5.4/§7）
    rs_cdb_capture(c, n, sig);
    lsq_cdb_capture(c, n, sig);
    rob_cdb_capture(c, n, sig);
}

}  // namespace riscv

