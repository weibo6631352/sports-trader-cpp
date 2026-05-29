// src/stcpp/debug_api/debug_server_main.cpp — 观测/调试 API server 可运行入口
// Owner: 老雷 (GM) 驱动前端集成 — 2026-05-29
// 关联:
//   server.hpp (HttpServer)
//   state_provider.hpp (StubStateProvider) / demo_state_provider.hpp (DemoStateProvider)
//   ADR-038 (观测 API) / ADR-037 (本地优先) / R-12 (server 独立线程)
//   frontend/ 观测看板默认连 127.0.0.1:8080
//
// 用途: 启动只读 debug_api server, 让 frontend/ 看板连真后端而非 ?stub=1 mock。
//   这是"能看到任何已经开发的功能状态"的后端落地点。
//
// CLI:
//   stcpp_debug_server [--port N] [--host ADDR] [--empty]
//     --port N     监听端口 (默认 8080; 对齐 frontend BASE_URL)
//     --host ADDR  绑定地址 (默认 127.0.0.1; ADR-038 §5 安全默认, 远程走 SSH 隧道)
//     --empty      用 StubStateProvider (全空, 各 endpoint 返回结构合法的空值);
//                  默认用 DemoStateProvider (代表性演示数据, 看板全面板可渲染)
//
// 安全: 默认 127.0.0.1 only; 只读 endpoint; 黑名单字段 (私钥/签名字节) 物理不在 schema 中。
// 模式: build-time STCPP_EXEC_MODE_STR 决定 mode 字段 (paper/live/backtest)。

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "src/stcpp/debug_api/demo_state_provider.hpp"
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

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (a == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (a == "--empty") {
            empty = true;
        } else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: stcpp_debug_server [--port N] [--host ADDR] [--empty]\n"
                "  --port N     listen port (default 8080)\n"
                "  --host ADDR  bind address (default 127.0.0.1)\n"
                "  --empty      use empty StubStateProvider (default: DemoStateProvider)\n");
            return 0;
        } else {
            std::fprintf(stderr, "[debug_server] unknown arg: %s (try --help)\n", a.c_str());
            return 2;
        }
    }

    const ExecMode mode = mode_from_build();

    // provider 生命周期必须 outlive HttpServer (HttpServer 只持 const 句柄)。
    StubStateProvider stub{mode};
    DemoStateProvider demo{mode};
    const StateProvider* provider =
        empty ? static_cast<const StateProvider*>(&stub) : static_cast<const StateProvider*>(&demo);

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
                static_cast<unsigned>(port), STCPP_EXEC_MODE_STR, empty ? "stub-empty" : "demo");
    std::printf("[debug_server] endpoints: /healthz /version /status /metrics\n");
    std::printf(
        "[debug_server]            /api/v1/{positions,pnl/timeseries,pnl/attribution,"
        "risk/rejects,gate/paper,market/<id>,book/<id>}\n");
    std::printf("[debug_server] 看板: cd frontend && python3 serve.py  → http://127.0.0.1:3000/\n");
    std::printf("[debug_server] Ctrl-C 停止\n");
    std::fflush(stdout);

    while (!g_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("\n[debug_server] 收到停止信号, 关闭 server...\n");
    server.stop();
    std::printf("[debug_server] 已停止\n");
    return 0;
}
