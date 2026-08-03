#pragma once

#include <cstdint>
#include <string>

#include "cpu_state.hpp"

namespace riscv {

class CPU {
public:
    // cur = 当前周期状态，next = 下一周期状态
    CPUState cur;
    CPUState next;

    CPU();

    bool load_program(const std::string& path);
    uint8_t run();

private:
    /// 是否有空闲 RS 槽位
    bool rs_has_free(const CPUState& s) const;
    /// ROB 是否满
    bool rob_is_full(const CPUState& s) const;
    /// LSU 是否有空闲槽位接受新访存
    bool lsu_has_free(const CPUState& s) const;
    /// ROB 是否为空
    bool rob_empty(const CPUState& s) const;

    // --- 每周期至多调用一次的操作（port） ---
    /// 发射一条指令（取指+解码+分配 RS/ROB）
    void issue(const CPUState& c, CPUState& n);

    /// 执行阶段：RS 就绪项执行、LSU 推进
    void execute(const CPUState& c, CPUState& n);

    /// CDB 仲裁：从完成结果中选一个广播
    void cdb_arbitrate(const CPUState& c, CPUState& n);

    /// CDB 广播捕获：所有 RS 槽位 + ROB 捕获 CDB 结果
    void cdb_capture(const CPUState& c, CPUState& n);

    /// ROB 提交：顺序提交一条指令
    void commit(const CPUState& c, CPUState& n);

    /// Flush 流水线（分支预测失败时）
    void flush_pipeline(const CPUState& c, CPUState& n, uint32_t target_pc);
};

}  // namespace riscv