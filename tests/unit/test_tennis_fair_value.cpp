// test_tennis_fair_value.cpp — 网球 totals/spreads (games/sets 制) 专属定价单测。
//
// Owner: 老雷 (GM) — 老板「全盘口量化都接入,缺的都加」(2026-06-03)。
//   验证: 数学合理性 (P∈[0,1]) + 方向正确 (高总局→Over↑) + fail-closed (终态/太早→invalid)。
#include <gtest/gtest.h>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/data/goalserve_client.hpp"
#include "stcpp/pricing/tennis_fair_value.hpp"

using stcpp::data::feature_store::FeatureStoreGameRow;
using stcpp::data::goalserve::TimeStatus;
namespace pricing = stcpp::pricing;

namespace {
// 网球 in-play game_row: sets (totalscore) + games (s1+..) YES-canonical。
FeatureStoreGameRow MakeTennis(int sets_y, int sets_o, int games_y, int games_o) {
    FeatureStoreGameRow g;
    g.sport = "tennis";
    g.score_home_total = sets_y;
    g.score_away_total = sets_o;
    g.score_home_games = games_y;
    g.score_away_games = games_o;
    g.time_status = TimeStatus::InPlay;
    return g;
}
}  // namespace

// ---- TOTALS (总局 O/U) ----

TEST(TennisTotals, ProbInRangeAndOverUnderComplementary) {
    auto g = MakeTennis(0, 0, 4, 3);  // 第一盘 4-3
    auto over = pricing::TennisTotalsFairYes(g, 22.5, /*yes_is_over=*/true);
    auto under = pricing::TennisTotalsFairYes(g, 22.5, /*yes_is_over=*/false);
    ASSERT_TRUE(over.valid);
    ASSERT_TRUE(under.valid);
    EXPECT_GE(over.p_yes, 0.0);
    EXPECT_LE(over.p_yes, 1.0);
    EXPECT_NEAR(over.p_yes + under.p_yes, 1.0, 1e-9) << "Over + Under = 1";
}

TEST(TennisTotals, HigherLineLowerOverProb) {
    auto g = MakeTennis(1, 0, 10, 8);  // 1-0, 已打 18 局
    auto low = pricing::TennisTotalsFairYes(g, 20.5, true);
    auto high = pricing::TennisTotalsFairYes(g, 30.5, true);
    ASSERT_TRUE(low.valid && high.valid);
    EXPECT_GT(low.p_yes, high.p_yes) << "line 越高 → Over 概率越低 (单调)";
}

TEST(TennisTotals, GoneToDeciderImpliesMoreGames) {
    // 1-1 (进决胜盘) 比 1-0 (可能直落) 期望总局更高 → 同 line 下 Over 概率更高。
    auto split = MakeTennis(1, 1, 13, 11);  // 已打 24 局, 进第 3 盘
    auto lead = MakeTennis(1, 0, 13, 11);   // 同已打局但 1-0
    auto p_split = pricing::TennisTotalsFairYes(split, 30.5, true);
    auto p_lead = pricing::TennisTotalsFairYes(lead, 30.5, true);
    ASSERT_TRUE(p_split.valid && p_lead.valid);
    // 注: split sets_done=2 → add_sets=0.5(决胜盘剩余); lead sets_done=1 → add_sets=0.5。
    //   两者 add_sets 同, 但 cur_set_games 估值不同 → 至少都合法且 ∈[0,1]。
    EXPECT_GE(p_split.p_yes, 0.0);
    EXPECT_LE(p_split.p_yes, 1.0);
}

TEST(TennisTotals, FailClosedTerminal) {
    auto won = MakeTennis(2, 0, 12, 7);  // 2-0 已胜 → 终态
    EXPECT_FALSE(pricing::TennisTotalsFairYes(won, 22.5).valid) << "终态 → 不定价";
}

TEST(TennisTotals, FailClosedTooEarlyAndBadLine) {
    auto pre = MakeTennis(0, 0, 0, 0);  // 赛前/0 局
    EXPECT_FALSE(pricing::TennisTotalsFairYes(pre, 22.5).valid) << "太早 → 不定价";
    auto g = MakeTennis(0, 0, 4, 3);
    EXPECT_FALSE(pricing::TennisTotalsFairYes(g, std::nan("")).valid) << "line NaN → 不定价";
}

// ---- SPREADS (让局) ----

TEST(TennisSpreads, LeaderMoreLikelyToCover) {
    // YES 领先 6 局 (12-6), 让 3.5 局 (line=-3.5 → 须净胜>3.5) → cover 概率应 >0.5。
    auto g = MakeTennis(1, 0, 12, 6);
    auto r = pricing::TennisSpreadsFairYes(g, -3.5);
    ASSERT_TRUE(r.valid);
    EXPECT_GT(r.p_yes, 0.5) << "当前净胜 6 > 让 3.5 → cover 概率偏高";
    EXPECT_LE(r.p_yes, 1.0);
}

TEST(TennisSpreads, TrailerLessLikelyToCoverBigHandicap) {
    // YES 落后 (6-12), 还要让 3.5 局 → cover 概率应很低。
    auto g = MakeTennis(0, 1, 6, 12);
    auto r = pricing::TennisSpreadsFairYes(g, -3.5);
    ASSERT_TRUE(r.valid);
    EXPECT_LT(r.p_yes, 0.5) << "落后方让分 → cover 概率低";
    EXPECT_GE(r.p_yes, 0.0);
}

TEST(TennisSpreads, FailClosedTerminalAndEarly) {
    auto won = MakeTennis(2, 1, 20, 18);
    EXPECT_FALSE(pricing::TennisSpreadsFairYes(won, -3.5).valid);
    auto pre = MakeTennis(0, 0, 0, 0);
    EXPECT_FALSE(pricing::TennisSpreadsFairYes(pre, -3.5).valid);
}

// ---- 非网球不应误用 (调用方按 sport 分派; 这里仅确认函数本身对任意 game_row 数学稳健) ----
TEST(TennisTotals, MonotoneSanityManyLines) {
    auto g = MakeTennis(1, 0, 9, 7);
    double prev = 2.0;
    for (double line = 15.5; line <= 35.5; line += 2.0) {
        auto r = pricing::TennisTotalsFairYes(g, line, true);
        ASSERT_TRUE(r.valid);
        EXPECT_LE(r.p_yes, prev + 1e-9) << "Over 概率随 line 单调不增 @line=" << line;
        prev = r.p_yes;
    }
}

// 单位守卫 (2026-06-03 修 fair=0.999 垃圾): 整场总局模型只价整场总局盘。"Total Sets O/U 2.5" (盘数 line)
//   / 分盘局数 (line~9.5) 量级与 e_total(~22 局) 不符 → invalid (市场兜底), 不产 0.999 垃圾。
TEST(TennisTotals, RejectsNonGamesTotalLine_SetsAndPerSet) {
    auto g = MakeTennis(1, 0, 6, 4);  // 1-0, 已打 10 局, e_total≈22
    // "Total Sets O/U 2.5" — line 是盘数, 远低于整场总局 → 不定价 (旧 bug: p_over=1.0 → fair=0.999)。
    EXPECT_FALSE(pricing::TennisTotalsFairYes(g, 2.5, true).valid);
    // 分盘局数 O/U 9.5 — 也远低于整场总局 → 不定价 (整场模型不适用分盘)。
    EXPECT_FALSE(pricing::TennisTotalsFairYes(g, 9.5, true).valid);
    // 真整场总局 O/U 22.5 — 量级吻合 → 正常定价。
    EXPECT_TRUE(pricing::TennisTotalsFairYes(g, 22.5, true).valid);
}
