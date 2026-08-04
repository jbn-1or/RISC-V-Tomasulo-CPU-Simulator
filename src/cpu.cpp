#include "cpu.hpp"

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
void CPU::issue(const CPUState& c, CPUState& n) {
    // 1. 停机后不再发射
    if (c.stopped) {
        return;
    }

    // 2. 取指
    const uint32_t ins = fetch_instruction(memory, c.pc);

    // 3. 解码；0x0ff00513 作为普通commit 阶段停
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

    // bug操作数旁路 原因：CDB capture 只读 cur 且先于 issue 执行，看不到本周期才诞生的消费者
    // 若生产者的广播恰好发生在消费指令被发射的同一周期，其 q 指向的ROB 条目将不会再有第二次广播，导致永久等待
    auto operand_bypass = [&](int32_t& q, uint32_t& v) {
        if (q < 0 || q >= ROB_SIZE) {
            return;
        }
        if (c.rob[q].ready) { // 广播已发生在更早周期
            v = c.rob[q].value;
            q = -1;
            return;
        }
        for (int i = 0; i < RS_SIZE; ++i) {  // 算术/分支生产者已执行
            if (c.rs[i].busy && c.rs[i].done && c.rs[i].rob_idx == q) {
                v = c.rs[i].result;
                q = -1;
                return;
            }
        }
        for (int i = 0; i < LSQ_COUNT; ++i) { // load 生产者读回值已就绪
            if (c.lsq[i].busy && c.lsq[i].done &&
                c.lsq[i].rob_idx == q) {
                v = c.lsq[i].result;
                q = -1;
                return;
            }
        }
    };
    operand_bypass(n.rs[rs_tag].q1, n.rs[rs_tag].v1);
    operand_bypass(n.rs[rs_tag].q2, n.rs[rs_tag].v2);
    if (lsq_idx >= 0) {
        operand_bypass(n.lsq[lsq_idx].q1, n.lsq[lsq_idx].v1);
        operand_bypass(n.lsq[lsq_idx].q2, n.lsq[lsq_idx].v2);
    }

    // 10. 更新 reg_status：指向 ROB 条目
    if (cmd.writes_rd() && cmd.rd != 0) {
        n.reg_status[cmd.rd] = static_cast<int32_t>(rob_idx);
    }

    // 11. 更新 PC
    n.pc = next_pc;
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
void CPU::commit(const CPUState& c, CPUState& n) {
    ::riscv::rob_commit(c, n, memory);
}

// Flush 流水线（分支预测失败）
void CPU::flush_pipeline(const CPUState& c, CPUState& n, uint32_t target_pc) {
    const int32_t branch_idx = static_cast<int32_t>(c.rob_head);
    const uint32_t branch_age = rob_age(c, branch_idx);

    // 1. 恢复 PC 为正确目标
    n.pc = target_pc;

    // 2. 清空 RS 中分支之后的条目
    for (int i = 0; i < RS_SIZE; ++i) {
        if (n.rs[i].busy && rob_age(c, n.rs[i].rob_idx) > branch_age) {
            n.rs[i].busy = false;
            n.rs[i].done = false;
        }
    }

    // 3. 清空 LSQ 中分支之后的条目
    for (int i = 0; i < LSQ_COUNT; ++i) {
        if (n.lsq[i].busy && rob_age(c, n.lsq[i].rob_idx) > branch_age) {
            n.lsq[i].busy = false;
            n.lsq[i].done = false;
            n.lsq[i].address_ready = false;
        }
    }

    // 4. 清空 ROB 中分支之后的条目并回退 tail
    const uint32_t new_tail = (static_cast<uint32_t>(branch_idx) + 1) % ROB_SIZE;
    for (uint32_t i = new_tail; i != n.rob_tail; i = (i + 1) % ROB_SIZE) {
        if (n.rob[i].busy) {
            n.rob[i].busy = false;
            n.rob[i].ready = false;
        }
    }
    n.rob_tail = new_tail;

    // 恢复 reg_status：扫描 ROB 重建
    // 每个 rd 找最近写入者——正常执行下分支后条目全清 → 全部恢复 -1）
    n.reg_status[0] = -1;  // x0 恒就绪
    for (int rd = 1; rd < 32; ++rd) {
        int32_t found = -1;
        uint32_t best_age = 0;  // age 最大 = 程序序最近
        for (int i = 0; i < ROB_SIZE; ++i) {
            const RobEntry& e = n.rob[i];
            if (!e.busy || !e.cmd.writes_rd() ||
                static_cast<int32_t>(e.cmd.rd) != rd) {
                continue;
            }
            const uint32_t a = rob_age(n, i);
            if (a >= best_age) {
                best_age = a;
                found = i;
            }
        }
        n.reg_status[rd] = found;
    }

    n.need_flush = false;
}

// 主循环
uint8_t CPU::run() {
    constexpr uint64_t MAX_CYCLES = 500000000;

    while(true) {
        next.regs[0] = 0;
        CdbSignal sig = cdb_arbitrate(cur, next); // CDB 选择
        cdb_capture(cur, next, sig);              // CDB 广播
        commit(cur, next);     // 提交
        if (next.stopped) {
            return next.return_value;   // 终止
        }
        execute(cur, next);   // 执行（RS 就绪项 + LSQ 推进）
        if (!cur.stopped) {
            issue(cur, next);    // 发射
        }
        if (next.need_flush) {
            // 正确目标由分支条目计算：
            //   B 型：taken → branch_target；not taken → next_pc（=pc+4）
            //   JALR：branch_target（ALU 真实目标）
            const RobEntry& br = cur.rob[cur.rob_head];
            const uint32_t target =
                (br.cmd.type == InstrType::B)
                    ? (br.branch_taken ? br.branch_target : br.next_pc)
                    : br.branch_target;
            flush_pipeline(cur, next, target);
        }
        next.cycles = cur.cycles + 1;
        cur = next;
        if (cur.cycles > MAX_CYCLES) {
            return cur.regs[10] & 0xFF;
        }
    }
}

}  // namespace riscv