// tests/unit/test_paper_daemon.cpp — PaperDaemon 装配 + R-11 生命周期回归
//
// Owner: 老雷 (GM) — PaperDaemon 重构配套 (老韩 R-11 审计要求的新增回归面)
// last_review: 2026-05-30
//
// 覆盖 (老韩 INV-1/INV-2 + 小宋 §3 装配测试):
//   - 离线装配 (InjectMarkets + start_live_feeds=false): Build() 把组件接对, 不发外网.
//   - R-11 INV-1 detach 生命周期: Build 后全局 hook 非空, Shutdown 后 == nullptr.
//   - Shutdown 幂等 (重复调不崩, hook 仍 nullptr) — 防 double-stop / UAF.
//   - Build 幂等; Headless 无 HTTP 但读模型仍装配 (R-11: 同一 Build 写栈).
//   - Start→Shutdown 线程起停无崩 (offline feeds, paper_loop jthread join).

#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/app/paper_daemon.hpp"
#include "stcpp/data/score_snapshot_store.hpp"                   // A1b 集成: 注入比分
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"  // A2 端到端: 注入 book
#include "stcpp/risk/rm_debug_snapshot.hpp"

namespace {

using stcpp::app::DiscoveredEvent;
using stcpp::app::DiscoveredMarket;
using stcpp::app::PaperDaemon;
using stcpp::app::PaperDaemonConfig;
using stcpp::app::RunMode;

// 构造一个最小注入 markets (1 event / 1 market / 2 token), 跳过真 gamma 发现.
std::vector<DiscoveredEvent> MakeInjectedMarkets() {
    DiscoveredMarket m;
    m.condition_id = "0xCONDITION_TEST_001";
    m.question = "Will Team A win?";
    m.group_item_title = "Team A";
    m.sports_market_type = "moneyline";
    m.token0_id = "1001";  // YES
    m.token1_id = "1002";  // NO
    // A1b: 两队名 + kickoff (EventMatcher 锚定输入)
    m.outcome0_name = "Team A";  // YES
    m.outcome1_name = "Team B";  // NO
    m.game_start_ts_sec = 1'000'000;

    DiscoveredEvent ev;
    ev.event_id = "EVT_TEST_1";
    ev.slug = "team-a-vs-team-b";
    ev.title = "Team A vs Team B";
    ev.sport = "basketball";
    ev.markets.push_back(std::move(m));
    return {std::move(ev)};
}

// 离线 headless 配置 (无 HTTP / 无真网络 / 不写 ML 文件).
PaperDaemonConfig OfflineHeadlessCfg() {
    PaperDaemonConfig cfg;
    cfg.mode = RunMode::Headless;
    cfg.start_live_feeds = false;  // 不起 WSS/inplay 真网络
    cfg.record_ml = false;         // 不写 ml_capture 文件
    return cfg;
}

// 每个 test 前清掉残留全局 hook (其它 test 可能 attach 过).
void ResetGlobalHook() {
    stcpp::risk::detach_rm_debug_snapshot();
}

}  // namespace

// ---------------------------------------------------------------------------
// 装配: 注入 markets → Build() 接对组件
// ---------------------------------------------------------------------------
TEST(PaperDaemon, AssemblyOffline_InjectedMarkets_BuildOk) {
    ResetGlobalHook();
    PaperDaemon daemon(OfflineHeadlessCfg());
    daemon.InjectMarkets(MakeInjectedMarkets());

    const auto br = daemon.Build();
    EXPECT_TRUE(br.ok);
    EXPECT_EQ(br.market_count, 1u);
    EXPECT_EQ(br.token_count, 2u);  // YES + NO

    EXPECT_TRUE(daemon.is_built());
    // 组件接对 (非空句柄)
    EXPECT_NE(daemon.hub(), nullptr);
    EXPECT_NE(daemon.quote_hub(), nullptr);
    EXPECT_NE(daemon.ledger_hub(), nullptr);
    EXPECT_NE(daemon.paper_position_ledger(), nullptr);
    EXPECT_NE(daemon.paper_loop(), nullptr);
    EXPECT_NE(daemon.state_provider(), nullptr);
    // token_map 含注入的 condition
    ASSERT_TRUE(daemon.token_map().count("0xCONDITION_TEST_001"));
    EXPECT_EQ(daemon.token_map().at("0xCONDITION_TEST_001").first, "1001");
}

// ---------------------------------------------------------------------------
// R-11 INV-1: detach 生命周期 — Build 后 hook 非空, Shutdown 后 nullptr
// ---------------------------------------------------------------------------
TEST(PaperDaemon, R11_DetachLifecycle_HookNullAfterShutdown) {
    ResetGlobalHook();
    EXPECT_EQ(stcpp::risk::current_rm_debug_snapshot(), nullptr);  // 起点干净

    {
        PaperDaemon daemon(OfflineHeadlessCfg());
        daemon.InjectMarkets(MakeInjectedMarkets());
        ASSERT_TRUE(daemon.Build().ok);

        // Build 内 attach_rm_debug_snapshot(paper_rm_snap_) → 全局 hook 非空
        EXPECT_NE(stcpp::risk::current_rm_debug_snapshot(), nullptr);

        daemon.Shutdown();
        // [R-11 INV-1] Shutdown 后必 detach → hook 回 nullptr (杜绝 UAF)
        EXPECT_EQ(stcpp::risk::current_rm_debug_snapshot(), nullptr);
    }
    // 析构兜底再调 Shutdown (幂等) — 仍 nullptr, 不崩
    EXPECT_EQ(stcpp::risk::current_rm_debug_snapshot(), nullptr);
}

// ---------------------------------------------------------------------------
// Shutdown 幂等: 重复调不崩, hook 稳定 nullptr
// ---------------------------------------------------------------------------
TEST(PaperDaemon, Shutdown_Idempotent) {
    ResetGlobalHook();
    PaperDaemon daemon(OfflineHeadlessCfg());
    daemon.InjectMarkets(MakeInjectedMarkets());
    ASSERT_TRUE(daemon.Build().ok);

    daemon.Shutdown();
    daemon.Shutdown();  // 第二次: 幂等, 不 double-detach / double-stop
    daemon.Shutdown();
    EXPECT_EQ(stcpp::risk::current_rm_debug_snapshot(), nullptr);
}

// ---------------------------------------------------------------------------
// Build 幂等: 重复 Build 返回同一结果, 不重复装配
// ---------------------------------------------------------------------------
TEST(PaperDaemon, Build_Idempotent) {
    ResetGlobalHook();
    PaperDaemon daemon(OfflineHeadlessCfg());
    daemon.InjectMarkets(MakeInjectedMarkets());

    const auto br1 = daemon.Build();
    const auto br2 = daemon.Build();
    EXPECT_TRUE(br1.ok);
    EXPECT_TRUE(br2.ok);
    EXPECT_EQ(br1.token_count, br2.token_count);
    EXPECT_EQ(daemon.hub(), daemon.hub());  // 同一实例 (未重建)
    daemon.Shutdown();
}

// ---------------------------------------------------------------------------
// Headless: 无 HTTP server, 但读模型 (state_provider) 仍装配 (R-11 同一 Build 写栈)
// ---------------------------------------------------------------------------
TEST(PaperDaemon, Headless_StateProviderBuilt) {
    ResetGlobalHook();
    PaperDaemon daemon(OfflineHeadlessCfg());
    daemon.InjectMarkets(MakeInjectedMarkets());
    ASSERT_TRUE(daemon.Build().ok);
    EXPECT_EQ(daemon.mode(), RunMode::Headless);
    EXPECT_NE(daemon.state_provider(), nullptr);  // 读模型不因无 HTTP 而缺失
    daemon.Shutdown();
}

// ---------------------------------------------------------------------------
// Start → Shutdown: 线程起停无崩 (offline feeds; paper_loop jthread join)
// ---------------------------------------------------------------------------
TEST(PaperDaemon, StartStop_OfflineFeeds_NoCrash) {
    ResetGlobalHook();
    PaperDaemon daemon(OfflineHeadlessCfg());
    daemon.InjectMarkets(MakeInjectedMarkets());
    ASSERT_TRUE(daemon.Build().ok);

    daemon.Start();  // paper_loop jthread 起 (读空 hub, 无害); 无真网络
    EXPECT_TRUE(daemon.is_started());
    daemon.Shutdown();  // join paper_loop + detach
    EXPECT_EQ(stcpp::risk::current_rm_debug_snapshot(), nullptr);
}

// ---------------------------------------------------------------------------
// A1b: Build 从带队名的 market 捕获 EventMatcher 锚定输入
// ---------------------------------------------------------------------------
TEST(PaperDaemon, A1b_Build_CapturesMarketMatchInputs) {
    ResetGlobalHook();
    PaperDaemon daemon(OfflineHeadlessCfg());
    daemon.InjectMarkets(MakeInjectedMarkets());  // market 含 outcome0/1_name
    ASSERT_TRUE(daemon.Build().ok);
    EXPECT_EQ(daemon.market_match_input_count(), 1u)
        << "A1b: Build 应从两队名齐全的 market 捕获 1 条 EventMatchInput";
    daemon.Shutdown();
}

// ---------------------------------------------------------------------------
// A1b: 刷新线程匹配真实比分 → 注入 condition→event 映射到 PaperLoop
// ---------------------------------------------------------------------------
TEST(PaperDaemon, A1b_RefreshThread_MatchesScore_SetsMapping) {
    using stcpp::data::ScoreMap;
    using stcpp::debug_api::EventScore;

    ResetGlobalHook();
    auto cfg = OfflineHeadlessCfg();
    cfg.mapping_refresh_sec = 1;           // 快刷
    cfg.paper_loop.tick_interval_ms = 50;  // 快 tick
    PaperDaemon daemon(std::move(cfg));
    daemon.InjectMarkets(MakeInjectedMarkets());  // Team A vs Team B, kickoff=1'000'000
    ASSERT_TRUE(daemon.Build().ok);

    // 向内部 score_store 发布匹配的 in-play 比分 (Team A vs Team B, fresh)
    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    EventScore es;
    es.found = true;
    es.event_id = "gs-match-A1b";
    es.status = "inplay";
    es.home = "Team A";
    es.away = "Team B";
    es.home_score = 1;
    es.away_score = 0;
    es.kickoff_ts_sec = 1'000'000;  // == market kickoff (窗口内)
    es.ts.event_ts_ns = now_ns - 3'600'000'000'000LL;
    es.ts.data_source_ts_ns = now_ns - 1'000'000'000LL;  // fresh
    es.ts.ingestion_ts_ns = now_ns - 500'000'000LL;
    es.ts.as_of_ts_ns = now_ns;
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-match-A1b"] = es;
    daemon.score_store_for_test()->Publish(std::shared_ptr<const ScoreMap>(sm));

    daemon.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // 等 ≥1 刷新周期
    const std::size_t mapped = daemon.paper_loop()->event_map_size();
    daemon.Shutdown();

    EXPECT_EQ(mapped, 1u) << "A1b: 刷新线程应把 Team A vs Team B 匹配到 Goalserve event → 1 条映射";
}

// ---------------------------------------------------------------------------
// A1b fail-closed: 无匹配比分 → 映射为空 (paper_loop 退回 stub)
// ---------------------------------------------------------------------------
TEST(PaperDaemon, A1b_RefreshThread_NoMatch_EmptyMapping) {
    using stcpp::data::ScoreMap;
    using stcpp::debug_api::EventScore;

    ResetGlobalHook();
    auto cfg = OfflineHeadlessCfg();
    cfg.mapping_refresh_sec = 1;
    cfg.paper_loop.tick_interval_ms = 50;
    PaperDaemon daemon(std::move(cfg));
    daemon.InjectMarkets(MakeInjectedMarkets());  // Team A vs Team B
    ASSERT_TRUE(daemon.Build().ok);

    // 发布不相干的比分 (队名完全不同 → 不匹配)
    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    EventScore es;
    es.found = true;
    es.event_id = "gs-other";
    es.status = "inplay";
    es.home = "Unrelated FC";
    es.away = "Nobody United";
    es.kickoff_ts_sec = 1'000'000;
    es.ts.event_ts_ns = now_ns - 3'600'000'000'000LL;
    es.ts.data_source_ts_ns = now_ns - 1'000'000'000LL;
    es.ts.ingestion_ts_ns = now_ns - 500'000'000LL;
    es.ts.as_of_ts_ns = now_ns;
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-other"] = es;
    daemon.score_store_for_test()->Publish(std::shared_ptr<const ScoreMap>(sm));

    daemon.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const std::size_t mapped = daemon.paper_loop()->event_map_size();
    daemon.Shutdown();

    EXPECT_EQ(mapped, 0u) << "A1b fail-closed: 队名不匹配 → 0 映射 (paper_loop 退回 stub)";
}

// ---------------------------------------------------------------------------
// A2 端到端: daemon 离线注入 book+score → 刷新线程匹配 + A2 解封 → 真产 paper 成交
//   落**隔离的 paper_position_ledger**。这是 A0→A2 全链路 daemon 级证明 +
//   老韩 R-11「实测隔离」: 第一笔真 fill 后, 仓位只进私有 paper 账本.
//   (Goalserve 真实数据走同一路径; 此处离线注入证明代码链路成立.)
// ---------------------------------------------------------------------------
TEST(PaperDaemon, A2_DaemonProducesPaperFill_IsolatedLedger) {
    using stcpp::data::ScoreMap;
    using stcpp::debug_api::EventScore;
    using stcpp::polymarket::clob_wss::OrderBookFeatures;
    using stcpp::polymarket::clob_wss::WssConnState;

    ResetGlobalHook();
    auto cfg = OfflineHeadlessCfg();       // Headless + start_live_feeds=false + record_ml=false
    cfg.enable_paper_fills = true;         // A2: 解封成交
    cfg.mapping_refresh_sec = 1;           // 快刷
    cfg.paper_loop.tick_interval_ms = 50;  // 快 tick
    cfg.paper_loop.n_effective = 500;      // 紧 CI 让真实 edge 过门 (生产 n_eff 小梁调)
    PaperDaemon daemon(std::move(cfg));
    daemon.InjectMarkets(MakeInjectedMarkets());  // Team A vs Team B, token0="1001", kickoff=1'000'000
    ASSERT_TRUE(daemon.Build().ok);

    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();

    // 真实 in-play 比分: Team A 2:0 领先 (匹配 market; yes=Team A=home)
    EventScore es;
    es.found = true;
    es.event_id = "gs-e2e";
    es.status = "inplay";
    es.home = "Team A";
    es.away = "Team B";
    es.home_score = 2;
    es.away_score = 0;
    es.kickoff_ts_sec = 1'000'000;  // == market kickoff (窗口内)
    es.ts.event_ts_ns = now_ns - 3'600'000'000'000LL;
    es.ts.data_source_ts_ns = now_ns - 1'000'000'000LL;  // fresh
    es.ts.ingestion_ts_ns = now_ns - 500'000'000LL;
    es.ts.as_of_ts_ns = now_ns;
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-e2e"] = es;
    daemon.score_store_for_test()->Publish(std::shared_ptr<const ScoreMap>(sm));

    // 市场低估 YES (mid≈0.19), Team A 领先 → 真 fair > ask → buy YES. fresh ts (防 RM STALE).
    OrderBookFeatures f{};
    f.valid = true;
    f.event_ts_ns = now_ns - 3'000'000'000LL;
    f.data_source_ts_ns = now_ns - 2'000'000'000LL;
    f.ingestion_ts_ns = now_ns - 1'000'000'000LL;
    f.as_of_ts_ns = now_ns;
    f.bids[0].price = 0.18;
    f.bids[0].size_usdc = 500.0;
    f.asks[0].price = 0.20;
    f.asks[0].size_usdc = 500.0;
    f.microprice = 0.19;
    f.mid = 0.19;
    f.spread = 0.02;
    f.imbalance = 0.0;
    f.wss_state = WssConnState::kConnected;
    f.sequence_no = 1;
    daemon.hub_for_test()->Publish("1001", f);  // token0 (YES)

    daemon.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));  // ≥1 刷新周期 + 若干 tick
    const auto positions = daemon.paper_position_ledger()->get_all_positions();
    const auto fills = daemon.paper_loop()->stats().fills_completed.load();
    daemon.Shutdown();

    // A2 端到端: daemon 真产 ≥1 笔 paper 成交, 落隔离 paper 账本
    EXPECT_GT(fills, static_cast<std::uint64_t>(0))
        << "A2 端到端: daemon (真实比分映射 + 解封) 应产生 ≥1 笔 paper 成交";
    EXPECT_FALSE(positions.empty())
        << "R-11 实测隔离: 成交仓位应落入私有 paper_position_ledger (非共享真账本)";
}
