// tests/unit/test_orderbook_snapshot_hub.cpp
//
// Owner: 小冯 (#34)
// last_review: 2026-05-29
//
// 测试覆盖:
//   T01: 基础快照正确 — 发布后读取字段完全匹配
//   T02: 互补镜像 — token0/token1 各自独立 (per-token 隔离)
//   T03: double-buffer flip — 第二次 Publish 后读到新值, 旧值不可见
//   T04: per-token 隔离 — 多 token 互不干扰
//   T05: 未发布 token → Read 返回 nullopt
//   T06: max_tokens 超限 → Publish 静默丢弃, 不崩溃
//   T07: ResetToken — 重置后 valid=false
//   T08: ResetAll — 全部重置后 valid=false
//   T09: R-20 ts_chain_ok 验证
//   T10: WssConnState 名字映射
//   T11: ReadRaw — 返回同一指针内容与 Read 一致
//   T12: 并发读安全 (TSan-friendly: 多读者同时 Read, writer 持续 Publish)
//   T13: publish_count 计数正确
//   T14: OrderBookFeatures trivially copyable 静态断言 (编译期)

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"

using namespace stcpp::polymarket::clob_wss;
using stcpp::microstructure::OrderBookLevel;

// ---------------------------------------------------------------------------
// 帮助函数
// ---------------------------------------------------------------------------

static constexpr std::int64_t kTs1 = 1'000'000'000LL;
static constexpr std::int64_t kTs2 = 2'000'000'000LL;
static constexpr std::int64_t kTs3 = 3'000'000'000LL;
static constexpr std::int64_t kTs4 = 4'000'000'000LL;

static OrderBookFeatures MakeFeatures(double bid0_price, double ask0_price, double bid0_size = 1000.0,
                                      double ask0_size = 1200.0, std::int64_t seq = 100, bool valid = true) {
    OrderBookFeatures f{};
    f.valid = valid;
    f.event_ts_ns = kTs1;
    f.data_source_ts_ns = kTs2;
    f.ingestion_ts_ns = kTs3;
    f.as_of_ts_ns = kTs4;
    f.sequence_no = seq;
    f.gap_count = 0;
    f.wss_state = WssConnState::kConnected;

    // L1
    f.bids[0] = OrderBookLevel{bid0_price, bid0_size};
    f.asks[0] = OrderBookLevel{ask0_price, ask0_size};
    // L2..L4: 有值
    for (std::size_t i = 1; i < 4; ++i) {
        f.bids[i] = OrderBookLevel{bid0_price - static_cast<double>(i) * 0.01,
                                   bid0_size / static_cast<double>(i + 1)};
        f.asks[i] = OrderBookLevel{ask0_price + static_cast<double>(i) * 0.01,
                                   ask0_size / static_cast<double>(i + 1)};
    }
    // L5: NaN
    f.bids[4] = OrderBookLevel{std::numeric_limits<double>::quiet_NaN(), 0.0};
    f.asks[4] = OrderBookLevel{std::numeric_limits<double>::quiet_NaN(), 0.0};

    const double mid = 0.5 * (bid0_price + ask0_price);
    f.mid = mid;
    f.microprice = mid + 0.001;  // 简化: 接近 mid
    f.spread = ask0_price - bid0_price;
    f.imbalance = (bid0_size - ask0_size) / (bid0_size + ask0_size);
    return f;
}

// ---------------------------------------------------------------------------
// T01: 基础快照正确
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T01_BasicPublishRead) {
    OrderBookSnapshotHub hub;
    const auto feat = MakeFeatures(0.64, 0.66);

    hub.Publish("tok-abc-0", feat);

    const auto snap = hub.Read("tok-abc-0");
    ASSERT_TRUE(snap.has_value());
    EXPECT_TRUE(snap->valid);
    EXPECT_DOUBLE_EQ(snap->bids[0].price, 0.64);
    EXPECT_DOUBLE_EQ(snap->asks[0].price, 0.66);
    EXPECT_DOUBLE_EQ(snap->bids[0].size_usdc, 1000.0);
    EXPECT_DOUBLE_EQ(snap->asks[0].size_usdc, 1200.0);
    EXPECT_EQ(snap->sequence_no, 100);
    EXPECT_EQ(snap->gap_count, 0);
    EXPECT_EQ(snap->wss_state, WssConnState::kConnected);
    EXPECT_EQ(snap->event_ts_ns, kTs1);
    EXPECT_EQ(snap->data_source_ts_ns, kTs2);
    EXPECT_EQ(snap->ingestion_ts_ns, kTs3);
    EXPECT_EQ(snap->as_of_ts_ns, kTs4);
}

// ---------------------------------------------------------------------------
// T02: 互补镜像 per-token 隔离
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T02_ComplementaryMirror) {
    OrderBookSnapshotHub hub;

    // NBA LAL/BOS 互补: bid0 + ask1 ≈ 1.0, ask0 + bid1 ≈ 1.0
    const auto feat0 = MakeFeatures(0.644, 0.656, 3200.0, 2700.0, 88421);
    const auto feat1 = MakeFeatures(0.344, 0.356, 2700.0, 3200.0, 88422);

    hub.Publish("tok-lal-0", feat0);
    hub.Publish("tok-bos-1", feat1);

    const auto s0 = hub.Read("tok-lal-0");
    const auto s1 = hub.Read("tok-bos-1");

    ASSERT_TRUE(s0.has_value());
    ASSERT_TRUE(s1.has_value());

    // 互补检查: best_bid0 + best_ask1 ≈ 1.000
    EXPECT_NEAR(s0->bids[0].price + s1->asks[0].price, 1.000, 1e-6);
    // best_ask0 + best_bid1 ≈ 1.000
    EXPECT_NEAR(s0->asks[0].price + s1->bids[0].price, 1.000, 1e-6);

    // per-token 独立序号
    EXPECT_EQ(s0->sequence_no, 88421);
    EXPECT_EQ(s1->sequence_no, 88422);
}

// ---------------------------------------------------------------------------
// T03: double-buffer flip — 第二次 Publish 后读到新值
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T03_DoubleBufferFlip) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-flip";

    // 第一次发布
    const auto feat1 = MakeFeatures(0.60, 0.62, 500.0, 600.0, 1);
    hub.Publish(tok, feat1);

    auto snap1 = hub.Read(tok);
    ASSERT_TRUE(snap1.has_value());
    EXPECT_DOUBLE_EQ(snap1->bids[0].price, 0.60);
    EXPECT_EQ(snap1->sequence_no, 1);

    // 第二次发布 — 不同价格
    const auto feat2 = MakeFeatures(0.61, 0.63, 800.0, 700.0, 2);
    hub.Publish(tok, feat2);

    auto snap2 = hub.Read(tok);
    ASSERT_TRUE(snap2.has_value());
    // 读到新值
    EXPECT_DOUBLE_EQ(snap2->bids[0].price, 0.61);
    EXPECT_DOUBLE_EQ(snap2->asks[0].price, 0.63);
    EXPECT_EQ(snap2->sequence_no, 2);

    // snap1 (之前的 value copy) 不受影响 — 验证值语义
    EXPECT_DOUBLE_EQ(snap1->bids[0].price, 0.60);
    EXPECT_EQ(snap1->sequence_no, 1);
}

// ---------------------------------------------------------------------------
// T04: per-token 隔离 — 多 token 互不干扰
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T04_PerTokenIsolation) {
    OrderBookSnapshotHub hub;

    // 注册 10 个 token
    for (int i = 0; i < 10; ++i) {
        const std::string tok = "tok-" + std::to_string(i);
        const double bid = 0.50 + static_cast<double>(i) * 0.01;
        const double ask = bid + 0.01;
        hub.Publish(tok, MakeFeatures(bid, ask, 1000.0, 1000.0, static_cast<std::int64_t>(i)));
    }

    EXPECT_EQ(hub.token_count(), 10u);

    // 验证每个 token 读到自己的值
    for (int i = 0; i < 10; ++i) {
        const std::string tok = "tok-" + std::to_string(i);
        const auto snap = hub.Read(tok);
        ASSERT_TRUE(snap.has_value()) << "token " << tok << " missing";
        EXPECT_EQ(snap->sequence_no, static_cast<std::int64_t>(i)) << "token " << tok << " wrong seq";
        const double expected_bid = 0.50 + static_cast<double>(i) * 0.01;
        EXPECT_NEAR(snap->bids[0].price, expected_bid, 1e-9) << "token " << tok << " wrong bid";
    }
}

// ---------------------------------------------------------------------------
// T05: 未发布 token → Read 返回 nullopt
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T05_UnknownTokenReturnsNullopt) {
    OrderBookSnapshotHub hub;
    hub.Publish("tok-known", MakeFeatures(0.5, 0.51));

    const auto snap = hub.Read("tok-unknown");
    EXPECT_FALSE(snap.has_value());

    const auto* raw = hub.ReadRaw("tok-unknown");
    EXPECT_EQ(raw, nullptr);
}

// ---------------------------------------------------------------------------
// T06: max_tokens 超限 → 静默丢弃, 不崩溃
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T06_MaxTokensCapSilentDrop) {
    constexpr std::size_t kMax = 4;
    OrderBookSnapshotHub hub{kMax};

    for (std::size_t i = 0; i < kMax; ++i) {
        hub.Publish("tok-cap-" + std::to_string(i), MakeFeatures(0.5, 0.51));
    }
    EXPECT_EQ(hub.token_count(), kMax);

    // 第 5 个 token — 应静默忽略
    hub.Publish("tok-cap-overflow", MakeFeatures(0.5, 0.51));

    // token_count 不超 kMax
    EXPECT_EQ(hub.token_count(), kMax);

    // 超限 token 不可读
    EXPECT_FALSE(hub.Read("tok-cap-overflow").has_value());

    // 已有 token 不受影响
    for (std::size_t i = 0; i < kMax; ++i) {
        EXPECT_TRUE(hub.Read("tok-cap-" + std::to_string(i)).has_value());
    }
}

// ---------------------------------------------------------------------------
// T07: ResetToken — 重置后 valid=false
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T07_ResetToken) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-reset";

    hub.Publish(tok, MakeFeatures(0.60, 0.62));
    {
        const auto snap = hub.Read(tok);
        ASSERT_TRUE(snap.has_value());
        EXPECT_TRUE(snap->valid);
    }

    hub.ResetToken(tok);

    const auto snap = hub.Read(tok);
    // token 仍然注册 (不移除), 但 valid=false
    ASSERT_TRUE(snap.has_value());
    EXPECT_FALSE(snap->valid);
    EXPECT_EQ(snap->event_ts_ns, 0);
}

// ---------------------------------------------------------------------------
// T08: ResetAll — 全部重置后 valid=false
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T08_ResetAll) {
    OrderBookSnapshotHub hub;

    hub.Publish("tok-a", MakeFeatures(0.60, 0.62));
    hub.Publish("tok-b", MakeFeatures(0.40, 0.42));

    hub.ResetAll();

    for (const auto& tok : std::vector<std::string>{"tok-a", "tok-b"}) {
        const auto snap = hub.Read(tok);
        ASSERT_TRUE(snap.has_value()) << tok;
        EXPECT_FALSE(snap->valid) << tok;
        EXPECT_EQ(snap->event_ts_ns, 0) << tok;
    }
}

// ---------------------------------------------------------------------------
// T09: R-20 ts_chain_ok 验证
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T09_R20TsChainOk) {
    // 正常 4-ts 链
    OrderBookFeatures f{};
    f.valid = true;
    f.event_ts_ns = 1000;
    f.data_source_ts_ns = 1100;
    f.ingestion_ts_ns = 1200;
    f.as_of_ts_ns = 1300;
    EXPECT_TRUE(f.ts_chain_ok());

    // 违规: data_source < event
    f.data_source_ts_ns = 900;
    EXPECT_FALSE(f.ts_chain_ok());

    // 违规: event_ts = 0
    f = OrderBookFeatures{};
    EXPECT_FALSE(f.ts_chain_ok());

    // 边界: 所有 ts 相等且 > 0 → ok (monotonic 不要求严格递增)
    f.event_ts_ns = 500;
    f.data_source_ts_ns = 500;
    f.ingestion_ts_ns = 500;
    f.as_of_ts_ns = 500;
    EXPECT_TRUE(f.ts_chain_ok());
}

// ---------------------------------------------------------------------------
// T10: WssConnState 名字映射
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T10_WssConnStateName) {
    EXPECT_STREQ(WssConnStateName(WssConnState::kConnected), "CONNECTED");
    EXPECT_STREQ(WssConnStateName(WssConnState::kReconnecting), "RECONNECTING");
    EXPECT_STREQ(WssConnStateName(WssConnState::kDisconnected), "DISCONNECTED");
    EXPECT_STREQ(WssConnStateName(WssConnState::kUnknown), "UNKNOWN");
}

// ---------------------------------------------------------------------------
// T11: ReadRaw — 与 Read 内容一致
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T11_ReadRawConsistentWithRead) {
    OrderBookSnapshotHub hub;
    const auto feat = MakeFeatures(0.55, 0.57, 2000.0, 1800.0, 42);
    hub.Publish("tok-raw", feat);

    const auto snap_val = hub.Read("tok-raw");
    const auto* snap_ptr = hub.ReadRaw("tok-raw");

    ASSERT_TRUE(snap_val.has_value());
    ASSERT_NE(snap_ptr, nullptr);

    EXPECT_DOUBLE_EQ(snap_val->bids[0].price, snap_ptr->bids[0].price);
    EXPECT_DOUBLE_EQ(snap_val->asks[0].price, snap_ptr->asks[0].price);
    EXPECT_EQ(snap_val->sequence_no, snap_ptr->sequence_no);
    EXPECT_EQ(snap_val->valid, snap_ptr->valid);
}

// ---------------------------------------------------------------------------
// T12: 并发读安全 (TSan-friendly)
//
//   1 writer 线程持续 Publish (200 次)
//   4 reader 线程持续 Read (各 500 次)
//   验证: 读取到的快照永远是 valid=true (writer 只发布 valid=true 值)
//         读取到的 best_bid ∈ {0.60, 0.61} (writer 交替发布两个价格)
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T12_ConcurrentReadSafe) {
    OrderBookSnapshotHub hub{128};
    const std::string tok = "tok-concurrent";

    // 预发布一个初始值, 确保 Read 不返回 nullopt
    hub.Publish(tok, MakeFeatures(0.60, 0.62, 1000.0, 1000.0, 1));

    std::atomic<bool> stop{false};
    std::atomic<int> read_errors{0};

    // Writer
    std::thread writer([&] {
        for (int i = 0; i < 300; ++i) {
            const double bid = (i % 2 == 0) ? 0.60 : 0.61;
            const double ask = bid + 0.02;
            hub.Publish(tok, MakeFeatures(bid, ask, 1000.0, 1000.0, static_cast<std::int64_t>(i + 1)));
        }
        stop.store(true, std::memory_order_release);
    });

    // Readers
    std::vector<std::thread> readers;
    readers.reserve(4);
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_acquire)) {
                const auto snap = hub.Read(tok);
                if (!snap.has_value()) {
                    read_errors.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                const double bid = snap->bids[0].price;
                // 合法价格: 0.60 或 0.61
                if (bid < 0.599 || bid > 0.612) {
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
// T13: publish_count 计数正确
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T13_PublishCount) {
    OrderBookSnapshotHub hub;
    EXPECT_EQ(hub.publish_count(), 0u);

    hub.Publish("tok-p1", MakeFeatures(0.5, 0.51));
    EXPECT_EQ(hub.publish_count(), 1u);

    hub.Publish("tok-p1", MakeFeatures(0.51, 0.52));  // 同 token 再发
    EXPECT_EQ(hub.publish_count(), 2u);

    hub.Publish("tok-p2", MakeFeatures(0.40, 0.41));  // 新 token
    EXPECT_EQ(hub.publish_count(), 3u);
}

// ---------------------------------------------------------------------------
// T14: 编译期静态断言 (在 .hpp 中已有, 此处再显式验证)
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T14_TriviallyCopiableCompileCheck) {
    // 如果 OrderBookFeatures 不是 trivially copyable, static_assert 会在编译时报错
    // 此 test 仅作为运行期占位 (编译成功即通过)
    static_assert(std::is_trivially_copyable_v<OrderBookFeatures>,
                  "OrderBookFeatures trivially copyable check");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// T15: 多档 bid/ask 正确保存 (L1..L4)
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T15_MultiLevelBidAskStored) {
    OrderBookSnapshotHub hub;
    const auto feat = MakeFeatures(0.65, 0.67);
    hub.Publish("tok-ml", feat);

    const auto snap = hub.Read("tok-ml");
    ASSERT_TRUE(snap.has_value());

    // L1
    EXPECT_DOUBLE_EQ(snap->bids[0].price, 0.65);
    EXPECT_DOUBLE_EQ(snap->asks[0].price, 0.67);

    // L2 (distance 0.01 from L1)
    EXPECT_NEAR(snap->bids[1].price, 0.64, 1e-9);
    EXPECT_NEAR(snap->asks[1].price, 0.68, 1e-9);

    // L3 (distance 0.02 from L1)
    EXPECT_NEAR(snap->bids[2].price, 0.63, 1e-9);
    EXPECT_NEAR(snap->asks[2].price, 0.69, 1e-9);

    // L5 (NaN)
    EXPECT_TRUE(std::isnan(snap->bids[4].price));
    EXPECT_TRUE(std::isnan(snap->asks[4].price));
}

// ---------------------------------------------------------------------------
// T16: best_bid() / best_ask() 快捷方法
// ---------------------------------------------------------------------------
TEST(OrderBookSnapshotHub, T16_BestBidAskHelpers) {
    OrderBookFeatures f = MakeFeatures(0.44, 0.46);
    EXPECT_DOUBLE_EQ(f.best_bid(), 0.44);
    EXPECT_DOUBLE_EQ(f.best_ask(), 0.46);
    EXPECT_DOUBLE_EQ(f.best_bid_size(), 1000.0);
    EXPECT_DOUBLE_EQ(f.best_ask_size(), 1200.0);
}
