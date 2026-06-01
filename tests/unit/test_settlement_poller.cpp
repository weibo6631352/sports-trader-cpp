// tests/unit/test_settlement_poller.cpp — SettlementStore + SettlementPoller 单测 (M2)
//
// Owner: 老雷 (GM) — fake fetcher 注入, 无网络
// 覆盖: store swap/Get/Size / poller fetch+parse+publish / 已结算 cid 不再轮询

#include <string>
#include <unordered_map>

#include <gtest/gtest.h>

#include "stcpp/data/settlement_poller.hpp"
#include "stcpp/data/settlement_store.hpp"

using stcpp::data::SettlementMap;
using stcpp::data::SettlementPoller;
using stcpp::data::SettlementRecord;
using stcpp::data::SettlementStore;

namespace {
const std::string kActiveJson =
    R"({"closed":false,"accepting_orders":true,"accepting_order_timestamp":"2025-07-02T22:27:17Z",)"
    R"("tokens":[{"token_id":"111","outcome":"Yes","winner":false},)"
    R"({"token_id":"222","outcome":"No","winner":false}]})";
const std::string kSettledJson =
    R"({"closed":true,"accepting_orders":false,"accepting_order_timestamp":"2026-02-27T23:33:23Z",)"
    R"("tokens":[{"token_id":"333","outcome":"Yes","winner":true},)"
    R"({"token_id":"444","outcome":"No","winner":false}]})";
}  // namespace

// ST-01: SettlementStore swap/Get/Size
TEST(SettlementStore, ST01_PublishGet) {
    SettlementStore s;
    EXPECT_EQ(s.Size(), 0u);
    EXPECT_FALSE(s.Get("0xabc").has_value());
    auto m = std::make_shared<SettlementMap>();
    SettlementRecord r;
    r.condition_id = "0xabc";
    r.closed = true;
    r.settlement_value = 1;
    (*m)["0xabc"] = r;
    s.Publish(m);
    EXPECT_EQ(s.Size(), 1u);
    auto got = s.Get("0xabc");
    ASSERT_TRUE(got.has_value());
    EXPECT_TRUE(got->closed);
    EXPECT_EQ(got->settlement_value, 1);
}

// ST-02: poller fetch+parse+publish — 活跃 + 已结算两 cid
TEST(SettlementPoller, ST02_PollPublish) {
    SettlementStore store;
    std::unordered_map<std::string, int> fetch_count;
    auto fetcher = [&](const std::string& cid) -> std::string {
        ++fetch_count[cid];
        if (cid == "0xactive") return kActiveJson;
        if (cid == "0xsettled") return kSettledJson;
        return "";
    };
    SettlementPoller poller(store, {"0xactive", "0xsettled"}, fetcher);
    poller.PollAllOnce();

    EXPECT_EQ(store.Size(), 2u);
    auto a = store.Get("0xactive");
    ASSERT_TRUE(a.has_value());
    EXPECT_FALSE(a->closed);
    EXPECT_EQ(a->resolution_status(), 0);  // Open
    auto se = store.Get("0xsettled");
    ASSERT_TRUE(se.has_value());
    EXPECT_TRUE(se->closed);
    EXPECT_EQ(se->settlement_value, 1) << "YES winner → 1";
    EXPECT_EQ(se->resolution_status(), 2);  // Resolved
}

// ST-03: 已结算 cid 第二轮不再 fetch (acc_ 携带, 省请求 + 守 ToS)
TEST(SettlementPoller, ST03_SettledNotRepolled) {
    SettlementStore store;
    std::unordered_map<std::string, int> fetch_count;
    auto fetcher = [&](const std::string& cid) -> std::string {
        ++fetch_count[cid];
        return (cid == "0xsettled") ? kSettledJson : kActiveJson;
    };
    SettlementPoller poller(store, {"0xactive", "0xsettled"}, fetcher);
    poller.PollAllOnce();  // 轮1: 两个都 fetch
    poller.PollAllOnce();  // 轮2: 0xsettled 已 closed, 跳过
    EXPECT_EQ(fetch_count["0xsettled"], 1) << "已结算 cid 只 fetch 一次 (acc_ 携带, 省请求)";
    EXPECT_EQ(fetch_count["0xactive"], 2) << "活跃 cid 每轮 fetch";
    EXPECT_EQ(poller.poll_count(), 2u);
}

// ST-04: 空 fetch (拉取失败) → 跳过该 cid, 不崩
TEST(SettlementPoller, ST04_EmptyFetchSkip) {
    SettlementStore store;
    auto fetcher = [](const std::string&) -> std::string { return ""; };
    SettlementPoller poller(store, {"0xabc"}, fetcher);
    poller.PollAllOnce();
    EXPECT_EQ(store.Size(), 0u) << "空 fetch → 不写记录";
    EXPECT_EQ(poller.poll_count(), 1u);
}

// ST-05: SetConditionIds 退订盘 → 从 acc_/published map prune (防无界增长, 2026-06-01 老板「越来越多」)
TEST(SettlementPoller, ST05_PruneOnConditionUpdate) {
    SettlementStore store;
    auto fetcher = [](const std::string& cid) -> std::string {
        return (cid == "0xsettled") ? kSettledJson : kActiveJson;
    };
    SettlementPoller poller(store, {"0xactive", "0xsettled"}, fetcher);
    poller.PollAllOnce();
    EXPECT_EQ(store.Size(), 2u);  // 两个都在累积/发布
    // 动态更新 condition 集: 只留 0xactive (0xsettled 退订, 模拟比赛结束掉出 discovery)
    poller.SetConditionIds({"0xactive"});
    poller.PollAllOnce();
    EXPECT_EQ(store.Size(), 1u) << "退订的 0xsettled 应从 acc_/published map prune (防无界增长)";
    EXPECT_TRUE(store.Get("0xactive").has_value());
    EXPECT_FALSE(store.Get("0xsettled").has_value()) << "退订盘已 prune, 不再发布";
}
