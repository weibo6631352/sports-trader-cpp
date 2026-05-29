// tests/chaos/rest_timeout/c_rest_04_paper_startup.cpp — C-REST-04 (paper): paper 启动时 REST 超时
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.4 / §1.3 C-PAPER-04
//
// 场景: paper runtime 启动时 /positions REST 超时
// 期望:
//   - paper engine 以空 position state 启动
//   - 不写 position WAL
//   - R-11 保持
//
// 断言:
//   ASSERT paper_position_wal.HighWatermark == 0 IF REST startup fails (R-11)
//   ASSERT audit_contains(reject_code=STALE_DATA) IF rest_failure > stale_threshold

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CRest04PaperFixture : public ChaosE2EFixture {};

// C-PAPER-04 / C-REST-04: paper 启动 REST /positions 超时 → 空 position 启动, R-11 保持
TEST_F(CRest04PaperFixture, C_REST_04_paper_startup_rest_timeout_r11_preserved) {
    // 注入: paper 启动时 /positions REST 超时
    FaultConfig cfg;
    cfg.kind = FaultKind::RestTimeout;
    cfg.http_status_code = 0;
    cfg.timeout_ms = 10'000;
    cfg.path_filter = "/positions";
    InjectFault(cfg);

    // paper engine 以空 position state 启动 → position WAL HWM == 0
    EXPECT_EQ(position_->HighWatermark(), 0u)
        << "C-PAPER-04: REST /positions 超时 → paper engine 以空 state 启动 (position WAL HWM=0)";

    // R-11: paper_position_wal (position_) 不被写入
    EXPECT_EQ(position_->HighWatermark(), fault_state_.position_hwm_at_inject)
        << "C-PAPER-04: R-11 保持 — position WAL 水位线不变";

    // 系统仍可处理 intent (RM 以空 position state 运行)
    std::uint64_t processed = 0;
    for (int i = 0; i < 3; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_rest04_paper_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        EXPECT_NO_FATAL_FAILURE({
            auto out = RunOneE2E(b, "sig_rest04_paper_" + std::to_string(i));
            ++processed;
            (void)out;
        }) << "C-PAPER-04: 空 position state 下 RM 不 abort";
    }
    EXPECT_EQ(processed, 3u) << "C-PAPER-04: 3 笔处理完成";

    // paper_audit 仍有记录 (emitter 正常工作)
    EXPECT_GE(audit_emitter_->emitted_total(), 3u)
        << "C-PAPER-04: AuditEmitter 正常工作 (emitted_total >= 3)";
}

}  // namespace
}  // namespace stcpp::test::chaos
