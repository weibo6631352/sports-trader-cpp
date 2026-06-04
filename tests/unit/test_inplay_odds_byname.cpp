// tests/unit/test_inplay_odds_byname.cpp — 按 name 选赛果盘 + de-vig (小田 sharp fair 选盘修复)
//
// Owner: 老雷 (GM) — 验证 SelectMatchWinnerMarketId/ParseInplayOddsDevigByName 选对赛果盘、
//   排掉 set/game/handicap 等非赛果盘、de-vig 守恒。这是非足球 sharp fair 能不能产的关键。

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/data/inplay_odds_parser.hpp"

namespace {

using stcpp::data::ParseInplayOddsDevigByName;
using stcpp::data::SelectMatchWinnerMarketId;

// 真实 tennis odds 结构缩样: To Win(赛果, id 67) + Set 2 Winner + Game Winner + Next Break Serve。
constexpr const char* kTennisOdds = R"JSON({
  "7895452": {"id":7895452,"name":"Next Break Serve","participants":{
      "a":{"name":"1","value_eu":"1.615","suspend":"0"},"b":{"name":"2","value_eu":"2.2","suspend":"0"}}},
  "130015": {"id":130015,"name":"Set 2 Winner","participants":{
      "a":{"name":"Home","value_eu":"1.3","suspend":"0"},"b":{"name":"Away","value_eu":"3.4","suspend":"0"}}},
  "80088": {"id":80088,"name":"Game Winner (2nd Set)","participants":{
      "a":{"name":"Home","value_eu":"1.9","suspend":"0"},"b":{"name":"Away","value_eu":"1.9","suspend":"0"}}},
  "67": {"id":67,"name":"To Win","participants":{
      "a":{"name":"Home","value_eu":"1.02","suspend":"0"},"b":{"name":"Away","value_eu":"19","suspend":"0"}}}
})JSON";

const std::vector<std::string_view> kTennisAllow = {"To Win", "Match Result", "Money Line"};
const std::vector<std::string_view> kForbid = {"Set ", "Game", "Map ", "Handicap", "Half",
                                               "Over", "Under", "Total", "Break", "Next "};

TEST(InplayOddsByName, SelectsToWinNotSetOrGameWinner) {
    const std::string mid = SelectMatchWinnerMarketId(kTennisOdds, kTennisAllow, kForbid);
    EXPECT_EQ(mid, "67") << "应选赛果盘 To Win(id 67), 而非 Set/Game Winner";
}

TEST(InplayOddsByName, DevigToWin2WaySumsToOne) {
    const auto d = ParseInplayOddsDevigByName(kTennisOdds, kTennisAllow, kForbid);
    ASSERT_TRUE(d.valid);
    EXPECT_NEAR(d.home_fair + d.away_fair, 1.0, 1e-6) << "2-way de-vig 必和为 1";
    EXPECT_EQ(d.draw_fair, 0.0);
    // value_eu Home=1.02/Away=19 → implied 0.980/0.053 → de-vig home≈0.949
    EXPECT_GT(d.home_fair, 0.93);
    EXPECT_LT(d.away_fair, 0.07);
}

TEST(InplayOddsByName, ForbiddenBlocksEvenIfNameWouldMatch) {
    // allow 含 "Winner", 但所有含 Winner 的盘都带 forbidden(Set/Game) → 应选不到
    const std::vector<std::string_view> allow_winner = {"Winner"};
    const std::string mid = SelectMatchWinnerMarketId(kTennisOdds, allow_winner, kForbid);
    EXPECT_EQ(mid, "") << "Set Winner/Game Winner 被 forbidden 排掉, 无裸 'Winner' 盘 → 选不到";
}

TEST(InplayOddsByName, SoccerFullTimeNotHalfNotGoals) {
    // soccer: 1x2(Full Time) 赛果 vs 1x2(1st Half) vs Home Team Goals(id=1 陷阱)
    constexpr const char* kSoccer = R"JSON({
      "1": {"id":1,"name":"Home Team Goals","participants":{
          "a":{"name":"Over","value_eu":"1.8","suspend":"0"},"b":{"name":"Under","value_eu":"2.0","suspend":"0"}}},
      "27": {"id":27,"name":"1x2 (1st Half)","participants":{
          "a":{"name":"Home","value_eu":"2.5","suspend":"0"},"c":{"name":"Draw","value_eu":"2.0","suspend":"0"},"b":{"name":"Away","value_eu":"3.0","suspend":"0"}}},
      "1777": {"id":1777,"name":"1x2 (Full Time)","participants":{
          "a":{"name":"Home","value_eu":"1.8","suspend":"0"},"c":{"name":"Draw","value_eu":"3.5","suspend":"0"},"b":{"name":"Away","value_eu":"4.2","suspend":"0"}}}
    })JSON";
    const std::vector<std::string_view> allow = {"1x2 (Full Time)", "Fulltime Result", "Match Result"};
    const std::string mid = SelectMatchWinnerMarketId(kSoccer, allow, kForbid);
    EXPECT_EQ(mid, "1777") << "应选全场赛果(1777), 排掉 Home Team Goals(id=1 陷阱) 和 1st Half";

    const auto d = ParseInplayOddsDevigByName(kSoccer, allow, kForbid);
    ASSERT_TRUE(d.valid);
    EXPECT_GT(d.draw_fair, 0.0) << "soccer 3-way 应有 draw 腿";
    EXPECT_NEAR(d.home_fair + d.away_fair + d.draw_fair, 1.0, 1e-6);
}

// ============================================================================
// 字典驱动选盘 (2026-06-04 老板「用 goalserve 字典, 单白名单太脆弱」):
//   IsResultMarketName 启发式 + SelectResultMarketId(id-set 优先 + 启发式回退)。
//   名单/边界对照 8 运动字典实拉真值 (soccer/basket/tennis/baseball/hockey/amf/volley)。
// ============================================================================
using stcpp::data::ParseInplayOddsDevigResult;
using stcpp::data::SelectResultMarketId;
using stcpp::data::inplay_odds_detail::IsResultMarketName;

TEST(InplayDictResult, IsResultMarketName_RealDictNames_Positive) {
    // 8 运动字典实拉的真·全场赛果盘名 → 全部应判 true。
    EXPECT_TRUE(IsResultMarketName("Fulltime Result"));        // soccer 1777
    EXPECT_TRUE(IsResultMarketName("Game Lines Money Line"));  // basket 180032 / hockey 9632 (旧 bug 误杀)
    EXPECT_TRUE(IsResultMarketName("To Win"));                 // tennis 67
    EXPECT_TRUE(IsResultMarketName("Home/Away"));              // baseball/amf/volley/hockey
    EXPECT_TRUE(IsResultMarketName("1x2"));                    // basket 96 / baseball 192 / amf 63420
    EXPECT_TRUE(IsResultMarketName("Money Line"));
    EXPECT_TRUE(IsResultMarketName("Match Result"));
}

TEST(InplayDictResult, IsResultMarketName_DerivedMarkets_Negative) {
    // 派生/分段盘 → 全部应判 false (字典实拉的真·陷阱名)。
    EXPECT_FALSE(IsResultMarketName("Game Winner (2nd Set)"));   // tennis 80088
    EXPECT_FALSE(IsResultMarketName("Game Winner"));             // tennis 80856 — 裸 "Game" 不再误杀主盘但此盘仍排掉
    EXPECT_FALSE(IsResultMarketName("Point Winner"));            // tennis 130096
    EXPECT_FALSE(IsResultMarketName("Home/Away (5th Set)"));     // volley/tennis 分盘
    EXPECT_FALSE(IsResultMarketName("1x2 (1st Half)"));          // soccer 27
    EXPECT_FALSE(IsResultMarketName("1x2 Extra Time"));          // soccer 2134
    EXPECT_FALSE(IsResultMarketName("1x2 between 00:00 m and 15:00 m"));  // soccer 1353
    EXPECT_FALSE(IsResultMarketName("To Win the Trophy"));       // soccer 9200415
    EXPECT_FALSE(IsResultMarketName("To win both halves"));      // soccer 451
    EXPECT_FALSE(IsResultMarketName("Penalties Shootout Winner"));  // soccer 50010
    EXPECT_FALSE(IsResultMarketName("Winner by shootout"));      // hockey 5100016
    EXPECT_FALSE(IsResultMarketName("Asian Handicap"));
    EXPECT_FALSE(IsResultMarketName("Total Points"));
    EXPECT_FALSE(IsResultMarketName("Home Team Goals"));         // soccer id=1 旧陷阱
    EXPECT_FALSE(IsResultMarketName(""));
}

TEST(InplayDictResult, Heuristic_PicksGameLinesMoneyLine_FixesBasketBug) {
    // 旧静态表把裸 "Game" 拉黑 → "Game Lines Money Line" 被误杀 → 篮球 sharp 全丢。
    //   新启发式 (空 id-set) 应正确选中它, 排掉 Total/Spread。
    constexpr const char* kBasket = R"JSON({
      "90": {"id":90,"name":"Total Points","participants":{
          "a":{"name":"Over","value_eu":"1.9","suspend":"0"},"b":{"name":"Under","value_eu":"1.9","suspend":"0"}}},
      "1446": {"id":1446,"name":"Game Lines Spread","participants":{
          "a":{"name":"Home","value_eu":"1.9","suspend":"0"},"b":{"name":"Away","value_eu":"1.9","suspend":"0"}}},
      "180032": {"id":180032,"name":"Game Lines Money Line","participants":{
          "a":{"name":"Home","value_eu":"1.45","suspend":"0"},"b":{"name":"Away","value_eu":"2.75","suspend":"0"}}}
    })JSON";
    const std::unordered_set<std::string> kNoIds;
    EXPECT_EQ(SelectResultMarketId(kBasket, kNoIds), "180032")
        << "启发式应选 Game Lines Money Line (旧 bug: 被裸 Game 拉黑)";
    const auto d = ParseInplayOddsDevigResult(kBasket, kNoIds);
    ASSERT_TRUE(d.valid);
    EXPECT_NEAR(d.home_fair + d.away_fair, 1.0, 1e-6);
    EXPECT_GT(d.home_fair, 0.6);  // 1.45 < 2.75 → home 占优
}

TEST(InplayDictResult, IdSet_MatchesByIdAcrossNameDrift) {
    // name 漂移: 字典 id 1777 叫 "Fulltime Result", feed 同 id 却叫 "Random Sponsor Name"。
    //   id-set 命中 → 仍正确选 1777 (id 比 name 稳, 老板要的字典 id 匹配)。
    constexpr const char* kDrift = R"JSON({
      "27": {"id":27,"name":"1x2 (1st Half)","participants":{
          "a":{"name":"Home","value_eu":"2.5","suspend":"0"},"c":{"name":"Draw","value_eu":"2.0","suspend":"0"},"b":{"name":"Away","value_eu":"3.0","suspend":"0"}}},
      "1777": {"id":1777,"name":"Sponsored Fulltime XYZ","participants":{
          "a":{"name":"Home","value_eu":"1.8","suspend":"0"},"c":{"name":"Draw","value_eu":"3.5","suspend":"0"},"b":{"name":"Away","value_eu":"4.2","suspend":"0"}}}
    })JSON";
    const std::unordered_set<std::string> ids = {"1777"};
    EXPECT_EQ(SelectResultMarketId(kDrift, ids), "1777") << "id-set 应免疫 name 漂移";
    // 无 id-set 时, 启发式对怪名 "Sponsored Fulltime XYZ" 含 "fulltime"? 不含 "fulltime result" 词组 → 选不到。
    const std::unordered_set<std::string> kNoIds;
    EXPECT_EQ(SelectResultMarketId(kDrift, kNoIds), "") << "纯启发式对漂移怪名 fail-closed";
}

TEST(InplayDictResult, IdPriorityOverHeuristic) {
    // 同时有启发式可选盘 + id-set 指定另一盘 → id 优先。
    constexpr const char* kBoth = R"JSON({
      "67": {"id":67,"name":"To Win","participants":{
          "a":{"name":"Home","value_eu":"1.5","suspend":"0"},"b":{"name":"Away","value_eu":"2.6","suspend":"0"}}},
      "999": {"id":999,"name":"Custom Result Board","participants":{
          "a":{"name":"Home","value_eu":"1.4","suspend":"0"},"b":{"name":"Away","value_eu":"2.9","suspend":"0"}}}
    })JSON";
    const std::unordered_set<std::string> ids = {"999"};
    EXPECT_EQ(SelectResultMarketId(kBoth, ids), "999") << "id-set 命中优先于启发式 To Win";
}

}  // namespace
