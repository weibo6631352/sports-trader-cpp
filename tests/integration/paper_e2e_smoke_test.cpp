// tests/integration/paper_e2e_smoke_test.cpp — paper E2E 60s smoke (W5 Wave 24)
//
// Owner: 小宋  Sprint-2 W5 Wave 24 (W5-E-03, 老胡 spec)
// 关联: docs/RESEARCH/laozhou-architecture-v0.6-e2e.md §5 M1 10 hard gate
//        - M1-G1: 1 笔 paper intent 端到端 < 50ms (WSS recv → VirtualFill emit)
//        - M1-G3: paper_audit / risk_audit / position / shadow_audit 4 流物理隔离 (R-11)
//        - M1-G4: 0 个 R-12 违例 (本测在 worker thread 同步跑, 无 event-loop 阻塞)
//        - M1-G5: 全 4 ts R-20 链路通 (PIT AssertChain 全 enforce)
//        - M1-G8: 12 RejectCode 端到端 fixture (本测覆盖至少 1 reject 路径)
//
// 关联 acceptance (xiaoying-acceptance-spec-v1.md):
//   M1-A04 21 RejectCode ≥1 单测; M1-D01 端到端 <50ms;
//   M1-D02 paper/risk WAL 物理隔离; M1-D04 4 ts 违例 → INVALID_INTENT.TS_*;
//   M1-F04 0 个 R-12 违例.
//
// 红线: R-1 (RM 必经), R-7 (paper-only build, CMake 已隔离),
//        R-11 (4 wal 物理隔离), R-12 (event loop 不阻塞), R-20 (4 ts 全链路).
//
// W5 末替换点: MockPmWss → 小冯 PM WSS subscriber callback (不动其他).

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>  // std::memcpy (Wave 81: explicit after clang-format include reorder)
#include <string>
#include <thread>
#include <vector>

#include "tests/integration/test_fixture.hpp"

namespace stcpp::test::integration {
namespace {

using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::pit::NowRealtimeNs;

// ---- 端到端 latency 预算 (老姜 latency-budget-v1) ----
//  端到端 p99 < 50ms = 50'000'000 ns (M1-D01 / M1-G1)
constexpr std::int64_t kE2EP99BudgetNs = 50'000'000LL;

// 构造一笔合法的 PM book update (R-20 4 ts 满足: event ≤ ds ≤ ingestion ≤ as_of)
PmBookUpdate MakeValidBook(const std::string& mid, std::int64_t now_ns) {
    PmBookUpdate b;
    b.market_id = mid;
    b.is_buy = true;
    b.price = 0.55;
    b.book_depth_l1_usdc = 20'000.0;
    b.event_ts_ns = now_ns - 5'000'000;  // 5ms 前 event
    b.data_source_ts_ns = now_ns - 4'000'000;
    b.ingestion_ts_ns = now_ns - 2'000'000;
    return b;
}

// ---- T1: M1-G1 端到端 10 笔 smoke + p99 < 50ms ------------------------------
TEST_F(PaperE2EFixture, T1_E2E_10_book_updates_under_50ms_p99) {
    // 10 笔 mock WSS book → e2e
    for (int i = 0; i < 10; ++i) {
        const auto now = NowRealtimeNs();
        pm_wss_.push(MakeValidBook("mkt_e2e_" + std::to_string(i), now));
    }
    ASSERT_EQ(pm_wss_.size(), 10u);

    std::vector<std::int64_t> latencies;
    latencies.reserve(10);
    std::uint64_t approved = 0;

    while (!pm_wss_.empty()) {
        auto b = pm_wss_.pop();
        const std::string sid = "sig_" + b.market_id;
        auto out = RunOneE2E(b, sid);
        latencies.push_back(out.latency_ns);
        if (out.rm_decision.is_approved()) {
            ++approved;
            EXPECT_TRUE(out.went_through_signer);
            // R-11: SignResponse.audit_wal_kind 硬填 PaperAudit
            EXPECT_EQ(out.sign_resp.audit_wal_kind, WalKind::PaperAudit);
            // R-20: VirtualFill 4 ts 透传
            EXPECT_EQ(out.fill.event_ts_ns, b.event_ts_ns);
            EXPECT_EQ(out.fill.data_source_ts_ns, b.data_source_ts_ns);
            EXPECT_EQ(out.fill.ingestion_ts_ns, b.ingestion_ts_ns);
            EXPECT_GT(out.fill.as_of_ts_ns, b.ingestion_ts_ns);
            // R-11: VirtualFill.audit_wal_kind = PaperAudit (硬约束)
            EXPECT_EQ(out.fill.audit_wal_kind, WalKind::PaperAudit);
            // W6 Wave 29: VirtualFill.market_id / outcome 透传验证
            // market_id: "mkt_e2e_N" → array<char,32> null-padded
            {
                std::array<char, 32> expected_mid{};
                const std::string& mid = b.market_id;
                std::memcpy(expected_mid.data(), mid.data(),
                            std::min(mid.size(), static_cast<std::size_t>(32)));
                EXPECT_EQ(out.fill.market_id, expected_mid)
                    << "W6: VirtualFill.market_id 透传 market=" << mid;
            }
            // outcome: fixture MakeValidIntent 设 "YES" (RunOneE2E req.outcome="YES")
            EXPECT_EQ(out.fill.outcome, std::uint8_t{0}) << "W6: VirtualFill.outcome YES=0 透传";
        }
        // R-1: 即使 reject, audit_id 也应非全零 (老韩 R-1 invariant).
        // BUG-W5-001 fixed (老沈 W5 Wave 24, 2026-05-28): next_audit_id() shift
        //   exponent 72 UB 已修 (seq 段 6B big-endian). 单测 AuditId.NonZero_O2
        //   1000 次唯一非零覆盖.
        std::array<std::uint8_t, 16> const zero{};
        EXPECT_NE(out.rm_decision.audit_id, zero) << "R-1: audit_id 非空 invariant (BUG-W5-001 regression)";
    }

    EXPECT_EQ(latencies.size(), 10u);
    EXPECT_GE(approved, 1u) << "10 笔小单 happy path 至少 1 笔过 RM";
    const auto p99 = p99_ns(latencies);
    EXPECT_LT(p99, kE2EP99BudgetNs) << "M1-G1 / M1-D01: 端到端 p99 < 50ms (实测 " << p99 << " ns)";
    // 公开实测数 (老胡 review 看)
    std::printf("[T1 e2e p99] %lld ns (budget %lld)\n", static_cast<long long>(p99),
                static_cast<long long>(kE2EP99BudgetNs));
}

// ---- T2: M1-G3 4 wal 物理隔离 (R-11) ----------------------------------------
TEST_F(PaperE2EFixture, T2_R11_four_wal_physical_isolation) {
    // 4 wal writer 各自 path_prefix 必命中 kPathRoots 白名单 (skeleton Open()
    // 已硬校验; 这里复核 + path 不重叠).
    ASSERT_NE(paper_audit_, nullptr) << "paper_audit writer Open 失败";
    ASSERT_NE(risk_audit_, nullptr) << "risk_audit writer Open 失败";
    ASSERT_NE(position_, nullptr) << "position writer Open 失败";
    ASSERT_NE(shadow_audit_, nullptr) << "shadow_audit writer Open 失败";

    EXPECT_EQ(paper_audit_->Kind(), WalKind::PaperAudit);
    EXPECT_EQ(risk_audit_->Kind(), WalKind::RiskAudit);
    EXPECT_EQ(position_->Kind(), WalKind::Position);
    EXPECT_EQ(shadow_audit_->Kind(), WalKind::ShadowAudit);

    // 跑 5 笔 e2e, 计 paper_audit_ HighWatermark, 其余 3 wal 必 0
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        auto b = MakeValidBook("mkt_isol_" + std::to_string(i), now);
        const auto out = RunOneE2E(b, "sig_isol_" + std::to_string(i));
        (void)out;
    }
    EXPECT_GT(paper_audit_->HighWatermark(), 0u) << "paper_audit.wal 必有 record (5 笔 → audit emit)";
    EXPECT_EQ(risk_audit_->HighWatermark(), 0u) << "M1-G3 / R-11: paper mode 不写 risk_audit.wal";
    EXPECT_EQ(position_->HighWatermark(), 0u)
        << "M1-G3 / R-11: paper mode 不写 position.wal (paper 不动真账本)";
    EXPECT_EQ(shadow_audit_->HighWatermark(), 0u) << "M1-G3 / R-11: paper mode 不写 shadow_audit.wal";
}

// ---- T3: M1-G5 / M1-D04 R-20 4 ts 全链路 + PIT 拦截 -------------------------
TEST_F(PaperE2EFixture, T3_R20_4ts_full_chain_and_PIT_reject) {
    // happy path: 4 ts 单调, audit emit 全 OK, paper_audit_ 计数 +N
    const auto t0_paper = paper_audit_->HighWatermark();
    {
        const auto now = NowRealtimeNs();
        auto b = MakeValidBook("mkt_ts_ok", now);
        auto out = RunOneE2E(b, "sig_ts_ok");
        EXPECT_TRUE(out.rm_decision.is_approved() || out.rm_decision.is_rejected());  // 不抛
    }
    EXPECT_GT(paper_audit_->HighWatermark(), t0_paper) << "R-20 + audit emit happy path 应写 paper_audit";

    // M1-D04 / M1-G8: 4 ts 违例 → INVALID_INTENT.TS_ORDER_VIOLATED
    // 构造 data_source < event (R-20 §7 不等式违反)
    {
        risk::OrderIntent it{};
        const auto now = NowRealtimeNs();
        it.event_ts_ns = now - 1'000'000;
        it.data_source_ts_ns = now - 2'000'000;  // < event_ts → 违例
        it.ingestion_ts_ns = now - 500'000;
        it.as_of_ts_ns = now;
        it.condition_id = "0xmkt_ts_violate";  // v0.5: was market_id
        it.token_id = "1234567890";            // v0.5: new
        it.outcome = risk::Outcome::Yes;       // v0.5: new
        it.side = risk::Side::Buy;             // v0.5: was is_buy=true
        it.strategy_id = "strat_p001_paper";
        it.signal_id = "sig_ts_violate";
        it.feature_snapshot_id = "fs_ts_violate";
        it.price = 0.55;
        it.size_usdc = 100;
        it.book_depth_l1_usdc = 20'000.0;
        it.book_snapshot_ts_ns = it.data_source_ts_ns;
        it.tick_size = 0.01;
        auto d = rg_->evaluate(it);
        EXPECT_TRUE(d.is_rejected()) << "R-20: data_source < event 必拒";
        EXPECT_EQ(d.reject, risk::RejectCode::INVALID_INTENT);
        EXPECT_EQ(d.sub_reason, risk::InvalidIntentSubReason::TS_ORDER_VIOLATED);
        // R-1 audit_id 非零 (BUG-W5-001 fixed, 老沈 W5 Wave 24) + audit emit 计数兜底.
        std::array<std::uint8_t, 16> const zero{};
        EXPECT_NE(d.audit_id, zero) << "R-1: audit_id 非空 invariant";
        EXPECT_GE(audit_emitter_->emitted_total(), 1u)
            << "R-1: 每笔 evaluate 必 audit emit (audit_id 计数兜底)";
    }
}

// ---- T4: M1-G8 reject 路径覆盖 (12 RejectCode 至少 1 笔) --------------------
TEST_F(PaperE2EFixture, T4_RM_reject_path_at_least_one_code) {
    // stale data path: 注入 market_freshness > halt_ms 阈值
    {
        rg_->set_market_state("mkt_stale", risk::MarketState::INPLAY_HOT_CRIT);
        rg_->set_market_freshness_ms("mkt_stale", 5'000);  // > 800ms halt_ms
        const auto now = NowRealtimeNs();
        auto b = MakeValidBook("mkt_stale", now);
        auto out = RunOneE2E(b, "sig_stale_1");
        EXPECT_TRUE(out.rm_decision.is_rejected());
        EXPECT_EQ(out.rm_decision.reject, risk::RejectCode::STALE_DATA);
        EXPECT_FALSE(out.went_through_signer) << "reject 不应走 signer (R-1: 必经 RM)";
    }

    // duplicate path: 同 signal_id 两次
    {
        const auto now = NowRealtimeNs();
        auto b = MakeValidBook("mkt_dup", now);
        const auto o1 = RunOneE2E(b, "sig_dup_same");
        (void)o1;
        const auto o2 = RunOneE2E(b, "sig_dup_same");  // 同 signal_id
        EXPECT_TRUE(o2.rm_decision.is_rejected());
        EXPECT_EQ(o2.rm_decision.reject, risk::RejectCode::DUPLICATE_INTENT);
    }

    // per_order_cap 越线
    {
        const auto now = NowRealtimeNs();
        auto b = MakeValidBook("mkt_cap", now);
        risk::OrderIntent it = MakeValidIntent(b, "sig_cap_1");
        it.size_usdc = 100'000;  // > per_order_cap_usdc(10'000)
        auto d = rg_->evaluate(it);
        EXPECT_TRUE(d.is_rejected());
        EXPECT_EQ(d.reject, risk::RejectCode::EXCEED_PER_ORDER_CAP);
    }

    // state HALTED
    {
        rg_->set_state(risk::RmState::HALTED);
        const auto now = NowRealtimeNs();
        auto b = MakeValidBook("mkt_halted", now);
        auto out = RunOneE2E(b, "sig_halted");
        EXPECT_TRUE(out.rm_decision.is_rejected());
        EXPECT_EQ(out.rm_decision.reject, risk::RejectCode::STATE_HALTED);
        rg_->set_state(risk::RmState::RUNNING);
    }

    // 至少 4 类 reject 命中 (M1-G8 → W5 末派老沈 12 全覆盖)
    EXPECT_GE(audit_emitter_->reject_count(risk::RejectCode::STALE_DATA), 1u);
    EXPECT_GE(audit_emitter_->reject_count(risk::RejectCode::DUPLICATE_INTENT), 1u);
    EXPECT_GE(audit_emitter_->reject_count(risk::RejectCode::EXCEED_PER_ORDER_CAP), 1u);
    EXPECT_GE(audit_emitter_->reject_count(risk::RejectCode::STATE_HALTED), 1u);
}

// ---- T5: M1-G4 0 R-12 违例 (worker thread 同步 e2e, event loop 不阻塞) -----
//
// 本测在 e2e 同步循环中跑, 不开 WSS event loop 线程 — paper engine M1 不要求
// real-time event loop, 此测验证: 100 笔 e2e 全程无锁 > 100us 持有.
TEST_F(PaperE2EFixture, T5_R12_no_blocking_io_in_worker_thread) {
    constexpr int kN = 100;
    std::vector<std::int64_t> per_evaluate_ns;
    per_evaluate_ns.reserve(kN);

    for (int i = 0; i < kN; ++i) {
        const auto now = NowRealtimeNs();
        auto b = MakeValidBook("mkt_r12_" + std::to_string(i), now);
        auto it = MakeValidIntent(b, "sig_r12_" + std::to_string(i));
        const auto t0 = std::chrono::steady_clock::now();
        auto d = rg_->evaluate(it);
        const auto t1 = std::chrono::steady_clock::now();
        (void)d;
        per_evaluate_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    const auto p99 = p99_ns(per_evaluate_ns);
    // R-12 §17.1.1: hot path 无单步 > 100us 阻塞 IO. evaluate() 单笔 < 100us = OK.
    // (老姜 budget: RM evaluate p99 < 1ms; 这里更严, R-12 实测 < 100us)
    EXPECT_LT(p99, 100'000) << "R-12: RM evaluate p99 < 100us (实测 " << p99 << " ns) — 0 阻塞 IO";
    std::printf("[T5 RM evaluate p99] %lld ns\n", static_cast<long long>(p99));
}

}  // namespace
}  // namespace stcpp::test::integration
