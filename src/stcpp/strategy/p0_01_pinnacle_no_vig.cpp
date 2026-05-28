// src/stcpp/strategy/p0_01_pinnacle_no_vig.cpp — P0-01 impl v0.1
//
// 落: 小程 P0-01 spec v0.1

#include "stcpp/strategy/p0_01_pinnacle_no_vig.hpp"

#include <cmath>
#include <utility>

namespace stcpp::strategy {

namespace {

[[nodiscard]] bool is_finite(double x) noexcept {
    return !(x != x) && x > -1e300 && x < 1e300;
}

[[nodiscard]] double clamp01(double x) noexcept {
    if (x < 0.0) {
        return 0.0;
    }
    if (x > 1.0) {
        return 1.0;
    }
    return x;
}

[[nodiscard]] std::int32_t round_to_bps(double edge) noexcept {
    double const raw = edge * 10'000.0;
    double const adj = raw >= 0.0 ? raw + 0.5 : raw - 0.5;
    return static_cast<std::int32_t>(adj);
}

}  // namespace

NoVigResult compute_no_vig(double decimal_yes, double decimal_no) noexcept {
    NoVigResult r;
    if (!is_finite(decimal_yes) || !is_finite(decimal_no)) {
        return r;
    }
    if (decimal_yes < MIN_DECIMAL_ODDS || decimal_no < MIN_DECIMAL_ODDS) {
        return r;
    }
    r.p_yes_raw  = 1.0 / decimal_yes;
    r.p_no_raw   = 1.0 / decimal_no;
    r.overround  = r.p_yes_raw + r.p_no_raw;
    if (r.overround <= 0.0 || !is_finite(r.overround)) {
        return r;
    }
    r.p_yes_fair = r.p_yes_raw / r.overround;
    // overround 健康范围 ∈ [1.0, 1.20] (小程 spec 1.02-1.05 典型, 放宽到 1.20 兼容停盘前抖动).
    // overround < 1.0 → 套利 (异常), > 1.20 → 极不健康. 当前不在此 reject, 留给调用方判.
    r.valid = (r.p_yes_fair > 0.0 && r.p_yes_fair < 1.0);
    return r;
}

// ----- Mock sources -----

void MockPinnacleSource::put(std::string market_id, PinnacleQuote q) {
    book_.insert_or_assign(std::move(market_id), q);
}

bool MockPinnacleSource::lookup(std::string const& market_id, PinnacleQuote& out) const noexcept {
    auto const it = book_.find(market_id);
    if (it == book_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

void MockPmSnapshotSource::put(std::string market_id, PmSnapshot s) {
    book_.insert_or_assign(std::move(market_id), s);
}

bool MockPmSnapshotSource::lookup(std::string const& market_id, PmSnapshot& out) const noexcept {
    auto const it = book_.find(market_id);
    if (it == book_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

void MockGameStateSource::put(std::string market_id, GameState g) {
    book_.insert_or_assign(std::move(market_id), g);
}

bool MockGameStateSource::lookup(std::string const& market_id, GameState& out) const noexcept {
    auto const it = book_.find(market_id);
    if (it == book_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

// ----- PinnacleNoVigSignal -----

PinnacleNoVigSignal::PinnacleNoVigSignal(IPinnacleSource const& pinnacle,
                                         IPmSnapshotSource const& pm,
                                         IGameStateSource const& games,
                                         std::int64_t bankroll_usdc) noexcept
    : pinnacle_(pinnacle), pm_(pm), games_(games), bankroll_usdc_(bankroll_usdc) {}

bool PinnacleNoVigSignal::validate_context_(SignalContext const& ctx) const noexcept {
    // R-20: 4 ts 全 > 0 且单调非降, feature_snapshot_id 非空, market_id 非空.
    if (ctx.event_ts_ns       <= 0 ||
        ctx.data_source_ts_ns <= 0 ||
        ctx.ingestion_ts_ns   <= 0 ||
        ctx.as_of_ts_ns       <= 0) {
        return false;
    }
    if (ctx.event_ts_ns       > ctx.data_source_ts_ns ||
        ctx.data_source_ts_ns > ctx.ingestion_ts_ns   ||
        ctx.ingestion_ts_ns   > ctx.as_of_ts_ns) {
        return false;
    }
    if (ctx.market_id.empty() || ctx.feature_snapshot_id.empty()) {
        return false;
    }
    return true;
}

std::optional<SignalOutput> PinnacleNoVigSignal::tick(SignalContext const& ctx) noexcept {
    // R-20 PIT 闸门
    if (!validate_context_(ctx)) {
        return std::nullopt;
    }

    // 数据 lookup (任一缺 → nullopt, fail-closed)
    PinnacleQuote pin{};
    if (!pinnacle_.lookup(ctx.market_id, pin)) {
        return std::nullopt;
    }
    PmSnapshot pm{};
    if (!pm_.lookup(ctx.market_id, pm) || !pm.valid) {
        return std::nullopt;
    }
    GameState g{};
    if (!games_.lookup(ctx.market_id, g)) {
        return std::nullopt;
    }

    // PM mid sanity (∈ (0, 1) 严格)
    if (!is_finite(pm.mid) || pm.mid <= PM_MID_EPS || pm.mid >= 1.0 - PM_MID_EPS) {
        return std::nullopt;
    }

    // no-vig 公式
    NoVigResult const nv = compute_no_vig(pin.decimal_yes, pin.decimal_no);
    if (!nv.valid) {
        return std::nullopt;
    }

    // edge
    double const edge_signed = pm.mid - nv.p_yes_fair;
    double const edge_abs    = edge_signed < 0.0 ? -edge_signed : edge_signed;

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

    // ----- size 计算 -----
    // kelly_full = edge / (1 - p_yes_fair) / (PM_mid · (1 - PM_mid))
    double const denom_a = 1.0 - nv.p_yes_fair;
    double const denom_b = pm.mid * (1.0 - pm.mid);
    if (denom_a <= 0.0 || denom_b <= 0.0) {
        return std::nullopt;
    }
    double const kelly_full = edge_abs / denom_a / denom_b;
    if (!is_finite(kelly_full) || kelly_full <= 0.0) {
        return std::nullopt;
    }

    double const raw_size_usdc = KELLY_FRACTION * kelly_full * pm.expected_fill_rate *
                                 static_cast<double>(bankroll_usdc_);
    // clip [0, MAX_SIZE_USDC]
    double clipped = raw_size_usdc;
    if (clipped < 0.0) {
        clipped = 0.0;
    }
    auto const max_d = static_cast<double>(MAX_SIZE_USDC);
    if (clipped > max_d) {
        clipped = max_d;
    }
    auto size_usdc = static_cast<std::int64_t>(clipped + 0.5);
    if (size_usdc <= 0) {
        // size 太小不出单, fail-closed
        return std::nullopt;
    }

    // ----- 装配 SignalOutput -----
    SignalOutput out;
    out.signal_id           = SignalId::P0_01_PinnacleNoVig;
    out.side                = (pm.mid < nv.p_yes_fair) ? Side::BuyYes : Side::BuyNo;
    out.edge_bps            = round_to_bps(edge_abs);
    out.suggested_size_usdc = size_usdc;
    out.confidence          = clamp01(edge_abs / CONFIDENCE_NORMALIZER);
    return out;
}

}  // namespace stcpp::strategy
