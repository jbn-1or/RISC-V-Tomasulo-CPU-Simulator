#pragma once

#include <cstdint>
#include <map>

#include "branch_predictor.hpp"
#include "cdb.hpp"
#include "load_store_buffer.hpp"
#include "reservation_station.hpp"
#include "rob.hpp"

namespace riscv {

/// 全部硬件状态（cur / next 两份实例）
struct CPUState {
    // 寄存器文件（仅 commit 时写，ROB 有序提交后的架构状态）
    uint32_t regs[32] = {0};

    // 寄存器状态表（-1 表示值在 regs 中就绪；否则指向保留站 tag）
    int32_t reg_status[32];

    // 内存（字节寻址）
    std::map<uint32_t, uint8_t> memory;

    // 程序计数器
    uint32_t pc = 0;

    // 保留站（统一池，8 槽位）
    RsEntry rs[RS_SIZE];

    // 重排序缓冲（环形缓冲区，16 项）
    RobEntry rob[ROB_SIZE];
    uint32_t rob_head = 0;
    uint32_t rob_tail = 0;

    // 访存流水线单元
    LsuSlot lsu_slots[LSU_COUNT];

    // 分支预测器
    BranchPredictor predictor;

    // CDB 广播信号
    CdbSignal cdb;

    // 流水线控制 / 统计
    bool need_flush = false;   // 分支预测失败
    bool stopped = false;      // 遇到终止哨兵 0x0ff00513
    uint8_t return_value = 0;  // 停机时的返回值（regs[10] & 0xFF）
    uint64_t cycles = 0;       // 已运行周期数

    CPUState() {
        // 寄存器状态表全部初始化为 -1，x0 恒就绪
        for (int i = 0; i < 32; ++i) {
            reg_status[i] = -1;
        }
    }
};

}  // namespace riscv