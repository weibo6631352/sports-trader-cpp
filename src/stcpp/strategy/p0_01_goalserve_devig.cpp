// src/stcpp/strategy/p0_01_goalserve_devig.cpp — P0-01 Goalserve multiplicative de-vig impl v0.1
//
// ADR-008 multiplicative de-vig (Wave 28, 小卢 IC pool E-035-02).
// 替换 W4 Pinnacle 锚源 (p0_01_pinnacle_no_vig.cpp).
//
// 红线:
//   R-20    SignalContext 4 ts + feature_snapshot_id 必带
//   ADR-008 multiplicative only (~30 行核心), 不上 Shin
//   老周 ABI lock: SignalContext / SignalOutput 不动
//   小邓 ML-R6: W6 paper 数据收集, M2 后 shadow

#include "stcpp/strategy/p0_01_goalserve_devig.hpp"

#include <cmath>
#include <utility>

namespace stcpp::strategy {

namespace {

[[nodiscard]] bool is_finite_pos(double x) noexcept {
    // 正有限值 (排除 NaN / Inf / 0 / 负)
    return (x > 0.0) && (x < 1e300);
}

[[nodiscard]] double clamp01(double x) noexcept {
    if (x < 0.0)
        return 0.0;
    if (x > 1.0)
        return 1.0;
    return x;
}

[[nodiscard]] std::int32_t round_to_bps(double edge) noexcept {
    double const raw = edge * 10'000.0;
    double const adj = raw >= 0.0 ? raw + 0.5 : raw - 0.5;
    return static_cast<std::int32_t>(adj);
}

}  // namespace

// ---------------------------------------------------------------------------
// compute_multiplicative_devig — ADR-008 核心算法
//
// 迭代 bookmakers span:
//   - 跳过 odds_yes / odds_no <= 0 的行 (数据缺失)
//   - overround_i = p_yes_raw_i + p_no_raw_i
//   - p_yes_fair_i = p_yes_raw_i / overround_i
//   - 累加 sum(p_yes_fair_i) + sum(overround_i) → 等权均值
//   - count < 3 → fallback valid=false, p_yes_fair_avg=0
// ---------------------------------------------------------------------------
DevigResult compute_multiplicative_devig(std::span<const BookmakerOdds> bookmakers) noexcept {
    DevigResult r;
    double sum_fair = 0.0;
    double sum_overround = 0.0;
    std::size_t count = 0;

    for (auto const& bm : bookmakers) {
        if (!is_finite_pos(bm.odds_yes) || !is_finite_pos(bm.odds_no)) {
            continue;
        }
        double const p_yes_raw = 1.0 / bm.odds_yes;
        double const p_no_raw = 1.0 / bm.odds_no;
        double const overround = p_yes_raw + p_no_raw;
        if (!is_finite_pos(overround)) {
            continue;
        }
        double const p_yes_fair = p_yes_raw / overround;
        sum_fair += p_yes_fair;
        sum_overround += overround;
        ++count;
    }

    r.books_used = count;
    if (count < MIN_BOOKMAKERS) {
        // 小梁 P1: < 3 家有效 bookmaker → fallback, 不出 fair value
        return r;
    }

    double const n = static_cast<double>(count);
    r.p_yes_fair_avg = sum_fair / n;
    r.overround_avg = sum_overround / n;
    r.valid = (r.p_yes_fair_avg > 0.0 && r.p_yes_fair_avg < 1.0);
    return r;
}

// ---------------------------------------------------------------------------
// MockGoalserveOddsSource
// ---------------------------------------------------------------------------

void MockGoalserveOddsSource::put(std::string market_id, std::vector<BookmakerOdds> odds) {
    book_.insert_or_assign(std::move(market_id), std::move(odds));
}

bool MockGoalserveOddsSource::lookup(std::string const& market_id,
                                     std::vector<BookmakerOdds>& out) const noexcept {
    auto const it = book_.find(market_id);
    if (it == book_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

// ---------------------------------------------------------------------------
// GoalserveDevigSignal
// ---------------------------------------------------------------------------

GoalserveDevigSignal::GoalserveDevigSignal(IGoalserveOddsSource const& goalserve, IPmSnapshotSource const& pm,
                                           IGameStateSource const& games, std::int64_t bankroll_usdc) noexcept
    : goalserve_(goalserve), pm_(pm), games_(games), bankroll_usdc_(bankroll_usdc) {}

bool GoalserveDevigSignal::validate_context_(SignalContext const& ctx) const noexcept {
    // R-20: 4 ts 全 > 0 且单调非降, feature_snapshot_id 非空, market_id 非空.
    if (ctx.event_ts_ns <= 0 || ctx.data_source_ts_ns <= 0 || ctx.ingestion_ts_ns <= 0 ||
        ctx.as_of_ts_ns <= 0) {
        return false;
    }
    if (ctx.event_ts_ns > ctx.data_source_ts_ns || ctx.data_source_ts_ns > ctx.ingestion_ts_ns ||
        ctx.ingestion_ts_ns > ctx.as_of_ts_ns) {
        return false;
    }
    if (ctx.market_id.empty() || ctx.feature_snapshot_id.empty()) {
        return false;
    }
    return true;
}

std::optional<SignalOutput> GoalserveDevigSignal::tick(SignalContext const& ctx) noexcept {
    // R-20 PIT 闸门
    if (!validate_context_(ctx)) {
        return std::nullopt;
    }

    // Goalserve 多 bookmaker 报价 lookup (缺 → nullopt, fail-closed)
    std::vector<BookmakerOdds> bm_odds;
    if (!goalserve_.lookup(ctx.market_id, bm_odds)) {
        return std::nullopt;
    }

    // ADR-008 multiplicative de-vig
    DevigResult const dv = compute_multiplicative_devig(bm_odds);
    if (!dv.valid) {
        // books_used < 3 或 p_yes_fair 不在 (0,1) → nullopt
        return std::nullopt;
    }

    // PM snapshot lookup
    PmSnapshot pm{};
    if (!pm_.lookup(ctx.market_id, pm) || !pm.valid) {
        return std::nullopt;
    }

    // GameState lookup
    GameState g{};
    if (!games_.lookup(ctx.market_id, g)) {
        return std::nullopt;
    }

    // PM mid sanity (∈ (0, 1) 严格)
    if (pm.mid <= PM_MID_EPS || pm.mid >= 1.0 - PM_MID_EPS) {
        return std::nullopt;
    }

    // edge
    double const edge_signed = pm.mid - dv.p_yes_fair_avg;
    double const edge_abs = edge_signed < 0.0 ? -edge_signed : edge_signed;

    // 触发条件 1: |edge| > 0.05 (严格大于)
    if (!(edge_abs > EDGE_THRESHOLD)) {
        return std::nullopt;
    }

    // 触发条件 2: top-3 流动性 >= $2K
    if (!(pm.top3_liquidity_usdc >= MIN_TOP3_LIQUIDITY_USDC)) {
        return std::nullopt;
    }

    // 触发条件 3: T_kickoff - now < 6h OR game.live
    std::int64_t const delta_kickoff_ns = g.kickoff_ts_ns - ctx.as_of_ts_ns;
    bool const within_6h_or_live = (delta_kickoff_ns < SIX_HOURS_NS) || g.live;
    if (!within_6h_or_live) {
        return std::nullopt;
    }

    // 触发条件 4: slippage.fill_rate >= 0.50
    if (!(pm.expected_fill_rate >= MIN_FILL_RATE)) {
        return std::nullopt;
    }

    // 触发条件 5: LiveSection ∈ {Live, Soon}
    LiveSection const sec = classify(g, ctx.as_of_ts_ns);
    if (sec != LiveSection::Live && sec != LiveSection::Soon) {
        return std::nullopt;
    }

    // ----- size 计算 (小程 spec §Kelly, 与 W4 相同) -----
    // kelly_full = edge / (1 - p_yes_fair) / (PM_mid · (1 - PM_mid))
    double const denom_a = 1.0 - dv.p_yes_fair_avg;
    double const denom_b = pm.mid * (1.0 - pm.mid);
    if (denom_a <= 0.0 || denom_b <= 0.0) {
        return std::nullopt;
    }
    double const kelly_full = edge_abs / denom_a / denom_b;
    if (!(kelly_full > 0.0) || !(kelly_full < 1e15)) {
        // NaN / Inf / <=0 → nullopt
        return std::nullopt;
    }

    double raw_size =
        KELLY_FRACTION * kelly_full * pm.expected_fill_rate * static_cast<double>(bankroll_usdc_);
    if (raw_size < 0.0)
        raw_size = 0.0;
    auto const max_d = static_cast<double>(MAX_SIZE_USDC);
    if (raw_size > max_d)
        raw_size = max_d;
    auto const size_usdc = static_cast<std::int64_t>(raw_size + 0.5);
    if (size_usdc <= 0) {
        return std::nullopt;
    }

    // ----- 装配 SignalOutput (ABI 不动) -----
    SignalOutput out;
    out.signal_id = SignalId::P0_01_PinnacleNoVig;  // ABI lock (老周)
    // v0.5: Side 解耦 outcome; Buy = 开仓方向 (outcome 由 Orchestrator 层根据 price 比较设 token_id)
    out.side = Side::Buy;
    out.edge_bps = round_to_bps(edge_abs);
    out.suggested_size_usdc = size_usdc;
    out.confidence = clamp01(edge_abs / CONFIDENCE_NORMALIZER);
    return out;
}

}  // namespace stcpp::strategy
