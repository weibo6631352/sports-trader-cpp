// tests/unit/test_portfolio_metrics.cpp — Phase 0 项5 组合度量 (Sharpe/maxDD/VaR)
#include <cmath>
#include <gtest/gtest.h>

#include <memory>

#include "stcpp/eval/portfolio_metrics.hpp"

using stcpp::eval::PortfolioMetrics;

namespace {
std::unique_ptr<PortfolioMetrics> MakeCurve(std::initializer_list<double> eq) {
    auto m = std::make_unique<PortfolioMetrics>(1000, /*ppy=*/0.0);  // 2026-06-11: +mutex 后不可拷贝
    std::int64_t t = 1'000;
    for (double e : eq) {
        m->RecordEquity(t, e);
        t += 1'000'000'000LL;  // 1s 等间隔
    }
    return m;
}
}  // namespace

// PM01: 已知曲线 → total_return / maxDD / VaR 数值正确
TEST(PortfolioMetrics, PM01_KnownCurve) {
    auto m = MakeCurve({100, 110, 105, 120, 90, 130});
    const auto r = m->report(/*ppy=*/1.0);  // ppy=1 → sharpe = mean/std (不年化)
    EXPECT_EQ(r.samples, 6u);
    EXPECT_NEAR(r.total_return, 0.30, 1e-9);          // (130−100)/100
    EXPECT_NEAR(r.max_drawdown, 0.25, 1e-9);          // 峰 120 → 谷 90 = 25%
    EXPECT_NEAR(r.peak_equity, 130.0, 1e-9);
    EXPECT_NEAR(r.last_equity, 130.0, 1e-9);
    EXPECT_GT(r.sharpe, 0.0) << "均值正收益 → Sharpe>0";
    EXPECT_TRUE(std::isfinite(r.sharpe));
    // VaR95: 5 个收益排序后 5% 分位损失 ≈ 0.209 (最差 −0.25 与次差 −0.045 间插值)
    EXPECT_NEAR(r.var_95, 0.2091, 1e-3);
    EXPECT_GT(r.var_99, r.var_95) << "99% VaR ≥ 95% (更尾部)";
}

// PM02: 单调上升 → 零回撤
TEST(PortfolioMetrics, PM02_MonotonicNoDrawdown) {
    auto m = MakeCurve({100, 101, 103, 108, 120});
    const auto r = m->report(1.0);
    EXPECT_NEAR(r.max_drawdown, 0.0, 1e-12) << "单调上升无回撤";
    EXPECT_GT(r.total_return, 0.0);
    EXPECT_GT(r.sharpe, 0.0);
}

// PM03: 样本不足 (<2 收益) → Sharpe/VaR = 0, 不崩
TEST(PortfolioMetrics, PM03_InsufficientSamples) {
    auto m = MakeCurve({100});
    const auto r = m->report(1.0);
    EXPECT_EQ(r.samples, 1u);
    EXPECT_DOUBLE_EQ(r.sharpe, 0.0);
    EXPECT_DOUBLE_EQ(r.var_95, 0.0);
    EXPECT_DOUBLE_EQ(r.max_drawdown, 0.0);
}

// PM04: 年化因子放大 Sharpe (sqrt(ppy))
TEST(PortfolioMetrics, PM04_Annualization) {
    auto m = MakeCurve({100, 101, 102, 103, 104});
    const double s1 = m->report(1.0).sharpe;
    const double s4 = m->report(4.0).sharpe;
    EXPECT_NEAR(s4, s1 * 2.0, 1e-6) << "sqrt(4)=2 → Sharpe 翻倍";
}

// PM05: 非有限输入被忽略, 不污染
TEST(PortfolioMetrics, PM05_NaNIgnored) {
    PortfolioMetrics m(1000, 1.0);
    m.RecordEquity(1, 100.0);
    m.RecordEquity(2, std::numeric_limits<double>::quiet_NaN());  // 忽略
    m.RecordEquity(3, 110.0);
    const auto r = m.report();
    EXPECT_EQ(r.samples, 2u) << "NaN 不入样本";
    EXPECT_NEAR(r.total_return, 0.10, 1e-9);
}
