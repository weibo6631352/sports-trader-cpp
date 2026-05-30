// tests/unit/test_coverage_metrics.cpp — 覆盖率/识别率 metric 单测
//
// Owner: 小卢 (senior-ic-pool, debug_api owner)
// Date:  2026-05-30
// ADR:   ADR-038 (观测 API)  §4.4 低基数 metric
//
// 测试覆盖:
//   TC-01: 盘口类型识别率 — 全部已知类型 → recognized=N, unknown=0
//   TC-02: 盘口类型识别率 — 混合 (known + unknown) → 分桶正确
//   TC-03: 盘口类型识别率 — 全部 unknown (outright 场景) → recognized=0, unknown=N
//   TC-04: 盘口类型识别率 — 空 catalog → 两项均 0
//   TC-05: 市场覆盖 — markets_discovered_total = catalog 条目数
//   TC-06: 市场覆盖 — 空 catalog → discovered=0
//   TC-07: 直播员/比分匹配率 — score_store 全匹配 → matched=N
//   TC-08: 直播员/比分匹配率 — 部分匹配 → matched=k < N
//   TC-09: 直播员/比分匹配率 — score_store 为空 → matched=0
//   TC-10: 直播员/比分匹配率 — score_store nullptr → matched=0 (无崩溃)
//   TC-11: MetricsSnapshot 默认构造 — 新字段均为 0 (结构正确)
//   TC-12: 派生率不变量 — recognized + unknown == discovered (catalog 一致性)
//
// 设计约束:
//   R-12: 测试全程只读, 无锁 > 100us
//   R-20: 测试不依赖实时时钟 (EventScore ts 字段手动设置)
//   低基数: 不用 per-market/per-order label, 只验证总量计数
//
// 不依赖网络/文件 IO — 全内存合成数据

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "src/stcpp/debug_api/state_provider.hpp"

// 包含 ScoreSnapshotStore (用于 score_store 注入)
#include "stcpp/data/score_snapshot_store.hpp"

// 包含 RealStateProvider (被测单元; 纯 header-only 实现)
// 注意: real_state_provider.hpp 依赖的 hub/snap 均可 nullptr/stub
#include "src/stcpp/debug_api/real_state_provider.hpp"

// OrderBookSnapshotHub stub (构造不需要 CLOB 连接)
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"

namespace stcpp::debug_api {
namespace {

// ---------------------------------------------------------------------------
// 辅助: 构造最小 MarketInfoMap (仅 sports_market_type + event_id 有意义)
// ---------------------------------------------------------------------------
MarketInfo make_mi(const std::string& condition_id, const std::string& smt,
                   const std::string& event_id = "") {
    MarketInfo mi;
    mi.found = true;
    mi.condition_id = condition_id;
    mi.market_id = condition_id;
    mi.sports_market_type = smt;
    mi.event_id = event_id;
    mi.active = true;
    return mi;
}

// 构造 ScoreSnapshotStore 并 Publish 一批 event_id
std::unique_ptr<data::ScoreSnapshotStore> make_score_store(const std::vector<std::string>& event_ids) {
    auto store = std::make_unique<data::ScoreSnapshotStore>();
    auto map = std::make_shared<data::ScoreMap>();
    for (const auto& eid : event_ids) {
        EventScore es;
        es.found = true;
        es.event_id = eid;
        es.sport = "basketball";
        es.status = "inplay";
        // 4ts: 非零合法值 (R-20: event_ts <= data_source_ts <= ingestion_ts)
        es.ts.event_ts_ns = 1'000'000'000LL;
        es.ts.data_source_ts_ns = 1'100'000'000LL;
        es.ts.ingestion_ts_ns = 1'200'000'000LL;
        es.ts.as_of_ts_ns = 1'300'000'000LL;
        (*map)[eid] = es;
    }
    store->Publish(std::move(map));
    return store;
}

// 构造 RealStateProvider (最小构造; 不接 WSS/paper/ledger)
// catalog 和 score_store 通过 set_* 注入
class CoverageMetricsFixture : public ::testing::Test {
protected:
    polymarket::clob_wss::OrderBookSnapshotHub hub_;
    // RealStateProvider 内部 snap/ledger/quote 均 nullptr (合规: 对应指标降级 0)
    std::unique_ptr<RealStateProvider> make_provider(const MarketInfoMap& catalog,
                                                     const data::ScoreSnapshotStore* score_store = nullptr) {
        MarketTokenMap token_map;
        // 从 catalog 中为每个 condition_id 构造伪 token pair (不影响 coverage metric)
        for (const auto& [cid, _] : catalog) {
            token_map[cid] = {cid + "_tok0", cid + "_tok1"};
        }
        auto p = std::make_unique<RealStateProvider>(hub_,
                                                     /*snap=*/nullptr,
                                                     /*score_store=*/score_store,
                                                     /*token_map=*/token_map,
                                                     /*risk_cfg=*/risk::RiskConfig{},
                                                     /*mode=*/ExecMode::Paper,
                                                     /*ledger_hub=*/nullptr,
                                                     /*quote_hub=*/nullptr);
        p->set_market_catalog(MarketInfoMap(catalog));
        return p;
    }
};

// ---------------------------------------------------------------------------
// TC-01: 全部已知类型 → recognized=N, unknown=0
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC01_AllRecognized) {
    MarketInfoMap catalog;
    catalog["cid-ml"] = make_mi("cid-ml", "moneyline");
    catalog["cid-sp"] = make_mi("cid-sp", "spread");
    catalog["cid-to"] = make_mi("cid-to", "totals");
    catalog["cid-pr"] = make_mi("cid-pr", "prop");
    catalog["cid-se"] = make_mi("cid-se", "series");

    auto p = make_provider(catalog);
    const MetricsSnapshot m = p->metrics();

    EXPECT_EQ(m.market_type_recognized_total, 5);
    EXPECT_EQ(m.market_type_unknown_total, 0);
    EXPECT_EQ(m.markets_discovered_total, 5);
}

// ---------------------------------------------------------------------------
// TC-02: 混合 (known + unknown) → 分桶正确
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC02_Mixed) {
    MarketInfoMap catalog;
    catalog["cid-ml"] = make_mi("cid-ml", "moneyline");
    catalog["cid-ot"] = make_mi("cid-ot", "outright");
    catalog["cid-uk"] = make_mi("cid-uk", "unknown");  // 显式 "unknown"
    catalog["cid-em"] = make_mi("cid-em", "");         // 空字符串

    auto p = make_provider(catalog);
    const MetricsSnapshot m = p->metrics();

    EXPECT_EQ(m.market_type_recognized_total, 2);  // moneyline + outright
    EXPECT_EQ(m.market_type_unknown_total, 2);     // "unknown" + ""
    EXPECT_EQ(m.markets_discovered_total, 4);
}

// ---------------------------------------------------------------------------
// TC-03: 全部 unknown (outright/futures 场景) → recognized=0, unknown=N
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC03_AllUnknown) {
    MarketInfoMap catalog;
    catalog["cid-a"] = make_mi("cid-a", "unknown");
    catalog["cid-b"] = make_mi("cid-b", "");
    catalog["cid-c"] = make_mi("cid-c", "unknown");

    auto p = make_provider(catalog);
    const MetricsSnapshot m = p->metrics();

    EXPECT_EQ(m.market_type_recognized_total, 0);
    EXPECT_EQ(m.market_type_unknown_total, 3);
    EXPECT_EQ(m.markets_discovered_total, 3);
}

// ---------------------------------------------------------------------------
// TC-04: 空 catalog → 全部 0
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC04_EmptyCatalog) {
    auto p = make_provider({});
    const MetricsSnapshot m = p->metrics();

    EXPECT_EQ(m.market_type_recognized_total, 0);
    EXPECT_EQ(m.market_type_unknown_total, 0);
    EXPECT_EQ(m.markets_discovered_total, 0);
    EXPECT_EQ(m.score_matched_total, 0);
}

// ---------------------------------------------------------------------------
// TC-05: 市场覆盖 — markets_discovered_total = catalog 条目数
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC05_DiscoveredEqualsOatalogSize) {
    MarketInfoMap catalog;
    for (int i = 0; i < 7; ++i) {
        const std::string cid = "cid-" + std::to_string(i);
        catalog[cid] = make_mi(cid, "moneyline");
    }

    auto p = make_provider(catalog);
    const MetricsSnapshot m = p->metrics();

    EXPECT_EQ(m.markets_discovered_total, static_cast<std::int64_t>(catalog.size()));
    EXPECT_EQ(m.markets_discovered_total, 7);
}

// ---------------------------------------------------------------------------
// TC-06: 空 catalog → discovered=0
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC06_DiscoveredEmpty) {
    auto p = make_provider({});
    EXPECT_EQ(p->metrics().markets_discovered_total, 0);
}

// ---------------------------------------------------------------------------
// TC-07: 直播员/比分匹配率 — score_store 全匹配 → matched=N
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC07_ScoreAllMatched) {
    MarketInfoMap catalog;
    catalog["cid-1"] = make_mi("cid-1", "moneyline", "event-A");
    catalog["cid-2"] = make_mi("cid-2", "spread", "event-B");
    catalog["cid-3"] = make_mi("cid-3", "totals", "event-A");  // 同一 event, 两个盘口

    auto store = make_score_store({"event-A", "event-B"});
    auto p = make_provider(catalog, store.get());
    const MetricsSnapshot m = p->metrics();

    // cid-1 → event-A (found), cid-2 → event-B (found), cid-3 → event-A (found)
    EXPECT_EQ(m.score_matched_total, 3);
    EXPECT_EQ(m.markets_discovered_total, 3);
}

// ---------------------------------------------------------------------------
// TC-08: 直播员/比分匹配率 — 部分匹配 → matched=k < N
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC08_ScorePartialMatch) {
    MarketInfoMap catalog;
    catalog["cid-1"] = make_mi("cid-1", "moneyline", "event-X");
    catalog["cid-2"] = make_mi("cid-2", "outright", "event-Y");  // outright → 无 inplay
    catalog["cid-3"] = make_mi("cid-3", "totals", "event-X");
    catalog["cid-4"] = make_mi("cid-4", "spread", "event-Z");  // event-Z 无比分

    auto store = make_score_store({"event-X"});  // 只有 event-X 有比分
    auto p = make_provider(catalog, store.get());
    const MetricsSnapshot m = p->metrics();

    // cid-1 → event-X (found), cid-2 → event-Y (miss), cid-3 → event-X (found), cid-4 → event-Z (miss)
    EXPECT_EQ(m.score_matched_total, 2);
    EXPECT_EQ(m.markets_discovered_total, 4);
}

// ---------------------------------------------------------------------------
// TC-09: 直播员/比分匹配率 — score_store 为空 (Publish 前) → matched=0
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC09_ScoreStoreEmpty) {
    MarketInfoMap catalog;
    catalog["cid-1"] = make_mi("cid-1", "moneyline", "event-A");
    catalog["cid-2"] = make_mi("cid-2", "spread", "event-B");

    // 构造空 store (不 Publish)
    auto store = std::make_unique<data::ScoreSnapshotStore>();
    auto p = make_provider(catalog, store.get());
    const MetricsSnapshot m = p->metrics();

    EXPECT_EQ(m.score_matched_total, 0);
}

// ---------------------------------------------------------------------------
// TC-10: 直播员/比分匹配率 — score_store=nullptr → matched=0 (无崩溃)
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC10_ScoreStoreNullptr) {
    MarketInfoMap catalog;
    catalog["cid-1"] = make_mi("cid-1", "moneyline", "event-A");

    // score_store = nullptr 路径
    auto p = make_provider(catalog, /*score_store=*/nullptr);
    const MetricsSnapshot m = p->metrics();

    EXPECT_EQ(m.score_matched_total, 0);
    // 其他字段不受影响
    EXPECT_EQ(m.market_type_recognized_total, 1);
    EXPECT_EQ(m.market_type_unknown_total, 0);
    EXPECT_EQ(m.markets_discovered_total, 1);
}

// ---------------------------------------------------------------------------
// TC-11: MetricsSnapshot 默认构造 — 新字段均为 0
// ---------------------------------------------------------------------------
TEST(CoverageMetricsStruct, TC11_DefaultZero) {
    MetricsSnapshot m;
    EXPECT_EQ(m.market_type_recognized_total, 0);
    EXPECT_EQ(m.market_type_unknown_total, 0);
    EXPECT_EQ(m.markets_discovered_total, 0);
    EXPECT_EQ(m.score_matched_total, 0);
}

// ---------------------------------------------------------------------------
// TC-12: 派生率不变量 — recognized + unknown == discovered (catalog 一致性)
// ---------------------------------------------------------------------------
TEST_F(CoverageMetricsFixture, TC12_Invariant_RecognizedPlusUnknownEqualsDiscovered) {
    MarketInfoMap catalog;
    catalog["cid-ml"] = make_mi("cid-ml", "moneyline", "ev-1");
    catalog["cid-ot"] = make_mi("cid-ot", "outright", "ev-2");
    catalog["cid-uk"] = make_mi("cid-uk", "unknown", "ev-3");
    catalog["cid-em"] = make_mi("cid-em", "", "ev-4");
    catalog["cid-to"] = make_mi("cid-to", "totals", "ev-5");

    auto p = make_provider(catalog);
    const MetricsSnapshot m = p->metrics();

    EXPECT_EQ(m.market_type_recognized_total + m.market_type_unknown_total, m.markets_discovered_total);
}

}  // namespace
}  // namespace stcpp::debug_api
