// tests/unit/test_event_matcher.cpp — EventMatcher (A0 映射桥) 单测
//
// Owner: 老雷 (GM) — A0 配套 (M1 路线评审会决议)
// last_review: 2026-05-30
//
// 覆盖: 队名归一化 / overlap 相似度 / 双向分配 / kickoff 窗口 / fail-closed / 网球难例。

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/app/event_matcher.hpp"

using stcpp::app::EventMatcher;
using stcpp::app::EventMatchInput;
using stcpp::debug_api::EventScore;

namespace {

EventScore MakeEv(const std::string& id, const std::string& home, const std::string& away,
                  std::int64_t kickoff_sec) {
    EventScore es;
    es.found = true;
    es.event_id = id;
    es.home = home;
    es.away = away;
    es.kickoff_ts_sec = kickoff_sec;
    return es;
}

}  // namespace

// ============================================================================
// NormalizeTeamTokens
// ============================================================================
TEST(EventMatcherNorm, LowercaseAlnumTokens) {
    const auto t = EventMatcher::NormalizeTeamTokens("Los Angeles Lakers");
    ASSERT_EQ(t.size(), 3u);
    // 排序去重: angeles / lakers / los
    EXPECT_EQ(t[0], "angeles");
    EXPECT_EQ(t[1], "lakers");
    EXPECT_EQ(t[2], "los");
}

TEST(EventMatcherNorm, StripsPunctuationAndDedup) {
    // "F.C." 中的 '.' 是分隔符 → 拆成 f / c; "Porto" 出现两次去重为一.
    const auto t = EventMatcher::NormalizeTeamTokens("F.C. Porto (Porto)");
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0], "c");
    EXPECT_EQ(t[1], "f");
    EXPECT_EQ(t[2], "porto");
}

TEST(EventMatcherNorm, EmptyAndSymbolsOnly) {
    EXPECT_TRUE(EventMatcher::NormalizeTeamTokens("").empty());
    EXPECT_TRUE(EventMatcher::NormalizeTeamTokens("--- !!!").empty());
}

// ============================================================================
// TeamSimilarity (overlap coefficient)
// ============================================================================
TEST(EventMatcherSim, ExactMatch) {
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("Galorys", "Galorys"), 1.0);
}

TEST(EventMatcherSim, CaseAndPunctInsensitive) {
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("LA Lakers", "la lakers"), 1.0);
}

TEST(EventMatcherSim, AbbrevContainment_LALakers) {
    // "LA Lakers"{la,lakers} vs "Los Angeles Lakers"{los,angeles,lakers}
    // overlap = |{lakers}| / min(2,3) = 1/2 = 0.5
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("LA Lakers", "Los Angeles Lakers"), 0.5);
}

TEST(EventMatcherSim, NoCommonToken_Zero) {
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("Galorys", "largadosypelados"), 0.0);
}

TEST(EventMatcherSim, EmptyZero) {
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("", "Lakers"), 0.0);
}

// ============================================================================
// Match — 基础正向
// ============================================================================
TEST(EventMatcher, ExactBothTeams_Match) {
    EventMatcher m;
    EventMatchInput in{"Galorys", "largadosypelados", 1000000, "cs2"};
    std::vector<EventScore> cands{MakeEv("134000001", "Galorys", "largadosypelados", 1000000)};
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.inplay_match_id, "134000001");
}

TEST(EventMatcher, CrossAssignment_HomeAwaySwapped) {
    // market t0=A t1=B; event home=B away=A (顺序相反) → 交叉配应匹配
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 2000000, "nba"};
    std::vector<EventScore> cands{MakeEv("134000002", "Celtics", "Lakers", 2000000)};
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.inplay_match_id, "134000002");
}

TEST(EventMatcher, Orientation_DirectAssignment_YesIsHome) {
    // market t0(YES)=Lakers, event home=Lakers → YES=home → yes_is_home=true
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 2000000, "nba"};
    std::vector<EventScore> cands{MakeEv("d", "Lakers", "Celtics", 2000000)};
    const auto r = m.Match(in, cands);
    ASSERT_TRUE(r.matched);
    EXPECT_TRUE(r.yes_is_home);
}

TEST(EventMatcher, Orientation_CrossAssignment_YesIsAway) {
    // market t0(YES)=Lakers, event home=Celtics away=Lakers → YES=away → yes_is_home=false
    // (消费侧据此把 away_score 填进 score_home_total, 防 prior 方向反 → 老周张冠李戴)
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 2000000, "nba"};
    std::vector<EventScore> cands{MakeEv("c", "Celtics", "Lakers", 2000000)};
    const auto r = m.Match(in, cands);
    ASSERT_TRUE(r.matched);
    EXPECT_FALSE(r.yes_is_home);
}

TEST(EventMatcher, Orientation_Ambiguous_FailClosed) {
    // 队名近似 (City / City FC): 直配与交叉两种分配都过门且分差≈0 → orientation 模糊
    //   靠 >= 任意拍一边 = 比分方向可能接反 = 反向下单。P2-1 (老郭): fail-closed 不匹配。
    EventMatcher m;
    EventMatchInput in{"City", "City FC", 2000000, "soccer"};
    std::vector<EventScore> cands{MakeEv("amb", "City", "City FC", 2000000)};
    const auto r = m.Match(in, cands);
    EXPECT_FALSE(r.matched) << "P2-1: orientation 模糊 (直配/交叉都过门且分差<margin) → fail-closed 不匹配";
}

TEST(EventMatcher, AbbrevTeamsWithinThreshold_Match) {
    // 双队 overlap=0.5 (== 默认阈值 0.5) → 过门
    EventMatcher m;
    EventMatchInput in{"LA Lakers", "GS Warriors", 3000000, "nba"};
    std::vector<EventScore> cands{
        MakeEv("134000003", "Los Angeles Lakers", "Golden State Warriors", 3000000)};
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.inplay_match_id, "134000003");
}

// ============================================================================
// Match — fail-closed
// ============================================================================
TEST(EventMatcher, NoTeamMatch_FailClosed) {
    EventMatcher m;
    EventMatchInput in{"Galorys", "largadosypelados", 1000000, "cs2"};
    std::vector<EventScore> cands{MakeEv("134000004", "Team X", "Team Y", 1000000)};
    const auto r = m.Match(in, cands);
    EXPECT_FALSE(r.matched);
    EXPECT_TRUE(r.inplay_match_id.empty());
}

TEST(EventMatcher, OneTeamMatchOnly_FailClosed) {
    // 只有一队匹配 (min < 阈值) → 不匹配 (防张冠李戴)
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 1000000, "nba"};
    std::vector<EventScore> cands{MakeEv("134000005", "Lakers", "Bulls", 1000000)};
    const auto r = m.Match(in, cands);
    EXPECT_FALSE(r.matched);
}

TEST(EventMatcher, KickoffTooFar_FailClosed) {
    // 队名全配但 kickoff 差 > 15min → 时间窗口否决 (同队不同场次防误配)
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 1000000, "nba"};
    std::vector<EventScore> cands{MakeEv("134000006", "Lakers", "Celtics", 1000000 + 1000)};  // +1000s > 900
    const auto r = m.Match(in, cands);
    EXPECT_FALSE(r.matched);
}

TEST(EventMatcher, KickoffWithinWindow_Match) {
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 1000000, "nba"};
    std::vector<EventScore> cands{MakeEv("134000007", "Lakers", "Celtics", 1000000 + 600)};  // +600s < 900
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
}

TEST(EventMatcher, UnknownKickoff_TeamOnly_Match) {
    // kickoff 任一未知 (0) → 不据时间否决, 纯队名匹配
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 0, "nba"};
    std::vector<EventScore> cands{MakeEv("134000008", "Lakers", "Celtics", 0)};
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
}

TEST(EventMatcher, EmptyMarketTeams_FailClosed) {
    EventMatcher m;
    EventMatchInput in{"", "Celtics", 1000000, "nba"};
    std::vector<EventScore> cands{MakeEv("134000009", "Lakers", "Celtics", 1000000)};
    EXPECT_FALSE(m.Match(in, cands).matched);
}

TEST(EventMatcher, EmptyCandidates_FailClosed) {
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 1000000, "nba"};
    EXPECT_FALSE(m.Match(in, {}).matched);
}

// ============================================================================
// Match — 多候选择优 + 同队多场 (时间窗口区分)
// ============================================================================
TEST(EventMatcher, MultipleCandidates_PicksBestTeamScore) {
    EventMatcher m;
    EventMatchInput in{"Los Angeles Lakers", "Boston Celtics", 1000000, "nba"};
    std::vector<EventScore> cands{
        MakeEv("weak", "LA Lakers", "Celtics", 1000000),                   // 部分缩写, score 较低
        MakeEv("strong", "Los Angeles Lakers", "Boston Celtics", 1000000)  // 全配, score 满
    };
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.inplay_match_id, "strong");
}

TEST(EventMatcher, SameTeamsDoubleHeader_TimeWindowDisambiguates) {
    // 同两队同日两场 (G1/G2): 仅 kickoff 接近的那场匹配
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 5000000, "nba"};
    std::vector<EventScore> cands{
        MakeEv("game1", "Lakers", "Celtics", 5000000 + 100),    // 近 → 匹配
        MakeEv("game2", "Lakers", "Celtics", 5000000 + 100000)  // 远 → 否决
    };
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.inplay_match_id, "game1");
}

// ============================================================================
// 网球难例 (小段: 运动员名格式差异, 匹配率 ~70%)
// ============================================================================
TEST(EventMatcher, Tennis_PlayerNameVariation_KnownLimitation) {
    // Goalserve "Djokovic N." vs Polymarket "Novak Djokovic": {djokovic,n} vs {novak,djokovic}
    // overlap = |{djokovic}| / min(2,2) = 0.5 == 阈值 → 过 (姓匹配); 名/缩写不阻塞
    EXPECT_GE(EventMatcher::TeamSimilarity("Djokovic N.", "Novak Djokovic"), 0.5);
    // 但完全不同译名/拼写 → 不匹配 (fail-closed, 该盘不交易)
    EXPECT_LT(EventMatcher::TeamSimilarity("Alcaraz C.", "Rafael Nadal"), 0.5);
}
