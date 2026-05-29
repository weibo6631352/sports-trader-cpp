// src/stcpp/debug_api/debug_server_main.cpp — 观测/调试 API server 可运行入口
// Owner: 小卢 (senior-ic-pool)
// 关联:
//   server.hpp (HttpServer)
//   state_provider.hpp (StubStateProvider) / demo_state_provider.hpp (DemoStateProvider)
//   real_state_provider.hpp (RealStateProvider — book/rejects 已接真)
//   ADR-038 (观测 API) / ADR-037 (本地优先) / R-12 (server 独立线程)
//   frontend/ 观测看板走独立 Vite dev server (localhost:3000), 跨域调此 API
//
// 用途: 启动只读 debug_api 纯 API server (无前端静态托管)。
//   前端走 Vite dev server; CORS 头已注入, 支持跨域访问。
//
// CLI:
//   stcpp_debug_server [--port N] [--host ADDR] [--empty] [--real] [--replay]
//     --port N        监听端口 (默认 8080; 对齐 frontend BASE_URL)
//     --host ADDR     绑定地址 (默认 127.0.0.1; ADR-038 §5 安全默认, 远程走 SSH 隧道)
//     --empty         用 StubStateProvider (全空, 各 endpoint 返回结构合法的空值);
//                     默认用 DemoStateProvider (代表性演示数据, 看板全面板可渲染)
//     --real          用 RealStateProvider (book/rejects 接真实快照, 其余委托 Demo;
//                     data_source="mixed"; 启动时 hub/snap 均空 → 回落 Demo 优雅降级)
//     --replay        与 --real 配合: 启动 ReplayDriver 后台线程持续向 hub Publish
//                     kSynthetic 合成事件 (12 个 demo token, 各盘口 book 实时流动).
//                     不带 --replay 时维持现状 (hub 空 → book 回落 Demo).
//                     R-12: replay 线程独立, hub Publish 原子无阻塞.
//                     R-20: 合成事件 4-ts 全来自 ReplayDriver (非 now() 替代).
//
// 安全: 默认 127.0.0.1 only; 只读 endpoint; 黑名单字段 (私钥/签名字节) 物理不在 schema 中。
// 模式: build-time STCPP_EXEC_MODE_STR 决定 mode 字段 (paper/live/backtest)。

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "stcpp/backtest/replay_driver.hpp"     // ReplayDriver (kSynthetic 模式)
#include "stcpp/data/score_snapshot_store.hpp"  // ScoreSnapshotStore (小段, 集成 ③)
#include "stcpp/risk/ledger_snapshot_hub.hpp"   // LedgerSnapshotHub (集成 ④)
#include "stcpp/sizing/quote_snapshot_hub.hpp"  // QuoteSnapshotHub (集成 ④)

#include "src/stcpp/debug_api/demo_state_provider.hpp"
#include "src/stcpp/debug_api/real_state_provider.hpp"
#include "src/stcpp/debug_api/replay_feed_coordinator.hpp"  // ReplayFeedCoordinator (集成 ④)
#include "src/stcpp/debug_api/server.hpp"
#include "src/stcpp/debug_api/state_provider.hpp"

namespace {

std::atomic<bool> g_stop{false};

void handle_sigint(int /*sig*/) {
    g_stop.store(true, std::memory_order_release);
}

stcpp::debug_api::ExecMode mode_from_build() noexcept {
    using stcpp::debug_api::ExecMode;
    if (std::strcmp(STCPP_EXEC_MODE_STR, "live") == 0) {
        return ExecMode::Live;
    }
    if (std::strcmp(STCPP_EXEC_MODE_STR, "backtest") == 0) {
        return ExecMode::Backtest;
    }
    return ExecMode::Paper;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace stcpp::debug_api;

    std::uint16_t port = 8080;
    std::string host = "127.0.0.1";
    bool empty = false;
    bool real = false;
    bool replay = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (a == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (a == "--empty") {
            empty = true;
        } else if (a == "--real") {
            real = true;
        } else if (a == "--replay") {
            replay = true;
        } else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: stcpp_debug_server [--port N] [--host ADDR] [--empty] [--real] [--replay]\n"
                "  --port N        listen port (default 8080)\n"
                "  --host ADDR     bind address (default 127.0.0.1)\n"
                "  --empty         use empty StubStateProvider (default: DemoStateProvider)\n"
                "  --real          use RealStateProvider (book/rejects=real, rest=demo;\n"
                "                  hub/snap empty at start → graceful fallback to demo)\n"
                "  --replay        with --real: start ReplayDriver background threads feeding hub\n"
                "                  with kSynthetic events (12 demo tokens, all books live-flowing)\n"
                "  (frontend served by Vite dev server, not this process)\n");
            return 0;
        } else {
            std::fprintf(stderr, "[debug_server] unknown arg: %s (try --help)\n", a.c_str());
            return 2;
        }
    }

    // --empty 和 --real 不可同时使用
    if (empty && real) {
        std::fprintf(stderr, "[debug_server] error: --empty and --real are mutually exclusive\n");
        return 2;
    }

    // --replay 必须与 --real 配合
    if (replay && !real) {
        std::fprintf(stderr, "[debug_server] error: --replay requires --real\n");
        return 2;
    }

    const ExecMode mode = mode_from_build();

    // provider 生命周期必须 outlive HttpServer (HttpServer 只持 const 句柄)。
    // --real 模式: 构造空 hub + null snap (生产接入后由外部 vCPU0 写入 hub;
    //              standalone 独立运行时 hub 空 → book/rejects 自动回落 Demo)
    StubStateProvider stub{mode};
    DemoStateProvider demo{mode};

    // hub / score_store / real_provider 用 unique_ptr 管理
    std::unique_ptr<stcpp::polymarket::clob_wss::OrderBookSnapshotHub> hub_owned;
    std::unique_ptr<stcpp::data::ScoreSnapshotStore> score_store_owned;
    std::unique_ptr<RealStateProvider> real_provider;

    // 集成 ④: LedgerSnapshotHub + QuoteSnapshotHub (--replay 时由 ReplayFeedCoordinator Publish)
    std::unique_ptr<stcpp::risk::LedgerSnapshotHub> ledger_hub_owned;
    std::unique_ptr<stcpp::sizing::QuoteSnapshotHub> quote_hub_owned;
    std::unique_ptr<ReplayFeedCoordinator> feed_coordinator;

    // --replay 模式: per-token ReplayDriver 后台线程列表 (RAII — 析构时 Stop()+join)
    // R-12: 各 driver 独立线程, hub.Publish() 原子无阻塞
    std::vector<std::unique_ptr<stcpp::backtest::ReplayDriver>> replay_drivers;

    const char* data_label = "demo";
    if (empty) {
        data_label = "stub-empty";
    } else if (real) {
        data_label = replay ? "real+replay" : "real";
        // 构造独立 hub (standalone 模式, hub 为空 → book 回落 Demo)
        hub_owned = std::make_unique<stcpp::polymarket::clob_wss::OrderBookSnapshotHub>();
        // ScoreSnapshotStore (小段): standalone 空 store, 无 live feed → score 回落 Demo
        // 生产接入时由 Goalserve 采集线程 Publish() 写入
        score_store_owned = std::make_unique<stcpp::data::ScoreSnapshotStore>();
        // 集成 ④: LedgerSnapshotHub + QuoteSnapshotHub
        // --replay 时由 ReplayFeedCoordinator Publish; 无 replay 时空 hub → 回落 Demo
        ledger_hub_owned = std::make_unique<stcpp::risk::LedgerSnapshotHub>();
        quote_hub_owned = std::make_unique<stcpp::sizing::QuoteSnapshotHub>();

        // SizingConfig: 可配置 demo 输入 (计算路径走真实 SizingCalculator, ADR-042)
        SizingConfig sizing_cfg;
        // RiskConfig: 默认 cap 参数 (per_order=10k, per_outcome=25k, market=50k)
        stcpp::risk::RiskConfig risk_cfg;

        // --replay 模式: 构建 MarketTokenMap 并启动 per-token ReplayDriver 后台线程
        // token_map: condition_id → (token0_id, token1_id) 与 DemoStateProvider::derive_token_ids() 对齐
        // R-20: 各 driver 的合成事件 4-ts 来自 SyntheticConfig (非 now() 替代 data_source_ts)
        MarketTokenMap token_map;
        if (replay) {
            // 6 个盘口, 12 个 token — 与 DemoStateProvider kMap 完全对齐
            token_map = {
                {"nba-lal-bos-ml", {"tok-lal-ml-0", "tok-bos-ml-1"}},
                {"nba-lal-bos-total", {"tok-over220-0", "tok-under220-1"}},
                {"nba-lal-bos-spread", {"tok-lal-spd-0", "tok-bos-spd-1"}},
                {"epl-ars-che-total", {"tok-ars-001", "tok-che-001"}},
                {"nfl-kc-buf-spread", {"tok-kc-001", "tok-buf-001"}},
                {"mlb-nyy-bos-ml", {"tok-nyy-001", "tok-bos-nyy-001"}},
            };

            // 每个 token 独立 ReplayDriver (kSynthetic), 各有不同相位/振幅/中间价
            // 使 各盘口 book 动态独立、可视觉区分. 相位偏移通过 base_event_ts_ns 偏移实现.
            // tick_sleep_ns = 200ms (5 Hz), 各盘口双边共享 hub 无竞争 (SWMR R-12).
            struct TokenCfg {
                const char* token_id;
                double mid_center;
                double amplitude;
                double spread;
                double base_size;
                std::int64_t phase_period_ticks;
                std::int64_t base_ts_offset_ns;  // 相位偏移 (互相错开初始角)
            };
            static const TokenCfg kTokenCfgs[] = {
                // nba-lal-bos-ml
                {"tok-lal-ml-0", 0.620, 0.040, 0.020, 1200.0, 20, 0LL},
                {"tok-bos-ml-1", 0.380, 0.040, 0.020, 1100.0, 22, 1'000'000'000LL},
                // nba-lal-bos-total
                {"tok-over220-0", 0.520, 0.030, 0.018, 900.0, 18, 2'000'000'000LL},
                {"tok-under220-1", 0.480, 0.030, 0.018, 850.0, 24, 3'000'000'000LL},
                // nba-lal-bos-spread
                {"tok-lal-spd-0", 0.490, 0.035, 0.022, 700.0, 16, 4'000'000'000LL},
                {"tok-bos-spd-1", 0.510, 0.035, 0.022, 680.0, 19, 5'000'000'000LL},
                // epl-ars-che-total
                {"tok-ars-001", 0.550, 0.045, 0.025, 1500.0, 25, 6'000'000'000LL},
                {"tok-che-001", 0.450, 0.045, 0.025, 1400.0, 21, 7'000'000'000LL},
                // nfl-kc-buf-spread
                {"tok-kc-001", 0.580, 0.050, 0.028, 2000.0, 30, 8'000'000'000LL},
                {"tok-buf-001", 0.420, 0.050, 0.028, 1900.0, 28, 9'000'000'000LL},
                // mlb-nyy-bos-ml
                {"tok-nyy-001", 0.600, 0.038, 0.021, 1000.0, 17, 10'000'000'000LL},
                {"tok-bos-nyy-001", 0.400, 0.038, 0.021, 980.0, 23, 11'000'000'000LL},
            };

            // 单个线程 tick_sleep_ns: 200ms → 5 Hz per token, 所有线程合计约 60 Hz
            // n_ticks: 极大值, 依靠 Stop() 信号提前退出
            constexpr std::int64_t kTickSleepNs = 200'000'000LL;  // 200ms
            constexpr std::int64_t kMaxTicks = std::numeric_limits<std::int64_t>::max() / 2;
            // base_event_ts_ns 公共基准 (R-20: 合成事件 event_ts 起点)
            constexpr std::int64_t kBaseEventTs = 1'700'000'000LL * 1'000'000'000LL;  // 2023-11-14

            replay_drivers.reserve(sizeof(kTokenCfgs) / sizeof(kTokenCfgs[0]));
            for (const auto& tc : kTokenCfgs) {
                stcpp::backtest::SyntheticConfig cfg;
                cfg.mid_center = tc.mid_center;
                cfg.amplitude = tc.amplitude;
                cfg.spread = tc.spread;
                cfg.base_size = tc.base_size;
                cfg.tick = 0.01;
                cfg.base_event_ts_ns = kBaseEventTs + tc.base_ts_offset_ns;
                cfg.tick_interval_ns = 100'000'000LL;  // 100ms event_ts 步长 (R-20 合成 ts)
                cfg.phase_period_ticks = tc.phase_period_ticks;

                auto drv = std::make_unique<stcpp::backtest::ReplayDriver>(
                    *hub_owned, std::string(tc.token_id), std::move(cfg));
                drv->RunAsync(kMaxTicks, kTickSleepNs);
                replay_drivers.push_back(std::move(drv));
            }
            std::printf("[debug_server] --replay: 启动 %zu ReplayDriver 线程 (12 demo token, 5Hz/token)\n",
                        replay_drivers.size());

            // 集成 ④: 构造 ReplayFeedCoordinator — 从 book hub 派生 LedgerFeatures + QuoteFeatures
            // MarketFeedEntry: 6 个盘口, 与 token_map 对齐; avg_entry_price ≈ mid_center - 0.01
            // net_qty: 代表性演示仓位 (USDC; 正=多头)
            // tick_sleep_ns: 200ms (与 ReplayDriver 同频, 5Hz)
            // R-12: coordinator 独立线程, hub.Read()/Publish() 均原子无阻塞
            // R-11: LedgerFeatures.mode = kPaper (coordinator 内部固定)
            // R-20: 4-ts 透传 OrderBookFeatures (data_source_ts 来自合成链路)
            std::vector<MarketFeedEntry> feed_entries = {
                {"nba-lal-bos-ml", "tok-lal-ml-0", 0.610, 1500.0, "LAL"},
                {"nba-lal-bos-total", "tok-over220-0", 0.510, 900.0, "OVER_220.5"},
                {"nba-lal-bos-spread", "tok-lal-spd-0", 0.480, 600.0, "LAL_-5.5"},
                {"epl-ars-che-total", "tok-ars-001", 0.540, 2200.0, "OVER_2.5"},
                {"nfl-kc-buf-spread", "tok-kc-001", 0.570, 1000.0, "KC_-3.5"},
                {"mlb-nyy-bos-ml", "tok-nyy-001", 0.590, 1200.0, "NYY"},
            };
            feed_coordinator = std::make_unique<ReplayFeedCoordinator>(
                *hub_owned, *ledger_hub_owned, *quote_hub_owned, std::move(feed_entries), risk_cfg,
                /*tick_sleep_ns=*/200'000'000LL);
            feed_coordinator->Start();
            std::printf(
                "[debug_server] --replay: ReplayFeedCoordinator 启动 (6 盘口, 200ms/tick)\n"
                "[debug_server]           positions/pnl/quote 随 book microprice 实时流动\n");
        }

        // snap=nullptr: 无 RiskGateway 注入 → risk_rejects 回落 Demo
        real_provider = std::make_unique<RealStateProvider>(
            *hub_owned,
            /*snap=*/nullptr,
            /*score_store=*/score_store_owned.get(),
            /*token_map=*/std::move(token_map), sizing_cfg, risk_cfg, mode,
            /*ledger_hub=*/ledger_hub_owned.get(),  // 集成 ④: positions/pnl hub
            /*quote_hub=*/quote_hub_owned.get());   // 集成 ④: quote hub
    }

    const StateProvider* provider = nullptr;
    if (empty) {
        provider = &stub;
    } else if (real) {
        provider = real_provider.get();
    } else {
        provider = &demo;
    }

    HttpServer server{port, provider, host.c_str()};
    server.start();

    if (!server.is_running()) {
        std::fprintf(stderr, "[debug_server] FATAL: 无法在 %s:%u 启动 (端口被占用?)\n", host.c_str(),
                     static_cast<unsigned>(port));
        return 1;
    }

    std::signal(SIGINT, handle_sigint);
    std::signal(SIGTERM, handle_sigint);

    std::printf("[debug_server] 观测/调试 API @ http://%s:%u  (mode=%s, data=%s)\n", host.c_str(),
                static_cast<unsigned>(port), STCPP_EXEC_MODE_STR, data_label);
    if (real) {
        std::printf(
            "[debug_server] --real 模式: book/rejects=真实快照, score=ScoreSnapshotStore, "
            "quote=SizingCalculator\n");
        std::printf(
            "[debug_server]   score: ScoreSnapshotStore (空 → 回落 demo; Goalserve feed Publish 后生效)\n");
        std::printf("[debug_server]   quote: SizingCalculator 真实 Kelly 计算 (demo 输入, 非 hardcode)\n");
        if (replay) {
            std::printf(
                "[debug_server]   --replay: ReplayDriver kSynthetic, 12 token, 5Hz/token\n"
                "[debug_server]             book_pair best_bid/ask/imbalance/seq 实时流动\n"
                "[debug_server]             R-12: 独立后台线程, hub Publish 原子无阻塞\n"
                "[debug_server]             R-20: 4-ts 来自合成事件 (非 now() 替代 data_source_ts)\n");
        } else {
            std::printf("[debug_server]   standalone 启动: hub/store 空 → book/score 自动回落 demo\n");
        }
    }
    std::printf("[debug_server] endpoints: /healthz /version /status /metrics\n");
    std::printf(
        "[debug_server]            /api/v1/{positions,pnl/timeseries,pnl/attribution,"
        "risk/rejects,gate/paper,market/<id>,book/<id>}\n");
    std::printf("[debug_server] (前端走独立 Vite dev server, 跨域 CORS 已开放)\n");
    std::printf("[debug_server] Ctrl-C 停止\n");
    std::fflush(stdout);

    while (!g_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("\n[debug_server] 收到停止信号, 关闭 server...\n");
    server.stop();

    // 停止 ReplayFeedCoordinator (集成 ④; 先于 ReplayDriver 停止, 避免 hub 被写但读端已消失)
    // RAII: Stop() 内含 join, 析构也会调用 Stop()
    if (feed_coordinator) {
        std::printf("[debug_server] 停止 ReplayFeedCoordinator...\n");
        feed_coordinator->Stop();
        std::printf("[debug_server] ReplayFeedCoordinator 已停止\n");
    }

    // 停止所有 ReplayDriver 后台线程 (Stop() 内含 WaitDone()/join, RAII 保证无泄漏)
    // R-12: Stop() 发信号后线程在当前 tick 完成后退出, 无强杀
    if (!replay_drivers.empty()) {
        std::printf("[debug_server] 停止 %zu ReplayDriver 线程...\n", replay_drivers.size());
        for (auto& drv : replay_drivers) {
            drv->Stop();  // 发停止信号 + join
        }
        replay_drivers.clear();
        std::printf("[debug_server] ReplayDriver 线程已全部停止\n");
    }

    std::printf("[debug_server] 已停止\n");
    return 0;
}
