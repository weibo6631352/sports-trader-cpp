// include/stcpp/eval/portfolio_metrics.hpp — 组合度量层 (Sharpe / maxDD / VaR)
//
// Owner: 老雷 (GM) — Phase 0 项5 (联合评审 2026-05-31, 小梁: 北极星 KPI「连分母都没采」)
// last_review: 2026-05-31
//
// 北极星: 单策略年化 PnL ≥ $5M / Sharpe ≥ 1.5 / 最大回撤 ≤ 15%。这三个 KPI 此前无任何采集。
// 本模块从权益曲线 (equity = bankroll + 累计 realized PnL [+ 未实现 MtM, 后续]) 周期采样,
//   派生 Sharpe / 最大回撤 / 历史 VaR。纯计算, header-only, 无 IO/无锁 (loop_thread_ 单 writer)。
//   离线/监控用, 绝不回喂决策 (与 CLV tracker 同定位)。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace stcpp::eval {

class PortfolioMetrics {
public:
    struct Report {
        std::size_t samples{0};
        double total_return{0.0};   // (last − first) / first
        double sharpe{0.0};         // 年化 Sharpe (mean/std × sqrt(periods_per_year)); <2 样本 = 0
        double max_drawdown{0.0};   // 最大回撤 ∈ [0,1] (峰到谷跌幅占比)
        double var_95{0.0};         // 历史 VaR 95% (单期损失幅度, 正数=亏)
        double var_99{0.0};         // 历史 VaR 99%
        double last_equity{0.0};
        double peak_equity{0.0};
    };

    explicit PortfolioMetrics(std::size_t cap = 100'000, double periods_per_year = 0.0) noexcept
        : cap_(cap == 0 ? 1 : cap), periods_per_year_(periods_per_year) {}

    // RecordEquity — 周期采样权益 (单 writer)。ts 仅留作未来按时间归一; 当前按等间隔样本处理。
    void RecordEquity(std::int64_t ts_ns, double equity) noexcept {
        if (!std::isfinite(equity)) return;
        equity_.push_back(equity);
        ts_.push_back(ts_ns);
        while (equity_.size() > cap_) {
            equity_.pop_front();
            ts_.pop_front();
        }
        if (equity > peak_) peak_ = equity;
        // 回撤实时跟踪 (峰到谷, 不受 ring 淘汰影响)。
        if (peak_ > 0.0) {
            const double dd = (peak_ - equity) / peak_;
            if (dd > max_dd_) max_dd_ = dd;
        }
    }

    // periods_per_year: 年化因子 (e.g. 采样间隔 1s → 31.5M; 1tick/500ms → ~63M)。0 = 用构造值。
    [[nodiscard]] Report report(double periods_per_year = 0.0) const noexcept {
        Report r;
        r.samples = equity_.size();
        if (equity_.empty()) return r;
        r.last_equity = equity_.back();
        r.peak_equity = peak_;
        r.max_drawdown = max_dd_;
        const double first = equity_.front();
        if (first != 0.0) r.total_return = (equity_.back() - first) / first;

        // 单期简单收益序列 ret[i] = (E[i]−E[i-1]) / E[i-1]。
        std::vector<double> rets;
        rets.reserve(equity_.size());
        for (std::size_t i = 1; i < equity_.size(); ++i) {
            const double prev = equity_[i - 1];
            if (prev != 0.0 && std::isfinite(prev) && std::isfinite(equity_[i])) {
                rets.push_back((equity_[i] - prev) / prev);
            }
        }
        if (rets.size() >= 2) {
            double sum = 0.0;
            for (double x : rets) sum += x;
            const double mean = sum / static_cast<double>(rets.size());
            double var = 0.0;
            for (double x : rets) var += (x - mean) * (x - mean);
            var /= static_cast<double>(rets.size() - 1);  // 样本方差
            const double sd = std::sqrt(var);
            const double ppy = (periods_per_year > 0.0) ? periods_per_year : periods_per_year_;
            if (sd > 0.0) {
                r.sharpe = (mean / sd) * ((ppy > 0.0) ? std::sqrt(ppy) : 1.0);
            }
            // 历史 VaR: 收益升序, 取低分位的损失幅度 (正数)。
            std::vector<double> sorted = rets;
            std::sort(sorted.begin(), sorted.end());
            r.var_95 = -PercentileLow(sorted, 0.05);
            r.var_99 = -PercentileLow(sorted, 0.01);
        }
        return r;
    }

    [[nodiscard]] std::size_t sample_count() const noexcept { return equity_.size(); }
    [[nodiscard]] double max_drawdown() const noexcept { return max_dd_; }

private:
    // 低分位插值 (sorted 升序)。q∈(0,1); 返回该分位的收益值 (通常为负=损失)。
    [[nodiscard]] static double PercentileLow(const std::vector<double>& sorted, double q) noexcept {
        if (sorted.empty()) return 0.0;
        if (sorted.size() == 1) return sorted.front();
        const double idx = q * static_cast<double>(sorted.size() - 1);
        const std::size_t lo = static_cast<std::size_t>(std::floor(idx));
        const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
        const double frac = idx - static_cast<double>(lo);
        return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
    }

    std::size_t cap_;
    double periods_per_year_;
    std::deque<double> equity_;
    std::deque<std::int64_t> ts_;
    double peak_{0.0};
    double max_dd_{0.0};
};

}  // namespace stcpp::eval
