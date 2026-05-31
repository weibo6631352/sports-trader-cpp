// tests/unit/test_rank_ic.cpp — Spearman rank-IC (小蒋评审 P0 信号质量指标)
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

#include "stcpp/backtest/stats.hpp"

namespace st = stcpp::backtest::stats;

// IC01: 完全正相关 → +1
TEST(RankIC, IC01_PerfectPositive) {
    std::vector<double> pred{1, 2, 3, 4, 5};
    std::vector<double> real{10, 20, 30, 40, 50};
    EXPECT_NEAR(st::rank_ic(pred, real), 1.0, 1e-9);
}

// IC02: 完全负相关 → −1 (单调反序也算完美秩相关)
TEST(RankIC, IC02_PerfectNegative) {
    std::vector<double> pred{1, 2, 3, 4, 5};
    std::vector<double> real{50, 40, 30, 20, 10};
    EXPECT_NEAR(st::rank_ic(pred, real), -1.0, 1e-9);
}

// IC03: 非线性单调 → Spearman=1 (秩相关不受单调变换影响, 优于 Pearson)
TEST(RankIC, IC03_MonotoneNonlinear) {
    std::vector<double> pred{1, 2, 3, 4, 5};
    std::vector<double> real{1, 4, 9, 16, 25};  // 平方 (非线性但单调)
    EXPECT_NEAR(st::rank_ic(pred, real), 1.0, 1e-9);
}

// IC04: ties 用平均秩
TEST(RankIC, IC04_Ties) {
    std::vector<double> pred{1, 1, 2, 2};
    std::vector<double> real{10, 10, 20, 20};
    EXPECT_NEAR(st::rank_ic(pred, real), 1.0, 1e-9) << "ties 同秩 → 完美相关";
}

// IC05: NaN 对剔除 + <2 → NaN
TEST(RankIC, IC05_NaNAndInsufficient) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> pred{1, nan, 3, 4};
    std::vector<double> real{10, 99, 30, 40};
    EXPECT_NEAR(st::rank_ic(pred, real), 1.0, 1e-9) << "NaN 对剔除后 3 点完美相关";
    // 常数序列 → NaN
    EXPECT_TRUE(std::isnan(st::rank_ic({1, 1, 1}, {1, 2, 3})));
    // <2 → NaN
    EXPECT_TRUE(std::isnan(st::rank_ic({1.0}, {2.0})));
}

// IC06: 零相关 (随机) ≈ 0
TEST(RankIC, IC06_NoCorrelation) {
    std::vector<double> pred{1, 2, 3, 4};
    std::vector<double> real{3, 1, 4, 2};  // 打乱
    const double ic = st::rank_ic(pred, real);
    EXPECT_GE(ic, -1.0);
    EXPECT_LE(ic, 1.0);
    EXPECT_LT(std::abs(ic), 1.0) << "非完美相关";
}
