// stcpp/microstructure/fill_rate_model.hpp — FillRateModel v0.2
//
// Owner: 小袁 (quant-microstructure)
// Sprint-2 W4 Wave 20 (v0.1); Wave 3 升级 (v0.2)
//
// 落:
//   docs/RESEARCH/xiaoyuan-fill-rate-model-v0.1.md (本 lib spec v0.1)
//   docs/RESEARCH/xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md §2.1 (slippage ~0.3%)
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
//   - Wave 3: 新增 compute_from_clob_book() — 基于真实 CLOB 多档 depth 的 queue position fill
//
// 公式 v0.1 (Maker / Taker 分流, 保持不变):
//   base_fill_rate = clamp(quoted_depth_within_2_ticks / intent_size, 0.10, 0.95)
//   penalties:
//     qhl_penalty       = (qhl_ms < QHL_THRESHOLD_MS) ?  -0.15 : 0.00
//     spread_penalty    = (spread_bps > 50)            ?  -0.10 : 0.00
//     adverse_selection = (AS_score > 0.5)             ?  -0.20 : 0.00
//     time_decay        = (phase == Late)              ?  -profile.late_decay_penalty : 0.00
//   sport_bias = profile_of(sport, phase).base_fill_rate - 0.65 (向中位回正)
//   fill_rate = clamp(base + sum_penalties + sport_bias, 0.0, 1.0)
//
// 公式 v0.2 新增 (compute_from_clob_book — 真实多档 CLOB depth):
//   queue_depth_at_price  = sum(level.size_usdc for level.price == intent.price, bid/ask)
//   total_depth_within_2t = top3_depth_usdc (±2 tick 累计, caller 已填)
//   queue_position_ratio  = intent.size_usdc / queue_depth_at_price  (∈ [0, ∞))
//   p_queue_fill          = exp(-queue_position_ratio)                 (指数衰减, §2)
//   slippage_bps          = round(rho * tick_size / quote_price * 5000) (保守 0.3% 估算)
//   fill_rate_clob        = clamp(p_queue_fill * (top3_depth / (top3_depth + intent_size)), 0.1, 0.95)
//   最终再叠加 penalty: spread / adverse / time_decay (同 v0.1, qhl 由 probe 携带)
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

struct FillIntent {
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
// 5. ClobFillOutput — compute_from_clob_book 输出 (v0.2 Wave 3 新增)
// ---------------------------------------------------------------------------
//
// 与 FillRateOutput 独立, 以避免 ABI 破坏 v0.1 接口.
// backtest 复用: 小程 §5.1 feature 表 pm_fill_rate 字段来源.
//
struct ClobFillOutput {
    FillRateReject reject{FillRateReject::Ok};

    // 核心输出
    double fill_rate{0.0};             // ∈ [0, 1], clob + penalty 合成
    double p_queue_fill{0.0};          // exp(-queue_position_ratio)
    double queue_position_ratio{0.0};  // intent.size / queue_depth_at_price
    double queue_depth_at_price{0.0};  // 目标价档累计 USD 深度
    double slippage_rate{0.0};         // 保守估算 ≈ 0.003 (0.3%), 小程 §2.1 口径
    std::int32_t slippage_bps{0};      // round(slippage_rate * 10000)

    // penalty 分解 (审计可还原)
    double spread_penalty{0.0};
    double adverse_penalty{0.0};
    double time_decay_penalty{0.0};
    double sport_bias{0.0};
    double sum_penalties{0.0};

    // R-20 4 ts 透传
    OrderBookTs ts{};
};

// ---------------------------------------------------------------------------
// 6. FillRateModel — 主类 (无状态, static method, paper / live / backtest 共用)
// ---------------------------------------------------------------------------
class FillRateModel {
public:
    // Maker 路径 (默认, paper Mode A++ 用) — v0.1 接口不变
    [[nodiscard]] static FillRateOutput compute_maker(OrderBookSnapshot const& book, Microprobe const& probe,
                                                      FillIntent const& intent) noexcept;

    // Taker 路径 (穿价立即成交; fill_rate ~ 1.0, 留 5% 给 cancel race)
    [[nodiscard]] static FillRateOutput compute_taker(OrderBookSnapshot const& book,
                                                      FillIntent const& intent) noexcept;

    // 统一入口 — 按 intent.path 分流 (v0.1 接口不变)
    [[nodiscard]] static FillRateOutput compute(OrderBookSnapshot const& book, Microprobe const& probe,
                                                FillIntent const& intent) noexcept {
        if (intent.path == MatchPath::Taker) {
            return compute_taker(book, intent);
        }
        return compute_maker(book, probe, intent);
    }

    // Wave 3 v0.2 新增: 基于真实 CLOB 多档深度的 queue position fill probability.
    //
    // 设计要点:
    //   1. queue_depth_at_price: 遍历 bid/ask 档位, 找到与 intent.price 最近匹配档的 USD 深度.
    //      (Polymarket tick=0.01, price 离散, 允许 ±1 tick 误差区匹配)
    //   2. queue_position_ratio = intent.size_usdc / queue_depth_at_price
    //      比率越高 (排在深处), fill 概率越低.
    //   3. p_queue_fill = exp(-queue_position_ratio), 指数衰减建模 queue 等待.
    //   4. top3 调节: fill_rate_base = p_queue_fill × top3_depth / (top3_depth + intent_size)
    //      — 防止 top3 深度不足时 fill_rate 虚高.
    //   5. slippage 保守估算: rho = intent_size / best_L1_depth;
    //      slippage_rate = min(rho * tick_size / quote_price * 0.5, 0.005)  (上限 0.5%)
    //      对应小程 §2.1 pregame 参考值 ~0.3%.
    //   6. penalty (spread / adverse / time_decay) 叠加, 与 v0.1 逻辑一致.
    //   7. R-7: 纯函数, mode-agnostic (paper / backtest 共用, 接口相同).
    //
    // probe 仅用于 QHL + adverse_selection_score (可传空 Microprobe{} 退化到 0 penalty).
    [[nodiscard]] static ClobFillOutput compute_from_clob_book(OrderBookSnapshot const& book,
                                                               Microprobe const& probe,
                                                               FillIntent const& intent) noexcept;

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
