// tests/unit/test_feature_history.cpp — 时序特征环形缓冲单测 (PIT / BR-1 / 数值 / 流动性)
//
// Owner: 老雷 (GM) — 时序地基 (老板 2026-05-31)
// 覆盖: push/单调门/容量回绕/窗口边界/变化率/realized vol/PIT 无前视 +
//       slice-2 卖不出 (无 bid 占比 / 退出深度) + observe-always (无价/无 bid tick 仍记录)

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "stcpp/ml/feature_history.hpp"

using stcpp::ml::FeatureHistory;

namespace {
constexpr std::int64_t kSec = 1'000'000'000LL;  // 1s in ns
constexpr double kNaNp = std::numeric_limits<double>::quiet_NaN();
// 便捷: 推一个"有效价 + 有 bid"样本 (bid = mp-0.01, size 500)。
void push_ok(FeatureHistory& h, std::int64_t ts, double mp) { h.Push(ts, mp, mp - 0.01, 500.0); }
}  // namespace

// FH-01: 空缓冲 — 所有派生 NaN, size=0
TEST(FeatureHistory, FH01_EmptyYieldsNaN) {
    FeatureHistory h;
    EXPECT_EQ(h.size(), 0u);
    EXPECT_TRUE(h.empty());
    EXPECT_TRUE(std::isnan(h.RateOfChangePerSec(30 * kSec)));
    EXPECT_TRUE(std::isnan(h.RealizedVol(30 * kSec)));
    EXPECT_TRUE(std::isnan(h.BidAbsenceFrac(30 * kSec)));
    EXPECT_TRUE(std::isnan(h.ExitDepthMean(30 * kSec)));
}

// FH-02: 单样本 — 价 derive 不足 2, NaN; 流动性 derive 单样本即可
TEST(FeatureHistory, FH02_SingleSample) {
    FeatureHistory h;
    push_ok(h, 10 * kSec, 0.50);
    EXPECT_EQ(h.size(), 1u);
    EXPECT_TRUE(std::isnan(h.RateOfChangePerSec(30 * kSec)));
    EXPECT_TRUE(std::isnan(h.RealizedVol(30 * kSec)));
    EXPECT_NEAR(h.BidAbsenceFrac(30 * kSec), 0.0, 1e-12);  // 有 bid
    EXPECT_NEAR(h.ExitDepthMean(30 * kSec), 500.0, 1e-9);
}

// FH-03: 变化率 — 价 0.50→0.56 跨 3s → +0.02/sec
TEST(FeatureHistory, FH03_RateOfChange) {
    FeatureHistory h;
    push_ok(h, 10 * kSec, 0.50);
    push_ok(h, 13 * kSec, 0.56);  // +0.06 / 3s = 0.02/s
    EXPECT_NEAR(h.RateOfChangePerSec(30 * kSec), 0.02, 1e-12);
    push_ok(h, 16 * kSec, 0.50);  // first(0.50@10) last(0.50@16) → 0/6 = 0
    EXPECT_NEAR(h.RateOfChangePerSec(30 * kSec), 0.0, 1e-12);
}

// FH-04: realized vol — 相邻变化 {+0.06, -0.06} → RMS = 0.06
TEST(FeatureHistory, FH04_RealizedVol) {
    FeatureHistory h;
    push_ok(h, 10 * kSec, 0.50);
    push_ok(h, 11 * kSec, 0.56);  // Δ +0.06
    push_ok(h, 12 * kSec, 0.50);  // Δ -0.06
    EXPECT_NEAR(h.RealizedVol(30 * kSec), 0.06, 1e-12);
    FeatureHistory flat;
    push_ok(flat, 10 * kSec, 0.40);
    push_ok(flat, 11 * kSec, 0.40);
    push_ok(flat, 12 * kSec, 0.40);
    EXPECT_NEAR(flat.RealizedVol(30 * kSec), 0.0, 1e-12);
}

// FH-05: 单调门 — ts ≤ last 跳过 (停滞 book 重复读不污染; 乱序不入)
TEST(FeatureHistory, FH05_MonotonicGate) {
    FeatureHistory h;
    push_ok(h, 10 * kSec, 0.50);
    push_ok(h, 10 * kSec, 0.99);  // 重复 ts → 跳过
    push_ok(h, 9 * kSec, 0.01);   // 乱序 → 跳过
    EXPECT_EQ(h.size(), 1u);
    EXPECT_EQ(h.last_ts_ns(), 10 * kSec);
}

// FH-06: observe-always — 无价/无 bid 的 tick 仍记录 (卖不出正是要观测的事件), 价 derive 跳过无效价
TEST(FeatureHistory, FH06_ObserveAlways) {
    FeatureHistory h;
    push_ok(h, 10 * kSec, 0.50);                  // 有效价 + 有 bid
    h.Push(11 * kSec, kNaNp, 0.0, 0.0);           // 无价 + 无 bid (卖不出 tick) → 仍记录
    h.Push(12 * kSec, 1.5, 0.0, 0.0);             // 越界价 + 无 bid → 仍记录
    push_ok(h, 13 * kSec, 0.54);                  // 有效价 + 有 bid
    EXPECT_EQ(h.size(), 4u) << "observe-always: 无价/无 bid tick 也入缓冲 (不审查偏置)";
    // 价 derive 只用有效价样本 (0.50@10 → 0.54@13): ROC=(0.54-0.50)/3s
    EXPECT_NEAR(h.RateOfChangePerSec(30 * kSec), 0.04 / 3.0, 1e-12)
        << "价 derive 跳过无效价, 只算 0.50→0.54";
    EXPECT_NEAR(h.RealizedVol(30 * kSec), 0.04, 1e-12);  // 唯一有效价 diff = 0.04
    // 流动性 derive 含全部 4 样本: 2/4 无 bid
    EXPECT_NEAR(h.BidAbsenceFrac(30 * kSec), 0.5, 1e-12) << "4 样本中 2 个无 bid → 0.5";
}

// FH-07: PIT 窗口边界 — 只用 [as_of−W, as_of] 内样本, 窗口外不计 (无前视/无陈旧泄漏)
TEST(FeatureHistory, FH07_WindowBoundaryPIT) {
    FeatureHistory h;
    push_ok(h, 1 * kSec, 0.10);
    push_ok(h, 2 * kSec, 0.90);  // 老样本大跳, 窗口外
    push_ok(h, 98 * kSec, 0.50);
    push_ok(h, 100 * kSec, 0.52);  // +0.02 / 2s = 0.01/s
    EXPECT_EQ(h.WindowSampleCount(30 * kSec), 2u);
    EXPECT_NEAR(h.RateOfChangePerSec(30 * kSec), 0.01, 1e-12)
        << "PIT: 老 0.10→0.90 大跳被窗口正确排除";
    EXPECT_NEAR(h.RealizedVol(30 * kSec), 0.02, 1e-12);
}

// FH-08: 容量回绕 — 超 kCapacity 后只保留最近样本, 不崩
TEST(FeatureHistory, FH08_CapacityWraparound) {
    FeatureHistory h;
    const std::size_t cap = FeatureHistory::kCapacity;
    for (std::size_t i = 0; i < cap * 2; ++i) {
        const double p = (i % 2 == 0) ? 0.30 : 0.31;
        push_ok(h, static_cast<std::int64_t>(i + 1) * kSec, p);
    }
    EXPECT_EQ(h.size(), cap);
    EXPECT_NEAR(h.RealizedVol(static_cast<std::int64_t>(cap) * kSec), 0.01, 1e-9);
}

// FH-09: BR-1 确定性 — 同序列两次独立喂 → 派生逐位一致 (回测/实盘同源前提)
TEST(FeatureHistory, FH09_DeterministicBR1) {
    auto feed = [](FeatureHistory& h) {
        push_ok(h, 10 * kSec, 0.40);
        push_ok(h, 12 * kSec, 0.45);
        h.Push(15 * kSec, kNaNp, 0.0, 0.0);  // 含 observe-always 无效样本
        push_ok(h, 20 * kSec, 0.50);
    };
    FeatureHistory a, b;
    feed(a);
    feed(b);
    EXPECT_DOUBLE_EQ(a.RateOfChangePerSec(30 * kSec), b.RateOfChangePerSec(30 * kSec));
    EXPECT_DOUBLE_EQ(a.RealizedVol(30 * kSec), b.RealizedVol(30 * kSec));
    EXPECT_DOUBLE_EQ(a.BidAbsenceFrac(30 * kSec), b.BidAbsenceFrac(30 * kSec));
    EXPECT_DOUBLE_EQ(a.ExitDepthMean(30 * kSec), b.ExitDepthMean(30 * kSec));
}

// FH-10: 卖不出 — 无 bid 占比 (单边倒挂) + 退出深度均值
TEST(FeatureHistory, FH10_ExitLiquidity) {
    FeatureHistory h;
    h.Push(10 * kSec, 0.50, 0.49, 400.0);  // 有 bid 400
    h.Push(11 * kSec, 0.50, 0.0, 0.0);     // 无 bid (卖不出)
    h.Push(12 * kSec, 0.50, 0.48, 200.0);  // 有 bid 200
    h.Push(13 * kSec, 0.50, -1.0, 0.0);    // 无 bid (脏价)
    // 4 样本, 2 无 bid → 0.5; 深度均值 = (400+0+200+0)/4 = 150
    EXPECT_NEAR(h.BidAbsenceFrac(30 * kSec), 0.5, 1e-12);
    EXPECT_NEAR(h.ExitDepthMean(30 * kSec), 150.0, 1e-9);
    // 全无 bid → frac 1.0 (整窗卖不出), depth 0
    FeatureHistory dead;
    dead.Push(10 * kSec, kNaNp, 0.0, 0.0);
    dead.Push(11 * kSec, kNaNp, 0.0, 0.0);
    EXPECT_NEAR(dead.BidAbsenceFrac(30 * kSec), 1.0, 1e-12);
    EXPECT_NEAR(dead.ExitDepthMean(30 * kSec), 0.0, 1e-12);
}

// FH-11: 流动性 derive 的 PIT 窗口 — 老的"有 bid"样本不掩盖近期"卖不出"
TEST(FeatureHistory, FH11_ExitLiquidityWindowPIT) {
    FeatureHistory h;
    h.Push(1 * kSec, 0.50, 0.49, 1000.0);   // 老: 充裕 bid, 窗口外
    h.Push(98 * kSec, 0.50, 0.0, 0.0);      // 近期: 卖不出
    h.Push(100 * kSec, 0.50, 0.0, 0.0);     // 近期: 卖不出
    // 30s 窗口 (cutoff 70s): 只 2 个近期样本, 全无 bid → frac 1.0 (老充裕样本不掩盖)
    EXPECT_NEAR(h.BidAbsenceFrac(30 * kSec), 1.0, 1e-12)
        << "PIT: 近期卖不出不被老的充裕流动性掩盖";
    EXPECT_NEAR(h.ExitDepthMean(30 * kSec), 0.0, 1e-12);
}
