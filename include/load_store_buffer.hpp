#pragma once

#include <cstdint>
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

}  // namespace riscv