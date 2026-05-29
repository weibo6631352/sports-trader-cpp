// stcpp/microstructure/orderbook.hpp — OrderBookSnapshot + Microprobe v0.1
//
// Owner: 小袁 (quant-microstructure)
// Sprint-2 W4 Wave 20 — microstructure C++ lib v0.1 (header + types)
//
// 落:
//   docs/RESEARCH/xiaoyuan-microstructure-v1.md §1 (5 sport 实测) / §2 (microprice)
//   docs/RESEARCH/xiaoyuan-fill-rate-model-v0.1.md (W4 配套 spec)
//   docs/RESEARCH/laoli-polymarket-api-spec-v1.md §5.1 (WSS market channel)
//   docs/RESEARCH/laohan-riskmanager-design-v0.3.1.md §14.1 (MarketState 5 档)
//
// 红线:
//   R-20  OrderBookSnapshot 自带 4 ts (event ≤ data_source ≤ ingestion ≤ as_of)
//         time stamp 优先来自上游 WSS / REST payload (老李 spec §5.1 hash + ts), 严禁本地 now()
//         替代 data_source_ts; 仅 ingestion / as_of 是本地采的
//   R-12  本头只定义 POD + nodiscard helper; 任何 I/O / 阻塞操作禁止在 WSS event loop 内调用
//   R-7   header-only / mode-agnostic; live / paper / backtest 共用同一 POD 形状
//
// 不耻下问:
//   - WSS event loop 接入 → @老李 (polymarket-research)
//   - book hash 校验 → @小余 (data-etl)
//   - ETL schema 对齐 → §1.5 of xiaoyuan-microstructure-v1.md
//
// ============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace stcpp::microstructure {

// ---------------------------------------------------------------------------
// 1. 常量 (派单 §1 + xiaoyuan-microstructure-v1.md §3 / §4 实测)
// ---------------------------------------------------------------------------

// taker 费率: feeSchedule.rate = 0.03 (3%), takerOnly = true (v1 §3.7 实测)
inline constexpr double TAKER_FEE_PCT = 0.03;

// MVP gameday mainline 最小可吃 L1 美元深度; 低于此值 LOW_FILL_RATE 风险偏高
// 实测中位 NBA $124 / MLB $681 / Tennis $5017 (§1.2), 我们取保守底线 $2000
inline constexpr double MIN_LIQUIDITY_USDC = 2'000.0;

// Quote half-life 阈值 (ms). xiaoyuan §3.5 实测:
//   - PREGAME_FAR (临场 > 1h):  T_{1/2} > 120s
//   - PREGAME_NEAR (临场 5-60min): ~ 5s
//   - INPLAY_HOT  (临场 ± 10min): ~ 0.21s (实测均值 0.30s inter-arrival)
// 派单 §2 fill_rate 公式: qhl < 500ms 触发 -0.15 penalty (反映 quote 急衰)
inline constexpr std::int64_t QHL_THRESHOLD_MS = 500;

// 价区窗口大小 (tick 数). 实测 ±2 tick 累计深度是 sizing 的硬参考 (§1.2)
inline constexpr std::int32_t TICK_WINDOW = 2;

// Polymarket mainline tick = 1¢, longtail tick = 0.1¢ (§6.1)
inline constexpr double TICK_001 = 0.001;
inline constexpr double TICK_01 = 0.01;

// L1 + L2 + L3 累计深度 = "top3_depth_usdc" 取 ±2 tick 窗口 (§1.2 表头)
inline constexpr std::size_t kBookDepthLevels = 5;  // bid/ask 各 5 档

// ---------------------------------------------------------------------------
// 2. OrderBookLevel + OrderBookSnapshot (R-20 4 ts)
// ---------------------------------------------------------------------------

struct OrderBookLevel {
    double price{0.0};      // ∈ (0, 1), 离散到 tick 整倍数
    double size_usdc{0.0};  // L_i = price * qty (notional USD)
};

// 4 ts (R-20). 命名与 老唐 audit-schema v1.1 / 老韩 RM v0.3.1 对齐.
struct OrderBookTs {
    std::int64_t event_ts_ns{0};        // 上游事件时间 (e.g., trade matched at exchange)
    std::int64_t data_source_ts_ns{0};  // 数据源生成时间 (WSS payload @ts ms epoch → ns)
    std::int64_t ingestion_ts_ns{0};    // 本地收到字节流时间 (CLOCK_MONOTONIC_RAW)
    std::int64_t as_of_ts_ns{0};        // 决策点时间 (CLOCK_REALTIME @ evaluate)
};

[[nodiscard]] constexpr bool ts_order_ok(OrderBookTs const& t) noexcept {
    // event ≤ data_source ≤ ingestion ≤ as_of  (4 ts 严格不递减, R-20 PIT)
    return t.event_ts_ns <= t.data_source_ts_ns && t.data_source_ts_ns <= t.ingestion_ts_ns &&
           t.ingestion_ts_ns <= t.as_of_ts_ns;
}

[[nodiscard]] constexpr bool ts_all_positive(OrderBookTs const& t) noexcept {
    return t.event_ts_ns > 0 && t.data_source_ts_ns > 0 && t.ingestion_ts_ns > 0 && t.as_of_ts_ns > 0;
}

struct OrderBookSnapshot {
    // R-20 4 ts (上游优先, 禁本地 now() 替代 data_source_ts)
    OrderBookTs ts{};
    // Polymarket condition_id / asset_id (token_id)
    std::string_view market_id{};
    // 5 档 bid / ask, 0 = best
    std::array<OrderBookLevel, kBookDepthLevels> bid{};
    std::array<OrderBookLevel, kBookDepthLevels> ask{};
    // tick lattice (0.01 / 0.001)
    double tick_size{TICK_01};
    // 派单 §1 OrderBookSnapshot 字段
    double top3_depth_usdc{0.0};       // ±2 tick 累计 (实测 §1.2 主参考)
    std::int32_t spread_bps{0};        // (ask[0] - bid[0]) / mid * 10000
    std::int64_t last_trade_ts_ns{0};  // 用于 staleness 判断
};

// ---------------------------------------------------------------------------
// 3. Microprobe — fill_rate model 的微观输入概要 (v0.1 简化 microprice + QHL + AS)
// ---------------------------------------------------------------------------
//
// xiaoyuan-microstructure-v1.md §2.1:
//   microprice = (bid_qty * best_ask + ask_qty * best_bid) / (bid_qty + ask_qty)
//
// xiaoyuan-microstructure-v1.md §2.3 cap:
//   |microprice - mid| ≤ 2 * tick, 超出则 fallback 用 mid (outright 极偏 case)
//
struct Microprobe {
    double microprice{0.0};  // capped microprice (≤ 2 tick from mid)
    double mid{0.0};
    double imbalance{0.0};                    // ∈ [-1, 1]
    std::int64_t quote_half_life_ms{30'000};  // §3.5 实测 (gameday cold 30s, hot 500ms)
    double adverse_selection_score{0.0};      // ∈ [0, 1], 1 = quote 移向 against us 强烈
    bool is_hot_token{false};                 // λ > 1/s (§5.3) 或临场 ± 10min
};

// ---------------------------------------------------------------------------
// 4. Helper: 计算 microprice + imbalance (pure functions, 单测可 round-trip)
// ---------------------------------------------------------------------------

namespace detail {

[[nodiscard]] inline constexpr bool finite_pos(double x) noexcept {
    return !(x != x) && x > 0.0 && x < 1e300;
}

[[nodiscard]] inline constexpr bool finite(double x) noexcept {
    return !(x != x) && x > -1e300 && x < 1e300;
}

}  // namespace detail

// L1 microprice / mid / imbalance — input qty 视作 size (USD notional 或 contract qty 一致即可)
struct L1Probe {
    double mid{0.0};
    double microprice{0.0};
    double imbalance{0.0};
    bool valid{false};
};

[[nodiscard]] inline L1Probe compute_l1_probe(OrderBookLevel const& best_bid, OrderBookLevel const& best_ask,
                                              double tick) noexcept {
    L1Probe out;
    if (!detail::finite_pos(best_bid.price) || !detail::finite_pos(best_ask.price) ||
        !detail::finite_pos(best_bid.size_usdc) || !detail::finite_pos(best_ask.size_usdc) ||
        !detail::finite_pos(tick)) {
        return out;
    }
    if (best_ask.price <= best_bid.price) {
        return out;  // crossed / locked book — caller 处理
    }
    out.mid = 0.5 * (best_bid.price + best_ask.price);

    // imbalance = (bid_qty - ask_qty) / (bid_qty + ask_qty)
    double const sum_qty = best_bid.size_usdc + best_ask.size_usdc;
    out.imbalance = (best_bid.size_usdc - best_ask.size_usdc) / sum_qty;

    // microprice = (bid_qty * best_ask + ask_qty * best_bid) / sum
    double const micro_raw =
        (best_bid.size_usdc * best_ask.price + best_ask.size_usdc * best_bid.price) / sum_qty;
    // §2.3 cap: |micro - mid| ≤ 2 ticks; 超出 fallback mid
    double const cap = 2.0 * tick;
    double const delta = micro_raw - out.mid;
    double const clamped_delta = (delta > cap) ? cap : (delta < -cap) ? -cap : delta;
    out.microprice = out.mid + clamped_delta;
    out.valid = true;
    return out;
}

// 取 ±N tick 累计 ask / bid 深度 (USD)
[[nodiscard]] inline double depth_within_ticks(std::array<OrderBookLevel, kBookDepthLevels> const& side,
                                               double ref_price, double tick, std::int32_t n_ticks) noexcept {
    if (!detail::finite_pos(ref_price) || !detail::finite_pos(tick) || n_ticks <= 0) {
        return 0.0;
    }
    double const band = tick * static_cast<double>(n_ticks);
    double sum = 0.0;
    for (auto const& lvl : side) {
        if (!detail::finite_pos(lvl.price) || !detail::finite_pos(lvl.size_usdc))
            continue;
        double const d = lvl.price - ref_price;
        double const ad = d < 0 ? -d : d;
        if (ad <= band + 1e-9) {
            sum += lvl.size_usdc;
        }
    }
    return sum;
}

}  // namespace stcpp::microstructure
