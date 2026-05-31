// tests/unit/test_settlement_record.cpp — clob /markets JSON → SettlementRecord 解析单测 (M2)
//
// Owner: 老雷 (GM) — 用小余/小冯实测 payload 字段构造
// 覆盖: 活跃/已结算市场解析 / winner→settlement_value (YES-canonical) / ISO时戳 / resolution_status

#include <gtest/gtest.h>

#include "stcpp/data/settlement_record.hpp"

using stcpp::data::ParseMarketJson;
using stcpp::data::SettlementRecord;
using stcpp::data::settlement_detail::ParseIso8601ToUnixNs;

// SR-01: 活跃市场 — closed=false, accepting=true, winner 全 false → settlement -1, status Open
TEST(SettlementRecord, SR01_ActiveMarket) {
    // 实测字段 (小余: Congo DR World Cup 活跃市场)
    const std::string j =
        R"({"condition_id":"0xcd83","closed":false,"accepting_orders":true,)"
        R"("accepting_order_timestamp":"2025-07-02T22:27:17Z","end_date_iso":"2026-07-20T00:00:00Z",)"
        R"("tokens":[{"token_id":"111","outcome":"Yes","price":0.5,"winner":false},)"
        R"({"token_id":"222","outcome":"No","price":0.5,"winner":false}]})";
    const auto r = ParseMarketJson(j, "0xcd83");
    EXPECT_TRUE(r.valid);
    EXPECT_FALSE(r.closed);
    EXPECT_TRUE(r.accepting_orders);
    EXPECT_EQ(r.settlement_value, -1) << "无 winner → settlement 未知";
    EXPECT_EQ(r.resolution_status(), 0) << "Open";
    EXPECT_GT(r.accepting_order_ts_ns, 0) << "ISO 时戳应解析 (R-20 data_source 锚)";
    EXPECT_GT(r.end_date_ts_ns, 0);
}

// SR-02: 已结算市场 — closed=true, NO 赢 (winner=true on No) → settlement_value=0, status Resolved
TEST(SettlementRecord, SR02_SettledMarket_NoWon) {
    // 实测字段 (小余: Katana FDV 已结算, NO 赢)
    const std::string j =
        R"({"condition_id":"0x8ccc","closed":true,"accepting_orders":false,)"
        R"("accepting_order_timestamp":"2026-02-27T23:33:23Z",)"
        R"("tokens":[{"token_id":"333","outcome":"Yes","price":0,"winner":false},)"
        R"({"token_id":"444","outcome":"No","price":1,"winner":true}]})";
    const auto r = ParseMarketJson(j, "0x8ccc");
    EXPECT_TRUE(r.closed);
    EXPECT_FALSE(r.accepting_orders);
    EXPECT_EQ(r.settlement_value, 0) << "NO 赢 → YES 结算 0 (YES-canonical)";
    EXPECT_EQ(r.winner_token_id, "444");
    EXPECT_EQ(r.resolution_status(), 2) << "Resolved";
}

// SR-03: YES 赢 → settlement_value=1
TEST(SettlementRecord, SR03_SettledMarket_YesWon) {
    const std::string j =
        R"({"condition_id":"0xabc","closed":true,"accepting_orders":false,)"
        R"("tokens":[{"token_id":"555","outcome":"Yes","price":1,"winner":true},)"
        R"({"token_id":"666","outcome":"No","price":0,"winner":false}]})";
    const auto r = ParseMarketJson(j, "0xabc");
    EXPECT_EQ(r.settlement_value, 1) << "YES 赢 → 结算 1";
    EXPECT_EQ(r.winner_token_id, "555");
}

// SR-04: resolution_status Resolving — 停接单但未链上结算 (accepting=false, closed=false)
TEST(SettlementRecord, SR04_Resolving) {
    const std::string j = R"({"condition_id":"0xdef","closed":false,"accepting_orders":false,"tokens":[]})";
    const auto r = ParseMarketJson(j, "0xdef");
    EXPECT_EQ(r.resolution_status(), 1) << "停接单未结算 = Resolving";
    EXPECT_EQ(r.settlement_value, -1);
}

// SR-05: ISO8601 → unix ns 正确性
TEST(SettlementRecord, SR05_Iso8601Parse) {
    // 2026-02-27T23:33:23Z = 1772235203 unix (UTC)
    const std::int64_t ns = ParseIso8601ToUnixNs("2026-02-27T23:33:23Z");
    EXPECT_EQ(ns, 1772235203LL * 1'000'000'000LL);
    // 退化: 太短/脏 → 0
    EXPECT_EQ(ParseIso8601ToUnixNs("bad"), 0);
    EXPECT_EQ(ParseIso8601ToUnixNs(""), 0);
}
