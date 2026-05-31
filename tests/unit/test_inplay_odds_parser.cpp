// tests/unit/test_inplay_odds_parser.cpp — inplay bet365 odds → 单源 de-vig (按文档结构)
#include <cmath>
#include <gtest/gtest.h>
#include "stcpp/data/inplay_odds_parser.hpp"
using stcpp::data::ParseInplayOddsDevig;

// IO-01: 3-way (1x2) — home 1.85 / draw 3.40 / away 4.20 → de-vig
TEST(InplayOdds, IO01_ThreeWay) {
    const std::string j =
        R"({"odds":{"1x2":{"name":"Match Result","participants":{)"
        R"("1":{"value_eu":"1.85","suspend":false},)"
        R"("2":{"value_eu":"3.40"},)"
        R"("3":{"value_eu":"4.20"}}}}})";
    const auto r = ParseInplayOddsDevig(j, "1x2");
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.n_participants, 3u);
    const double ih = 1.0 / 1.85, id = 1.0 / 3.40, ia = 1.0 / 4.20;
    const double sum = ih + id + ia;
    EXPECT_NEAR(r.home_fair, ih / sum, 1e-9);   // home/YES
    EXPECT_NEAR(r.draw_fair, id / sum, 1e-9);
    EXPECT_NEAR(r.away_fair, ia / sum, 1e-9);
    EXPECT_NEAR(r.home_fair + r.draw_fair + r.away_fair, 1.0, 1e-9) << "de-vig 后归一";
}

// IO-02: 2-way (无平局) — home/away
TEST(InplayOdds, IO02_TwoWay) {
    const std::string j =
        R"({"odds":{"ml":{"participants":{"1":{"value_eu":"1.50"},"2":{"value_eu":"2.60"}}}}})";
    const auto r = ParseInplayOddsDevig(j, "ml");
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.n_participants, 2u);
    EXPECT_NEAR(r.home_fair + r.away_fair, 1.0, 1e-9);
    EXPECT_GT(r.home_fair, r.away_fair) << "1.50 < 2.60 → home 概率高";
}

// IO-03: market 不存在 / 无 value → fail-closed
TEST(InplayOdds, IO03_FailClosed) {
    EXPECT_FALSE(ParseInplayOddsDevig(R"({"odds":{"1x2":{"participants":{}}}})", "1x2").valid);
    EXPECT_FALSE(ParseInplayOddsDevig(R"({"odds":{}})", "1x2").valid);
    EXPECT_FALSE(ParseInplayOddsDevig("", "1x2").valid);
}

// ---- 真实 Goalserve 结构 (xiaoduan-w8 §3.2 实测样本; name + 长pid + suspend + 兄弟 market) ----

// 真实样本: market "1"=1X2全场, 兄弟 "27"=1X2上半场 (在前, 含暂停腿 = 干扰项); pid 长数字;
//   name=Home/Draw/Away; suspend "0"活跃/"1"暂停。单 raw string (多行, JSON 不在意空白)。
// 注: market name 含 "(1st Half)" → 默认 R"(...)" 的 )" 会提前终止; 用自定义分隔符 JSON。
static const char* kRealInplaySoccer = R"JSON({"odds":{
  "27":{"name":"1x2 (1st Half)","participants":{
    "p270":{"name":"Home","value_eu":"4.5","suspend":"1"},
    "p271":{"name":"Draw","value_eu":"2.1","suspend":"0"},
    "p272":{"name":"Away","value_eu":"3.0","suspend":"0"}}},
  "1":{"name":"1x2 (Full Time)","participants":{
    "p370":{"name":"Home","value_eu":"1.85","suspend":"0"},
    "p371":{"name":"Draw","value_eu":"3.40","suspend":"0"},
    "p372":{"name":"Away","value_eu":"4.20","suspend":"0"}}}}})JSON";

// IO-04: 真实结构 — 取 market "1" (全场), 不被兄弟 "27" 或 "suspend":"1" 干扰。
TEST(InplayOdds, IO04_RealStructure_FullTimeMarket) {
    const auto r = ParseInplayOddsDevig(kRealInplaySoccer, "1");
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.n_participants, 3u);
    // 必须用全场 (1.85/3.40/4.20), 不是上半场 (4.5/2.1/3.0)。
    const double ih = 1.0 / 1.85, id = 1.0 / 3.40, ia = 1.0 / 4.20;
    const double sum = ih + id + ia;
    EXPECT_NEAR(r.home_fair, ih / sum, 1e-9) << "应取 market 1 全场 Home, 非兄弟 27/暂停值";
    EXPECT_NEAR(r.home_fair + r.draw_fair + r.away_fair, 1.0, 1e-9);
}

// IO-05: market key 锚定 — "1" 不被 "suspend":"1" 误匹配 (旧 bug)。
TEST(InplayOdds, IO05_MarketKeyAnchor_NotSuspendValue) {
    // market "27" 在前且含 "suspend":"1"; 若用旧 `"1"` 松匹配会命中 suspend 值 → 解错。
    const auto r = ParseInplayOddsDevig(kRealInplaySoccer, "1");
    ASSERT_TRUE(r.valid);
    EXPECT_GT(r.home_fair, r.away_fair) << "全场 1.85<4.20 → home 概率应更高 (证明取对了 market)";
}

// IO-06: 按 name 匹配, 非位置 — participant 乱序 (Away 在前, Home 在后) home_fair 仍取 Home 腿。
TEST(InplayOdds, IO06_NameBased_NotPositional) {
    const char* shuffled = R"({"odds":{"1":{"participants":{)"
        R"("pA":{"name":"Away","value_eu":"4.20","suspend":"0"},)"  // Away 排第一
        R"("pD":{"name":"Draw","value_eu":"3.40","suspend":"0"},)"
        R"("pH":{"name":"Home","value_eu":"1.85","suspend":"0"}}}}})";  // Home 排最后
    const auto r = ParseInplayOddsDevig(shuffled, "1");
    ASSERT_TRUE(r.valid);
    const double ih = 1.0 / 1.85, id = 1.0 / 3.40, ia = 1.0 / 4.20;
    const double sum = ih + id + ia;
    EXPECT_NEAR(r.home_fair, ih / sum, 1e-9) << "name=Home 取 1.85, 不受位置 (排最后) 影响";
    EXPECT_NEAR(r.away_fair, ia / sum, 1e-9) << "name=Away 取 4.20";
}

// IO-07: 暂停腿剔除 — home 暂停 → 无活跃 home 腿 → fail-closed (不喂模型暂停的 stale 值)。
TEST(InplayOdds, IO07_SuspendedHome_FailClosed) {
    const char* suspended_home = R"({"odds":{"1":{"participants":{)"
        R"("pH":{"name":"Home","value_eu":"1.85","suspend":"1"},)"  // home 暂停
        R"("pA":{"name":"Away","value_eu":"2.10","suspend":"0"}}}}})";
    EXPECT_FALSE(ParseInplayOddsDevig(suspended_home, "1").valid)
        << "home 腿暂停 → fail-closed, 宁可 NaN 不喂 stale 赔率";
}
