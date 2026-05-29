// tests/unit/test_backtest_metrics.cpp — MetricsComputer + StationaryBootstrap 单测
//
// Owner: 小蒋 (quant-backtest)
// Wave: backtest framework v0.2
// Last review: 2026-05-29
//
// 覆盖:
//   T01: compute_basic — 空序列 → valid=false
//   T02: compute_basic — 全 win 序列
//   T03: compute_basic — 混合 win/lose 序列 hit rate + net_edge
//   T04: compute_basic — filtered_out=true 排除
//   T05: compute_daily_returns — 日收益率聚合
//   T06: compute_sharpe — 正 Sharpe
//   T07: compute_sharpe — 序列太短 → 0
//   T08: compute_max_drawdown — 先涨后跌
//   T09: compute_max_drawdown — 单调上涨 → MDD=0
//   T10: one_sample_t_test — 显著正均值 (p < 0.05)
//   T11: one_sample_t_test — 均值=0 → p≈0.5
//   T12: StationaryBootstrap — CI 下界 < 均值 < CI 上界
//   T13: StationaryBootstrap — 确定性 (同 seed 同结果)
//   T14: StationaryBootstrap — 序列太短 → 空 result (n=0 → ci=0)

#include <algorithm>
#include <cmath>

#include <gtest/gtest.h>

#include "stcpp/backtest/metrics.hpp"

namespace stcpp::backtest {
namespace {

static constexpr std::int64_t kDayNs = 86'400'000'000'000LL;

// 辅助: 构造已结算 TradeRecord
TradeRecord make_settled(TradeSide side, SettleOutcome outcome, double fill_price, double size,
                         std::int64_t as_of_ns, bool filtered = false) {
    TradeRecord t;
    t.event_ts_ns = as_of_ns - 3;
    t.data_source_ts_ns = as_of_ns - 2;
    t.ingestion_ts_ns = as_of_ns - 1;
    t.as_of_ts_ns = as_of_ns;
    t.side = side;
    t.outcome = outcome;
    t.fill_price = fill_price;
    t.size_usdc = size;
    t.fee_rate = 0.03;
    t.slippage_rate = 0.003;
    t.bucket = TradeBucket::Pregame;
    t.filtered_out = filtered;
    t.realized_pnl_usdc = compute_realized_pnl(side, outcome, fill_price, size, t.fee_rate, t.slippage_rate);
    return t;
}

// T01: 空序列
TEST(MetricsBasic, EmptySequence) {
    std::vector<TradeRecord> trades;
    auto const s = MetricsComputer::compute_basic(trades);
    EXPECT_FALSE(s.valid);
    EXPECT_EQ(s.n_trades, 0u);
}

// T02: 全 win 序列 (5 笔)
TEST(MetricsBasic, AllWin) {
    std::vector<TradeRecord> trades;
    for (int i = 0; i < 5; ++i) {
        trades.push_back(make_settled(TradeSide::BuyYes, SettleOutcome::YesWins, 0.45, 100.0,
                                      static_cast<std::int64_t>(i * kDayNs + 1000)));
    }
    auto const s = MetricsComputer::compute_basic(trades);
    EXPECT_TRUE(s.valid);
    EXPECT_EQ(s.n_trades, 5u);
    EXPECT_EQ(s.n_wins, 5u);
    EXPECT_NEAR(s.hit_rate, 1.0, 1e-9);
    EXPECT_GT(s.net_edge_mean, 0.0);
    EXPECT_GT(s.total_net_pnl, 0.0);
}

// T03: 混合序列 — 3 win / 2 lose
TEST(MetricsBasic, MixedWinLose) {
    std::vector<TradeRecord> trades;
    trades.push_back(make_settled(TradeSide::BuyYes, SettleOutcome::YesWins, 0.45, 100.0, 1000));
    trades.push_back(make_settled(TradeSide::BuyYes, SettleOutcome::YesWins, 0.45, 100.0, 2000));
    trades.push_back(make_settled(TradeSide::BuyYes, SettleOutcome::YesWins, 0.45, 100.0, 3000));
    trades.push_back(make_settled(TradeSide::BuyYes, SettleOutcome::NoWins, 0.45, 100.0, 4000));
    trades.push_back(make_settled(TradeSide::BuyYes, SettleOutcome::NoWins, 0.45, 100.0, 5000));

    auto const s = MetricsComputer::compute_basic(trades);
    EXPECT_EQ(s.n_trades, 5u);
    EXPECT_EQ(s.n_wins, 3u);
    EXPECT_NEAR(s.hit_rate, 0.60, 1e-9);
}

// T04: filtered_out=true 排除
TEST(MetricsBasic, FilteredOutExcluded) {
    std::vector<TradeRecord> trades;
    trades.push_back(make_settled(TradeSide::BuyYes, SettleOutcome::YesWins, 0.45, 100.0, 1000));
    trades.push_back(make_settled(TradeSide::BuyYes, SettleOutcome::YesWins, 0.45, 100.0, 2000,
                                  /*filtered=*/true));

    auto const s = MetricsComputer::compute_basic(trades);
    // 只有 1 笔非过滤 trade
    EXPECT_EQ(s.n_trades, 1u);
}

// T05: compute_daily_returns — 日收益率聚合
TEST(MetricsDaily, DailyReturns) {
    // 3 天 window, day 0 / day 1 / day 2 各 1 笔
    std::int64_t const start = 0LL;
    std::int64_t const end = 3LL * kDayNs;
    double const bankroll = 10'000.0;

    std::vector<TradeRecord> trades;
    // day 0 (as_of = 0.5 day):
    trades.push_back(make_settled(TradeSide::BuyYes, SettleOutcome::YesWins, 0.45, 100.0, kDayNs / 2));
    // day 1 (as_of = 1.5 day):
    trades.push_back(
        make_settled(TradeSide::BuyYes, SettleOutcome::NoWins, 0.45, 100.0, kDayNs + kDayNs / 2));
    // day 2 (as_of = 2.5 day):
    trades.push_back(
        make_settled(TradeSide::BuyYes, SettleOutcome::YesWins, 0.45, 100.0, 2LL * kDayNs + kDayNs / 2));

    auto const daily = MetricsComputer::compute_daily_returns(trades, bankroll, start, end);
    ASSERT_EQ(daily.size(), 3u);

    // day 0: win → pnl > 0 → daily > 0
    EXPECT_GT(daily[0], 0.0);
    // day 1: lose → pnl < 0 → daily < 0
    EXPECT_LT(daily[1], 0.0);
    // day 2: win → pnl > 0 → daily > 0
    EXPECT_GT(daily[2], 0.0);
}

// T06: compute_sharpe — 正 Sharpe
TEST(MetricsSharpe, PositiveSharpe) {
    // 一系列正均值收益 (每天 +1%) → std=0 → Sharpe=0 (退化情况)
    std::vector<double> rets(30, 0.01);
    // std=0 → SR=0 (verify it doesn't crash)
    EXPECT_DOUBLE_EQ(MetricsComputer::compute_sharpe(rets, 252.0), 0.0);

    // 有波动的序列 → 正 Sharpe
    std::vector<double> rets2;
    for (int i = 0; i < 30; ++i) {
        rets2.push_back(0.01 + (i % 3 == 0 ? 0.002 : -0.001));
    }
    double const sr2 = MetricsComputer::compute_sharpe(rets2, 252.0);
    EXPECT_GT(sr2, 0.0);
}

// T07: 序列长度 < 2 → Sharpe = 0
TEST(MetricsSharpe, TooShort) {
    std::vector<double> one = {0.05};
    EXPECT_DOUBLE_EQ(MetricsComputer::compute_sharpe(one), 0.0);

    std::vector<double> empty;
    EXPECT_DOUBLE_EQ(MetricsComputer::compute_sharpe(empty), 0.0);
}

// T08: compute_max_drawdown — 先涨后跌
TEST(MetricsMDD, PeakThenTrough) {
    // trades: +100, +100, +100, -150, -50 (从高点跌落)
    std::int64_t ts = 1000;
    std::vector<TradeRecord> trades;

    // 3 win
    for (int i = 0; i < 3; ++i) {
        TradeRecord t;
        t.event_ts_ns = t.data_source_ts_ns = t.ingestion_ts_ns = ts;
        t.as_of_ts_ns = ts++;
        t.outcome = SettleOutcome::YesWins;
        t.realized_pnl_usdc = 100.0;
        trades.push_back(t);
    }
    // 2 big lose
    {
        TradeRecord t;
        t.event_ts_ns = t.data_source_ts_ns = t.ingestion_ts_ns = ts;
        t.as_of_ts_ns = ts++;
        t.outcome = SettleOutcome::NoWins;
        t.realized_pnl_usdc = -150.0;
        trades.push_back(t);
    }
    {
        TradeRecord t;
        t.event_ts_ns = t.data_source_ts_ns = t.ingestion_ts_ns = ts;
        t.as_of_ts_ns = ts++;
        t.outcome = SettleOutcome::NoWins;
        t.realized_pnl_usdc = -50.0;
        trades.push_back(t);
    }

    // bankroll=1000, peak=1300, trough=1100, DD=(1300-1100)/1300 ≈ 0.1538
    double const mdd = MetricsComputer::compute_max_drawdown(trades, 1000.0);
    EXPECT_NEAR(mdd, (1300.0 - 1100.0) / 1300.0, 1e-6);
    EXPECT_GT(mdd, 0.0);
}

// T09: 单调上涨 → MDD = 0
TEST(MetricsMDD, MonotonicUp) {
    std::int64_t ts = 1000;
    std::vector<TradeRecord> trades;
    for (int i = 0; i < 5; ++i) {
        TradeRecord t;
        t.event_ts_ns = t.data_source_ts_ns = t.ingestion_ts_ns = ts;
        t.as_of_ts_ns = ts++;
        t.outcome = SettleOutcome::YesWins;
        t.realized_pnl_usdc = 50.0;
        trades.push_back(t);
    }
    double const mdd = MetricsComputer::compute_max_drawdown(trades, 1000.0);
    EXPECT_DOUBLE_EQ(mdd, 0.0);
}

// T10: one_sample_t_test — 显著正均值
TEST(MetricsTTest, SignificantPositive) {
    // 100 笔净 edge, 均值 0.02, std ≈ 0.01
    std::vector<double> vals(100, 0.02);
    // 加一点波动使 std > 0
    for (int i = 0; i < 100; i += 5) {
        vals[static_cast<std::size_t>(i)] = 0.01;
    }
    auto const res = MetricsComputer::one_sample_t_test(vals);
    EXPECT_TRUE(res.valid);
    EXPECT_GT(res.t_stat, 0.0);
    EXPECT_LT(res.p_value, 0.05);  // 显著正
}

// T11: 均值=0 → p ≈ 0.5 (双侧检验转单侧)
TEST(MetricsTTest, ZeroMean) {
    // 交替 +0.01 / -0.01 → 均值=0
    std::vector<double> vals;
    for (int i = 0; i < 100; ++i) {
        vals.push_back((i % 2 == 0) ? 0.01 : -0.01);
    }
    auto const res = MetricsComputer::one_sample_t_test(vals);
    EXPECT_TRUE(res.valid);
    EXPECT_NEAR(res.t_stat, 0.0, 1e-6);
    EXPECT_NEAR(res.p_value, 0.5, 0.01);
}

// T12: StationaryBootstrap — CI 下界 < 均值 < CI 上界
TEST(Bootstrap, CiBoundsReasonable) {
    // 30 天正收益序列 (均值 0.01, std 0.003)
    std::vector<double> daily;
    for (int i = 0; i < 30; ++i) {
        double const v = 0.01 + (i % 5 == 0 ? 0.003 : -0.00075);
        daily.push_back(v);
    }

    StationaryBootstrap boot(1000 /*n_bootstrap*/, 5 /*block*/, 42);
    auto const res = boot.compute_sharpe_ci(daily, 252.0);

    EXPECT_LT(res.ci_lower, res.mean_sharpe);
    EXPECT_GT(res.ci_upper, res.mean_sharpe);
    EXPECT_LT(res.ci_lower, res.ci_upper);
    EXPECT_GT(res.std_sharpe, 0.0);
}

// T13: 确定性 (同 seed 同结果)
TEST(Bootstrap, Deterministic) {
    std::vector<double> daily;
    for (int i = 0; i < 30; ++i) {
        daily.push_back(0.01 + (i % 3 == 0 ? 0.002 : -0.001));
    }

    StationaryBootstrap boot1(500, 5, 99);
    StationaryBootstrap boot2(500, 5, 99);
    auto const r1 = boot1.compute_sharpe_ci(daily, 252.0);
    auto const r2 = boot2.compute_sharpe_ci(daily, 252.0);

    EXPECT_DOUBLE_EQ(r1.ci_lower, r2.ci_lower);
    EXPECT_DOUBLE_EQ(r1.ci_upper, r2.ci_upper);
    EXPECT_DOUBLE_EQ(r1.mean_sharpe, r2.mean_sharpe);
}

// T14: 序列太短 (< 2) → 空 result
TEST(Bootstrap, TooShort) {
    std::vector<double> one = {0.05};
    StationaryBootstrap boot(100, 5, 42);
    auto const res = boot.compute_sharpe_ci(one);
    EXPECT_DOUBLE_EQ(res.ci_lower, 0.0);
    EXPECT_DOUBLE_EQ(res.ci_upper, 0.0);
}

}  // namespace
}  // namespace stcpp::backtest
