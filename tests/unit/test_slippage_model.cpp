// tests/unit/test_slippage_model.cpp — 小肖 W3 Wave 18 7 case
// 关联: docs/RESEARCH/xiaoxiao-slippage-model-lib-v1.md §2
//
// 数值断言: 用 spec §3.1 Linear 公式算的真值 (而非 spec doc 中文表近似), 容差 1e-9.
// case 6 / case 7 数值与 spec §2 表一致.

#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include "stcpp/numerical/slippage_model.hpp"

namespace {

using stcpp::numerical::Confidence;
using stcpp::numerical::InvalidIntentSubReason;
using stcpp::numerical::RejectCode;
using stcpp::numerical::SlippageInput;
using stcpp::numerical::SlippageModel;
using stcpp::numerical::SlippageOutput;

// baseline: wall_now_ns = 1.7e18 ns (Polymarket Q4 2024 wall_now 参考)
constexpr std::int64_t WALL_NOW = 1'700'000'000'000'000'000LL;
constexpr std::int64_t NS_PER_MS = 1'000'000LL;

constexpr SlippageInput make_input(double size, double q, double l1, std::int64_t dt_ms, double tick = 0.01) {
    return SlippageInput{size, q, l1, WALL_NOW - dt_ms * NS_PER_MS, WALL_NOW, tick};
}

// Case 1: $2K gameday 0¢ (小袁实测中位)
TEST(SlippageModel, Case1_TwoK_Gameday_Zero) {
    auto const out = SlippageModel::compute(make_input(2000.0, 0.50, 2500.0, 200));
    ASSERT_EQ(out.reject, RejectCode::Ok);
    // ρ = 0.8 ≤ 1: pf = 0.50 + 0.01·0.8·0.5 = 0.504
    EXPECT_NEAR(out.expected_fill_price, 0.504, 1e-9);
    // s_stale = 1 - exp(-0.2/30) ≈ 0.006645; π_w = 1 - exp(-0.24) ≈ 0.213381
    // fill_rate = 1 - 0.213381 - 0.006645 ≈ 0.77997
    EXPECT_GT(out.expected_fill_rate, 0.77);
    EXPECT_LT(out.expected_fill_rate, 0.79);
    EXPECT_EQ(out.confidence, Confidence::High);  // dt=200ms ≤ 1s, ρ=0.8 ≤ 1
    // slippage_bps = (0.504 - 0.50)/0.50 · 10000 = 80
    EXPECT_EQ(out.slippage_bps, 80);
}

// Case 2: $10K gameday 0.4-0.9¢
TEST(SlippageModel, Case2_TenK_Gameday) {
    auto const out = SlippageModel::compute(make_input(10000.0, 0.55, 8000.0, 500));
    ASSERT_EQ(out.reject, RejectCode::Ok);
    // ρ = 1.25 > 1: pf = 0.55 + 0.01·(0.5 + 0.25·1.0) = 0.5575
    EXPECT_NEAR(out.expected_fill_price, 0.5575, 1e-9);
    // slippage = 0.0075/0.55·10000 ≈ 136.4 bps
    EXPECT_GE(out.slippage_bps, 130);
    EXPECT_LE(out.slippage_bps, 140);
    // s_stale=1-exp(-1/60)≈0.01653; π_w=1-exp(-0.375)≈0.31271
    // fill_rate = (1/1.25)·(1-0.31271)·(1-0.01653) ≈ 0.5410
    EXPECT_GT(out.expected_fill_rate, 0.53);
    EXPECT_LT(out.expected_fill_rate, 0.55);
    EXPECT_EQ(out.confidence, Confidence::Medium);  // 500ms ≤ 5s, ρ=1.25 ≤ 2
}

// Case 3: ρ → ρ_max 边界 + EXCEED_BOOK_DEPTH
// 注: ρ ∈ (2, 3] 时 π_w 已让 fill_rate 跌破 0.50 (multi-level 公式必然),
//     所以 ρ=2.999 不会以 Ok 通过, 而是 FillRateBelowFloor.
//     EXCEED_BOOK_DEPTH 闸优先于 FillRateBelowFloor (代码顺序), ρ>3 直接 EXCEED.
TEST(SlippageModel, Case3_RhoMax_Boundary) {
    // ρ = 16000/5000 = 3.2 > 3 → EXCEED_BOOK_DEPTH (优先)
    auto const reject = SlippageModel::compute(make_input(16000.0, 0.55, 5000.0, 200));
    EXPECT_EQ(reject.reject, RejectCode::ExceedBookDepth);
    EXPECT_EQ(reject.sub_reason, InvalidIntentSubReason::None);

    // ρ = 3.001 > 3 → EXCEED_BOOK_DEPTH
    auto const just_over = SlippageModel::compute(make_input(3.001 * 5000.0, 0.55, 5000.0, 200));
    EXPECT_EQ(just_over.reject, RejectCode::ExceedBookDepth);

    // ρ = 3.0 inclusive: 不 EXCEED, 但 fill_rate 已塌 → FillRateBelowFloor (有别 EXCEED)
    auto const at_bound = SlippageModel::compute(make_input(3.0 * 5000.0, 0.55, 5000.0, 200));
    EXPECT_NE(at_bound.reject, RejectCode::ExceedBookDepth);
    EXPECT_EQ(at_bound.reject, RejectCode::FillRateBelowFloor);

    // ρ = 2.999 → 同样 FillRateBelowFloor (fuzz R-B 边界: 不跨 ExceedBookDepth)
    auto const near_bound = SlippageModel::compute(make_input(2.999 * 5000.0, 0.55, 5000.0, 200));
    EXPECT_NE(near_bound.reject, RejectCode::ExceedBookDepth);
}

// Case 4: p→0 数值稳定 (ULP < 4·ε·q)
TEST(SlippageModel, Case4_NumericalStability_PriceNearZero) {
    auto const out = SlippageModel::compute(make_input(500.0, 0.01, 5000.0, 100, 0.001));
    ASSERT_EQ(out.reject, RejectCode::Ok);
    // ρ = 0.1 ≤ 1: pf = 0.01 + 0.001·0.1·0.5 = 0.01005
    EXPECT_NEAR(out.expected_fill_price, 0.01005, 1e-12);
    // 无 NaN / Inf
    EXPECT_TRUE(std::isfinite(out.expected_fill_price));
    EXPECT_TRUE(std::isfinite(out.expected_fill_rate));
    EXPECT_FALSE(std::isnan(out.expected_fill_price));
    EXPECT_FALSE(std::isnan(out.expected_fill_rate));
    // ULP 验证: pf 与理论值差 < 4·ε·q (ε = DBL_EPSILON, q = 0.01)
    constexpr double k_eps = 2.220446049250313e-16;  // DBL_EPSILON
    EXPECT_LT(std::abs(out.expected_fill_price - 0.01005), 4.0 * k_eps * 0.01);
}

// Case 5: paper 0.58/0.55/0.585 quote-vs-fill 算账打脸
// spec §1.4 paper case: 用 quote 算 edge=3¢ 还想下, 用 fill 算 edge=-1¢ 应该不下.
// Linear 模型在 ρ≈3 时 fill_rate 必塌 → 我们提前 FillRateBelowFloor 拒, 比 Kelly EDGE_NEGATED 更早,
// 这正是 spec §4.1 拒单闸门 (slippage 比 Kelly 先跑) 想要的语义 — 比 doc 写"≥ 300 bps" 更严谨.
// audit slippage_bps 仍然记录, 给后续校准用.
TEST(SlippageModel, Case5_PaperCase_SlippageNegatesEdge) {
    // size=5000, q=0.55, L1=1670 → ρ ≈ 2.994 (multi-level, 接近 ρ_max)
    auto const out = SlippageModel::compute(make_input(5000.0, 0.55, 1670.0, 200));
    EXPECT_EQ(out.reject, RejectCode::FillRateBelowFloor);
    // pf = 0.55 + 0.01·(0.5 + 1.994·1.0) = 0.57494; slippage_bps ≈ 453
    EXPECT_GE(out.slippage_bps, 300);  // spec §2: ≥ 3 ticks worth, audit 字段填出来
    EXPECT_GE(out.slippage_bps, 450);
    EXPECT_LE(out.slippage_bps, 460);
    // pf 字段也填 (审计 + Kelly 上游 EDGE_NEGATED 可二次确认)
    EXPECT_NEAR(out.expected_fill_price, 0.57494, 1e-5);
}

// Case 6: fill_rate < FILL_RATE_FLOOR (0.50) → REJECT
TEST(SlippageModel, Case6_FillRateBelowFloor) {
    // size=10000, q=0.55, L1=4000 (ρ=2.5), Δt=3000ms
    // s_stale ≈ 0.0952; π_w = 1-exp(-0.75) ≈ 0.5276
    // fill_rate = (1/2.5)·(1-0.5276)·(1-0.0952) ≈ 0.171 < 0.50
    auto const out = SlippageModel::compute(make_input(10000.0, 0.55, 4000.0, 3000));
    EXPECT_EQ(out.reject, RejectCode::FillRateBelowFloor);
    EXPECT_LT(out.expected_fill_rate, 0.50);
    EXPECT_GT(out.expected_fill_rate, 0.15);
    EXPECT_LT(out.expected_fill_rate, 0.20);
}

// Case 7: INVALID_INTENT 5 子测试
TEST(SlippageModel, Case7a_InvalidIntent_BookTsZero) {
    SlippageInput in = make_input(2000.0, 0.50, 2500.0, 200);
    in.book_snapshot_ts_ns = 0;
    auto const out = SlippageModel::compute(in);
    EXPECT_EQ(out.reject, RejectCode::InvalidIntent);
    EXPECT_EQ(out.sub_reason, InvalidIntentSubReason::BookTsZero);
}

TEST(SlippageModel, Case7b_InvalidIntent_BookTsStale) {
    // Δt = 90s > 60s
    auto const out = SlippageModel::compute(make_input(2000.0, 0.50, 2500.0, 90'000));
    EXPECT_EQ(out.reject, RejectCode::InvalidIntent);
    EXPECT_EQ(out.sub_reason, InvalidIntentSubReason::BookTsStale);
}

TEST(SlippageModel, Case7c_InvalidIntent_NaN) {
    SlippageInput in = make_input(2000.0, 0.50, 2500.0, 200);
    in.quote_price = std::nan("");
    auto const out = SlippageModel::compute(in);
    EXPECT_EQ(out.reject, RejectCode::InvalidIntent);
    EXPECT_EQ(out.sub_reason, InvalidIntentSubReason::NanOrInf);
}

TEST(SlippageModel, Case7d_InvalidIntent_Negative) {
    SlippageInput in = make_input(2000.0, 0.50, 2500.0, 200);
    in.book_depth_l1_usdc = -1.0;
    auto const out = SlippageModel::compute(in);
    EXPECT_EQ(out.reject, RejectCode::InvalidIntent);
    EXPECT_EQ(out.sub_reason, InvalidIntentSubReason::Negative);
}

TEST(SlippageModel, Case7e_InvalidIntent_IllegalTick) {
    // tick = 0.005 ∉ {0.001, 0.01}
    auto const out = SlippageModel::compute(make_input(2000.0, 0.50, 2500.0, 200, 0.005));
    EXPECT_EQ(out.reject, RejectCode::InvalidIntent);
    EXPECT_EQ(out.sub_reason, InvalidIntentSubReason::IllegalTick);
}

}  // namespace
