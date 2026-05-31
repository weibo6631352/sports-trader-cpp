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
