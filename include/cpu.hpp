#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>

#include "cpu_state.hpp"

namespace riscv {

class CPU {
public:                   
    CPUState cur;
    CPUState next;

    // 内存
    std::map<uint32_t, uint8_t> memory;

    CPU();

    // 加载 .data 格式机器指令到内存
    bool load_program(std::istream& input);
    uint8_t run();

    // 乱序验证，flush / merge 钉尾，cycles_out 返回总周期数
    uint8_t run_with_order(uint32_t shuffle_seed, uint64_t& cycles_out);

private:
    /// 是否有空闲 RS 槽位
    bool rs_has_free(const CPUState& s) const;
    /// ROB 是否满
    bool rob_is_full(const CPUState& s) const;
    /// LSQ 是否有空闲槽位
    bool lsq_has_free(const CPUState& s) const;
    /// ROB 是否为空
    bool rob_empty(const CPUState& s) const;

    // ------------------------------------每周期至多调用一次的操作（port）：
    // issue（取指+解码+分配 RS/ROB）
    void issue(const CPUState& c, CPUState& n, ConflictDeltas& d);

    void execute(const CPUState& c, CPUState& n);

    // CDB 选择：从完成结果中选一个广播，返回本周期广播信号
    CdbSignal cdb_arbitrate(const CPUState& c, CPUState& n);

    // 广播到 RS/LSQ/ROB
    void cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig);

    // ROB 提交：顺序提交一条指令；reg_status 清除与误预测 PC 填增量 d
    void commit(const CPUState& c, CPUState& n, ConflictDeltas& d);

    // Flush 流水线
    void flush_pipeline(const CPUState& c, CPUState& n, ConflictDeltas& d);

    // reg_status / pc 冲突
    // reg_status —— flush 重建 > issue 重命名 > commit 清除
    // pc —— 误预测恢复 > issue 预测 next_pc
    void merge_conflict(const CPUState& c, CPUState& n, const ConflictDeltas& d);
};

}  // namespace riscv