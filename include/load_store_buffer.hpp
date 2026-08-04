#pragma once

#include <cstdint>
#include <map>

#include "command.hpp"

namespace riscv {

constexpr int LSU_PIPE_DEPTH = 3;

struct LsuSlot {
    bool busy = false;
    Command cmd;

    // tag 关联
    int32_t rs_tag = -1;     // 关联 RS 槽位索引（load 被 CDB 选中后释放该 RS 槽；store 完成回填时释放）
    int32_t rob_idx = -1;    // 关联 ROB 条目（load 供 ROB 捕获 / store 回填地址与数据）

    // 操作数等待机制（仿 RS，处理地址基址 / store 数据的 RAW 依赖）。
    // q1/q2 等待的是 ROB 条目索引（唤醒按 rob_idx 匹配 CDB，见 DESIGN.md §2）。
    uint32_t v1 = 0;
    int32_t q1 = -1;         // 地址基址: 值 + 等待的 ROB 条目索引
    uint32_t v2 = 0;
    int32_t q2 = -1;         // store 数据: 值 + 等待的 ROB 条目索引

    // 执行状态
    uint32_t address = 0;        // 操作数就绪后算出访存地址 (=v1+imm)
    uint32_t store_data = 0;     // store 待写数据 (=v2)
    uint32_t result = 0;         // load 读回值（直通 CDB 广播的数据源）
    bool address_ready = false;  // 操作数就绪、地址已计算
    bool done = false;           // load 访存完成，待 CDB 仲裁广播（与 RsEntry.done 对称）
    int cycle = 0;               // 0→1→2→完成（cycle 达 LSU_PIPE_DEPTH 即完成）
};

constexpr int LSU_COUNT = 4;

struct CPUState;
struct CdbSignal;

// --- wire 方法（只读状态、不修改） ---

/// 是否有空闲 LSQ 槽位（遍历 4 槽，busy == false 即空闲）
bool lsq_has_free(const CPUState& s);

// --- port 方法（读 cur、写 next） ---

/// 发射阶段：在任意空闲槽登记一条访存指令（load / store），返回槽位索引（0..LSU_COUNT-1）
/// 读 c.regs / c.reg_status 填充 v1/v2/q1/q2（q = reg_status[rs] = 等待的 ROB 条目索引，-1 就绪）
/// rs_tag 由 issue 阶段同周期回填（见 DESIGN.md §6 步骤 9），本函数不写
int32_t lsq_allocate(const CPUState& c, CPUState& n, const Command& cmd,
                     int32_t rob_idx);

/// CDB 捕获阶段：所有槽位 q1/q2 匹配 sig.rob_idx → 捕获 value 并清 q
/// （唤醒/捕获 tag 统一为 rob_idx，见 DESIGN.md §2 顶部说明）
void lsq_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig);

/// 执行阶段：LSQ 流水推进（3 级流水，见 DESIGN.md §5.3）
///   - load：q1 就绪 → 地址计算启动流水；cycle 达 LSU_PIPE_DEPTH 时满足
///     store-to-load 内存序约束后完成（done=true、result=读回值，直通 CDB 待仲裁）
///   - store：q1 与 q2 均就绪 → 启动流水；cycle 达 LSU_PIPE_DEPTH 时回填 ROB
///     （value/store_address/ready）+ 释放关联 RS 槽 + 释放 LSQ 槽
/// memory 为 CPU 类持有的单实例内存（load 完成读内存用，见 cpu_state.hpp 顶部说明）
void lsq_advance(const CPUState& c, CPUState& n,
                 std::map<uint32_t, uint8_t>& memory);

}  // namespace riscv