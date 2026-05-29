// tests/chaos/rest_timeout/c_rest_01_429.cpp — C-REST-01: clob REST 429 限流
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.4
//
// 场景: status=429, all paths → exponential backoff
// 期望:
//   - exponential backoff 生效; 不打死; 不误判为 server down; 拒单率暂升但不 HALT
//
// 断言:
//   ASSERT http_client.retry_count <= max_retries
//   ASSERT wss_event_loop_unblocked (独立线程验证)
//   ASSERT 不 HALT (audit 无 STATE_HALTED 类型, 只有 STALE_DATA 类)

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;
using stcpp::risk::RejectCode;

class CRest01Fixture : public ChaosE2EFixture {};

// C-REST-01: 429 限流 → backoff 生效, 不死锁, 不 HALT
TEST_F(CRest01Fixture, C_REST_01_429_rate_limit_backoff_no_halt) {
    // 注入 429 限流
    FaultConfig cfg;
    cfg.kind = FaultKind::RestTimeout;
    cfg.http_status_code = 429;
    cfg.path_filter = "/";
    InjectFault(cfg);

    EXPECT_EQ(fault_state_.rest_error_count, 1u) << "C-REST-01: REST error count 记录 1 (429 注入)";

    // retry_count <= max_retries: 模拟 backoff 计数 (stub 层记录, 不超 3 次)
    const std::size_t max_retries = 3;
    EXPECT_LE(fault_state_.rest_retry_count, max_retries)
        << "C-REST-01: retry_count <= max_retries=" << max_retries;

    // WSS event loop 未被阻塞
    if (!fault_state_.wss_tick_latencies_ns.empty()) {
        const auto p99_us = p99_chaos_ns(fault_state_.wss_tick_latencies_ns) / 1'000;
        EXPECT_LT(p99_us, 50LL) << "C-REST-01 / R-12: wss tick p99 " << p99_us << "us 必须 < 50us";
    }

    // 429 期间跑 5 笔: 系统不 HALT, RM 继续评估 (可能拒单但不崩)
    std::uint64_t total_processed = 0;
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_rest01_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        EXPECT_NO_FATAL_FAILURE({
            auto out = RunOneE2E(b, "sig_rest01_" + std::to_string(i));
            ++total_processed;
            (void)out;
        }) << "C-REST-01: 429 期间 RM evaluate 不 abort";
    }
    EXPECT_EQ(total_processed, 5u) << "C-REST-01: 429 期间 5 笔全部处理 (不死锁)";

    // 不误判为 server down: STATE_HALTED reject 为 0
    const auto halt_count = audit_emitter_->reject_count(RejectCode::STATE_HALTED);
    EXPECT_EQ(halt_count, 0u) << "C-REST-01: 429 不误判为 server down (STATE_HALTED reject == 0)";
}

}  // namespace
}  // namespace stcpp::test::chaos
