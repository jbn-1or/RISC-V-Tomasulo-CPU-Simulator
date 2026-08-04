#include "rob.hpp"

#include "cpu_state.hpp"

namespace riscv {

bool rob_is_full(const CPUState& s) {
    return (s.rob_tail + 1) % ROB_SIZE == s.rob_head;
}

bool rob_empty(const CPUState& s) {
    return s.rob_head == s.rob_tail;
}

uint32_t rob_age(const CPUState& s, int32_t rob_idx) {
    // 无效索引
    if (rob_idx < 0 || rob_idx >= ROB_SIZE) {
        return ROB_SIZE;
    }
    return (static_cast<uint32_t>(rob_idx) + ROB_SIZE - s.rob_head) % ROB_SIZE;
}

int32_t rob_allocate(const CPUState& c, CPUState& n, const Command& cmd,
                     bool is_branch, bool branch_predicted,
                     uint32_t branch_target, uint32_t predicted_target,
                     uint32_t next_pc) {
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

void rob_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig) {
    if (!sig.valid) {
        return;
    }
    // 按 rob_idx 匹配
    int32_t idx = sig.rob_idx;
    if (idx < 0 || idx >= ROB_SIZE) {
        return;
    }
    if (!c.rob[idx].busy) {
        return;
    }
    RobEntry& slot = n.rob[idx];
    slot.ready = true;
    slot.value = sig.value;
    if (sig.is_branch) {
        // 分支指令
        slot.branch_taken = sig.branch_taken;
        slot.branch_target = sig.branch_target;
    }
}

void rob_commit(const CPUState& c, CPUState& n,
                std::map<uint32_t, uint8_t>& memory) {
    if (rob_empty(c)) {
        return;
    }
    uint32_t head = c.rob_head;
    const RobEntry& slot = c.rob[head];
    // 当 head 条目已就绪
    if (!slot.busy || !slot.ready) {
        return;
    }

    if (slot.cmd.raw == 0x0ff00513u) {
        n.stopped = true;
        n.return_value = c.regs[10] & 0xFF;
        n.rob[head].busy = false;
        n.rob_head = (head + 1) % ROB_SIZE;
        return;
    }

    // 2. Store：按 funct3 决定字节宽度，逐字节写内存
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
            memory[addr + b] =
                static_cast<uint8_t>((data >> (b * 8)) & 0xFF);
        }
    }

    // 3. 写寄存器文件（排除 x0 与不写 rd 的指令类型）
    if (slot.cmd.writes_rd() && slot.cmd.rd != 0) {
        n.regs[slot.cmd.rd] = slot.value;
        if (c.reg_status[slot.cmd.rd] == static_cast<int32_t>(head)) {
            n.reg_status[slot.cmd.rd] = -1;
        }
    }

    // 4. 控制流：更新预测器（仅 B 型）；检测 mispredict 并给出正确的下一 PC
    if (slot.is_branch) {
        if (slot.cmd.type == InstrType::B) {
            n.predictor = c.predictor;
            n.predictor.update(slot.cmd.pc, slot.branch_taken);
        }

        // 误预测判定
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
            // JALR：目标恒 = branch_target
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