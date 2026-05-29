// src/stcpp/debug_api/server.hpp — HttpServer class declaration
// Owner: 小卢 (senior-ic-pool)  W9 W2
// 关联:
//   xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md §3
//   laozhou-w8-debug-rest-api-spec-v1.md §3 + §5
//   ADR-018 (cpp-httplib v0.16.3)
//   ADR-015 (vCPU 分工; API server 独立 vCPU6)
//   R-12 (独立 std::thread; 不占 vCPU0/1/2)
//   R-20 (start_time_ 用于 uptime_sec; as_of_ts = now() epoch ns)
//
// 前端静态资源托管 (feat/xiaolu-cpp-serve-frontend):
//   cpp-httplib set_mount_point("/", frontend_dir) 把 frontend/dist 挂到根路径。
//   优先级: API handler 先注册, set_mount_point 后调用; cpp-httplib 已注册的精确/regex
//   handler 优先于 mount point 静态文件 — /api/v1/* /healthz /status /version /metrics 不受影响。
//   frontend_dir 为空或目录不存在 → 跳过挂载 (API 仍正常, 不崩)。

#pragma once

// cpp-httplib header-only (SYSTEM include — 不受公司 -Werror 约束)
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include "src/stcpp/debug_api/state_provider.hpp"
#include <httplib.h>

namespace stcpp::debug_api {

// HttpServer — 轻量 HTTP/1.1 debug server (cpp-httplib blocking model)
//
// 设计约束:
//   1. 在独立 std::thread 启动 httplib::Server::listen() (blocking per-connection).
//      调用方不阻塞: start() 返回时 server_thread_ 已运行.
//   2. vCPU0/1/2 (Ingest/Signal/Risk) 绝对不调用任何 HttpServer 方法 (ADR-015).
//   3. running_ 是 wait-free read (std::atomic<bool>), /healthz 和 watchdog 均可安全读.
//   4. RAII: 析构时自动调用 stop(), join server_thread_.
//      防止线程泄漏 (R-12: 线程孤立 = P0).
//
// W9 W2 端点:
//   GET /healthz   — 200 + {"ok":true, "threads":{...}, "uptime_sec":N, "as_of_ts":T}
//   GET /version   — 200 + {"version":"0.1.0", "git_hash":"...", "build_mode":"paper", ...}
//   GET /status    — 200 + {"state":"RUNNING", "mode":"paper", ..., "as_of_ts":T}
//
// W9 W3 将扩展注入接口 (SignalEngine* / SystemState* 等), 接口兼容 W9 W2 stub.
class HttpServer {
public:
    // provider: 观测只读状态契约 (ADR-038 §2/§3). 不传 = 内置 StubStateProvider(paper),
    //   返回结构合法的空/0 值。各模块 owner 提供真实 double-buffer snapshot 后,
    //   由 main 注入。HttpServer 只持 const 句柄, 不拥有生命周期 (caller 保证 outlive)。
    // bind: 安全默认 "127.0.0.1" (ADR-038 §5 / 小白 §2). 远程走 SSH 隧道, 不开 0.0.0.0。
    // frontend_dir: 前端构建产物目录 (挂载到 "/" 根路径).
    //   空串/nullptr → 不挂载 (API 仍正常).
    //   目录不存在 → 打印 warn + 跳过挂载 (不 abort).
    //   cpp-httplib handler 优先于 mount point:
    //     API handler 在 register_handlers() 先注册, set_mount_point 后调用,
    //     两者顺序均可 — httplib 内部已注册的精确/regex route 始终比 mount point 优先匹配.
    explicit HttpServer(uint16_t port, const StateProvider* provider = nullptr,
                        const char* bind_addr = "127.0.0.1", std::string frontend_dir = {});

    // 禁止拷贝/移动 (httplib::Server 不可拷贝; thread 不可拷贝)
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;

    ~HttpServer();

    // 在独立 std::thread 启动 httplib::Server::listen("0.0.0.0", port_).
    // 调用方不阻塞: 方法返回时 server_thread_ 已处于 running 状态 (spin-wait ≤ 200ms).
    // 必须在 hot-path 线程 (vCPU0/1/2) 之外调用.
    // 重复调用: 若已在运行则 no-op.
    void start();

    // 触发 httplib::Server::stop(), join server_thread_.
    // 析构时自动调用 (RAII). 幂等.
    void stop();

    // wait-free read; 供 watchdog + /healthz 使用
    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    // 进程启动时刻 (server 构造时记录); 用于计算 uptime_sec
    std::chrono::steady_clock::time_point start_time() const noexcept { return start_time_; }

    // 观测只读状态契约 (const 句柄, 永不为空: 缺省指向内置 stub)
    const StateProvider& provider() const noexcept { return *provider_; }

private:
    // 注册所有 endpoint handler 到 server_
    void register_handlers();

    // 挂载前端静态目录 (API handler 注册之后调用; 目录不存在则 warn + 跳过)
    void maybe_mount_frontend();

    httplib::Server server_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    uint16_t port_;
    std::string bind_addr_;
    std::string frontend_dir_;  // 空串 → 不挂载

    // 缺省 stub provider (caller 未注入时使用); provider_ 指向它或外部注入的实现
    StubStateProvider default_provider_;
    const StateProvider* provider_;

    // 构造时记录, 不变; 用于 uptime_sec (steady_clock 不受系统时钟调整影响)
    std::chrono::steady_clock::time_point start_time_;
};

}  // namespace stcpp::debug_api
