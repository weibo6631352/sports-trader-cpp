// tests/replay/audit_chain/r_chain_05_golden_head_match.cpp — R-chain-05: golden chain head 对比
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.2.1
//
// 场景: 同一批 10 条 event replay 两遍 → 两次 chain head 完全一致
// 期望: 同 seed (相同 event 序列) → 同 chain head (确定性)
//
// OQ-03 处理: 真实生产 golden head 待 paper runtime 首次运行后产出;
//             本 case 用本地 emit 两遍 验 replay 确定性.

#include <gtest/gtest.h>

#include <cstdint>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_emitter.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/observability/blake3_hash.hpp"
#include "tests/replay/paper/audit_chain_replay_assertion.hpp"

namespace stcpp::test::replay {
namespace {

using stcpp::observability::AuditEmitter;
using stcpp::observability::AuditEventType;
using stcpp::observability::Blake3Hasher;
using stcpp::observability::RiskDecisionInput;
using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::observability::AuditRecord;

// 固定 decision_ts 生成 (确定性 replay 所需: 不依赖 wall clock)
static std::vector<std::int64_t> kFixedDecisionTs;

RiskDecisionInput MakeFixedCtx(int i, std::int64_t base_ts) {
    RiskDecisionInput ctx{};
    ctx.event_ts       = base_ts;
    ctx.data_source_ts = base_ts + 100;
    ctx.ingestion_ts   = base_ts + 200;
    ctx.as_of_ts       = base_ts + 300;
    ctx.decision_ts    = base_ts + 400 + static_cast<std::int64_t>(i) * 10;
    ctx.audit_id_bytes = {
        static_cast<std::uint8_t>(i), 0xAB, 0xCD, 0xEF, 1, 2, 3, 4,
        5, 6, 7, 8, 9, 10, 11, 12};
    ctx.market_id      = "mkt_golden";
    ctx.strategy_id    = "strat_golden";
    ctx.event_type     = AuditEventType::OrderApproved;
    ctx.reject_code    = stcpp::risk::RejectCode::INTERNAL_ERROR;
    ctx.sub_reason     = stcpp::risk::InvalidIntentSubReason::NONE;
    return ctx;
}

// R-chain-05: 同序列 replay 两遍 → chain head 完全一致 (确定性)
TEST(AuditChainReplay, R_chain_05_golden_head_deterministic_replay) {
    // 固定 base_ts (确保两次跑用同样的 event payload, 不依赖 wall clock 漂移)
    const std::int64_t base_ts = stcpp::infra::wal::pit::NowRealtimeNs() - 1'000'000'000LL;

    constexpr int kN = 10;

    // --- Round 1: emit + 记录 chain head ---
    Blake3Hasher::Hash256 round1_head{};
    {
        WalConfig cfg{};
        cfg.kind        = WalKind::PaperAudit;
        cfg.path_prefix = "/var/lib/stcpp/paper/r_chain_05_round1";
        auto wres = WalWriter<AuditRecord>::Open(cfg);
        ASSERT_TRUE(wres);
        auto writer = std::move(wres).value();
        AuditEmitter emitter(writer.get());

        for (int i = 0; i < kN; ++i) {
            auto ctx = MakeFixedCtx(i, base_ts);
            ASSERT_TRUE(emitter.emit_decision(ctx));
        }
        round1_head = emitter.last_hash();
        EXPECT_EQ(emitter.emitted_count(), static_cast<std::uint64_t>(kN));
    }

    // --- Round 2: 同序列 replay → AuditChainReplayAssertion mirror_prev 应与 round1_head 一致 ---
    {
        WalConfig cfg{};
        cfg.kind        = WalKind::PaperAudit;
        cfg.path_prefix = "/var/lib/stcpp/paper/r_chain_05_round2";
        auto wres = WalWriter<AuditRecord>::Open(cfg);
        ASSERT_TRUE(wres);
        auto writer = std::move(wres).value();
        AuditEmitter emitter(writer.get());

        // 设置 golden (round 1 的 chain head)
        AuditChainReplayAssertion chain_assert;
        chain_assert.set_golden(round1_head, static_cast<std::uint64_t>(kN));

        for (int i = 0; i < kN; ++i) {
            auto ctx = MakeFixedCtx(i, base_ts);
            ASSERT_TRUE(emitter.emit_decision(ctx));
            const bool ok = chain_assert.on_decision(
                static_cast<std::uint64_t>(i + 1),
                AuditEventType::OrderApproved,
                ctx.decision_ts,
                emitter.last_hash());
            EXPECT_TRUE(ok) << "R-chain-05: round2 第 " << i << " 笔 chain head 一致";
        }

        // finalize: mirror_prev 必须与 round1_head 一致
        const bool finalize_ok = chain_assert.finalize();
        EXPECT_TRUE(finalize_ok)
            << "R-chain-05: replay 结束 chain head 与 golden 一致 (确定性 replay)";
        EXPECT_EQ(chain_assert.violation_count(), 0u)
            << "R-chain-05: 0 chain violation";
        EXPECT_EQ(chain_assert.last_mirror_hash(), round1_head)
            << "R-chain-05: round2 mirror_head == round1_head (R-7 确定性)";
    }
}

}  // namespace
}  // namespace stcpp::test::replay
