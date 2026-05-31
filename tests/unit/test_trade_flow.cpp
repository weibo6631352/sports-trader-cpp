// test_trade_flow.cpp — trade-flow 滚动聚合单测 (签名净流/买方占比/强度/窗口/方向)。
#include <gtest/gtest.h>

#include <cmath>  // std::isnan (gcc 需显式; clang 经 gtest 传递包含)

#include "stcpp/microstructure/trade_flow.hpp"

using stcpp::microstructure::TradeFlowWindow;

namespace {
constexpr std::int64_t kSec = 1'000'000'000LL;
}

TEST(TradeFlow, SignedFlowAndBuyRatio) {
    TradeFlowWindow w;
    const std::int64_t t0 = 1000 * kSec;
    w.observe(t0, 100.0, /*is_buy=*/true);   // 买主动 100
    w.observe(t0 + kSec, 60.0, true);        // 买主动 60
    w.observe(t0 + 2 * kSec, 40.0, false);   // 卖主动 40
    const auto m = w.snapshot(t0 + 3 * kSec);
    EXPECT_DOUBLE_EQ(m.signed_vol, 120.0) << "买160 − 卖40";
    EXPECT_DOUBLE_EQ(m.buy_ratio, 160.0 / 200.0) << "买占比 0.8";
    EXPECT_DOUBLE_EQ(m.intensity, 3.0) << "3 笔";
}

TEST(TradeFlow, WindowExcludesOld) {
    TradeFlowWindow w;
    const std::int64_t t0 = 1000 * kSec;
    w.observe(t0, 100.0, true);                       // 10min 前 (窗口外)
    w.observe(t0 + 600 * kSec, 50.0, false);          // now 处
    const auto m = w.snapshot(t0 + 600 * kSec, 300 * kSec);  // 5min 窗口
    EXPECT_DOUBLE_EQ(m.signed_vol, -50.0) << "只算窗口内卖 50; 旧买 100 被排除";
    EXPECT_DOUBLE_EQ(m.intensity, 1.0);
}

TEST(TradeFlow, EmptyIsNaN) {
    TradeFlowWindow w;
    const auto m = w.snapshot(1000 * kSec);
    EXPECT_TRUE(std::isnan(m.signed_vol));
    EXPECT_TRUE(std::isnan(m.buy_ratio));
    EXPECT_TRUE(std::isnan(m.intensity));
}

TEST(TradeFlow, IgnoresInvalid) {
    TradeFlowWindow w;
    w.observe(1000 * kSec, -5.0, true);  // 负 size 忽略
    w.observe(0, 5.0, true);             // ts<=0 忽略
    EXPECT_EQ(w.size(), 0u);
}
