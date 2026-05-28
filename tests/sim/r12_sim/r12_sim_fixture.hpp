// tests/sim/r12_sim/r12_sim_fixture.hpp — R-12 4 场景共享 fixture (W3 placeholder)
//
// Owner: 小宋 (test-replay-engineer)  Sprint-2 W3 Wave 18
// 关联: docs/RESEARCH/xiaosong-test-framework-cpp-skeleton-v1.md §2.3 / §5
//      docs/ADR/2026-05-28-gm-redline-websocket-non-blocking.md (R-12)
//
// W3 占位: mock harness 接口 stub, W4+ 接小邹 mock server 真实现
// W4 owners: @老周 (R-12 架构) / @老王 (WAL adapter 接口)

#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace stcpp::test::r12 {

// ---- Mock 计数器 stub (W4 切真 MockClobServer) -------------------------------
class MockClobStub {
public:
    void set_response_delay_ms(std::int64_t d) noexcept { response_delay_ms_ = d; }
    [[nodiscard]] std::int64_t response_delay_ms() const noexcept { return response_delay_ms_; }

    void record_request(const std::string& path) { ++recv_[path]; }
    [[nodiscard]] std::size_t recv_count(const std::string& path) const {
        auto it = recv_.find(path);
        return it == recv_.end() ? 0u : it->second;
    }

private:
    std::int64_t response_delay_ms_{0};
    std::unordered_map<std::string, std::size_t> recv_;
};

class MockWssStub {
public:
    void publish(std::string /*market_id*/) { ++published_; }
    void disconnect_all() { ++disconnect_calls_; }
    [[nodiscard]] std::size_t published()        const noexcept { return published_; }
    [[nodiscard]] std::size_t disconnect_calls() const noexcept { return disconnect_calls_; }

private:
    std::size_t published_{0};
    std::size_t disconnect_calls_{0};
};

// ---- R-12 fixture base ------------------------------------------------------
class R12SimFixture : public ::testing::Test {
protected:
    void SetUp() override {
        wss_tick_latencies_ns_.clear();
        clob_ = MockClobStub{};
        wss_  = MockWssStub{};
    }

    // S-1: 给 mock CLOB 注入慢响应
    void inject_rest_delay_ms(std::int64_t d) { clob_.set_response_delay_ms(d); }

    // 工具: p99 / p99.9 计算 (R-12 §17.1.1 量化口径)
    static std::int64_t p99_ns(std::vector<std::int64_t> xs) {
        if (xs.empty()) return 0;
        std::sort(xs.begin(), xs.end());
        const auto idx = static_cast<std::size_t>(
            static_cast<double>(xs.size()) * 0.99);
        return xs[std::min(idx, xs.size() - 1)];
    }

    MockClobStub clob_;
    MockWssStub  wss_;
    std::vector<std::int64_t> wss_tick_latencies_ns_;  // 子 case 填
};

}  // namespace stcpp::test::r12
