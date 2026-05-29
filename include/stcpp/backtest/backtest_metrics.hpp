// stcpp/backtest/backtest_metrics.hpp — BacktestMetrics 汇总计算接口
//
// Owner: 小蒋 (quant-backtest)
// Last review: 2026-05-29
//
// 提供 compute_full_metrics() — 从 TradeRecord 序列 + WalkForwardWindow 生成完整 BacktestMetrics.
// 实现在 src/stcpp/backtest/backtest_metrics.cpp.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "stcpp/backtest/metrics.hpp"
#include "stcpp/backtest/stats.hpp"
#include "stcpp/backtest/types.hpp"
#include "stcpp/backtest/walk_forward.hpp"

namespace stcpp::backtest {

// 从 TradeRecord 序列生成完整 BacktestMetrics
// 包含: basic stats + Sharpe + MDD + t-test + bootstrap CI + Bonferroni + DSR
//
// 参数:
//   trades                    — 已回放的 TradeRecord 序列
//   window                    — 当前 walk-forward 时间窗
//   bucket                    — PREGAME / INPLAY 桶 (分桶独立计算, 不混合)
//   is_in_sample              — true=IS, false=OOS
//   bankroll                  — 初始本金 (USDC)
//   n_bootstrap               — bootstrap 重采样次数 (默认 5000)
//   block_size                — stationary block 长度 (天, 默认 10)
//   bootstrap_seed            — bootstrap 随机 seed (确定性复现)
//   n_bonferroni_experiments  — 参数扫描组合数 (P0-02: 81)
//   n_strategies_dsr          — DSR 策略数 (= n_bonferroni_experiments)

[[nodiscard]] BacktestMetrics compute_full_metrics(
    std::vector<TradeRecord> const& trades,
    WalkForwardWindow const&        window,
    TradeBucket                     bucket,
    bool                            is_in_sample,
    double                          bankroll            = 100'000.0,
    std::size_t                     n_bootstrap         = 5000,
    std::size_t                     block_size          = 10,
    std::uint64_t                   bootstrap_seed      = 42,
    std::size_t                     n_bonferroni_experiments = 81,
    std::size_t                     n_strategies_dsr    = 81);

}  // namespace stcpp::backtest
