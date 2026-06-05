// tests/unit/test_rolling_clv.cpp — 实时 CLV 滚动环单测 (持仓管理 Stage 2)
#include <cmath>

#include <gtest/gtest.h>

#include "stcpp/eval/rolling_clv.hpp"

using stcpp::eval::RollingClv;

TEST(RollingClv, Empty_MeanNaN) {
    RollingClv r;
    EXPECT_EQ(r.Count(), 0u);
    EXPECT_TRUE(std::isnan(r.Mean()));
}

TEST(RollingClv, RecordsAndMean) {
    RollingClv r;
    r.Record(0.01);
    r.Record(0.03);
    EXPECT_EQ(r.Count(), 2u);
    EXPECT_NEAR(r.Mean(), 0.02, 1e-12);
}

TEST(RollingClv, NonFiniteSkipped) {
    RollingClv r;
    r.Record(0.02);
    r.Record(std::numeric_limits<double>::quiet_NaN());
    r.Record(std::numeric_limits<double>::infinity());
    EXPECT_EQ(r.Count(), 1u);  // 仅有限值计入
    EXPECT_NEAR(r.Mean(), 0.02, 1e-12);
}

TEST(RollingClv, RingWrapsKeepsLast50) {
    RollingClv r;
    // 推 60 笔 (0..59); 环 cap=50 → 只保留最后 50 笔 (10..59), 均值 = (10+59)/2 = 34.5
    for (int i = 0; i < 60; ++i) r.Record(static_cast<double>(i));
    EXPECT_EQ(r.Count(), RollingClv::kCapacity);
    EXPECT_NEAR(r.Mean(), 34.5, 1e-9);
}

TEST(RollingClv, NegativeMean) {
    RollingClv r;
    r.Record(-0.01);
    r.Record(-0.03);
    EXPECT_NEAR(r.Mean(), -0.02, 1e-12);
}

TEST(RollingClv, ResetClears) {
    RollingClv r;
    r.Record(0.05);
    r.Reset();
    EXPECT_EQ(r.Count(), 0u);
    EXPECT_TRUE(std::isnan(r.Mean()));
}
