#pragma once

#include <cstdint>

#include "branch_predictor.hpp"
#include "cdb.hpp"
#include "load_store_buffer.hpp"
#include "reservation_station.hpp"
#include "rob.hpp"

namespace riscv {

// 全部硬件状态（cur / next）
// 由需要访存的 port（issue 取指 / LSQ load 读 / rob_commit 写 store）按引用传入。
struct CPUState {
    // 寄存器（ROB 有序提交后的架构状态）
    uint32_t regs[32] = {0};

    // 寄存器状态表（-1 表示值在 regs 中就绪；否则指向 ROB 条目索引 rob_idx)
    int32_t reg_status[32];

    uint32_t pc = 0;

    // 保留站（统一，8）
    RsEntry rs[RS_SIZE];

    // 用于重排序缓冲
    RobEntry rob[ROB_SIZE];
    uint32_t rob_head = 0;
    uint32_t rob_tail = 0;

    // lsq
    LsqEntry lsq[LSQ_COUNT];

    // 分支预测器
    BranchPredictor predictor;

    // 流水线控制 / 统计
    bool need_flush = false;   // 分支预测失败
    bool stopped = false;      // 遇到终止哨兵 0x0ff00513
    uint8_t return_value = 0;  // 停机时的返回值（regs[10] & 0xFF）
    uint64_t cycles = 0;       // 已运行周期数

    CPUState() {
        for (int i = 0; i < 32; ++i) {
            reg_status[i] = -1;
        }
    }
};

}  // namespace riscv