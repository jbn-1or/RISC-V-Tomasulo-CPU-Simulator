#include "rob.hpp"

#include "cpu_state.hpp"

namespace riscv {

bool rob_is_full(const CPUState& s) {
    return (s.rob_tail + 1) % ROB_SIZE == s.rob_head;
}

bool rob_empty(const CPUState& s) {
    return s.rob_head == s.rob_tail;
}

// --- port-helper 方法（读 cur、写 next） ---

int32_t rob_allocate(const CPUState& c, CPUState& n, const Command& cmd,
                     bool is_branch, bool branch_predicted,
                     uint32_t branch_target, uint32_t predicted_target,
                     uint32_t next_pc) {
    // 调用方（issue 阶段）保证 cur 中 ROB 未满，tail 处必有空槽
    uint32_t idx = c.rob_tail;
    RobEntry& slot = n.rob[idx];
    slot.busy = true;
    slot.cmd = cmd;
    slot.ready = false;
    slot.value = 0;
    slot.store_address = 0;
    slot.is_branch = is_branch;
    slot.branch_taken = false;
    slot.branch_predicted = branch_predicted;
    slot.branch_target = branch_target;
    slot.predicted_target = predicted_target;
    slot.next_pc = next_pc;
    n.rob_tail = (c.rob_tail + 1) % ROB_SIZE;
    return static_cast<int32_t>(idx);
}

void rob_cdb_capture(const CPUState& c, CPUState& n) {
    if (!c.cdb.valid) {
        return;
    }
    // 按 rob_idx 匹配：CDB 唤醒/捕获 tag 统一为 rob_idx（见 DESIGN.md §2 顶部说明）
    int32_t idx = c.cdb.rob_idx;
    if (idx < 0 || idx >= ROB_SIZE) {
        return;
    }
    if (!c.rob[idx].busy) {
        return;
    }
    RobEntry& slot = n.rob[idx];
    slot.ready = true;
    slot.value = c.cdb.value;
    if (c.cdb.is_branch) {
        // 分支指令：回填执行结果（taken 与目标地址）
        slot.branch_taken = c.cdb.branch_taken;
        slot.branch_target = c.cdb.branch_target;
    }
}

void rob_commit(const CPUState& c, CPUState& n) {
    if (rob_empty(c)) {
        return;
    }
    uint32_t head = c.rob_head;
    const RobEntry& slot = c.rob[head];
    // 仅当 head 条目已就绪（结果已通过 CDB 写回）时提交
    if (!slot.busy || !slot.ready) {
        return;
    }

    // 1. 终止哨兵 0x0ff00513（li a0, 255）：停机并记录返回值。
    //    哨兵不执行（其写回 a0←255 不落盘）；哨兵之前的指令已全部按序提交
    //    （此判断位于写 regs/写 memory 之前），返回值 = 执行前 regs[10] 的低 8 位。
    //    （不能用 slot.value，否则恒为 255 而非程序真实返回值，见 DESIGN.md §4.1）
    if (slot.cmd.raw == 0x0ff00513u) {
        n.stopped = true;
        n.return_value = c.regs[10] & 0xFF;
        n.rob[head].busy = false;
        n.rob_head = (head + 1) % ROB_SIZE;
        return;
    }

    // 2. Store：按 funct3 决定字节宽度，逐字节写内存
    //    sb=0b000(1B), sh=0b001(2B), sw=0b010(4B)
    if (slot.cmd.type == InstrType::S) {
        uint32_t addr = slot.store_address;
        uint32_t data = slot.value;
        uint32_t width;
        switch (slot.cmd.funct3) {
            case 0b000: width = 1; break;
            case 0b001: width = 2; break;
            default:    width = 4; break;
        }
        for (uint32_t b = 0; b < width; ++b) {
            n.memory[addr + b] =
                static_cast<uint8_t>((data >> (b * 8)) & 0xFF);
        }
    }

    // 3. 写寄存器文件（排除 x0 与不写 rd 的指令类型）
    if (slot.cmd.writes_rd() && slot.cmd.rd != 0) {
        n.regs[slot.cmd.rd] = slot.value;
        // 若该寄存器最近一次重命名正是本 ROB 条目，则提交后恢复为"就绪"（-1）。
        // reg_status 指向 ROB 条目索引（见 DESIGN.md §2 顶部说明）。
        if (c.reg_status[slot.cmd.rd] == static_cast<int32_t>(head)) {
            n.reg_status[slot.cmd.rd] = -1;
        }
    }

    // 4. 控制流：更新预测器（仅 B 型）；检测 mispredict 并给出正确的下一 PC
    if (slot.is_branch) {
        // JAL/JALR 恒 taken；仅 B 型条件分支使用 2-bit 预测器
        if (slot.cmd.type == InstrType::B) {
            n.predictor = c.predictor;
            n.predictor.update(slot.cmd.pc, slot.branch_taken);
        }

        // 误预测判定（见 DESIGN.md §5.2）：
        //   B  型：taken != predicted
        //   JAL：  target == predicted_target 恒成立（预测精确）→ 永不 flush
        //   JALR： branch_target（ALU 真实目标）!= predicted_target（pc+imm 猜测）→ flush
        bool mispredicted = false;
        if (slot.cmd.type == InstrType::B) {
            mispredicted = (slot.branch_taken != slot.branch_predicted);
        } else if (slot.cmd.type == InstrType::J) {
            mispredicted = false;  // 预测精确
        } else if (slot.cmd.type == InstrType::I && slot.cmd.cmdname == "jalr") {
            mispredicted = (slot.branch_target != slot.predicted_target);
        }

        if (mispredicted) {
            n.need_flush = true;
            // B 型：taken → 实际目标；not taken → 顺序流 pc+4
            // JALR：目标恒 = branch_target（ALU 真实目标）
            n.pc = (slot.cmd.type == InstrType::B)
                       ? (slot.branch_taken ? slot.branch_target : slot.next_pc)
                       : slot.branch_target;
        }
    }

    // 5. 释放 head 条目并推进
    n.rob[head].busy = false;
    n.rob_head = (head + 1) % ROB_SIZE;
}

}  // namespace riscv