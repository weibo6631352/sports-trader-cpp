// tests/unit/test_live_section_classifier.cpp — LiveSection 5 enum 全覆盖
// 落: 小程 P0-01 spec v0.1 §LiveSection (老李 GM 逆向反推)

#include <cstdint>

#include <gtest/gtest.h>

#include "stcpp/strategy/live_section_classifier.hpp"

namespace {

using stcpp::strategy::classify;
using stcpp::strategy::GameState;
using stcpp::strategy::LiveSection;
using stcpp::strategy::SIX_HOURS_NS;

constexpr std::int64_t NOW = 1'700'000'000'000'000'000LL;

TEST(LiveSection, Live_LiveTrueEndedFalse) {
    GameState g;
    g.live = true;
    g.ended = false;
    g.delayed = false;
    g.kickoff_ts_ns = NOW - 60 * 60 * 1'000'000'000LL;  // 已开 1h
    EXPECT_EQ(classify(g, NOW), LiveSection::Live);
}

TEST(LiveSection, Soon_KickoffWithin6h) {
    GameState g;
    g.live = false;
    g.ended = false;
    g.delayed = false;
    // kickoff 3h 后
    g.kickoff_ts_ns = NOW + 3 * 60 * 60 * 1'000'000'000LL;
    EXPECT_EQ(classify(g, NOW), LiveSection::Soon);

    // 边界: kickoff - now = 6h - 1ns → Soon (严格 <)
    g.kickoff_ts_ns = NOW + SIX_HOURS_NS - 1;
    EXPECT_EQ(classify(g, NOW), LiveSection::Soon);
}

TEST(LiveSection, Delayed_OverrideLive) {
    GameState g;
    g.delayed = true;
    g.live = true;  // delayed 凌驾 (雨延 transient)
    g.ended = false;
    g.kickoff_ts_ns = NOW - 1'000'000'000;
    EXPECT_EQ(classify(g, NOW), LiveSection::Delayed);

    // delayed + 未开赛
    g.live = false;
    g.kickoff_ts_ns = NOW + 2 * 60 * 60 * 1'000'000'000LL;
    EXPECT_EQ(classify(g, NOW), LiveSection::Delayed);
}

TEST(LiveSection, Closed_EndedTrue) {
    GameState g;
    g.ended = true;
    g.live = false;
    g.delayed = false;
    g.kickoff_ts_ns = NOW - 4 * 60 * 60 * 1'000'000'000LL;
    EXPECT_EQ(classify(g, NOW), LiveSection::Closed);

    // ended 凌驾 live (赛后短暂 live=true 边界, ended 仍生效)
    g.live = true;
    EXPECT_EQ(classify(g, NOW), LiveSection::Closed);
}

TEST(LiveSection, Future_KickoffBeyond6h) {
    GameState g;
    g.live = false;
    g.ended = false;
    g.delayed = false;
    // kickoff = now + 6h (恰 6h → Future, spec ">= 6h")
    g.kickoff_ts_ns = NOW + SIX_HOURS_NS;
    EXPECT_EQ(classify(g, NOW), LiveSection::Future);

    // kickoff = now + 12h → Future
    g.kickoff_ts_ns = NOW + 12 * 60 * 60 * 1'000'000'000LL;
    EXPECT_EQ(classify(g, NOW), LiveSection::Future);
}

TEST(LiveSection, ToString_5Enum) {
    EXPECT_EQ(stcpp::strategy::to_string(LiveSection::Live), "Live");
    EXPECT_EQ(stcpp::strategy::to_string(LiveSection::Soon), "Soon");
    EXPECT_EQ(stcpp::strategy::to_string(LiveSection::Delayed), "Delayed");
    EXPECT_EQ(stcpp::strategy::to_string(LiveSection::Closed), "Closed");
    EXPECT_EQ(stcpp::strategy::to_string(LiveSection::Future), "Future");
}

// 边界: 全 false flag + kickoff 已过 (源未刷新) → Future 兜底
TEST(LiveSection, Edge_AllFalse_KickoffPast) {
    GameState g;
    g.live = false;
    g.ended = false;
    g.delayed = false;
    g.kickoff_ts_ns = NOW - 1'000'000'000;  // 已过 1s 但源未标 live
    EXPECT_EQ(classify(g, NOW), LiveSection::Future);
}

}  // namespace
