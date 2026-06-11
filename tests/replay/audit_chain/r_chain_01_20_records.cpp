// tests/replay/audit_chain/r_chain_01_20_records.cpp — R-chain-01: 20 条 replay chain 校验
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.2
//         升级自 integration/audit_chain_verify_test.cpp T1
//
// 场景: 20 条 audit record replay, 验证 AuditChainReplayAssertion 正确追踪链
// 期望: 每条 chain head 与 mirror 一致; emitted_count == 20

#include <cstdint>

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_emitter.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/observability/blake3_hash.hpp"

#include "tests/replay/paper/audit_chain_replay_assertion.hpp"

namespace stcpp::test::replay {
namespace {

using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::observability::AuditEmitter;
using stcpp::observability::AuditEventType;
using stcpp::observability::AuditRecord;
using stcpp::observability::Blake3Hasher;
using stcpp::observability::RiskDecisionInput;

static std::int64_t NowNs() noexcept {
    return stcpp::infra::wal::pit::NowRealtimeNs();
}

RiskDecisionInput MakeCtx(int i, AuditEventType type) {
    const std::int64_t now = NowNs();
    const std::int64_t base = now - 1'000'000'000LL;
    RiskDecisionInput in{};
    in.event_ts = base;
    in.data_source_ts = base + 100;
    in.ingestion_ts = base + 200;
    in.as_of_ts = base + 300;
    in.decision_ts = base + 400;
    in.audit_id_bytes = {static_cast<std::uint8_t>(i), 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    in.market_id = "mkt_replay_chain_01";
    in.strategy_id = "strat_chain";
    in.event_type = type;
    in.reject_code = stcpp::risk::RejectCode::INTERNAL_ERROR;
    in.sub_reason = stcpp::risk::InvalidIntentSubReason::NONE;
    return in;
}

// R-chain-01: 20 条 emit + AuditChainReplayAssertion 逐条校验 chain head
TEST(AuditChainReplay, R_chain_01_20_records_chain_assertion_pass) {
    // Open writer
    WalConfig cfg{};
    cfg.kind = WalKind::PaperAudit;
    cfg.path_prefix = "/var/lib/stcpp/engine/r_chain_01";
    auto wres = WalWriter<AuditRecord>::Open(cfg);
    ASSERT_TRUE(wres);
    auto writer = std::move(wres).value();
    AuditEmitter emitter(writer.get());

    AuditChainReplayAssertion chain_assert;

    constexpr int kN = 20;
    for (int i = 0; i < kN; ++i) {
        auto ctx = MakeCtx(i, AuditEventType::OrderApproved);
        const auto r = emitter.emit_decision(ctx);
        ASSERT_TRUE(r) << "R-chain-01: emit_decision #" << i << " 失败";

        // AuditChainReplayAssertion 逐条校验 (replay 协议)
        const auto seq = static_cast<std::uint64_t>(i + 1);
        const bool chain_ok = chain_assert.on_decision(seq, AuditEventType::OrderApproved, ctx.decision_ts,
                                                       emitter.last_hash());

        EXPECT_TRUE(chain_ok) << "R-chain-01: replay chain head 第 " << i << " 笔不匹配";
    }

    EXPECT_EQ(chain_assert.emitted_count(), static_cast<std::uint64_t>(kN))
        << "R-chain-01: emitted_count == " << kN;
    EXPECT_EQ(chain_assert.violation_count(), 0u) << "R-chain-01: 0 chain violation";
    EXPECT_EQ(emitter.emitted_count(), static_cast<std::uint64_t>(kN));
}

}  // namespace
}  // namespace stcpp::test::replay
