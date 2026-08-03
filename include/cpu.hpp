#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "alu.hpp"
#include "branch_predictor.hpp"
#include "cdb.hpp"
#include "command.hpp"
#include "load_store_buffer.hpp"
#include "reservation_station.hpp"
#include "rob.hpp"

namespace riscv {

class CPU {
public:
    // === 寄存器文件（仅 commit 时写，ROB 有序提交后的架构状态）===
    uint32_t regs[32] = {0};

    // === 内存 ===
    std::map<uint32_t, uint8_t> memory;

    // === 程序计数器 ===
    uint32_t pc = 0;

    // === 寄存器状态表（-1 表示值在 regs 中就绪；否则指向 RS tag）===
    int32_t reg_status[32];

    // === 保留站（统一池） ===
    RsEntry rs[RS_SIZE];

    // === 重排序缓冲（环形缓冲区） ===
    RobEntry rob[ROB_SIZE];
    uint32_t rob_head = 0;
    uint32_t rob_tail = 0;

    // === 访存流水线 ===
    LsuSlot lsu_slots[LSU_COUNT];

    // === 分支预测器 ===
    BranchPredictor predictor;

    // === CDB 信号 ===
    CdbSignal cdb;
    bool need_flush = false;

    // === 统计 ===
    uint64_t cycles = 0;
    bool stopped = false;
    uint8_t return_value = 0;

    // === 方法 ===
    CPU();

    bool load_program(const std::string& path);
    uint8_t run();

private:
    // --- const wire 方法（只读状态、不修改） ---
    /// 是否有空闲 RS 槽位
    bool rs_has_free() const;
    /// ROB 是否满
    bool rob_is_full() const;
    /// LSU 是否有空闲槽位接受新访存
    bool lsu_has_free() const;
    /// ROB 是否为空
    bool rob_empty() const;

    // --- 每周期至多调用一次的操作（port） ---
    /// 发射一条指令（取指+解码+分配 RS/ROB）
    void issue();

    /// 执行阶段：RS 就绪项执行、LSU 推进
    void execute();

    /// CDB 仲裁：从完成结果中选一个广播
    void cdb_arbitrate();

    /// CDB 广播捕获：所有 RS 槽位 + ROB 捕获 CDB 结果
    void cdb_capture();

    /// ROB 提交：顺序提交一条指令
    void commit();

    /// Flush 流水线（分支预测失败时）
    void flush_pipeline(uint32_t target_pc);
};

}  // namespace riscv