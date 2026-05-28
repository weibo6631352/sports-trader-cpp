// tests/unit/test_gate_evaluator.cpp — M4.5 7 hard gate evaluator 单测
//
// Owner: 小董 (stats-inference-advisor, W4 Wave 20)
// 落: include/stcpp/stats/gate_evaluator.hpp + docs/RESEARCH/xiaodong-m45-gate-framework-v1.md
//
// 14 核心 case (7 gate × pass/fail) + boundary + NaN/Inf + bootstrap determinism + PIT
//
// 红线 R-20 PIT 前置: 默认 fixture 用 helper MakeValidPit() 生成合规 4 ts.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

#include "stcpp/stats/gate_evaluator.hpp"

namespace {

using namespace stcpp::stats;

// 合规 4 ts: 1d ago → now-1s → now-500us → now
constexpr std::int64_t kNs = 1'000'000'000LL;
constexpr std::int64_t kNowNs        = 1'700'000'000LL * kNs;        // 任取一个未来值, 但 < real now() — wait, 不能用 NowRealtimeNs (会比真实小).
// 注: CheckPit 不调用 NowRealtimeNs, 只查链式不等式 + as_of>0. 真 R-20 含 "as_of <= now()" 已下沉到 pit.hpp.
// 本 evaluator 的 CheckPit 不查未来, 留给上游 PaperAudit 调 pit::AssertChain.

GateMetrics MakeValidPit() {
    GateMetrics m{};
    m.window_start_ts_ns        = kNowNs - 14 * 86400LL * kNs;
    m.window_end_ts_ns          = kNowNs - 1LL * kNs;
    m.ingestion_completed_ts_ns = kNowNs - 500'000LL;     // -500us
    m.as_of_ts_ns               = kNowNs;
    m.total_window_seconds      = 14.0 * 86400.0;
    m.uptime_seconds            = 14.0 * 86400.0;  // 100% 默认, 失败用例覆 G4 时改
    m.sharpe_annualizer         = 1.0;             // 默认不年化
    m.bootstrap_seed            = 42;
    return m;
}

// 生成正期望 PnL 序列 (paper 解锁 happy path)
std::vector<double> PositivePnL(std::size_t n, double mean, double sd, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> nd(mean, sd);
    std::vector<double> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) out.push_back(nd(rng));
    return out;
}

// equity curve from per-trade pnl, initial=1000
std::vector<double> EquityFromPnL(const std::vector<double>& pnl, double init = 1000.0) {
    std::vector<double> eq;
    eq.reserve(pnl.size());
    double cur = init;
    for (double p : pnl) {
        cur += p;
        eq.push_back(cur);
    }
    return eq;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// G1 PnL t-test
// ---------------------------------------------------------------------------

TEST(GateEvaluator_G1, PassWhenMeanPositiveAndSignificant) {
    auto m = MakeValidPit();
    // n=100, mean=2.0, sd=5.0 → t ~= 2.0/(5/10) = 4.0, p << 0.05
    m.per_trade_pnl_usdc = PositivePnL(100, 2.0, 5.0, /*seed*/1);
    m.per_trade_return   = m.per_trade_pnl_usdc;  // 复用
    m.equity_curve_usdc  = EquityFromPnL(m.per_trade_pnl_usdc);

    const auto r = GateEvaluator::EvalG1_PnLTTest(m);
    EXPECT_EQ(r.id, GateId::G1_PnLTTest);
    EXPECT_EQ(r.error, EvalError::Ok);
    EXPECT_LT(r.p_value, 0.05);
    EXPECT_TRUE(r.pass);
}

TEST(GateEvaluator_G1, FailWhenMeanZero) {
    auto m = MakeValidPit();
    // 中心化在 0, p 不显著
    m.per_trade_pnl_usdc = PositivePnL(100, 0.0, 5.0, /*seed*/2);
    const auto r = GateEvaluator::EvalG1_PnLTTest(m);
    EXPECT_EQ(r.error, EvalError::Ok);
    EXPECT_GT(r.p_value, 0.05);
    EXPECT_FALSE(r.pass);
}

TEST(GateEvaluator_G1, FailWhenMeanNegativeEvenIfSignificant) {
    auto m = MakeValidPit();
    // 显著但 mean 负, paper 显著亏, 必须 fail
    m.per_trade_pnl_usdc = PositivePnL(100, -2.0, 5.0, /*seed*/3);
    const auto r = GateEvaluator::EvalG1_PnLTTest(m);
    EXPECT_LT(r.p_value, 0.05);
    EXPECT_FALSE(r.pass);  // direction guard
}

TEST(GateEvaluator_G1, NaNInputReturnsInvalidInput) {
    auto m = MakeValidPit();
    m.per_trade_pnl_usdc = {1.0, 2.0, std::numeric_limits<double>::quiet_NaN(), 3.0};
    const auto r = GateEvaluator::EvalG1_PnLTTest(m);
    EXPECT_EQ(r.error, EvalError::InvalidInput);
    EXPECT_FALSE(r.pass);
}

// ---------------------------------------------------------------------------
// G2 Sharpe bootstrap CI
// ---------------------------------------------------------------------------

TEST(GateEvaluator_G2, PassWhenStrongSharpe) {
    auto m = MakeValidPit();
    // 强信号: mean=2, sd=1 → Sharpe ~ 2.0 (no annualizer). CI lower 应远 > 0.5.
    m.per_trade_return  = PositivePnL(200, 2.0, 1.0, /*seed*/10);
    m.sharpe_annualizer = 1.0;
    m.bootstrap_seed    = 42;
    const auto r = GateEvaluator::EvalG2_SharpeBootstrap(m);
    EXPECT_EQ(r.error, EvalError::Ok);
    EXPECT_GT(r.ci_lower, 0.5);
    EXPECT_TRUE(r.pass);
}

TEST(GateEvaluator_G2, FailWhenWeakSharpe) {
    auto m = MakeValidPit();
    // 弱信号: mean=0.05, sd=1 → Sharpe ~ 0.05, CI 必然横跨 0
    m.per_trade_return  = PositivePnL(200, 0.05, 1.0, /*seed*/11);
    m.sharpe_annualizer = 1.0;
    const auto r = GateEvaluator::EvalG2_SharpeBootstrap(m);
    EXPECT_EQ(r.error, EvalError::Ok);
    EXPECT_LT(r.ci_lower, 0.5);
    EXPECT_FALSE(r.pass);
}

TEST(GateEvaluator_G2, BootstrapDeterministicGivenSeed) {
    auto m = MakeValidPit();
    m.per_trade_return  = PositivePnL(50, 1.0, 2.0, /*seed*/12);
    m.bootstrap_seed    = 123;

    const auto r1 = GateEvaluator::EvalG2_SharpeBootstrap(m);
    const auto r2 = GateEvaluator::EvalG2_SharpeBootstrap(m);
    EXPECT_DOUBLE_EQ(r1.ci_lower, r2.ci_lower);
    EXPECT_DOUBLE_EQ(r1.ci_upper, r2.ci_upper);
}

TEST(GateEvaluator_G2, NaNInputReturnsInvalidInput) {
    auto m = MakeValidPit();
    m.per_trade_return = {1.0, std::numeric_limits<double>::infinity(), 2.0};
    const auto r = GateEvaluator::EvalG2_SharpeBootstrap(m);
    EXPECT_EQ(r.error, EvalError::InvalidInput);
    EXPECT_FALSE(r.pass);
}

// ---------------------------------------------------------------------------
// G3 RM failure count
// ---------------------------------------------------------------------------

TEST(GateEvaluator_G3, PassWhenZero) {
    auto m = MakeValidPit();
    m.rm_failure_count = 0;
    const auto r = GateEvaluator::EvalG3_RMZeroFail(m);
    EXPECT_EQ(r.error, EvalError::Ok);
    EXPECT_TRUE(r.pass);
    EXPECT_DOUBLE_EQ(r.actual, 0.0);
}

TEST(GateEvaluator_G3, FailWhenAnyPositive) {
    auto m = MakeValidPit();
    m.rm_failure_count = 1;
    const auto r = GateEvaluator::EvalG3_RMZeroFail(m);
    EXPECT_FALSE(r.pass);
    EXPECT_DOUBLE_EQ(r.actual, 1.0);
}

TEST(GateEvaluator_G3, NegativeIsInvalidInput) {
    auto m = MakeValidPit();
    m.rm_failure_count = -5;
    const auto r = GateEvaluator::EvalG3_RMZeroFail(m);
    EXPECT_EQ(r.error, EvalError::InvalidInput);
}

// ---------------------------------------------------------------------------
// G4 Uptime
// ---------------------------------------------------------------------------

TEST(GateEvaluator_G4, PassAtOrAboveThreshold) {
    auto m = MakeValidPit();
    m.total_window_seconds = 1'000'000.0;
    m.uptime_seconds       = 995'000.0;  // 99.5% exact
    const auto r = GateEvaluator::EvalG4_Uptime(m);
    EXPECT_EQ(r.error, EvalError::Ok);
    EXPECT_TRUE(r.pass);
    EXPECT_NEAR(r.actual, 0.995, 1e-9);
}

TEST(GateEvaluator_G4, FailJustBelow) {
    auto m = MakeValidPit();
    m.total_window_seconds = 1'000'000.0;
    m.uptime_seconds       = 994'999.0;  // 99.4999%
    const auto r = GateEvaluator::EvalG4_Uptime(m);
    EXPECT_FALSE(r.pass);
}

TEST(GateEvaluator_G4, InvalidInputWhenUptimeExceedsTotal) {
    auto m = MakeValidPit();
    m.total_window_seconds = 100.0;
    m.uptime_seconds       = 150.0;  // >total = 数据错
    const auto r = GateEvaluator::EvalG4_Uptime(m);
    EXPECT_EQ(r.error, EvalError::InvalidInput);
}

// ---------------------------------------------------------------------------
// G5 Max DD
// ---------------------------------------------------------------------------

TEST(GateEvaluator_G5, PassWhenSmallDD) {
    auto m = MakeValidPit();
    // 1000 → 1100 → 1050 → 1200 → max DD = (1100-1050)/1100 ≈ 4.5%
    m.equity_curve_usdc = {1000.0, 1100.0, 1050.0, 1200.0};
    const auto r = GateEvaluator::EvalG5_MaxDD(m);
    EXPECT_EQ(r.error, EvalError::Ok);
    EXPECT_TRUE(r.pass);
    EXPECT_NEAR(r.actual, (1100.0 - 1050.0) / 1100.0, 1e-9);
}

TEST(GateEvaluator_G5, FailWhenBigDD) {
    auto m = MakeValidPit();
    // 1000 → 1100 → 1000 → DD = (1100-1000)/1100 ≈ 9.1% > 8%
    m.equity_curve_usdc = {1000.0, 1100.0, 1000.0};
    const auto r = GateEvaluator::EvalG5_MaxDD(m);
    EXPECT_FALSE(r.pass);
    EXPECT_GT(r.actual, 0.08);
}

TEST(GateEvaluator_G5, BoundaryEightPercentExact) {
    auto m = MakeValidPit();
    // peak=1000, trough=920 → DD = 8.0% exact (boundary inclusive)
    m.equity_curve_usdc = {1000.0, 920.0};
    const auto r = GateEvaluator::EvalG5_MaxDD(m);
    EXPECT_EQ(r.error, EvalError::Ok);
    EXPECT_TRUE(r.pass);  // 8.0% 包含
    EXPECT_NEAR(r.actual, 0.08, 1e-12);
}

TEST(GateEvaluator_G5, BoundaryJustOverEightPercent) {
    auto m = MakeValidPit();
    // peak=1000, trough=919.9 → 8.01%, 失败
    m.equity_curve_usdc = {1000.0, 919.9};
    const auto r = GateEvaluator::EvalG5_MaxDD(m);
    EXPECT_FALSE(r.pass);
    EXPECT_GT(r.actual, 0.08);
}

TEST(GateEvaluator_G5, NaNInputReturnsInvalidInput) {
    auto m = MakeValidPit();
    m.equity_curve_usdc = {1000.0, std::numeric_limits<double>::quiet_NaN()};
    const auto r = GateEvaluator::EvalG5_MaxDD(m);
    EXPECT_EQ(r.error, EvalError::InvalidInput);
}

// ---------------------------------------------------------------------------
// G6 Min trades
// ---------------------------------------------------------------------------

TEST(GateEvaluator_G6, PassAtFifty) {
    auto m = MakeValidPit();
    m.per_trade_pnl_usdc = std::vector<double>(50, 1.0);
    const auto r = GateEvaluator::EvalG6_MinTrades(m);
    EXPECT_TRUE(r.pass);
    EXPECT_DOUBLE_EQ(r.actual, 50.0);
}

TEST(GateEvaluator_G6, FailAtFortyNine) {
    auto m = MakeValidPit();
    m.per_trade_pnl_usdc = std::vector<double>(49, 1.0);
    const auto r = GateEvaluator::EvalG6_MinTrades(m);
    EXPECT_FALSE(r.pass);
    EXPECT_DOUBLE_EQ(r.actual, 49.0);
}

// ---------------------------------------------------------------------------
// G7 Above random baseline
// ---------------------------------------------------------------------------

TEST(GateEvaluator_G7, PassWhenPaperBeatsRandom) {
    auto m = MakeValidPit();
    m.per_trade_pnl_usdc          = PositivePnL(100, 2.0, 3.0, /*seed*/20);
    m.random_baseline_pnl_usdc    = PositivePnL(100, 0.0, 3.0, /*seed*/21);
    const auto r = GateEvaluator::EvalG7_AboveRandom(m);
    EXPECT_EQ(r.error, EvalError::Ok);
    EXPECT_LT(r.p_value, 0.05);
    EXPECT_TRUE(r.pass);
}

TEST(GateEvaluator_G7, FailWhenPaperEqualsRandom) {
    auto m = MakeValidPit();
    m.per_trade_pnl_usdc          = PositivePnL(100, 0.5, 3.0, /*seed*/22);
    m.random_baseline_pnl_usdc    = PositivePnL(100, 0.5, 3.0, /*seed*/23);
    const auto r = GateEvaluator::EvalG7_AboveRandom(m);
    EXPECT_EQ(r.error, EvalError::Ok);
    EXPECT_GT(r.p_value, 0.05);
    EXPECT_FALSE(r.pass);
}

TEST(GateEvaluator_G7, FailWhenPaperBelowRandom) {
    auto m = MakeValidPit();
    m.per_trade_pnl_usdc          = PositivePnL(100, -1.0, 3.0, /*seed*/24);
    m.random_baseline_pnl_usdc    = PositivePnL(100, 2.0, 3.0, /*seed*/25);
    const auto r = GateEvaluator::EvalG7_AboveRandom(m);
    EXPECT_FALSE(r.pass);
}

// ---------------------------------------------------------------------------
// R-20 PIT 前置
// ---------------------------------------------------------------------------

TEST(GateEvaluator_PIT, PitViolationFailsAllGates) {
    auto m = MakeValidPit();
    // 故意把 ingestion 拖到 window_end 之前 → 链断
    m.ingestion_completed_ts_ns = m.window_end_ts_ns - 1;
    m.per_trade_pnl_usdc        = PositivePnL(100, 2.0, 5.0, 30);
    m.per_trade_return          = m.per_trade_pnl_usdc;
    m.equity_curve_usdc         = EquityFromPnL(m.per_trade_pnl_usdc);
    m.random_baseline_pnl_usdc  = PositivePnL(100, 0.0, 3.0, 31);

    const auto out = GateEvaluator::EvaluateAll(m);
    EXPECT_EQ(out.first_error, EvalError::PitViolation);
    EXPECT_FALSE(out.all_pass);
    EXPECT_EQ(out.pass_count, 0u);
    for (const auto& r : out.per_gate) {
        EXPECT_FALSE(r.pass);
        EXPECT_EQ(r.error, EvalError::PitViolation);
    }
}

TEST(GateEvaluator_PIT, EventTsZeroFailsPit) {
    auto m = MakeValidPit();
    m.window_start_ts_ns = 0;  // 红线: event_ts > 0
    EXPECT_FALSE(GateEvaluator::CheckPit(m));
}

// ---------------------------------------------------------------------------
// EvaluateAll happy path: 7/7 全过
// ---------------------------------------------------------------------------

TEST(GateEvaluator_Full, AllSevenGatesPassHappyPath) {
    auto m = MakeValidPit();
    // mean=2, sd=2 → Sharpe ~1.0, n=100 → CI lower ~0.8 > 0.5 (G2 pass)
    // mean=2, t-stat ~10 → G1 显著
    // baseline mean=0 sd=2 → G7 显著
    m.per_trade_pnl_usdc       = PositivePnL(100, 2.0, 2.0, /*seed*/40);
    m.per_trade_return         = m.per_trade_pnl_usdc;
    m.equity_curve_usdc        = EquityFromPnL(m.per_trade_pnl_usdc, /*init*/10000.0);
    m.random_baseline_pnl_usdc = PositivePnL(100, 0.0, 2.0, /*seed*/41);
    m.rm_failure_count         = 0;
    m.total_window_seconds     = 14.0 * 86400.0;
    m.uptime_seconds           = 14.0 * 86400.0;
    m.sharpe_annualizer        = 1.0;
    m.bootstrap_seed           = 42;

    const auto out = GateEvaluator::EvaluateAll(m);
    EXPECT_EQ(out.first_error, EvalError::Ok);
    EXPECT_EQ(out.pass_count, kNumGates);
    EXPECT_TRUE(out.all_pass);
}

TEST(GateEvaluator_Full, RMOneFailFlipsAllPassToFalse) {
    auto m = MakeValidPit();
    // 同 happy 强信号, 仅 RM 失效 1 次 → G3 fail, 其余 6 应仍 pass
    m.per_trade_pnl_usdc       = PositivePnL(100, 2.0, 2.0, /*seed*/50);
    m.per_trade_return         = m.per_trade_pnl_usdc;
    m.equity_curve_usdc        = EquityFromPnL(m.per_trade_pnl_usdc, 10000.0);
    m.random_baseline_pnl_usdc = PositivePnL(100, 0.0, 2.0, /*seed*/51);
    m.rm_failure_count         = 1;  // 单一 RM 失效
    m.total_window_seconds     = 14.0 * 86400.0;
    m.uptime_seconds           = 14.0 * 86400.0;

    const auto out = GateEvaluator::EvaluateAll(m);
    EXPECT_FALSE(out.all_pass);
    EXPECT_FALSE(out.per_gate[2].pass);  // G3 idx=2
}

// ---------------------------------------------------------------------------
// T_G2_StageThresholds — ADR-016 G2 4 stage 阈值边界测试
// 覆盖: M2 0.3 / M4_5 0.5 / M5_Q1 0.8 / NorthStar 1.5
// 原则: CI_lower 刚好 = threshold → pass (boundary inclusive),
//       CI_lower = threshold - epsilon → fail.
// 注: 本 case 直接测 kG2SharpeCILowerByStage 数组值, 不依赖 evaluator 跑 bootstrap
//     (bootstrap 已在 G2 系列 case 覆盖), 保持 boundary 测试简洁确定.
// ---------------------------------------------------------------------------

TEST(GateEvaluator_G2_StageThresholds, M2_Threshold_Is_0_3) {
    // ADR-016 §Stage-1: M2 alpha gate 下限 = 0.3
    constexpr double kExpected = 0.3;
    EXPECT_DOUBLE_EQ(kG2SharpeCILowerByStage[static_cast<std::size_t>(GateStage::M2)],
                     kExpected)
        << "M2 stage threshold must be 0.3 (ADR-016)";
}

TEST(GateEvaluator_G2_StageThresholds, M4_5_Threshold_Is_0_5) {
    // ADR-016 §Stage-2 + 小梁 W5 会签 SSOT: M4.5 paper 解锁 CI 下限 = 0.5
    // (retro 口头 0.3 已废弃, 以本值为准)
    constexpr double kExpected = 0.5;
    EXPECT_DOUBLE_EQ(kG2SharpeCILowerByStage[static_cast<std::size_t>(GateStage::M4_5)],
                     kExpected)
        << "M4.5 stage threshold must be 0.5 (ADR-016, 小梁会签 SSOT)";
    // 同时验证 kG2_SharpeCILow 引用 M4_5 slot
    EXPECT_DOUBLE_EQ(kG2_SharpeCILow, kExpected)
        << "kG2_SharpeCILow must equal M4_5 slot (0.5)";
}

TEST(GateEvaluator_G2_StageThresholds, M5_Q1_Threshold_Is_0_8) {
    // ADR-016 §Stage-3: live 第 1 季度稳态 CI 下限 = 0.8
    constexpr double kExpected = 0.8;
    EXPECT_DOUBLE_EQ(kG2SharpeCILowerByStage[static_cast<std::size_t>(GateStage::M5_Q1)],
                     kExpected)
        << "M5_Q1 stage threshold must be 0.8 (ADR-016)";
}

TEST(GateEvaluator_G2_StageThresholds, NorthStar_Threshold_Is_1_5) {
    // ADR-016 §Stage-4 + CLAUDE.md §2 北极星: T+36 月 Sharpe ≥ 1.5
    constexpr double kExpected = 1.5;
    EXPECT_DOUBLE_EQ(kG2SharpeCILowerByStage[static_cast<std::size_t>(GateStage::NorthStar)],
                     kExpected)
        << "NorthStar stage threshold must be 1.5 (ADR-016, CLAUDE.md §2)";
}

TEST(GateEvaluator_G2_StageThresholds, StageArrayMonotonicallyIncreasing) {
    // 4 stage 阈值必须严格单调递增 (alpha gate < paper 解锁 < live Q1 < 北极星)
    for (std::size_t i = 1; i < kG2SharpeCILowerByStage.size(); ++i) {
        EXPECT_GT(kG2SharpeCILowerByStage[i], kG2SharpeCILowerByStage[i - 1])
            << "kG2SharpeCILowerByStage must be strictly increasing (ADR-016)";
    }
}
