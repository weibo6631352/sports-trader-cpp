// tests/unit/test_fair_value_devig_prior.cpp — P1-8 de-vig + P0-3 真实先验单测
//
// Owner: 小肖 (numerical-algorithms, A 系统工程部)
// last_review: 2026-05-30
//
// 背景: docs/MEETINGS/2026-05-30-live-dogfood-remediation.md (P0-3 / P1-8)
//   M1 stub 把低价 outright (Spain mid=0.169) 强拉到 fair=0.434 → 假 edge 1076bps.
//   修法: edge 锚在 de-vig 概率 (剥 overround); 无真实先验 → fair=de-vig → edge≈0.
//
// 覆盖 (gtest, GLOB 自动发现, 无需改 CMake):
//   D01: devig_binary 双边 — 剥 overround, 对称归一
//   D02: devig_binary 单边 YES — 退化为裸 mid
//   D03: devig_binary 单边 NO — 隐含 YES = 1 - no_mid
//   D04: devig_binary 双边无效 — nullopt (fail-closed)
//   D05: devig_binary clamp — 极端值落在 (kProbEps, kProbMax)
//   D06: prior_confidence ramp — 0.15 → 0.60 线性, 越界 clamp
//   D07: blend_prob — 凸组合 (conf=0 全市场 / conf=1 全先验 / 中间加权)
//   D08: inplay_score_prior_yes — 0:0 恒 0.5, 领先随时钟更确定, 终态确定值
//   D09: REGRESSION Spain outright 0.169 — 无真实先验 → fair=de-vig → edge≈0
//   D10: de-vig 路径 vs 裸 mid — 锚在 de-vig 上消除 overround 偏置
//   D11: 终态混合 — conf=1.0 → fair 锚到确定性先验
//   D12: in-play 领先混合 — 真实先验把 fair 拉离市场, 产生正 edge
//
// 红线: 纯函数 noexcept; 不触 RM/账本; R-20 不在此层校验.

#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include "stcpp/pricing/fair_value_estimator.hpp"

namespace {

using namespace stcpp::pricing;

constexpr double kTol = 1e-9;

// edge(bps) = |p_fair - p_anchor| * 10000, 复刻 paper_loop 锚定语义.
double edge_bps(double p_fair, double p_anchor) {
    return std::fabs(p_fair - p_anchor) * 10'000.0;
}

// ---------------------------------------------------------------------------
// D01: de-vig 双边 — 剥 overround
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D01_Devig_TwoSided) {
    // 0.55 / 0.50 overround 1.05 → fair YES = 0.55 / 1.05
    auto p = devig_binary(0.55, 0.50);
    ASSERT_TRUE(p.has_value());
    EXPECT_NEAR(*p, 0.55 / 1.05, kTol);

    // 对称: 0.50/0.55 → fair = 0.50/1.05
    auto q = devig_binary(0.50, 0.55);
    ASSERT_TRUE(q.has_value());
    EXPECT_NEAR(*q, 0.50 / 1.05, kTol);
    // YES + NO de-vig 应归一
    EXPECT_NEAR(*p + *q, 1.0, kTol);
}

// ---------------------------------------------------------------------------
// D02: 单边 YES → 裸 mid
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D02_Devig_SingleSidedYes) {
    auto p = devig_binary(0.169, std::numeric_limits<double>::quiet_NaN());
    ASSERT_TRUE(p.has_value());
    EXPECT_NEAR(*p, 0.169, kTol);

    auto z = devig_binary(0.169, 0.0);  // no_mid<=0 视作无效
    ASSERT_TRUE(z.has_value());
    EXPECT_NEAR(*z, 0.169, kTol);
}

// ---------------------------------------------------------------------------
// D03: 单边 NO → 隐含 YES
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D03_Devig_SingleSidedNo) {
    auto p = devig_binary(std::numeric_limits<double>::quiet_NaN(), 0.30);
    ASSERT_TRUE(p.has_value());
    EXPECT_NEAR(*p, 1.0 - 0.30, kTol);
}

// ---------------------------------------------------------------------------
// D04: 双边无效 → nullopt (fail-closed)
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D04_Devig_BothInvalid) {
    EXPECT_FALSE(devig_binary(0.0, 0.0).has_value());
    EXPECT_FALSE(devig_binary(-1.0, -2.0).has_value());
    const double nan_v = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(devig_binary(nan_v, nan_v).has_value());
    const double inf_v = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(devig_binary(inf_v, inf_v).has_value());
}

// ---------------------------------------------------------------------------
// D05: clamp 边界
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D05_Devig_Clamp) {
    // 极小 yes vs 大 no → fair 近 0 但 >= kProbEps
    auto p = devig_binary(1e-12, 0.99);
    ASSERT_TRUE(p.has_value());
    EXPECT_GE(*p, kProbEps);
    EXPECT_LE(*p, kProbMax);
    // 极大 yes vs 极小 no → fair 近 1 但 <= kProbMax
    auto q = devig_binary(0.99, 1e-12);
    ASSERT_TRUE(q.has_value());
    EXPECT_GE(*q, kProbEps);
    EXPECT_LE(*q, kProbMax);
}

// ---------------------------------------------------------------------------
// D06: prior_confidence ramp
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D06_PriorConfidenceRamp) {
    EXPECT_NEAR(prior_confidence(0.0), kBasePriorConfidence, kTol);
    EXPECT_NEAR(prior_confidence(1.0), kMaxPriorConfidence, kTol);
    EXPECT_NEAR(prior_confidence(0.5), (kBasePriorConfidence + kMaxPriorConfidence) * 0.5, kTol);
    // 越界 clamp 到端点
    EXPECT_NEAR(prior_confidence(-1.0), kBasePriorConfidence, kTol);
    EXPECT_NEAR(prior_confidence(2.0), kMaxPriorConfidence, kTol);
    // 单调非降
    EXPECT_GE(prior_confidence(0.8), prior_confidence(0.2));
}

// ---------------------------------------------------------------------------
// D07: blend_prob 凸组合
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D07_BlendProb) {
    EXPECT_NEAR(blend_prob(0.9, 0.2, 0.0), 0.2, kTol);  // conf=0 → 全市场
    EXPECT_NEAR(blend_prob(0.9, 0.2, 1.0), 0.9, kTol);  // conf=1 → 全先验
    EXPECT_NEAR(blend_prob(0.8, 0.4, 0.5), 0.6, kTol);  // 中间加权
    // conf 越界 clamp
    EXPECT_NEAR(blend_prob(0.9, 0.2, 2.0), 0.9, kTol);
    EXPECT_NEAR(blend_prob(0.9, 0.2, -1.0), 0.2, kTol);
    // 结果 clamp 在 (kProbEps, kProbMax)
    EXPECT_GE(blend_prob(1.0, 1.0, 0.5), kProbEps);
    EXPECT_LE(blend_prob(1.0, 1.0, 0.5), kProbMax);
}

// ---------------------------------------------------------------------------
// D08: inplay_score_prior_yes — 比分/时钟耦合 + 终态
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D08_InplayScorePrior) {
    // 0:0 恒 0.5 不论时钟
    EXPECT_NEAR(inplay_score_prior_yes(0.0, 0.1, false), 0.5, kTol);
    EXPECT_NEAR(inplay_score_prior_yes(0.0, 0.9, false), 0.5, kTol);

    // 领先 > 0.5, 且越晚越确定
    const double lead_early = inplay_score_prior_yes(2.0, 0.1, false);
    const double lead_late = inplay_score_prior_yes(2.0, 0.9, false);
    EXPECT_GT(lead_early, 0.5);
    EXPECT_GT(lead_late, lead_early);

    // 落后 < 0.5
    EXPECT_LT(inplay_score_prior_yes(-2.0, 0.5, false), 0.5);

    // 终态: 领先→近 1, 落后→近 0, 平→0.5
    EXPECT_GT(inplay_score_prior_yes(1.0, 1.0, true), 0.99);
    EXPECT_LT(inplay_score_prior_yes(-1.0, 1.0, true), 0.01);
    EXPECT_NEAR(inplay_score_prior_yes(0.0, 1.0, true), 0.5, kTol);
}

// ---------------------------------------------------------------------------
// D09: REGRESSION — Spain outright 0.169, 无真实先验 → fair=de-vig → edge≈0
//
// 复刻 paper_loop has_real_fair=false 路径: p_fair = p_market_devig.
// 单边 (无 NO book) de-vig 退化为裸 0.169; fair 锚在同一值上 → edge=0.
// 这就是 P0-3 假阳性 (1076bps) 的根除点.
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D09_SpainOutright_NoFalsePositive) {
    const double yes_mid = 0.169;
    auto devig = devig_binary(yes_mid, std::numeric_limits<double>::quiet_NaN());
    ASSERT_TRUE(devig.has_value());
    const double p_market_devig = *devig;

    // has_real_fair=false → p_fair = p_market_devig (paper_loop 锚定语义)
    const double p_fair = p_market_devig;

    EXPECT_NEAR(p_market_devig, 0.169, kTol);
    EXPECT_NEAR(p_fair, 0.169, kTol);
    EXPECT_NEAR(edge_bps(p_fair, p_market_devig), 0.0, 1e-6)
        << "无真实先验时 edge 必须 ≈0 (P0-3 回归锁), 不再凭空 1076bps";
}

// ---------------------------------------------------------------------------
// D10: de-vig 锚消除 overround 偏置 (对比裸 mid 锚)
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D10_DevigRemovesVigBias) {
    // 带 vig 的两边 mid: 0.55 / 0.50 (overround 5%).
    const double yes_mid = 0.55, no_mid = 0.50;
    auto devig = devig_binary(yes_mid, no_mid);
    ASSERT_TRUE(devig.has_value());
    // de-vig fair (0.5238) < 裸 yes_mid (0.55): 锚在 de-vig 上, fair=de-vig → edge=0,
    // 而若错误锚在裸 mid 上自我比较仍 0, 但对真实先验做 edge 时 de-vig 才是无偏基准.
    EXPECT_LT(*devig, yes_mid) << "de-vig 应低于带 vig 的裸 yes_mid";
    EXPECT_NEAR(*devig, 0.55 / 1.05, kTol);
}

// ---------------------------------------------------------------------------
// D11: 终态混合 — conf=1.0 → fair 锚到确定性先验
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D11_TerminalBlend) {
    // 比赛已结束, 主队赢 → 先验近 1; 市场 de-vig 还在 0.80.
    const double p_prior = inplay_score_prior_yes(2.0 - 1.0, 1.0, /*terminal=*/true);
    auto devig = devig_binary(0.80, 0.20);
    ASSERT_TRUE(devig.has_value());
    const double conf = 1.0;  // 终态
    const double p_fair = blend_prob(p_prior, *devig, conf);
    EXPECT_GT(p_fair, 0.99) << "终态 conf=1.0 → fair 锚到确定性先验";
    EXPECT_GT(edge_bps(p_fair, *devig), 0.0) << "fair 远高于市场 → 正 edge";
}

// ---------------------------------------------------------------------------
// D12: in-play 领先混合 — 真实先验把 fair 拉离市场
// ---------------------------------------------------------------------------
TEST(FairValueDevigPrior, D12_InPlayLeadBlend) {
    // 进行中, 主队 3:0 大领先 80% 时钟 → 先验高; 市场 de-vig 还便宜 (0.40/0.60).
    const double p_prior = inplay_score_prior_yes(3.0, 0.8, /*terminal=*/false);
    auto devig = devig_binary(0.40, 0.60);
    ASSERT_TRUE(devig.has_value());
    const double conf = prior_confidence(0.8);
    const double p_fair = blend_prob(p_prior, *devig, conf);

    EXPECT_GT(p_prior, 0.5);
    EXPECT_GT(p_fair, *devig) << "领先先验把 fair 拉到市场之上";
    EXPECT_GT(edge_bps(p_fair, *devig), 0.0) << "产生正 edge (有真实信号)";
    // 混合后仍在 (de-vig, prior) 之间 (凸组合不外推)
    EXPECT_LE(p_fair, p_prior);
    EXPECT_GE(p_fair, *devig);
}

}  // namespace
