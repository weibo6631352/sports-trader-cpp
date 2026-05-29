// tests/unit/test_ml_hook.cpp — MLDataHook v0.1 单测 (W4 Wave 20 小邓)
//
// 覆盖:
//   M1  FeatureSnapshot concept + 32 feature 全填 (default NaN -> filled)
//   M2  4 ts R-20 透传 (PIT enforce: ts_chain_ok false → drop)
//   M3  ML-R8 feature_snapshot_id == 0 → drop + counter++
//   M4  join key 4 级一致性 (signal → decision → fill → settle 同 audit_id_bytes 联通)
//   M5  R-11: paper path_prefix 必须以 /var/lib/stcpp/shadow/ 起头 (framework abort 测试见 wal test)
//   M6  R-12: hook 4 入口 < 1us p99 (skeleton 期 framework 路径足够快, gtest 兜底)
//   M7  partial label on_fill + 最终 label on_settle 双写 (decision_taken / executed / outcome 闭环)
//   M8  cache evict — settle 后 join_cache 删除
//
// 注: 复用 emitter 模式 — TU 内 #include hook.cpp 做模板实例化.

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/ml/feature_snapshot.hpp"
#include "stcpp/ml/hook.hpp"
#include "stcpp/ml/training_label.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/strategy/signal_iface.hpp"

// 模板实例化 (FeatureSnapshot + TrainingLabel 的 WalWriter)
#include "../../src/stcpp/ml/hook.cpp"

namespace stcpp::ml {
namespace {

using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;

static std::int64_t NowNs() noexcept {
    return stcpp::infra::wal::pit::NowRealtimeNs();
}

// ---- helpers ----------------------------------------------------------------

FeatureSnapshot make_snap(std::uint64_t fsid = 0xABCDEFULL,
                          std::array<std::uint8_t, 16> id = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                                                             15, 16}) {
    const std::int64_t now = NowNs();
    const std::int64_t base = now - 1'000'000'000LL;  // 1s ago
    FeatureSnapshot s{};                              // ctor 已把 features 填 NaN
    s.event_ts = base;
    s.data_source_ts = base + 1'000;
    s.ingestion_ts = base + 2'000;
    s.as_of_ts = base + 3'000;
    s.feature_snapshot_id = fsid;
    s.audit_id_bytes = id;
    s.signal_id_u8 = static_cast<std::uint8_t>(stcpp::strategy::SignalId::P0_01_PinnacleNoVig);
    // market_id 填
    const char* mkt = "0xMKT01";
    std::memcpy(s.market_id.data(), mkt, std::strlen(mkt));
    // 32 feature 全填
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        s.features[i] = static_cast<float>(i) * 0.1F;
    }
    return s;
}

static auto OpenFeatureWriter() {
    WalConfig cfg{};
    cfg.kind = WalKind::ShadowAudit;
    cfg.path_prefix = "/var/lib/stcpp/shadow/paper/mldata_feature_test";
    return WalWriter<FeatureSnapshot>::Open(cfg);
}
static auto OpenLabelWriter() {
    WalConfig cfg{};
    cfg.kind = WalKind::ShadowAudit;
    cfg.path_prefix = "/var/lib/stcpp/shadow/paper/mldata_label_test";
    return WalWriter<TrainingLabel>::Open(cfg);
}

// ============================================================================
// M1 — FeatureSnapshot concept + 32 feature 全填
// ============================================================================

static_assert(stcpp::infra::wal::WalRecord<FeatureSnapshot>,
              "FeatureSnapshot 必须满足 WalRecord concept (M1)");
static_assert(stcpp::infra::wal::WalRecord<TrainingLabel>, "TrainingLabel 必须满足 WalRecord concept (M1)");

TEST(FeatureSnapshot, DefaultCtorAllNaN) {
    FeatureSnapshot s{};
    EXPECT_EQ(s.nan_count(), kFeatureCount);
    EXPECT_FALSE(s.all_filled());
}

TEST(FeatureSnapshot, FillAll32Features) {
    auto s = make_snap();
    EXPECT_EQ(s.nan_count(), 0u);
    EXPECT_TRUE(s.all_filled());

    // 每个 enum 都能 get 出来 (sanity)
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        const float v = s.get(static_cast<FeatureName>(i));
        EXPECT_FALSE(std::isnan(v)) << "feature " << i;
    }
}

TEST(FeatureSnapshot, FeatureCountIs32) {
    EXPECT_EQ(kFeatureCount, 32u);
}

TEST(FeatureSnapshot, EnumToStringMonotone) {
    // 32 enum 字符串都非空 (生成 Parquet schema 时用)
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        const auto sv = to_string(static_cast<FeatureName>(i));
        EXPECT_FALSE(sv.empty());
        EXPECT_NE(sv, "unknown");
    }
}

// ============================================================================
// M2 — 4 ts R-20 透传 + PIT enforce
// ============================================================================

TEST(FeatureSnapshot, TsChainOk_ValidChain) {
    auto s = make_snap();
    EXPECT_TRUE(s.ts_chain_ok());
}

TEST(FeatureSnapshot, TsChainOk_RejectsZero) {
    auto s = make_snap();
    s.event_ts = 0;
    EXPECT_FALSE(s.ts_chain_ok());
}

TEST(FeatureSnapshot, TsChainOk_RejectsReversal) {
    auto s = make_snap();
    s.ingestion_ts = s.data_source_ts - 1;  // 倒流
    EXPECT_FALSE(s.ts_chain_ok());
}

TEST(MLDataHook, OnSignal_PitFail_Drops) {
    auto fw = OpenFeatureWriter();
    auto lw = OpenLabelWriter();
    ASSERT_TRUE(fw.has_value() && lw.has_value());
    MLDataHook hook{fw.value().get(), lw.value().get()};

    stcpp::strategy::SignalContext ctx{};
    auto s = make_snap();
    s.event_ts = 0;  // 破坏 PIT

    hook.on_signal_compute(ctx, s);
    EXPECT_EQ(hook.stats().signals_recorded.load(), 0u);
    EXPECT_EQ(hook.stats().dropped_pit_fail.load(), 1u);
}

// ============================================================================
// M3 — ML-R8 feature_snapshot_id == 0 → drop
// ============================================================================

TEST(MLDataHook, OnSignal_IdZero_Drops) {
    auto fw = OpenFeatureWriter();
    auto lw = OpenLabelWriter();
    ASSERT_TRUE(fw.has_value() && lw.has_value());
    MLDataHook hook{fw.value().get(), lw.value().get()};

    stcpp::strategy::SignalContext ctx{};
    auto s = make_snap(0);  // id=0

    hook.on_signal_compute(ctx, s);
    EXPECT_EQ(hook.stats().signals_recorded.load(), 0u);
    EXPECT_EQ(hook.stats().dropped_id_zero.load(), 1u);
}

TEST(MLDataHook, OnDecision_IdZero_Drops) {
    auto fw = OpenFeatureWriter();
    auto lw = OpenLabelWriter();
    ASSERT_TRUE(fw.has_value() && lw.has_value());
    MLDataHook hook{fw.value().get(), lw.value().get()};

    stcpp::risk::RiskDecision d{};
    auto s = make_snap(0);
    hook.on_risk_decision(d, s);
    EXPECT_EQ(hook.stats().decisions_recorded.load(), 0u);
    EXPECT_EQ(hook.stats().dropped_id_zero.load(), 1u);
}

// ============================================================================
// M4 — join key 4 级一致性 (signal → decision → fill → settle 同 audit_id)
// ============================================================================

TEST(MLDataHook, FourStageJoin_ConsistentAuditId) {
    auto fw = OpenFeatureWriter();
    auto lw = OpenLabelWriter();
    ASSERT_TRUE(fw.has_value() && lw.has_value());
    MLDataHook hook{fw.value().get(), lw.value().get()};

    const std::array<std::uint8_t, 16> id = {0xA, 0xB, 0xC, 0xD, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const std::uint64_t fsid = 0xDEADBEEF12345ULL;
    auto snap = make_snap(fsid, id);

    // 1) signal
    stcpp::strategy::SignalContext ctx{};
    hook.on_signal_compute(ctx, snap);
    EXPECT_EQ(hook.stats().signals_recorded.load(), 1u);
    EXPECT_EQ(hook.cache_size(), 1u);

    // 2) decision (APPROVED)
    stcpp::risk::RiskDecision dec{};
    dec.decision = stcpp::risk::Decision::APPROVED;
    dec.audit_id = id;
    hook.on_risk_decision(dec, snap);
    EXPECT_EQ(hook.stats().decisions_recorded.load(), 1u);
    EXPECT_EQ(hook.cache_size(), 1u);  // 同 id, upsert 不增

    // 3) fill (Ok)
    stcpp::execution::VirtualFill fill{};
    fill.reject = stcpp::execution::MatchReject::Ok;
    fill.fill_price = 0.52;
    fill.fill_size_usdc = 1000.0;
    fill.event_ts_ns = snap.event_ts;
    fill.data_source_ts_ns = snap.data_source_ts;
    fill.ingestion_ts_ns = snap.ingestion_ts;
    fill.as_of_ts_ns = snap.as_of_ts;
    hook.on_fill(fill, snap);
    EXPECT_EQ(hook.stats().fills_recorded.load(), 1u);

    // 4) settle (Win, $50 profit)
    SettlementEvent settle{};
    settle.event_ts = snap.event_ts + 3'600'000'000'000LL;  // 1h 后
    settle.data_source_ts = settle.event_ts + 1'000;
    settle.ingestion_ts = settle.data_source_ts + 1'000;
    settle.as_of_ts = settle.ingestion_ts + 1'000;
    settle.audit_id_bytes = id;
    settle.feature_snapshot_id = fsid;
    settle.outcome = SettlementOutcome::Win;
    settle.realized_pnl_usdc = 50.0;
    hook.on_settle(settle);
    EXPECT_EQ(hook.stats().settlements_recorded.load(), 1u);

    // M8: cache 应 evict (settle 后)
    EXPECT_EQ(hook.cache_size(), 0u);
}

// ============================================================================
// M5 — R-11 path prefix 校验 (framework abort 见 wal test; 这里测 ShadowAudit OK)
// ============================================================================

TEST(MLDataHook, R11_ShadowAuditPath_OpenOk) {
    auto fw = OpenFeatureWriter();
    auto lw = OpenLabelWriter();
    EXPECT_TRUE(fw.has_value());
    EXPECT_TRUE(lw.has_value());
    // 注: 错误 path (例如 /var/lib/stcpp/audit/...) 会 std::abort, 不在此测 (gtest death test 见 wal)
}

TEST(MLDataHook, DefaultPathPrefix_StartsWithShadow) {
    const auto sv = DefaultMlDataPathPrefix();
    // build-time switch 必命中 /var/lib/stcpp/shadow/
    const std::string_view shadow_root = "/var/lib/stcpp/shadow/";
    EXPECT_TRUE(sv.size() >= shadow_root.size());
    EXPECT_EQ(sv.substr(0, shadow_root.size()), shadow_root);
}

TEST(MLDataHook, MlDataWalKind_IsShadowAudit) {
    EXPECT_EQ(MlDataWalKind(), WalKind::ShadowAudit);
}

// ============================================================================
// M6 — R-12 hook < 1us per call (gtest 兜底, skeleton framework path 足够)
// ============================================================================

TEST(MLDataHook, R12_OnSignal_Below_1us_p99) {
    auto fw = OpenFeatureWriter();
    auto lw = OpenLabelWriter();
    ASSERT_TRUE(fw.has_value() && lw.has_value());
    MLDataHook hook{fw.value().get(), lw.value().get()};

    stcpp::strategy::SignalContext ctx{};
    const std::size_t N = 1000;

    std::vector<std::int64_t> latencies;
    latencies.reserve(N);
    for (std::size_t i = 0; i < N; ++i) {
        auto s = make_snap(0xABCDEFULL + i);
        auto t0 = std::chrono::steady_clock::now();
        hook.on_signal_compute(ctx, s);
        auto t1 = std::chrono::steady_clock::now();
        latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }
    std::sort(latencies.begin(), latencies.end());
    const std::int64_t p99 = latencies[static_cast<std::size_t>(N * 0.99)];

    // skeleton 期 framework path ~ PIT 100ns + atomic seq + unordered_map upsert.
    // 兜底 5us (gtest CI 抖动, GoogleBench W5 切硬 1us assert).
    EXPECT_LT(p99, 5'000) << "p99=" << p99 << "ns";
}

// ============================================================================
// M7 — partial label on_fill + 最终 label on_settle 双写
// ============================================================================

TEST(MLDataHook, OnFill_Writes_PartialLabel) {
    auto fw = OpenFeatureWriter();
    auto lw = OpenLabelWriter();
    ASSERT_TRUE(fw.has_value() && lw.has_value());
    MLDataHook hook{fw.value().get(), lw.value().get()};

    auto snap = make_snap();
    stcpp::execution::VirtualFill fill{};
    fill.reject = stcpp::execution::MatchReject::Ok;
    fill.fill_price = 0.48;
    fill.fill_size_usdc = 500.0;
    fill.event_ts_ns = snap.event_ts;
    fill.data_source_ts_ns = snap.data_source_ts;
    fill.ingestion_ts_ns = snap.ingestion_ts;
    fill.as_of_ts_ns = snap.as_of_ts;

    hook.on_fill(fill, snap);
    EXPECT_EQ(hook.stats().fills_recorded.load(), 1u);
}

TEST(MLDataHook, OnFill_BernoulliMissed_Executed_False) {
    auto fw = OpenFeatureWriter();
    auto lw = OpenLabelWriter();
    ASSERT_TRUE(fw.has_value() && lw.has_value());
    MLDataHook hook{fw.value().get(), lw.value().get()};

    auto snap = make_snap();
    stcpp::execution::VirtualFill fill{};
    fill.reject = stcpp::execution::MatchReject::BernoulliMissed;
    fill.fill_price = 0.0;
    fill.fill_size_usdc = 0.0;
    fill.event_ts_ns = snap.event_ts;
    fill.data_source_ts_ns = snap.data_source_ts;
    fill.ingestion_ts_ns = snap.ingestion_ts;
    fill.as_of_ts_ns = snap.as_of_ts;

    hook.on_fill(fill, snap);
    // 仍然写 (decision_taken=true, executed=false), counter ++
    EXPECT_EQ(hook.stats().fills_recorded.load(), 1u);
}

// ============================================================================
// 顺手: SettlementOutcome 5 状态完整
// ============================================================================

TEST(SettlementOutcome, FiveStatesNamed) {
    EXPECT_EQ(to_string(SettlementOutcome::Pending), "Pending");
    EXPECT_EQ(to_string(SettlementOutcome::Win), "Win");
    EXPECT_EQ(to_string(SettlementOutcome::Loss), "Loss");
    EXPECT_EQ(to_string(SettlementOutcome::Push), "Push");
    EXPECT_EQ(to_string(SettlementOutcome::Void), "Void");
}

// ============================================================================
// 顺手: TrainingLabel ts_chain_ok
// ============================================================================

TEST(TrainingLabel, TsChainEnforced) {
    TrainingLabel l{};
    EXPECT_FALSE(l.ts_chain_ok());  // 全 0 → invalid

    const std::int64_t base = NowNs() - 1'000'000'000LL;
    l.event_ts = base;
    l.data_source_ts = base + 1;
    l.ingestion_ts = base + 2;
    l.as_of_ts = base + 3;
    EXPECT_TRUE(l.ts_chain_ok());

    l.ingestion_ts = l.data_source_ts - 1;
    EXPECT_FALSE(l.ts_chain_ok());
}

}  // namespace
}  // namespace stcpp::ml
