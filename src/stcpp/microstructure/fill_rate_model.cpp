// stcpp/microstructure/fill_rate_model.cpp — FillRateModel v0.1 实现
//
// 落: xiaoyuan-fill-rate-model-v0.1.md (本 lib spec)
// 红线: 见 fill_rate_model.hpp 头注 (R-7 / R-11 / R-20 / R-1)

#include "stcpp/microstructure/fill_rate_model.hpp"

#include <algorithm>

namespace stcpp::microstructure {

namespace {

// 校验 OrderBookSnapshot — fail-closed: 任何异常返 InvalidSnapshot.
[[nodiscard]] FillRateReject validate_snapshot(OrderBookSnapshot const& b) noexcept {
    if (!ts_all_positive(b.ts) || !ts_order_ok(b.ts)) {
        return FillRateReject::InvalidSnapshot;
    }
    if (!detail::finite_pos(b.tick_size)) {
        return FillRateReject::InvalidSnapshot;
    }
    // best_bid + best_ask 必须 finite_pos 且 ask > bid
    auto const& bb = b.bid[0];
    auto const& ba = b.ask[0];
    if (!detail::finite_pos(bb.price) || !detail::finite_pos(ba.price) || !detail::finite_pos(bb.size_usdc) ||
        !detail::finite_pos(ba.size_usdc)) {
        return FillRateReject::InvalidSnapshot;
    }
    if (ba.price <= bb.price) {
        // crossed / locked
        return FillRateReject::InvalidSnapshot;
    }
    if (!detail::finite(b.top3_depth_usdc) || b.top3_depth_usdc < 0.0) {
        return FillRateReject::InvalidSnapshot;
    }
    return FillRateReject::Ok;
}

[[nodiscard]] FillRateReject validate_intent(FillIntent const& it) noexcept {
    if (!detail::finite_pos(it.size_usdc) || it.size_usdc <= 0.0) {
        return FillRateReject::InvalidIntent;
    }
    if (!detail::finite(it.price) || it.price <= 0.0 || it.price >= 1.0) {
        return FillRateReject::InvalidIntent;
    }
    return FillRateReject::Ok;
}

[[nodiscard]] inline double clamp_to_range(double x, double lo, double hi) noexcept {
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

}  // namespace

// ---------------------------------------------------------------------------
// Maker 路径 (默认)
// ---------------------------------------------------------------------------
FillRateOutput FillRateModel::compute_maker(OrderBookSnapshot const& book, Microprobe const& probe,
                                            FillIntent const& intent) noexcept {
    FillRateOutput out;
    out.ts = book.ts;

    if (auto r = validate_snapshot(book); r != FillRateReject::Ok) {
        out.reject = r;
        return out;
    }
    if (auto r = validate_intent(intent); r != FillRateReject::Ok) {
        out.reject = r;
        return out;
    }

    // === Step 1: base_fill_rate = quoted_depth_within_2_ticks / intent_size ===
    //
    // 注: book.top3_depth_usdc 在 caller 已计算 ±2 tick 累计 (§1.2 实测主参考).
    // 若 caller 未填 (=0), 现场算: depth_within_ticks(side, ref_price, tick, 2)
    double effective_depth = book.top3_depth_usdc;
    if (effective_depth <= 0.0) {
        // 现场 fallback (使用 best ask/bid 作为 ref_price)
        if (intent.side == Side::Buy) {
            effective_depth = depth_within_ticks(book.ask, book.ask[0].price, book.tick_size, TICK_WINDOW);
        } else {
            effective_depth = depth_within_ticks(book.bid, book.bid[0].price, book.tick_size, TICK_WINDOW);
        }
    }
    double const depth_ratio = effective_depth / intent.size_usdc;
    double const base_rate = clamp_to_range(depth_ratio, BASE_MIN, BASE_MAX);
    out.breakdown.base_rate = base_rate;

    // === Step 2: penalties ===
    out.breakdown.qhl_penalty = (probe.quote_half_life_ms > 0 && probe.quote_half_life_ms < QHL_THRESHOLD_MS)
                                    ? PENALTY_QHL_SHORT
                                    : 0.0;

    out.breakdown.spread_penalty = (book.spread_bps > SPREAD_WIDE_BPS) ? PENALTY_SPREAD_WIDE : 0.0;

    out.breakdown.adverse_penalty =
        (probe.adverse_selection_score > AS_THRESHOLD) ? PENALTY_ADVERSE_SELECT : 0.0;

    // Time decay: 仅 Late phase 触发 (公式 §2 time_decay)
    auto const& prof = profile_of(intent.sport, intent.phase);
    out.breakdown.time_decay_penalty = (intent.phase == InplayPhase::Late) ? prof.late_decay_penalty : 0.0;

    out.breakdown.sum_penalties = -(out.breakdown.qhl_penalty + out.breakdown.spread_penalty +
                                    out.breakdown.adverse_penalty + out.breakdown.time_decay_penalty);

    // === Step 3: sport_bias = profile.base_fill_rate - 0.65 ===
    out.breakdown.sport_bias = prof.base_fill_rate - PROFILE_BIAS_PIVOT;

    // === Step 4: 合成 + clamp [0, 1] ===
    double const pre = base_rate + out.breakdown.sum_penalties + out.breakdown.sport_bias;
    out.breakdown.pre_clamp = pre;
    out.fill_rate = clamp_unit(pre);
    out.breakdown.final_rate = out.fill_rate;

    // === Step 5: BelowFloor 不强拒, 标记给 caller (R-1: 拒由 RM) ===
    if (out.fill_rate < FILL_RATE_FLOOR) {
        out.reject = FillRateReject::BelowFloor;
    } else {
        out.reject = FillRateReject::Ok;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Taker 路径 — 穿价立即成交, fill_rate ~ 1.0 (留 cancel race 余量)
// ---------------------------------------------------------------------------
FillRateOutput FillRateModel::compute_taker(OrderBookSnapshot const& book,
                                            FillIntent const& intent) noexcept {
    FillRateOutput out;
    out.ts = book.ts;

    if (auto r = validate_snapshot(book); r != FillRateReject::Ok) {
        out.reject = r;
        return out;
    }
    if (auto r = validate_intent(intent); r != FillRateReject::Ok) {
        out.reject = r;
        return out;
    }

    // Taker fill_rate = 1 - cancel_race_factor
    //   cancel_race_factor 简单建模: top-of-book size 比 intent_size 越小, 越容易被前面的 taker 抢走
    //   intent_size <= L1 → 0.95;  intent_size > L1 → 0.85 (multi-level taker)
    //   spread > 50 bps → 额外 -0.05 (宽 spread maker 易撤)
    auto const& top = (intent.side == Side::Buy) ? book.ask[0] : book.bid[0];
    double const base = (intent.size_usdc <= top.size_usdc) ? 0.95 : 0.85;
    double const spread_adj = (book.spread_bps > SPREAD_WIDE_BPS) ? -0.05 : 0.0;

    out.breakdown.base_rate = base;
    out.breakdown.spread_penalty = (spread_adj < 0.0) ? -spread_adj : 0.0;
    out.breakdown.sum_penalties = spread_adj;
    out.breakdown.pre_clamp = base + spread_adj;
    out.fill_rate = clamp_unit(out.breakdown.pre_clamp);
    out.breakdown.final_rate = out.fill_rate;
    out.reject = (out.fill_rate < FILL_RATE_FLOOR) ? FillRateReject::BelowFloor : FillRateReject::Ok;
    return out;
}

}  // namespace stcpp::microstructure
