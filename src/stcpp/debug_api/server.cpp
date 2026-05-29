// src/stcpp/debug_api/server.cpp — HttpServer implementation
// Owner: 小卢 (senior-ic-pool)  W9 W2
// 关联:
//   server.hpp
//   xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md §3
//   R-12: 独立 std::thread; listen() blocking 在 server_thread_
//   R-20: as_of_ts = std::chrono::system_clock::now() epoch_ns (debug endpoint 无上游 ts)
//   ADR-015: API server 独立 vCPU; paper 阶段不强 pin

#include "src/stcpp/debug_api/server.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

// Forward declarations for endpoint registration functions
// 每个 endpoint_xxx.cpp 提供一个 register 函数, server.cpp 统一调用
namespace stcpp::debug_api {
void register_healthz(httplib::Server& svr, const HttpServer& hs);
void register_version(httplib::Server& svr);
void register_status(httplib::Server& svr, const HttpServer& hs);
// ADR-038 MVP read-only endpoints
void register_positions(httplib::Server& svr, const HttpServer& hs);
void register_pnl(httplib::Server& svr, const HttpServer& hs);
void register_risk(httplib::Server& svr, const HttpServer& hs);
void register_gate(httplib::Server& svr, const HttpServer& hs);
void register_metrics(httplib::Server& svr, const HttpServer& hs);
void register_market(httplib::Server& svr, const HttpServer& hs);
}  // namespace stcpp::debug_api

namespace stcpp::debug_api {

// 编译期 mode 字符串 → ExecMode (stub provider 缺省 mode, R-11)
static ExecMode mode_from_build() noexcept {
    if (std::strcmp(STCPP_EXEC_MODE_STR, "live") == 0) {
        return ExecMode::Live;
    }
    if (std::strcmp(STCPP_EXEC_MODE_STR, "backtest") == 0) {
        return ExecMode::Backtest;
    }
    return ExecMode::Paper;
}

HttpServer::HttpServer(uint16_t port, const StateProvider* provider, const char* bind_addr)
    : port_(port),
      bind_addr_(bind_addr ? bind_addr : "127.0.0.1"),
      default_provider_(mode_from_build()),
      provider_(provider ? provider : &default_provider_),
      start_time_(std::chrono::steady_clock::now()) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    if (running_.load(std::memory_order_acquire)) {
        return;  // 幂等
    }

    register_handlers();

    // 启动 server_thread_; listen() 是 blocking call — 在独立线程运行 (R-12)
    server_thread_ = std::thread([this] {
        running_.store(true, std::memory_order_release);
        // listen() blocks until server_.stop() is called.
        // bind_addr_ 默认 "127.0.0.1" (ADR-038 §5 安全默认; 远程走 SSH 隧道)
        server_.listen(bind_addr_.c_str(), static_cast<int>(port_));
        running_.store(false, std::memory_order_release);
    });

    // Spin-wait: 等待 server 实际进入 listening 状态 (最多 200ms)
    // httplib::Server::is_running() 在 listen() 开始后变 true
    constexpr int k_max_wait_ms = 200;
    constexpr int k_poll_us = 1000;  // 1ms
    int waited_ms = 0;
    while (!server_.is_running() && waited_ms < k_max_wait_ms) {
        std::this_thread::sleep_for(std::chrono::microseconds(k_poll_us));
        waited_ms++;
    }
    // 即使超时也不 throw — server 可能在极低负载的 CI 环境稍慢, 继续运行
}

void HttpServer::stop() {
    if (!server_thread_.joinable()) {
        return;  // 幂等
    }
    server_.stop();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    running_.store(false, std::memory_order_release);
}

void HttpServer::register_handlers() {
    register_healthz(server_, *this);
    register_version(server_);
    register_status(server_, *this);
    // ADR-038 MVP read-only endpoints (全部只读, 不碰交易/控制面)
    register_positions(server_, *this);
    register_pnl(server_, *this);
    register_risk(server_, *this);
    register_gate(server_, *this);
    register_metrics(server_, *this);
    register_market(server_, *this);
}

}  // namespace stcpp::debug_api
