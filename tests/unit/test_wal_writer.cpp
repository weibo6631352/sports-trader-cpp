// tests/unit/test_wal_writer.cpp — W3 单测 (派单 §7)
//
// 覆盖:
//   T1 test_open_path_validation         R-11 wrong-root → abort (death test)
//   T2 test_pit_assert_chain             R-20 4 ts 不等式 + 穿越未来拒收
//   T3 test_append_sync_p99              同步 Append p99 ≤ 6us (gtest 自测, 无 GoogleBench)
//   T4 test_header_layout                64B + offset (老唐 v1.1 §2.x 对齐)
//
// 注: GoogleBench 未接 (W4 接). 同步 p99 用 ns_steady + N=100k sampling 排序 p99 兜底.
//
// 不耻下问: GoogleBench 接入 @小石 / @老练 W4

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_record_header.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"

namespace stcpp::infra::wal {

// MockRecord 满足 WalRecord concept. 4 ts 可调, audit_id 固定.
struct MockRecord {
    std::int64_t event_ts = 0;
    std::int64_t data_source_ts = 0;
    std::int64_t ingestion_ts = 0;
    std::int64_t as_of_ts = 0;
    std::array<std::uint8_t, 16> ulid{};

    [[nodiscard]] std::int64_t event_ts_ns() const noexcept { return event_ts; }
    [[nodiscard]] std::int64_t data_source_ts_ns() const noexcept { return data_source_ts; }
    [[nodiscard]] std::int64_t ingestion_ts_ns() const noexcept { return ingestion_ts; }
    [[nodiscard]] std::int64_t as_of_ts_ns() const noexcept { return as_of_ts; }
    [[nodiscard]] std::array<std::uint8_t, 16> audit_id() const noexcept { return ulid; }

    std::size_t serialize_into(std::span<std::byte> out) const noexcept {
        (void)out;
        return 0;
    }
    static constexpr std::size_t max_serialized_size() noexcept { return 1024; }
};
static_assert(WalRecord<MockRecord>);

}  // namespace stcpp::infra::wal

// 模板实例化 (skeleton 在 .cpp 没显式实例化, 测试 TU 自实例化)
#include "../../src/stcpp/infra/wal/wal_writer.cpp"
namespace stcpp::infra::wal {
template class WalWriter<MockRecord>;
}

namespace stcpp::infra::wal {
namespace {

static std::int64_t NowNs() noexcept {
    return pit::NowRealtimeNs();
}

// 构造一条 PIT 合法的 record (4 ts 全 = now), 用 now-1s 兜底浮动.
MockRecord ValidRecord() {
    const std::int64_t t = NowNs() - 1'000'000'000LL;
    return MockRecord{t, t, t, t, {}};
}

// ---------- T4 header layout (老唐 v1.1 §2.x 对齐) -----------------------

TEST(WalRecordHeader, LayoutV2_64B) {
    EXPECT_EQ(sizeof(WalRecordHeader), 64u);
    EXPECT_EQ(offsetof(WalRecordHeader, magic), 0u);
    EXPECT_EQ(offsetof(WalRecordHeader, ver), 4u);
    EXPECT_EQ(offsetof(WalRecordHeader, wal_kind), 5u);
    EXPECT_EQ(offsetof(WalRecordHeader, len_payload), 6u);
    EXPECT_EQ(offsetof(WalRecordHeader, seq), 8u);
    EXPECT_EQ(offsetof(WalRecordHeader, event_ts_ns), 16u);        // 老唐 v1.1
    EXPECT_EQ(offsetof(WalRecordHeader, data_source_ts_ns), 24u);  // 老唐 v1.1
    EXPECT_EQ(offsetof(WalRecordHeader, ingestion_ts_ns), 32u);    // 老唐 v1.1
    EXPECT_EQ(offsetof(WalRecordHeader, as_of_ts_ns), 40u);        // 老唐 v1.1
    EXPECT_EQ(offsetof(WalRecordHeader, audit_id), 48u);           // 老唐 v1.1
}

TEST(WalKind, PathRoots4Class) {
    EXPECT_EQ(PathRootOf(WalKind::RiskAudit), "/var/lib/stcpp/audit/");
    EXPECT_EQ(PathRootOf(WalKind::Position), "/var/lib/stcpp/exec/");
    EXPECT_EQ(PathRootOf(WalKind::PaperAudit), "/var/lib/stcpp/paper/");
    EXPECT_EQ(PathRootOf(WalKind::ShadowAudit), "/var/lib/stcpp/shadow/");
}

// ---------- T2 PIT assert chain (R-20) -----------------------------------

TEST(PitAssert, ValidChainPasses) {
    WalRecordHeader h{};
    const std::int64_t t = NowNs() - 1'000'000'000LL;
    h.event_ts_ns = t;
    h.data_source_ts_ns = t;
    h.ingestion_ts_ns = t;
    h.as_of_ts_ns = t;
    EXPECT_TRUE(pit::AssertChain(h));
    EXPECT_EQ(pit::DiagnoseViolation(h), pit::PitViolation::Ok);
}

TEST(PitAssert, EventTsZeroFails) {
    WalRecordHeader h{};
    h.event_ts_ns = 0;
    EXPECT_FALSE(pit::AssertChain(h));
    EXPECT_EQ(pit::DiagnoseViolation(h), pit::PitViolation::EventTsZero);
}

TEST(PitAssert, DsBeforeEventFails) {
    WalRecordHeader h{};
    const std::int64_t t = NowNs() - 1'000'000'000LL;
    h.event_ts_ns = t + 1000;
    h.data_source_ts_ns = t;  // < event
    h.ingestion_ts_ns = t + 2000;
    h.as_of_ts_ns = t + 3000;
    EXPECT_FALSE(pit::AssertChain(h));
    EXPECT_EQ(pit::DiagnoseViolation(h), pit::PitViolation::DsBeforeEvent);
}

TEST(PitAssert, IngestionBeforeDsFails) {
    WalRecordHeader h{};
    const std::int64_t t = NowNs() - 1'000'000'000LL;
    h.event_ts_ns = t;
    h.data_source_ts_ns = t + 1000;
    h.ingestion_ts_ns = t;  // < ds
    h.as_of_ts_ns = t + 2000;
    EXPECT_FALSE(pit::AssertChain(h));
    EXPECT_EQ(pit::DiagnoseViolation(h), pit::PitViolation::IngestionBeforeDs);
}

TEST(PitAssert, AsOfBeforeIngestionFails) {
    WalRecordHeader h{};
    const std::int64_t t = NowNs() - 1'000'000'000LL;
    h.event_ts_ns = t;
    h.data_source_ts_ns = t;
    h.ingestion_ts_ns = t + 1000;
    h.as_of_ts_ns = t;  // < ingestion
    EXPECT_FALSE(pit::AssertChain(h));
    EXPECT_EQ(pit::DiagnoseViolation(h), pit::PitViolation::AsOfBeforeIngestion);
}

// R-20 §7: 穿越未来 (as_of > now) 拒收
TEST(PitAssert, AsOfInFutureFails) {
    WalRecordHeader h{};
    const std::int64_t now = NowNs();
    const std::int64_t future = now + 5'000'000'000LL;  // +5s
    h.event_ts_ns = future;
    h.data_source_ts_ns = future;
    h.ingestion_ts_ns = future;
    h.as_of_ts_ns = future;
    EXPECT_FALSE(pit::AssertChain(h));
    EXPECT_EQ(pit::DiagnoseViolation(h), pit::PitViolation::AsOfInFuture);
}

// ---------- T1 Open path validation (R-11 死亡测试) ----------------------

TEST(WalWriterOpen, ValidPathPrefixOk) {
    WalConfig cfg{};
    cfg.kind = WalKind::RiskAudit;
    cfg.path_prefix = "/var/lib/stcpp/audit/test_w3";
    auto r = WalWriter<MockRecord>::Open(cfg);
    EXPECT_TRUE(r.has_value());
}

// Death test: wrong root → std::abort (R-11)
TEST(WalWriterOpenDeathTest, WrongRootAborts) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    WalConfig cfg{};
    cfg.kind = WalKind::RiskAudit;
    cfg.path_prefix = "/tmp/evil_audit/";  // 不命中白名单
    EXPECT_DEATH({ (void)WalWriter<MockRecord>::Open(cfg); }, "");
}

TEST(WalWriterOpenDeathTest, ShadowToPaperAborts) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    WalConfig cfg{};
    cfg.kind = WalKind::ShadowAudit;
    cfg.path_prefix = "/var/lib/stcpp/paper/";  // 错路径
    EXPECT_DEATH({ (void)WalWriter<MockRecord>::Open(cfg); }, "");
}

TEST(WalWriterOpen, NonPow2RingRejected) {
    WalConfig cfg{};
    cfg.kind = WalKind::PaperAudit;
    cfg.path_prefix = "/var/lib/stcpp/paper/x";
    cfg.ring_capacity = 1000;  // 非 2 的幂
    auto r = WalWriter<MockRecord>::Open(cfg);
    EXPECT_FALSE(r.has_value());
}

// ---------- T2.append PIT 失败回 Backpressure 路径 --------------------------

TEST(WalWriterAppend, PitViolationReturnsError) {
    WalConfig cfg{};
    cfg.kind = WalKind::RiskAudit;
    cfg.path_prefix = "/var/lib/stcpp/audit/pit_test";
    auto w_or = WalWriter<MockRecord>::Open(cfg);
    ASSERT_TRUE(w_or.has_value());
    auto& w = *w_or.value();

    MockRecord bad{};  // event_ts=0
    auto r = w.Append(bad);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), WalError::PitViolation);
}

TEST(WalWriterAppend, ValidRecordReturnsSeq) {
    WalConfig cfg{};
    cfg.kind = WalKind::RiskAudit;
    cfg.path_prefix = "/var/lib/stcpp/audit/seq_test";
    auto w_or = WalWriter<MockRecord>::Open(cfg);
    ASSERT_TRUE(w_or.has_value());
    auto& w = *w_or.value();

    auto r = w.Append(ValidRecord());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 1u);
    EXPECT_EQ(w.HighWatermark(), 1u);

    r = w.Append(ValidRecord());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 2u);
}

// ---------- T3 同步 Append p99 ≤ 6us (兜底, 真 bench W4) ----------------

TEST(WalWriterAppend, SyncP99Budget) {
    WalConfig cfg{};
    cfg.kind = WalKind::RiskAudit;
    cfg.path_prefix = "/var/lib/stcpp/audit/bench";
    auto w_or = WalWriter<MockRecord>::Open(cfg);
    ASSERT_TRUE(w_or.has_value());
    auto& w = *w_or.value();

    constexpr std::size_t N = 10'000;
    std::vector<std::int64_t> samples_ns;
    samples_ns.reserve(N);

    auto rec = ValidRecord();
    for (std::size_t i = 0; i < N; ++i) {
        // 刷新 4 ts → now, 防被 AsOfInFuture / DsBeforeEvent 撞
        const std::int64_t t = pit::NowRealtimeNs() - 1'000'000LL;  // now - 1ms
        rec.event_ts = t;
        rec.data_source_ts = t;
        rec.ingestion_ts = t;
        rec.as_of_ts = t;

        const auto t0 = std::chrono::steady_clock::now();
        (void)w.Append(rec);
        const auto t1 = std::chrono::steady_clock::now();
        samples_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    std::sort(samples_ns.begin(), samples_ns.end());
    const std::int64_t p50 = samples_ns[N / 2];
    const std::int64_t p99 = samples_ns[(N * 99) / 100];

    // 预算: skeleton 不含 SPSC + CRC, 实际数值会比 W4 低很多. 验证 budget 不超
    // 即可 (sanity check). W4 接 SPSC + CRC 后真测 6us.
    EXPECT_LT(p99, 6'000) << "p50=" << p50 << "ns p99=" << p99 << "ns (budget 6000ns/6us)";
}

}  // namespace
}  // namespace stcpp::infra::wal
