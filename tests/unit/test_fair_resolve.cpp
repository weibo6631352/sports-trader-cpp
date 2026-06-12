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

// 4. 赔率解耦 (老板 2026-06-12 删比分门): sharp 有效 + 无比分 → 仍锚 sharp (进场认赔率不认比分)。
TEST(ResolveFair, SharpUsedWithoutRealFair) {
    FairInputs in;
    in.p_market_devig = 0.33;
    in.sharp_yes = 0.9;          // 有赔率
    in.has_real_fair = false;    // 无比分
    const auto r = ResolveFair(in);
    EXPECT_EQ(r.src, FairSrc::kSharpInplay);  // 解耦后: 赔率即用, 不被比分门锁
    EXPECT_DOUBLE_EQ(r.p_fair, 0.9);
}

// 5. 无赔率 + 无比分 → 默认市场 de-vig (edge≈0, 不交易)。
TEST(ResolveFair, DefaultMarketDevigWhenNeither) {
    FairInputs in;
    in.p_market_devig = 0.33;
    in.sharp_yes = -1.0;         // 无赔率
    in.has_real_fair = false;    // 无比分
    const auto r = ResolveFair(in);
    EXPECT_EQ(r.src, FairSrc::kMarketDevig);
    EXPECT_DOUBLE_EQ(r.p_fair, 0.33);
}

// 6. 备用: 赔率消失 (sharp 无效) + 有比分 → score-prior 当备用 (老板「比分作为备用 sharp」)。
TEST(ResolveFair, ScorePriorBackupWhenSharpGone) {
    FairInputs in;
    in.p_market_devig = 0.4;
    in.sharp_yes = -1.0;          // 赔率没了
    in.score_prior_yes = 0.65;
    in.prior_conf = 1.0;          // 纯先验
    in.has_real_fair = true;      // 还有比分 → 走备用
    const auto r = ResolveFair(in);
    EXPECT_EQ(r.src, FairSrc::kScorePriorBlend);
    EXPECT_DOUBLE_EQ(r.p_fair, 0.65);
}

}  // namespace
