// test_derivative_fair_value.cpp — totals/spreads 专属定价模型单测 (数学正确性 + 边界 fail-closed)。
//
// Owner: 老雷 (GM) — 老板 2026-05-31「缺盘口模型就加」配套测试。
#include <gtest/gtest.h>

#include <cmath>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/data/goalserve_client.hpp"
#include "stcpp/pricing/derivative_fair_value.hpp"

using stcpp::data::feature_store::FeatureStoreGameRow;
using stcpp::data::goalserve::TimeStatus;
namespace pricing = stcpp::pricing;

namespace {

// 构造 in-play game_row: sport slug + 双边比分 + 当前已用秒 (time_frac = elapsed/total_game_seconds)。
FeatureStoreGameRow MakeInPlay(const char* sport, int home, int away, int elapsed_sec) {
    FeatureStoreGameRow g;
    g.sport = sport;
    g.score_home_total = home;  // YES-canonical: home = YES 边
    g.score_away_total = away;
    g.time_status = TimeStatus::InPlay;
    g.elapsed_sec = elapsed_sec;
    return g;
}

}  // namespace

// ---- 底层数学 -------------------------------------------------------------

TEST(DerivativePricingMath, NormalCdf) {
    EXPECT_NEAR(pricing::detail::NormalCdf(0.0), 0.5, 1e-9);
    EXPECT_NEAR(pricing::detail::NormalCdf(1.0), 0.8413, 1e-3);
    EXPECT_NEAR(pricing::detail::NormalCdf(-1.0), 0.1587, 1e-3);
}

TEST(DerivativePricingMath, PoissonSf) {
    // P(X>0.5) = P(X>=1) = 1 − e^−λ
    EXPECT_NEAR(pricing::detail::PoissonSf(0.5, 2.0), 1.0 - std::exp(-2.0), 1e-9);
    // P(X>−0.5) = 1 (X≥0)
    EXPECT_NEAR(pricing::detail::PoissonSf(-0.5, 2.0), 1.0, 1e-12);
    // λ=0 (无剩余得分): P(X>0.5)=0
    EXPECT_NEAR(pricing::detail::PoissonSf(0.5, 0.0), 0.0, 1e-12);
    // P(X>1.5)=P(X>=2)=1−e^−λ(1+λ)
    EXPECT_NEAR(pricing::detail::PoissonSf(1.5, 2.0), 1.0 - std::exp(-2.0) * 3.0, 1e-9);
}

// ---- TOTALS (大小分) ------------------------------------------------------

TEST(TotalsFairValue, BasketballOverWhenPaceHigh) {
    // 篮球半场 120 分 → 节奏外推终场 240 > line 211.5 → Over 概率高。
    auto g = MakeInPlay("basket", 70, 50, 1440);  // total=120, time_frac=1440/2880=0.5
    auto r = pricing::TotalsFairYes(g, 211.5);
    ASSERT_TRUE(r.valid);
    EXPECT_GT(r.p_yes, 0.85) << "终场外推 240 远超 211.5";
}

TEST(TotalsFairValue, BasketballUnderWhenPaceLow) {
    // 半场仅 90 分 → 外推终场 180 < 211.5 → Over 概率低。
    auto g = MakeInPlay("basket", 45, 45, 1440);
    auto r = pricing::TotalsFairYes(g, 211.5);
    ASSERT_TRUE(r.valid);
    EXPECT_LT(r.p_yes, 0.15);
}

TEST(TotalsFairValue, SoccerPoissonTail) {
    // 足球半场 2 球 → 外推终场 4 (剩余期望 2 球, 泊松)。line 2.5 → P(2+Poisson(2)>2.5)=1−e^−2≈0.865。
    auto g = MakeInPlay("soccer", 1, 1, 2700);  // total=2, time_frac=2700/5400=0.5
    auto r = pricing::TotalsFairYes(g, 2.5);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.p_yes, 1.0 - std::exp(-2.0), 1e-3);
}

TEST(TotalsFairValue, YesIsUnderFlipsProbability) {
    auto g = MakeInPlay("basket", 70, 50, 1440);
    auto over = pricing::TotalsFairYes(g, 211.5, /*yes_is_over=*/true);
    auto under = pricing::TotalsFairYes(g, 211.5, /*yes_is_over=*/false);
    ASSERT_TRUE(over.valid && under.valid);
    EXPECT_NEAR(over.p_yes + under.p_yes, 1.0, 1e-9);
}

TEST(TotalsFairValue, TooEarlyFailClosed) {
    auto g = MakeInPlay("basket", 8, 6, 100);  // time_frac=100/2880≈0.035 < 0.10
    EXPECT_FALSE(pricing::TotalsFairYes(g, 211.5).valid);
}

TEST(TotalsFairValue, PreGameFailClosed) {
    FeatureStoreGameRow g;
    g.sport = "basket";
    g.time_status = TimeStatus::NotStarted;  // 赛前 → 无法外推
    EXPECT_FALSE(pricing::TotalsFairYes(g, 211.5).valid);
}

TEST(TotalsFairValue, UnsupportedSportFailClosed) {
    auto g = MakeInPlay("tennis", 1, 0, 600);  // 网球 set 制, 无连续时钟 → 不定价
    EXPECT_FALSE(pricing::TotalsFairYes(g, 20.5).valid);
}

TEST(TotalsFairValue, NoLineFailClosed) {
    auto g = MakeInPlay("basket", 70, 50, 1440);
    EXPECT_FALSE(pricing::TotalsFairYes(g, std::nan("")).valid);
}

TEST(TotalsFairValue, TerminalDeterministic) {
    auto g = MakeInPlay("basket", 110, 105, 2880);
    g.time_status = TimeStatus::Ended;  // 终场 total=215 > 211.5 → Over 必然
    auto r = pricing::TotalsFairYes(g, 211.5);
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.p_yes, 1.0);
}

// ---- SPREADS (让分) -------------------------------------------------------

TEST(SpreadsFairValue, FavoriteCoveringWhenLeadLarge) {
    // YES 领先 20 (margin=+20), 让分 line=−5.5 → 需 margin>5.5, 当前已远超 → cover 概率高。
    auto g = MakeInPlay("basket", 70, 50, 1440);
    auto r = pricing::SpreadsFairYes(g, -5.5);
    ASSERT_TRUE(r.valid);
    EXPECT_GT(r.p_yes, 0.8);
}

TEST(SpreadsFairValue, NotCoveringWhenLeadThin) {
    // YES 仅领先 2, 让分 −5.5 (需 >5.5) → cover 概率 < 0.5。
    auto g = MakeInPlay("basket", 61, 59, 1440);
    auto r = pricing::SpreadsFairYes(g, -5.5);
    ASSERT_TRUE(r.valid);
    EXPECT_LT(r.p_yes, 0.5);
}

TEST(SpreadsFairValue, UnderdogPositiveLine) {
    // YES 是 underdog (line=+5.5 → 需 margin>−5.5, 即输不超 5.5 即 cover)。落后 3 → 仍 cover 概率高。
    auto g = MakeInPlay("basket", 57, 60, 1440);  // margin=−3
    auto r = pricing::SpreadsFairYes(g, 5.5);
    ASSERT_TRUE(r.valid);
    EXPECT_GT(r.p_yes, 0.5) << "落后 3 < 5.5 让分线 → underdog cover";
}

TEST(SpreadsFairValue, TooEarlyFailClosed) {
    auto g = MakeInPlay("basket", 8, 6, 100);
    EXPECT_FALSE(pricing::SpreadsFairYes(g, -5.5).valid);
}

// ---- 统一入口分派 ---------------------------------------------------------

TEST(DerivativeFairYes, DispatchByMarketType) {
    auto g = MakeInPlay("basket", 70, 50, 1440);
    EXPECT_TRUE(pricing::DerivativeFairYes(g, 2, 211.5).valid);   // totals
    EXPECT_TRUE(pricing::DerivativeFairYes(g, 1, -5.5).valid);    // spreads
    EXPECT_FALSE(pricing::DerivativeFairYes(g, 0, 0.0).valid);    // moneyline 不在此定价
    EXPECT_FALSE(pricing::DerivativeFairYes(g, 3, 0.0).valid);    // outright 无模型
}
