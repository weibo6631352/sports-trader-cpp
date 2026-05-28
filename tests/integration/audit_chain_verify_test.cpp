// tests/integration/audit_chain_verify_test.cpp — audit chain integration verify
//
// Owner: 小宋 (W5 Wave 24) / 老唐更新 (W6 Wave 29 BLAKE3_REAL 配套)
// 关联:
//   docs/RESEARCH/laotang-audit-schema-v1.1.md §4 (hash chain)
//   docs/RESEARCH/xiaoying-acceptance-spec-v1.md
//     - M1-A06 audit chain hash 不可篡改 (篡改任意 record → verify 失败)
//     - M1-E03 audit chain hash 全过 (72h 0 mismatch)
//
// W6 Wave 29 更新 (老唐):
//   BLAKE3_REAL=1 后 RecomputeDigest/XorChain 替换为 Blake3Hasher 重算.
//   W5 XOR stub 路径保留在 #else 分支 (向后兼容测试历史).
//
// 验证:
//   T1: 20 笔 audit record, last_hash 链式累积 + 本地重算一致
//   T2: 篡改任意 record decision_ts → 重算链失败定位 (M1-A06)
//   T3: 4 ts R-20 全链路 PIT 拦截
//   T4: chain 在 emit 后单调推进

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_emitter.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/observability/blake3_hash.hpp"
#include "stcpp/risk/reject_enum.hpp"

namespace stcpp::test::integration {
namespace {

using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::observability::AuditEmitter;
using stcpp::observability::AuditEventType;
using stcpp::observability::AuditRecord;
using stcpp::observability::Blake3Hasher;
using stcpp::observability::RiskDecisionInput;
using stcpp::observability::kHashBytes;

static std::int64_t NowNs() noexcept {
    return stcpp::infra::wal::pit::NowRealtimeNs();
}

// W6 Wave 29: BLAKE3_REAL 后用真 BLAKE3 重算, W5 XOR stub 保留 #else 分支
[[nodiscard]] Blake3Hasher::Hash256 RecomputePayload(
    std::uint64_t seq, AuditEventType type, std::int64_t decision_ts) noexcept {
#if defined(BLAKE3_REAL)
    return Blake3Hasher::compute_payload_hash(
        seq, static_cast<std::uint8_t>(type), decision_ts);
#else
    // W5 XOR stub 重算
    Blake3Hasher::Hash256 out{};
    std::memcpy(out.data(),     &seq,         sizeof(seq));
    out[8] = static_cast<std::uint8_t>(type);
    std::memcpy(out.data() + 9, &decision_ts, sizeof(decision_ts));
    return out;
#endif
}

[[nodiscard]] Blake3Hasher::Hash256 ChainCombine(
    const Blake3Hasher::Hash256& prev,
    const Blake3Hasher::Hash256& payload) noexcept {
#if defined(BLAKE3_REAL)
    return Blake3Hasher::hash_chain(prev, payload);
#else
    Blake3Hasher::Hash256 out{};
    for (std::size_t i = 0; i < kHashBytes; ++i) {
        out[i] = static_cast<std::uint8_t>(prev[i] ^ payload[i]);
    }
    return out;
#endif
}

// 构造 R-20 4 ts 合法 RiskDecisionInput
RiskDecisionInput MakeValidCtx(int i, AuditEventType type) {
    const std::int64_t now  = NowNs();
    const std::int64_t base = now - 1'000'000'000LL;   // 1s ago 容差
    RiskDecisionInput in{};
    in.event_ts        = base;
    in.data_source_ts  = base + 100;
    in.ingestion_ts    = base + 200;
    in.as_of_ts        = base + 300;
    in.decision_ts     = base + 400;
    in.audit_id_bytes  = {
        static_cast<std::uint8_t>(i),  1, 2, 3, 4, 5, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15};
    in.market_id       = "mkt_chain_verify";
    in.strategy_id     = "strat_chain";
    in.size_usdc       = 100 + i;
    in.price           = 0.5 + 0.001 * i;
    in.is_buy          = (i % 2) == 0;
    in.event_type      = type;
    in.reject_code     = stcpp::risk::RejectCode::INTERNAL_ERROR;
    in.sub_reason      = stcpp::risk::InvalidIntentSubReason::NONE;
    return in;
}

auto OpenPaperWriter() {
    WalConfig cfg{};
    cfg.kind        = WalKind::PaperAudit;
    cfg.path_prefix = "/var/lib/stcpp/paper/audit_chain_verify";
    return WalWriter<AuditRecord>::Open(cfg);
}

// ---- T1: 20 笔 emit, chain head 推进, 每条 prev→current 链一致 -------------
TEST(AuditChainIntegration, T1_emit_20_records_chain_advances) {
    auto wres = OpenPaperWriter();
    ASSERT_TRUE(wres);
    auto writer = std::move(wres).value();
    AuditEmitter emitter(writer.get());

    constexpr int kN = 20;

    // 本地 mirror chain — 与 emitter 内部一致 (验证算法可还原)
    Blake3Hasher::Hash256 mirror_prev{};   // chain 起点 = 全 0

    for (int i = 0; i < kN; ++i) {
        auto ctx = MakeValidCtx(i, AuditEventType::OrderApproved);
        const auto r = emitter.emit_decision(ctx);
        ASSERT_TRUE(r) << "emit_decision #" << i << " 失败: "
                       << static_cast<int>(r.error());
        // 算 expected: ChainCombine(mirror_prev, RecomputePayload(...))
        // W6: 真 BLAKE3 重算; W5 stub: XOR 重算 (Blake3Hasher 内 #if dispatch)
        const auto seq = static_cast<std::uint64_t>(i + 1);
        const auto digest = RecomputePayload(seq, AuditEventType::OrderApproved,
                                              ctx.decision_ts);
        const auto expected_current = ChainCombine(mirror_prev, digest);
        EXPECT_EQ(emitter.last_hash(), expected_current)
            << "M1-A06 / M1-E03: chain head 第 " << i << " 笔与本地 mirror 一致";
        mirror_prev = expected_current;
    }
    EXPECT_EQ(emitter.emitted_count(), static_cast<std::uint64_t>(kN));
    EXPECT_GE(writer->HighWatermark(), static_cast<std::uint64_t>(kN));
}

// ---- T2: 篡改某条 payload_hash → 重算链失败 --------------------------------
TEST(AuditChainIntegration, T2_tamper_detection_via_recompute) {
    auto wres = OpenPaperWriter();
    ASSERT_TRUE(wres);
    auto writer = std::move(wres).value();
    AuditEmitter emitter(writer.get());

    constexpr int kN = 20;

    // 记录每笔 (ctx + post-emit chain head)
    struct Snapshot {
        RiskDecisionInput  ctx;
        Blake3Hasher::Hash256 chain_after;
    };
    std::vector<Snapshot> snaps;
    snaps.reserve(kN);

    for (int i = 0; i < kN; ++i) {
        auto ctx = MakeValidCtx(i, AuditEventType::OrderApproved);
        const auto r = emitter.emit_decision(ctx);
        ASSERT_TRUE(r);
        snaps.push_back({ctx, emitter.last_hash()});
    }

    // 重算 chain (mirror) 与 snaps 比对, 全过.
    {
        Blake3Hasher::Hash256 prev{};
        for (std::size_t i = 0; i < static_cast<std::size_t>(kN); ++i) {
            const auto seq = static_cast<std::uint64_t>(i + 1);
            const auto dig = RecomputePayload(seq, AuditEventType::OrderApproved,
                                               snaps[i].ctx.decision_ts);
            prev = ChainCombine(prev, dig);
            EXPECT_EQ(prev, snaps[i].chain_after)
                << "未篡改 mirror 必与 emitter chain 全等 i=" << i;
        }
    }

    // 篡改第 10 条 decision_ts → 重算从 #10 起的 chain 必与 snaps 不一致.
    {
        constexpr std::size_t kTamperIdx = 10;
        Blake3Hasher::Hash256 prev{};
        bool mismatch_seen = false;
        std::size_t mismatch_idx = static_cast<std::size_t>(-1);
        for (std::size_t i = 0; i < static_cast<std::size_t>(kN); ++i) {
            const auto seq = static_cast<std::uint64_t>(i + 1);
            // 攻击者改 snaps[10] decision_ts; chain 重算从这里之后必不一致.
            const std::int64_t ts =
                (i == kTamperIdx) ? (snaps[i].ctx.decision_ts + 1)
                                  : snaps[i].ctx.decision_ts;
            const auto dig = RecomputePayload(seq, AuditEventType::OrderApproved, ts);
            prev = ChainCombine(prev, dig);
            if (prev != snaps[i].chain_after) {
                if (!mismatch_seen) {
                    mismatch_idx = i;
                    mismatch_seen = true;
                }
            }
        }
        EXPECT_TRUE(mismatch_seen)
            << "M1-A06: 篡改任意 record 必被 chain verify 检出";
        EXPECT_EQ(mismatch_idx, kTamperIdx)
            << "M1-A06: chain 首次不一致定位到被篡改 record (定位准确)";
    }
}

// ---- T3: 4 ts R-20 全链路 (PIT 拦截) ---------------------------------------
TEST(AuditChainIntegration, T3_R20_4ts_full_chain_PIT_blocks_violation) {
    auto wres = OpenPaperWriter();
    ASSERT_TRUE(wres);
    auto writer = std::move(wres).value();
    AuditEmitter emitter(writer.get());

    // happy path: 4 ts 单调
    {
        auto ctx = MakeValidCtx(0, AuditEventType::OrderApproved);
        EXPECT_TRUE(emitter.emit_decision(ctx));
    }

    // violation A: ds < event
    {
        auto ctx = MakeValidCtx(1, AuditEventType::OrderApproved);
        ctx.data_source_ts = ctx.event_ts - 1;
        const auto r = emitter.emit_decision(ctx);
        EXPECT_FALSE(r);
        EXPECT_EQ(r.error(), stcpp::infra::wal::WalError::PitViolation);
    }
    // violation B: ingestion < ds
    {
        auto ctx = MakeValidCtx(2, AuditEventType::OrderApproved);
        ctx.ingestion_ts = ctx.data_source_ts - 1;
        const auto r = emitter.emit_decision(ctx);
        EXPECT_FALSE(r);
        EXPECT_EQ(r.error(), stcpp::infra::wal::WalError::PitViolation);
    }
    // violation C: as_of < ingestion
    {
        auto ctx = MakeValidCtx(3, AuditEventType::OrderApproved);
        ctx.as_of_ts = ctx.ingestion_ts - 1;
        const auto r = emitter.emit_decision(ctx);
        EXPECT_FALSE(r);
        EXPECT_EQ(r.error(), stcpp::infra::wal::WalError::PitViolation);
    }
    // violation D: event_ts == 0
    {
        auto ctx = MakeValidCtx(4, AuditEventType::OrderApproved);
        ctx.event_ts = 0;
        const auto r = emitter.emit_decision(ctx);
        EXPECT_FALSE(r);
        EXPECT_EQ(r.error(), stcpp::infra::wal::WalError::PitViolation);
    }
}

// ---- T4: chain 在 emit 后单调推进 (last_hash 与上一条一致) ----------------
TEST(AuditChainIntegration, T4_chain_head_matches_last_record_current_hash) {
    auto wres = OpenPaperWriter();
    ASSERT_TRUE(wres);
    auto writer = std::move(wres).value();
    AuditEmitter emitter(writer.get());

    Blake3Hasher::Hash256 prev = emitter.last_hash();
    // 起点 chain head 应全 0 (laotang v1.1 §4)
    Blake3Hasher::Hash256 zero{};
    EXPECT_EQ(prev, zero) << "chain 起点 last_hash 必全 0";

    for (int i = 0; i < 5; ++i) {
        auto ctx = MakeValidCtx(i, AuditEventType::OrderApproved);
        EXPECT_TRUE(emitter.emit_decision(ctx));
        const auto cur = emitter.last_hash();
        EXPECT_NE(cur, prev) << "chain head 每 emit 必推进";
        prev = cur;
    }
    EXPECT_EQ(emitter.emitted_count(), 5u);
}

}  // namespace
}  // namespace stcpp::test::integration
