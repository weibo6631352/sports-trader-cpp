// tests/chaos/latency/c_lat_04_paper_e2e.cpp — C-LAT-04 (paper): paper runtime 全链延迟叠加
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.3 / §1.3 C-PAPER-03
//
// 场景: spike_ms=200, all targets
// 期望:
//   - end-to-end latency p99 < 500ms
//   - PaperSigner.Sign 不超时崩溃
//
// 断言:
//   ASSERT wss_event_loop_tick_p99_us < 50 (R-12)
//   ASSERT paper_signer 不崩溃
//   ASSERT e2e p99 latency < 500ms

#include "tests/chaos/chaos_fixture.hpp"

#include <vector>

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CLat04PaperFixture : public ChaosE2EFixture {};

// C-PAPER-03 / C-LAT-04: paper 全链 200ms 延迟叠加 → e2e p99 < 500ms, signer 不崩
TEST_F(CLat04PaperFixture, C_LAT_04_paper_e2e_latency_p99_under_500ms) {
    // 注入 200ms 全链延迟
    FaultConfig cfg;
    cfg.kind        = FaultKind::LatencySpike;
    cfg.spike_ms    = 200;
    cfg.spike_count = 10;
    cfg.target_rest = true;
    cfg.target_rpc  = true;
    InjectFault(cfg);

    // 收集 30 笔 e2e 延迟样本
    std::vector<std::int64_t> e2e_latencies_ns;
    e2e_latencies_ns.reserve(30);

    for (int i = 0; i < 30; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_lat04_paper_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;

        EXPECT_NO_FATAL_FAILURE({
            auto out = RunOneE2E(b, "sig_lat04_paper_" + std::to_string(i));
            e2e_latencies_ns.push_back(out.latency_ns);
        }) << "C-LAT-04: PaperSigner.Sign 不崩溃 i=" << i;
    }

    ASSERT_EQ(e2e_latencies_ns.size(), 30u) << "C-LAT-04: 30 笔 e2e 完成";

    // e2e p99 < 500ms (500'000'000 ns) — in-process mock 远低于此
    const auto p99_e2e_ns = p99_chaos_ns(e2e_latencies_ns);
    EXPECT_LT(p99_e2e_ns, 500'000'000LL)
        << "C-LAT-04: end-to-end latency p99 "
        << (p99_e2e_ns / 1'000'000) << "ms 必须 < 500ms";

    // WSS tick p99 < 50us (R-12)
    if (!fault_state_.wss_tick_latencies_ns.empty()) {
        const auto wss_p99_us = p99_chaos_ns(fault_state_.wss_tick_latencies_ns) / 1'000;
        EXPECT_LT(wss_p99_us, 50LL)
            << "C-LAT-04 / R-12: wss tick p99 " << wss_p99_us << "us 必须 < 50us";
    }

    // R-11: paper mode 不写 position WAL
    EXPECT_EQ(position_->HighWatermark(), 0u)
        << "C-LAT-04: paper mode 不写 position WAL (R-11)";
}

}  // namespace
}  // namespace stcpp::test::chaos
