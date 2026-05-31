// src/stcpp/polymarket/gate/live_order_gate.cpp — 实盘下单强制风控门
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 D-A。
#include "stcpp/polymarket/live_order_gate.hpp"

#include <cmath>

namespace stcpp::polymarket {

LiveOrderRequest TranslateIntent(const stcpp::risk::OrderIntent& intent, bool neg_risk) noexcept {
    LiveOrderRequest req;
    req.token_id = intent.token_id;
    req.is_buy = (intent.side == stcpp::risk::Side::Buy);
    req.neg_risk = neg_risk;
    req.signature_type = 1;  // POLY_PROXY
    req.order_type = "FOK";  // MVP 锁 FOK (老周 C-3)

    const std::uint64_t size_micro =
        intent.size_pUSD_micro > 0 ? static_cast<std::uint64_t>(intent.size_pUSD_micro) : 0;
    std::uint64_t shares_micro = 0;
    if (intent.price > 0.0)
        shares_micro = static_cast<std::uint64_t>(std::llround(static_cast<double>(size_micro) / intent.price));

    if (req.is_buy) {
        req.maker_amount = size_micro;    // USDC 付出
        req.taker_amount = shares_micro;  // shares 获得
    } else {
        req.maker_amount = shares_micro;  // shares 卖出
        req.taker_amount = size_micro;    // USDC 获得
    }
    return req;
}

GateResult LiveOrderGate::Submit(const stcpp::risk::OrderIntent& intent, bool neg_risk) noexcept {
    GateResult gr;
    // 红线 §8: 任何下单必经 RM。先评估。
    const stcpp::risk::RiskDecision rd = rm_.evaluate(intent);
    gr.rm_approved = rd.is_approved();
    gr.reject_code = rd.reject;
    // REJECTED / DEFERRED 一律不下单 (fail-closed: 仅 APPROVED 放行)。
    if (!rd.is_approved())
        return gr;

    const LiveOrderRequest req = TranslateIntent(intent, neg_risk);
    if (sink_) {
        gr.order = sink_(req);
        gr.submitted = true;
    }
    return gr;
}

}  // namespace stcpp::polymarket
