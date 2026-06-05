// tests/unit/test_quote_snapshot_hub.cpp
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
//   T06: ResetKey — 重置后 valid=false
//   T07: ResetAll — 全部重置后 valid=false
//   T08: R-20 ts_chain_ok 验证 (正常链 + 违规链 + 边界)
//   T09: baseline fair 来源标记透传 (predict_ok / model_as_of_ts; 大模型 provenance 已砍 2026-06-05)
//   T10: 并发读安全 (TSan-friendly: 多读者同时 Read, writer 持续 Publish)
//   T11: publish_count 计数正确
//   T12: QuoteFeatures trivially_copyable static_assert (编译期)
//   T13: ReadRaw — 与 Read 内容一致
//   (T14 ModelKindTag 枚举测试已砍 2026-06-05「砍掉大模型训练功能」)

#include <atomic>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/sizing/quote_snapshot_hub.hpp"

using namespace stcpp::sizing;

// ---------------------------------------------------------------------------
// 帮助函数
// ---------------------------------------------------------------------------

static constexpr std::int64_t kTs1 = 1'000'000'000LL;
static constexpr std::int64_t kTs2 = 2'000'000'000LL;
static constexpr std::int64_t kTs3 = 3'000'000'000LL;
static constexpr std::int64_t kTs4 = 4'000'000'000LL;

static QuoteFeatures MakeFeatures(double fair_value, double market_mid, double edge_bps, double kelly = 0.05,
                                  double notional = 500.0, double signal = 0.7) {
    QuoteFeatures f{};
    f.valid = true;
    f.event_ts_ns = kTs1;
    f.data_source_ts_ns = kTs2;
    f.ingestion_ts_ns = kTs3;
    f.as_of_ts_ns = kTs4;
    f.fair_value = fair_value;
    f.market_mid = market_mid;
    f.edge_bps = edge_bps;
    f.kelly_fraction = kelly;
    f.suggested_notional = notional;
    f.signal_strength = signal;
    f.predict_ok = false;
    f.model_as_of_ts_ns = kTs3;
    return f;
}

// ---------------------------------------------------------------------------
// T01: 基础快照正确
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T01_BasicPublishRead) {
    QuoteSnapshotHub hub;
    const auto feat = MakeFeatures(0.68, 0.65, 300.0, 0.06, 600.0, 0.8);

    hub.Publish("cond-abc", feat);

    const auto snap = hub.Read("cond-abc");
    ASSERT_TRUE(snap.has_value());
    EXPECT_TRUE(snap->valid);
    EXPECT_DOUBLE_EQ(snap->fair_value, 0.68);
    EXPECT_DOUBLE_EQ(snap->market_mid, 0.65);
    EXPECT_DOUBLE_EQ(snap->edge_bps, 300.0);
    EXPECT_DOUBLE_EQ(snap->kelly_fraction, 0.06);
    EXPECT_DOUBLE_EQ(snap->suggested_notional, 600.0);
    EXPECT_DOUBLE_EQ(snap->signal_strength, 0.8);
    EXPECT_EQ(snap->event_ts_ns, kTs1);
    EXPECT_EQ(snap->data_source_ts_ns, kTs2);
    EXPECT_EQ(snap->ingestion_ts_ns, kTs3);
    EXPECT_EQ(snap->as_of_ts_ns, kTs4);
}

// ---------------------------------------------------------------------------
// T02: per-key 隔离
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T02_PerKeyIsolation) {
    QuoteSnapshotHub hub;

    for (int i = 0; i < 8; ++i) {
        const std::string key = "cond-" + std::to_string(i);
        const double fair = 0.50 + static_cast<double>(i) * 0.02;
        const double mid = fair - 0.01;
        const double edge = (fair - mid) * 10000.0;
        hub.Publish(key, MakeFeatures(fair, mid, edge));
    }

    EXPECT_EQ(hub.key_count(), 8u);

    for (int i = 0; i < 8; ++i) {
        const std::string key = "cond-" + std::to_string(i);
        const auto snap = hub.Read(key);
        ASSERT_TRUE(snap.has_value()) << "key " << key << " missing";
        const double expected_fair = 0.50 + static_cast<double>(i) * 0.02;
        EXPECT_NEAR(snap->fair_value, expected_fair, 1e-9) << "key " << key << " wrong fair";
    }
}

// ---------------------------------------------------------------------------
// T03: double-buffer flip
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T03_DoubleBufferFlip) {
    QuoteSnapshotHub hub;
    const std::string key = "cond-flip";

    // 第一次发布
    const auto feat1 = MakeFeatures(0.60, 0.58, 200.0, 0.04, 400.0);
    hub.Publish(key, feat1);

    auto snap1 = hub.Read(key);
    ASSERT_TRUE(snap1.has_value());
    EXPECT_DOUBLE_EQ(snap1->fair_value, 0.60);
    EXPECT_DOUBLE_EQ(snap1->kelly_fraction, 0.04);

    // 第二次发布 — 不同值
    const auto feat2 = MakeFeatures(0.62, 0.60, 200.0, 0.05, 500.0);
    hub.Publish(key, feat2);

    auto snap2 = hub.Read(key);
    ASSERT_TRUE(snap2.has_value());
    EXPECT_DOUBLE_EQ(snap2->fair_value, 0.62);
    EXPECT_DOUBLE_EQ(snap2->kelly_fraction, 0.05);

    // snap1 (之前的 value copy) 不受影响 — 值语义
    EXPECT_DOUBLE_EQ(snap1->fair_value, 0.60);
    EXPECT_DOUBLE_EQ(snap1->kelly_fraction, 0.04);
}

// ---------------------------------------------------------------------------
// T04: 未发布 key → nullopt / nullptr
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T04_UnknownKeyReturnsNullopt) {
    QuoteSnapshotHub hub;
    hub.Publish("cond-known", MakeFeatures(0.6, 0.58, 200.0));

    const auto snap = hub.Read("cond-unknown");
    EXPECT_FALSE(snap.has_value());

    const auto* raw = hub.ReadRaw("cond-unknown");
    EXPECT_EQ(raw, nullptr);
}

// ---------------------------------------------------------------------------
// T05: max_keys 超限 → 静默丢弃
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T05_MaxKeysCapSilentDrop) {
    constexpr std::size_t kMax = 4;
    QuoteSnapshotHub hub{kMax};

    for (std::size_t i = 0; i < kMax; ++i) {
        hub.Publish("cond-cap-" + std::to_string(i), MakeFeatures(0.5, 0.48, 200.0));
    }
    EXPECT_EQ(hub.key_count(), kMax);

    hub.Publish("cond-cap-overflow", MakeFeatures(0.5, 0.48, 200.0));

    EXPECT_EQ(hub.key_count(), kMax);
    EXPECT_FALSE(hub.Read("cond-cap-overflow").has_value());

    for (std::size_t i = 0; i < kMax; ++i) {
        EXPECT_TRUE(hub.Read("cond-cap-" + std::to_string(i)).has_value());
    }
}

// ---------------------------------------------------------------------------
// T06: ResetKey — 重置后 valid=false
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T06_ResetKey) {
    QuoteSnapshotHub hub;
    const std::string key = "cond-reset";

    hub.Publish(key, MakeFeatures(0.65, 0.63, 200.0));
    {
        const auto snap = hub.Read(key);
        ASSERT_TRUE(snap.has_value());
        EXPECT_TRUE(snap->valid);
    }

    hub.ResetKey(key);

    const auto snap = hub.Read(key);
    ASSERT_TRUE(snap.has_value());
    EXPECT_FALSE(snap->valid);
    EXPECT_EQ(snap->event_ts_ns, 0);
    EXPECT_DOUBLE_EQ(snap->fair_value, 0.0);
}

// ---------------------------------------------------------------------------
// T07: ResetAll — 全部重置后 valid=false
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T07_ResetAll) {
    QuoteSnapshotHub hub;

    hub.Publish("cond-a", MakeFeatures(0.65, 0.63, 200.0));
    hub.Publish("cond-b", MakeFeatures(0.35, 0.37, 200.0));

    hub.ResetAll();

    for (const auto& key : std::vector<std::string>{"cond-a", "cond-b"}) {
        const auto snap = hub.Read(key);
        ASSERT_TRUE(snap.has_value()) << key;
        EXPECT_FALSE(snap->valid) << key;
        EXPECT_EQ(snap->event_ts_ns, 0) << key;
    }
}

// ---------------------------------------------------------------------------
// T08: R-20 ts_chain_ok 验证
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T08_R20TsChainOk) {
    // 正常 4-ts 链
    QuoteFeatures f{};
    f.event_ts_ns = 1000;
    f.data_source_ts_ns = 1100;
    f.ingestion_ts_ns = 1200;
    f.as_of_ts_ns = 1300;
    EXPECT_TRUE(f.ts_chain_ok());

    // 违规: data_source < event
    f.data_source_ts_ns = 999;
    EXPECT_FALSE(f.ts_chain_ok());

    // 违规: event_ts = 0
    f = QuoteFeatures{};
    EXPECT_FALSE(f.ts_chain_ok());

    // 边界: 所有 ts 相等 > 0 → ok
    f.event_ts_ns = 500;
    f.data_source_ts_ns = 500;
    f.ingestion_ts_ns = 500;
    f.as_of_ts_ns = 500;
    EXPECT_TRUE(f.ts_chain_ok());
}

// ---------------------------------------------------------------------------
// T09: ML provenance 字段透传
// ---------------------------------------------------------------------------
// T09: baseline fair 来源标记透传 (大模型 provenance 已砍 2026-06-05; 仅 predict_ok + PIT 锚保留)
TEST(QuoteSnapshotHub, T09_BaselineFairFlagsTransparent) {
    QuoteSnapshotHub hub;
    QuoteFeatures feat = MakeFeatures(0.70, 0.68, 200.0);

    feat.predict_ok = true;
    feat.model_as_of_ts_ns = kTs3;

    hub.Publish("cond-ml", feat);

    const auto snap = hub.Read("cond-ml");
    ASSERT_TRUE(snap.has_value());

    EXPECT_TRUE(snap->predict_ok);
    EXPECT_EQ(snap->model_as_of_ts_ns, kTs3);
}

// ---------------------------------------------------------------------------
// T10: 并发读安全 (TSan-friendly)
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T10_ConcurrentReadSafe) {
    QuoteSnapshotHub hub{128};
    const std::string key = "cond-concurrent";

    hub.Publish(key, MakeFeatures(0.60, 0.58, 200.0));

    std::atomic<bool> stop{false};
    std::atomic<int> read_errors{0};

    std::thread writer([&] {
        for (int i = 0; i < 300; ++i) {
            const double fair = (i % 2 == 0) ? 0.60 : 0.61;
            const double mid = fair - 0.02;
            const double edge = (fair - mid) * 10000.0;
            hub.Publish(key, MakeFeatures(fair, mid, edge));
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
                const double fair = snap->fair_value;
                // fair ∈ {0.60, 0.61}
                if (fair < 0.599 || fair > 0.612) {
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
TEST(QuoteSnapshotHub, T11_PublishCount) {
    QuoteSnapshotHub hub;
    EXPECT_EQ(hub.publish_count(), 0u);

    hub.Publish("c1", MakeFeatures(0.6, 0.58, 200.0));
    EXPECT_EQ(hub.publish_count(), 1u);

    hub.Publish("c1", MakeFeatures(0.61, 0.59, 200.0));  // 同 key 再发
    EXPECT_EQ(hub.publish_count(), 2u);

    hub.Publish("c2", MakeFeatures(0.40, 0.38, 200.0));  // 新 key
    EXPECT_EQ(hub.publish_count(), 3u);
}

// ---------------------------------------------------------------------------
// T12: QuoteFeatures trivially_copyable 编译期验证
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T12_TriviallyCopiableCompileCheck) {
    static_assert(std::is_trivially_copyable_v<QuoteFeatures>, "QuoteFeatures trivially copyable check");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// T13: ReadRaw — 与 Read 内容一致
// ---------------------------------------------------------------------------
TEST(QuoteSnapshotHub, T13_ReadRawConsistentWithRead) {
    QuoteSnapshotHub hub;
    const auto feat = MakeFeatures(0.72, 0.70, 200.0, 0.07, 700.0, 0.85);
    hub.Publish("cond-raw", feat);

    const auto snap_val = hub.Read("cond-raw");
    const auto* snap_ptr = hub.ReadRaw("cond-raw");

    ASSERT_TRUE(snap_val.has_value());
    ASSERT_NE(snap_ptr, nullptr);

    EXPECT_DOUBLE_EQ(snap_val->fair_value, snap_ptr->fair_value);
    EXPECT_DOUBLE_EQ(snap_val->kelly_fraction, snap_ptr->kelly_fraction);
    EXPECT_DOUBLE_EQ(snap_val->suggested_notional, snap_ptr->suggested_notional);
    EXPECT_EQ(snap_val->valid, snap_ptr->valid);
    EXPECT_EQ(snap_val->as_of_ts_ns, snap_ptr->as_of_ts_ns);
}

// (T14 ModelKindTag 枚举覆盖测试已删 2026-06-05「砍掉大模型训练功能」: ModelKindTag 枚举已砍)
