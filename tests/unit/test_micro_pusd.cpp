// tests/unit/test_micro_pusd.cpp — MicroPUSD 强类型单测 (方案 B B1 c1)
//
// Owner: 老雷 (GM)  last_review: 2026-05-30
// 实施老周 spec §1; 验证运算符 / 转换 / UDL / ABI + 单位语义 (×1e6 钉死).

#include <type_traits>

#include <gtest/gtest.h>

#include "stcpp/domain/micro_pusd.hpp"

using stcpp::domain::MicroPUSD;
using namespace stcpp::domain;  // UDL _pusd / _upusd

// ---------------------------------------------------------------------------
// 构造 + 转换
// ---------------------------------------------------------------------------
TEST(MicroPUSD, ExplicitConstructAndRaw) {
    constexpr MicroPUSD m{10'000'000};  // 10 pUSD (micro)
    EXPECT_EQ(m.v, 10'000'000);
    EXPECT_DOUBLE_EQ(m.to_pusd(), 10.0);
}

TEST(MicroPUSD, FromPusdRoundsToNearest) {
    EXPECT_EQ(MicroPUSD::from_pusd(10.0).v, 10'000'000);
    EXPECT_EQ(MicroPUSD::from_pusd(3.6307).v, 3'630'700);
    // round-to-nearest (非截断): 1.9999995 → 2000000 (截断会系统性低估 exposure)
    EXPECT_EQ(MicroPUSD::from_pusd(1.9999995).v, 2'000'000);
    EXPECT_EQ(MicroPUSD::from_pusd(-5.0).v, -5'000'000);
}

TEST(MicroPUSD, FromMicroIsConstexpr) {
    constexpr MicroPUSD m = MicroPUSD::from_micro(25'000'000);
    static_assert(m.v == 25'000'000);
    EXPECT_DOUBLE_EQ(m.to_pusd(), 25.0);
}

TEST(MicroPUSD, RoundTrip) {
    for (double pusd : {0.0, 1.0, 10.5, 1000.0, 0.001, -3.33}) {
        EXPECT_NEAR(MicroPUSD::from_pusd(pusd).to_pusd(), pusd, 1e-6);
    }
}

// ---------------------------------------------------------------------------
// UDL (constexpr)
// ---------------------------------------------------------------------------
TEST(MicroPUSD, PusdLiteralIntAndFloat) {
    static_assert((10_pusd).v == 10'000'000);    // unsigned long long 版
    static_assert((10.0_pusd).v == 10'000'000);  // long double 版
    static_assert((0.5_pusd).v == 500'000);
    static_assert((1_upusd).v == 1);  // micro 直读
    EXPECT_EQ((10_pusd), (10.0_pusd));
    EXPECT_EQ((10_pusd), MicroPUSD::from_micro(10'000'000));
}

// ---------------------------------------------------------------------------
// 单位语义 — 演示本 bug: cap 写 10 (本意 pUSD) 应是 10_pusd=1e7 micro, 非 from_micro(10)
// ---------------------------------------------------------------------------
TEST(MicroPUSD, UnitSemantics_BugDemo) {
    const MicroPUSD cap_correct = 10_pusd;                  // 10 pUSD 上限 (本意)
    const MicroPUSD cap_buggy = MicroPUSD::from_micro(10);  // 原 bug: 把 10 当 micro = 1e-5 pUSD
    EXPECT_EQ(cap_correct.v, 10'000'000);
    EXPECT_EQ(cap_buggy.v, 10);
    EXPECT_NE(cap_correct, cap_buggy);  // 差 1e6 量级 — 强类型让这种漏乘在边界被钉死

    // 一笔 3.63 pUSD 的单 (size): 过正确 cap, 超 buggy cap
    const MicroPUSD order = MicroPUSD::from_pusd(3.63);
    EXPECT_LT(order, cap_correct) << "3.63 pUSD < 10 pUSD cap (正确)";
    EXPECT_GT(order, cap_buggy) << "3.63 pUSD(micro) > 10 micro (原 bug: 恒拒)";
}

// ---------------------------------------------------------------------------
// 运算符: 同型加减 / 整数倍均摊 / 同型除返比例
// ---------------------------------------------------------------------------
TEST(MicroPUSD, AddSubExposureAccumulate) {
    MicroPUSD e = 5_pusd;
    e += 3_pusd;
    EXPECT_EQ(e, 8_pusd);
    e -= 2_pusd;
    EXPECT_EQ(e, 6_pusd);
    EXPECT_EQ(6_pusd + 4_pusd, 10_pusd);
    EXPECT_EQ(10_pusd - 4_pusd, 6_pusd);
    EXPECT_EQ(-(5_pusd), MicroPUSD{-5'000'000});
}

TEST(MicroPUSD, IntScaleAndSplit) {
    EXPECT_EQ(5_pusd * 3, 15_pusd);
    EXPECT_EQ(3 * 5_pusd, 15_pusd);  // 左右对称
    EXPECT_EQ(15_pusd / 3, 5_pusd);
    MicroPUSD m = 10_pusd;
    m *= 2;
    EXPECT_EQ(m, 20_pusd);
    m /= 4;
    EXPECT_EQ(m, 5_pusd);
}

TEST(MicroPUSD, SameTypeDivideIsRatio) {
    // DD% / 利用率: 同型相除 → 无量纲 double
    EXPECT_DOUBLE_EQ(3_pusd / 10_pusd, 0.3);
    EXPECT_DOUBLE_EQ(50_pusd / 1000_pusd, 0.05);
}

// ---------------------------------------------------------------------------
// 比较 (4 cap 比较点用)
// ---------------------------------------------------------------------------
TEST(MicroPUSD, Comparison) {
    EXPECT_TRUE(3_pusd < 10_pusd);
    EXPECT_TRUE(10_pusd <= 10_pusd);
    EXPECT_TRUE(11_pusd > 10_pusd);
    EXPECT_TRUE(10_pusd == 10_pusd);
    EXPECT_TRUE(3_pusd != 10_pusd);
    static_assert(3_pusd < 10_pusd);
}

// ---------------------------------------------------------------------------
// ABI / layout (老周 §6.1; 与 header static_assert 双保险)
// ---------------------------------------------------------------------------
TEST(MicroPUSD, AbiLayout) {
    static_assert(sizeof(MicroPUSD) == 8);
    static_assert(alignof(MicroPUSD) == alignof(std::int64_t));
    static_assert(std::is_standard_layout_v<MicroPUSD>);
    static_assert(std::is_trivially_copyable_v<MicroPUSD>);
    EXPECT_EQ(sizeof(MicroPUSD), sizeof(std::int64_t));
}

// 注: operator*(MicroPUSD, MicroPUSD) 故意不声明 (pUSD² 无意义) —— 误用是编译错,
//     无法运行期测; 老高 CI grep 守护「三边界裸 ×1e6」。
