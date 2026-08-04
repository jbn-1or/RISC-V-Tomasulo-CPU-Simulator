#pragma once

#include <cstdint>

#include "branch_predictor.hpp"
#include "cdb.hpp"
#include "load_store_buffer.hpp"
#include "reservation_station.hpp"
#include "rob.hpp"

namespace riscv {

// 冲突字段增量（小纸条）：commit / issue / flush 对 reg_status / pc 两个
// 动态冲突字段不直接写 next，改为各产一张只读 cur 的增量，由
// CPU::merge_conflict 作为唯一写者按代码内固定优先级落笔
// （等价硬件优先级编码器，见 handlemistake.md §6.4）。
struct ConflictDeltas {
    // commit 阶段产出
    bool commit_retire_rd = false;  // head 退役且写 rd（≠ x0）
    int commit_rd = -1;
    int32_t commit_head = -1;       // 本拍退役的 ROB head 索引
    bool commit_mispredict = false;
    uint32_t commit_target = 0;     // 误预测恢复目标

    // issue 阶段产出
    bool issue_fired = false;       // 本拍实际发射（未 stall / 未停机）
    bool issue_rename = false;      // 写 rd（≠ x0）→ 重命名
    int issue_rd = -1;
    int32_t issue_rob_idx = -1;     // 新分配的 ROB 条目（重命名映射值）
    uint32_t issue_next_pc = 0;     // 预测流下一 PC
    int32_t issue_rs_tag = -1;      // 新分配 RS 槽（flush 清除用）
    int32_t issue_lsq_idx = -1;     // 新分配 LSQ 槽（flush 清除用）

    // flush 阶段产出（由 flush_pipeline 依 cur + 上两段增量推导，不读 next）
    bool flush_active = false;
    int32_t flush_branch_idx = -1;  // 误预测分支所在 ROB 条目（= cur.rob_head）
};

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