// tests/unit/test_inplay_score_odds_integration.cpp — InplayScoreParser 同时解析 score + inplay odds
//
// Owner: 老雷 (GM) — 数据组接入设计落地 (小段 build-ready)
// 验证: 同一 inplay JSON, Parse 出 score + inplay_home_fairs (de-vig home fair); 1:1 对齐。
//   待 odds plan + 白名单: 结构就位, 开通即流入。

#include <cmath>
#include <gtest/gtest.h>

#include "stcpp/data/inplay_score_parser.hpp"

namespace inps = stcpp::data::inplay;
namespace gsi = stcpp::data::goalserve;

// 文档结构 soccer event: info (score/period) + odds (market "1"=1X2 Full Time)
static const std::string kSoccerWithOdds = R"JSON({
  "bm":"bet365","updated_ts":1780066172550,
  "events":{"134261101":{
    "info":{"id":"134261101","league_id":"18235","period":"2nd Half","score":"1:0","minute":"89:21","seconds":"89:21","time_status":"1"},
    "odds":{"1":{"name":"1X2 (Full Time)","participants":{
      "10270":{"name":"Home","value_eu":"1.20","suspend":"0"},
      "10271":{"name":"Draw","value_eu":"6.50","suspend":"0"},
      "10272":{"name":"Away","value_eu":"9.00","suspend":"0"}}}}}}})JSON";

// IOI-01: score + inplay_fair 同时解析, 1:1 对齐
TEST(InplayScoreOdds, IOI01_BothParsed) {
    const auto pr = inps::InplayScoreParser::Parse(kSoccerWithOdds, gsi::GoalserveSport::Soccer,
                                                   1780066172700000000LL);
    ASSERT_EQ(pr.scores.size(), 1u);
    ASSERT_EQ(pr.inplay_home_fairs.size(), pr.scores.size()) << "1:1 对齐";
    const double fair = pr.inplay_home_fairs[0];
    // de-vig: home = (1/1.20) / (1/1.20 + 1/6.50 + 1/9.00)
    const double ih = 1.0 / 1.20, id = 1.0 / 6.50, ia = 1.0 / 9.00;
    const double sum = ih + id + ia;
    EXPECT_NEAR(fair, ih / sum, 1e-9) << "inplay bet365 单源 de-vig home fair";
    EXPECT_GT(fair, 0.0);
    EXPECT_LT(fair, 1.0);
    // 双边/三边完整透传 (不丢信息; orientation 翻转在 trading_loop 按 yes_is_home 做)。
    ASSERT_EQ(pr.inplay_away_fairs.size(), pr.scores.size());
    ASSERT_EQ(pr.inplay_draw_fairs.size(), pr.scores.size());
    EXPECT_NEAR(pr.inplay_away_fairs[0], ia / sum, 1e-9) << "away de-vig fair (双边完整)";
    EXPECT_NEAR(pr.inplay_draw_fairs[0], id / sum, 1e-9) << "draw de-vig fair (3-way 完整)";
    // 三边和 = 1 (de-vig 归一; 完整无遗漏)。
    EXPECT_NEAR(fair + pr.inplay_away_fairs[0] + pr.inplay_draw_fairs[0], 1.0, 1e-9);
}

// IOI-02: 无 market "1" → -1 sentinel (fail-closed, 与 game_row 默认对齐)
TEST(InplayScoreOdds, IOI02_NoMarketSentinel) {
    const std::string j = R"JSON({"updated_ts":1780066172550,"events":{"1":{
      "info":{"id":"1","score":"0:0","time_status":"1"},
      "odds":{"27":{"participants":{"a":{"value_eu":"2.0"},"b":{"value_eu":"2.0"}}}}}}})JSON";
    const auto pr = inps::InplayScoreParser::Parse(j, gsi::GoalserveSport::Soccer, 1780066172700000000LL);
    ASSERT_EQ(pr.inplay_home_fairs.size(), 1u);
    EXPECT_NEAR(pr.inplay_home_fairs[0], -1.0, 1e-12) << "market '1' 缺 → sentinel -1";
}
