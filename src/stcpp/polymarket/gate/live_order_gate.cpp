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
    // 门1: ARM (fail-closed)。未开闸绝不下单, 也不进 RM。
    if (!armed_) {
        gr.gate_block = GateBlock::DISARMED;
        return gr;
    }
    // 门2: OrderRateCap (老韩新红线, 频控机器连发)。
    if (orders_today_ >= cfg_.max_orders_per_day) {
        gr.gate_block = GateBlock::RATE_CAP;
        return gr;
    }
    // 门3: 红线 §8 — 任何下单必经 RM。
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
        ++orders_today_;  // 仅真触达 CLOB 才计数
    }
    return gr;
}

stcpp::risk::RiskConfig MakeGrayLaunchRiskConfig() noexcept {
    using stcpp::domain::MicroPUSD;
    stcpp::risk::RiskConfig c;
    c.per_order_cap_usdc = MicroPUSD::from_pusd(1.0);        // $1
    c.market_exposure_cap_usdc = MicroPUSD::from_pusd(2.0);  // $2 per-condition
    c.per_outcome_cap_usdc = MicroPUSD::from_pusd(2.0);      // $2 per-outcome
    c.bankroll_usdc = MicroPUSD::from_pusd(25.0);            // $25 (绝不复用 paper 100k)
    c.daily_loss_halt_usdc = MicroPUSD::from_pusd(5.0);      // $5 日亏硬 kill
    c.consec_loss_halt_count = 3;                            // 灰度收紧 (默认 5)
    return c;
}

}  // namespace stcpp::polymarket
