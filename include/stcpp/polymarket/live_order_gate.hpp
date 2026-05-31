// include/stcpp/polymarket/live_order_gate.hpp — 实盘下单强制风控门 (mode-agnostic)
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 D-A (评审会全员共识第一步)。
//
// 红线 §8 第一条落地: 任何下单必经 RiskManager。LiveOrderGate 是策略到 CLOB 的**唯一通路**:
//   策略只持有 LiveOrderGate, 拿不到裸 LiveOrderSubmitter。Submit 内部强制先 RM.evaluate(),
//   **仅 APPROVED 才翻译成 LiveOrderRequest 调下单 sink; REJECTED 绝不触达 sink。**
//
// 下单 sink 注入 (OrderSinkFn): 生产绑 LiveOrderSubmitter::Submit; 单测注入 stub。
//   → gate 逻辑可在 paper ctest 离线验证 (RM 拒 → sink 零调用)。
//
// 老周 C-1: gate 只接 is_approved() 的 intent, 内部不重决策、不改 size/price。
#pragma once

#include "stcpp/polymarket/live_order_types.hpp"
#include "stcpp/risk/risk_gateway.hpp"

#include <functional>

namespace stcpp::polymarket {

// 下单出口 (生产 = LiveOrderSubmitter::Submit; 测试 = stub)。
using OrderSinkFn = std::function<LiveOrderResult(const LiveOrderRequest&)>;

struct GateResult {
    bool submitted{false};   // 是否真触达 CLOB
    bool rm_approved{false};
    // 仅 rm_approved=false 时有意义 (默认值同 RiskDecision)。
    stcpp::risk::RejectCode reject_code{stcpp::risk::RejectCode::INTERNAL_ERROR};
    LiveOrderResult order;   // 仅 submitted 时有效
};

class LiveOrderGate {
public:
    // rm: 真实风控门 (调用方持有, gate 不拥有)。sink: 下单出口。
    LiveOrderGate(stcpp::risk::RiskGateway& rm, OrderSinkFn sink) noexcept
        : rm_(rm), sink_(std::move(sink)) {}

    // 强制 RM → 仅 APPROVED 才下单。neg_risk 来自市场目录 (gate 不臆造)。
    [[nodiscard]] GateResult Submit(const stcpp::risk::OrderIntent& intent, bool neg_risk) noexcept;

private:
    stcpp::risk::RiskGateway& rm_;
    OrderSinkFn sink_;
};

// intent → LiveOrderRequest 翻译 (size_pUSD_micro + price → maker/taker micro)。
//   BUY : maker=size_pUSD_micro(USDC), taker=round(size/price)(shares)
//   SELL: maker=round(size/price)(shares), taker=size_pUSD_micro(USDC)
// 单独导出供测试。
[[nodiscard]] LiveOrderRequest TranslateIntent(const stcpp::risk::OrderIntent& intent, bool neg_risk) noexcept;

}  // namespace stcpp::polymarket
