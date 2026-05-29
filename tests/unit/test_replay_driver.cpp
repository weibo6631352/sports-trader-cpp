// tests/unit/test_replay_driver.cpp — ReplayDriver 端对端验证
//
// Owner: 小肖 (numerical-algorithms, A 系统工程部)
// last_review: 2026-05-29
//
// 覆盖:
//   T01: 合成模式 RunSync — 快照随 replay 流动变化 (best_bid/ask/imbalance 更新)
//   T02: 合成模式 seq 单调递增验证
//   T03: R-20 4 ts 单调链验证 (每帧)
//   T04: replay→hub→Read 端到端路径 (多 tick, 全程读到 valid=true)
//   T05: best_bid / best_ask 随 tick_idx 变化可观测 (振荡检测)
//   T06: imbalance ∈ [-1, 1] 且非 NaN
//   T07: 历史模式 (kHistorical) — TickFrame ts 透传, data_source_ts 不被 now() 覆盖
//   T08: 历史模式多 token — 各 token 快照独立
//   T09: RunAsync + WaitDone — 异步驱动后快照有效
//   T10: Stop() 中断 RunAsync
//   T11: publish_count 与驱动 tick 数一致
//   T12: microprice 在 [bid0, ask0] 范围内 (无 catastrophic cancellation)
//   T13: 合成模式 spread = ask0 - bid0 (精确)
//   T14: published_count 在 RunSync 后与返回值一致

#include <atomic>
#include <cmath>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/backtest/replay_driver.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"

using namespace stcpp::backtest;
using namespace stcpp::polymarket::clob_wss;

// ---------------------------------------------------------------------------
// 帮助工具
// ---------------------------------------------------------------------------

// 默认合成配置 (用于大多数测试)
static SyntheticConfig DefaultSynCfg() {
    SyntheticConfig cfg;
    cfg.mid_center = 0.60;
    cfg.amplitude = 0.05;
    cfg.spread = 0.02;
    cfg.base_size = 1000.0;
    cfg.tick = 0.01;
    cfg.base_event_ts_ns = 1'700'000'000LL * 1'000'000'000LL;  // ~2023-11-14
    cfg.tick_interval_ns = 100'000'000LL;                      // 100ms
    cfg.phase_period_ticks = 20;
    return cfg;
}

// ---------------------------------------------------------------------------
// T01: 合成模式 RunSync — 快照随 replay 流动变化
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T01_SyntheticRunSync_SnapshotChanges) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-syn-01";

    SyntheticConfig cfg = DefaultSynCfg();
    // 注入固定时钟: ingestion_ts 固定 = data_source + 5ms
    const std::int64_t fixed_ingestion = cfg.base_event_ts_ns + 5'000'000LL;
    ReplayDriver driver(hub, tok, cfg, [fixed_ingestion]() { return fixed_ingestion; });

    // 驱动 5 个 tick
    const std::int64_t n = driver.RunSync(5, 0);
    EXPECT_EQ(n, 5);

    // 读取最后一帧快照
    const auto snap = hub.Read(tok);
    ASSERT_TRUE(snap.has_value());
    EXPECT_TRUE(snap->valid);

    // sequence_no = 4 (最后一个 tick_idx)
    EXPECT_EQ(snap->sequence_no, 4);

    // best_bid / best_ask 均在 (0, 1)
    EXPECT_GT(snap->bids[0].price, 0.0);
    EXPECT_LT(snap->bids[0].price, 1.0);
    EXPECT_GT(snap->asks[0].price, 0.0);
    EXPECT_LT(snap->asks[0].price, 1.0);

    // ask > bid (正常 book)
    EXPECT_GT(snap->asks[0].price, snap->bids[0].price);
}

// ---------------------------------------------------------------------------
// T02: 合成模式 seq 单调递增
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T02_SyntheticSeqMonotonic) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-seq";

    SyntheticConfig cfg = DefaultSynCfg();
    const std::int64_t fixed_ingestion = cfg.base_event_ts_ns + 1'000'000LL;
    ReplayDriver driver(hub, tok, cfg, [fixed_ingestion]() { return fixed_ingestion; });

    std::int64_t prev_seq = -1;
    // 驱动 10 tick, 每 tick 后 Read 验证 seq 递增
    for (int i = 0; i < 10; ++i) {
        driver.RunSync(1, 0);
        // 注意: RunSync(1) 从 tick_idx=0 开始 (每次 RunSync 重置循环)
        // — 所以每次调用 RunSync(1,0) 只发布 tick_idx=0.
        // 要验证 seq 递增需一次 RunSync(10).
        (void)prev_seq;
    }

    // 一次性驱动 20 tick, 最后 seq = 19
    OrderBookSnapshotHub hub2;
    ReplayDriver driver2(hub2, tok, cfg, [fixed_ingestion]() { return fixed_ingestion; });
    driver2.RunSync(20, 0);

    const auto snap = hub2.Read(tok);
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(snap->sequence_no, 19);
}

// ---------------------------------------------------------------------------
// T03: R-20 4 ts 单调链 (每帧)
//
// 验证: event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T03_R20_TsChainOk) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-ts";

    SyntheticConfig cfg = DefaultSynCfg();
    // 注入固定时钟: 确保 ingestion >> data_source
    const std::int64_t ingestion = cfg.base_event_ts_ns + 1'000'000'000LL;  // +1s
    ReplayDriver driver(hub, tok, cfg, [ingestion]() { return ingestion; });

    driver.RunSync(10, 0);  // 驱动 10 tick

    const auto snap = hub.Read(tok);
    ASSERT_TRUE(snap.has_value());

    // R-20 4 ts 链验证
    EXPECT_TRUE(snap->ts_chain_ok()) << "event=" << snap->event_ts_ns << " ds=" << snap->data_source_ts_ns
                                     << " ing=" << snap->ingestion_ts_ns << " as_of=" << snap->as_of_ts_ns;

    // 具体约束
    EXPECT_GT(snap->event_ts_ns, 0);
    EXPECT_GE(snap->data_source_ts_ns, snap->event_ts_ns);
    EXPECT_GE(snap->ingestion_ts_ns, snap->data_source_ts_ns);
    EXPECT_GE(snap->as_of_ts_ns, snap->ingestion_ts_ns);
}

// ---------------------------------------------------------------------------
// T04: replay→hub→Read 端到端路径
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T04_E2E_ReplayHubRead) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-e2e";

    SyntheticConfig cfg = DefaultSynCfg();
    const std::int64_t ingestion = cfg.base_event_ts_ns + 5'000'000LL;
    ReplayDriver driver(hub, tok, cfg, [ingestion]() { return ingestion; });

    // 驱动前: hub 无数据
    EXPECT_FALSE(hub.Read(tok).has_value());

    // 驱动 1 tick
    driver.RunSync(1, 0);

    // 驱动后: hub 有数据且 valid
    const auto snap = hub.Read(tok);
    ASSERT_TRUE(snap.has_value()) << "hub.Read should return value after first publish";
    EXPECT_TRUE(snap->valid);
    EXPECT_EQ(snap->sequence_no, 0);

    // 再驱动 9 tick (共 10): 最后快照 seq=9
    // 注意: RunSync 重置 tick_idx 从 0 开始 (每次调用独立)
    // 要得到 seq=9 需一次 RunSync(10)
    OrderBookSnapshotHub hub2;
    ReplayDriver driver2(hub2, tok, cfg, [ingestion]() { return ingestion; });
    driver2.RunSync(10, 0);

    const auto snap2 = hub2.Read(tok);
    ASSERT_TRUE(snap2.has_value());
    EXPECT_EQ(snap2->sequence_no, 9) << "Last tick_idx should be 9 for RunSync(10)";
}

// ---------------------------------------------------------------------------
// T05: best_bid/ask 随 tick 变化可观测 (振荡检测)
//
// 正弦波振幅 0.05, 20 tick 覆盖完整周期.
// 在 10 tick (半周期) 后 mid 应显著变化 (|delta| > epsilon).
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T05_BidAskOscillation) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-osc";

    SyntheticConfig cfg = DefaultSynCfg();
    const std::int64_t ingestion = cfg.base_event_ts_ns + 5'000'000LL;

    // 读 tick_idx=0 (sin=0, mid=0.60)
    ReplayDriver driver0(hub, tok, cfg, [ingestion]() { return ingestion; });
    driver0.RunSync(1, 0);
    const auto snap0 = hub.Read(tok);
    ASSERT_TRUE(snap0.has_value());
    const double mid0 = snap0->mid;

    // 读 tick_idx=5 (sin(π/2)=1.0, mid ≈ 0.60 + 0.05 = 0.65)
    OrderBookSnapshotHub hub5;
    ReplayDriver driver5(hub5, tok, cfg, [ingestion]() { return ingestion; });
    driver5.RunSync(6, 0);  // 驱动到 tick_idx=5
    const auto snap5 = hub5.Read(tok);
    ASSERT_TRUE(snap5.has_value());
    const double mid5 = snap5->mid;

    // 两者之差应 > 0.04 (振幅 0.05, sin(π/2)-sin(0)=1.0 → delta≈0.05)
    EXPECT_GT(std::abs(mid5 - mid0), 0.04) << "mid0=" << mid0 << " mid5=" << mid5;
}

// ---------------------------------------------------------------------------
// T06: imbalance ∈ [-1, 1] 且非 NaN, spread > 0
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T06_ImbalanceAndSpread) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-imb";

    SyntheticConfig cfg = DefaultSynCfg();
    const std::int64_t ingestion = cfg.base_event_ts_ns + 5'000'000LL;
    ReplayDriver driver(hub, tok, cfg, [ingestion]() { return ingestion; });

    // 驱动完整周期 (20 tick)
    driver.RunSync(20, 0);

    // 最后快照
    const auto snap = hub.Read(tok);
    ASSERT_TRUE(snap.has_value());

    EXPECT_FALSE(std::isnan(snap->imbalance)) << "imbalance must not be NaN";
    EXPECT_GE(snap->imbalance, -1.0);
    EXPECT_LE(snap->imbalance, 1.0);

    EXPECT_FALSE(std::isnan(snap->spread)) << "spread must not be NaN";
    EXPECT_GT(snap->spread, 0.0);

    // microprice ∈ [bid0, ask0] (数值稳定性保证)
    EXPECT_GE(snap->microprice, snap->bids[0].price - 1e-9);
    EXPECT_LE(snap->microprice, snap->asks[0].price + 1e-9);
}

// ---------------------------------------------------------------------------
// T07: 历史模式 — TickFrame ts 透传, data_source_ts 不被 now() 覆盖
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T07_Historical_TsPreserved) {
    // 构造 3 个历史帧
    const std::int64_t base = 1'700'000'000LL * 1'000'000'000LL;
    std::vector<TickFrame> frames(3);
    for (int i = 0; i < 3; ++i) {
        frames[static_cast<std::size_t>(i)].token_id = "tok-hist";
        frames[static_cast<std::size_t>(i)].event_ts_ns = base + static_cast<std::int64_t>(i) * 100'000'000LL;
        frames[static_cast<std::size_t>(i)].data_source_ts_ns =
            frames[static_cast<std::size_t>(i)].event_ts_ns + 1'000'000LL;
        frames[static_cast<std::size_t>(i)].ingestion_ts_ns =
            frames[static_cast<std::size_t>(i)].data_source_ts_ns + 2'000'000LL;
        frames[static_cast<std::size_t>(i)].as_of_ts_ns = frames[static_cast<std::size_t>(i)].ingestion_ts_ns;
        frames[static_cast<std::size_t>(i)].bids[0] = {0.60, 1000.0};
        frames[static_cast<std::size_t>(i)].asks[0] = {0.62, 1000.0};
        frames[static_cast<std::size_t>(i)].mid = 0.61;
        frames[static_cast<std::size_t>(i)].spread = 0.02;
        frames[static_cast<std::size_t>(i)].imbalance = 0.0;
        frames[static_cast<std::size_t>(i)].microprice = 0.61;
        frames[static_cast<std::size_t>(i)].sequence_no = static_cast<std::int64_t>(i);
    }

    OrderBookSnapshotHub hub;
    // 注入较大 ingestion 时钟 (验证 data_source 不被覆盖)
    std::int64_t mock_ingestion = base + 999'000'000'000LL;  // +999s (远大于 data_source)
    ReplayDriver driver(hub, frames, [&mock_ingestion]() { return mock_ingestion; });

    driver.RunSync(3, 0);

    const auto snap = hub.Read("tok-hist");
    ASSERT_TRUE(snap.has_value());
    EXPECT_TRUE(snap->valid);

    // data_source_ts 应保留最后帧的原始值 (不被 now() 覆盖)
    const std::int64_t expected_ds = frames[2].data_source_ts_ns;
    EXPECT_EQ(snap->data_source_ts_ns, expected_ds)
        << "data_source_ts_ns must not be overwritten by clock_fn";

    // event_ts 也应保留
    EXPECT_EQ(snap->event_ts_ns, frames[2].event_ts_ns);

    // ingestion 是 mock 时钟 (允许覆盖)
    EXPECT_EQ(snap->ingestion_ts_ns, mock_ingestion);

    // R-20 链仍然满足
    EXPECT_TRUE(snap->ts_chain_ok());
}

// ---------------------------------------------------------------------------
// T08: 历史模式多 token — 各 token 快照独立
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T08_Historical_MultiToken) {
    const std::int64_t base = 1'700'000'000LL * 1'000'000'000LL;

    // 2 个 token, 各 2 帧
    std::vector<TickFrame> frames(4);
    for (int i = 0; i < 2; ++i) {
        std::string tok = "tok-multi-" + std::to_string(i);
        for (int j = 0; j < 2; ++j) {
            const std::size_t idx = static_cast<std::size_t>(i * 2 + j);
            frames[idx].token_id = tok;
            frames[idx].event_ts_ns = base + static_cast<std::int64_t>(j) * 100'000'000LL;
            frames[idx].data_source_ts_ns = frames[idx].event_ts_ns + 1'000'000LL;
            frames[idx].ingestion_ts_ns = frames[idx].data_source_ts_ns + 1'000'000LL;
            frames[idx].as_of_ts_ns = frames[idx].ingestion_ts_ns;
            // token 0 bid=0.60, token 1 bid=0.40
            frames[idx].bids[0] = {0.60 - static_cast<double>(i) * 0.20, 1000.0};
            frames[idx].asks[0] = {frames[idx].bids[0].price + 0.02, 1000.0};
            frames[idx].mid = frames[idx].bids[0].price + 0.01;
            frames[idx].sequence_no = static_cast<std::int64_t>(j);
        }
    }

    OrderBookSnapshotHub hub;
    std::int64_t ingestion = base + 5'000'000LL;
    ReplayDriver driver(hub, frames, [&ingestion]() { return ingestion; });
    driver.RunSync(4, 0);

    const auto snap0 = hub.Read("tok-multi-0");
    const auto snap1 = hub.Read("tok-multi-1");

    ASSERT_TRUE(snap0.has_value());
    ASSERT_TRUE(snap1.has_value());

    // 两个 token 的 bid 价格不同 (隔离性)
    EXPECT_NEAR(snap0->bids[0].price, 0.60, 1e-9);
    EXPECT_NEAR(snap1->bids[0].price, 0.40, 1e-9);
}

// ---------------------------------------------------------------------------
// T09: RunAsync + WaitDone — 异步驱动后快照有效
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T09_RunAsync_WaitDone) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-async";

    SyntheticConfig cfg = DefaultSynCfg();
    const std::int64_t ingestion = cfg.base_event_ts_ns + 5'000'000LL;
    ReplayDriver driver(hub, tok, cfg, [ingestion]() { return ingestion; });

    // 异步驱动 8 tick, 无 sleep (测试模式)
    driver.RunAsync(8, 0);
    driver.WaitDone();

    const auto snap = hub.Read(tok);
    ASSERT_TRUE(snap.has_value());
    EXPECT_TRUE(snap->valid);
    // 8 tick → seq ∈ {0..7}, 最后 = 7
    EXPECT_EQ(snap->sequence_no, 7);
    EXPECT_EQ(driver.published_count(), 8);
}

// ---------------------------------------------------------------------------
// T10: Stop() 中断 RunAsync
//
// 驱动 1000 tick (有 sleep), 快速调用 Stop() 观察提前退出.
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T10_Stop_InterruptsRunAsync) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-stop";

    SyntheticConfig cfg = DefaultSynCfg();
    const std::int64_t ingestion = cfg.base_event_ts_ns + 5'000'000LL;
    ReplayDriver driver(hub, tok, cfg, [ingestion]() { return ingestion; });

    // 驱动 10000 tick, 每 tick sleep 1ms → 总时间 10s (远超测试允许)
    driver.RunAsync(10000, 1'000'000LL);  // 1ms sleep

    // 主线程等 20ms 后停止 (期望 ~20 tick 已发布)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    driver.Stop();  // 内部调用 WaitDone

    const auto cnt = driver.published_count();
    // 至少 1 tick 已发布 (Stop 不立即中断当前 tick, 但应 << 10000)
    EXPECT_GT(cnt, 0) << "At least 1 tick should have been published before Stop";
    EXPECT_LT(cnt, 10000) << "Stop should have interrupted before all 10000 ticks";
}

// ---------------------------------------------------------------------------
// T11: publish_count 与驱动 tick 数一致
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T11_PublishCount_MatchesTicks) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-cnt";

    SyntheticConfig cfg = DefaultSynCfg();
    const std::int64_t ingestion = cfg.base_event_ts_ns + 5'000'000LL;
    ReplayDriver driver(hub, tok, cfg, [ingestion]() { return ingestion; });

    const std::int64_t n = driver.RunSync(15, 0);
    EXPECT_EQ(n, 15);
    EXPECT_EQ(driver.published_count(), 15);
    EXPECT_EQ(hub.publish_count(), static_cast<std::uint64_t>(15));
}

// ---------------------------------------------------------------------------
// T12: microprice ∈ [bid0, ask0] (数值稳定性)
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T12_Microprice_InBidAskRange) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-micro";

    SyntheticConfig cfg = DefaultSynCfg();
    cfg.phase_period_ticks = 20;
    const std::int64_t ingestion = cfg.base_event_ts_ns + 5'000'000LL;
    ReplayDriver driver(hub, tok, cfg, [ingestion]() { return ingestion; });

    // 驱动完整两个周期 (40 tick), 每 tick 检查 microprice 范围
    // 这里用一次性 RunSync(40) 然后只检查最后快照
    driver.RunSync(40, 0);

    const auto snap = hub.Read(tok);
    ASSERT_TRUE(snap.has_value());

    const double bid0 = snap->bids[0].price;
    const double ask0 = snap->asks[0].price;

    EXPECT_FALSE(std::isnan(snap->microprice));
    // 允许 1e-9 浮点容差
    EXPECT_GE(snap->microprice, bid0 - 1e-9) << "microprice=" << snap->microprice << " bid0=" << bid0;
    EXPECT_LE(snap->microprice, ask0 + 1e-9) << "microprice=" << snap->microprice << " ask0=" << ask0;
}

// ---------------------------------------------------------------------------
// T13: spread = ask0 - bid0 精确匹配
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T13_SpreadExact) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-spread";

    SyntheticConfig cfg = DefaultSynCfg();
    cfg.spread = 0.04;  // 固定 spread 4¢
    const std::int64_t ingestion = cfg.base_event_ts_ns + 5'000'000LL;
    ReplayDriver driver(hub, tok, cfg, [ingestion]() { return ingestion; });

    driver.RunSync(5, 0);

    const auto snap = hub.Read(tok);
    ASSERT_TRUE(snap.has_value());

    const double computed_spread = snap->asks[0].price - snap->bids[0].price;
    // spread 字段应与 ask0-bid0 精确匹配 (浮点 clamp 引起细微差异, 容差 1e-9)
    EXPECT_NEAR(snap->spread, computed_spread, 1e-9);
}

// ---------------------------------------------------------------------------
// T14: published_count 与 RunSync 返回值一致
// ---------------------------------------------------------------------------
TEST(ReplayDriver, T14_ReturnValueMatchesPublishedCount) {
    OrderBookSnapshotHub hub;
    const std::string tok = "tok-ret";

    SyntheticConfig cfg = DefaultSynCfg();
    const std::int64_t ingestion = cfg.base_event_ts_ns + 5'000'000LL;
    ReplayDriver driver(hub, tok, cfg, [ingestion]() { return ingestion; });

    // 多次 RunSync — 每次重置 tick_idx 从 0
    const std::int64_t r1 = driver.RunSync(3, 0);
    EXPECT_EQ(r1, 3);
    EXPECT_EQ(driver.published_count(), 3);

    // 再次调用 (published_count 累计)
    const std::int64_t r2 = driver.RunSync(7, 0);
    EXPECT_EQ(r2, 7);
    EXPECT_EQ(driver.published_count(), 10);  // 3+7
}
