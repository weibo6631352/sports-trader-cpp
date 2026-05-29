// tests/unit/test_orderbook_adapter.cpp
//
// Owner: 小冯 (#34)  spec: 小田 (#24) feature-store contract
// Wave ADR-037 / Sprint-2 — OrderBookAdapter v0.1 单测
//
// 8 test cases:
//   T1: snapshot + 4-ts chain (R-20)
//   T2: delta rejected before snapshot (P-05)
//   T3: delta accepted after snapshot
//   T4: R-20 违规 event → 拒绝 + ts_violations counter
//   T5: ResetToken + state 回 kWaitingSnapshot
//   T6: InjectMetadata 字段填充
//   T7: 诚实命名 — kBookDepthLevels SSOT (=5, 与 kOrderBookLevels 一致)
//   T8: FeatureStoreBookRow.ts_chain_ok() — 通过 4-ts 自检
//
// v0.1 限制文档 (老高评审要求):
//   bid[1..4] / ask[1..4] = NaN (单档合成, L2 多档待 W10 simdjson @老李)
//   microprice / imbalance = NaN (size_usdc 未知, v0.1 无 L2)
//

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_adapter.hpp"
#include "stcpp/polymarket/wss/wss_event.hpp"

using namespace stcpp::polymarket::clob_wss;
using namespace stcpp::polymarket::wss;
using namespace stcpp::microstructure;
using namespace stcpp::data::feature_store;

namespace {

constexpr std::int64_t kMsToNs = 1'000'000LL;

// Build a WssEvent kBook with given timestamp_ms and mid_bps
WssEvent MakeBookEvent(std::int64_t timestamp_ms, std::uint32_t mid_bps,
                       std::uint32_t tick_size_bps = 100u,
                       std::int64_t recv_ts_ns = 0) {
    WssEvent ev;
    ev.topic = SubTopic::kBook;
    auto& b = ev.payload.book;
    b.topic        = SubTopic::kBook;
    b.is_snapshot  = 1;
    b.mid_bps      = mid_bps;
    b.tick_size_bps = tick_size_bps;
    b.spread_bps   = static_cast<std::uint32_t>(tick_size_bps);  // 1 tick spread
    // R-20: data_source_ts = timestamp_ms * 1e6
    std::int64_t ds_ns = timestamp_ms * kMsToNs;
    std::int64_t ingest = (recv_ts_ns > ds_ns) ? recv_ts_ns : ds_ns + 1'000'000LL;
    b.ts.event_ts_ns       = ds_ns;
    b.ts.data_source_ts_ns = ds_ns;
    b.ts.ingestion_ts_ns   = ingest;
    b.ts.as_of_ts_ns       = ingest;
    b.ts.ds_origin         = DataSourceTsOrigin::kUpstreamPayload;
    return ev;
}

// Build a WssEvent kPriceChange
WssEvent MakePriceChangeEvent(std::int64_t timestamp_ms, std::uint32_t mid_bps,
                               std::int64_t recv_ts_ns = 0) {
    WssEvent ev;
    ev.topic = SubTopic::kPriceChange;
    auto& b = ev.payload.book;
    b.topic        = SubTopic::kPriceChange;
    b.is_snapshot  = 0;
    b.mid_bps      = mid_bps;
    std::int64_t ds_ns = timestamp_ms * kMsToNs;
    std::int64_t ingest = (recv_ts_ns > ds_ns) ? recv_ts_ns : ds_ns + 1'000'000LL;
    b.ts.event_ts_ns       = ds_ns;
    b.ts.data_source_ts_ns = ds_ns;
    b.ts.ingestion_ts_ns   = ingest;
    b.ts.as_of_ts_ns       = ingest;
    b.ts.ds_origin         = DataSourceTsOrigin::kUpstreamPayload;
    return ev;
}

}  // namespace

// ============================================================================
// T1: snapshot → 4-ts chain (R-20) + snapshot state transitions
// ============================================================================
TEST(OrderBookAdapter, T1_SnapshotFourTs) {
    OrderBookAdapter adapter;

    const std::string token_id = "tok_yes_001";
    constexpr std::int64_t kTsMs = 1'748'400'000'000LL;
    constexpr std::int64_t kDsNs = kTsMs * kMsToNs;
    constexpr std::int64_t kRecv = kDsNs + 50'000'000LL;

    WssEvent ev = MakeBookEvent(kTsMs, 5500u, 100u, kRecv);
    OrderBookSnapshot snap;
    FeatureStoreBookRow row;

    bool ok = adapter.OnEvent(ev, token_id, kRecv, snap, row);
    ASSERT_TRUE(ok) << "snapshot event should be accepted";

    // R-20 4-ts monotonic chain on snap
    EXPECT_LE(snap.ts.event_ts_ns,       snap.ts.data_source_ts_ns) << "event_ts <= data_source_ts";
    EXPECT_LE(snap.ts.data_source_ts_ns, snap.ts.ingestion_ts_ns)   << "ds_ts <= ingestion_ts";
    EXPECT_LE(snap.ts.ingestion_ts_ns,   snap.ts.as_of_ts_ns)       << "ingestion_ts <= as_of_ts";

    // data_source_ts = timestamp_ms * 1e6 (R-20 P-03)
    EXPECT_EQ(snap.ts.data_source_ts_ns, kDsNs);
    EXPECT_EQ(snap.ts.ingestion_ts_ns,   kRecv);

    // Snapshot state should be kLive
    EXPECT_EQ(adapter.GetSnapshotState(token_id), AdapterSnapshotState::kLive);
    EXPECT_EQ(adapter.snapshots_received(), 1u);

    // Bid/ask L1 should be valid finite values in (0,1)
    EXPECT_GT(snap.bid[0].price, 0.0);
    EXPECT_LT(snap.bid[0].price, 1.0);
    EXPECT_GT(snap.ask[0].price, 0.0);
    EXPECT_LT(snap.ask[0].price, 1.0);
    EXPECT_GT(snap.ask[0].price, snap.bid[0].price) << "ask must be > bid";

    // Levels 1..4 should be NaN (v0.1 single-level synthesis)
    for (std::size_t i = 1; i < kBookDepthLevels; ++i) {
        EXPECT_TRUE(std::isnan(snap.bid[i].price))
            << "bid[" << i << "].price must be NaN (v0.1 single-level)";
        EXPECT_TRUE(std::isnan(snap.ask[i].price))
            << "ask[" << i << "].price must be NaN (v0.1 single-level)";
    }
}

// ============================================================================
// T2: delta rejected before snapshot (P-05 state machine)
// ============================================================================
TEST(OrderBookAdapter, T2_DeltaRejectedBeforeSnapshot) {
    OrderBookAdapter adapter;

    const std::string token_id = "tok_no_002";
    constexpr std::int64_t kTsMs = 1'748'400'001'000LL;

    // Send price_change BEFORE any snapshot → must be rejected
    WssEvent ev = MakePriceChangeEvent(kTsMs, 5200u);
    OrderBookSnapshot snap;
    FeatureStoreBookRow row;

    bool ok = adapter.OnEvent(ev, token_id, kTsMs * kMsToNs + 1'000'000LL, snap, row);
    EXPECT_FALSE(ok) << "price_change before snapshot must be rejected (P-05)";
    EXPECT_EQ(adapter.deltas_rejected_early(), 1u);
    EXPECT_EQ(adapter.GetSnapshotState(token_id), AdapterSnapshotState::kWaitingSnapshot);
}

// ============================================================================
// T3: delta accepted after snapshot
// ============================================================================
TEST(OrderBookAdapter, T3_DeltaAcceptedAfterSnapshot) {
    OrderBookAdapter adapter;
    const std::string token_id = "tok_yes_003";

    // First send snapshot
    constexpr std::int64_t kTs1Ms = 1'748'400'002'000LL;
    WssEvent snap_ev = MakeBookEvent(kTs1Ms, 5500u, 100u, kTs1Ms * kMsToNs + 1'000'000LL);
    OrderBookSnapshot snap;
    FeatureStoreBookRow row;
    ASSERT_TRUE(adapter.OnEvent(snap_ev, token_id, kTs1Ms * kMsToNs + 1'000'000LL, snap, row));

    // Now send price_change (later timestamp)
    constexpr std::int64_t kTs2Ms = kTs1Ms + 500LL;  // 500 ms later
    WssEvent delta_ev = MakePriceChangeEvent(kTs2Ms, 5600u, kTs2Ms * kMsToNs + 2'000'000LL);
    OrderBookSnapshot snap2;
    FeatureStoreBookRow row2;
    bool ok = adapter.OnEvent(delta_ev, token_id, kTs2Ms * kMsToNs + 2'000'000LL, snap2, row2);
    EXPECT_TRUE(ok) << "price_change after snapshot must be accepted";
    EXPECT_EQ(adapter.deltas_accepted(), 1u);

    // The mid should reflect the delta (5600 bps = 0.56 mid)
    EXPECT_GT(snap2.bid[0].price, 0.0);
    EXPECT_LT(snap2.ask[0].price, 1.0);

    // 4-ts of the updated snapshot
    EXPECT_EQ(snap2.ts.data_source_ts_ns, kTs2Ms * kMsToNs);
    EXPECT_LE(snap2.ts.data_source_ts_ns, snap2.ts.ingestion_ts_ns);
}

// ============================================================================
// T4: R-20 violation → event rejected + ts_violations counter
// ============================================================================
TEST(OrderBookAdapter, T4_TsViolationRejected) {
    OrderBookAdapter adapter;
    const std::string token_id = "tok_ts_004";

    // Create a WssEvent with bad 4-ts (data_source_ts < event_ts → violation)
    WssEvent ev;
    ev.topic = SubTopic::kBook;
    auto& b = ev.payload.book;
    b.topic       = SubTopic::kBook;
    b.is_snapshot = 1;
    b.mid_bps     = 5000u;
    b.ts.event_ts_ns       = 2'000'000'000'000LL;  // future event_ts
    b.ts.data_source_ts_ns = 1'000'000'000'000LL;  // data_source_ts < event_ts → VIOLATION
    b.ts.ingestion_ts_ns   = 3'000'000'000'000LL;
    b.ts.as_of_ts_ns       = 3'000'000'000'000LL;

    OrderBookSnapshot snap;
    FeatureStoreBookRow row;
    bool ok = adapter.OnEvent(ev, token_id, 3'000'000'000'000LL, snap, row);
    EXPECT_FALSE(ok) << "R-20 ts violation must reject the event";
    EXPECT_EQ(adapter.ts_violations(), 1u) << "ts_violations counter must increment";
    EXPECT_EQ(adapter.GetSnapshotState(token_id), AdapterSnapshotState::kWaitingSnapshot)
        << "token must still be in WaitingSnapshot after rejected event";
}

// ============================================================================
// T5: ResetToken clears state → subsequent delta rejected again
// ============================================================================
TEST(OrderBookAdapter, T5_ResetToken) {
    OrderBookAdapter adapter;
    const std::string token_id = "tok_reset_005";

    // Establish snapshot
    constexpr std::int64_t kTs1Ms = 1'748'400'005'000LL;
    WssEvent snap_ev = MakeBookEvent(kTs1Ms, 5000u, 100u);
    OrderBookSnapshot snap;
    FeatureStoreBookRow row;
    ASSERT_TRUE(adapter.OnEvent(snap_ev, token_id, kTs1Ms * kMsToNs + 1'000'000LL, snap, row));
    EXPECT_EQ(adapter.GetSnapshotState(token_id), AdapterSnapshotState::kLive);

    // Reset token
    adapter.ResetToken(token_id);
    EXPECT_EQ(adapter.GetSnapshotState(token_id), AdapterSnapshotState::kWaitingSnapshot)
        << "after ResetToken, state must be kWaitingSnapshot";

    // Delta after reset should be rejected
    WssEvent delta_ev = MakePriceChangeEvent(kTs1Ms + 1000LL, 5100u);
    OrderBookSnapshot snap2;
    FeatureStoreBookRow row2;
    bool ok = adapter.OnEvent(delta_ev, token_id, (kTs1Ms + 1000LL) * kMsToNs + 1'000'000LL,
                              snap2, row2);
    EXPECT_FALSE(ok) << "delta after ResetToken must be rejected (P-05)";
}

// ============================================================================
// T6: InjectMetadata fills consumer-domain fields
// ============================================================================
TEST(OrderBookAdapter, T6_InjectMetadata) {
    OrderBookAdapter adapter;
    const std::string token_id = "tok_meta_006";

    // First get a row
    WssEvent ev = MakeBookEvent(1'748'400'006'000LL, 5500u);
    OrderBookSnapshot snap;
    FeatureStoreBookRow row;
    ASSERT_TRUE(adapter.OnEvent(ev, token_id, ev.ts().ingestion_ts_ns + 100LL, snap, row));

    // Before inject: market_id / token_side / sport empty
    EXPECT_TRUE(row.market_id.empty());
    EXPECT_TRUE(row.token_side.empty());

    // Inject
    OrderBookAdapter::InjectMetadata(row,
        "0xabc123",   // market_id
        "YES",        // token_side
        "Basketball", // sport
        20000,        // event_date_epoch_days (2024-10-18 approx)
        "Moneyline"   // market_type
    );

    EXPECT_EQ(row.market_id, "0xabc123");
    EXPECT_EQ(row.token_side, "YES");
    EXPECT_EQ(row.sport, "Basketball");
    EXPECT_EQ(row.event_date_epoch_days, 20000);
    EXPECT_EQ(row.market_type, "Moneyline");

    // validate_book_row should now pass (as_of_ts placeholder = ingestion, supply now >= as_of)
    auto vr = validate_book_row(row, row.as_of_ts_ns + 1LL);
    EXPECT_TRUE(vr.valid) << "validate_book_row must pass after InjectMetadata: "
                          << vr.error_msg;
}

// ============================================================================
// T7: SSOT depth constant — kBookDepthLevels == kOrderBookLevels == 5
//     (老高评审要求: no custom kAdapterBookDepth=20; use SSOT directly)
// ============================================================================
TEST(OrderBookAdapter, T7_DepthConstantSSOT) {
    // Both constants come from their respective headers; adapter must not define its own
    static_assert(kBookDepthLevels == 5u,
                  "kBookDepthLevels SSOT must be 5 (orderbook.hpp)");
    static_assert(kOrderBookLevels == 5u,
                  "kOrderBookLevels SSOT must be 5 (feature_store_contract.hpp)");
    static_assert(kBookDepthLevels == kOrderBookLevels,
                  "kBookDepthLevels and kOrderBookLevels must match");

    EXPECT_EQ(kBookDepthLevels, 5u);
    EXPECT_EQ(kOrderBookLevels, 5u);
    EXPECT_EQ(kBookDepthLevels, kOrderBookLevels);
}

// ============================================================================
// T8: FeatureStoreBookRow 4-ts self-check passes after adapter fill
// ============================================================================
TEST(OrderBookAdapter, T8_BookRowTsChainOk) {
    OrderBookAdapter adapter;
    const std::string token_id = "tok_tscheck_008";

    constexpr std::int64_t kTsMs   = 1'748'400'008'000LL;
    constexpr std::int64_t kDsNs   = kTsMs * kMsToNs;
    constexpr std::int64_t kRecvNs = kDsNs + 100'000'000LL;

    WssEvent ev = MakeBookEvent(kTsMs, 5000u, 100u, kRecvNs);
    OrderBookSnapshot snap;
    FeatureStoreBookRow row;
    ASSERT_TRUE(adapter.OnEvent(ev, token_id, kRecvNs, snap, row));

    // Inject metadata to complete the row for full validation
    OrderBookAdapter::InjectMetadata(row, "market-001", "NO", "Soccer", 19900, "Totals");

    // ts_chain_ok must pass
    EXPECT_TRUE(row.ts_chain_ok())
        << "FeatureStoreBookRow::ts_chain_ok() must be true after adapter fill";

    // pit_ok: as_of_ts_ns == ingestion_ts_ns (placeholder); supply now >= as_of
    std::int64_t now_ns = row.as_of_ts_ns + 1'000'000LL;
    EXPECT_TRUE(row.pit_ok(now_ns))
        << "FeatureStoreBookRow::pit_ok(now_ns) must be true";

    // data_source_ts_ns must equal kDsNs (R-20 P-03)
    EXPECT_EQ(row.data_source_ts_ns, kDsNs)
        << "data_source_ts_ns must equal WSS timestamp_ms * 1e6 (R-20)";

    // ingestion_ts_ns must equal kRecvNs
    EXPECT_EQ(row.ingestion_ts_ns, kRecvNs)
        << "ingestion_ts_ns must equal recv_ts_ns";
}
