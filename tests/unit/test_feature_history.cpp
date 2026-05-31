// tests/unit/test_feature_history.cpp — 时序特征环形缓冲单测 (PIT / BR-1 / 数值)
//
// Owner: 老雷 (GM) — 时序地基 (老板 2026-05-31)
// 覆盖: push/单调门/容量回绕/窗口边界/变化率/realized vol/PIT 无前视/脏样本拒绝

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "stcpp/ml/feature_history.hpp"

using stcpp::ml::FeatureHistory;

namespace {
constexpr std::int64_t kSec = 1'000'000'000LL;  // 1s in ns
}

// FH-01: 空缓冲 — 所有派生 NaN, size=0
TEST(FeatureHistory, FH01_EmptyYieldsNaN) {
    FeatureHistory h;
    EXPECT_EQ(h.size(), 0u);
    EXPECT_TRUE(h.empty());
    EXPECT_TRUE(std::isnan(h.RateOfChangePerSec(30 * kSec)));
    EXPECT_TRUE(std::isnan(h.RealizedVol(30 * kSec)));
}

// FH-02: 单样本 — 不足 2, 派生仍 NaN
TEST(FeatureHistory, FH02_SingleSampleNaN) {
    FeatureHistory h;
    h.Push(10 * kSec, 0.50);
    EXPECT_EQ(h.size(), 1u);
    EXPECT_TRUE(std::isnan(h.RateOfChangePerSec(30 * kSec)));
    EXPECT_TRUE(std::isnan(h.RealizedVol(30 * kSec)));
}

// FH-03: 变化率 — 价 0.50→0.56 跨 3s → +0.02/sec
TEST(FeatureHistory, FH03_RateOfChange) {
    FeatureHistory h;
    h.Push(10 * kSec, 0.50);
    h.Push(13 * kSec, 0.56);  // +0.06 / 3s = 0.02/s
    EXPECT_NEAR(h.RateOfChangePerSec(30 * kSec), 0.02, 1e-12);
    // 下跌方向: 再降到 0.50 @16s → first(0.50@10) last(0.50@16) → 0/6 = 0
    h.Push(16 * kSec, 0.50);
    EXPECT_NEAR(h.RateOfChangePerSec(30 * kSec), 0.0, 1e-12);
}

// FH-04: realized vol — 相邻变化 {+0.06, -0.06} → RMS = 0.06
TEST(FeatureHistory, FH04_RealizedVol) {
    FeatureHistory h;
    h.Push(10 * kSec, 0.50);
    h.Push(11 * kSec, 0.56);  // Δ +0.06
    h.Push(12 * kSec, 0.50);  // Δ -0.06
    // RMS = sqrt((0.06^2 + 0.06^2)/2) = 0.06
    EXPECT_NEAR(h.RealizedVol(30 * kSec), 0.06, 1e-12);
    // 静止序列 → vol 0
    FeatureHistory flat;
    flat.Push(10 * kSec, 0.40);
    flat.Push(11 * kSec, 0.40);
    flat.Push(12 * kSec, 0.40);
    EXPECT_NEAR(flat.RealizedVol(30 * kSec), 0.0, 1e-12);
}

// FH-05: 单调门 — ts ≤ last 跳过 (停滞 book 重复读不污染; 乱序不入)
TEST(FeatureHistory, FH05_MonotonicGate) {
    FeatureHistory h;
    h.Push(10 * kSec, 0.50);
    h.Push(10 * kSec, 0.99);  // 重复 ts → 跳过
    h.Push(9 * kSec, 0.01);   // 乱序 (过去) → 跳过
    EXPECT_EQ(h.size(), 1u);
    EXPECT_EQ(h.last_ts_ns(), 10 * kSec);
    // 重复读停滞 book 不应制造 vol
    h.Push(10 * kSec, 0.50);
    EXPECT_EQ(h.size(), 1u);
}

// FH-06: 脏样本拒绝 — 非有限 / 越界 (0,1) 不入
TEST(FeatureHistory, FH06_DirtySampleRejected) {
    FeatureHistory h;
    h.Push(10 * kSec, std::numeric_limits<double>::quiet_NaN());
    h.Push(11 * kSec, 0.0);   // 边界 (不 ∈ open (0,1))
    h.Push(12 * kSec, 1.0);   // 边界
    h.Push(13 * kSec, 1.5);   // 越界
    h.Push(14 * kSec, -0.1);  // 越界
    EXPECT_EQ(h.size(), 0u);
    h.Push(15 * kSec, 0.50);  // 合法
    EXPECT_EQ(h.size(), 1u);
}

// FH-07: PIT 窗口边界 — 只用 [as_of−W, as_of] 内样本, 窗口外不计 (无前视/无陈旧泄漏)
TEST(FeatureHistory, FH07_WindowBoundaryPIT) {
    FeatureHistory h;
    // 老样本 (远在窗口外) — 不应进入 30s 窗口派生
    h.Push(1 * kSec, 0.10);
    h.Push(2 * kSec, 0.90);  // 大跳, 但在窗口外
    // 窗口内样本 (as_of=100s, 回看 30s → cutoff=70s)
    h.Push(98 * kSec, 0.50);
    h.Push(100 * kSec, 0.52);  // +0.02 / 2s = 0.01/s
    EXPECT_EQ(h.WindowSampleCount(30 * kSec), 2u) << "窗口内只 2 样本 (老样本在 70s cutoff 外)";
    EXPECT_NEAR(h.RateOfChangePerSec(30 * kSec), 0.01, 1e-12)
        << "PIT: 变化率只看窗口内 (老 0.10→0.90 大跳被正确排除)";
    // realized vol 只算窗口内一个 diff (0.50→0.52)=0.02
    EXPECT_NEAR(h.RealizedVol(30 * kSec), 0.02, 1e-12);
}

// FH-08: 容量回绕 — 超 kCapacity 后只保留最近样本, 不崩, 派生用最近窗
TEST(FeatureHistory, FH08_CapacityWraparound) {
    FeatureHistory h;
    const std::size_t cap = FeatureHistory::kCapacity;
    // 推 cap*2 个样本, 价 0.30↔0.31 交替 (每 1s)
    for (std::size_t i = 0; i < cap * 2; ++i) {
        const double p = (i % 2 == 0) ? 0.30 : 0.31;
        h.Push(static_cast<std::int64_t>(i + 1) * kSec, p);
    }
    EXPECT_EQ(h.size(), cap) << "容量封顶在 kCapacity";
    // 最近窗口仍可派生 (不崩, 不读越界)
    EXPECT_FALSE(std::isnan(h.RealizedVol(static_cast<std::int64_t>(cap) * kSec)));
    EXPECT_NEAR(h.RealizedVol(static_cast<std::int64_t>(cap) * kSec), 0.01, 1e-9)
        << "交替 0.30/0.31 → |Δ|=0.01 RMS=0.01";
}

// FH-09: BR-1 确定性 — 同序列两次独立喂 → 派生逐位一致 (回测/实盘同源前提)
TEST(FeatureHistory, FH09_DeterministicBR1) {
    auto feed = [](FeatureHistory& h) {
        h.Push(10 * kSec, 0.40);
        h.Push(12 * kSec, 0.45);
        h.Push(15 * kSec, 0.43);
        h.Push(20 * kSec, 0.50);
    };
    FeatureHistory a, b;
    feed(a);
    feed(b);
    EXPECT_DOUBLE_EQ(a.RateOfChangePerSec(30 * kSec), b.RateOfChangePerSec(30 * kSec));
    EXPECT_DOUBLE_EQ(a.RealizedVol(30 * kSec), b.RealizedVol(30 * kSec));
}
