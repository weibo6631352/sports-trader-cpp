// include/stcpp/polymarket/live_executor.hpp — 实盘执行器 (决策→gate→中性成交事件)
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 接线 (执行器层, 老周 G-1 executor 注入 / G-2 中性 fill)。
//
// 完整 live 执行链: OrderIntent(RM-approved) → LiveExecutor.Execute → LiveOrderGate
//   (arm/ratecap/RM) → LiveOrderSubmitter → 回执 → **中性 ExecReport**。
//
// ExecReport 是 paper/live 共用的中性成交事件 (FillEvent 中性化思路): 未来 VirtualExecutor
//   也产同型 ExecReport, 账本只消费一种中性类型, 与 VirtualFill 解耦。
//
// 老韩硬要求: filled_* 取自 CLOB 回执实际成交量 (making/taking), **非请求量** —— 否则 RM
//   exposure 喂假数。FOK: matched=全成 / 否则不成交不写账本。
#pragma once

#include "stcpp/polymarket/live_order_gate.hpp"

#include <string>

namespace stcpp::polymarket {

// 中性成交事件 (账本/RM 回写源; paper 与 live 共用形态)。
struct ExecReport {
    bool submitted{false};                  // 是否触达 CLOB
    bool rm_approved{false};
    bool filled{false};                     // 是否成交 (FOK matched)
    GateBlock gate_block{GateBlock::NONE};   // gate 层拦截 (DISARMED/RATE_CAP)
    // 实际成交 (回执真相, 非请求量):
    double filled_usdc{0.0};                // BUY:实付 USDC / SELL:实得 USDC
    double filled_shares{0.0};              // BUY:实得 shares / SELL:实卖 shares
    double fill_price{0.0};                 // = filled_usdc / filled_shares
    std::string order_id;
    std::string tx_hash;
};

class LiveExecutor {
public:
    explicit LiveExecutor(LiveOrderGate& gate) noexcept : gate_(gate) {}

    // 入参须为 RM-approved 的 intent (老周 C-1; gate 内部仍会再过 RM, 双保险)。
    [[nodiscard]] ExecReport Execute(const stcpp::risk::OrderIntent& approved_intent, bool neg_risk) noexcept;

private:
    LiveOrderGate& gate_;
};

}  // namespace stcpp::polymarket
