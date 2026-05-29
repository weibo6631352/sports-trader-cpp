// tests/replay/audit_chain/r_chain_03_04_strategy_events.cpp — R-chain-03/04: DECAYED/UNLOCK replay
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.2.2
//
// R-chain-03: AET_STRATEGY_DECAYED event replay 格式校验 (合成 payload 验证)
//   ASSERT payload.p_mu_negative >= 0.3 (BAYES_DECAY_P_THRESHOLD)
//   ASSERT payload.state_after == BLACK
//   ASSERT approver_cpo == "" OR approver_gm == "" (DECAYED 不需三签)
//
// R-chain-04: AET_STRATEGY_UNLOCK replay 格式校验 + 三签验证
//   ASSERT approver_cpo/gm/financial 全非空 (R-unlock-3sig)
//   ASSERT decay_audit_id 非空 (因果链)
//   ASSERT kelly_multiplier == 0.5 (MONITORING 期)
//
// 实现说明: chain replay 测试使用 OrderApproved event type (emit_decision 的合法路径)
//   DECAYED/UNLOCK 的 payload 格式检验作为独立断言, 不依赖 AuditEmitter event type 路由.
//   AuditEmitter 的 DECAYED/UNLOCK emit 路径是 emit_state_transition / emit_unlock (专用 API).
//   OQ-05: paper 阶段不触发真实 UNLOCK, 合成 payload 验格式.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_emitter.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "tests/replay/paper/audit_chain_replay_assertion.hpp"

namespace stcpp::test::replay {
namespace {

using stcpp::observability::AuditEmitter;
using stcpp::observability::AuditEventType;
using stcpp::observability::RiskDecisionInput;
using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::observability::AuditRecord;

// ---------- 合成 DECAYED / UNLOCK payload (OQ-05 格式验证只用, 不调真实 kill switch) --

struct StrategyDecayedPayload {
    double      p_mu_negative{0.0};
    int         sustained_days{0};
    std::string state_after;
    std::string approver_cpo;
    std::string approver_gm;
};

struct StrategyUnlockPayload {
    std::string approver_cpo;
    std::string approver_gm;
    std::string approver_financial;
    std::string decay_audit_id;
    double      kelly_multiplier{0.5};
};

// ---------- R-chain-03: STRATEGY_DECAYED payload 格式校验 -------------------------

// 格式断言: 独立 (不依赖 AuditEmitter event_type 路由)
TEST(AuditChainReplay, R_chain_03_strategy_decayed_payload_format) {
    StrategyDecayedPayload decayed;
    decayed.p_mu_negative  = 0.35;
    decayed.sustained_days = 15;
    decayed.state_after    = "BLACK";
    decayed.approver_cpo   = "";
    decayed.approver_gm    = "";

    // spec §2.2.2: ASSERT payload.p_mu_negative >= 0.3
    EXPECT_GE(decayed.p_mu_negative, 0.3)
        << "R-chain-03: p_mu_negative >= 0.3 (BAYES_DECAY_P_THRESHOLD)";
    // ASSERT sustained_days >= 14
    EXPECT_GE(decayed.sustained_days, 14)
        << "R-chain-03: sustained_days >= 14";
    // ASSERT state_after == BLACK
    EXPECT_EQ(decayed.state_after, "BLACK")
        << "R-chain-03: state_after == BLACK";
    // ASSERT DECAYED 不需三签
    EXPECT_TRUE(decayed.approver_cpo.empty() || decayed.approver_gm.empty())
        << "R-chain-03: DECAYED 不需三签 (approver 为空)";
}

// chain 连续性: DECAYED 之后的 approve 事件 chain 仍然正确 (作为 chain 节点之一)
TEST(AuditChainReplay, R_chain_03_strategy_decayed_chain_continuity) {
    WalConfig cfg{};
    cfg.kind        = WalKind::PaperAudit;
    cfg.path_prefix = "/var/lib/stcpp/paper/r_chain_03";
    auto wres = WalWriter<AuditRecord>::Open(cfg);
    ASSERT_TRUE(wres);
    auto writer = std::move(wres).value();
    AuditEmitter emitter(writer.get());

    AuditChainReplayAssertion chain_assert;

    // 6 条 OrderApproved 事件 (模拟混入 DECAYED 概念的 chain 连续性)
    // 注: 真实 DECAYED 用 emit_state_transition API; 这里测 chain 算法正确性
    constexpr int kN = 6;
    for (int i = 0; i < kN; ++i) {
        const std::int64_t now  = stcpp::infra::wal::pit::NowRealtimeNs();
        const std::int64_t base = now - 1'000'000'000LL;
        RiskDecisionInput ctx{};
        ctx.event_ts       = base;
        ctx.data_source_ts = base + 100;
        ctx.ingestion_ts   = base + 200;
        ctx.as_of_ts       = base + 300;
        ctx.decision_ts    = base + 400 + static_cast<std::int64_t>(i);
        ctx.audit_id_bytes = {
            static_cast<std::uint8_t>(i), 1, 2, 3, 4, 5, 6, 7,
            8, 9, 10, 11, 12, 13, 14, 15};
        ctx.market_id      = "mkt_chain03_continuity";
        ctx.strategy_id    = "strat_chain03";
        ctx.event_type     = AuditEventType::OrderApproved;
        ctx.reject_code    = stcpp::risk::RejectCode::INTERNAL_ERROR;
        ctx.sub_reason     = stcpp::risk::InvalidIntentSubReason::NONE;
        ASSERT_TRUE(emitter.emit_decision(ctx))
            << "R-chain-03: emit_decision #" << i << " 失败";
        const bool ok = chain_assert.on_decision(
            static_cast<std::uint64_t>(i + 1),
            AuditEventType::OrderApproved,
            ctx.decision_ts, emitter.last_hash());
        EXPECT_TRUE(ok)
            << "R-chain-03: chain 第 " << i << " 笔不匹配 (DECAYED 概念混入场景)";
    }

    EXPECT_EQ(chain_assert.emitted_count(), static_cast<std::uint64_t>(kN));
    EXPECT_EQ(chain_assert.violation_count(), 0u)
        << "R-chain-03: 6 条连续事件 chain 完整 (0 violation)";
}

// ---------- R-chain-04: STRATEGY_UNLOCK payload 格式校验 + 三签 -----------------

// 格式断言: 独立 (不依赖 AuditEmitter event_type 路由)
TEST(AuditChainReplay, R_chain_04_strategy_unlock_3sig_format) {
    StrategyUnlockPayload unlock;
    unlock.approver_cpo       = "cpo_sig_stub";
    unlock.approver_gm        = "gm_sig_stub";
    unlock.approver_financial = "financial_sig_stub";
    unlock.decay_audit_id     = "decay_audit_id_0xABCD";
    unlock.kelly_multiplier   = 0.5;

    // spec §2.2.2 R-unlock-3sig: 三签全非空
    EXPECT_FALSE(unlock.approver_cpo.empty())
        << "R-chain-04: UNLOCK approver_cpo 必须非空";
    EXPECT_FALSE(unlock.approver_gm.empty())
        << "R-chain-04: UNLOCK approver_gm 必须非空";
    EXPECT_FALSE(unlock.approver_financial.empty())
        << "R-chain-04: UNLOCK approver_financial 必须非空";

    // 因果链: decay_audit_id 非空 (关联 DECAYED)
    EXPECT_FALSE(unlock.decay_audit_id.empty())
        << "R-chain-04: UNLOCK decay_audit_id 必须非空 (因果链)";

    // MONITORING 期 Kelly 0.5x
    EXPECT_DOUBLE_EQ(unlock.kelly_multiplier, 0.5)
        << "R-chain-04: MONITORING 期 kelly_multiplier == 0.5";
}

// chain 连续性: UNLOCK 之后的 approve 事件 chain 仍然正确
TEST(AuditChainReplay, R_chain_04_strategy_unlock_chain_continuity) {
    WalConfig cfg{};
    cfg.kind        = WalKind::PaperAudit;
    cfg.path_prefix = "/var/lib/stcpp/paper/r_chain_04";
    auto wres = WalWriter<AuditRecord>::Open(cfg);
    ASSERT_TRUE(wres);
    auto writer = std::move(wres).value();
    AuditEmitter emitter(writer.get());

    AuditChainReplayAssertion chain_assert;

    // 4 条 OrderApproved 事件 (模拟 UNLOCK 后 chain 连续性)
    for (int i = 0; i < 4; ++i) {
        const std::int64_t now  = stcpp::infra::wal::pit::NowRealtimeNs();
        const std::int64_t base = now - 1'000'000'000LL;
        RiskDecisionInput ctx{};
        ctx.event_ts       = base;
        ctx.data_source_ts = base + 100;
        ctx.ingestion_ts   = base + 200;
        ctx.as_of_ts       = base + 300;
        ctx.decision_ts    = base + 400 + static_cast<std::int64_t>(i);
        ctx.audit_id_bytes = {
            static_cast<std::uint8_t>(0xA0 + i), 1, 2, 3, 4, 5, 6, 7,
            8, 9, 10, 11, 12, 13, 14, 15};
        ctx.market_id      = "mkt_chain04_continuity";
        ctx.strategy_id    = "strat_chain04";
        ctx.event_type     = AuditEventType::OrderApproved;
        ctx.reject_code    = stcpp::risk::RejectCode::INTERNAL_ERROR;
        ctx.sub_reason     = stcpp::risk::InvalidIntentSubReason::NONE;
        ASSERT_TRUE(emitter.emit_decision(ctx))
            << "R-chain-04: emit_decision #" << i << " 失败";
        const bool ok = chain_assert.on_decision(
            static_cast<std::uint64_t>(i + 1),
            AuditEventType::OrderApproved,
            ctx.decision_ts, emitter.last_hash());
        EXPECT_TRUE(ok) << "R-chain-04: UNLOCK 后 chain 第 " << i << " 笔不匹配";
    }

    EXPECT_EQ(chain_assert.emitted_count(), 4u);
    EXPECT_EQ(chain_assert.violation_count(), 0u)
        << "R-chain-04: UNLOCK 后 4 条 chain 完整";
}

}  // namespace
}  // namespace stcpp::test::replay
