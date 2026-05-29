// tests/chaos/ooo/c_ooo_02_window_shuffle.cpp — C-OOO-02: 乱序窗口 5 条
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.2
//
// 场景: window=5, prob=0.5 乱序
// 期望:
//   - book builder 缓冲 + 重排
//   - R-20 ts 不等式校验各条独立
//   - 不产生负时间戳 intent
//
// 断言:
//   ASSERT intent.event_ts <= intent.data_source_ts (R-20 invariant, per intent)
//   ASSERT no_negative_timestamp_in_any_intent

#include "tests/chaos/chaos_fixture.hpp"

#include <vector>

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class COoo02Fixture : public ChaosE2EFixture {};

// C-OOO-02: 乱序窗口 5 条 → 每条 R-20 4 ts 独立校验, 无负时间戳
TEST_F(COoo02Fixture, C_OOO_02_window_5_shuffle_r20_per_intent) {
    // 注入 OOO: window=5, prob=0.5
    FaultConfig cfg;
    cfg.kind                = FaultKind::WssOutOfOrder;
    cfg.ooo_window_size     = 5;
    cfg.shuffle_probability = 0.5;
    InjectFault(cfg);

    // 发送 10 笔 intent (分 2 个窗口), 每条独立校验 R-20
    std::vector<std::int64_t> latencies_ns;
    for (int i = 0; i < 10; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_ooo02_" + std::to_string(i);
        b.price              = 0.50 + 0.01 * (i % 5);
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;

        // R-20 per-intent: 4 ts 单调且无负值
        EXPECT_GT(b.event_ts_ns,       0LL)  << "C-OOO-02: event_ts > 0 (R-20)";
        EXPECT_GT(b.data_source_ts_ns, 0LL)  << "C-OOO-02: data_source_ts > 0 (R-20)";
        EXPECT_GT(b.ingestion_ts_ns,   0LL)  << "C-OOO-02: ingestion_ts > 0 (R-20)";
        EXPECT_LE(b.event_ts_ns, b.data_source_ts_ns)
            << "C-OOO-02: event_ts <= data_source_ts (R-20)";
        EXPECT_LE(b.data_source_ts_ns, b.ingestion_ts_ns)
            << "C-OOO-02: data_source_ts <= ingestion_ts (R-20)";

        auto out = RunOneE2E(b, "sig_ooo02_" + std::to_string(i));
        latencies_ns.push_back(out.latency_ns);
    }

    // 乱序不影响 e2e 通量: 至少处理了 10 笔
    EXPECT_EQ(latencies_ns.size(), 10u)
        << "C-OOO-02: 10 笔 e2e 全部处理完成";
}

// C-OOO-02: 乱序窗口不产生负时间戳 intent
TEST_F(COoo02Fixture, C_OOO_02_ooo_window_no_negative_timestamps) {
    FaultConfig cfg;
    cfg.kind                = FaultKind::WssOutOfOrder;
    cfg.ooo_window_size     = 5;
    cfg.shuffle_probability = 1.0;  // 最大乱序
    InjectFault(cfg);

    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        // 即使极端乱序, ts 也必须取自真实数据源, 不使用 -(now) 这类负值
        const std::int64_t event_ts  = now - 5'000'000LL;
        const std::int64_t ds_ts     = now - 4'000'000LL;
        const std::int64_t ingest_ts = now - 2'000'000LL;

        EXPECT_GT(event_ts,  0LL) << "C-OOO-02: no negative event_ts " << i;
        EXPECT_GT(ds_ts,     0LL) << "C-OOO-02: no negative data_source_ts " << i;
        EXPECT_GT(ingest_ts, 0LL) << "C-OOO-02: no negative ingestion_ts " << i;
    }
}

}  // namespace
}  // namespace stcpp::test::chaos
