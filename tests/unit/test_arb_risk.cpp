// test_arb_risk.cpp — 套利风控单测: 连续型 Kelly sizing + RJ-ARB 门 + open-leg 强平台账。
#include <gtest/gtest.h>

#include "stcpp/risk/arb_risk.hpp"
#include "stcpp/risk/arb_sizing.hpp"

using namespace stcpp::risk;

namespace {
constexpr std::int64_t S = 1'000'000'000LL;
}

// ---- 连续型 Kelly sizing ----
TEST(ArbSizing, ContinuousKellyAndCaps) {
    ArbSizingInput in;
    in.edge_arb = 0.01;       // 1¢ 净边际
    in.sigma_dmid = 0.02;     // σ=2¢ → var=0.0004
    in.bankroll_usdc = 10000;
    in.lambda = 0.10;
    in.max_notional_usdc = 500;
    in.exit_depth_usdc = 1000;
    const auto o = ComputeArbSizing(in);
    ASSERT_TRUE(o.valid);
    // f* = 0.10 · 0.01/0.0004 = 0.10·25 = 2.5 → clamp 1.0; notional = 1.0·10000=10000 → cap 500
    EXPECT_DOUBLE_EQ(o.f_star, 1.0) << "f* clamp [0,1]";
    EXPECT_DOUBLE_EQ(o.notional_usdc, 500.0) << "per-order cap 封顶";
}

TEST(ArbSizing, ExitDepthCaps) {
    ArbSizingInput in;
    in.edge_arb = 0.002;
    in.sigma_dmid = 0.05;  // var=0.0025; f*=0.10·0.002/0.0025=0.08
    in.bankroll_usdc = 10000;
    in.max_notional_usdc = 5000;
    in.exit_depth_usdc = 300;  // 出不来约束
    const auto o = ComputeArbSizing(in);
    ASSERT_TRUE(o.valid);
    EXPECT_NEAR(o.f_star, 0.08, 1e-9);
    EXPECT_DOUBLE_EQ(o.notional_usdc, 300.0) << "退出深度封顶 (进得去出不来兜底)";
}

TEST(ArbSizing, NotProfitableNoTrade) {
    ArbSizingInput in;
    in.edge_arb = -0.001;  // 不够本
    in.sigma_dmid = 0.02;
    in.bankroll_usdc = 10000;
    EXPECT_FALSE(ComputeArbSizing(in).valid);
}

TEST(ArbSizing, NoSigmaNoTrade) {
    ArbSizingInput in;
    in.edge_arb = 0.01;
    in.sigma_dmid = 0.0;  // 无方差估计 → 禁裸点估计 sizing
    in.bankroll_usdc = 10000;
    EXPECT_FALSE(ComputeArbSizing(in).valid);
}

// ---- RJ-ARB 门 ----
TEST(ArbGate, PassWhenAllOk) {
    ArbGateInput in;
    in.pred_dmid = 0.05;
    in.ci_low = 0.02;
    in.ci_high = 0.08;  // 多头 ci_low>0
    in.exit_depth_usdc = 1000;
    in.target_notional_usdc = 500;
    in.horizon_ns = 30 * S;
    in.pred_gen_ts_ns = 0;
    in.now_ns = 1 * S;
    in.est_rtt_ns = 1 * S;  // age 1s + rtt 1s = 2s << 15s (horizon/2)
    EXPECT_EQ(CheckArbGate(in), ArbRejectCode::None);
}

TEST(ArbGate, StalePredictionDelayKillsWindow) {
    ArbGateInput in;
    in.pred_dmid = 0.05;
    in.ci_low = 0.02;
    in.ci_high = 0.08;
    in.exit_depth_usdc = 1000;
    in.target_notional_usdc = 500;
    in.horizon_ns = 3 * S;       // 3s 窗口
    in.pred_gen_ts_ns = 0;
    in.now_ns = 1 * S;           // 预测 1s 前生成
    in.est_rtt_ns = 1 * S;       // 1s RTT → 1+1=2s > 1.5s (horizon/2) → 延迟吃掉窗口
    EXPECT_EQ(CheckArbGate(in), ArbRejectCode::StalePrediction);
}

TEST(ArbGate, CiCrossesZeroRejected) {
    ArbGateInput in;
    in.pred_dmid = 0.05;
    in.ci_low = -0.01;  // 多头但 CI 下界 <0 → 方向不确定
    in.ci_high = 0.10;
    in.exit_depth_usdc = 1000;
    in.target_notional_usdc = 500;
    in.horizon_ns = 30 * S;
    in.est_rtt_ns = 1 * S;
    EXPECT_EQ(CheckArbGate(in), ArbRejectCode::PredCiCrossesZero);
}

TEST(ArbGate, ExitDepthInsufficient) {
    ArbGateInput in;
    in.pred_dmid = 0.05;
    in.ci_low = 0.02;
    in.ci_high = 0.08;
    in.exit_depth_usdc = 300;        // < target → 出不来
    in.target_notional_usdc = 500;
    in.horizon_ns = 30 * S;
    in.est_rtt_ns = 1 * S;
    EXPECT_EQ(CheckArbGate(in), ArbRejectCode::ExitDepthInsufficient);
}

TEST(ArbGate, RoundTripBudget) {
    ArbGateInput in;
    in.pred_dmid = 0.05;
    in.ci_low = 0.02;
    in.ci_high = 0.08;
    in.exit_depth_usdc = 1000;
    in.target_notional_usdc = 500;
    in.horizon_ns = 30 * S;
    in.est_rtt_ns = 1 * S;
    in.open_leg_count = 5;
    in.max_open_legs = 5;  // 已满
    EXPECT_EQ(CheckArbGate(in), ArbRejectCode::RoundTripBudgetExceeded);
}

// ---- open-leg 强平台账 (fail-safe) ----
TEST(OpenLegLedger, SweepExpiredForceClose) {
    OpenLegLedger led;
    led.Add(OpenArbLeg{"tokA", "condA", 0.5, 100, 0.55, 0, 10 * S});   // deadline 10s
    led.Add(OpenArbLeg{"tokB", "condB", 0.4, 50, 0.45, 0, 30 * S});    // deadline 30s
    EXPECT_EQ(led.open_count(), 2u);
    // now=15s: tokA 到期 (10s≤15s), tokB 未 (30s>15s)
    const auto expired = led.SweepExpired(15 * S);
    ASSERT_EQ(expired.size(), 1u);
    EXPECT_EQ(expired[0].token_id, "tokA") << "到 deadline 未平 → 强平名单";
    // 平掉 tokA
    EXPECT_TRUE(led.Close("tokA"));
    EXPECT_EQ(led.open_count(), 1u);
    EXPECT_TRUE(led.SweepExpired(15 * S).empty()) << "tokA 平后无到期腿";
}

TEST(OpenLegLedger, AllOpenForPredictionFailure) {
    OpenLegLedger led;
    led.Add(OpenArbLeg{"t1", "c1", 0.5, 10, 0.5, 0, 100 * S});
    led.Add(OpenArbLeg{"t2", "c2", 0.5, 10, 0.5, 0, 100 * S});
    // 预测源失效 → 全部强平 (不等 deadline; 裸方向暴露)
    EXPECT_EQ(led.AllOpen().size(), 2u);
}
