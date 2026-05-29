// stcpp/backtest/types.hpp — Backtest framework v0.2 core types
//
// Owner: 小蒋 (quant-backtest, C 量化研究部 IC #20)
// Sprint: W10+ (backtest framework v0.2 C++ skeleton)
// Last review: 2026-05-29
//
// 落:
//   docs/RESEARCH/xiaojiang-backtest-framework-v0.2-cpp.md (本 lib 设计 SSOT)
//   docs/RESEARCH/xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md §3 (backtest spec)
//
// 红线:
//   R-7   ExecutionMode-agnostic (paper/live/backtest 共用同一类型)
//   R-11  不写 ledger, 纯数据结构 + 纯函数
//   R-20  所有 TradeRecord 携 4 ts (event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts)
//
// 设计原则:
//   - TradeRecord: 单笔交易记录, 含 4 ts + PnL + fill 信息
//   - WalkForwardWindow: IS/OOS 时间窗口定义
//   - BacktestMetrics: 回测指标汇总 (net edge / hit rate / Sharpe / MDD)
//   - BootstrapResult: stationary bootstrap CI 输出
//   - BacktestBucket: PREGAME / INPLAY 分桶 (小程 §2.2 混算禁止)

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace stcpp::backtest {

// ---------------------------------------------------------------------------
// 1. 交易桶 (小程 §2.2 — 禁止 PREGAME + INPLAY 混算)
// ---------------------------------------------------------------------------

enum class TradeBucket : std::uint8_t {
    Pregame = 0,  // LiveSection::Soon, T_kickoff - as_of ≤ 6h
    Inplay  = 1,  // LiveSection::Live, game.live == true
    Unknown = 2,
};

[[nodiscard]] constexpr const char* TradeBucketName(TradeBucket b) noexcept {
    switch (b) {
        case TradeBucket::Pregame: return "PREGAME";
        case TradeBucket::Inplay:  return "INPLAY";
        case TradeBucket::Unknown: return "UNKNOWN";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// 2. 交易方向
// ---------------------------------------------------------------------------

enum class TradeSide : std::uint8_t {
    BuyYes  = 0,  // 买 Yes token (看多)
    SellYes = 1,  // 卖 Yes token (看空)
};

// ---------------------------------------------------------------------------
// 3. 结算结果
// ---------------------------------------------------------------------------

enum class SettleOutcome : std::uint8_t {
    Pending  = 0,  // 未结算
    YesWins  = 1,  // Yes token = 1.0
    NoWins   = 2,  // Yes token = 0.0
};

// ---------------------------------------------------------------------------
// 4. TradeRecord — 单笔交易完整记录 (R-20 4 ts)
// ---------------------------------------------------------------------------
//
// 用于 walk-forward + 指标计算. 字段对应小程 §2.3 net_edge 口径.
//
struct TradeRecord {
    // ---- R-20 4 时间戳 (必须满足单调: event ≤ ds ≤ ingestion ≤ as_of) ----
    std::int64_t event_ts_ns{0};        // 上游事件时间 (bookmaker 报价时刻)
    std::int64_t data_source_ts_ns{0};  // 数据源时间 (Goalserve snapshot_ts_ns)
    std::int64_t ingestion_ts_ns{0};    // 本地入库时间
    std::int64_t as_of_ts_ns{0};        // 决策/信号触发时刻

    // ---- 市场标识 ----
    std::string  market_id;             // Polymarket condition_id
    std::string  game_id;               // Goalserve game_id (purged k-fold 分组键)

    // ---- 信号 + 决策 ----
    TradeSide    side{TradeSide::BuyYes};
    TradeBucket  bucket{TradeBucket::Pregame};

    double fair_value{0.0};             // Goalserve de-vig fair probability (∈ (0,1))
    double pm_mid{0.0};                 // Polymarket mid price at as_of_ts (∈ (0,1))
    double gross_edge{0.0};             // |pm_mid - fair_value|
    double fee_rate{0.03};              // Polymarket taker fee (3%)
    double slippage_rate{0.003};        // 预期 slippage (FillRateModel 输出)

    // ---- 执行 ----
    double fill_price{0.0};             // 实际成交价 (≈ pm_mid ± slippage)
    double size_usdc{0.0};              // 成交金额 (USDC)

    // ---- 结算 ----
    SettleOutcome outcome{SettleOutcome::Pending};

    // ---- PnL (结算后填充) ----
    double realized_pnl_usdc{0.0};      // 净 PnL (扣 fee + slippage)
    double net_edge{0.0};               // realized_pnl_usdc / size_usdc

    // ---- 过滤标记 ----
    bool   filtered_out{false};         // 被 C2 / 死区等过滤掉的信号 (不计入统计)
};

// ---------------------------------------------------------------------------
// 5. WalkForwardWindow — IS / OOS 时间窗口
// ---------------------------------------------------------------------------
//
// 对应小程 §3.2 + 小蒋 framework §3.1 rolling walk-forward + purged embargo.
//

struct WalkForwardWindow {
    std::int64_t is_start_ns{0};        // IS 窗口开始 (ns epoch)
    std::int64_t is_end_ns{0};          // IS 窗口结束
    std::int64_t embargo_end_ns{0};     // embargo 结束 (= is_end + embargo_duration)
    std::int64_t oos_start_ns{0};       // OOS 窗口开始 (= embargo_end)
    std::int64_t oos_end_ns{0};         // OOS 窗口结束

    // 索引 (rolling 第几轮)
    std::uint32_t fold_index{0};
};

// ---------------------------------------------------------------------------
// 6. BacktestMetrics — 回测指标汇总 (§3.3 口径)
// ---------------------------------------------------------------------------
//
// 分 IS / OOS 两份; 分 PREGAME / INPLAY 两桶.
//

struct BacktestMetrics {
    // --- 基础计数 ---
    std::size_t  n_trades{0};           // 总成交笔数
    std::size_t  n_wins{0};             // 方向正确笔数

    // --- Hit Rate ---
    double hit_rate{0.0};               // n_wins / n_trades ∈ [0, 1]

    // --- Net Edge ---
    double net_edge_mean{0.0};          // E[net_pnl] / size (% per trade)
    double net_edge_std{0.0};           // std dev
    double total_net_pnl{0.0};          // 累计净 PnL (USDC)

    // --- Sharpe (日收益率序列, 年化 ×√252) ---
    double sharpe_ratio{0.0};           // (mean_daily_ret / std_daily_ret) × sqrt(252)

    // --- MDD ---
    double max_drawdown{0.0};           // peak-to-trough / peak_equity ∈ [0, 1]

    // --- 统计检验 ---
    double t_stat{0.0};                 // 单尾 t-stat (H0: E[net_pnl] ≤ 0)
    double p_value{0.0};                // 单尾 p-value

    // --- Bootstrap ---
    double sharpe_ci_lower{0.0};        // stationary bootstrap 90% CI 下界
    double sharpe_ci_upper{0.0};        // stationary bootstrap 90% CI 上界

    // --- Bonferroni (参数扫描后校正) ---
    double bonferroni_p{0.0};           // p_value × n_experiments (校正后)

    // --- DSR (Deflated Sharpe Ratio) ---
    double deflated_sharpe{0.0};        // > 1.0 才算 IS 通过

    // --- 桶标记 ---
    TradeBucket  bucket{TradeBucket::Pregame};

    // --- 窗口标记 ---
    bool         is_in_sample{true};    // true=IS, false=OOS
    std::uint32_t fold_index{0};
};

// ---------------------------------------------------------------------------
// 7. BootstrapResult — stationary bootstrap CI 输出 (§3.4)
// ---------------------------------------------------------------------------

struct BootstrapResult {
    double ci_lower{0.0};               // 90% CI 下界 (5th percentile)
    double ci_upper{0.0};               // 90% CI 上界 (95th percentile)
    double mean_sharpe{0.0};            // bootstrap 样本均值
    double std_sharpe{0.0};             // bootstrap 样本 std
    std::size_t n_bootstrap{5000};      // 重采样次数
    std::size_t block_size{10};         // stationary block 平均长度 (天)
};

// ---------------------------------------------------------------------------
// 8. ParamSet — 参数扫描网格单元 (§3.2)
// ---------------------------------------------------------------------------

struct ParamSet {
    double   gross_edge_threshold{0.06};    // C2 gross_edge 门 (5/6/7¢)
    int      min_bookmakers{3};             // MIN_BOOKMAKERS (3/4/5)
    double   kelly_fraction{0.20};          // Kelly fraction (0.15/0.20/0.25)
    double   dead_zone_threshold{0.25};     // 死区阈值 (0.20/0.25/0.30)
};

// ---------------------------------------------------------------------------
// 9. 辅助: R-20 4 ts 合法性校验
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr bool trade_ts_ok(TradeRecord const& t) noexcept {
    return t.event_ts_ns       > 0
        && t.data_source_ts_ns > 0
        && t.ingestion_ts_ns   > 0
        && t.as_of_ts_ns       > 0
        && t.event_ts_ns       <= t.data_source_ts_ns
        && t.data_source_ts_ns <= t.ingestion_ts_ns
        && t.ingestion_ts_ns   <= t.as_of_ts_ns;
}

// ---------------------------------------------------------------------------
// 10. 辅助: net_edge 计算 (小程 §2.3 口径)
//
// net_edge = (fair_value - fill_price) - (fee_rate + slippage_rate) * fill_price
//   ≈ gross_edge - fee_rate - slippage_rate  (fill_price ≈ 0.5 near-even 近似)
// ---------------------------------------------------------------------------

[[nodiscard]] inline double compute_net_edge(double fair_value, double fill_price,
                                              double fee_rate, double slippage_rate) noexcept {
    return (fair_value - fill_price) - (fee_rate + slippage_rate) * fill_price;
}

// Polymarket 二元结算 realized_pnl:
//   BuyYes + YesWins → (1 - fill_price) * size - fee * fill_price * size - slippage * size
//   BuyYes + NoWins  → -fill_price * size - fee * fill_price * size - slippage * size
[[nodiscard]] inline double compute_realized_pnl(TradeSide side, SettleOutcome outcome,
                                                  double fill_price, double size_usdc,
                                                  double fee_rate, double slippage_rate) noexcept {
    if (outcome == SettleOutcome::Pending) {
        return 0.0;
    }
    bool const won = (side == TradeSide::BuyYes && outcome == SettleOutcome::YesWins)
                  || (side == TradeSide::SellYes && outcome == SettleOutcome::NoWins);
    double const gross = won ? (1.0 - fill_price) * size_usdc
                              : -fill_price * size_usdc;
    double const cost = (fee_rate + slippage_rate) * fill_price * size_usdc;
    return gross - cost;
}

// 方向是否正确 (用于 hit rate 计算)
[[nodiscard]] inline bool is_winning_trade(TradeSide side, SettleOutcome outcome) noexcept {
    return (side == TradeSide::BuyYes && outcome == SettleOutcome::YesWins)
        || (side == TradeSide::SellYes && outcome == SettleOutcome::NoWins);
}

}  // namespace stcpp::backtest
