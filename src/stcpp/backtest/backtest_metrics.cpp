// stcpp/backtest/backtest_metrics.cpp — BacktestMetrics 汇总计算实现
//
// Owner: 小蒋 (quant-backtest)
// Last review: 2026-05-29
//
// 本文件实现 compute_full_metrics — 从 TradeRecord 序列生成完整 BacktestMetrics,
// 包含: basic stats + Sharpe + MDD + t-test + bootstrap CI + Bonferroni + DSR.
//
// 红线: R-7 / R-11 / R-20

#include "stcpp/backtest/backtest_metrics.hpp"

#include <algorithm>
#include <cmath>

namespace stcpp::backtest {

BacktestMetrics compute_full_metrics(
    std::vector<TradeRecord> const& trades,
    WalkForwardWindow const&        window,
    TradeBucket                     bucket,
    bool                            is_in_sample,
    double                          bankroll,
    std::size_t                     n_bootstrap,
    std::size_t                     block_size,
    std::uint64_t                   bootstrap_seed,
    std::size_t                     n_bonferroni_experiments,
    std::size_t                     n_strategies_dsr) {

    BacktestMetrics m;
    m.bucket       = bucket;
    m.is_in_sample = is_in_sample;
    m.fold_index   = window.fold_index;

    // 过滤出目标桶的 trade
    std::vector<TradeRecord> bucket_trades;
    bucket_trades.reserve(trades.size());
    for (auto const& t : trades) {
        if (!t.filtered_out && t.bucket == bucket
            && t.outcome != SettleOutcome::Pending) {
            bucket_trades.push_back(t);
        }
    }

    // --- Basic stats ---
    auto const basic = MetricsComputer::compute_basic(bucket_trades);
    m.n_trades       = basic.n_trades;
    m.n_wins         = basic.n_wins;
    m.hit_rate       = basic.hit_rate;
    m.net_edge_mean  = basic.net_edge_mean;
    m.net_edge_std   = basic.net_edge_std;
    m.total_net_pnl  = basic.total_net_pnl;

    if (m.n_trades < 2) {
        return m;
    }

    // --- MDD ---
    // 先按 as_of_ts 排序 (确保 MDD 时序正确)
    std::vector<TradeRecord> sorted = bucket_trades;
    std::sort(sorted.begin(), sorted.end(), [](TradeRecord const& a, TradeRecord const& b) {
        return a.as_of_ts_ns < b.as_of_ts_ns;
    });

    m.max_drawdown = MetricsComputer::compute_max_drawdown(sorted, bankroll);

    // --- 日收益率 + Sharpe ---
    std::int64_t const win_start = is_in_sample ? window.is_start_ns : window.oos_start_ns;
    std::int64_t const win_end   = is_in_sample ? window.is_end_ns   : window.oos_end_ns;

    auto const daily_rets = MetricsComputer::compute_daily_returns(
        sorted, bankroll, win_start, win_end);

    m.sharpe_ratio = MetricsComputer::compute_sharpe(daily_rets, 252.0);

    // --- t-test ---
    auto const edges = stats::extract_net_edge_series(sorted);
    auto const tt    = MetricsComputer::one_sample_t_test(edges);
    m.t_stat  = tt.t_stat;
    m.p_value = tt.p_value;

    // --- Bonferroni 校正 ---
    m.bonferroni_p = stats::bonferroni_adjust_single(m.p_value, n_bonferroni_experiments);

    // --- Bootstrap Sharpe CI ---
    StationaryBootstrap boot(n_bootstrap, block_size, bootstrap_seed);
    auto const boot_result = boot.compute_sharpe_ci(daily_rets, 252.0);
    m.sharpe_ci_lower = boot_result.ci_lower;
    m.sharpe_ci_upper = boot_result.ci_upper;

    // --- DSR ---
    stats::DsrInput dsr_in;
    dsr_in.sr_hat    = m.sharpe_ratio;
    dsr_in.sigma_sr  = boot_result.std_sharpe > 0.0 ? boot_result.std_sharpe : 0.3;
    dsr_in.gamma     = 0.0;  // MVP 保守
    dsr_in.T         = m.n_trades;
    dsr_in.N         = n_strategies_dsr;
    auto const dsr   = stats::compute_deflated_sharpe(dsr_in);
    m.deflated_sharpe = dsr.deflated_sharpe;

    return m;
}

}  // namespace stcpp::backtest
