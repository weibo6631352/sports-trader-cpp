// test_arb_signal.cpp — 套利信号决策核单测 (选最优 horizon / CI下界过滤 / gate 拒 / stub 不发)。
#include <gtest/gtest.h>

#include "stcpp/risk/arb_signal.hpp"

using namespace stcpp::risk;
using stcpp::ml::ArbPrediction;

namespace {
constexpr std::int64_t S = 1'000'000'000LL;

ArbMarketState BaseMkt() {
    ArbMarketState m;
    m.mid = 0.50;
    m.best_ask = 0.51;
    m.best_bid = 0.49;       // spread 0.02
    m.exit_depth_usdc = 5000;
    m.fee_roundtrip = 0.015;  // 1.5¢ 往返
    m.slip_est = 0.003;
    m.bankroll_usdc = 100000;
    m.max_notional_usdc = 2000;
    m.lambda = 0.10;
    m.now_ns = 1 * S;
    m.pred_gen_ts_ns = 0;
    m.est_rtt_ns = 1 * S;
    m.max_open_legs = 10;
    return m;
}
}  // namespace

TEST(ArbSignal, StubPredictionNotActionable) {
    ArbPrediction pred;
    pred.ok = false;  // stub
    const auto sig = ComputeArbSignal(pred, BaseMkt());
    EXPECT_FALSE(sig.actionable) << "无预测 → 不发单";
}

TEST(ArbSignal, PicksProfitableHorizon) {
    ArbPrediction pred;
    pred.ok = true;
    // BE = fee 0.015 + slip 0.003 + spread 0.02 = 0.038. 需 ci_low > 0.038 才够本。
    // horizon 3 (10s): dmid=0.06, ci_low=0.05 (>0.038 够本), ci_high=0.07, conf=0.8
    pred.wall[3].dmid = 0.06;
    pred.wall[3].ci_low = 0.05;
    pred.wall[3].ci_high = 0.07;
    pred.wall[3].confidence = 0.8;
    // horizon 0 (2s): ci_low=0.02 (<0.038 不够本)
    pred.wall[0].dmid = 0.03;
    pred.wall[0].ci_low = 0.02;
    pred.wall[0].ci_high = 0.04;
    pred.wall[0].confidence = 0.9;
    const auto sig = ComputeArbSignal(pred, BaseMkt());
    ASSERT_TRUE(sig.actionable);
    EXPECT_EQ(sig.horizon_sec, 10) << "选够本的 10s horizon (2s 不够本被跳过)";
    EXPECT_NEAR(sig.net_edge, 0.05 - 0.038, 1e-9) << "保守净 edge = ci_low − BE";
    EXPECT_DOUBLE_EQ(sig.entry_px, 0.51) << "多头进场 = best_ask";
    EXPECT_GT(sig.suggested_notional, 0.0);
    EXPECT_GT(sig.signal_quality, 0.0);
}

TEST(ArbSignal, CiCrossesZeroNotActionable) {
    ArbPrediction pred;
    pred.ok = true;
    pred.wall[3].dmid = 0.06;
    pred.wall[3].ci_low = -0.01;  // CI 下界 <0 → cons_move 负 → 不够本 (且 gate 也会拒)
    pred.wall[3].ci_high = 0.13;
    pred.wall[3].confidence = 0.8;
    const auto sig = ComputeArbSignal(pred, BaseMkt());
    EXPECT_FALSE(sig.actionable) << "CI 跨 0 → 方向不确定不发";
}

TEST(ArbSignal, StaleKillsActionable) {
    ArbPrediction pred;
    pred.ok = true;
    // 只给 2s horizon 一个够本信号, 但延迟吃掉窗口
    pred.wall[0].dmid = 0.10;
    pred.wall[0].ci_low = 0.08;
    pred.wall[0].ci_high = 0.12;
    pred.wall[0].confidence = 0.9;
    auto m = BaseMkt();
    m.now_ns = 1 * S;       // pred age 1s
    m.est_rtt_ns = 1 * S;   // +1s = 2s > 1s (2s horizon /2) → stale
    const auto sig = ComputeArbSignal(pred, m);
    EXPECT_FALSE(sig.actionable);
    EXPECT_EQ(sig.reject, ArbRejectCode::StalePrediction);
}

TEST(ArbSignal, ExitDepthInsufficientNotActionable) {
    ArbPrediction pred;
    pred.ok = true;
    pred.wall[6].dmid = 0.10;  // 30s horizon (延迟充裕)
    pred.wall[6].ci_low = 0.08;
    pred.wall[6].ci_high = 0.12;
    pred.wall[6].confidence = 0.8;
    auto m = BaseMkt();
    m.exit_depth_usdc = 10;  // 极薄 → 出不来
    const auto sig = ComputeArbSignal(pred, m);
    EXPECT_FALSE(sig.actionable);
    EXPECT_EQ(sig.reject, ArbRejectCode::ExitDepthInsufficient);
}
