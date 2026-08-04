#pragma once

#include <cstdint>

namespace riscv {

/// CDB 广播信号（每周期至多一条）
struct CdbSignal {
    bool valid = false;
    int32_t rs_tag = -1;       // 生产者 RS 槽位 tag（仅用于广播后释放该槽位）
    int32_t rob_idx = -1;      // 唤醒 / 捕获 tag：RS/LSQ 的 q1/q2 与 ROB 均按此匹配
    uint32_t value = 0;        // 计算结果

    // 控制流指令相关（B/J/JALR 有效；J/JALR 恒 taken）
    bool is_branch = false;
    bool branch_taken = false;
    uint32_t branch_target = 0;
};

struct CPUState;

// --- 组合线方法（只读 cur、写 next；每周期至多调用一次，见 DESIGN.md §5.4/§7/§9） ---

/// CDB 仲裁：从 RS.done ∪ LSQ load 完成项中选 ROB 程序序最早（age 最小，§5.3）者
/// 生成本周期广播信号，并同周期释放被选中生产者的槽位（写 next）：
///   - 算术 / 分支：释放自身 RS 槽（下一周期才对 issue 可见）
///   - load 直通：释放关联 RS 槽 + LSQ 槽
/// 返回的 CdbSignal 直接喂给 cdb_capture（同周期仲裁→捕获，不跨周期保留）
CdbSignal cdb_arbitrate(const CPUState& c, CPUState& n);

/// CDB 广播捕获：以仲裁信号为输入，广播到 RS / LSQ（q1/q2 匹配 sig.rob_idx → 捕获
/// value 清 q）与 ROB（rob_idx 匹配 → ready/value/分支元数据捕获）。
/// 三个子模块各自只读 cur、可交换（见 DESIGN.md §5.4）
void cdb_capture(const CPUState& c, CPUState& n, const CdbSignal& sig);

}  // namespace riscv
