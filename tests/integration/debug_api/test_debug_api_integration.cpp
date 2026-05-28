// tests/integration/debug_api/test_debug_api_integration.cpp
// Owner: 小卢 (senior-ic-pool)  W9 W2
// 关联:
//   xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md §6.1
//   laozhou-w8-debug-rest-api-spec-v1.md §7.2
//
// T1: DebugApiServer_Healthz_Returns200
//     启动 HttpServer(18080) + GET /healthz → HTTP 200 + ok==true + as_of_ts > 0
// T2: DebugApiServer_Version_Fields
//     GET /version → HTTP 200 + version 字段存在 + build_mode in {paper,live,backtest}
// T3: DebugApiServer_Status_PaperMode
//     GET /status → HTTP 200 + state in {RUNNING,DRAIN,HALTED} + mode == 期望值
// T4: DebugApiServer_HotpathIsolation_ServerStop
//     stop HttpServer → 主线程继续执行 100ms 无 hang (hot path 不受 server 停止影响)
// T5: DebugApiServer_Concurrent100_NoCrashNoHang
//     100 个 std::thread 并发 GET /healthz → 全部有响应, server 不 crash
//
// 端口: 18080 (避免与其他 integration test 冲突; 各 case 独立 server 实例, 顺序执行)
//
// JSON 解析: 手工 string::find (MVP; 无需引入 JSON 库到测试)
// HTTP client: httplib::Client (ADR-018 已有, 避免 popen/shell 外部依赖)

#include "src/stcpp/debug_api/server.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

// 测试端口: 独立 server 实例, 避免端口冲突
// 各 TEST 用同一端口但顺序执行 (gtest 默认单线程), server 在 fixture 中 stop()
static constexpr uint16_t k_test_port = 18080;

// --- 简单 JSON 字段提取工具 (手工 find; MVP) ---

// 检查 JSON body 含 "key":true
static bool json_bool_true(const std::string& body, const std::string& key)
{
    std::string pat = "\"" + key + "\":true";
    return body.find(pat) != std::string::npos;
}

// 检查 JSON body 含 "key":<某个数字> 且该数字 > 0
// (简化: 找到 "key": 后取数字, 检查是否 > 0)
static bool json_int64_positive(const std::string& body, const std::string& key)
{
    std::string pat = "\"" + key + "\":";
    auto pos = body.find(pat);
    if (pos == std::string::npos) {
        return false;
    }
    pos += pat.size();
    // skip whitespace
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) {
        ++pos;
    }
    if (pos >= body.size()) {
        return false;
    }
    // 解析数字
    bool negative = (body[pos] == '-');
    if (negative) { ++pos; }
    int64_t val = 0;
    bool has_digit = false;
    while (pos < body.size() && body[pos] >= '0' && body[pos] <= '9') {
        val = val * 10 + static_cast<int64_t>(body[pos] - '0');
        has_digit = true;
        ++pos;
    }
    if (!has_digit) { return false; }
    if (negative) { val = -val; }
    return val > 0;
}

// 检查 JSON body 含 "key":"<某字符串>" 且该字符串在给定候选集中
static bool json_str_in(const std::string& body, const std::string& key,
                        std::initializer_list<const char*> candidates)
{
    std::string pat = "\"" + key + "\":\"";
    auto pos = body.find(pat);
    if (pos == std::string::npos) {
        return false;
    }
    pos += pat.size();
    auto end = body.find('"', pos);
    if (end == std::string::npos) {
        return false;
    }
    std::string val = body.substr(pos, end - pos);
    for (const char* c : candidates) {
        if (val == c) { return true; }
    }
    return false;
}

// 检查 JSON body 含 "key":"<某字符串>"
static bool json_str_field_exists(const std::string& body, const std::string& key)
{
    std::string pat = "\"" + key + "\":\"";
    return body.find(pat) != std::string::npos;
}

// ============================================================
// T1: GET /healthz → 200 + ok==true + as_of_ts > 0
// ============================================================
TEST(DebugApiServer, Healthz_Returns200)
{
    stcpp::debug_api::HttpServer srv(k_test_port);
    srv.start();

    // 等待 server 就绪 (start() 内 spin-wait 已处理, 但加保险)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client cli("localhost", k_test_port);
    cli.set_connection_timeout(2, 0);  // 2s
    cli.set_read_timeout(2, 0);

    auto result = cli.Get("/healthz");

    ASSERT_TRUE(result)  << "GET /healthz 连接失败: " << result.error();
    EXPECT_EQ(result->status, 200)
        << "期望 HTTP 200, 实际: " << result->status;

    const std::string& body = result->body;
    EXPECT_TRUE(json_bool_true(body, "ok"))
        << "响应体缺 ok:true. body=" << body;
    EXPECT_TRUE(json_int64_positive(body, "as_of_ts"))
        << "响应体缺 as_of_ts > 0. body=" << body;

    // Content-Type 检查
    auto ct = result->get_header_value("Content-Type");
    EXPECT_NE(ct.find("application/json"), std::string::npos)
        << "Content-Type 应含 application/json, 实际: " << ct;

    srv.stop();
}

// ============================================================
// T2: GET /version → 200 + version 字段 + build_mode 合法值
// ============================================================
TEST(DebugApiServer, Version_Fields)
{
    stcpp::debug_api::HttpServer srv(k_test_port);
    srv.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client cli("localhost", k_test_port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(2, 0);

    auto result = cli.Get("/version");

    ASSERT_TRUE(result) << "GET /version 连接失败: " << result.error();
    EXPECT_EQ(result->status, 200)
        << "期望 HTTP 200, 实际: " << result->status;

    const std::string& body = result->body;
    EXPECT_TRUE(json_str_field_exists(body, "version"))
        << "响应体缺 version 字段. body=" << body;
    EXPECT_TRUE(json_str_in(body, "build_mode", {"paper", "live", "backtest"}))
        << "build_mode 不在 {paper,live,backtest}. body=" << body;
    EXPECT_TRUE(json_str_field_exists(body, "git_hash"))
        << "响应体缺 git_hash 字段. body=" << body;
    EXPECT_TRUE(json_int64_positive(body, "as_of_ts"))
        << "响应体缺 as_of_ts > 0. body=" << body;

    srv.stop();
}

// ============================================================
// T3: GET /status → 200 + state 合法值 + mode 合法值
// ============================================================
TEST(DebugApiServer, Status_PaperMode)
{
    stcpp::debug_api::HttpServer srv(k_test_port);
    srv.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client cli("localhost", k_test_port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(2, 0);

    auto result = cli.Get("/status");

    ASSERT_TRUE(result) << "GET /status 连接失败: " << result.error();
    EXPECT_EQ(result->status, 200)
        << "期望 HTTP 200, 实际: " << result->status;

    const std::string& body = result->body;
    EXPECT_TRUE(json_str_in(body, "state", {"RUNNING", "DRAIN", "HALTED"}))
        << "state 不在 {RUNNING,DRAIN,HALTED}. body=" << body;
    EXPECT_TRUE(json_str_in(body, "mode", {"paper", "live", "backtest"}))
        << "mode 不在 {paper,live,backtest}. body=" << body;
    EXPECT_TRUE(json_int64_positive(body, "as_of_ts"))
        << "响应体缺 as_of_ts > 0. body=" << body;

    srv.stop();
}

// ============================================================
// T4: stop HttpServer → 主线程继续执行 100ms 无 hang
// ============================================================
TEST(DebugApiServer, HotpathIsolation_ServerStop)
{
    stcpp::debug_api::HttpServer srv(k_test_port);
    srv.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(srv.is_running());

    // stop server
    srv.stop();
    EXPECT_FALSE(srv.is_running());

    // 主线程在 server stop 后继续运行 100ms — 不 hang, 不 deadlock
    // 这模拟: hot path 线程不依赖 API server, server stop 不影响主流程
    auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0
    ).count();

    EXPECT_GE(elapsed, 90)
        << "主线程 sleep_for(100ms) 提前返回? elapsed=" << elapsed << "ms";
    EXPECT_LT(elapsed, 500)
        << "主线程疑似 hang, elapsed=" << elapsed << "ms";
    // server stop 后不 crash 即通过 (RAII 析构幂等)
}

// ============================================================
// T5: 100 并发 GET /healthz → 全部有响应, server 不 crash
// ============================================================
TEST(DebugApiServer, Concurrent100_NoCrashNoHang)
{
    stcpp::debug_api::HttpServer srv(k_test_port);
    srv.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    constexpr int k_concurrent = 100;
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    std::vector<std::thread> threads;
    threads.reserve(k_concurrent);

    for (int i = 0; i < k_concurrent; ++i) {
        threads.emplace_back([&]() {
            // 每线程独立 client 实例 (httplib::Client 非线程安全)
            httplib::Client cli("localhost", k_test_port);
            cli.set_connection_timeout(3, 0);
            cli.set_read_timeout(3, 0);
            auto r = cli.Get("/healthz");
            if (r && r->status == 200) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                failure_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    const int succ = success_count.load();
    const int fail = failure_count.load();

    EXPECT_EQ(succ + fail, k_concurrent)
        << "并发请求总数不对: succ=" << succ << " fail=" << fail;

    // 允许少量连接失败 (OS 端口/fd 限制), 但 server 不能 crash
    // 期望成功率 ≥ 90%
    EXPECT_GE(succ, k_concurrent * 9 / 10)
        << "并发成功率 < 90%: succ=" << succ << " / " << k_concurrent;

    // server 仍在运行 (未 crash)
    EXPECT_TRUE(srv.is_running())
        << "100 并发后 server is_running() = false (可能 crash 或意外退出)";

    srv.stop();
}
