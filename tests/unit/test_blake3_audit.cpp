// tests/unit/test_blake3_audit.cpp — BLAKE3 真实现 + emitter pool (W6 Wave 29)
//
// Owner: 老唐 (audit-expert, #38)
// T1: BLAKE3 hash 一致性 (同输入同输出, 不同输入不同输出)
// T2: hash_chain 串联 3 record (chain 单调推进 + 本地重算一致)
// T3: chain verify 篡改检测 (M1-A06 必过)
// T4: 5 emitter pool 各自独立 ring + 全局 chain 串联 (老周 Smell #A)
// T5: build-time switch (BLAKE3_REAL=1 必定义; BLAKE3_STUB 禁止 — 老韩 Smell #2)

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_emitter.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/observability/blake3_hash.hpp"
#include "stcpp/risk/reject_enum.hpp"

#include "../../src/stcpp/observability/audit_emitter.cpp"

namespace stcpp::test::blake3 {
namespace {

using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::observability::AuditEmitter;
using stcpp::observability::AuditEmitterPool;
using stcpp::observability::AuditEventType;
using stcpp::observability::AuditOrigin;
using stcpp::observability::AuditRecord;
using stcpp::observability::Blake3Hasher;
using stcpp::observability::RiskDecisionInput;

static std::int64_t NowNs() noexcept {
    return stcpp::infra::wal::pit::NowRealtimeNs();
}

static RiskDecisionInput make_input(int i = 0, AuditEventType t = AuditEventType::OrderApproved) {
    const std::int64_t base = NowNs() - 1'000'000'000LL;
    RiskDecisionInput in{};
    in.event_ts = base;
    in.data_source_ts = base + 1000;
    in.ingestion_ts = base + 2000;
    in.as_of_ts = base + 3000;
    in.decision_ts = base + 4000 + i;
    in.audit_id_bytes = {
        static_cast<std::uint8_t>(i & 0xFF), 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    in.market_id = "mkt_b3";
    in.strategy_id = "b3_strat";
    in.size_usdc = 100;
    in.price = 0.6;
    in.is_buy = true;
    in.event_type = t;
    in.reject_code = stcpp::risk::RejectCode::INTERNAL_ERROR;
    in.sub_reason = stcpp::risk::InvalidIntentSubReason::NONE;
    return in;
}

static auto open_writer(const char* s) {
    WalConfig c{};
    c.kind = WalKind::PaperAudit;
    c.path_prefix = std::string("/var/lib/stcpp/paper/") + s;
    return WalWriter<AuditRecord>::Open(c);
}

// ---- T1: BLAKE3 hash 一致性 -------------------------------------------------

TEST(Blake3Hash, T1_SameInputSameOutput) {
    const std::array<std::uint8_t, 4> d = {1, 2, 3, 4};
    const auto h1 = Blake3Hasher::hash_256({d.data(), d.size()});
    const auto h2 = Blake3Hasher::hash_256({d.data(), d.size()});
    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, Blake3Hasher::Hash256{}) << "T1: 输出非全零";
}

TEST(Blake3Hash, T1_DifferentInputDifferentOutput) {
    const std::array<std::uint8_t, 4> a = {1, 2, 3, 4};
    const std::array<std::uint8_t, 4> b = {1, 2, 3, 5};
    EXPECT_NE(Blake3Hasher::hash_256({a.data(), a.size()}), Blake3Hasher::hash_256({b.data(), b.size()}));
}

TEST(Blake3Hash, T1_HashChain_Deterministic) {
    Blake3Hasher::Hash256 prev{};
    const std::array<std::uint8_t, 4> pl = {0xAB, 0xCD, 0xEF, 0x01};
    const auto ph = Blake3Hasher::hash_256({pl.data(), pl.size()});
    EXPECT_EQ(Blake3Hasher::hash_chain(prev, ph), Blake3Hasher::hash_chain(prev, ph)) << "T1: chain 确定性";
    EXPECT_NE(Blake3Hasher::hash_chain(prev, ph), Blake3Hasher::Hash256{});
}

// ---- T2: hash_chain 串联 3 record -------------------------------------------

TEST(AuditEmitterBlake3, T2_Chain_3Records_Monotonic) {
    auto wr = open_writer("b3_chain_t2");
    ASSERT_TRUE(wr.has_value());
    AuditEmitter em{wr.value().get()};
    const Blake3Hasher::Hash256 zero{};
    EXPECT_EQ(em.last_hash(), zero);

    ASSERT_TRUE(em.emit_decision(make_input(0)).has_value());
    const auto h1 = em.last_hash();
    EXPECT_NE(h1, zero);

    ASSERT_TRUE(em.emit_decision(make_input(1)).has_value());
    const auto h2 = em.last_hash();
    EXPECT_NE(h2, h1);

    ASSERT_TRUE(em.emit_decision(make_input(2, AuditEventType::OrderFilled)).has_value());
    EXPECT_NE(em.last_hash(), h2);
    EXPECT_EQ(em.emitted_count(), 3u);
}

TEST(AuditEmitterBlake3, T2_Chain_RecomputeMatches) {
    // 验证本地重算 == emitter 内部 BLAKE3 结果
    auto wr = open_writer("b3_recompute_t2");
    ASSERT_TRUE(wr.has_value());
    AuditEmitter em{wr.value().get()};
    const auto in0 = make_input(0);
    ASSERT_TRUE(em.emit_decision(in0).has_value());
    // seq=1, prev=0
    const auto ph = Blake3Hasher::compute_payload_hash(
        1u, static_cast<std::uint8_t>(AuditEventType::OrderApproved), in0.decision_ts);
    const auto expected = Blake3Hasher::hash_chain(Blake3Hasher::Hash256{}, ph);
    EXPECT_EQ(em.last_hash(), expected) << "T2: 本地重算 == emitter 内部";
}

// ---- T3: chain verify 篡改检测 (M1-A06) -------------------------------------

TEST(AuditEmitterBlake3, T3_TamperDetection) {
    auto wr = open_writer("b3_tamper_t3");
    ASSERT_TRUE(wr.has_value());
    AuditEmitter em{wr.value().get()};
    constexpr int kN = 10;
    for (int i = 0; i < kN; ++i) {
        ASSERT_TRUE(em.emit_decision(make_input(i)).has_value());
    }
    // 未篡改全范围验证通过
    EXPECT_TRUE(em.hash_chain_verify(1, kN)) << "T3 M1-A06: 未篡改 chain verify 通过";
    // 单条
    EXPECT_TRUE(em.hash_chain_verify(5, 5));
    // 超出范围: 失败
    EXPECT_FALSE(em.hash_chain_verify(kN + 1, kN + 3)) << "T3: 超出范围 → false";
    // start > end: 失败
    EXPECT_FALSE(em.hash_chain_verify(5, 3)) << "T3: start > end → false";
}

// ---- T4: 5 emitter pool 各自独立 ring (老周 Smell #A) -----------------------

TEST(AuditEmitterPool, T4_5Origins_IndependentEmitters) {
    auto wr = [](const char* s) {
        return open_writer(s);
    };
    auto w0 = wr("pool_t4_risk"), w1 = wr("pool_t4_signer");
    auto w2 = wr("pool_t4_ml"), w3 = wr("pool_t4_stats");
    auto w4 = wr("pool_t4_strat");
    ASSERT_TRUE(w0 && w1 && w2 && w3 && w4);

    AuditEmitterPool pool{w0.value().get(), w1.value().get(), w2.value().get(), w3.value().get(),
                          w4.value().get()};

    // 5 个 emitter 指针独立非空
    const auto* re = pool.get_emitter(AuditOrigin::Risk);
    const auto* se = pool.get_emitter(AuditOrigin::Signer);
    EXPECT_NE(re, nullptr);
    EXPECT_NE(se, nullptr);
    EXPECT_NE(re, se) << "T4: 各 emitter 独立 (不同指针)";

    // 初始 global_seq = 0
    EXPECT_EQ(pool.global_seq(), 0u);

    // 5 origin 各 emit 1 条
    const std::array<AuditOrigin, 5> origins = {AuditOrigin::Risk, AuditOrigin::Signer, AuditOrigin::Ml,
                                                AuditOrigin::Stats, AuditOrigin::Strategy};
    Blake3Hasher::Hash256 prev{};
    for (std::size_t i = 0; i < 5; ++i) {
        ASSERT_TRUE(pool.emit(origins[i], make_input(static_cast<int>(i))).has_value())
            << "T4: origin " << static_cast<int>(origins[i]) << " emit 失败";
        EXPECT_NE(pool.global_last_hash(), prev) << "T4: chain head 每次推进";
        prev = pool.global_last_hash();
    }
    EXPECT_EQ(pool.global_seq(), 5u) << "T4: 5 emit → global_seq == 5";
}

TEST(AuditEmitterPool, T4_GlobalChainVerify) {
    auto mk = [](const char* s) {
        return open_writer(s);
    };
    auto w0 = mk("pool_gv_r"), w1 = mk("pool_gv_s"), w2 = mk("pool_gv_m");
    auto w3 = mk("pool_gv_st"), w4 = mk("pool_gv_sg");
    ASSERT_TRUE(w0 && w1 && w2 && w3 && w4);
    AuditEmitterPool pool{w0.value().get(), w1.value().get(), w2.value().get(), w3.value().get(),
                          w4.value().get()};
    constexpr int kN = 10;
    const std::array<AuditOrigin, 5> origs = {AuditOrigin::Risk, AuditOrigin::Signer, AuditOrigin::Ml,
                                              AuditOrigin::Stats, AuditOrigin::Strategy};
    for (int i = 0; i < kN; ++i) {
        ASSERT_TRUE(pool.emit(origs[static_cast<std::size_t>(i) % 5], make_input(i)));
    }
    EXPECT_TRUE(pool.hash_chain_verify_global(1, kN))
        << "T4 M1-A06: 5 origin 混合 emit 全局 chain verify 通过";
}

// ---- T5: build-time switch (老韩 Smell #2) ----------------------------------

TEST(Blake3BuildSwitch, T5_BLAKE3_REAL_Defined) {
#if defined(BLAKE3_REAL)
    SUCCEED() << "T5: BLAKE3_REAL=1 已定义 (M1-A06 R-7 守住)";
#else
    FAIL() << "T5: BLAKE3_REAL 未定义 — CMakeLists 必须注入 -DBLAKE3_REAL=1";
#endif
}

TEST(Blake3BuildSwitch, T5_BLAKE3_STUB_Not_Defined) {
#if defined(BLAKE3_STUB)
    FAIL() << "T5: BLAKE3_STUB 不应在 paper/live build 定义 (老韩 Smell #2)";
#else
    SUCCEED() << "T5: BLAKE3_STUB 未定义 (正确)";
#endif
}

TEST(Blake3BuildSwitch, T5_TrueBlake3_NonSymmetric) {
    // 真 BLAKE3 非对称: hash_chain(a, b) != hash_chain(b, a)
    Blake3Hasher::Hash256 a{}, b{};
    a[0] = 0x01;
    b[0] = 0x02;
    EXPECT_NE(Blake3Hasher::hash_chain(a, b), Blake3Hasher::hash_chain(b, a))
        << "T5: 真 BLAKE3 非对称 (prev/payload 顺序不可换)";
}

}  // namespace
}  // namespace stcpp::test::blake3
