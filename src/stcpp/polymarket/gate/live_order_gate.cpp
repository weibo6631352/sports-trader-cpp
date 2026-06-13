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
    auto floor_to = [](std::uint64_t v, std::uint64_t step) noexcept -> std::uint64_t {
        return step == 0 ? v : v - (v % step);
    };

    // CLOB 下单精度/最小额硬约束 (2026-06-13 链路探针实测 CLOB 400 回执坐实, 买卖【相反】, 必须分开):
    //   market BUY : maker(USDC) ≤ 2 位小数 (step 10'000 micro), taker(shares) ≤ 2 位 (step 10'000, 见下保守理由);
    //                且 maker(USDC 名义) ≥ $1 ("invalid amount for a marketable BUY order, min size: 1")。
    //   market SELL: maker(shares) ≤ 2 位 (step 10'000), taker(USDC) ≤ 4 位 (step 100)。SELL 无 $1 门。
    //   ⚠ 旧实现对买卖【套用同一套】(shares 2 位 / USDC 4 位) → 对 SELL 对、对 BUY 全错 (maker=USDC 做成 4 位):
    //     price×shares 只在偶然落 2 位 USDC 的 BUY 才过 (0.50×5=2.50 ✓ / 0.18×6.11=1.0998 ✗) → 便宜/odd 价
    //     market BUY 几乎全被 400 拒 → ExecReport 丢 error → 被当 BernoulliMissed 可重试 → 不入账 → cap 瞎 →
    //     每 tick 重发 (同秒 6 连发风暴, Alan 真钱事故根因)。
    //   marketable 保持: 限价 = maker/taker。BUY 向下圆整 taker(shares) → 限价 ≥ ask; SELL 向下圆整 USDC → 限价 ≤ bid。
    if (req.is_buy) {
        std::uint64_t maker_usdc = floor_to(size_micro, 10'000ULL);    // USDC 付出, 2 位小数
        if (maker_usdc < 1'000'000ULL) maker_usdc = 1'000'000ULL;       // $1 最小名义门 (round-up 凑够, 老板「凑够最小额」)
        // taker(shares) 圆整到【2 位小数】: 2026-06-13 live 实测 CLOB「max accuracy」按市场不一 (US-Iran 盘 5 位 /
        //   网球盘 4 位) → 5 位会被部分市场 400 拒。2 位 = 最保守 (与 SELL shares 同档, 实测可接受); 对任意
        //   「max ≥2 位」市场都合法, 一劳永逸不再赌每盘是 4 还是 5 位。圆整代价 ≤0.01 股 (≪$0.01) 可忽略。
        std::uint64_t taker_shares = 0;                                 // shares 获得, 2 位小数
        if (intent.price > 0.0)
            taker_shares = floor_to(
                static_cast<std::uint64_t>(std::llround(static_cast<double>(maker_usdc) / intent.price)), 10'000ULL);
        if (taker_shares == 0) taker_shares = 10'000ULL;               // price≈0 兜底, 不产 0 量 (最小 0.01 股)
        req.maker_amount = maker_usdc;
        req.taker_amount = taker_shares;
    } else {
        std::uint64_t shares_micro = 0;
        if (intent.price > 0.0)
            shares_micro = static_cast<std::uint64_t>(std::llround(static_cast<double>(size_micro) / intent.price));
        const std::uint64_t shares_2dec = floor_to(shares_micro, 10'000ULL);  // 卖出 shares, 2 位
        std::uint64_t usdc_4dec = floor_to(
            static_cast<std::uint64_t>(std::llround(intent.price * static_cast<double>(shares_2dec))), 100ULL);  // 4 位
        if (usdc_4dec == 0) usdc_4dec = floor_to(size_micro, 100ULL);  // price≈0 兜底
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
