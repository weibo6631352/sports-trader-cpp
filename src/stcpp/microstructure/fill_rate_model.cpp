// stcpp/microstructure/fill_rate_model.cpp — FillRateModel v0.2 实现
//
// 落: xiaoyuan-fill-rate-model-v0.1.md (本 lib spec v0.1)
//     xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md §2.1 (slippage ~0.3%)
// 红线: 见 fill_rate_model.hpp 头注 (R-7 / R-11 / R-20 / R-1)

#include "stcpp/microstructure/fill_rate_model.hpp"

#include <algorithm>
#include <cmath>

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
    if (!detail::finite_pos(bb.price) || !detail::finite_pos(ba.price)
        || !detail::finite_pos(bb.size_usdc) || !detail::finite_pos(ba.size_usdc)) {
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
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

}  // namespace

// ---------------------------------------------------------------------------
// Maker 路径 (默认)
// ---------------------------------------------------------------------------
FillRateOutput FillRateModel::compute_maker(OrderBookSnapshot const& book,
                                            Microprobe       const& probe,
                                            FillIntent       const& intent) noexcept {
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
            effective_depth = depth_within_ticks(book.ask, book.ask[0].price,
                                                 book.tick_size, TICK_WINDOW);
        } else {
            effective_depth = depth_within_ticks(book.bid, book.bid[0].price,
                                                 book.tick_size, TICK_WINDOW);
        }
    }
    double const depth_ratio = effective_depth / intent.size_usdc;
    double const base_rate   = clamp_to_range(depth_ratio, BASE_MIN, BASE_MAX);
    out.breakdown.base_rate  = base_rate;

    // === Step 2: penalties ===
    out.breakdown.qhl_penalty = (probe.quote_half_life_ms > 0
                                 && probe.quote_half_life_ms < QHL_THRESHOLD_MS)
                                ? PENALTY_QHL_SHORT : 0.0;

    out.breakdown.spread_penalty = (book.spread_bps > SPREAD_WIDE_BPS)
                                    ? PENALTY_SPREAD_WIDE : 0.0;

    out.breakdown.adverse_penalty = (probe.adverse_selection_score > AS_THRESHOLD)
                                     ? PENALTY_ADVERSE_SELECT : 0.0;

    // Time decay: 仅 Late phase 触发 (公式 §2 time_decay)
    auto const& prof = profile_of(intent.sport, intent.phase);
    out.breakdown.time_decay_penalty = (intent.phase == InplayPhase::Late)
                                        ? prof.late_decay_penalty : 0.0;

    out.breakdown.sum_penalties = -(out.breakdown.qhl_penalty
                                  + out.breakdown.spread_penalty
                                  + out.breakdown.adverse_penalty
                                  + out.breakdown.time_decay_penalty);

    // === Step 3: sport_bias = profile.base_fill_rate - 0.65 ===
    out.breakdown.sport_bias = prof.base_fill_rate - PROFILE_BIAS_PIVOT;

    // === Step 4: 合成 + clamp [0, 1] ===
    double const pre = base_rate + out.breakdown.sum_penalties + out.breakdown.sport_bias;
    out.breakdown.pre_clamp = pre;
    out.fill_rate           = clamp_unit(pre);
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
                                            FillIntent        const& intent) noexcept {
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

    out.breakdown.base_rate  = base;
    out.breakdown.spread_penalty = (spread_adj < 0.0) ? -spread_adj : 0.0;
    out.breakdown.sum_penalties  = spread_adj;
    out.breakdown.pre_clamp  = base + spread_adj;
    out.fill_rate            = clamp_unit(out.breakdown.pre_clamp);
    out.breakdown.final_rate = out.fill_rate;
    out.reject               = (out.fill_rate < FILL_RATE_FLOOR)
                                ? FillRateReject::BelowFloor : FillRateReject::Ok;
    return out;
}

// ---------------------------------------------------------------------------
// compute_from_clob_book — Wave 3 v0.2: 真实 CLOB 多档 depth queue position fill
// ---------------------------------------------------------------------------
ClobFillOutput FillRateModel::compute_from_clob_book(
    OrderBookSnapshot const& book,
    Microprobe        const& probe,
    FillIntent        const& intent) noexcept {

    ClobFillOutput out;
    out.ts = book.ts;

    // === 校验 ===
    if (auto r = validate_snapshot(book); r != FillRateReject::Ok) {
        out.reject = r;
        return out;
    }
    if (auto r = validate_intent(intent); r != FillRateReject::Ok) {
        out.reject = r;
        return out;
    }

    // === Step 1: 找目标价档 (intent.price ± 1 tick) 的累计深度 ===
    //
    // Polymarket tick = 0.01; Buy 方向看 ask 侧 (maker 挂单等对手 taker 来吃),
    // Sell 方向看 bid 侧.
    //
    auto const& target_side = (intent.side == Side::Buy) ? book.ask : book.bid;
    double const price_band  = book.tick_size + 1e-9;  // ±1 tick 容差
    double queue_depth = 0.0;
    for (auto const& lvl : target_side) {
        if (!detail::finite_pos(lvl.price) || !detail::finite_pos(lvl.size_usdc)) continue;
        double const d = lvl.price - intent.price;
        double const ad = (d < 0.0) ? -d : d;
        if (ad <= price_band) {
            queue_depth += lvl.size_usdc;
        }
    }
    // 若目标价档完全无流动性 (价格不在盘口) → 使用 best_ask/best_bid 作为 fallback
    if (queue_depth < 1.0) {
        auto const& best_lvl = (intent.side == Side::Buy) ? book.ask[0] : book.bid[0];
        queue_depth = detail::finite_pos(best_lvl.size_usdc) ? best_lvl.size_usdc : 1.0;
    }
    out.queue_depth_at_price = queue_depth;

    // === Step 2: queue position ratio + p_queue_fill ===
    //
    // queue_position_ratio = intent.size_usdc / queue_depth_at_price
    // p_queue_fill = exp(-queue_position_ratio)   — 指数衰减建模排队等待
    //   ratio=0.5 → p~0.61  (排在前半截, 成交概率较高)
    //   ratio=1.0 → p~0.37  (占满整档, 中等)
    //   ratio=2.0 → p~0.14  (超过整档 2 倍, 低)
    //
    double const ratio = intent.size_usdc / queue_depth;
    out.queue_position_ratio = ratio;
    double const p_queue = std::exp(-ratio);
    out.p_queue_fill = p_queue;

    // === Step 3: top3 深度调节 ===
    //
    // fill_rate_base = p_queue × (top3_depth / (top3_depth + intent_size))
    // — top3 越厚, 对 fill_rate 的调节影响越小 (当 top3 >> intent_size 时接近 p_queue)
    // — top3 薄时 (top3 ~ intent_size), fill_rate 额外折扣防虚高
    //
    double const top3 = (book.top3_depth_usdc > 0.0) ? book.top3_depth_usdc
                        : depth_within_ticks((intent.side == Side::Buy) ? book.ask : book.bid,
                                             intent.price, book.tick_size, TICK_WINDOW);
    double const top3_adj = (top3 > 0.0) ? (top3 / (top3 + intent.size_usdc)) : 0.5;
    double const fill_rate_base = clamp_to_range(p_queue * top3_adj, BASE_MIN, BASE_MAX);

    // === Step 4: slippage 估算 (保守, 小程 §2.1 pregame ~0.3%) ===
    //
    // rho = intent.size_usdc / best_L1_depth   (与 SlippageModel 同定义)
    // slippage_rate ≈ rho × tick_size / intent.price × 0.5
    //   — 上限 0.5% (防极端 book 虚高)
    //   — 取 tick/price × rho × 0.5: 近 even market price~0.5, tick=0.01
    //     rho=0.5 → slip ≈ 0.5×0.01/0.5×0.5 = 0.005 → clamp → 0.003 (0.3%)
    //
    auto const& best_l1 = (intent.side == Side::Buy) ? book.ask[0] : book.bid[0];
    double const l1_size = detail::finite_pos(best_l1.size_usdc) ? best_l1.size_usdc : 1.0;
    double const rho     = intent.size_usdc / l1_size;
    double const ref_price = detail::finite_pos(intent.price) ? intent.price : 0.5;
    double const slip_raw  = rho * book.tick_size / ref_price * 0.5;
    // 保守上限 0.5% (5 tick), 下限 0.001% (noise floor)
    double const slip_clamped = clamp_to_range(slip_raw, 0.00001, 0.005);
    out.slippage_rate = slip_clamped;
    auto const slip_bps_raw = static_cast<double>(slip_clamped * 10'000.0);
    out.slippage_bps = static_cast<std::int32_t>(slip_bps_raw + 0.5);

    // === Step 5: penalty 叠加 (与 v0.1 逻辑一致) ===
    //
    // spread_penalty: 宽价差增加 queue 等待风险
    double const sp = (book.spread_bps > SPREAD_WIDE_BPS) ? PENALTY_SPREAD_WIDE : 0.0;
    out.spread_penalty = sp;

    // adverse_selection_penalty: 盘口快速移动对手方 → queue 跟不上
    double const ap = (probe.adverse_selection_score > AS_THRESHOLD) ? PENALTY_ADVERSE_SELECT : 0.0;
    out.adverse_penalty = ap;

    // time_decay: Late phase 流动性枯竭
    auto const& prof = profile_of(intent.sport, intent.phase);
    double const tp = (intent.phase == InplayPhase::Late) ? prof.late_decay_penalty : 0.0;
    out.time_decay_penalty = tp;

    // sport_bias: CLOB 模式不依赖 sport profile base_fill_rate (已通过真实 depth 建模),
    // 但保留轻微 bias 修正 (-0.10~+0.10 范围), 防 esports 等超薄市场虚高
    double const sb = (prof.base_fill_rate - PROFILE_BIAS_PIVOT) * 0.5;
    out.sport_bias = sb;

    out.sum_penalties = -(sp + ap + tp) + sb;

    // === Step 6: 合成 fill_rate ===
    double const pre = fill_rate_base + out.sum_penalties;
    out.fill_rate = clamp_unit(pre);

    // BelowFloor 不强拒, 标记给 caller (R-1)
    if (out.fill_rate < FILL_RATE_FLOOR) {
        out.reject = FillRateReject::BelowFloor;
    }
    return out;
}

}  // namespace stcpp::microstructure
