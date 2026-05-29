// tests/unit/test_backtest_types.cpp — Backtest types + R-20 PIT 单测
//
// Owner: 小蒋 (quant-backtest)
// Wave: backtest framework v0.2
// Last review: 2026-05-29
//
// 覆盖:
//   T01: TradeBucket 名称字符串
//   T02: trade_ts_ok — 合法 4 ts 链
//   T03: trade_ts_ok — event_ts=0 拒绝
//   T04: trade_ts_ok — 时序倒序 (data_source < event) 拒绝
//   T05: compute_net_edge — near-even market 公式校验
//   T06: compute_realized_pnl — BuyYes + YesWins
//   T07: compute_realized_pnl — BuyYes + NoWins
//   T08: compute_realized_pnl — SellYes + NoWins
//   T09: is_winning_trade — 四种组合
//   T10: ParamSet 默认值

#include <gtest/gtest.h>

#include "stcpp/backtest/types.hpp"

namespace stcpp::backtest {
namespace {

// T01: TradeBucket 名称
TEST(BacktestTypes, TradeBucketName) {
    EXPECT_STREQ(TradeBucketName(TradeBucket::Pregame), "PREGAME");
    EXPECT_STREQ(TradeBucketName(TradeBucket::Inplay), "INPLAY");
    EXPECT_STREQ(TradeBucketName(TradeBucket::Unknown), "UNKNOWN");
}

// T02: 合法 4 ts 链
TEST(BacktestTypes, TsOkValid) {
    TradeRecord t;
    t.event_ts_ns = 1000;
    t.data_source_ts_ns = 1001;
    t.ingestion_ts_ns = 1002;
    t.as_of_ts_ns = 1003;
    EXPECT_TRUE(trade_ts_ok(t));
}

// T03: event_ts=0 拒绝
TEST(BacktestTypes, TsOkZeroEventTs) {
    TradeRecord t;
    t.event_ts_ns = 0;
    t.data_source_ts_ns = 1;
    t.ingestion_ts_ns = 2;
    t.as_of_ts_ns = 3;
    EXPECT_FALSE(trade_ts_ok(t));
}

// T04: data_source < event → 拒绝
TEST(BacktestTypes, TsOkOutOfOrder) {
    TradeRecord t;
    t.event_ts_ns = 1005;
    t.data_source_ts_ns = 1000;  // 倒序
    t.ingestion_ts_ns = 1010;
    t.as_of_ts_ns = 1020;
    EXPECT_FALSE(trade_ts_ok(t));
}

// T05: compute_net_edge — near-even market (fill_price ≈ 0.5)
// net_edge = (fair - fill) - (fee + slip) × fill
// = (0.53 - 0.50) - (0.03 + 0.003) × 0.50
// = 0.03 - 0.0165 = 0.0135
TEST(BacktestTypes, ComputeNetEdge) {
    double const fair = 0.53;
    double const fill = 0.50;
    double const fee = 0.03;
    double const slip = 0.003;
    double const expected = (fair - fill) - (fee + slip) * fill;
    double const actual = compute_net_edge(fair, fill, fee, slip);
    EXPECT_NEAR(actual, expected, 1e-10);
    EXPECT_GT(actual, 0.0);  // 正期望
}

// T06: BuyYes + YesWins → 正收益
TEST(BacktestTypes, RealizePnlBuyYesWin) {
    double const fill = 0.40;
    double const size = 1000.0;
    double const fee = 0.03;
    double const slip = 0.003;
    // win: (1 - 0.40) * 1000 - (0.03 + 0.003) * 0.40 * 1000
    //    = 600 - 13.2 = 586.8
    double const pnl = compute_realized_pnl(TradeSide::BuyYes, SettleOutcome::YesWins, fill, size, fee, slip);
    EXPECT_NEAR(pnl, 586.8, 1e-6);
    EXPECT_GT(pnl, 0.0);
}

// T07: BuyYes + NoWins → 负收益
TEST(BacktestTypes, RealizePnlBuyYesLose) {
    double const fill = 0.40;
    double const size = 1000.0;
    double const fee = 0.03;
    double const slip = 0.003;
    // lose: -0.40 * 1000 - (0.03 + 0.003) * 0.40 * 1000
    //     = -400 - 13.2 = -413.2
    double const pnl = compute_realized_pnl(TradeSide::BuyYes, SettleOutcome::NoWins, fill, size, fee, slip);
    EXPECT_NEAR(pnl, -413.2, 1e-6);
    EXPECT_LT(pnl, 0.0);
}

// T08: SellYes + NoWins → 正收益
TEST(BacktestTypes, RealizePnlSellYesWin) {
    double const fill = 0.60;
    double const size = 1000.0;
    double const fee = 0.03;
    double const slip = 0.003;
    // SellYes wins when NoWins
    // win: (1 - 0.60) * 1000 - (0.03 + 0.003) * 0.60 * 1000
    //    = 400 - 19.8 = 380.2
    double const pnl = compute_realized_pnl(TradeSide::SellYes, SettleOutcome::NoWins, fill, size, fee, slip);
    EXPECT_NEAR(pnl, 380.2, 1e-6);
    EXPECT_GT(pnl, 0.0);
}

// T09: is_winning_trade 四种组合
TEST(BacktestTypes, IsWinningTrade) {
    EXPECT_TRUE(is_winning_trade(TradeSide::BuyYes, SettleOutcome::YesWins));
    EXPECT_FALSE(is_winning_trade(TradeSide::BuyYes, SettleOutcome::NoWins));
    EXPECT_TRUE(is_winning_trade(TradeSide::SellYes, SettleOutcome::NoWins));
    EXPECT_FALSE(is_winning_trade(TradeSide::SellYes, SettleOutcome::YesWins));
}

// T10: Pending outcome → pnl=0
TEST(BacktestTypes, PendingOutcomePnlZero) {
    double const pnl =
        compute_realized_pnl(TradeSide::BuyYes, SettleOutcome::Pending, 0.5, 1000.0, 0.03, 0.003);
    EXPECT_DOUBLE_EQ(pnl, 0.0);
}

}  // namespace
}  // namespace stcpp::backtest
