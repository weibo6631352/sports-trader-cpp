// tests/unit/test_backtest_stats.cpp — Bonferroni + DSR 过拟合检测单测
//
// Owner: 小蒋 (quant-backtest)
// Wave: backtest framework v0.2
// Last review: 2026-05-29
//
// 覆盖:
//   T01: bonferroni_adjust — 单值校正 (N=81)
//   T02: bonferroni_adjust — 多值校正 (上限 clamp = 1.0)
//   T03: bonferroni_adjust_single — N=1 恒等
//   T04: compute_deflated_sharpe — 高 Sharpe → DSR > 1.0 pass
//   T05: compute_deflated_sharpe — 低 Sharpe → DSR < 1.0 fail
//   T06: compute_deflated_sharpe — 无效输入 (T=1) → valid=false
//   T07: normal_quantile — 已知分位数 (0.5→0, 0.975→~1.96)
//   T08: select_best_params — pass 中选最高 Sharpe
//   T09: extract_net_edge_series — 过滤 Pending + filtered_out
//   T10: DSR correction 随 N 增大而增大 (过拟合惩罚更重)

#include <cmath>

#include <gtest/gtest.h>

#include "stcpp/backtest/stats.hpp"

namespace stcpp::backtest::stats {
namespace {

// Pull in types from stcpp::backtest
using stcpp::backtest::SettleOutcome;
using stcpp::backtest::TradeRecord;
using stcpp::backtest::TradeSide;

// T01: 单值 Bonferroni 校正 N=81
TEST(Bonferroni, SingleValue) {
    // p=0.001, N=81 → adjusted = 0.081
    double const adj = bonferroni_adjust_single(0.001, 81);
    EXPECT_NEAR(adj, 0.081, 1e-10);
    EXPECT_GT(adj, 0.05);  // 校正后不显著
}

// T02: 多值 + 上限 clamp
TEST(Bonferroni, MultipleValuesClamped) {
    std::vector<double> pv = {0.001, 0.01, 0.5, 0.02};
    auto const adj = bonferroni_adjust(pv, 10);
    // 0.001 × 10 = 0.01; 0.01 × 10 = 0.1; 0.5 × 10 = clamp(5) = 1.0; 0.02 × 10 = 0.2
    EXPECT_NEAR(adj[0], 0.01, 1e-10);
    EXPECT_NEAR(adj[1], 0.10, 1e-10);
    EXPECT_NEAR(adj[2], 1.00, 1e-10);  // clamped
    EXPECT_NEAR(adj[3], 0.20, 1e-10);
}

// T03: N=1 → 恒等变换
TEST(Bonferroni, NOne) {
    double const adj = bonferroni_adjust_single(0.03, 1);
    EXPECT_NEAR(adj, 0.03, 1e-10);
}

// T04: 高 Sharpe → DSR > 1.0 (pass)
TEST(Dsr, HighSharpePass) {
    DsrInput in;
    in.sr_hat = 3.0;  // 高 Sharpe
    in.sigma_sr = 0.3;
    in.gamma = 0.0;
    in.T = 200;
    in.N = 81;

    auto const res = compute_deflated_sharpe(in);
    EXPECT_TRUE(res.valid);
    EXPECT_TRUE(res.pass);
    EXPECT_GT(res.deflated_sharpe, 1.0);
}

// T05: 低 Sharpe → DSR < 1.0 (fail)
TEST(Dsr, LowSharpeFail) {
    DsrInput in;
    in.sr_hat = 0.5;  // 偏低
    in.sigma_sr = 0.5;
    in.gamma = 0.0;
    in.T = 50;
    in.N = 81;

    auto const res = compute_deflated_sharpe(in);
    EXPECT_TRUE(res.valid);
    EXPECT_FALSE(res.pass);
    EXPECT_LT(res.deflated_sharpe, 1.0);
}

// T06: 无效输入 (T < 2) → valid=false
TEST(Dsr, InvalidInput) {
    DsrInput in;
    in.sr_hat = 2.0;
    in.sigma_sr = 0.3;
    in.T = 1;  // 无效
    in.N = 81;

    auto const res = compute_deflated_sharpe(in);
    EXPECT_FALSE(res.valid);
}

// T07: normal_quantile 已知分位数
TEST(NormalQuantile, KnownValues) {
    // Φ^-1(0.5) = 0
    EXPECT_NEAR(detail::normal_quantile(0.5), 0.0, 0.01);
    // Φ^-1(0.975) ≈ 1.96
    EXPECT_NEAR(detail::normal_quantile(0.975), 1.96, 0.02);
    // Φ^-1(0.025) ≈ -1.96
    EXPECT_NEAR(detail::normal_quantile(0.025), -1.96, 0.02);
    // Φ^-1(0.84) ≈ 1.0
    EXPECT_NEAR(detail::normal_quantile(0.84), 1.0, 0.02);
}

// T08: select_best_params — 从 pass 中选最高 Sharpe
TEST(SelectBest, PicksHighestPassingSharpe) {
    std::vector<ScanResult> results(3);

    // result 0: p=0.001 (小), Sharpe=1.2, DSR pass
    results[0].is_p_value = 0.001;
    results[0].is_sharpe = 1.2;
    results[0].dsr.deflated_sharpe = 1.5;
    results[0].dsr.pass = true;
    results[0].dsr.valid = true;
    results[0].n_trades = 100;

    // result 1: p=0.001, Sharpe=0.8, DSR pass
    results[1].is_p_value = 0.001;
    results[1].is_sharpe = 0.8;
    results[1].dsr.deflated_sharpe = 1.2;
    results[1].dsr.pass = true;
    results[1].dsr.valid = true;
    results[1].n_trades = 100;

    // result 2: p=0.8 → Bonferroni 校正后 > 0.05 → fail
    results[2].is_p_value = 0.8;
    results[2].is_sharpe = 2.0;  // 高但 p 不显著
    results[2].dsr.deflated_sharpe = 2.0;
    results[2].dsr.pass = true;
    results[2].dsr.valid = true;
    results[2].n_trades = 100;

    auto const best = select_best_params(results, 3 /*n_experiments*/);
    // result 0 和 1 在 N=3 下 Bonferroni 校正:
    //   0: 0.001 × 3 = 0.003 < 0.05 ✓
    //   1: 0.001 × 3 = 0.003 < 0.05 ✓
    //   2: 0.8 × 3 = 2.4 → clamp → 1.0 > 0.05 ✗
    // 最优 = result 0 (Sharpe=1.2)
    EXPECT_NEAR(best.is_sharpe, 1.2, 1e-9);
}

// T09: extract_net_edge_series — 过滤 Pending + filtered_out
TEST(ExtractSeries, FilterPendingAndFiltered) {
    std::vector<TradeRecord> trades;

    // 正常已结算
    {
        TradeRecord t;
        t.event_ts_ns = t.data_source_ts_ns = t.ingestion_ts_ns = t.as_of_ts_ns = 1;
        t.outcome = SettleOutcome::YesWins;
        t.realized_pnl_usdc = 10.0;
        t.size_usdc = 100.0;
        trades.push_back(t);
    }
    // Pending — 排除
    {
        TradeRecord t;
        t.event_ts_ns = t.data_source_ts_ns = t.ingestion_ts_ns = t.as_of_ts_ns = 2;
        t.outcome = SettleOutcome::Pending;
        t.realized_pnl_usdc = 0.0;
        t.size_usdc = 100.0;
        trades.push_back(t);
    }
    // filtered_out — 排除
    {
        TradeRecord t;
        t.event_ts_ns = t.data_source_ts_ns = t.ingestion_ts_ns = t.as_of_ts_ns = 3;
        t.outcome = SettleOutcome::YesWins;
        t.realized_pnl_usdc = 20.0;
        t.size_usdc = 100.0;
        t.filtered_out = true;
        trades.push_back(t);
    }

    auto const series = extract_net_edge_series(trades);
    ASSERT_EQ(series.size(), 1u);
    EXPECT_NEAR(series[0], 10.0 / 100.0, 1e-10);
}

// T10: DSR correction 随 N 增大而增大
TEST(Dsr, CorrectionGrowsWithN) {
    DsrInput base;
    base.sr_hat = 2.0;
    base.sigma_sr = 0.3;
    base.gamma = 0.0;
    base.T = 100;

    base.N = 10;
    auto const r10 = compute_deflated_sharpe(base);

    base.N = 81;
    auto const r81 = compute_deflated_sharpe(base);

    base.N = 500;
    auto const r500 = compute_deflated_sharpe(base);

    ASSERT_TRUE(r10.valid);
    ASSERT_TRUE(r81.valid);
    ASSERT_TRUE(r500.valid);

    // 更多策略数 → 更大 correction → 更小 DSR
    EXPECT_GT(r10.deflated_sharpe, r81.deflated_sharpe);
    EXPECT_GT(r81.deflated_sharpe, r500.deflated_sharpe);
}

}  // namespace
}  // namespace stcpp::backtest::stats
