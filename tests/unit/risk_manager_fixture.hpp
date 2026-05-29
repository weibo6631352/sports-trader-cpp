// tests/unit/risk_manager_fixture.hpp — gtest Fixture base, 21 reject enum 共享入口
// Owner: 小宋  W3 Wave 18  关联: skeleton v1 §2.1 / reject_enum.hpp
//
// W3 mock RiskGateway, W4+ owner (老韩) commit 真 RM 后切真 (改一处).
// PIT 4 ts 辅助 (R-20 §6). 红线 R-1: reject 必 audit_id 非空.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <gtest/gtest.h>

#include "stcpp/risk/reject_enum.hpp"

namespace stcpp::test {

class VirtualClock {
public:
    explicit VirtualClock(std::int64_t init_ns) : now_ns_(init_ns) {}
    [[nodiscard]] std::int64_t now_ns() const noexcept { return now_ns_; }
    void advance_ns(std::int64_t d) noexcept { now_ns_ += d; }

private:
    std::int64_t now_ns_;
};

// W3 stub, W4 切真 risk::OrderIntent
struct OrderIntentStub {
    // PIT 4 ts (R-20 §6, 老孙 v5.1)
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
    std::string feature_snapshot_id;  // R-20 §7 / ML-R8
    std::string market_id;
    std::string strategy_id;
    std::int64_t size_usdc{0};
    double price{0.0};
    bool is_buy{true};
};

enum class Decision : std::uint8_t { APPROVED, REJECTED, DEFERRED };

struct DecisionStub {
    Decision decision{Decision::APPROVED};
    risk::RejectCode reject_code{risk::RejectCode::INTERNAL_ERROR};
    risk::InvalidIntentSubReason sub_reason{risk::InvalidIntentSubReason::NONE};
    std::string audit_id;  // R-1: 非空
};

class MockRiskGateway {
public:
    void prime_reject(const std::string& key, risk::RejectCode code,
                      risk::InvalidIntentSubReason sub = risk::InvalidIntentSubReason::NONE) {
        primed_[key] = DecisionStub{Decision::REJECTED, code, sub, "01HMOCK" + key};
    }
    void prime_deferred(const std::string& key) {
        primed_[key] = DecisionStub{Decision::DEFERRED, risk::RejectCode::INTERNAL_ERROR,
                                    risk::InvalidIntentSubReason::NONE, "01HMOCK" + key};
    }
    [[nodiscard]] DecisionStub evaluate(const OrderIntentStub& it) const {
        auto i = primed_.find(it.strategy_id);
        return i == primed_.end() ? DecisionStub{Decision::APPROVED, risk::RejectCode::INTERNAL_ERROR,
                                                 risk::InvalidIntentSubReason::NONE, "01HMOCKOK"}
                                  : i->second;
    }

private:
    std::unordered_map<std::string, DecisionStub> primed_;
};

class RiskManagerFixture : public ::testing::Test {
protected:
    void SetUp() override {
        clock_ = std::make_shared<VirtualClock>(1'700'000'000'000'000'000LL);
        gw_ = std::make_unique<MockRiskGateway>();
    }
    // 4 ts PIT 构造器: 默认满足单调不等式
    OrderIntentStub make_intent_pit_ok(std::string strategy_id = "s_default") const {
        const auto now = clock_->now_ns();
        return OrderIntentStub{
            .event_ts_ns = now - 200'000'000,
            .data_source_ts_ns = now - 150'000'000,
            .ingestion_ts_ns = now - 50'000'000,
            .as_of_ts_ns = now - 1'000'000,
            .feature_snapshot_id = "fs_01HMOCK",
            .market_id = "mkt_test",
            .strategy_id = std::move(strategy_id),
            .size_usdc = 100,
            .price = 0.55,
            .is_buy = true,
        };
    }
    OrderIntentStub make_intent_pit_violated() const {
        auto it = make_intent_pit_ok();
        it.ingestion_ts_ns = it.event_ts_ns - 1;  // ts3 < ts1
        return it;
    }
    void expect_reject(const OrderIntentStub& it, risk::RejectCode code) {
        const auto d = gw_->evaluate(it);
        EXPECT_EQ(d.decision, Decision::REJECTED);
        EXPECT_EQ(d.reject_code, code);
        EXPECT_FALSE(d.audit_id.empty());  // R-1
    }
    std::shared_ptr<VirtualClock> clock_;
    std::unique_ptr<MockRiskGateway> gw_;
};

}  // namespace stcpp::test
