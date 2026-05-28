// stcpp/numerical/slippage_model.hpp — SlippageModel header-only lib v1
//
// 小肖 Sprint-2 W3 真实现, 7 单测 + Google Benchmark p99 < 200ns.
// 关联 spec:
//   - docs/RESEARCH/xiaoxiao-slippage-model-lib-v1.md (§1 接口 / §2 7 case / §3 bench)
//   - docs/RESEARCH/xiaoxiao-kelly-slippage-model-v1.md (§3.1 Linear / §3.4 confidence)
//   - docs/RESEARCH/laohan-riskmanager-design-v0.3.1.md (INVALID_INTENT 5 子原因)
//
// 红线:
//   - R-1 任何 reject 由 RiskGateway 决策, 本 lib 只算数值并标 RejectCode
//   - R-7 ExecutionMode-agnostic, paper/live/backtest 共用同 binary 同公式
//   - R-11 不写 ledger, 纯函数
//   - R-20 4 ts PIT, signer 入口 assert chain (本 lib 验 book_snapshot_ts vs as_of_ts)
//   - fail-closed: NaN / Inf / 负值 / ts=0 / ts stale > 60s / illegal-tick → INVALID_INTENT

#pragma once

#include <cmath>
#include <cstdint>

namespace stcpp::numerical {

enum class SlippageMode : std::uint8_t {
    Linear = 0,  // MVP, 小袁 microstructure v1 校准 KAPPA_DEPTH_GAMEDAY=1.0
    Sqrt   = 1,  // M5 后, Almgren-Chriss; 触发阈 RMSE(B)/RMSE(A) < 0.85
    Clob   = 2,  // M5 后, 真实 CLOB 微观仿真 (atomic batch / lattice)
};

enum class Confidence : std::uint8_t {
    High   = 0,  // Δt ≤ 1s, ρ ≤ 1
    Medium = 1,  // 1s < Δt ≤ 5s, ρ ≤ 2
    Low    = 2,  // Δt > 5s 或 ρ > 2
};

// lib-local RejectCode (与老韩 v0.3.1 risk::RejectCode 子集映射, RM 端再扩 21 enum)
enum class RejectCode : std::uint8_t {
    Ok                  = 0,
    InvalidIntent       = 1,  // 子原因见 InvalidIntentSubReason
    ExceedBookDepth     = 2,  // ρ > RHO_MAX
    FillRateBelowFloor  = 3,  // expected_fill_rate < FILL_RATE_FLOOR
};

// INVALID_INTENT 子原因 (与 risk::InvalidIntentSubReason 一致, 复制避免 lib 反向依赖)
enum class InvalidIntentSubReason : std::uint8_t {
    None            = 0,
    BookTsZero      = 1,  // book_snapshot_ts_ns == 0
    BookTsStale     = 2,  // wall_now - book_snapshot > 60s
    NanOrInf        = 3,
    Negative        = 4,
    IllegalTick     = 5,
};

struct SlippageInput {
    double       order_size_usdc;      // > 0
    double       quote_price;          // ∈ (EPS, 1-EPS)
    double       book_depth_l1_usdc;   // > 0
    std::int64_t book_snapshot_ts_ns;  // > 0
    std::int64_t wall_now_ns;          // > 0, 调用方注入 (单测 deterministic)
    double       tick_size;            // ∈ {0.001, 0.01}
};

struct SlippageOutput {
    double                 expected_fill_price{0.0};   // VWAP
    double                 expected_fill_rate{0.0};    // [0, 1]
    std::int32_t           slippage_bps{0};            // (pf - pq) / pq · 10000
    Confidence             confidence{Confidence::Low};
    RejectCode             reject{RejectCode::Ok};
    InvalidIntentSubReason sub_reason{InvalidIntentSubReason::None};
};

// === 常量 (lib spec v1 §1) ===
inline constexpr double       FILL_RATE_FLOOR         = 0.50;
inline constexpr double       RHO_MAX                 = 3.0;
inline constexpr std::int32_t MAX_SLIPPAGE_TICKS      = 3;
inline constexpr double       KAPPA_DEPTH_GAMEDAY     = 1.0;     // 小袁 microstructure v1
inline constexpr std::int64_t T_HALFLIFE_QUOTE_MS     = 30'000;  // gameday 30s
inline constexpr std::int64_t T_HALFLIFE_QUOTE_HOT_MS = 500;     // 关键事件 0.5s
inline constexpr double       BETA_WITHDRAW           = 0.3;
inline constexpr std::int64_t STALE_MAX_NS            = 60'000'000'000LL;  // 60s
inline constexpr double       EPS                     = 1e-6;
inline constexpr double       TICK_001                = 0.001;
inline constexpr double       TICK_01                 = 0.01;
inline constexpr double       TICK_TOL                = 1e-9;  // tick-equality epsilon

namespace detail {

[[nodiscard]] inline constexpr bool is_finite_pos(double x) noexcept {
    // constexpr-friendly NaN/Inf/negative-or-zero check; std::isfinite is non-constexpr in C++20
    // 但 NaN != NaN 和 Inf 大于任何 finite 上界这两条性质 constexpr 安全.
    return !(x != x) && x > 0.0 && x < 1e300;
}

[[nodiscard]] inline constexpr bool is_finite(double x) noexcept {
    return !(x != x) && x > -1e300 && x < 1e300;
}

[[nodiscard]] inline constexpr bool tick_legal(double tick) noexcept {
    // {0.001, 0.01} ± 1e-9
    double const d1 = tick - TICK_001;
    double const d2 = tick - TICK_01;
    double const a1 = d1 < 0 ? -d1 : d1;
    double const a2 = d2 < 0 ? -d2 : d2;
    return a1 < TICK_TOL || a2 < TICK_TOL;
}

[[nodiscard]] inline SlippageOutput make_reject(RejectCode code,
                                                InvalidIntentSubReason sub) noexcept {
    SlippageOutput out;
    out.reject = code;
    out.sub_reason = sub;
    return out;
}

[[nodiscard]] inline SlippageOutput validate(SlippageInput const& in) noexcept {
    SlippageOutput out;  // Ok by default

    if (in.book_snapshot_ts_ns <= 0 || in.wall_now_ns <= 0) {
        return make_reject(RejectCode::InvalidIntent, InvalidIntentSubReason::BookTsZero);
    }
    if (in.wall_now_ns - in.book_snapshot_ts_ns > STALE_MAX_NS) {
        return make_reject(RejectCode::InvalidIntent, InvalidIntentSubReason::BookTsStale);
    }
    // NaN / Inf
    if (!is_finite(in.order_size_usdc) || !is_finite(in.quote_price) ||
        !is_finite(in.book_depth_l1_usdc) || !is_finite(in.tick_size)) {
        return make_reject(RejectCode::InvalidIntent, InvalidIntentSubReason::NanOrInf);
    }
    // 负值 / 零 (size / L1 / quote / tick 必须严格 > 0)
    if (in.order_size_usdc <= 0.0 || in.book_depth_l1_usdc <= 0.0 ||
        in.quote_price <= EPS || in.quote_price >= 1.0 - EPS || in.tick_size <= 0.0) {
        return make_reject(RejectCode::InvalidIntent, InvalidIntentSubReason::Negative);
    }
    // 非法 tick
    if (!tick_legal(in.tick_size)) {
        return make_reject(RejectCode::InvalidIntent, InvalidIntentSubReason::IllegalTick);
    }
    return out;
}

[[nodiscard]] inline Confidence classify(double rho, std::int64_t dt_ms) noexcept {
    constexpr std::int64_t MS_1S = 1'000;
    constexpr std::int64_t MS_5S = 5'000;
    if (dt_ms <= MS_1S && rho <= 1.0) {
        return Confidence::High;
    }
    if (dt_ms <= MS_5S && rho <= 2.0) {
        return Confidence::Medium;
    }
    return Confidence::Low;
}

[[nodiscard]] inline SlippageOutput compute_linear(SlippageInput const& in) noexcept {
    // staleness Δt (ns → ms)
    std::int64_t const dt_ns = in.wall_now_ns - in.book_snapshot_ts_ns;
    auto const dt_ms = static_cast<std::int64_t>(dt_ns / 1'000'000);
    auto const dt_ms_d = static_cast<double>(dt_ms);
    auto const t_half = static_cast<double>(T_HALFLIFE_QUOTE_MS);
    double const s_stale = 1.0 - std::exp(-dt_ms_d / t_half);

    // ρ = S / L1
    double const rho = in.order_size_usdc / in.book_depth_l1_usdc;

    // π_withdraw = 1 - exp(-β · ρ)
    double const pi_withdraw = 1.0 - std::exp(-BETA_WITHDRAW * rho);

    // ρ > RHO_MAX → EXCEED_BOOK_DEPTH (拒, 不外推)
    if (rho > RHO_MAX) {
        return make_reject(RejectCode::ExceedBookDepth, InvalidIntentSubReason::None);
    }

    // VWAP fill price
    double pf = 0.0;
    double fill_rate = 0.0;
    if (rho <= 1.0) {
        // 一档内
        pf = in.quote_price + in.tick_size * rho * 0.5;
        fill_rate = 1.0 - pi_withdraw - s_stale;
    } else {
        // 多档 (1 < ρ ≤ ρ_max)
        pf = in.quote_price + in.tick_size * (0.5 + (rho - 1.0) * KAPPA_DEPTH_GAMEDAY);
        fill_rate = (1.0 / rho) * (1.0 - pi_withdraw) * (1.0 - s_stale);
    }

    // clamp [0, 1] for fill_rate
    if (fill_rate < 0.0) {
        fill_rate = 0.0;
    } else if (fill_rate > 1.0) {
        fill_rate = 1.0;
    }

    // slippage_bps = (pf - pq) / pq · 10000; round-to-nearest (banker-safe via +0.5 for non-neg)
    // 注: reject 时也填, 给 audit 留信号 (老韩 v0.3 §5 audit schema slippage_bps 字段)
    double const slip_raw = (pf - in.quote_price) / in.quote_price * 10'000.0;
    auto const slip_bps = static_cast<std::int32_t>(slip_raw + (slip_raw >= 0.0 ? 0.5 : -0.5));

    // FILL_RATE_FLOOR (闸门, 比 Kelly EDGE_NEGATED 更早拒, 见 spec §1.4 paper case)
    if (fill_rate < FILL_RATE_FLOOR) {
        SlippageOutput rej = make_reject(RejectCode::FillRateBelowFloor, InvalidIntentSubReason::None);
        rej.expected_fill_price = pf;
        rej.expected_fill_rate = fill_rate;
        rej.slippage_bps = slip_bps;
        return rej;
    }

    SlippageOutput out;
    out.expected_fill_price = pf;
    out.expected_fill_rate = fill_rate;
    out.slippage_bps = slip_bps;
    out.confidence = classify(rho, dt_ms);
    out.reject = RejectCode::Ok;
    out.sub_reason = InvalidIntentSubReason::None;
    return out;
}

}  // namespace detail

class SlippageModel {
 public:
    [[nodiscard]] static SlippageOutput compute(SlippageInput const& in,
                                                SlippageMode mode = SlippageMode::Linear) noexcept {
        // INVALID_INTENT 闸门 (fail-closed)
        SlippageOutput const v = detail::validate(in);
        if (v.reject != RejectCode::Ok) {
            return v;
        }

        switch (mode) {
            case SlippageMode::Linear:
                return detail::compute_linear(in);
            case SlippageMode::Sqrt:
                // TODO(小肖 M5): Almgren-Chriss √(S/V), 触发阈 RMSE(B)/RMSE(A) < 0.85
                return detail::compute_linear(in);
            case SlippageMode::Clob:
                // TODO(小肖 M5+ + 小袁): batch / lattice / maker rebate
                return detail::compute_linear(in);
        }
        // unreachable; defensive
        return detail::make_reject(RejectCode::InvalidIntent, InvalidIntentSubReason::NanOrInf);
    }
};

}  // namespace stcpp::numerical
