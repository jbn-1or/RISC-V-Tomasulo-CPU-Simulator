#pragma once

#include <cstdint>
#include <map>

#include "command.hpp"

namespace riscv {

constexpr int LSQ_PIPE_DEPTH = 3;

struct LsqEntry {
    bool busy = false;
    Command cmd;

    // tag 关联
    int32_t rs_tag = -1;     // 关联 RS 槽位索引
    int32_t rob_idx = -1;    // 关联 ROB 条目

    // q1/q2 等待 ROB 条目索引
    uint32_t v1 = 0;
    int32_t q1 = -1;         // 地址基址: 值 + 等待的 ROB 条目索引
    uint32_t v2 = 0;
    int32_t q2 = -1;         // store 数据: 值 + 等待的 ROB 条目索引

    // 执行状态
    uint32_t address = 0;        // 操作数就绪后算出访存地址 (=v1+imm)
    uint32_t store_data = 0;     // store 待写数据 (=v2)
    uint32_t result = 0;         // load 读回值（CDB 广播的数据源）
    bool address_ready = false;  // 操作数就绪、地址已计算
    bool done = false;           // load 访存完成，待 CDB 广播
    int cycle = 0;               // 0→1→2→完成（LSQ_PIPE_DEPTH）
};

constexpr int LSQ_COUNT = 4;

struct CPUState;
struct CdbSignal;

// 是否有空闲 LSQ 槽位
bool lsq_has_free(const CPUState& s);

// 发射阶段：登记访存指令返回槽位索引，读 c.regs / c.reg_status 填 v1/v2/q1/q2
int32_t lsq_allocate(const CPUState& c, CPUState& n, const Command& cmd,
                     int32_t rob_idx);

// CDB 捕获， q1/q2 匹配 sig.rob_idx → 捕获 value 并清 q
void lsq_cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig);

// 执行阶段
void lsq_execute(const CPUState& c, CPUState& n,
                 std::map<uint32_t, uint8_t>& memory);

}  // namespace riscv