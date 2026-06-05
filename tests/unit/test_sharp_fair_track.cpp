// tests/unit/test_sharp_fair_track.cpp — sharp fair 时序环单测 (PIT / BR-1 / velocity / 收敛发散)
//
// Owner: 老雷 (GM) — 赔率源时序地基 (老板 2026-06-05「方向真值=赔率源 sharp; line movement 一阶导」)
// 覆盖: push/单调门/无效sharp过滤/容量回绕/窗口边界/sharp速度(正负)/收敛发散率/sharp波动/PIT 无前视

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "stcpp/ml/sharp_fair_track.hpp"

using stcpp::ml::SharpFairTrack;

namespace {
constexpr std::int64_t kSec = 1'000'000'000LL;  // 1s in ns
// 基准 epoch 偏移: ts_ns=0 被环当「无数据哨兵」丢弃 (PIT 单调门 last_ts_ 初值 0; 真实数据是 epoch_ns
//   大数永不为 0)。测试用非零基准, 与生产语义一致。
constexpr std::int64_t kT0 = 1'700'000'000LL * kSec;
}  // namespace

// SFT-01: 空环 — 所有派生 NaN, size 0
TEST(SharpFairTrack, SFT01_EmptyYieldsNaN) {
    SharpFairTrack t;
    EXPECT_EQ(t.size(), 0u);
    EXPECT_TRUE(t.empty());
    EXPECT_TRUE(std::isnan(t.Velocity(10 * kSec)));
    EXPECT_TRUE(std::isnan(t.ConvergenceRate(10 * kSec)));
    EXPECT_TRUE(std::isnan(t.Vol(10 * kSec)));
    EXPECT_TRUE(std::isnan(t.LastGap()));
    EXPECT_EQ(t.WindowSampleCount(10 * kSec), 0u);
}

// SFT-02: 单调门 — ts ≤ last 的乱序/重复样本跳过 (PIT)
TEST(SharpFairTrack, SFT02_MonotonicGate) {
    SharpFairTrack t;
    t.Push(5 * kSec, 0.50, 0.50);
    t.Push(5 * kSec, 0.60, 0.60);  // 同 ts → 跳过
    t.Push(3 * kSec, 0.40, 0.40);  // 更早 ts → 跳过
    EXPECT_EQ(t.size(), 1u);
    EXPECT_EQ(t.last_ts_ns(), 5 * kSec);
}

// SFT-03: 无效 sharp (≤0 / ≥1 / NaN) 不入环; mid 无效 → 退化存=sharp (gap 0)
TEST(SharpFairTrack, SFT03_InvalidSharpFiltered) {
    SharpFairTrack t;
    t.Push(1 * kSec, 0.0, 0.5);   // sharp=0 → 跳过
    t.Push(2 * kSec, 1.0, 0.5);   // sharp=1 → 跳过
    t.Push(3 * kSec, -0.1, 0.5);  // sharp<0 → 跳过
    t.Push(4 * kSec, std::numeric_limits<double>::quiet_NaN(), 0.5);  // NaN → 跳过
    EXPECT_EQ(t.size(), 0u);
    t.Push(5 * kSec, 0.60, std::numeric_limits<double>::quiet_NaN());  // mid NaN → mid=sharp
    EXPECT_EQ(t.size(), 1u);
    EXPECT_DOUBLE_EQ(t.LastGap(), 0.0);  // gap = |0.60 − 0.60| = 0
}

// SFT-04: sharp 上升 → velocity 正
TEST(SharpFairTrack, SFT04_VelocityRising) {
    SharpFairTrack t;
    t.Push(kT0, 0.50, 0.50);
    t.Push(kT0 + 5 * kSec, 0.55, 0.55);
    EXPECT_NEAR(t.Velocity(10 * kSec), 0.01, 1e-9);  // (0.55−0.50)/5s
}

// SFT-05: sharp 下降 → velocity 负
TEST(SharpFairTrack, SFT05_VelocityFalling) {
    SharpFairTrack t;
    t.Push(kT0, 0.60, 0.60);
    t.Push(kT0 + 4 * kSec, 0.52, 0.52);
    EXPECT_NEAR(t.Velocity(10 * kSec), -0.02, 1e-9);  // (0.52−0.60)/4s
}

// SFT-06: gap 缩小 → ConvergenceRate < 0 (收敛, 持仓变对)
TEST(SharpFairTrack, SFT06_Converging) {
    SharpFairTrack t;
    t.Push(kT0, 0.60, 0.50);            // gap 0.10
    t.Push(kT0 + 5 * kSec, 0.60, 0.55);  // gap 0.05
    EXPECT_NEAR(t.ConvergenceRate(10 * kSec), -0.01, 1e-9);  // (0.05−0.10)/5s
    EXPECT_LT(t.ConvergenceRate(10 * kSec), 0.0);
}

// SFT-07: gap 扩大 → ConvergenceRate > 0 (发散, 持仓变错)
TEST(SharpFairTrack, SFT07_Diverging) {
    SharpFairTrack t;
    t.Push(kT0, 0.60, 0.55);            // gap 0.05
    t.Push(kT0 + 5 * kSec, 0.60, 0.50);  // gap 0.10
    EXPECT_NEAR(t.ConvergenceRate(10 * kSec), 0.01, 1e-9);  // (0.10−0.05)/5s
    EXPECT_GT(t.ConvergenceRate(10 * kSec), 0.0);
}

// SFT-08: sharp 波动 = 相邻变化 RMS
TEST(SharpFairTrack, SFT08_Vol) {
    SharpFairTrack t;
    t.Push(kT0, 0.50, 0.50);
    t.Push(kT0 + 1 * kSec, 0.52, 0.52);  // Δ +0.02
    t.Push(kT0 + 2 * kSec, 0.50, 0.50);  // Δ −0.02
    EXPECT_NEAR(t.Vol(10 * kSec), 0.02, 1e-9);  // sqrt((0.02²+0.02²)/2)
}

// SFT-09: 窗口边界 — 窗口外样本不计入
TEST(SharpFairTrack, SFT09_WindowBoundary) {
    SharpFairTrack t;
    for (int i = 0; i < 10; ++i) t.Push(kT0 + static_cast<std::int64_t>(i) * kSec, 0.50, 0.50);
    EXPECT_EQ(t.size(), 10u);
    // last=9s, 窗口 5s → cutoff=4s; ts∈{4,5,6,7,8,9} = 6 个
    EXPECT_EQ(t.WindowSampleCount(5 * kSec), 6u);
    EXPECT_EQ(t.WindowSampleCount(100 * kSec), 10u);  // 大窗口含全部
}

// SFT-10: 容量回绕 — size 封顶 kCapacity, 最新样本正确
TEST(SharpFairTrack, SFT10_CapacityWraparound) {
    SharpFairTrack t;
    const std::size_t over = SharpFairTrack::kCapacity + 10;
    for (std::size_t i = 0; i < over; ++i)
        t.Push(kT0 + static_cast<std::int64_t>(i) * kSec, 0.30 + 0.001 * static_cast<double>(i), 0.30);
    EXPECT_EQ(t.size(), SharpFairTrack::kCapacity);
    EXPECT_EQ(t.last_ts_ns(), kT0 + static_cast<std::int64_t>(over - 1) * kSec);
}

// SFT-11: 窗口内 <2 有效样本 → velocity/conv NaN (无前视/不外推单点)
TEST(SharpFairTrack, SFT11_SingleSampleInWindowNaN) {
    SharpFairTrack t;
    t.Push(kT0, 0.50, 0.50);
    t.Push(kT0 + 100 * kSec, 0.55, 0.55);  // 距上一个 100s
    // 窗口 5s: 只含最新 1 个 → first_in_window 退化 → NaN
    EXPECT_TRUE(std::isnan(t.Velocity(5 * kSec)));
    EXPECT_EQ(t.WindowSampleCount(5 * kSec), 1u);
    // 大窗口含两个 → 有值
    EXPECT_FALSE(std::isnan(t.Velocity(200 * kSec)));
}
