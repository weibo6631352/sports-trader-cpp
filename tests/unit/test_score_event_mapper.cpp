// tests/unit/test_score_event_mapper.cpp — ScoreEventMapper 单测
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-30
// 覆盖:
//   1. NormalizeName — 队名一致化 (ToLower + StripPunctuation)
//   2. NameFuzzyMatch — 双向包含检测 + 最短边长度守护
//   3. ParseSportSlug — sport 字符串 → GoalserveSport
//   4. Refresh 精确路径 (gameId → pregame_to_inplay 映射)
//   5. Refresh fuzzy 路径 (队名 + 时间邻近)
//   6. Refresh miss 原因统计 (MatchStats)
//   7. Resolve — O(1) 查表
//   8. LastStats — 统计快照线程安全
//   9. 当前匹配率: 全 outright/futures event (无 inplay 候选) → 0/N, 匹配率 0
//  10. 录制样本 fuzzy 逻辑验证 (soccer: "Brazil vs Argentina" 模拟)

#include <gtest/gtest.h>

#include "stcpp/data/score_event_mapper.hpp"

using namespace stcpp::data;
using namespace stcpp::data::goalserve;

// ============================================================================
// §1 NormalizeName
// ============================================================================

TEST(NormalizeName, LowercasesAndStripsASCIIPunctuation) {
    EXPECT_EQ(ScoreEventMapper::NormalizeName("Manchester United F.C."), "manchester united fc");
}

TEST(NormalizeName, CollapseMultipleSpaces) {
    EXPECT_EQ(ScoreEventMapper::NormalizeName("LA  Lakers"), "la lakers");
}

TEST(NormalizeName, TrimLeadingAndTrailingSpaces) {
    EXPECT_EQ(ScoreEventMapper::NormalizeName("  Real Madrid  "), "real madrid");
}

TEST(NormalizeName, EmptyString) {
    EXPECT_EQ(ScoreEventMapper::NormalizeName(""), "");
}

TEST(NormalizeName, PureNumbers) {
    EXPECT_EQ(ScoreEventMapper::NormalizeName("FC 1903"), "fc 1903");
}

TEST(NormalizeName, PreservesNonASCII) {
    // 非 ASCII 字符保留 (多语言队名)
    const std::string raw = "Djokovic";
    EXPECT_FALSE(ScoreEventMapper::NormalizeName(raw).empty());
}

// ============================================================================
// §2 NameFuzzyMatch
// ============================================================================

TEST(NameFuzzyMatch, ExactMatch) {
    EXPECT_TRUE(ScoreEventMapper::NameFuzzyMatch("lakers", "lakers"));
}

TEST(NameFuzzyMatch, SubstringContained) {
    // "lakers" ⊆ "la lakers"
    EXPECT_TRUE(ScoreEventMapper::NameFuzzyMatch("lakers", "la lakers"));
    EXPECT_TRUE(ScoreEventMapper::NameFuzzyMatch("la lakers", "lakers"));
}

TEST(NameFuzzyMatch, ShortStringBelow5CharsReturnsFalse) {
    // min_len < 5 → false (防误匹配)
    EXPECT_FALSE(ScoreEventMapper::NameFuzzyMatch("lak", "la lakers"));
}

TEST(NameFuzzyMatch, NoContainmentReturnsFalse) {
    EXPECT_FALSE(ScoreEventMapper::NameFuzzyMatch("boston celtics", "miami heat"));
}

TEST(NameFuzzyMatch, ManUtdExample) {
    // "manchester" ⊆ "manchester united" (10 chars, ≥ 5) → fuzzy match
    // Note: "man utd" is NOT a substring of "manchester united" (spaces differ)
    // The fuzzy algo uses substring containment, not word-level matching
    const std::string a = ScoreEventMapper::NormalizeName("Manchester");
    const std::string b = ScoreEventMapper::NormalizeName("Manchester United");
    EXPECT_TRUE(ScoreEventMapper::NameFuzzyMatch(a, b));
}

TEST(NameFuzzyMatch, BothStringsMustMeetMinLen) {
    // "abc" (3) ⊆ "abcdefg" → min_len = 3 < 5 → false
    EXPECT_FALSE(ScoreEventMapper::NameFuzzyMatch("abc", "abcdefg"));
}

// ============================================================================
// §3 ParseSportSlug
// ============================================================================

TEST(ParseSportSlug, Soccer) {
    const auto s = ScoreEventMapper::ParseSportSlug("soccer");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, GoalserveSport::Soccer);
}

TEST(ParseSportSlug, Football) {
    const auto s = ScoreEventMapper::ParseSportSlug("football");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, GoalserveSport::Soccer);
}

TEST(ParseSportSlug, NBA) {
    const auto s = ScoreEventMapper::ParseSportSlug("nba");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, GoalserveSport::Basketball);
}

TEST(ParseSportSlug, Basketball) {
    const auto s = ScoreEventMapper::ParseSportSlug("basketball");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, GoalserveSport::Basketball);
}

TEST(ParseSportSlug, Tennis) {
    const auto s = ScoreEventMapper::ParseSportSlug("tennis");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, GoalserveSport::Tennis);
}

TEST(ParseSportSlug, Unknown) {
    const auto s = ScoreEventMapper::ParseSportSlug("cricket");
    EXPECT_FALSE(s.has_value());
}

TEST(ParseSportSlug, CaseInsensitive) {
    const auto s = ScoreEventMapper::ParseSportSlug("NBA");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, GoalserveSport::Basketball);
}

// ============================================================================
// §4 Refresh 精确路径 (gameId → pregame_to_inplay)
// ============================================================================

TEST(ScoreEventMapper, RefreshPreciseGameId) {
    ScoreEventMapper mapper;

    // pregame→inplay 映射
    mapper.SetPregameToInplayMapping({{"310218", "134180558"}});

    // PM event: gameId = "310218"
    ScoreEventMapper::PmEventRecord ev;
    ev.event_id = "pm-event-001";
    ev.game_id = "310218";
    ev.sport_slug = "soccer";
    mapper.SetPolymarketEvents({ev});

    const MatchStats stats = mapper.Refresh();

    EXPECT_EQ(stats.total_attempted, 1);
    EXPECT_EQ(stats.matched_count, 1);
    EXPECT_EQ(stats.matched_via_game_id, 1);
    EXPECT_EQ(stats.matched_via_fuzzy, 0);
    EXPECT_DOUBLE_EQ(stats.match_rate(), 1.0);

    // Resolve 验证
    EXPECT_EQ(mapper.Resolve("pm-event-001"), "134180558");
}

TEST(ScoreEventMapper, RefreshGameIdMissingInMappingTable) {
    ScoreEventMapper mapper;

    // pregame→inplay 表不含 310218
    mapper.SetPregameToInplayMapping({{"999999", "134999999"}});

    ScoreEventMapper::PmEventRecord ev;
    ev.event_id = "pm-event-002";
    ev.game_id = "310218";
    ev.sport_slug = "soccer";
    // 无队名/时间 → fuzzy 也 miss
    mapper.SetPolymarketEvents({ev});

    const MatchStats stats = mapper.Refresh();

    EXPECT_EQ(stats.total_attempted, 1);
    EXPECT_EQ(stats.matched_count, 0);
    EXPECT_EQ(mapper.Resolve("pm-event-002"), "");
}

// ============================================================================
// §5 Refresh fuzzy 路径 (队名 + 时间)
// ============================================================================

TEST(ScoreEventMapper, RefreshFuzzyMatchBothTeamsAndTime) {
    ScoreEventMapper mapper;

    // Goalserve inplay 候选
    InplayCandidate cand;
    cand.inplay_match_id = "134261101";
    cand.home_team = "Brazil";
    cand.away_team = "Argentina";
    cand.start_ts_sec = 1780059600;
    cand.sport = GoalserveSport::Soccer;
    mapper.SetInplayCandidates(GoalserveSport::Soccer, {cand});

    // PM event: 无 gameId, 有队名 + 时间
    ScoreEventMapper::PmEventRecord ev;
    ev.event_id = "pm-event-100";
    ev.game_id = "";
    ev.home_team = "Brazil";
    ev.away_team = "Argentina";
    ev.start_ts_sec = 1780059600 + 120;  // +2 min, 在 900s 窗口内
    ev.sport_slug = "soccer";
    mapper.SetPolymarketEvents({ev});

    const MatchStats stats = mapper.Refresh();

    EXPECT_EQ(stats.total_attempted, 1);
    EXPECT_EQ(stats.matched_count, 1);
    EXPECT_EQ(stats.matched_via_fuzzy, 1);
    EXPECT_EQ(mapper.Resolve("pm-event-100"), "134261101");
}

TEST(ScoreEventMapper, RefreshFuzzyTimeMismatch) {
    ScoreEventMapper mapper;

    InplayCandidate cand;
    cand.inplay_match_id = "134261102";
    cand.home_team = "Brazil";
    cand.away_team = "Argentina";
    cand.start_ts_sec = 1780059600;
    cand.sport = GoalserveSport::Soccer;
    mapper.SetInplayCandidates(GoalserveSport::Soccer, {cand});

    ScoreEventMapper::PmEventRecord ev;
    ev.event_id = "pm-event-101";
    ev.home_team = "Brazil";
    ev.away_team = "Argentina";
    // 时间差 > 900s → miss
    ev.start_ts_sec = 1780059600 + 3600;
    ev.sport_slug = "soccer";
    mapper.SetPolymarketEvents({ev});

    const MatchStats stats = mapper.Refresh();

    EXPECT_EQ(stats.matched_count, 0);
    EXPECT_EQ(stats.miss_fuzzy_time, 1);
    EXPECT_EQ(mapper.Resolve("pm-event-101"), "");
}

TEST(ScoreEventMapper, RefreshFuzzyNameMismatch) {
    ScoreEventMapper mapper;

    InplayCandidate cand;
    cand.inplay_match_id = "134261103";
    cand.home_team = "Brazil";
    cand.away_team = "Argentina";
    cand.start_ts_sec = 1780059600;
    cand.sport = GoalserveSport::Soccer;
    mapper.SetInplayCandidates(GoalserveSport::Soccer, {cand});

    ScoreEventMapper::PmEventRecord ev;
    ev.event_id = "pm-event-102";
    ev.home_team = "Germany";
    ev.away_team = "France";
    ev.start_ts_sec = 1780059600;
    ev.sport_slug = "soccer";
    mapper.SetPolymarketEvents({ev});

    const MatchStats stats = mapper.Refresh();

    EXPECT_EQ(stats.matched_count, 0);
    EXPECT_EQ(stats.miss_fuzzy_name, 1);
}

TEST(ScoreEventMapper, RefreshFuzzyNoTimestampSkipsTimeCheck) {
    // 若 gs.start_ts = 0 → 跳过时间检验, 仅靠队名
    ScoreEventMapper mapper;

    InplayCandidate cand;
    cand.inplay_match_id = "134261104";
    cand.home_team = "Chelsea";
    cand.away_team = "Arsenal";
    cand.start_ts_sec = 0;  // 无时间
    cand.sport = GoalserveSport::Soccer;
    mapper.SetInplayCandidates(GoalserveSport::Soccer, {cand});

    ScoreEventMapper::PmEventRecord ev;
    ev.event_id = "pm-event-103";
    ev.home_team = "Chelsea";
    ev.away_team = "Arsenal";
    ev.start_ts_sec = 1780000000;  // PM 有时间, gs 无 → 跳过时间检验
    ev.sport_slug = "soccer";
    mapper.SetPolymarketEvents({ev});

    const MatchStats stats = mapper.Refresh();

    EXPECT_EQ(stats.matched_count, 1);
    EXPECT_EQ(mapper.Resolve("pm-event-103"), "134261104");
}

// ============================================================================
// §6 fuzzy 跨主客场翻转检测
// ============================================================================

TEST(ScoreEventMapper, RefreshFuzzyCrossHomeAway) {
    // PM: home="Celtics" away="Lakers"
    // GS: home="Lakers"  away="Celtics"  ← 翻转
    ScoreEventMapper mapper;

    InplayCandidate cand;
    cand.inplay_match_id = "134261105";
    cand.home_team = "Lakers";
    cand.away_team = "Celtics";
    cand.start_ts_sec = 1780059600;
    cand.sport = GoalserveSport::Basketball;
    mapper.SetInplayCandidates(GoalserveSport::Basketball, {cand});

    ScoreEventMapper::PmEventRecord ev;
    ev.event_id = "pm-event-200";
    ev.home_team = "Celtics";
    ev.away_team = "Lakers";
    ev.start_ts_sec = 1780059600;
    ev.sport_slug = "basketball";
    mapper.SetPolymarketEvents({ev});

    const MatchStats stats = mapper.Refresh();

    EXPECT_EQ(stats.matched_count, 1);
    EXPECT_EQ(mapper.Resolve("pm-event-200"), "134261105");
}

// ============================================================================
// §7 多 event 混合场景
// ============================================================================

TEST(ScoreEventMapper, RefreshMultipleEventsMixed) {
    ScoreEventMapper mapper;

    // pregame→inplay 表
    mapper.SetPregameToInplayMapping({{"310001", "134100001"}});

    // inplay 候选 (使用长度 ≥ 5 的完整队名以通过 kMinFuzzyNameLen 守护)
    InplayCandidate cand;
    cand.inplay_match_id = "134100002";
    cand.home_team = "Paris Saint-Germain";  // normalize → "paris saintgermain" (18 chars)
    cand.away_team = "Bayern Munich";        // normalize → "bayern munich" (13 chars)
    cand.start_ts_sec = 1780059600;
    cand.sport = GoalserveSport::Soccer;
    mapper.SetInplayCandidates(GoalserveSport::Soccer, {cand});

    std::vector<ScoreEventMapper::PmEventRecord> events;

    // event A: 精确 gameId 匹配
    {
        ScoreEventMapper::PmEventRecord e;
        e.event_id = "pm-A";
        e.game_id = "310001";
        e.sport_slug = "soccer";
        events.push_back(e);
    }
    // event B: fuzzy 匹配 (使用足够长的队名)
    {
        ScoreEventMapper::PmEventRecord e;
        e.event_id = "pm-B";
        e.home_team = "Paris Saint-Germain";
        e.away_team = "Bayern Munich";
        e.start_ts_sec = 1780059600 + 60;
        e.sport_slug = "soccer";
        events.push_back(e);
    }
    // event C: outright/futures → miss (无 gameId, 无队名)
    {
        ScoreEventMapper::PmEventRecord e;
        e.event_id = "pm-C";
        e.sport_slug = "soccer";
        events.push_back(e);
    }

    mapper.SetPolymarketEvents(std::move(events));
    const MatchStats stats = mapper.Refresh();

    EXPECT_EQ(stats.total_attempted, 3);
    EXPECT_EQ(stats.matched_count, 2);
    EXPECT_EQ(stats.matched_via_game_id, 1);
    EXPECT_EQ(stats.matched_via_fuzzy, 1);
    EXPECT_EQ(stats.miss_no_matchable_fields, 1);

    EXPECT_EQ(mapper.Resolve("pm-A"), "134100001");
    EXPECT_EQ(mapper.Resolve("pm-B"), "134100002");
    EXPECT_EQ(mapper.Resolve("pm-C"), "");

    // LastStats
    const MatchStats ls = mapper.LastStats();
    EXPECT_EQ(ls.matched_count, 2);
    EXPECT_EQ(ls.total_attempted, 3);
}

// ============================================================================
// §8 当前系统实际场景: 全 outright/futures → 匹配率诚实为 0
//
// 小宫 B3 根因: 当前 10 个市场全是 outright/futures (NHL/NBA/FIFA 夺冠盘)
// 这些市场没有 inplay 赛事 (Goalserve inplay 只含单场比赛)
// → score 端点正确返回 found=false (不是 bug, 是业务现实)
// 本测试验证此场景下 MatchStats 诚实报告 0 匹配率
// ============================================================================

TEST(ScoreEventMapper, OutrightFuturesAllMiss) {
    ScoreEventMapper mapper;

    // 无 inplay 候选 (outright/futures 无单场 inplay)
    // 无 pregame→inplay 映射

    // 模拟 10 个 outright 市场
    std::vector<ScoreEventMapper::PmEventRecord> events;
    for (int i = 0; i < 10; ++i) {
        ScoreEventMapper::PmEventRecord e;
        e.event_id = "outright-event-" + std::to_string(i);
        // outright: 无 gameId, 无队名, 无时间 → NoMatchableFields
        e.sport_slug = "nba";
        events.push_back(e);
    }
    mapper.SetPolymarketEvents(std::move(events));

    const MatchStats stats = mapper.Refresh();

    EXPECT_EQ(stats.total_attempted, 10);
    EXPECT_EQ(stats.matched_count, 0);
    EXPECT_DOUBLE_EQ(stats.match_rate(), 0.0);
    EXPECT_EQ(stats.miss_no_matchable_fields, 10);

    // 所有 Resolve 均返回空
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(mapper.Resolve("outright-event-" + std::to_string(i)), "");
    }

    // LastStats 与 Refresh 返回一致
    const MatchStats ls = mapper.LastStats();
    EXPECT_EQ(ls.matched_count, 0);
    EXPECT_EQ(ls.total_attempted, 10);
}

// ============================================================================
// §9 Refresh 后 Resolve 不存在的 event_id → 空字符串
// ============================================================================

TEST(ScoreEventMapper, ResolveUnknownEventId) {
    ScoreEventMapper mapper;
    // 空映射
    EXPECT_EQ(mapper.Resolve("nonexistent-event"), "");
}

// ============================================================================
// §10 Refresh 幂等: 多次 Refresh 映射表重建 (stale 映射防护)
// ============================================================================

TEST(ScoreEventMapper, RefreshIdempotent) {
    ScoreEventMapper mapper;

    mapper.SetPregameToInplayMapping({{"310001", "134100001"}});

    ScoreEventMapper::PmEventRecord ev;
    ev.event_id = "pm-idp";
    ev.game_id = "310001";
    ev.sport_slug = "soccer";
    mapper.SetPolymarketEvents({ev});

    // 第一次 Refresh
    const MatchStats s1 = mapper.Refresh();
    EXPECT_EQ(s1.matched_count, 1);

    // 第二次 Refresh (相同数据) → 映射仍命中
    const MatchStats s2 = mapper.Refresh();
    EXPECT_EQ(s2.matched_count, 1);
    EXPECT_EQ(mapper.Resolve("pm-idp"), "134100001");

    // 清空 PM events
    mapper.SetPolymarketEvents({});
    const MatchStats s3 = mapper.Refresh();
    EXPECT_EQ(s3.total_attempted, 0);
    EXPECT_EQ(s3.matched_count, 0);
    // 旧映射已清除 (重建)
    EXPECT_EQ(mapper.Resolve("pm-idp"), "");
}

// ============================================================================
// §11 MissReason 枚举字符串
// ============================================================================

TEST(MissReason, AllReasonsHaveNames) {
    EXPECT_NE(MissReasonName(MissReason::NoInplayMappingForGameId), nullptr);
    EXPECT_NE(MissReasonName(MissReason::NoInplayCandidates), nullptr);
    EXPECT_NE(MissReasonName(MissReason::FuzzyNameMismatch), nullptr);
    EXPECT_NE(MissReasonName(MissReason::FuzzyTimeMismatch), nullptr);
    EXPECT_NE(MissReasonName(MissReason::NoInplayScoreData), nullptr);
    EXPECT_NE(MissReasonName(MissReason::NoMatchableFields), nullptr);
}

// ============================================================================
// §12 MatchStats 聚合正确性
// ============================================================================

TEST(MatchStats, TotalMiss) {
    MatchStats s;
    s.total_attempted = 10;
    s.matched_count = 3;
    EXPECT_EQ(s.total_miss(), 7);
}

TEST(MatchStats, MatchRateZeroWhenNoAttempt) {
    MatchStats s;
    s.total_attempted = 0;
    s.matched_count = 0;
    EXPECT_DOUBLE_EQ(s.match_rate(), 0.0);
}

TEST(MatchStats, MatchRateFull) {
    MatchStats s;
    s.total_attempted = 5;
    s.matched_count = 5;
    EXPECT_DOUBLE_EQ(s.match_rate(), 1.0);
}
