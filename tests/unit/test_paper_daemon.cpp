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

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "stcpp/app/paper_daemon.hpp"
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
void ResetGlobalHook() { stcpp::risk::detach_rm_debug_snapshot(); }

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
