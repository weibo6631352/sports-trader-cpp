// src/stcpp/polymarket/gate/live_order_gate.cpp — 实盘下单闸 (ARM + 翻译 + sink)
//
// Owner: GM (老雷) 2026-05-31 Phase 4; 2026-06-13 简化为 paper 同构 (设计依据见头文件)。
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

    // CLOB 下单精度硬约束 (2026-06-12 都柏林实测 SELL 拒单 "maker max 2 decimals, taker max 4 decimals"):
    //   shares ≤ 2 位小数 (micro 须是 10'000 倍数), USDC ≤ 4 位小数 (micro 须是 100 倍数)。
    //   修法: shares 向下圆整到 2 位 (SELL 避免超卖 / BUY 不夸大目标), USDC 由 price×shares 重算再钉 4 位
    //   → 隐含限价 = intent.price (已是 on-tick 的 ask/bid, 仍 marketable), 两侧精度天然满足。
    //   未修前 daemon 平仓 (持仓常是 6 位小数 odd-lot) 必被 400 拒 → 漏平仓 / 卡死。
    auto floor_to = [](std::uint64_t v, std::uint64_t step) noexcept -> std::uint64_t { return v - (v % step); };
    const std::uint64_t shares_2dec = floor_to(shares_micro, 10'000ULL);  // 2 位小数
    std::uint64_t usdc_4dec =
        floor_to(static_cast<std::uint64_t>(std::llround(intent.price * static_cast<double>(shares_2dec))), 100ULL);
    if (usdc_4dec == 0) usdc_4dec = floor_to(size_micro, 100ULL);  // price≈0 兜底, 不产 0 额单

    if (req.is_buy) {
        req.maker_amount = usdc_4dec;     // USDC 付出 (=price×shares, 4 位)
        req.taker_amount = shares_2dec;   // shares 获得 (2 位)
    } else {
        req.maker_amount = shares_2dec;   // shares 卖出 (2 位)
        req.taker_amount = usdc_4dec;     // USDC 获得 (4 位)
    }
    return req;
}

GateResult LiveOrderGate::Submit(const stcpp::risk::OrderIntent& intent, bool neg_risk) noexcept {
    GateResult gr;
    // ARM 总闸 (fail-closed)。未开闸绝不下单。§8 RM 闸在上游决策环 (见头文件)。
    if (!armed_) {
        gr.gate_block = GateBlock::DISARMED;
        return gr;
    }
    const LiveOrderRequest req = TranslateIntent(intent, neg_risk);
    if (sink_) {
        gr.order = sink_(req);
        gr.submitted = true;
    }
    return gr;
}

}  // namespace stcpp::polymarket
