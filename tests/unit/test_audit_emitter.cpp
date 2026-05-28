// tests/unit/test_audit_emitter.cpp — AuditEmitter v0.1 单测 (W4 Wave 19)
//
// 覆盖:
//   A1  12 AET 各 1 emit case (含 STRATEGY_DECAYED / UNLOCK)
//   A2  hash chain 3 record 串联校验 (prev → current → prev_next)
//   A3  4 ts PIT 不等式 emit 前必拦 (R-20)
//   A4  老韩 invariant: sub_reason 仅 INVALID_INTENT 时非 NONE
//   A5  R-11: AuditRecord 满足 WalRecord concept (编译期) + WalWriter<AuditRecord> 实例化通过
//   A6  emit_decision 误用 → 状态机入口 (StrategyDecayed) 拒绝 (Io)
//   A7  decision_ts < as_of_ts 拒绝 (R-20 扩展不等式)
//
// 注: 不引入 mock framework, 直接复用 stcpp::infra::wal::WalWriter<AuditRecord>
//     skeleton (path prefix + PIT + watermark 闭环, hash chain stub 走 emitter 内部).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <string_view>
#include <vector>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_emitter.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/risk/reject_enum.hpp"

// 复用 emitter.cpp 已有的模板实例化 (避免链接错误).
// 单测 TU 也带 wal_writer.cpp 让 WalWriter<MockRecord> 在别处仍可独立编译.
#include "../../src/stcpp/observability/audit_emitter.cpp"  // emitter + 模板实例化

namespace stcpp::observability {
namespace {

using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalError;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::risk::InvalidIntentSubReason;
using stcpp::risk::RejectCode;

static std::int64_t NowNs() noexcept {
    return stcpp::infra::wal::pit::NowRealtimeNs();
}

// 构造 R-20 合法 + decision_ts 合法的输入
RiskDecisionInput make_valid_input(AuditEventType type = AuditEventType::OrderApproved) {
    const std::int64_t now = NowNs();
    const std::int64_t base = now - 1'000'000'000LL;  // 1s ago, 给 4 ts + decision_ts 容差
    RiskDecisionInput in{};
    in.event_ts       = base;
    in.data_source_ts = base + 1'000;
    in.ingestion_ts   = base + 2'000;
    in.as_of_ts       = base + 3'000;
    in.decision_ts    = base + 4'000;
    in.audit_id_bytes = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    in.market_id      = "mkt_test_001";
    in.strategy_id    = "strat_alpha";
    in.size_usdc      = 1000;
    in.price          = 0.55;
    in.is_buy         = true;
    in.event_type     = type;
    in.reject_code    = RejectCode::INTERNAL_ERROR;
    in.sub_reason     = InvalidIntentSubReason::NONE;
    return in;
}

// helper: 起 paper kind writer (skeleton path prefix R-11 + PIT R-20 闭环)
static auto OpenWriter() {
    WalConfig cfg{};
    cfg.kind        = WalKind::PaperAudit;
    cfg.path_prefix = "/var/lib/stcpp/paper/audit_emitter_test";
    return WalWriter<AuditRecord>::Open(cfg);
}

// ---------- A5 concept + 实例化 (编译期) -------------------------------

static_assert(stcpp::infra::wal::WalRecord<AuditRecord>,
              "A5: AuditRecord 必须满足 WalRecord concept");

TEST(AuditRecord, ConceptAndLayout) {
    EXPECT_EQ(kAuditEventTypeCount, 12u);
    AuditRecord r{};
    EXPECT_EQ(r.event_ts_ns(), 0);
    EXPECT_EQ(r.max_serialized_size(), sizeof(AuditRecord));
}

TEST(AuditRecord, BuildTimeWalKind) {
    // paper / live 二选一 (build-time). 不能是 RiskAudit + paper 同进程.
    const auto k = AuditWalKindForBuild();
    EXPECT_TRUE(k == WalKind::PaperAudit || k == WalKind::RiskAudit);
    // path 必命中白名单 (R-11)
    const auto root = AuditWalPathRootForBuild();
    EXPECT_FALSE(root.empty());
}

// ---------- A1 12 AET 各 1 emit case -------------------------------------

TEST(AuditEmitter, Emit_OrderApproved) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input(AuditEventType::OrderApproved);
    auto r = em.emit_decision(in);
    ASSERT_TRUE(r.has_value()) << "WalError=" << static_cast<int>(r.error());
    EXPECT_EQ(em.emitted_count(), 1u);
}

TEST(AuditEmitter, Emit_OrderRejected_With_SubReason) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input(AuditEventType::OrderRejected);
    in.reject_code = RejectCode::INVALID_INTENT;
    in.sub_reason  = InvalidIntentSubReason::BOOK_TS_STALE;
    auto r = em.emit_decision(in);
    ASSERT_TRUE(r.has_value());
}

TEST(AuditEmitter, Emit_OrderDeferred) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto r = em.emit_decision(make_valid_input(AuditEventType::OrderDeferred));
    ASSERT_TRUE(r.has_value());
}

TEST(AuditEmitter, Emit_OrderFilled) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto r = em.emit_decision(make_valid_input(AuditEventType::OrderFilled));
    ASSERT_TRUE(r.has_value());
}

TEST(AuditEmitter, Emit_OrderCancelled) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto r = em.emit_decision(make_valid_input(AuditEventType::OrderCancelled));
    ASSERT_TRUE(r.has_value());
}

TEST(AuditEmitter, Emit_OrderTimedOut) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto r = em.emit_decision(make_valid_input(AuditEventType::OrderTimedOut));
    ASSERT_TRUE(r.has_value());
}

TEST(AuditEmitter, Emit_SafeModeEnter) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto r = em.emit_safe_mode_enter(make_valid_input(), "fsync_failed");
    ASSERT_TRUE(r.has_value());
}

TEST(AuditEmitter, Emit_SafeModeExit) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto r = em.emit_safe_mode_exit(make_valid_input());
    ASSERT_TRUE(r.has_value());
}

TEST(AuditEmitter, Emit_StateTransition) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto r = em.emit_state_transition(make_valid_input(), "DRAIN->HALT");
    ASSERT_TRUE(r.has_value());
}

TEST(AuditEmitter, Emit_StrategyDecayed) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input();
    in.reject_code = RejectCode::STRATEGY_DECAYED;  // 老韩 #19
    auto r = em.emit_strategy_decayed(in);
    ASSERT_TRUE(r.has_value());
}

TEST(AuditEmitter, Emit_Unlock) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto r = em.emit_unlock(make_valid_input(), "op_laotang");
    ASSERT_TRUE(r.has_value());
}

TEST(AuditEmitter, Emit_ReconDrift) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto r = em.emit_recon_drift(make_valid_input(), "pos_drift_5usdc");
    ASSERT_TRUE(r.has_value());
}

// ---------- A2 hash chain 3 record 串联 ----------------------------------

TEST(AuditEmitter, HashChain_3Record_Linked) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};

    // chain 起点 = 全 0
    const std::array<std::uint8_t, kHashBytes> zero{};
    EXPECT_EQ(em.last_hash(), zero);

    // 第 1 条
    ASSERT_TRUE(em.emit_decision(make_valid_input(AuditEventType::OrderApproved)).has_value());
    const auto h1 = em.last_hash();
    EXPECT_NE(h1, zero) << "current_hash 全 0 表示 chain stub 未生效";

    // 第 2 条 — prev_hash 应 = h1
    ASSERT_TRUE(em.emit_decision(make_valid_input(AuditEventType::OrderApproved)).has_value());
    const auto h2 = em.last_hash();
    EXPECT_NE(h2, h1);
    EXPECT_NE(h2, zero);

    // 第 3 条
    ASSERT_TRUE(em.emit_decision(make_valid_input(AuditEventType::OrderFilled)).has_value());
    const auto h3 = em.last_hash();
    EXPECT_NE(h3, h2);
    EXPECT_NE(h3, h1);

    EXPECT_EQ(em.emitted_count(), 3u);
}

// ---------- A3 R-20: 4 ts emit 前必拦 ------------------------------------

TEST(AuditEmitter, PitChain_EventTsZero_Rejected) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input();
    in.event_ts = 0;     // 触发 BOOK_TS_ZERO
    auto r = em.emit_decision(in);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), WalError::PitViolation);
    EXPECT_EQ(em.emitted_count(), 0u);
}

TEST(AuditEmitter, PitChain_DsBeforeEvent_Rejected) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input();
    in.data_source_ts = in.event_ts - 1;
    auto r = em.emit_decision(in);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), WalError::PitViolation);
}

TEST(AuditEmitter, PitChain_IngestionBeforeDs_Rejected) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input();
    in.ingestion_ts = in.data_source_ts - 1;
    auto r = em.emit_decision(in);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), WalError::PitViolation);
}

TEST(AuditEmitter, PitChain_AsOfBeforeIngestion_Rejected) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input();
    in.as_of_ts = in.ingestion_ts - 1;
    auto r = em.emit_decision(in);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), WalError::PitViolation);
}

// ---------- A7 decision_ts < as_of_ts 拒绝 -------------------------------

TEST(AuditEmitter, DecisionTsBeforeAsOf_Rejected) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input();
    in.decision_ts = in.as_of_ts - 1;
    auto r = em.emit_decision(in);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), WalError::PitViolation);
}

// ---------- A4 老韩 invariant: sub_reason 仅 INVALID_INTENT 时非 NONE ----

TEST(AuditEmitter, RejectInvariant_NonInvalidIntentMustNoneSub) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input(AuditEventType::OrderRejected);
    in.reject_code = RejectCode::EXCEED_PER_ORDER_CAP;
    in.sub_reason  = InvalidIntentSubReason::BOOK_TS_STALE;  // 违反 invariant
    auto r = em.emit_decision(in);
    EXPECT_FALSE(r.has_value());
}

TEST(AuditEmitter, RejectInvariant_InvalidIntentWithSubOk) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input(AuditEventType::OrderRejected);
    in.reject_code = RejectCode::INVALID_INTENT;
    in.sub_reason  = InvalidIntentSubReason::TS_ORDER_VIOLATED;
    auto r = em.emit_decision(in);
    EXPECT_TRUE(r.has_value());
}

// ---------- A6 emit_decision 误用专门入口的 AET ---------------------------

TEST(AuditEmitter, EmitDecision_WrongType_Rejected) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};
    auto in = make_valid_input(AuditEventType::StrategyDecayed);  // 应走 emit_strategy_decayed
    auto r = em.emit_decision(in);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(em.emitted_count(), 0u);
}

// ---------- 联动 RiskGateway (mock) — 与老韩 RiskDecision 适配 -----------

TEST(AuditEmitter, RiskGatewayMock_RejectFlow) {
    auto w_or = OpenWriter();
    ASSERT_TRUE(w_or.has_value());
    AuditEmitter em{w_or.value().get()};

    // 模拟 RM 决策 reject(EDGE_CI_NEGATIVE)
    auto in = make_valid_input(AuditEventType::OrderRejected);
    in.reject_code = RejectCode::EDGE_CI_NEGATIVE;
    in.sub_reason  = InvalidIntentSubReason::NONE;
    auto r = em.emit_decision(in);
    ASSERT_TRUE(r.has_value());

    // 再模拟 RM 决策 reject(AUDIT_WAL_BACKPRESSURE) — chain 连下去
    in.reject_code = RejectCode::AUDIT_WAL_BACKPRESSURE;
    r = em.emit_decision(in);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(em.emitted_count(), 2u);
}

// ---------- AuditEmitterPool.PublicEmitWithInjectedChain (W7 Wave 33) --------
//
// 验证: emit_with_injected_chain public API 工作 — 5 emitter pool 各 emit 1 笔,
//       全局 chain 正确串联 (seq 1..5, 每条 current_hash = Blake3(prev||payload)).

TEST(AuditEmitterPool, PublicEmitWithInjectedChain) {
    using stcpp::observability::AuditEmitterPool;
    using stcpp::observability::AuditOrigin;
    using stcpp::observability::Blake3Hasher;

    // 开 5 个 writer (paper kind, 独立 path prefix)
    auto open_w = [](const char* prefix) {
        WalConfig cfg{};
        cfg.kind        = WalKind::PaperAudit;
        cfg.path_prefix = prefix;
        return WalWriter<AuditRecord>::Open(cfg);
    };

    auto w0 = open_w("/var/lib/stcpp/paper/pool_test_risk");
    auto w1 = open_w("/var/lib/stcpp/paper/pool_test_signer");
    auto w2 = open_w("/var/lib/stcpp/paper/pool_test_ml");
    auto w3 = open_w("/var/lib/stcpp/paper/pool_test_stats");
    auto w4 = open_w("/var/lib/stcpp/paper/pool_test_strategy");
    ASSERT_TRUE(w0 && w1 && w2 && w3 && w4);

    AuditEmitterPool pool(
        w0.value().get(), w1.value().get(), w2.value().get(),
        w3.value().get(), w4.value().get());

    // 全局 chain 起点 = 全 0
    const Blake3Hasher::Hash256 zero{};
    EXPECT_EQ(pool.global_last_hash(), zero);
    EXPECT_EQ(pool.global_seq(), 0u);

    // 5 个 origin 各 emit 1 笔 (轮流)
    const AuditOrigin origins[5] = {
        AuditOrigin::Risk, AuditOrigin::Signer, AuditOrigin::Ml,
        AuditOrigin::Stats, AuditOrigin::Strategy};

    Blake3Hasher::Hash256 mirror_prev{};
    for (int i = 0; i < 5; ++i) {
        auto ctx = make_valid_input(AuditEventType::OrderApproved);
        const auto r = pool.emit(origins[i], ctx);
        ASSERT_TRUE(r) << "pool.emit origin=" << i << " 失败: "
                       << static_cast<int>(r.error());

        // 验证全局 seq 递增
        EXPECT_EQ(pool.global_seq(), static_cast<std::uint64_t>(i + 1));

        // 验证 chain 串联: current = Blake3(prev || payload)
        const auto seq     = static_cast<std::uint64_t>(i + 1);
        const auto payload = Blake3Hasher::compute_payload_hash(
            seq, static_cast<std::uint8_t>(AuditEventType::OrderApproved), ctx.decision_ts);
        const auto expected = Blake3Hasher::hash_chain(mirror_prev, payload);
        EXPECT_EQ(pool.global_last_hash(), expected)
            << "chain step " << i << " 不一致 (public API chain 注入验证)";
        mirror_prev = expected;
    }

    // hash_chain_verify_global 验证全局 chain 连续
    EXPECT_TRUE(pool.hash_chain_verify_global(1, 5))
        << "M1-A06: pool 5 笔全局 chain verify 应通过";
}

}  // namespace
}  // namespace stcpp::observability
