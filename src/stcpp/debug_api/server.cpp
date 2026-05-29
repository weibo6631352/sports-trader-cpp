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
#include <stdexcept>
#include <thread>

// Forward declarations for endpoint registration functions
// 每个 endpoint_xxx.cpp 提供一个 register 函数, server.cpp 统一调用
namespace stcpp::debug_api {
void register_healthz(httplib::Server& svr, const HttpServer& hs);
void register_version(httplib::Server& svr);
void register_status(httplib::Server& svr, const HttpServer& hs);
} // namespace stcpp::debug_api

namespace stcpp::debug_api {

HttpServer::HttpServer(uint16_t port)
    : port_(port)
    , start_time_(std::chrono::steady_clock::now())
{
}

HttpServer::~HttpServer()
{
    stop();
}

void HttpServer::start()
{
    if (running_.load(std::memory_order_acquire)) {
        return; // 幂等
    }

    register_handlers();

    // 启动 server_thread_; listen() 是 blocking call — 在独立线程运行 (R-12)
    server_thread_ = std::thread([this] {
        running_.store(true, std::memory_order_release);
        // listen() blocks until server_.stop() is called
        server_.listen("0.0.0.0", static_cast<int>(port_));
        running_.store(false, std::memory_order_release);
    });

    // Spin-wait: 等待 server 实际进入 listening 状态 (最多 200ms)
    // httplib::Server::is_running() 在 listen() 开始后变 true
    constexpr int k_max_wait_ms = 200;
    constexpr int k_poll_us     = 1000; // 1ms
    int waited_ms = 0;
    while (!server_.is_running() && waited_ms < k_max_wait_ms) {
        std::this_thread::sleep_for(std::chrono::microseconds(k_poll_us));
        waited_ms++;
    }
    // 即使超时也不 throw — server 可能在极低负载的 CI 环境稍慢, 继续运行
}

void HttpServer::stop()
{
    if (!server_thread_.joinable()) {
        return; // 幂等
    }
    server_.stop();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    running_.store(false, std::memory_order_release);
}

void HttpServer::register_handlers()
{
    register_healthz(server_, *this);
    register_version(server_);
    register_status(server_, *this);
}

} // namespace stcpp::debug_api
