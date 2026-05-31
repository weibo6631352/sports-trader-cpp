// tests/unit/test_commentaries_parser.cpp — commentaries Feed live_stats 提取 + poller + join
//
// Owner: 老雷 (GM) — live_stats 采集 hop 补齐 (5 个 g_*_diff 特征端到端)
// last_review: 2026-05-31
//
// 覆盖:
//   CP01 单 match: live_stats + 队名 → join_key + LiveStatsFields 正确
//   CP02 自闭合 <live_stats .../> 同样解析
//   CP03 无 live_stats 的 match → 跳过 (fail-safe)
//   CP04 多 match 全部解析
//   CP05 join_key 与消费侧 MakeLiveStatsJoinKey 一致 (大小写/空白 normalize)
//   CP06 CommentariesPoller PollAllOnce + fake fetcher → store Get
//   CP07 缺队名 → 跳过
//   CP08 SetLeagues 动态更新轮询集
//
// XML 信封按 docs/GOALSERVER/soccer-data-feed.md §5/§6/§8 (非想象): <match static_id id>,
//   <localteam name>, <visitorteam name>, <live_stats value="KV">。KV 格式 = 真实捕获。

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "stcpp/data/commentaries_parser.hpp"
#include "stcpp/data/commentaries_poller.hpp"
#include "stcpp/data/live_stats_store.hpp"

using stcpp::data::livescore::CommentariesParser;
using stcpp::data::livescore::CommentariesPoller;
using stcpp::data::livescore::LiveStatsMap;
using stcpp::data::livescore::LiveStatsStore;
using stcpp::data::livescore::MakeLiveStatsJoinKey;

namespace {

// doc-faithful 单 match (顶级联赛 commentaries 信封)
constexpr const char* kOneMatch =
    "<commentaries>"
    "<match status=\"Second Half\" static_id=\"2802908\" fix_id=\"3310076\" id=\"3472200\">"
    "<localteam name=\"Manchester City\" goals=\"2\" />"
    "<visitorteam name=\"Liverpool\" goals=\"1\" />"
    "<live_stats value=\"IDangerousAttacks=home:40,away:43|IOnTarget=home:3,away:6|"
    "IPosession=home:57,away:43|IRedCard=home:0,away:1|ICorner=home:5,away:3\" />"
    "</match>"
    "</commentaries>";

}  // namespace

TEST(CommentariesParser, CP01_SingleMatch_FieldsAndKey) {
    auto v = CommentariesParser::Parse(kOneMatch, "1457");
    ASSERT_EQ(v.size(), 1u);
    const auto& r = v[0];
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.static_id, "2802908");
    EXPECT_EQ(r.home_team, "Manchester City");
    EXPECT_EQ(r.away_team, "Liverpool");
    // KV → fields (de-vig 无关, 直读)
    EXPECT_EQ(r.stats.dangerous_attacks_home, 40);
    EXPECT_EQ(r.stats.dangerous_attacks_away, 43);
    EXPECT_EQ(r.stats.shots_on_target_home, 3);
    EXPECT_EQ(r.stats.shots_on_target_away, 6);
    EXPECT_EQ(r.stats.possession_home_pct, 57);
    EXPECT_EQ(r.stats.red_cards_away, 1);
    EXPECT_EQ(r.stats.corners_home, 5);
    // join_key = 消费侧同函数
    EXPECT_EQ(r.join_key, MakeLiveStatsJoinKey("1457", "Manchester City", "Liverpool"));
}

TEST(CommentariesParser, CP02_SelfClosingLiveStats) {
    // <live_stats value="..."/> 自闭合 (已是自闭合形态; 验证 '/>' 不影响 value 截取)
    auto v = CommentariesParser::Parse(kOneMatch, "1457");
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].stats.corners_away, 3);
}

TEST(CommentariesParser, CP03_NoLiveStats_Skipped) {
    constexpr const char* xml =
        "<commentaries><match static_id=\"99\" id=\"1\">"
        "<localteam name=\"A\" /><visitorteam name=\"B\" />"
        "</match></commentaries>";  // 无 live_stats
    auto v = CommentariesParser::Parse(xml, "1");
    EXPECT_TRUE(v.empty());
}

TEST(CommentariesParser, CP04_MultiMatch) {
    std::string xml = "<commentaries>";
    xml +=
        "<match static_id=\"11\" id=\"101\"><localteam name=\"Arsenal\" />"
        "<visitorteam name=\"Chelsea\" /><live_stats value=\"ICorner=home:7,away:2\" /></match>";
    xml +=
        "<match static_id=\"22\" id=\"102\"><localteam name=\"Spurs\" />"
        "<visitorteam name=\"Everton\" /><live_stats value=\"ICorner=home:1,away:9\" /></match>";
    xml += "</commentaries>";
    auto v = CommentariesParser::Parse(xml, "1");
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].home_team, "Arsenal");
    EXPECT_EQ(v[0].stats.corners_home, 7);
    EXPECT_EQ(v[1].away_team, "Everton");
    EXPECT_EQ(v[1].stats.corners_away, 9);
}

TEST(CommentariesParser, CP05_JoinKey_Normalize) {
    // 消费侧队名带空白 + 大小写差异 → normalize 后仍匹配 (Goalserve 同源理论一致, normalize 兜底)
    const std::string producer = MakeLiveStatsJoinKey("1457", "Manchester City", "Liverpool");
    const std::string consumer = MakeLiveStatsJoinKey("1457", "  manchester city ", "LIVERPOOL");
    EXPECT_EQ(producer, consumer);
    // league_id 不同 → key 不同 (防跨联赛同名碰撞)
    EXPECT_NE(producer, MakeLiveStatsJoinKey("9999", "Manchester City", "Liverpool"));
}

TEST(CommentariesParser, CP06_Poller_FakeFetch_StoreGet) {
    LiveStatsStore store;
    auto fetch = [](const std::string& league) -> std::string {
        if (league == "1457") return kOneMatch;
        return {};
    };
    CommentariesPoller poller(store, {"1457"}, fetch, /*poll_interval_ms=*/30'000);
    poller.PollAllOnce();
    EXPECT_EQ(poller.poll_count(), 1u);
    EXPECT_EQ(store.Size(), 1u);
    const auto got = store.Get(MakeLiveStatsJoinKey("1457", "Manchester City", "Liverpool"));
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->dangerous_attacks_home, 40);
    // 不存在的 key → nullopt
    EXPECT_FALSE(store.Get("nope").has_value());
}

TEST(CommentariesParser, CP07_MissingTeam_Skipped) {
    constexpr const char* xml =
        "<commentaries><match static_id=\"99\" id=\"1\">"
        "<localteam name=\"A\" />"  // 缺 visitorteam name
        "<live_stats value=\"ICorner=home:5,away:3\" /></match></commentaries>";
    auto v = CommentariesParser::Parse(xml, "1");
    EXPECT_TRUE(v.empty());
}

TEST(CommentariesParser, CP08_SetLeagues_Dynamic) {
    LiveStatsStore store;
    auto fetch = [](const std::string& league) -> std::string {
        if (league == "1457") return kOneMatch;
        return {};
    };
    // 构造空联赛集 → 首轮无数据
    CommentariesPoller poller(store, {}, fetch, /*poll_interval_ms=*/30'000);
    poller.PollAllOnce();
    EXPECT_EQ(store.Size(), 0u);
    // 动态注入联赛 → 下轮采到
    poller.SetLeagues({"1457"});
    poller.PollAllOnce();
    EXPECT_EQ(store.Size(), 1u);
}
