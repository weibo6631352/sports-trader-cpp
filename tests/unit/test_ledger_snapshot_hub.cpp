// tests/unit/test_ledger_snapshot_hub.cpp
//
// Owner: 小石 (#41, data-structures-expert, G-LEDGER-OWNER)
// last_review: 2026-05-29
//
// 测试覆盖:
//   T01: 基础快照正确 — Publish 后 Read 字段完全匹配
//   T02: per-key 隔离 — 多 market_key 互不干扰
//   T03: double-buffer flip — 第二次 Publish 后读到新值, 旧值语义安全
//   T04: 未发布 key → Read 返回 nullopt / ReadRaw 返回 nullptr
//   T05: max_keys 超限 → 静默丢弃, 不崩溃
//   T06: ResetKey — 重置后 valid=false, ts 归零
//   T07: ResetAll — 全部重置后 valid=false
//   T08: R-20 ts_chain_ok 验证 (正常链 + 违规链 + 边界)
//   T09: R-11 mode 字段透传 (paper/live/backtest)
//   T10: 并发读安全 (TSan-friendly: 多读者同时 Read, writer 持续 Publish)
//   T11: publish_count 计数正确
//   T12: LedgerFeatures trivially_copyable static_assert (编译期)
//   T13: ReadRaw — 与 Read 内容一致
//   T14: pnl_net() helper 正确

#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/risk/ledger_snapshot_hub.hpp"

using namespace stcpp::risk;

// ---------------------------------------------------------------------------
// 帮助函数
// ---------------------------------------------------------------------------

static constexpr std::int64_t kTs1 = 1'000'000'000LL;
static constexpr std::int64_t kTs2 = 2'000'000'000LL;
static constexpr std::int64_t kTs3 = 3'000'000'000LL;
static constexpr std::int64_t kTs4 = 4'000'000'000LL;

static LedgerFeatures MakeFeatures(double net_qty, double avg_entry, double mark_price,
                                   double pnl_realized = 0.0, double pnl_unrealized = 0.0,
                                   ExecutionModeTag mode = ExecutionModeTag::kPaper) {
    LedgerFeatures f{};
    f.valid = true;
    f.event_ts_ns = kTs1;
    f.data_source_ts_ns = kTs2;
    f.ingestion_ts_ns = kTs3;
    f.as_of_ts_ns = kTs4;
    f.net_qty = net_qty;
    f.avg_entry_price = avg_entry;
    f.mark_price = mark_price;
    f.pnl_realized = pnl_realized;
    f.pnl_unrealized = pnl_unrealized;
    f.pnl_fee = 0.03 * avg_entry * (net_qty > 0 ? net_qty : -net_qty);
    f.pnl_gross = pnl_realized + pnl_unrealized;
    f.mode = mode;
    return f;
}

// ---------------------------------------------------------------------------
// T01: 基础快照正确
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T01_BasicPublishRead) {
    LedgerSnapshotHub hub;
    const auto feat = MakeFeatures(100.0, 0.60, 0.65, 5.0, 5.0);

    hub.Publish("market-abc", feat);

    const auto snap = hub.Read("market-abc");
    ASSERT_TRUE(snap.has_value());
    EXPECT_TRUE(snap->valid);
    EXPECT_DOUBLE_EQ(snap->net_qty, 100.0);
    EXPECT_DOUBLE_EQ(snap->avg_entry_price, 0.60);
    EXPECT_DOUBLE_EQ(snap->mark_price, 0.65);
    EXPECT_DOUBLE_EQ(snap->pnl_realized, 5.0);
    EXPECT_DOUBLE_EQ(snap->pnl_unrealized, 5.0);
    EXPECT_DOUBLE_EQ(snap->pnl_gross, 10.0);
    EXPECT_EQ(snap->event_ts_ns, kTs1);
    EXPECT_EQ(snap->data_source_ts_ns, kTs2);
    EXPECT_EQ(snap->ingestion_ts_ns, kTs3);
    EXPECT_EQ(snap->as_of_ts_ns, kTs4);
    EXPECT_EQ(snap->mode, ExecutionModeTag::kPaper);
}

// ---------------------------------------------------------------------------
// T02: per-key 隔离
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T02_PerKeyIsolation) {
    LedgerSnapshotHub hub;

    for (int i = 0; i < 8; ++i) {
        const std::string key = "market-" + std::to_string(i);
        const double qty = static_cast<double>(i + 1) * 10.0;
        hub.Publish(key, MakeFeatures(qty, 0.50, 0.52));
    }

    EXPECT_EQ(hub.key_count(), 8u);

    for (int i = 0; i < 8; ++i) {
        const std::string key = "market-" + std::to_string(i);
        const auto snap = hub.Read(key);
        ASSERT_TRUE(snap.has_value()) << "key " << key << " missing";
        const double expected_qty = static_cast<double>(i + 1) * 10.0;
        EXPECT_DOUBLE_EQ(snap->net_qty, expected_qty) << "key " << key << " wrong qty";
    }
}

// ---------------------------------------------------------------------------
// T03: double-buffer flip
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T03_DoubleBufferFlip) {
    LedgerSnapshotHub hub;
    const std::string key = "market-flip";

    // 第一次发布
    const auto feat1 = MakeFeatures(50.0, 0.55, 0.58);
    hub.Publish(key, feat1);

    auto snap1 = hub.Read(key);
    ASSERT_TRUE(snap1.has_value());
    EXPECT_DOUBLE_EQ(snap1->net_qty, 50.0);
    EXPECT_DOUBLE_EQ(snap1->avg_entry_price, 0.55);

    // 第二次发布 — 不同持仓
    const auto feat2 = MakeFeatures(75.0, 0.57, 0.60);
    hub.Publish(key, feat2);

    auto snap2 = hub.Read(key);
    ASSERT_TRUE(snap2.has_value());
    // 读到新值
    EXPECT_DOUBLE_EQ(snap2->net_qty, 75.0);
    EXPECT_DOUBLE_EQ(snap2->avg_entry_price, 0.57);

    // snap1 (之前的 value copy) 不受影响 — 验证值语义
    EXPECT_DOUBLE_EQ(snap1->net_qty, 50.0);
    EXPECT_DOUBLE_EQ(snap1->avg_entry_price, 0.55);
}

// ---------------------------------------------------------------------------
// T04: 未发布 key → nullopt / nullptr
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T04_UnknownKeyReturnsNullopt) {
    LedgerSnapshotHub hub;
    hub.Publish("market-known", MakeFeatures(10.0, 0.5, 0.52));

    const auto snap = hub.Read("market-unknown");
    EXPECT_FALSE(snap.has_value());

    const auto* raw = hub.ReadRaw("market-unknown");
    EXPECT_EQ(raw, nullptr);
}

// ---------------------------------------------------------------------------
// T05: max_keys 超限 → 静默丢弃
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T05_MaxKeysCapSilentDrop) {
    constexpr std::size_t kMax = 4;
    LedgerSnapshotHub hub{kMax};

    for (std::size_t i = 0; i < kMax; ++i) {
        hub.Publish("market-cap-" + std::to_string(i), MakeFeatures(10.0, 0.5, 0.51));
    }
    EXPECT_EQ(hub.key_count(), kMax);

    // 第 5 个 key — 应静默忽略
    hub.Publish("market-cap-overflow", MakeFeatures(10.0, 0.5, 0.51));

    EXPECT_EQ(hub.key_count(), kMax);
    EXPECT_FALSE(hub.Read("market-cap-overflow").has_value());

    for (std::size_t i = 0; i < kMax; ++i) {
        EXPECT_TRUE(hub.Read("market-cap-" + std::to_string(i)).has_value());
    }
}

// ---------------------------------------------------------------------------
// T06: ResetKey — 重置后 valid=false, ts 归零
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T06_ResetKey) {
    LedgerSnapshotHub hub;
    const std::string key = "market-reset";

    hub.Publish(key, MakeFeatures(50.0, 0.60, 0.62));
    {
        const auto snap = hub.Read(key);
        ASSERT_TRUE(snap.has_value());
        EXPECT_TRUE(snap->valid);
    }

    hub.ResetKey(key);

    const auto snap = hub.Read(key);
    // key 仍然注册, 但 valid=false
    ASSERT_TRUE(snap.has_value());
    EXPECT_FALSE(snap->valid);
    EXPECT_EQ(snap->event_ts_ns, 0);
    EXPECT_DOUBLE_EQ(snap->net_qty, 0.0);
}

// ---------------------------------------------------------------------------
// T07: ResetAll — 全部重置后 valid=false
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T07_ResetAll) {
    LedgerSnapshotHub hub;

    hub.Publish("market-a", MakeFeatures(100.0, 0.60, 0.62));
    hub.Publish("market-b", MakeFeatures(200.0, 0.40, 0.41));

    hub.ResetAll();

    for (const auto& key : std::vector<std::string>{"market-a", "market-b"}) {
        const auto snap = hub.Read(key);
        ASSERT_TRUE(snap.has_value()) << key;
        EXPECT_FALSE(snap->valid) << key;
        EXPECT_EQ(snap->event_ts_ns, 0) << key;
    }
}

// ---------------------------------------------------------------------------
// T08: R-20 ts_chain_ok 验证
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T08_R20TsChainOk) {
    // 正常 4-ts 链
    LedgerFeatures f{};
    f.event_ts_ns = 1000;
    f.data_source_ts_ns = 1100;
    f.ingestion_ts_ns = 1200;
    f.as_of_ts_ns = 1300;
    EXPECT_TRUE(f.ts_chain_ok());

    // 违规: data_source < event
    f.data_source_ts_ns = 900;
    EXPECT_FALSE(f.ts_chain_ok());

    // 违规: event_ts = 0
    f = LedgerFeatures{};
    EXPECT_FALSE(f.ts_chain_ok());

    // 边界: 所有 ts 相等且 > 0 → ok (单调非严格)
    f.event_ts_ns = 500;
    f.data_source_ts_ns = 500;
    f.ingestion_ts_ns = 500;
    f.as_of_ts_ns = 500;
    EXPECT_TRUE(f.ts_chain_ok());

    // 违规: ingestion > as_of
    f.ingestion_ts_ns = 600;
    EXPECT_FALSE(f.ts_chain_ok());
}

// ---------------------------------------------------------------------------
// T09: R-11 mode 字段透传
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T09_R11ModeTransparent) {
    LedgerSnapshotHub hub;

    hub.Publish("m-paper", MakeFeatures(10.0, 0.5, 0.52, 0.0, 0.0, ExecutionModeTag::kPaper));
    hub.Publish("m-live", MakeFeatures(10.0, 0.5, 0.52, 0.0, 0.0, ExecutionModeTag::kLive));
    hub.Publish("m-backtest", MakeFeatures(10.0, 0.5, 0.52, 0.0, 0.0, ExecutionModeTag::kBacktest));

    const auto sp = hub.Read("m-paper");
    ASSERT_TRUE(sp.has_value());
    EXPECT_EQ(sp->mode, ExecutionModeTag::kPaper);

    const auto sl = hub.Read("m-live");
    ASSERT_TRUE(sl.has_value());
    EXPECT_EQ(sl->mode, ExecutionModeTag::kLive);

    const auto sb = hub.Read("m-backtest");
    ASSERT_TRUE(sb.has_value());
    EXPECT_EQ(sb->mode, ExecutionModeTag::kBacktest);
}

// ---------------------------------------------------------------------------
// T10: 并发读安全 (TSan-friendly)
//
//   1 writer 线程持续 Publish (200 次, 交替两种 net_qty)
//   4 reader 线程持续 Read (各自读到停止)
//   验证: 读到的 net_qty 只能是 100.0 或 200.0
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T10_ConcurrentReadSafe) {
    LedgerSnapshotHub hub{128};
    const std::string key = "market-concurrent";

    hub.Publish(key, MakeFeatures(100.0, 0.60, 0.62));

    std::atomic<bool> stop{false};
    std::atomic<int> read_errors{0};

    std::thread writer([&] {
        for (int i = 0; i < 300; ++i) {
            const double qty = (i % 2 == 0) ? 100.0 : 200.0;
            hub.Publish(key, MakeFeatures(qty, 0.60, 0.62));
        }
        stop.store(true, std::memory_order_release);
    });

    std::vector<std::thread> readers;
    readers.reserve(4);
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_acquire)) {
                const auto snap = hub.Read(key);
                if (!snap.has_value()) {
                    read_errors.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                const double qty = snap->net_qty;
                if (qty != 100.0 && qty != 200.0) {
                    read_errors.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    writer.join();
    for (auto& t : readers)
        t.join();

    EXPECT_EQ(read_errors.load(), 0) << "Concurrent read produced " << read_errors.load() << " errors";
}

// ---------------------------------------------------------------------------
// T11: publish_count 计数正确
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T11_PublishCount) {
    LedgerSnapshotHub hub;
    EXPECT_EQ(hub.publish_count(), 0u);

    hub.Publish("m1", MakeFeatures(10.0, 0.5, 0.51));
    EXPECT_EQ(hub.publish_count(), 1u);

    hub.Publish("m1", MakeFeatures(20.0, 0.5, 0.51));  // 同 key 再发
    EXPECT_EQ(hub.publish_count(), 2u);

    hub.Publish("m2", MakeFeatures(30.0, 0.4, 0.41));  // 新 key
    EXPECT_EQ(hub.publish_count(), 3u);
}

// ---------------------------------------------------------------------------
// T12: LedgerFeatures trivially_copyable 编译期验证
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T12_TriviallyCopiableCompileCheck) {
    static_assert(std::is_trivially_copyable_v<LedgerFeatures>, "LedgerFeatures trivially copyable check");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// T13: ReadRaw — 与 Read 内容一致
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T13_ReadRawConsistentWithRead) {
    LedgerSnapshotHub hub;
    const auto feat = MakeFeatures(80.0, 0.65, 0.67, 3.0, 1.6);
    hub.Publish("market-raw", feat);

    const auto snap_val = hub.Read("market-raw");
    const auto* snap_ptr = hub.ReadRaw("market-raw");

    ASSERT_TRUE(snap_val.has_value());
    ASSERT_NE(snap_ptr, nullptr);

    EXPECT_DOUBLE_EQ(snap_val->net_qty, snap_ptr->net_qty);
    EXPECT_DOUBLE_EQ(snap_val->avg_entry_price, snap_ptr->avg_entry_price);
    EXPECT_DOUBLE_EQ(snap_val->pnl_realized, snap_ptr->pnl_realized);
    EXPECT_EQ(snap_val->valid, snap_ptr->valid);
    EXPECT_EQ(snap_val->as_of_ts_ns, snap_ptr->as_of_ts_ns);
}

// ---------------------------------------------------------------------------
// T14: pnl_net() helper 正确
// ---------------------------------------------------------------------------
TEST(LedgerSnapshotHub, T14_PnlNetHelper) {
    LedgerFeatures f{};
    f.pnl_gross = 10.0;
    f.pnl_fee = 1.5;
    EXPECT_DOUBLE_EQ(f.pnl_net(), 8.5);

    f.pnl_gross = 0.0;
    f.pnl_fee = 0.0;
    EXPECT_DOUBLE_EQ(f.pnl_net(), 0.0);
}
