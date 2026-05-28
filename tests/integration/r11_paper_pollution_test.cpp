// tests/integration/r11_paper_pollution_test.cpp — R-11 paper 不污染真账本 (W5 Wave 24)
//
// Owner: 小宋  Sprint-2 W5 Wave 24 (W5-E-03)
// 关联:
//   docs/RESEARCH/laozhou-architecture-v0.6-e2e.md §5
//     - M1-G3 4 流物理隔离 (paper_audit / risk_audit / position / shadow_audit)
//     - M1-G9 paper 不污染真账本 (paper_position 与 live_position 物理隔离)
//   docs/RESEARCH/xiaoying-acceptance-spec-v1.md
//     - M1-D02 paper/risk WAL 物理隔离 (R-11)
//     - M1-D03 paper engine 不写 position / pnl_ledger / nonce_ledger
//   ADR R-11 (paper-mode-no-real-ledger-pollution)
//
// 验证:
//   T1: 50 笔 e2e (含 approved + reject), paper_audit_ HighWatermark > 0;
//       其余 3 wal (risk_audit / position / shadow_audit) HighWatermark == 0
//   T2: SignResponse.audit_wal_kind 全部 PaperAudit (paper signer 硬填)
//   T3: VirtualFill.audit_wal_kind 全部 PaperAudit
//   T4: PaperSigner.Mode() == Paper (R-7 mode tag, 防 live signer 串入)
//   T5: ExecutionContext build-time mode 锁定 paper (R-7 防 runtime 切换)
//
// 红线: R-7 / R-11 / R-20 全 enforce

#include "tests/integration/test_fixture.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace stcpp::test::integration {
namespace {

using stcpp::execution::ExecutionMode;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::pit::NowRealtimeNs;

// ---- T1: 50 笔 e2e → 仅 paper_audit 有 record, 其余 3 wal 空 ----------------
TEST_F(PaperE2EFixture, T1_R11_50_e2e_only_paper_audit_writes) {
    constexpr int kN = 50;
    std::uint64_t approved = 0;
    std::uint64_t rejected = 0;

    for (int i = 0; i < kN; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id           = "mkt_pollution_" + std::to_string(i);
        b.is_buy              = (i % 2) == 0;
        b.price               = 0.50 + 0.001 * (i % 20);
        b.book_depth_l1_usdc  = 20'000.0;
        b.event_ts_ns         = now - 5'000'000;
        b.data_source_ts_ns   = now - 4'000'000;
        b.ingestion_ts_ns     = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_pollution_" + std::to_string(i));
        if (out.rm_decision.is_approved())      ++approved;
        else if (out.rm_decision.is_rejected()) ++rejected;
    }

    EXPECT_EQ(approved + rejected, static_cast<std::uint64_t>(kN));
    EXPECT_GE(approved, 1u) << "50 笔小单 happy path 至少 1 笔过 RM";

    // M1-D02 / R-11: paper_audit 有 record, 其余 3 wal 空
    EXPECT_GT(paper_audit_->HighWatermark(), 0u)
        << "paper_audit.wal 必有 record (每笔 audit emit 至少 1)";
    EXPECT_EQ(risk_audit_->HighWatermark(),   0u)
        << "M1-D02 / R-11: paper 不写 risk_audit.wal (live mode 才写)";
    EXPECT_EQ(position_->HighWatermark(),     0u)
        << "M1-D03 / R-11: paper 不写 position.wal (paper 不动真账本)";
    EXPECT_EQ(shadow_audit_->HighWatermark(), 0u)
        << "M1-D02 / R-11: paper 不写 shadow_audit.wal";

    // audit emit 计数应 ≥ paper_audit HighWatermark
    EXPECT_EQ(audit_emitter_->paper_audit_records(),
              paper_audit_->HighWatermark());
    EXPECT_EQ(audit_emitter_->emitted_total(), static_cast<std::uint64_t>(kN));
}

// ---- T2: SignResponse.audit_wal_kind 全 PaperAudit (paper signer 硬填) -----
TEST_F(PaperE2EFixture, T2_R11_sign_response_wal_kind_always_PaperAudit) {
    constexpr int kN = 20;
    int went_through = 0;
    for (int i = 0; i < kN; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_sign_" + std::to_string(i);
        b.is_buy             = true;
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_sign_kind_" + std::to_string(i));
        if (out.went_through_signer) {
            ++went_through;
            EXPECT_EQ(out.sign_resp.audit_wal_kind, WalKind::PaperAudit)
                << "R-11: paper signer SignResponse.audit_wal_kind 必 PaperAudit";
            EXPECT_EQ(out.sign_resp.error, signer::SignerError::Ok);
        }
    }
    EXPECT_GE(went_through, 1) << "至少 1 笔 approved 走 signer";
}

// ---- T3: VirtualFill.audit_wal_kind 全 PaperAudit --------------------------
TEST_F(PaperE2EFixture, T3_R11_virtual_fill_wal_kind_always_PaperAudit) {
    constexpr int kN = 15;
    int fills = 0;
    for (int i = 0; i < kN; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_fill_" + std::to_string(i);
        b.is_buy             = true;
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_fill_kind_" + std::to_string(i));
        if (out.went_through_signer) {
            ++fills;
            EXPECT_EQ(out.fill.audit_wal_kind, WalKind::PaperAudit)
                << "R-11: VirtualFill.audit_wal_kind 必 PaperAudit";
        }
    }
    EXPECT_GE(fills, 1);
}

// ---- T4: PaperSigner Mode == Paper (R-7 mode tag) --------------------------
TEST_F(PaperE2EFixture, T4_R7_signer_mode_is_paper) {
    EXPECT_EQ(signer_->Mode(), ExecutionMode::Paper)
        << "R-7: paper signer Mode 必 Paper";
}

// ---- T5: build-time mode 锁定 paper (R-7) -----------------------------------
TEST(R11PollutionStandalone, T5_R7_compile_time_mode_paper) {
    // execution_mode.hpp: STCPP_EXEC_MODE_paper 注入 → kCompiledMode == Paper
    EXPECT_EQ(stcpp::execution::kCompiledMode, ExecutionMode::Paper)
        << "R-7: build-time mode 必 paper (CMake STCPP_EXEC_MODE=paper)";
    // AuditWalKindForBuild(): paper → PaperAudit
    EXPECT_EQ(stcpp::observability::AuditWalKindForBuild(), WalKind::PaperAudit)
        << "R-11: build-time AuditWalKindForBuild 必 PaperAudit";
}

}  // namespace
}  // namespace stcpp::test::integration
