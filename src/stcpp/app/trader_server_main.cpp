// src/stcpp/app/trader_server_main.cpp — paper daemon + 运营观测 HTTP 入口 (thin main)
//
// (原名 debug_server_main.cpp / stcpp_debug_server —— W9 骨架遗留名, 2026-05-30 改名:
//  它是 paper daemon 的运营观测 HTTP 服务, 非调试工具. headless 版见 paper_runtime_main.cpp)
//
// Owner: 老雷 (GM) — TraderDaemon 重构 (老郭 §A.1): 962 行装配逻辑已抽进
//   stcpp::app::TraderDaemon (stcpp_trader_app 库). 本 main 退化为 ~80 行:
//   parse args → 填 TraderDaemonConfig (RunMode::TraderDaemon) → Build → Run.
//
// last_review: 2026-05-30
//
// 角色: RunMode::TraderDaemon —— 带 HTTP 观测端 (= 原 stcpp_trader_server).
//   live book/event = 真实 Polymarket CLOB WSS + gamma /events
//   score = 真实 Goalserve inplay feed (soccer/basketball/tennis)
//   positions/pnl/quote = paper 交易循环驱动 (TradingLoop, 500ms tick)
//
// 红线: R-11 (paper 不污染真账本) / R-12 (后台线程独立) / R-20 (4 ts 透传) 由 TraderDaemon 守护.
// ToS: 只读公开 book channel + gamma REST, 不下单.

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "stcpp/app/trader_daemon.hpp"
#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/process/single_instance.hpp"  // 程序级防多开 (PID+flock)

namespace {

// 信号 → daemon 停止. RequestStop 是 noexcept, 信号上下文安全.
std::atomic<stcpp::app::TraderDaemon*> g_daemon{nullptr};

void handle_signal(int /*sig*/) {
    if (auto* d = g_daemon.load(std::memory_order_acquire)) {
        d->RequestStop();
    }
}

}  // namespace

int main(int argc, char** argv) {
    stcpp::app::TraderDaemonConfig cfg;
    cfg.mode = stcpp::app::RunMode::TraderDaemon;

    // 2026-06-12 架构合理化: mode 运行时单参数 (--mode paper|live, 默认 paper)。
    //   单 binary 双模式; live 需三重显式条件 (--mode live + 凭证齐全 + LIVE_ARMED=1)。
    stcpp::execution::ExecutionMode exec_mode = stcpp::execution::ExecutionMode::Paper;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            const char* m = argv[i + 1];
            if (std::strcmp(m, "live") == 0) {
                exec_mode = stcpp::execution::ExecutionMode::Live;
            } else if (std::strcmp(m, "paper") != 0) {
                std::fprintf(stderr, "[paper_server] FATAL: --mode 只接受 paper|live (got: %s)\n", m);
                return 2;
            }
        }
    }
    // R-11 一致性闸: PAPER_MODE=1 环境 (systemd 固化) 与 --mode live 互斥 → 拒启动。
    // live fail-fast: 凭证四件套入口即查 (不等装配 — 缺凭证的 live 进程一秒都不该跑)。
    if (exec_mode == stcpp::execution::ExecutionMode::Live) {
        const char* pm = std::getenv("PAPER_MODE");
        if (pm != nullptr && std::strcmp(pm, "1") == 0) {
            std::fprintf(stderr, "[paper_server] FATAL (R-11): PAPER_MODE=1 与 --mode live 冲突. abort.\n");
            return 2;
        }
        for (const char* k : {"POLYMARKET_API_KEY", "POLYMARKET_SECRET", "POLYMARKET_PASSPHRASE",
                              "POLYMARKET_PRIVATE_KEY"}) {
            const char* v = std::getenv(k);
            if (v == nullptr || v[0] == '\0') {
                std::fprintf(stderr, "[paper_server] FATAL (live fail-fast): 缺凭证 %s. abort.\n", k);
                return 2;
            }
        }
    }
    stcpp::execution::ExecutionContext::Init(exec_mode);
    cfg.exec_mode = (exec_mode == stcpp::execution::ExecutionMode::Live)
                        ? stcpp::debug_api::ExecMode::Live
                        : stcpp::debug_api::ExecMode::Paper;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--mode" && i + 1 < argc) {
            ++i;  // 已在上方预解析
        } else if (a == "--port" && i + 1 < argc) {
            cfg.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (a == "--host" && i + 1 < argc) {
            cfg.host = argv[++i];
        } else if (a == "--verbose" || a == "-v") {
            cfg.verbose = true;
        } else if (a == "--no-record-ml") {
            cfg.record_ml = false;
        } else if (a == "--ml-path" && i + 1 < argc) {
            cfg.ml_path = argv[++i];
        // (--ml-poll-sec 已砍 2026-06-05: FeatureRecorder 删, ml_poll_sec 无消费方)
        } else if (a == "--no-paper") {
            cfg.enable_paper_trading = false;  // 仅观测, 不起 TradingLoop
        } else if (a == "--enable-fills") {
            cfg.enable_paper_fills = true;  // P0-3: 默认仅观测, 显式开火
        } else if (a == "--help" || a == "-h") {
            // (大模型训练/ONNX 推理 flags 已砍 2026-06-05 老板「砍掉大模型训练功能」: --onnx-model /
            //  --auto-train / --train-python / --train-script / --min-train-samples / --train-window-days /
            //  --model-reload-interval。保留量化因子/统计计算。模型永不加载/训练/驱动 fair。)
            std::printf(
                "usage: stcpp_trader_server [--port N] [--host ADDR] [--verbose] [--no-paper]\n"
                "  --port N         listen port (default 8080)\n"
                "  --host ADDR      bind address (default 127.0.0.1)\n"
                "  --verbose        extra WSS/parser debug logging\n"
                "  --no-paper       observe only, do not run TradingLoop\n"
                "  --enable-fills   解封 paper 成交 (默认仅观测; 保守默认, 显式才开火)\n"
                "  (大模型训练/ONNX 推理 flags 已砍 2026-06-05; 量化因子/统计计算保留)\n"
                "\n"
                "RunMode::TraderDaemon — paper trading loop + HTTP observability API.\n"
                "  live book: gamma /events discovery -> CLOB WSS market channel\n"
                "  live score: Goalserve inplay (soccer/basketball/tennis)\n"
                "  positions/pnl/quote: TradingLoop (R-11 isolated, ToS: VirtualFill only)\n"
                "  (frontend served by Vite dev server, not this process)\n");
            return 0;
        } else {
            std::fprintf(stderr, "[paper_server] unknown arg: %s (try --help)\n", a.c_str());
            return 2;
        }
    }

    // 程序级防多开 (全局引擎锁; GM 决议: 任意 mode 只许一个引擎实例, 省资源)。
    //   防双订阅 WSS (ToS) / 双写账本污染状态 / (live) 双下真单。
    //   --help 在上方已 return; 锁仅真启动时 acquire (R-12: 仅启动期)。
    std::optional<stcpp::infra::process::SingleInstanceLock> instance_lock;
    try {
        instance_lock.emplace(stcpp::infra::process::kGlobalEngine);
    } catch (const stcpp::infra::process::SingleInstanceLockFailure& e) {
        std::fprintf(stderr,
                     "[paper_server] 拒绝多开: 已有引擎实例 (mode=%s) 运行中 (PID %lld)。\n"
                     "  锁文件: %s\n  一台机只许一个引擎; 先停旧实例再起。\n",
                     e.exec_mode_str.c_str(), static_cast<long long>(e.existing_pid),
                     stcpp::infra::process::SingleInstanceLock::global_engine_path().c_str());
        return 3;
    }

    stcpp::app::TraderDaemon daemon(std::move(cfg));
    g_daemon.store(&daemon, std::memory_order_release);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const stcpp::app::BuildResult br = daemon.Build();
    if (!br.ok) {
        std::fprintf(stderr, "[paper_server] FATAL: TraderDaemon::Build 失败: %s\n", br.error.c_str());
        return 1;
    }

    const int rc = daemon.Run();  // Start + WaitForStop(SIGINT/SIGTERM) + Shutdown
    g_daemon.store(nullptr, std::memory_order_release);
    std::printf("[paper_server] 已停止\n");
    return rc;
}
