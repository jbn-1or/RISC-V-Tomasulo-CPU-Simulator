#include "cpu.hpp"

#include <utility>

#include "cdb.hpp"
#include "instruction_fetch.hpp"
#include "instruction_decoder.hpp"
#include "load_store_buffer.hpp"
#include "memory_loader.hpp"
#include "reservation_station.hpp"
#include "rob.hpp"

namespace riscv {

CPU::CPU() {
    next = cur;
}

bool CPU::load_program(std::istream& input) {
    uint32_t max_addr;
    if (!load_program_from_stream(input, memory, max_addr))
        return false;
    cur.pc = 0;
    return true;
}

// 是否有空闲 RS 槽位
bool CPU::rs_has_free(const CPUState& s) const {
    return ::riscv::rs_has_free(s);
}

// ROB 是否满
bool CPU::rob_is_full(const CPUState& s) const {
    return ::riscv::rob_is_full(s);
}

// LSQ 是否有空
bool CPU::lsq_has_free(const CPUState& s) const {
    return ::riscv::lsq_has_free(s);
}

// ROB 是否为空
bool CPU::rob_empty(const CPUState& s) const {
    return ::riscv::rob_empty(s);
}

// 发射一条指令（取指+解码+分配 RS/ROB/LSQ）
// reg_status 重命名与 PC 更新不直接写 next，填增量 d 交由 merge_conflict 收口。
void CPU::issue(const CPUState& c, CPUState& n, ConflictDeltas& d) {
    // 1. 停机后不再发射
    if (c.stopped) {
        return;
    }

    // 2. 取指
    const uint32_t ins = fetch_instruction(memory, c.pc);

    // 3. 解码；0x0ff00513 作为普通指令发射，由 commit 阶段检测停机（见 DESIGN.md §4.1）
    const Command cmd = decode_instruction(ins, c.pc);

    const bool is_load = cmd.is_load();
    const bool is_store = cmd.is_store();
    const bool is_mem = is_load || is_store;

    // 4. 资源检查
    if (rob_is_full(c) || !rs_has_free(c) || (is_mem && !lsq_has_free(c))) {
        return;
    }

    // 5. 分支预测
    bool predicted_taken = false;   // B: 预测器结果；J/JALR: 恒 true
    uint32_t predicted_target = 0;  // J: pc+imm 精确；JALR: pc+4 猜测；B 无预测目标恒 0
    uint32_t next_pc = c.pc + 4;

    const bool is_control = (cmd.type == InstrType::B) ||
                            (cmd.type == InstrType::J) ||
                            (cmd.type == InstrType::I && cmd.cmdname == "jalr");

    if (cmd.type == InstrType::B) {
        // B 型：2-bit 预测器决定预测方向
        predicted_taken = c.predictor.predict(cmd.pc);
        next_pc = predicted_taken ? (cmd.pc + static_cast<uint32_t>(cmd.imm))
                                  : (cmd.pc + 4);
    } else if (cmd.type == InstrType::J) {
        // JAL：目标 = pc + imm 发射时已知，不 flush
        predicted_taken = true;
        predicted_target = cmd.pc + static_cast<uint32_t>(cmd.imm);
        next_pc = predicted_target;
    } else if (cmd.type == InstrType::I && cmd.cmdname == "jalr") {
        // JALR：依赖 rs1，predicted_target = pc + 4；
        // commit 比较真实目标 != predicted_target → flush
        predicted_taken = true;
        predicted_target = cmd.pc + 4;
        next_pc = predicted_target;
    }

    // 6. 分配 ROB
    const uint32_t rob_idx = static_cast<uint32_t>(rob_allocate(
        c, n, cmd,
        is_control,
        (cmd.type == InstrType::B) ? predicted_taken : true,
        0,             // branch_target（ALU 回填）
        predicted_target,
        c.pc + 4));           // next_pc（顺序流 / J/JALR 返回地址）

    // 7. 访存类分配 LSQ（先占 LSQ 槽、填 rob_idx；rs_tag 由步骤 9 同周期回填）
    int32_t lsq_idx = -1;
    if (is_mem) {
        lsq_idx = lsq_allocate(c, n, cmd, static_cast<int32_t>(rob_idx));
    }

    // 8. 分配 RS（访存类同样占 RS 槽——地址基址/数据经 RS 捕获，槽由 LSQ 显式释放）
    const int32_t rs_tag = rs_allocate(c, n, cmd, static_cast<int32_t>(rob_idx));

    // 9. 同周期回填关联：LSQ 记录关联 RS 槽（load 经 CDB 广播后 / store 完成回填时释放）
    if (lsq_idx >= 0) {
        n.lsq[lsq_idx].rs_tag = rs_tag;
    }

    // 操作数旁路 原因：CDB capture 只读 cur 且先于 issue 执行，看不到本周期才诞生的消费者
    // 若生产者的广播恰好发生在消费指令被发射的同一周期，其 q 指向的 ROB 条目将不会再有第二次广播，导致永久等待
    // 等待 tag 直接从 cur.reg_status 取（不读 next）
    auto operand_bypass = [&](uint32_t rs, int32_t& q, uint32_t& v) {
        const int32_t qq = c.reg_status[rs];
        if (qq < 0 || qq >= ROB_SIZE) {
            return;
        }
        if (c.rob[qq].ready) { // 广播已发生在更早周期
            v = c.rob[qq].value;
            q = -1;
            return;
        }
        for (int i = 0; i < RS_SIZE; ++i) {  // 算术/分支生产者已执行
            if (c.rs[i].busy && c.rs[i].done && c.rs[i].rob_idx == qq) {
                v = c.rs[i].result;
                q = -1;
                return;
            }
        }
        for (int i = 0; i < LSQ_COUNT; ++i) { // load 生产者读回值已就绪
            if (c.lsq[i].busy && c.lsq[i].done &&
                c.lsq[i].rob_idx == qq) {
                v = c.lsq[i].result;
                q = -1;
                return;
            }
        }
    };
    operand_bypass(cmd.rs1, n.rs[rs_tag].q1, n.rs[rs_tag].v1);
    operand_bypass(cmd.rs2, n.rs[rs_tag].q2, n.rs[rs_tag].v2);
    if (lsq_idx >= 0) {
        operand_bypass(cmd.rs1, n.lsq[lsq_idx].q1, n.lsq[lsq_idx].v1);
        operand_bypass(cmd.rs2, n.lsq[lsq_idx].q2, n.lsq[lsq_idx].v2);
    }

    // 10/11. reg_status 重命名与 PC 更新 → 填增量（merge 唯一写者）
    d.issue_fired = true;
    d.issue_rob_idx = static_cast<int32_t>(rob_idx);
    d.issue_next_pc = next_pc;
    d.issue_rs_tag = rs_tag;
    d.issue_lsq_idx = lsq_idx;
    if (cmd.writes_rd() && cmd.rd != 0) {
        d.issue_rename = true;
        d.issue_rd = static_cast<int>(cmd.rd);
    }
}

// 执行阶段
void CPU::execute(const CPUState& c, CPUState& n) {
    riscv::rs_execute(c, n);
    riscv::lsq_advance(c, n, memory);
}

// CDB 选择
CdbSignal CPU::cdb_arbitrate(const CPUState& c, CPUState& n) {
    return ::riscv::cdb_arbitrate(c, n);
}

// CDB 广播捕获
void CPU::cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig) {
    ::riscv::cdb_capture(c, n, sig);
}

// ROB 提交
void CPU::commit(const CPUState& c, CPUState& n, ConflictDeltas& d) {
    ::riscv::rob_commit(c, n, memory, d);
}

// Flush（分支预测失败）：ROB 的恢复尾巴。
// 全程只读 cur + 增量 d（不读 next）：
// kill 判定基于 cur 的 busy/age，外加 issue 增量里本拍新分配的条目；
// RS/LSQ/ROB 槽位清除与 rob_tail 回退直接写（本函数钉在 issue 之后，优先级有保证）；
// reg_status 重建与 PC 恢复不在这里写，填增量交由 merge_conflict。
void CPU::flush_pipeline(const CPUState& c, CPUState& n, ConflictDeltas& d) {
    const int32_t branch_idx = static_cast<int32_t>(c.rob_head);
    const uint32_t branch_age = rob_age(c, branch_idx);

    d.flush_active = true;
    d.flush_branch_idx = branch_idx;

    // 1. 清空 RS 中分支之后的条目（读 cur；issue 本拍新分配槽经增量补清）
    for (int i = 0; i < RS_SIZE; ++i) {
        if (c.rs[i].busy && rob_age(c, c.rs[i].rob_idx) > branch_age) {
            n.rs[i].busy = false;
            n.rs[i].done = false;
        }
    }
    if (d.issue_fired && d.issue_rs_tag >= 0 &&
        rob_age(c, d.issue_rob_idx) > branch_age) {
        n.rs[d.issue_rs_tag].busy = false;
        n.rs[d.issue_rs_tag].done = false;
    }

    // 2. 清空 LSQ 中分支之后的条目（同上）
    for (int i = 0; i < LSQ_COUNT; ++i) {
        if (c.lsq[i].busy && rob_age(c, c.lsq[i].rob_idx) > branch_age) {
            n.lsq[i].busy = false;
            n.lsq[i].done = false;
            n.lsq[i].address_ready = false;
        }
    }
    if (d.issue_fired && d.issue_lsq_idx >= 0 &&
        rob_age(c, d.issue_rob_idx) > branch_age) {
        n.lsq[d.issue_lsq_idx].busy = false;
        n.lsq[d.issue_lsq_idx].done = false;
        n.lsq[d.issue_lsq_idx].address_ready = false;
    }

    // 3. 清空 ROB 中分支之后的条目并回退 tail
    //    终点 = cur.rob_tail + （issue 本拍新分配 ? 1 : 0），不读 n.rob_tail
    const uint32_t new_tail = (static_cast<uint32_t>(branch_idx) + 1) % ROB_SIZE;
    const uint32_t eff_tail =
        d.issue_fired ? (c.rob_tail + 1) % ROB_SIZE : c.rob_tail;
    for (uint32_t i = new_tail; i != eff_tail; i = (i + 1) % ROB_SIZE) {
        if (c.rob[i].busy) {
            n.rob[i].busy = false;
            n.rob[i].ready = false;
        }
    }
    // issue 本拍新条目（cur 中尚为空闲槽，上面扫不到）必属分支之后，一并清
    if (d.issue_fired) {
        n.rob[d.issue_rob_idx].busy = false;
        n.rob[d.issue_rob_idx].ready = false;
    }
    n.rob_tail = new_tail;

    n.need_flush = false;
}

// reg_status / pc 的唯一写者（优先级编码器，只读 cur + 增量，与调用顺序无关）
void CPU::merge_conflict(const CPUState& c, CPUState& n,
                         const ConflictDeltas& d) {
    // reg_status：flush 重建 > issue 重命名 > commit 退役清除
    if (d.flush_active) {
        // 重建法：对每个 rd 扫描残余 ROB（cur）找最近写者
        // 残余 = cur 中 busy 且不在 [branch+1, eff_tail) 清除区间、且非本拍退役的分支自身
        const uint32_t new_tail =
            (static_cast<uint32_t>(d.flush_branch_idx) + 1) % ROB_SIZE;
        const uint32_t eff_tail =
            d.issue_fired ? (c.rob_tail + 1) % ROB_SIZE : c.rob_tail;
        const uint32_t span = (eff_tail + ROB_SIZE - new_tail) % ROB_SIZE;
        n.reg_status[0] = -1;  // x0 恒就绪
        for (int rd = 1; rd < 32; ++rd) {
            int32_t found = -1;
            uint32_t best_age = 0;  // age 最大 = 程序序最近
            for (int i = 0; i < ROB_SIZE; ++i) {
                const RobEntry& e = c.rob[i];
                if (!e.busy || !e.cmd.writes_rd() ||
                    static_cast<int>(e.cmd.rd) != rd) {
                    continue;
                }
                if (i == d.flush_branch_idx) {
                    continue;  // 分支自身本拍退役
                }
                if ((static_cast<uint32_t>(i) + ROB_SIZE - new_tail) %
                        ROB_SIZE < span) {
                    continue;  // 分支之后，已被清除
                }
                const uint32_t a = rob_age(c, i);
                if (a >= best_age) {
                    best_age = a;
                    found = i;
                }
            }
            n.reg_status[rd] = found;
        }
    } else {
        if (d.commit_retire_rd &&
            c.reg_status[d.commit_rd] == d.commit_head) {
            n.reg_status[d.commit_rd] = -1;
        }
        if (d.issue_rename) {
            n.reg_status[d.issue_rd] = d.issue_rob_idx;  // issue 覆盖（固定赢）
        }
    }

    // pc：误预测恢复目标 > issue 预测流 next_pc
    if (d.commit_mispredict) {
        n.pc = d.commit_target;
    } else if (d.issue_fired) {
        n.pc = d.issue_next_pc;
    }
}

// 主循环
uint8_t CPU::run() {
    uint64_t cycles_unused;
    return run_with_order(0, cycles_unused);
}

// 主循环实现。四个组合阶段（CDB / commit / execute / issue）只读 cur、
// 对 reg_status/pc 只产增量，两两可任意交换：shuffle_seed == 0 时按固定顺序
// CDB→commit→execute→issue，否则每周期用 xorshift 随机数做 Fisher–Yates 打乱。
// flush（ROB 恢复尾巴，只读 cur + 增量）与 merge_conflict（reg_status/pc 的
// 优先级编码器 + 唯一写者）钉在最后，对应硬件"组合求值 → 优先级编码器 → 锁存"。
uint8_t CPU::run_with_order(uint32_t shuffle_seed, uint64_t& cycles_out) {
    constexpr uint64_t MAX_CYCLES = 500000000;
    uint32_t rng = shuffle_seed ? shuffle_seed : 1u;
    auto next_rand = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };

    while (true) {
        next.regs[0] = 0;
        ConflictDeltas d{};
        CdbSignal sig;

        // 阶段编号：0=CDB（组合线，arbitrate→capture 内部有序）
        //           1=commit  2=execute  3=issue
        int order[4] = {0, 1, 2, 3};
        if (shuffle_seed != 0) {
            for (int i = 3; i > 0; --i) {
                std::swap(order[i],
                          order[next_rand() % static_cast<uint32_t>(i + 1)]);
            }
        }
        for (int k = 0; k < 4; ++k) {
            switch (order[k]) {
                case 0:
                    sig = cdb_arbitrate(cur, next);   // CDB 选择
                    cdb_capture(cur, next, sig);      // CDB 广播
                    break;
                case 1:
                    commit(cur, next, d);             // 提交
                    break;
                case 2:
                    execute(cur, next);               // 执行（RS 就绪项 + LSQ 推进）
                    break;
                case 3:
                    if (!cur.stopped) {
                        issue(cur, next, d);          // 发射
                    }
                    break;
            }
        }
        if (next.stopped) {
            cycles_out = cur.cycles;
            return next.return_value;   // 终止（本周期不计入周期数）
        }
        if (d.commit_mispredict) {
            flush_pipeline(cur, next, d);  // ROB 恢复尾巴（钉尾）
        }
        merge_conflict(cur, next, d);      // reg_status / pc 唯一写者（钉尾）
        next.cycles = cur.cycles + 1;
        cur = next;
        if (cur.cycles > MAX_CYCLES) {
            cycles_out = cur.cycles;
            return cur.regs[10] & 0xFF;
        }
    }
}

}  // namespace riscv