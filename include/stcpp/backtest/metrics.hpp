// stcpp/backtest/metrics.hpp — Backtest metrics computation v0.2
//
// Owner: 小蒋 (quant-backtest)
// Last review: 2026-05-29
//
// 落: xiaojiang-backtest-framework-v0.2-cpp.md §4 (过拟合检测)
//     xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md §3.3 (度量指标口径)
//
// 红线:
//   R-7  纯函数, mode-agnostic
//   R-11 不写 ledger
//   - 全部指标口径以 net edge (扣 fee + slippage) 为基础
//   - Sharpe: 日收益率序列, 年化 ×√252 (不用交易天年化, 防低频虚高)
//   - MDD: 以累计 net PnL 曲线计, 不用收益率曲线
//   - bootstrap: Stationary Bootstrap (Politis & Romano 1994), n=5000, block_size=10d
//
// 依赖:
//   <cmath>  std::sqrt / std::exp (无 Boost 依赖, 保持本 lib 轻量)
//   <random> std::mt19937_64 (bootstrap 重采样)

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "stcpp/backtest/types.hpp"

namespace stcpp::backtest {

// ---------------------------------------------------------------------------
// 1. MetricsComputer — 静态纯函数, 计算所有回测指标
// ---------------------------------------------------------------------------

class MetricsComputer {
public:
    // -----------------------------------------------------------------------
    // 1.1 基础: Hit Rate + Net Edge
    // -----------------------------------------------------------------------

    // 计算 hit rate + net edge + basic counts (无需排序, 一次遍历)
    struct BasicStats {
        std::size_t n_trades{0};
        std::size_t n_wins{0};
        double hit_rate{0.0};
        double net_edge_mean{0.0};
        double net_edge_std{0.0};
        double total_net_pnl{0.0};
        bool valid{false};
    };

    [[nodiscard]] static BasicStats compute_basic(std::vector<TradeRecord> const& trades) noexcept {
        BasicStats out;
        out.n_trades = trades.size();
        if (out.n_trades == 0) {
            return out;
        }

        // Re-count settled trades only
        std::size_t n_settled = 0;
        out.n_wins = 0;
        double sum_edge = 0.0;
        double sum_sq = 0.0;
        double sum_pnl = 0.0;
        for (auto const& t : trades) {
            if (t.filtered_out)
                continue;
            if (t.outcome == SettleOutcome::Pending)
                continue;
            ++n_settled;
            sum_pnl += t.realized_pnl_usdc;
            double const e = (t.size_usdc > 0.0) ? t.realized_pnl_usdc / t.size_usdc : 0.0;
            sum_edge += e;
            sum_sq += e * e;
            if (is_winning_trade(t.side, t.outcome)) {
                ++out.n_wins;
            }
        }
        out.n_trades = n_settled;
        if (out.n_trades == 0) {
            return out;
        }

        double const n = static_cast<double>(out.n_trades);
        out.hit_rate = static_cast<double>(out.n_wins) / n;
        out.net_edge_mean = sum_edge / n;
        out.total_net_pnl = sum_pnl;

        // 无偏 std dev (n ≥ 2)
        if (out.n_trades >= 2) {
            double const variance = (sum_sq - (sum_edge * sum_edge) / n) / (n - 1.0);
            out.net_edge_std = (variance > 0.0) ? std::sqrt(variance) : 0.0;
        }

        out.valid = true;
        return out;
    }

    // -----------------------------------------------------------------------
    // 1.2 日收益率序列 (Sharpe 计算用)
    // -----------------------------------------------------------------------
    //
    // 小程 §3.3 Sharpe 计算细节:
    //   - 每日 sum(net_pnl_per_trade) / bankroll
    //   - 若某日无交易: 收益率 = 0 (不排除, 保持样本连续性)
    //   - 年化: × sqrt(252) (日历天年化)
    //
    // 调用者需提供 bankroll (初始本金) 和 trades 已按 as_of_ts 排序
    //

    [[nodiscard]] static std::vector<double> compute_daily_returns(std::vector<TradeRecord> const& trades,
                                                                   double bankroll,
                                                                   std::int64_t window_start_ns,
                                                                   std::int64_t window_end_ns) noexcept {
        if (trades.empty() || bankroll <= 0.0) {
            return {};
        }

        static constexpr std::int64_t kDayNs = 86'400'000'000'000LL;
        std::int64_t const n_days = (window_end_ns - window_start_ns) / kDayNs;
        if (n_days <= 0) {
            return {};
        }

        std::vector<double> daily_ret(static_cast<std::size_t>(n_days), 0.0);

        for (auto const& t : trades) {
            if (t.filtered_out)
                continue;
            if (t.outcome == SettleOutcome::Pending)
                continue;
            if (t.as_of_ts_ns < window_start_ns || t.as_of_ts_ns >= window_end_ns)
                continue;

            std::int64_t const day_idx = (t.as_of_ts_ns - window_start_ns) / kDayNs;
            if (day_idx >= 0 && static_cast<std::size_t>(day_idx) < daily_ret.size()) {
                daily_ret[static_cast<std::size_t>(day_idx)] += t.realized_pnl_usdc / bankroll;
            }
        }

        return daily_ret;
    }

    // -----------------------------------------------------------------------
    // 1.3 Sharpe Ratio
    // -----------------------------------------------------------------------

    [[nodiscard]] static double compute_sharpe(std::vector<double> const& daily_returns,
                                               double annualizer = 252.0) noexcept {
        if (daily_returns.size() < 2) {
            return 0.0;
        }

        double sum = 0.0;
        double sum_sq = 0.0;
        double const n = static_cast<double>(daily_returns.size());

        for (double r : daily_returns) {
            sum += r;
            sum_sq += r * r;
        }

        double const mean = sum / n;
        double const variance = (sum_sq - sum * sum / n) / (n - 1.0);

        if (variance <= 0.0) {
            return 0.0;
        }

        return mean / std::sqrt(variance) * std::sqrt(annualizer);
    }

    // -----------------------------------------------------------------------
    // 1.4 MDD (Maximum Drawdown)
    // -----------------------------------------------------------------------
    //
    // 以累计 net PnL 曲线 (USDC) 计算:
    // MDD = max(peak - trough) / peak_equity
    //   peak_equity = initial_bankroll + cumulative_pnl_at_peak
    //

    [[nodiscard]] static double compute_max_drawdown(std::vector<TradeRecord> const& trades,
                                                     double initial_bankroll) noexcept {
        if (trades.empty() || initial_bankroll <= 0.0) {
            return 0.0;
        }

        double peak_equity = initial_bankroll;
        double current_equity = initial_bankroll;
        double max_dd = 0.0;

        for (auto const& t : trades) {
            if (t.filtered_out)
                continue;
            if (t.outcome == SettleOutcome::Pending)
                continue;

            current_equity += t.realized_pnl_usdc;
            if (current_equity > peak_equity) {
                peak_equity = current_equity;
            }
            if (peak_equity > 0.0) {
                double const dd = (peak_equity - current_equity) / peak_equity;
                if (dd > max_dd) {
                    max_dd = dd;
                }
            }
        }

        return max_dd;
    }

    // -----------------------------------------------------------------------
    // 1.5 t-test (单尾, H0: E[net_pnl] ≤ 0)
    // -----------------------------------------------------------------------

    struct TTestResult {
        double t_stat{0.0};
        double p_value{1.0};  // 单尾 p-value (高 → 不拒绝 H0)
        bool valid{false};
    };

    // 简化版单样本 t-test: t = mean / (std / sqrt(n))
    // p-value 用 t-分布近似 (Student's t, df = n-1)
    // 注: 完整 t 分布 CDF 需 Boost.Math; 此处用正态近似 (n ≥ 30 近似合理)
    [[nodiscard]] static TTestResult one_sample_t_test(std::vector<double> const& values) noexcept {
        TTestResult out;
        std::size_t const n = values.size();
        if (n < 2) {
            return out;
        }

        double sum = 0.0;
        double sum_sq = 0.0;
        for (double v : values) {
            sum += v;
            sum_sq += v * v;
        }

        double const dn = static_cast<double>(n);
        double const mean = sum / dn;
        double const variance = (sum_sq - sum * sum / dn) / (dn - 1.0);

        if (variance <= 0.0) {
            // 零方差: 全部值相同, t-stat 无意义
            out.t_stat = (mean > 0.0) ? 1e9 : (mean < 0.0 ? -1e9 : 0.0);
            out.p_value = (mean > 0.0) ? 0.0 : 1.0;
            out.valid = true;
            return out;
        }

        double const std_err = std::sqrt(variance / dn);
        out.t_stat = mean / std_err;

        // 正态近似 p-value (单尾, H0: mean ≤ 0)
        // P(Z > t_stat) ≈ 0.5 * erfc(t_stat / sqrt(2))
        // erfc 在 <cmath> 中可用
        out.p_value = 0.5 * std::erfc(out.t_stat / std::sqrt(2.0));
        out.valid = true;
        return out;
    }
};

// ---------------------------------------------------------------------------
// 2. StationaryBootstrap — Politis & Romano 1994 (§3.4)
// ---------------------------------------------------------------------------
//
// block_size 参数: 平均 block 长度 (天), 默认 10.
// n_bootstrap: 重采样次数, 默认 5000.
// CI: 90% 双侧 (5th ~ 95th percentile).
//

class StationaryBootstrap {
public:
    explicit StationaryBootstrap(std::size_t n_bootstrap = 5000, std::size_t block_size = 10,
                                 std::uint64_t seed = 42)
        : n_bootstrap_(n_bootstrap), block_size_(block_size), seed_(seed) {}

    // Stationary Bootstrap: 随机 block 长度 Geometric(1/block_size)
    // 输入: daily_returns 序列
    // 输出: BootstrapResult (Sharpe CI)
    [[nodiscard]] BootstrapResult compute_sharpe_ci(std::vector<double> const& daily_returns,
                                                    double annualizer = 252.0) const {
        BootstrapResult out;
        out.n_bootstrap = n_bootstrap_;
        out.block_size = block_size_;

        std::size_t const T = daily_returns.size();
        if (T < 2) {
            return out;
        }

        std::mt19937_64 rng(seed_);
        std::uniform_int_distribution<std::size_t> start_dist(0, T - 1);
        std::geometric_distribution<std::size_t> block_len_dist(1.0 / static_cast<double>(block_size_));

        std::vector<double> bootstrap_sharpes;
        bootstrap_sharpes.reserve(n_bootstrap_);

        std::vector<double> resample;
        resample.reserve(T);

        for (std::size_t b = 0; b < n_bootstrap_; ++b) {
            resample.clear();

            // Stationary bootstrap: 随机选起点, block 长度几何分布
            while (resample.size() < T) {
                std::size_t const start = start_dist(rng);
                // 几何分布均值 = block_size, 但至少 1
                std::size_t const len = block_len_dist(rng) + 1;
                std::size_t const actual_len = std::min(len, T - resample.size());
                for (std::size_t i = 0; i < actual_len; ++i) {
                    resample.push_back(daily_returns[(start + i) % T]);
                }
            }
            // 截断到 T
            resample.resize(T);

            double const sr = MetricsComputer::compute_sharpe(resample, annualizer);
            bootstrap_sharpes.push_back(sr);
        }

        // 排序取 percentile
        std::sort(bootstrap_sharpes.begin(), bootstrap_sharpes.end());

        auto const n = bootstrap_sharpes.size();
        if (n == 0) {
            return out;
        }

        // 90% CI: 5th ~ 95th percentile
        std::size_t const lo_idx = static_cast<std::size_t>(0.05 * static_cast<double>(n));
        std::size_t const hi_idx = static_cast<std::size_t>(0.95 * static_cast<double>(n));

        out.ci_lower = bootstrap_sharpes[lo_idx];
        out.ci_upper = bootstrap_sharpes[std::min(hi_idx, n - 1)];

        // 均值 + std
        double sum = 0.0;
        double sum_sq = 0.0;
        for (double sr : bootstrap_sharpes) {
            sum += sr;
            sum_sq += sr * sr;
        }
        double const dn = static_cast<double>(n);
        out.mean_sharpe = sum / dn;
        double const variance = (sum_sq - sum * sum / dn) / (dn - 1.0);
        out.std_sharpe = (variance > 0.0) ? std::sqrt(variance) : 0.0;

        return out;
    }

private:
    std::size_t n_bootstrap_;
    std::size_t block_size_;
    std::uint64_t seed_;
};

}  // namespace stcpp::backtest
