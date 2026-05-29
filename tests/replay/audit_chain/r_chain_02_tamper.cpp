// tests/replay/audit_chain/r_chain_02_tamper.cpp — R-chain-02: 篡改检测
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.2
//         升级自 integration/audit_chain_verify_test.cpp T2
//
// 场景: 篡改任意 record decision_ts → replay assertion 检出 chain mismatch
// 期望: chain 首次不一致定位到被篡改 record

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

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
using stcpp::observability::RiskDecisionInput;
using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::observability::AuditRecord;

static std::int64_t NowNs() noexcept {
    return stcpp::infra::wal::pit::NowRealtimeNs();
}

// R-chain-02: 篡改第 10 条 decision_ts → replay chain assertion 检出 (M1-A06)
TEST(AuditChainReplay, R_chain_02_tamper_detected_by_replay_assertion) {
    WalConfig cfg{};
    cfg.kind        = WalKind::PaperAudit;
    cfg.path_prefix = "/var/lib/stcpp/paper/r_chain_02";
    auto wres = WalWriter<AuditRecord>::Open(cfg);
    ASSERT_TRUE(wres);
    auto writer = std::move(wres).value();
    AuditEmitter emitter(writer.get());

    constexpr int kN = 20;
    struct Snap { RiskDecisionInput ctx; stcpp::observability::Blake3Hasher::Hash256 chain_after; };
    std::vector<Snap> snaps;
    snaps.reserve(kN);

    for (int i = 0; i < kN; ++i) {
        const std::int64_t now  = NowNs();
        const std::int64_t base = now - 1'000'000'000LL;
        RiskDecisionInput ctx{};
        ctx.event_ts        = base;
        ctx.data_source_ts  = base + 100;
        ctx.ingestion_ts    = base + 200;
        ctx.as_of_ts        = base + 300;
        ctx.decision_ts     = base + 400 + i;
        ctx.audit_id_bytes  = {
            static_cast<std::uint8_t>(i), 1, 2, 3, 4, 5, 6, 7,
            8, 9, 10, 11, 12, 13, 14, 15};
        ctx.market_id       = "mkt_chain02";
        ctx.strategy_id     = "strat_chain";
        ctx.event_type      = AuditEventType::OrderApproved;
        ctx.reject_code     = stcpp::risk::RejectCode::INTERNAL_ERROR;
        ctx.sub_reason      = stcpp::risk::InvalidIntentSubReason::NONE;
        ASSERT_TRUE(emitter.emit_decision(ctx));
        snaps.push_back({ctx, emitter.last_hash()});
    }

    // 正常 replay: assertion 全通过
    {
        AuditChainReplayAssertion chain_assert;
        for (std::size_t i = 0; i < static_cast<std::size_t>(kN); ++i) {
            const bool ok = chain_assert.on_decision(
                static_cast<std::uint64_t>(i + 1),
                AuditEventType::OrderApproved,
                snaps[i].ctx.decision_ts,
                snaps[i].chain_after);
            EXPECT_TRUE(ok) << "R-chain-02: 未篡改 chain 第 " << i << " 笔必须通过";
        }
        EXPECT_EQ(chain_assert.violation_count(), 0u) << "R-chain-02: 未篡改 0 violation";
    }

    // 篡改第 10 条 decision_ts: 修改 snaps[10].ctx.decision_ts + 1
    {
        AuditChainReplayAssertion chain_tamper;
        bool first_mismatch = false;
        std::uint64_t first_mismatch_seq = 0;

        for (std::size_t i = 0; i < static_cast<std::size_t>(kN); ++i) {
            constexpr std::size_t kTamperIdx = 10;
            const std::int64_t ts =
                (i == kTamperIdx) ? (snaps[i].ctx.decision_ts + 1)
                                  : snaps[i].ctx.decision_ts;

            // replay 用真实 decision_ts (篡改版) 但 emitter chain 是真实的
            // 因为 ts 不同, 重算的 expected_current 与 emitter 的 chain 不一致
            const bool ok = chain_tamper.on_decision(
                static_cast<std::uint64_t>(i + 1),
                AuditEventType::OrderApproved,
                ts,
                snaps[i].chain_after);  // 真实 chain (emitter 写的)

            if (!ok && !first_mismatch) {
                first_mismatch = true;
                first_mismatch_seq = static_cast<std::uint64_t>(i + 1);
            }
        }

        EXPECT_TRUE(first_mismatch)
            << "R-chain-02 / M1-A06: 篡改任意 record 必被 chain replay 检出";
        EXPECT_EQ(first_mismatch_seq, static_cast<std::uint64_t>(10 + 1))
            << "R-chain-02 / M1-A06: chain 首次不一致定位到被篡改 record (seq=11)";
        EXPECT_GE(chain_tamper.violation_count(), 1u)
            << "R-chain-02: 篡改后 violation_count >= 1";
    }
}

}  // namespace
}  // namespace stcpp::test::replay
