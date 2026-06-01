// src/stcpp/debug_api/server.cpp — HttpServer implementation
// Owner: 小卢 (senior-ic-pool)  W9 W2
// 关联:
//   server.hpp
//   xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md §3
//   R-12: 独立 std::thread; listen() blocking 在 server_thread_
//   R-20: as_of_ts = std::chrono::system_clock::now() epoch_ns (debug endpoint 无上游 ts)
//   ADR-015: API server 独立 vCPU; paper 阶段不强 pin
//
// 纯 API server: 不托管前端静态资源。前端走独立 Vite dev server, 跨域调 API;
//   CORS 头由 register_handlers() 统一注入 (Access-Control-Allow-Origin: *)。

#include "src/stcpp/debug_api/server.hpp"

#include <chrono>
#include <cstdio>
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
// 前端 v3 盯盘新增 (ADR-038 增量, 2026-05-29)
void register_score(httplib::Server& svr, const HttpServer& hs);
void register_quote(httplib::Server& svr, const HttpServer& hs);
// ADR-040: book_pair + 单边 book/token/{token_id}
void register_book_pair(httplib::Server& svr, const HttpServer& hs);
// 小冯 schema append: EventInfo 列表 (G-FREEZE-W 只增, 2026-05-29)
void register_events(httplib::Server& svr, const HttpServer& hs);
void register_features(httplib::Server& svr, const HttpServer& hs);
void register_mapping(httplib::Server& svr, const HttpServer& hs);
// 凯利评审 (2026-06-01): 账户级现金/估值
void register_account(httplib::Server& svr, const HttpServer& hs);
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
    // CORS 响应头 — 对齐 ADR-038 §5 安全约束
    //
    // 选 Access-Control-Allow-Origin: * 而非 allow-list 的理由:
    //   1. server 已绑 127.0.0.1 (ADR-038 §5), 远程强制走 SSH 隧道,
    //      网络层已是安全边界, CORS 只是浏览器同源策略的补充约束。
    //   2. 全部端点只读 (GET), 无 cookie / credential, W3C 规范允许 * + 无 credential。
    //   3. Allow-list (127.0.0.1:3000 + localhost:3000) 在实际开发中反而脆:
    //      本地看板端口可能随 dashboard 框架改变; * 简单稳定, 零维护成本。
    //   4. 若未来引入写端点或 credential, 必须改为精确 allow-list (届时修此注释)。
    //
    // set_default_headers: 对所有响应 (含 preflight 204) 注入 CORS 头。
    server_.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"},
    });

    // preflight handler — 浏览器在非简单请求时发 OPTIONS 探针。
    // GET + 无自定义请求头属"简单请求"不触发 preflight, 但显式处理更稳健:
    // 避免某些浏览器对非标准 Content-Type 场景静默失败。
    // 204 No Content 是 preflight 推荐响应码。
    server_.Options(".*", [](const httplib::Request& /*req*/, httplib::Response& res) { res.status = 204; });

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
    // 前端 v3 盯盘新增 (ADR-038 增量, 2026-05-29)
    register_score(server_, *this);
    register_quote(server_, *this);
    // ADR-040: book_pair ({condition_id}) + book/token/{token_id}
    // 注意: register_book_pair 内 book/token/{token_id} 先注册 (更具体路由优先于 book/{id})
    register_book_pair(server_, *this);
    // 小冯 schema append: EventInfo 列表 (G-FREEZE-W 只增, 2026-05-29)
    register_events(server_, *this);
    // 老雷 2026-06-01 可观测: 110 特征健康 (/api/v1/features/health)
    register_features(server_, *this);
    // 老雷 2026-06-01 可观测: condition↔Goalserve 映射状态 (/api/v1/mapping/status)
    register_mapping(server_, *this);
    // 老雷 2026-06-01 凯利评审: 账户级现金/估值 (/api/v1/account)
    register_account(server_, *this);
}

}  // namespace stcpp::debug_api
