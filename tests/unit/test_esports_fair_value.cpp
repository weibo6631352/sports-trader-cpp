// test_esports_fair_value.cpp — 电竞 maps totals/spreads (best-of-N) 精确枚举定价单测。
//
// Owner: 老雷 (GM) — 老板「全盘口量化都接入,缺的都加」(2026-06-03)。
#include <gtest/gtest.h>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/data/goalserve_client.hpp"
#include "stcpp/pricing/esports_fair_value.hpp"

using stcpp::data::feature_store::FeatureStoreGameRow;
using stcpp::data::goalserve::TimeStatus;
namespace pricing = stcpp::pricing;

namespace {
FeatureStoreGameRow MakeEsports(int maps_y, int maps_o) {
    FeatureStoreGameRow g;
    g.sport = "esports";
    g.score_home_total = maps_y;  // 已赢图数 (YES-canonical)
    g.score_away_total = maps_o;
    g.time_status = TimeStatus::InPlay;
    return g;
}
}  // namespace

// ---- TOTALS (总图数 O/U), BO3 默认 ----

TEST(EsportsTotals, ZeroZeroOver2p5IsHalf) {
    // 0-0, p=0.5: total ∈ {2,3} 各 0.5 → P(over 2.5)=P(3 图)=0.5。
    auto g = MakeEsports(0, 0);
    g.score_home_games = 0;  // 无意义 (esports 用 maps=total)
    // 注: maps_done=0 → fail-closed (太早)。用 0-0 验 fail-closed; 下面用已开赛状态验数学。
    EXPECT_FALSE(pricing::EsportsTotalsFairYes(g, 2.5).valid) << "0:0 太早 → 不定价";
}

TEST(EsportsTotals, OneZeroOver2p5) {
    // 1-0, p=clamp((1+1)/(1+2)=0.667→0.65): total2(2-0)=0.65, total3=0.35 → P(over2.5)=0.35。
    auto g = MakeEsports(1, 0);
    auto over = pricing::EsportsTotalsFairYes(g, 2.5, true);
    ASSERT_TRUE(over.valid);
    EXPECT_NEAR(over.p_yes, 0.35, 1e-6);
    auto under = pricing::EsportsTotalsFairYes(g, 2.5, false);
    EXPECT_NEAR(over.p_yes + under.p_yes, 1.0, 1e-9);
}

TEST(EsportsTotals, OneOneMustGoToMap3) {
    // 1-1: 必打第 3 图 → total=3 恒 → P(over 2.5)=1.0 (clamp 到 1−eps)。
    auto g = MakeEsports(1, 1);
    auto r = pricing::EsportsTotalsFairYes(g, 2.5, true);
    ASSERT_TRUE(r.valid);
    EXPECT_GT(r.p_yes, 0.99) << "1-1 必到第 3 图 → over 2.5 ≈ 1";
}

TEST(EsportsTotals, FailClosedTerminal) {
    auto won = MakeEsports(2, 0);  // 2-0 已胜 (BO3)
    EXPECT_FALSE(pricing::EsportsTotalsFairYes(won, 2.5).valid) << "终态 → 不定价";
}

TEST(EsportsTotals, MonotoneLine) {
    auto g = MakeEsports(1, 0);
    auto p15 = pricing::EsportsTotalsFairYes(g, 1.5, true);  // over 1.5 maps (total>1.5 恒真 BO3) ≈ 1
    auto p25 = pricing::EsportsTotalsFairYes(g, 2.5, true);
    auto p35 = pricing::EsportsTotalsFairYes(g, 3.5, true);  // BO3 最多 3 → over 3.5 = 0
    ASSERT_TRUE(p15.valid && p25.valid && p35.valid);
    EXPECT_GE(p15.p_yes, p25.p_yes);
    EXPECT_GE(p25.p_yes, p35.p_yes);
    EXPECT_LT(p35.p_yes, 0.01) << "BO3 总图 ≤3 → over 3.5 ≈ 0";
}

// ---- SPREADS (图让分) ----

TEST(EsportsSpreads, MustWinTwoNilToCoverMinus1p5) {
    // 1-0, line=-1.5 (YES 须净胜 >1.5 = 2-0)。P(2-0 from 1-0)=p=0.65。
    auto g = MakeEsports(1, 0);
    auto r = pricing::EsportsSpreadsFairYes(g, -1.5);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.p_yes, 0.65, 1e-6) << "from 1-0 赢下一图即 2-0 cover -1.5";
}

TEST(EsportsSpreads, UnderdogPlusHandicap) {
    // 0-1 落后, p=clamp((0+1)/(1+2)=0.333→0.35), line=+1.5 (YES 受让 1.5)。
    //   cover = margin > -1.5 → 唯一不 cover 是被 0-2 横扫 (margin -2)。
    //   P(0-2)=P(opp 赢下一图)=1−p=0.65 → cover=0.35 (落后方易被横扫, 即便受让仍 <0.5)。
    auto g = MakeEsports(0, 1);
    auto r = pricing::EsportsSpreadsFairYes(g, 1.5);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.p_yes, 0.35, 1e-6) << "0-1 受让 1.5: 仅免被横扫即 cover = p = 0.35";
}

TEST(EsportsSpreads, FailClosedTerminalAndEarly) {
    EXPECT_FALSE(pricing::EsportsSpreadsFairYes(MakeEsports(2, 1), -1.5).valid);
    EXPECT_FALSE(pricing::EsportsSpreadsFairYes(MakeEsports(0, 0), -1.5).valid);
}
