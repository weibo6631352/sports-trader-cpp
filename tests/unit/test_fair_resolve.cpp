// test_fair_resolve.cpp — ResolveFair 纯函数优先级单测 (R-2 老周/老郭 评审)
//   抽出纯函数的意义: 5 个 fair 源, 断言选了哪层 + 数值。原 inline 在 TickOne 600 行只能端到端测。
#include <gtest/gtest.h>

#include "stcpp/pricing/fair_resolve.hpp"

using stcpp::pricing::FairInputs;
using stcpp::pricing::FairSrc;
using stcpp::pricing::ResolveFair;

namespace {

// 1. derivative 覆盖一切 (totals/spreads 专属定价即该盘 fair; sharp/score/ML 全不叠)。
TEST(ResolveFair, DerivativeOverridesAll) {
    FairInputs in;
    in.p_market_devig = 0.3;
    in.derivative_p_yes = 0.62;
    in.sharp_yes = 0.5;          // 即便有 sharp
    in.has_real_fair = true;
    const auto r = ResolveFair(in);
    EXPECT_EQ(r.src, FairSrc::kDerivative);
    EXPECT_DOUBLE_EQ(r.p_fair, 0.62);  // 不被 ML 污染
}

// 2. 有真比分 + sharp 有效 → 锚 sharp (盈利修复核心)。
TEST(ResolveFair, SharpAnchorWhenValid) {
    FairInputs in;
    in.p_market_devig = 0.18;
    in.sharp_yes = 0.51;
    in.score_prior_yes = 0.5;
    in.prior_conf = 0.3;
    in.has_real_fair = true;
    const auto r = ResolveFair(in);
    EXPECT_EQ(r.src, FairSrc::kSharpInplay);
    EXPECT_DOUBLE_EQ(r.p_fair, 0.51);  // edge = sharp − 市场 = 0.51 − 0.18
}

// 3. sharp 无效 (-1) + 有真比分 → 回落 score-prior blend。
TEST(ResolveFair, FallbackToScorePriorWhenSharpInvalid) {
    FairInputs in;
    in.p_market_devig = 0.4;
    in.sharp_yes = -1.0;          // 无 bet365 odds
    in.score_prior_yes = 0.7;
    in.prior_conf = 1.0;          // 全信先验
    in.has_real_fair = true;
    const auto r = ResolveFair(in);
    EXPECT_EQ(r.src, FairSrc::kScorePriorBlend);
    EXPECT_DOUBLE_EQ(r.p_fair, 0.7);  // conf=1 → 纯先验
}

// 3b. sharp 越界 (>1) 也回落。
TEST(ResolveFair, SharpOutOfRangeFallsBack) {
    FairInputs in;
    in.sharp_yes = 1.5;
    in.has_real_fair = true;
    in.score_prior_yes = 0.6;
    in.p_market_devig = 0.6;
    in.prior_conf = 0.0;          // conf=0 → 纯市场
    const auto r = ResolveFair(in);
    EXPECT_EQ(r.src, FairSrc::kScorePriorBlend);
    EXPECT_DOUBLE_EQ(r.p_fair, 0.6);
}

// 4. 无真比分 + 无 derivative → 默认市场 de-vig (自己跟自己比, edge≈0)。
TEST(ResolveFair, DefaultMarketDevigWhenNoRealFair) {
    FairInputs in;
    in.p_market_devig = 0.33;
    in.sharp_yes = 0.9;          // 即便 sharp 有值, 但 has_real_fair=false 不用
    in.has_real_fair = false;
    const auto r = ResolveFair(in);
    EXPECT_EQ(r.src, FairSrc::kMarketDevig);
    EXPECT_DOUBLE_EQ(r.p_fair, 0.33);
}

}  // namespace
