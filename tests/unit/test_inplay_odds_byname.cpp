// tests/unit/test_inplay_odds_byname.cpp — 按 name 选赛果盘 + de-vig (小田 sharp fair 选盘修复)
//
// Owner: 老雷 (GM) — 验证 SelectMatchWinnerMarketId/ParseInplayOddsDevigByName 选对赛果盘、
//   排掉 set/game/handicap 等非赛果盘、de-vig 守恒。这是非足球 sharp fair 能不能产的关键。

#include <string>
#include <string_view>
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

}  // namespace
