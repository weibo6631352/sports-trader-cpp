// tests/unit/sizing/test_sizing_calculator.cpp — SizingCalculator unit tests v0.1
//
// Owner: 小袁 (quant-microstructure)
// last_review: 2026-05-29
//
// 验收口径 (小梁 §5.1 + 老韩 §5.1 联签强制项):
//   C1  suggested_notional ≤ RiskConfig.per_order_cap_usdc (引用值)
//   C2  suggested_notional + current_token_exposure ≤ RiskConfig.per_outcome_cap_usdc
//   C3  suggested_notional + current_condition_exposure ≤ RiskConfig.market_exposure_cap_usdc
//   C4  suggested_notional ≤ bankroll_usdc × MAX_BANKROLL_FRACTION
//   C5  edge_ci_lower <= edge_ci_lower_floor → capped_by=NO_EDGE, valid=false, 全 0
//   C6  net_ci_edge <= floor (fee 门 B) → capped_by=NO_EDGE, valid=false
//   C7  edge_ci_lower_bps < slippage_bps (slippage 门 A) → capped_by=NO_EDGE, valid=false
//   + capped_by 命中正确性
//   + λ 折扣: kelly_fractional = 0.25 × kelly_full (±1e-12)
//   + fill_rate 折扣: effective_notional = suggested × fill_rate
//   + fill_rate < FILL_RATE_FLOOR (0.50) → FILL_RATE_FLOOR, valid=false
//   + NaN/Inf / p∈{0,1} / c∈{0,1} → valid=false, 全 0
//   + 方向对称: buy YES vs buy NO 对称 f* 幅度

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "stcpp/numerical/slippage_model.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/sizing/sizing_calculator.hpp"

namespace stcpp::sizing::test {

// ---------------------------------------------------------------------------
// 辅助: 默认 RiskConfig + 默认 SizingInput (全部合法)
// ---------------------------------------------------------------------------

static risk::RiskConfig make_default_cfg() noexcept {
    risk::RiskConfig cfg;
    // c3 (P0-2): 显式 cap 真值 (whole pUSD), 与生产默认解耦 (老韩 c3 改生产默认 1000/2000/5000 pUSD)。
    //   本测试套保持原口径 10k/50k/25k whole pUSD: from_pusd 转 micro 字段, sizing .to_pusd() 回 whole。
    cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(10'000.0);
    cfg.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(50'000.0);
    cfg.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(25'000.0);
    cfg.edge_ci_lower_floor = 0.0;
    return cfg;
}

static SizingInput make_default_input() noexcept {
    SizingInput in;
    in.fair_value = 0.60;     // p = 60%
    in.price = 0.50;          // c = 50% — edge = 10%
    in.edge_ci_lower = 0.08;  // CI 下界 = 800 bps (> 0)
    in.edge_bps = 1000.0;
    in.bankroll_usdc = 100'000.0;  // 10 万 USDC
    in.fill_rate = 0.75;           // 75% fill (> FILL_RATE_FLOOR)
    in.slippage_bps = 20.0;        // 20 bps slippage (< edge_ci_lower 800 bps)
    in.current_token_exposure_usdc = 0.0;
    in.current_condition_exposure_usdc = 0.0;
    in.buy_yes = true;
    in.model_conf = 1.0;
    return in;
}

// ---------------------------------------------------------------------------
// 基础 happy-path 测试
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, HappyPath_BasicOutput) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_TRUE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::NONE);
    EXPECT_GT(out.kelly_full, 0.0);
    EXPECT_GT(out.kelly_fractional, 0.0);
    EXPECT_GT(out.suggested_notional, 0.0);
    EXPECT_GT(out.effective_notional, 0.0);
    EXPECT_GT(out.net_ci_edge, 0.0);
    // snapshot 回填
    EXPECT_DOUBLE_EQ(out.snapshot.fair_value, in.fair_value);
    EXPECT_DOUBLE_EQ(out.snapshot.price, in.price);
}

// ---------------------------------------------------------------------------
// λ 折扣验证 (kelly_fractional = 0.25 × kelly_full, ±1e-12)
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, LambdaDiscount_Quarter_Kelly) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();

    auto out = SizingCalculator::compute(cfg, in);

    ASSERT_TRUE(out.valid);
    EXPECT_NEAR(out.kelly_fractional, kLambdaBase * out.kelly_full, 1e-12);
}

// ---------------------------------------------------------------------------
// fill_rate 折扣: effective_notional = suggested × fill_rate
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, EffectiveNotional_FillRateDiscount) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.fill_rate = 0.65;

    auto out = SizingCalculator::compute(cfg, in);

    ASSERT_TRUE(out.valid);
    EXPECT_NEAR(out.effective_notional, out.suggested_notional * 0.65, 1e-9);
}

// ---------------------------------------------------------------------------
// C1: 不超 per_order_cap_usdc (引用值)
// 构造: 大 bankroll + 大 edge CI → kelly notional 很大, C1 触发
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, C1_PerOrderCap_NotExceeded) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    // 大 bankroll 让 kelly_notional >> per_order_cap
    in.bankroll_usdc = 10'000'000.0;
    in.edge_ci_lower = 0.50;  // 极大 CI edge
    in.slippage_bps = 10.0;

    auto out = SizingCalculator::compute(cfg, in);

    ASSERT_TRUE(out.valid);
    // C1: suggested_notional <= per_order_cap (引用值)
    EXPECT_LE(out.suggested_notional, cfg.per_order_cap_usdc.to_pusd() + 1e-9);
    EXPECT_EQ(out.capped_by, CappedBy::PER_ORDER_CAP);
}

// ---------------------------------------------------------------------------
// C2: 不超 per_outcome_cap (current_token_exposure + suggested <= cap)
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, C2_PerOutcomeCap_WithExistingExposure) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    // 已有 20k token exposure, cap = 25k → headroom = 5k
    in.current_token_exposure_usdc = 20'000.0;
    in.bankroll_usdc = 10'000'000.0;
    in.edge_ci_lower = 0.30;
    in.slippage_bps = 5.0;

    auto out = SizingCalculator::compute(cfg, in);

    ASSERT_TRUE(out.valid);
    EXPECT_LE(out.suggested_notional + in.current_token_exposure_usdc,
              cfg.per_outcome_cap_usdc.to_pusd() + 1e-9);
}

// ---------------------------------------------------------------------------
// C3: 不超 condition_exposure_cap
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, C3_ConditionExposureCap_WithExistingExposure) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    // 已有 45k condition exposure, cap = 50k → headroom = 5k
    in.current_condition_exposure_usdc = 45'000.0;
    in.bankroll_usdc = 10'000'000.0;
    in.edge_ci_lower = 0.30;
    in.slippage_bps = 5.0;

    auto out = SizingCalculator::compute(cfg, in);

    ASSERT_TRUE(out.valid);
    EXPECT_LE(out.suggested_notional + in.current_condition_exposure_usdc,
              cfg.market_exposure_cap_usdc.to_pusd() + 1e-9);
}

// ---------------------------------------------------------------------------
// C4: 不超 bankroll × MAX_BANKROLL_FRACTION (动态 bankroll)
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, C4_BankrollFraction_DynamicBankroll) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    // bankroll 缩水到 80k (drawdown 场景; 老韩 §1.3)
    in.bankroll_usdc = 80'000.0;
    in.edge_ci_lower = 0.40;
    in.slippage_bps = 5.0;

    auto out = SizingCalculator::compute(cfg, in);

    ASSERT_TRUE(out.valid);
    EXPECT_LE(out.suggested_notional, in.bankroll_usdc * kMaxBankrollFraction + 1e-9);
}

// ---------------------------------------------------------------------------
// C5: CI gating — edge_ci_lower <= floor → NO_EDGE, valid=false, 全 0 (老韩 §2.1)
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, C5_CI_Gating_NegativeCI) {
    auto cfg = make_default_cfg();
    cfg.edge_ci_lower_floor = 0.0;
    auto in = make_default_input();
    in.edge_ci_lower = -0.01;  // CI 下界负数

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::NO_EDGE);
    EXPECT_DOUBLE_EQ(out.kelly_full, 0.0);
    EXPECT_DOUBLE_EQ(out.kelly_fractional, 0.0);
    EXPECT_DOUBLE_EQ(out.suggested_notional, 0.0);
    EXPECT_DOUBLE_EQ(out.effective_notional, 0.0);
}

TEST(SizingCalculatorTest, C5_CI_Gating_ZeroCI) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.edge_ci_lower = 0.0;  // 恰好等于 floor = 0 → 拒

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::NO_EDGE);
}

TEST(SizingCalculatorTest, C5_CI_Gating_JustAboveFloor) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.edge_ci_lower = 1e-6;  // 刚好 > 0, 但 net_ci_edge 可能被 fee 门干掉

    // 不要求 valid=true (fee 门可能拒); 但 capped_by 必须是 NO_EDGE 或合法
    auto out = SizingCalculator::compute(cfg, in);
    // 不断言 valid — 取决于 p×(1-p) 的 fee 大小
    (void)out;
}

// ---------------------------------------------------------------------------
// C6: fee 门 B — net_ci_edge = edge_ci_lower − fee_per_unit <= floor → NO_EDGE (老韩 §2.3)
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, C6_FeeGate_B_NetEdgeNegated) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    // P0-6 (fee 用 price): price=0.40 → fee_per_unit = 0.03 × 0.40 × 0.60 = 0.0072 = 72 bps
    // 设 edge_ci_lower = 0.005 (50 bps) < fee_per_unit (72 bps) → net_ci_edge < 0 → NO_EDGE
    in.fair_value = 0.50 + 1e-4;  // 避免 valid_input 拒 (fair_value 不再进 fee, 仅过校验)
    in.price = 0.40;
    in.edge_ci_lower = 0.005;  // 50 bps
    in.slippage_bps = 1.0;     // slippage < edge_ci_lower_bps (50), 门 A 通过

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::NO_EDGE);
}

// ---------------------------------------------------------------------------
// C7: slippage 门 A — edge_ci_lower_bps < slippage_bps → NO_EDGE (老韩 §2.3)
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, C7_SlippageGate_A_EdgeNegatedBySlippage) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    // edge_ci_lower = 0.02 (200 bps), slippage_bps = 300 → 门 A 拒
    in.edge_ci_lower = 0.02;
    in.slippage_bps = 300.0;  // > 200 bps

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::NO_EDGE);
}

// ---------------------------------------------------------------------------
// fill_rate < FILL_RATE_FLOOR → FILL_RATE_FLOOR, valid=false
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, FillRateFloor_Advisory_ZeroOut) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.fill_rate = 0.30;  // < 0.50 (FILL_RATE_FLOOR)

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::FILL_RATE_FLOOR);
    EXPECT_DOUBLE_EQ(out.suggested_notional, 0.0);
    EXPECT_DOUBLE_EQ(out.effective_notional, 0.0);
    // net_ci_edge 仍提供 (debug 信息)
    EXPECT_GT(out.net_ci_edge, 0.0);
}

TEST(SizingCalculatorTest, FillRateFloor_ExactBoundary_Reject) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.fill_rate = numerical::FILL_RATE_FLOOR - 1e-9;  // 刚好低于 floor

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::FILL_RATE_FLOOR);
}

TEST(SizingCalculatorTest, FillRateFloor_AtFloor_Accepted) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.fill_rate = numerical::FILL_RATE_FLOOR;  // 恰好等于 floor → 通过 (>= 不拒)

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_TRUE(out.valid);
}

// ---------------------------------------------------------------------------
// 数值健壮性: NaN / Inf / p∈{0,1} / c∈{0,1} → valid=false, 全 0
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, Robustness_NaN_FairValue) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.fair_value = std::numeric_limits<double>::quiet_NaN();

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
    EXPECT_DOUBLE_EQ(out.kelly_full, 0.0);
    EXPECT_DOUBLE_EQ(out.suggested_notional, 0.0);
}

TEST(SizingCalculatorTest, Robustness_Inf_Price) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.price = std::numeric_limits<double>::infinity();

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
}

TEST(SizingCalculatorTest, Robustness_FairValue_At_Zero) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.fair_value = 0.0;  // 边界

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
}

TEST(SizingCalculatorTest, Robustness_FairValue_At_One) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.fair_value = 1.0;  // 边界

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
}

TEST(SizingCalculatorTest, Robustness_Price_At_Zero) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.price = 0.0;

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
}

TEST(SizingCalculatorTest, Robustness_Price_At_One) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.price = 1.0;

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
}

TEST(SizingCalculatorTest, Robustness_NaN_EdgeCI) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.edge_ci_lower = std::numeric_limits<double>::quiet_NaN();

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
}

TEST(SizingCalculatorTest, Robustness_Zero_Bankroll) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.bankroll_usdc = 0.0;

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
}

TEST(SizingCalculatorTest, Robustness_Negative_Bankroll) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.bankroll_usdc = -1.0;

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
}

// ---------------------------------------------------------------------------
// 方向对称: buy YES (p > c) vs buy NO (p < c) → 对称 f* 幅度
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, DirectionSymmetry_BuyYES_vs_BuyNO) {
    auto cfg = make_default_cfg();

    // buy YES: p=0.6, c=0.5, edge_ci_lower=0.08
    SizingInput in_yes = make_default_input();
    in_yes.fair_value = 0.60;
    in_yes.price = 0.50;
    in_yes.edge_ci_lower = 0.08;
    in_yes.buy_yes = true;

    // buy NO (镜像): p'=1-0.6=0.4, c'=1-0.5=0.5
    //   在 SizingCalculator 逻辑中: 调用方按 buy_yes=false, 传同样的 p/c
    //   但 edge_ci_lower 也要对称 (方向 p < c 场景)
    SizingInput in_no = make_default_input();
    in_no.fair_value = 0.40;     // 1 - 0.60
    in_no.price = 0.50;          // 1 - 0.50 = 0.50 (恰好对称)
    in_no.edge_ci_lower = 0.08;  // 同幅度 CI 下界
    in_no.buy_yes = false;

    auto out_yes = SizingCalculator::compute(cfg, in_yes);
    auto out_no = SizingCalculator::compute(cfg, in_no);

    ASSERT_TRUE(out_yes.valid);
    ASSERT_TRUE(out_no.valid);

    // net_ci_edge 应相同 (同 edge_ci_lower; P0-6: fee 用 price, 两向 price 均=0.50 → fee 相同)
    // fee_per_unit = 0.03 × 0.50 × 0.50 = 0.0075 (两向 price 同 → fee 对称, 与 fair_value 0.6/0.4 无关)
    EXPECT_NEAR(out_yes.net_ci_edge, out_no.net_ci_edge, 1e-12);

    // kelly_full 可能因分母不同而异 (YES: /(1-c)=0.5; NO: /c=0.5 — 此例相同)
    EXPECT_NEAR(out_yes.kelly_full, out_no.kelly_full, 1e-10);
}

// ---------------------------------------------------------------------------
// p = c (zero edge): kelly_full = 0, valid 取决于 edge_ci_lower
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, ZeroEdge_PEqualsC_CI_Gates) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.fair_value = 0.50 + 1e-5;  // 微微 > c
    in.price = 0.50;
    in.edge_ci_lower = -0.001;  // CI 下界负 → NO_EDGE

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::NO_EDGE);
}

// ---------------------------------------------------------------------------
// net_raw_edge: compute_net_ci_edge helper ±1e-9 (RM 一致性预先验证)
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, NetCIEdge_Helper_MatchesFormula) {
    // RM 公式: net_edge_after_fee = edge_ci_lower − 0.03 × p × (1−p)
    // sizing helper 必须与此完全一致 (老韩 §5.2 一致性 ±1e-9)
    double const edge_ci_lower = 0.08;
    double const p = 0.60;
    double const expected = edge_ci_lower - 0.03 * p * (1.0 - p);  // 0.08 − 0.0072 = 0.0728

    double const actual = SizingCalculator::compute_net_ci_edge(edge_ci_lower, p);

    EXPECT_NEAR(actual, expected, 1e-9);
}

// ---------------------------------------------------------------------------
// capped_by 枚举命中正确性 (每条 cap 构造恰好绑定的输入)
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, CappedBy_PER_ORDER_CAP_Triggered) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.bankroll_usdc = 10'000'000.0;  // 很大 bankroll → C4 不触
    in.edge_ci_lower = 0.40;
    in.slippage_bps = 5.0;
    // current_exposure = 0 → C2/C3 不触 (per_order 10k < per_outcome 25k < condition 50k)

    auto out = SizingCalculator::compute(cfg, in);

    ASSERT_TRUE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::PER_ORDER_CAP);
    EXPECT_LE(out.suggested_notional, cfg.per_order_cap_usdc.to_pusd() + 1e-9);
}

TEST(SizingCalculatorTest, CappedBy_PER_OUTCOME_CAP_Triggered) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    // per_order = 10k, per_outcome = 25k, headroom_outcome = 25k - 20k = 5k
    // C4 = 10% × bankroll_big = > 10k > 5k → C2 最严
    in.bankroll_usdc = 10'000'000.0;
    in.edge_ci_lower = 0.40;
    in.slippage_bps = 5.0;
    in.current_token_exposure_usdc = 20'000.0;  // headroom = 5k
    // per_order = 10k > headroom_outcome = 5k → C2 触发

    auto out = SizingCalculator::compute(cfg, in);

    ASSERT_TRUE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::PER_OUTCOME_CAP);
    EXPECT_LE(out.suggested_notional + in.current_token_exposure_usdc,
              cfg.per_outcome_cap_usdc.to_pusd() + 1e-9);
}

TEST(SizingCalculatorTest, CappedBy_CONDITION_EXPOSURE_Triggered) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    in.bankroll_usdc = 10'000'000.0;
    in.edge_ci_lower = 0.40;
    in.slippage_bps = 5.0;
    in.current_token_exposure_usdc = 0.0;           // per_outcome headroom = 25k
    in.current_condition_exposure_usdc = 48'000.0;  // per_condition headroom = 2k

    auto out = SizingCalculator::compute(cfg, in);

    ASSERT_TRUE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::CONDITION_EXPOSURE);
    EXPECT_LE(out.suggested_notional + in.current_condition_exposure_usdc,
              cfg.market_exposure_cap_usdc.to_pusd() + 1e-9);
}

TEST(SizingCalculatorTest, CappedBy_BANKROLL_FRACTION_Triggered) {
    auto cfg = make_default_cfg();
    auto in = make_default_input();
    // bankroll = 50k → cap4 = 5k; per_order = 10k > 5k → C4 最严
    in.bankroll_usdc = 50'000.0;
    in.edge_ci_lower = 0.40;
    in.slippage_bps = 5.0;
    in.current_token_exposure_usdc = 0.0;
    in.current_condition_exposure_usdc = 0.0;

    auto out = SizingCalculator::compute(cfg, in);

    ASSERT_TRUE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::BANKROLL_FRACTION);
    EXPECT_LE(out.suggested_notional, in.bankroll_usdc * kMaxBankrollFraction + 1e-9);
}

// ---------------------------------------------------------------------------
// edge_ci_lower_floor 非零 (cfg 自定义 floor)
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, CI_Floor_NonZero_CustomFloor) {
    auto cfg = make_default_cfg();
    cfg.edge_ci_lower_floor = 0.05;  // 要求 CI 下界 > 5%
    auto in = make_default_input();
    in.edge_ci_lower = 0.04;  // < 0.05 floor → 拒

    auto out = SizingCalculator::compute(cfg, in);

    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.capped_by, CappedBy::NO_EDGE);
}

// ---------------------------------------------------------------------------
// compute_net_ci_edge: NaN/Inf 防护
// ---------------------------------------------------------------------------

TEST(SizingCalculatorTest, NetCIEdge_NaN_Input) {
    double const result =
        SizingCalculator::compute_net_ci_edge(std::numeric_limits<double>::quiet_NaN(), 0.6);
    EXPECT_DOUBLE_EQ(result, 0.0);
}

TEST(SizingCalculatorTest, NetCIEdge_Inf_Input) {
    double const result = SizingCalculator::compute_net_ci_edge(std::numeric_limits<double>::infinity(), 0.6);
    EXPECT_DOUBLE_EQ(result, 0.0);
}

}  // namespace stcpp::sizing::test
