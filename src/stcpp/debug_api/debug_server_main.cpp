// src/stcpp/debug_api/debug_server_main.cpp — 观测/调试 API server 可运行入口
// Owner: 老雷 (GM) 驱动前端集成 — 2026-05-29
// 关联:
//   server.hpp (HttpServer)
//   state_provider.hpp (StubStateProvider) / demo_state_provider.hpp (DemoStateProvider)
//   real_state_provider.hpp (RealStateProvider — book/rejects 已接真)
//   ADR-038 (观测 API) / ADR-037 (本地优先) / R-12 (server 独立线程)
//   frontend/ 观测看板默认连 127.0.0.1:8080
//
// 用途: 启动只读 debug_api server, 让 frontend/ 看板连真后端而非 ?stub=1 mock。
//   这是"能看到任何已经开发的功能状态"的后端落地点。
//
// CLI:
//   stcpp_debug_server [--port N] [--host ADDR] [--empty] [--real] [--frontend DIR]
//     --port N        监听端口 (默认 8080; 对齐 frontend BASE_URL)
//     --host ADDR     绑定地址 (默认 127.0.0.1; ADR-038 §5 安全默认, 远程走 SSH 隧道)
//     --empty         用 StubStateProvider (全空, 各 endpoint 返回结构合法的空值);
//                     默认用 DemoStateProvider (代表性演示数据, 看板全面板可渲染)
//     --real          用 RealStateProvider (book/rejects 接真实快照, 其余委托 Demo;
//                     data_source="mixed"; 启动时 hub/snap 均空 → 回落 Demo 优雅降级)
//     --frontend DIR  前端构建产物目录 (默认 "frontend/dist", 相对 CWD; 也可绝对路径).
//                     cpp-httplib set_mount_point("/", DIR) 挂载; API handler 优先.
//                     目录不存在 → warn + 跳过 (API 仍正常, 不 abort).
//                     同源收益: 看板与 API 同在 :PORT, 无跨域.
//
// 安全: 默认 127.0.0.1 only; 只读 endpoint; 黑名单字段 (私钥/签名字节) 物理不在 schema 中。
// 模式: build-time STCPP_EXEC_MODE_STR 决定 mode 字段 (paper/live/backtest)。

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "src/stcpp/debug_api/demo_state_provider.hpp"
#include "src/stcpp/debug_api/real_state_provider.hpp"
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
    // 默认 "frontend/dist" (相对 CWD); 用户可传绝对路径或相对路径
    std::string frontend_dir = "frontend/dist";

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
        } else if (a == "--frontend" && i + 1 < argc) {
            frontend_dir = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: stcpp_debug_server [--port N] [--host ADDR] [--empty] [--real]"
                " [--frontend DIR]\n"
                "  --port N        listen port (default 8080)\n"
                "  --host ADDR     bind address (default 127.0.0.1)\n"
                "  --empty         use empty StubStateProvider (default: DemoStateProvider)\n"
                "  --real          use RealStateProvider (book/rejects=real, rest=demo;\n"
                "                  hub/snap empty at start → graceful fallback to demo)\n"
                "  --frontend DIR  frontend build dir (default: frontend/dist, relative to CWD)\n"
                "                  cpp-httplib mount_point(\"/\", DIR); API handlers take priority\n"
                "                  dir missing → warn + skip mount (API still works)\n");
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

    const ExecMode mode = mode_from_build();

    // provider 生命周期必须 outlive HttpServer (HttpServer 只持 const 句柄)。
    // --real 模式: 构造空 hub + null snap (生产接入后由外部 vCPU0 写入 hub;
    //              standalone 独立运行时 hub 空 → book/rejects 自动回落 Demo)
    StubStateProvider stub{mode};
    DemoStateProvider demo{mode};

    // hub 和 real_provider 用 unique_ptr 管理, 避免在 --real 未激活时有效载荷
    std::unique_ptr<stcpp::polymarket::clob_wss::OrderBookSnapshotHub> hub_owned;
    std::unique_ptr<RealStateProvider> real_provider;

    const char* data_label = "demo";
    if (empty) {
        data_label = "stub-empty";
    } else if (real) {
        data_label = "mixed";
        // 构造独立 hub (standalone 模式, 无 WSS vCPU0 接入 → hub 为空 → book 回落 Demo)
        // 生产中 hub 由 WSS event loop 所在进程传入; 此处 standalone 验证路径正确
        hub_owned = std::make_unique<stcpp::polymarket::clob_wss::OrderBookSnapshotHub>();
        // snap=nullptr: 无 RiskGateway 注入 → risk_rejects 回落 Demo
        // token_map 为空: book_pair 回落 Demo (standalone 无市场目录)
        real_provider = std::make_unique<RealStateProvider>(*hub_owned,
                                                            /*snap=*/nullptr,
                                                            /*token_map=*/MarketTokenMap{}, mode);
    }

    const StateProvider* provider = nullptr;
    if (empty) {
        provider = &stub;
    } else if (real) {
        provider = real_provider.get();
    } else {
        provider = &demo;
    }

    HttpServer server{port, provider, host.c_str(), frontend_dir};
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
        std::printf("[debug_server] --real 模式: book/risk_rejects=真实快照(hub/snap), 其余=demo\n");
        std::printf("[debug_server]   standalone 启动: hub 空 → book/rejects 自动回落 demo\n");
        std::printf("[debug_server]   生产接入: 外部 vCPU0 Publish() 到 hub 后自动生效\n");
    }
    std::printf("[debug_server] endpoints: /healthz /version /status /metrics\n");
    std::printf(
        "[debug_server]            /api/v1/{positions,pnl/timeseries,pnl/attribution,"
        "risk/rejects,gate/paper,market/<id>,book/<id>}\n");
    // 同源看板 banner (C++ 托管, 零 Node/Python 运行时)
    std::printf("[debug_server] 看板: http://%s:%u/ (C++ 托管, 无需 npm)\n", host.c_str(),
                static_cast<unsigned>(port));
    std::printf("[debug_server]   前端目录: %s\n", frontend_dir.c_str());
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
