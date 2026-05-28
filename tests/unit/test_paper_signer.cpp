// tests/unit/test_paper_signer.cpp — PaperSigner + 3 virtual mock 单测
//
// 落: xiaojiang-paper-engine-skeleton-v1.md §3 (3 mock 接口测试)
// 红线: R-7 mode tag / R-11 PaperAudit hard-bind / R-20 PIT chain

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <thread>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/signer/paper/paper_signer.hpp"
#include "stcpp/signer/signer_iface.hpp"

namespace {

using namespace stcpp;

signer::SignRequest MakeValidRequest(std::int64_t t_now) {
    signer::SignRequest req;
    req.intent_id         = 42;
    req.market_id         = "0xabc";
    req.outcome           = "YES";
    req.price             = 0.55;
    req.size_usdc         = 100.0;
    req.event_ts_ns       = t_now - 10'000'000;
    req.data_source_ts_ns = t_now -  8'000'000;
    req.ingestion_ts_ns   = t_now -  4'000'000;
    req.as_of_ts_ns       = t_now;
    return req;
}

}  // namespace

// ---------- VirtualNonceProvider ----------

TEST(VirtualNonceProvider, MonotonicFromZero) {
    signer::paper::VirtualNonceProvider n{0};
    EXPECT_EQ(n.Next(), 1u);
    EXPECT_EQ(n.Next(), 2u);
    EXPECT_EQ(n.Next(), 3u);
    EXPECT_EQ(n.Peek(), 3u);
}

TEST(VirtualNonceProvider, CustomInitial) {
    signer::paper::VirtualNonceProvider n{1000};
    EXPECT_EQ(n.Next(), 1001u);
    EXPECT_EQ(n.Next(), 1002u);
}

// ---------- VirtualGasEstimator ----------

TEST(VirtualGasEstimator, FixedAt80k) {
    signer::paper::VirtualGasEstimator g;
    signer::SignRequest req;
    EXPECT_EQ(g.Estimate(req), signer::paper::VirtualGasEstimator::kFixedGasEstimate);
    EXPECT_EQ(g.Estimate(req), 80'000u);
}

// ---------- VirtualConfirmWatcher ----------

TEST(VirtualConfirmWatcher, MockBlockTime2sWithJitter) {
    signer::paper::VirtualConfirmWatcher w{/*seed=*/0xDEADBEEF};
    const std::int64_t submit = 1'700'000'000'000'000'000LL;
    const auto r = w.Wait(1, submit);
    EXPECT_EQ(r.error, signer::SignerError::Ok);
    const std::int64_t delta = r.confirm_ts_ns - submit;
    // 2s ± 300ms
    EXPECT_GE(delta, signer::paper::VirtualConfirmWatcher::kBlockTimeNs
                     - signer::paper::VirtualConfirmWatcher::kJitterMaxNs);
    EXPECT_LE(delta, signer::paper::VirtualConfirmWatcher::kBlockTimeNs
                     + signer::paper::VirtualConfirmWatcher::kJitterMaxNs);
    EXPECT_GE(r.block_number, signer::paper::VirtualConfirmWatcher::kStartBlockNum);
}

TEST(VirtualConfirmWatcher, DeterministicSameSeed) {
    signer::paper::VirtualConfirmWatcher a{0x42}, b{0x42};
    const std::int64_t s = 1'700'000'000'000'000'000LL;
    EXPECT_EQ(a.Wait(1, s).confirm_ts_ns, b.Wait(1, s).confirm_ts_ns);
}

TEST(VirtualConfirmWatcher, BlockNumberMonotonic) {
    signer::paper::VirtualConfirmWatcher w{0x99};
    const std::int64_t s = 1'700'000'000'000'000'000LL;
    auto r1 = w.Wait(1, s);
    auto r2 = w.Wait(2, s);
    EXPECT_LT(r1.block_number, r2.block_number);
}

// ---------- PaperSigner ----------

TEST(PaperSigner, ModeIsPaper) {
    signer::paper::VirtualNonceProvider   n{0};
    signer::paper::VirtualGasEstimator    g;
    signer::paper::VirtualConfirmWatcher  c{0x1};
    signer::paper::PaperSigner            s{&n, &g, &c};
    EXPECT_EQ(s.Mode(), execution::ExecutionMode::Paper);
}

TEST(PaperSigner, ValidRequestSignsOk) {
    signer::paper::VirtualNonceProvider   n{0};
    signer::paper::VirtualGasEstimator    g;
    signer::paper::VirtualConfirmWatcher  c{0x2};
    signer::paper::PaperSigner            s{&n, &g, &c};

    const auto req  = MakeValidRequest(infra::wal::pit::NowRealtimeNs());
    const auto resp = s.Sign(req);

    EXPECT_EQ(resp.error, signer::SignerError::Ok);
    EXPECT_EQ(resp.nonce, 1u);
    EXPECT_EQ(resp.gas_estimate, 80'000u);
    EXPECT_GT(resp.block_number, 0u);
    // R-11: 出口硬绑 PaperAudit
    EXPECT_EQ(resp.audit_wal_kind, infra::wal::WalKind::PaperAudit);
    // R-20: 4 ts 透传
    EXPECT_EQ(resp.event_ts_ns,       req.event_ts_ns);
    EXPECT_EQ(resp.data_source_ts_ns, req.data_source_ts_ns);
    EXPECT_EQ(resp.ingestion_ts_ns,   req.ingestion_ts_ns);
    EXPECT_EQ(resp.as_of_ts_ns,       req.as_of_ts_ns);
}

TEST(PaperSigner, R11_AuditWalKindAlwaysPaperAudit) {
    // 即便 PIT 失败, audit_wal_kind 也必须 = PaperAudit (R-11 物理隔离, 决不能错填 RiskAudit/Position)
    signer::paper::VirtualNonceProvider   n{0};
    signer::paper::VirtualGasEstimator    g;
    signer::paper::VirtualConfirmWatcher  c{0x3};
    signer::paper::PaperSigner            s{&n, &g, &c};

    signer::SignRequest bad;
    bad.intent_id         = 1;
    bad.event_ts_ns       = 0;  // R-20 violation
    bad.data_source_ts_ns = 0;
    bad.ingestion_ts_ns   = 0;
    bad.as_of_ts_ns       = 0;

    const auto resp = s.Sign(bad);
    EXPECT_EQ(resp.error, signer::SignerError::PitViolation);
    EXPECT_EQ(resp.audit_wal_kind, infra::wal::WalKind::PaperAudit);  // 硬绑
    EXPECT_NE(resp.audit_wal_kind, infra::wal::WalKind::RiskAudit);
    EXPECT_NE(resp.audit_wal_kind, infra::wal::WalKind::Position);
}

TEST(PaperSigner, R20_PitViolation_EventTsZero) {
    signer::paper::VirtualNonceProvider   n{0};
    signer::paper::VirtualGasEstimator    g;
    signer::paper::VirtualConfirmWatcher  c{0x4};
    signer::paper::PaperSigner            s{&n, &g, &c};

    auto req = MakeValidRequest(infra::wal::pit::NowRealtimeNs());
    req.event_ts_ns = 0;
    const auto resp = s.Sign(req);
    EXPECT_EQ(resp.error, signer::SignerError::PitViolation);
    EXPECT_EQ(resp.nonce, 0u);  // 入口拒, nonce 没消耗
}

TEST(PaperSigner, R20_PitViolation_DsBeforeEvent) {
    signer::paper::VirtualNonceProvider   n{0};
    signer::paper::VirtualGasEstimator    g;
    signer::paper::VirtualConfirmWatcher  c{0x5};
    signer::paper::PaperSigner            s{&n, &g, &c};

    auto req = MakeValidRequest(infra::wal::pit::NowRealtimeNs());
    req.data_source_ts_ns = req.event_ts_ns - 1;  // 倒流
    const auto resp = s.Sign(req);
    EXPECT_EQ(resp.error, signer::SignerError::PitViolation);
}

TEST(PaperSigner, R20_PitViolation_AsOfInFuture) {
    signer::paper::VirtualNonceProvider   n{0};
    signer::paper::VirtualGasEstimator    g;
    signer::paper::VirtualConfirmWatcher  c{0x6};
    signer::paper::PaperSigner            s{&n, &g, &c};

    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    signer::SignRequest req;
    req.intent_id         = 1;
    req.event_ts_ns       = now + 10'000'000'000LL;     // event 10s 后
    req.data_source_ts_ns = req.event_ts_ns + 1;
    req.ingestion_ts_ns   = req.data_source_ts_ns + 1;
    req.as_of_ts_ns       = req.ingestion_ts_ns   + 1;  // 同样 in future
    const auto resp = s.Sign(req);
    EXPECT_EQ(resp.error, signer::SignerError::PitViolation);
}

TEST(PaperSigner, NonceConsumedOnSuccess_NotOnPitViolation) {
    signer::paper::VirtualNonceProvider   n{0};
    signer::paper::VirtualGasEstimator    g;
    signer::paper::VirtualConfirmWatcher  c{0x7};
    signer::paper::PaperSigner            s{&n, &g, &c};

    auto bad = MakeValidRequest(infra::wal::pit::NowRealtimeNs());
    bad.event_ts_ns = 0;
    (void)s.Sign(bad);
    EXPECT_EQ(n.Peek(), 0u);  // PIT 拒后, nonce 不动

    auto good = MakeValidRequest(infra::wal::pit::NowRealtimeNs());
    (void)s.Sign(good);
    EXPECT_EQ(n.Peek(), 1u);
}

TEST(PaperSigner, MockSignatureDeterministic) {
    signer::paper::VirtualNonceProvider   n{0};
    signer::paper::VirtualGasEstimator    g;
    signer::paper::VirtualConfirmWatcher  c{0x8};
    signer::paper::PaperSigner            s{&n, &g, &c};

    auto req = MakeValidRequest(infra::wal::pit::NowRealtimeNs());
    const auto r = s.Sign(req);
    // Paper tag 在 24..28 位
    EXPECT_EQ(r.signature[24], static_cast<std::uint8_t>('P'));
    EXPECT_EQ(r.signature[25], static_cast<std::uint8_t>('A'));
    EXPECT_EQ(r.signature[26], static_cast<std::uint8_t>('P'));
    EXPECT_EQ(r.signature[27], static_cast<std::uint8_t>('R'));
}

// ---------- ExecutionContext ----------

TEST(ExecutionContext, InitAndGetPaper) {
    execution::ExecutionContext::ResetForTesting();
    execution::ExecutionContext::Init(execution::ExecutionMode::Paper);
    EXPECT_EQ(execution::ExecutionContext::Mode(), execution::ExecutionMode::Paper);
    EXPECT_TRUE(execution::ExecutionContext::IsInitialized());
    execution::ExecutionContext::ResetForTesting();
}

TEST(ExecutionContextDeathTest, DoubleInitAborts) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    execution::ExecutionContext::ResetForTesting();
    execution::ExecutionContext::Init(execution::ExecutionMode::Paper);
    EXPECT_DEATH({
        execution::ExecutionContext::Init(execution::ExecutionMode::Live);
    }, "");
    execution::ExecutionContext::ResetForTesting();
}
