// stcpp/microstructure/fill_rate_model.hpp — FillRateModel v0.1
//
// Owner: 小袁 (quant-microstructure)
// Sprint-2 W4 Wave 20
//
// 落:
//   docs/RESEARCH/xiaoyuan-fill-rate-model-v0.1.md (本 lib spec)
//   docs/RESEARCH/xiaoyuan-microstructure-v1.md §3.5 / §4 (QHL + adverse selection 实测)
//   docs/RESEARCH/xiaoxiao-slippage-model-lib-v1.md (slippage 由小肖, fill_rate 由本 lib)
//   docs/RESEARCH/xiaojiang-paper-engine-skeleton-v1.md §4 (Mode A++ Bernoulli sampler)
//
// 红线:
//   R-7   ExecutionMode-agnostic (paper/live/backtest 共用同一公式)
//   R-11  不写 ledger / 不调 audit, 纯函数
//   R-20  fill_rate model 接受 OrderBookSnapshot 4 ts, 校验 ts_order_ok + ts_all_positive
//   R-1   本 lib 不做 reject 决策, 仅输出 fill_rate (RM 用 floor 0.50 拒)
//
// fill_rate 来源边界 (与小肖 SlippageModel 协同):
//   - 小肖 SlippageModel.expected_fill_rate 是数值近似 (rho / staleness 单因子)
//   - 小袁 FillRateModel 是多因子复合 (depth × QHL × spread × AS × time_decay × sport)
//   - 调用关系: paper Mode A++ VirtualMatcher 当前用小肖, W5 切到小袁 (W5 派单点)
//
// 公式 v0.1:
//   base_fill_rate = clamp(quoted_depth_within_2_ticks / intent_size, 0.10, 0.95)
//   penalties:
//     qhl_penalty       = (qhl_ms < QHL_THRESHOLD_MS) ?  -0.15 : 0.00
//     spread_penalty    = (spread_bps > 50)            ?  -0.10 : 0.00
//     adverse_selection = (AS_score > 0.5)             ?  -0.20 : 0.00
//     time_decay        = (phase == Late)              ?  -profile.late_decay_penalty : 0.00
//   sport_bias = profile_of(sport, phase).base_fill_rate - 0.65 (向中位回正)
//   fill_rate = clamp(base + sum_penalties + sport_bias, 0.0, 1.0)
//
// Maker / Taker 分流:
//   - Taker (穿价立即成交): fill_rate ~ 1.0, 但 caller 应另算 fee 3% + slippage
//   - Maker (排队等成交):    fill_rate = 本公式输出, 无 fee, 已成交可得 0.75% rebate
//   - 当前默认 Maker (paper engine 主路径); Taker 走单独入口 compute_taker()
//
// ============================================================================

#pragma once

#include <cstdint>

#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/microstructure/sport_profile.hpp"

namespace stcpp::microstructure {

// ---------------------------------------------------------------------------
// 1. 模型常量 (派单 §2 公式)
// ---------------------------------------------------------------------------

inline constexpr double FILL_RATE_FLOOR = 0.50;  // 与 risk_gateway / virtual_matcher 一致
inline constexpr double FILL_RATE_CAP = 1.00;    // 最大 clamp 上限
inline constexpr double BASE_MIN = 0.10;         // base 下界 (无深度也不直接 0, 留给 RM 拒)
inline constexpr double BASE_MAX = 0.95;         // base 上界 (留 5% 给非建模因素)

// Penalty 常量
inline constexpr double PENALTY_QHL_SHORT = 0.15;       // qhl < 500ms
inline constexpr double PENALTY_SPREAD_WIDE = 0.10;     // spread > 50 bps (= 0.5¢ @ p=1.0)
inline constexpr double PENALTY_ADVERSE_SELECT = 0.20;  // AS_score > 0.5

// 触发阈值
inline constexpr std::int32_t SPREAD_WIDE_BPS = 50;  // 0.5¢ @ mid 0.50 (= 1 tick)
inline constexpr double AS_THRESHOLD = 0.5;

// Sport profile bias 中位 (sport_bias 计算用; 8 sport × 4 phase 中位 ~ 0.65)
inline constexpr double PROFILE_BIAS_PIVOT = 0.65;

// ---------------------------------------------------------------------------
// 2. Intent + Side
// ---------------------------------------------------------------------------

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

enum class MatchPath : std::uint8_t {
    Maker = 0,  // 排队, fill_rate < 1
    Taker = 1,  // 穿价立即, fill_rate ~ 1
};

struct FillSpec {
    Side side{Side::Buy};
    double price{0.0};      // ∈ (0, 1)
    double size_usdc{0.0};  // > 0
    Sport sport{Sport::Basketball};
    InplayPhase phase{InplayPhase::Mid};
    MatchPath path{MatchPath::Maker};
};

// ---------------------------------------------------------------------------
// 3. Reject code (轻量, 与老韩 21 enum 子集映射)
// ---------------------------------------------------------------------------
enum class FillRateReject : std::uint8_t {
    Ok = 0,
    InvalidSnapshot = 1,  // ts / NaN / Inf / crossed book
    InvalidIntent = 2,    // size / price 非法
    BelowFloor = 3,       // fill_rate < FILL_RATE_FLOOR  (caller 决策, 本 lib 不强拒)
};

// ---------------------------------------------------------------------------
// 4. Output — 分解到每个 penalty (audit + W6 校准可还原)
// ---------------------------------------------------------------------------
struct FillRateBreakdown {
    double base_rate{0.0};
    double qhl_penalty{0.0};
    double spread_penalty{0.0};
    double adverse_penalty{0.0};
    double time_decay_penalty{0.0};
    double sport_bias{0.0};
    double sum_penalties{0.0};  // = -(qhl + spread + adverse + time_decay)
    double pre_clamp{0.0};
    double final_rate{0.0};
};

struct FillRateOutput {
    FillRateReject reject{FillRateReject::Ok};
    double fill_rate{0.0};
    FillRateBreakdown breakdown{};
    // 透传 4 ts (R-20 audit chain)
    OrderBookTs ts{};
};

// ---------------------------------------------------------------------------
// 5. FillRateModel — 主类 (无状态, static method, paper / live / backtest 共用)
// ---------------------------------------------------------------------------
class FillRateModel {
public:
    // Maker 路径 (默认, paper Mode A++ 用)
    [[nodiscard]] static FillRateOutput compute_maker(OrderBookSnapshot const& book, Microprobe const& probe,
                                                      FillSpec const& intent) noexcept;

    // Taker 路径 (穿价立即成交; fill_rate ~ 1.0, 留 5% 给 cancel race)
    [[nodiscard]] static FillRateOutput compute_taker(OrderBookSnapshot const& book,
                                                      FillSpec const& intent) noexcept;

    // 统一入口 — 按 intent.path 分流
    [[nodiscard]] static FillRateOutput compute(OrderBookSnapshot const& book, Microprobe const& probe,
                                                FillSpec const& intent) noexcept {
        if (intent.path == MatchPath::Taker) {
            return compute_taker(book, intent);
        }
        return compute_maker(book, probe, intent);
    }

    // 静态 helper (单测 + 调试 + audit)
    [[nodiscard]] static double clamp_unit(double x) noexcept {
        if (!detail::finite(x))
            return 0.0;
        if (x < 0.0)
            return 0.0;
        if (x > 1.0)
            return 1.0;
        return x;
    }
};

}  // namespace stcpp::microstructure
