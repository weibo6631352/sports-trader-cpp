// tests/unit/test_game_score_history.cpp — 比分时序 ring 单测 (批1 体育动态)
#include <cmath>
#include <gtest/gtest.h>
#include "stcpp/ml/game_score_history.hpp"
using stcpp::ml::GameScoreHistory;
namespace { constexpr std::int64_t kSec = 1'000'000'000LL; }

// GH-01: 进球新鲜度 — 刚进球 freshness≈1, 衰减
TEST(GameScoreHistory, GH01_GoalFreshness) {
    GameScoreHistory h;
    h.Observe(10 * kSec, 0, 0);
    h.Observe(20 * kSec, 1, 0);  // 进球 @20s
    EXPECT_NEAR(h.GoalFreshness(20 * kSec), 1.0, 1e-9) << "刚进球 freshness=1";
    EXPECT_NEAR(h.GoalFreshness(140 * kSec), std::exp(-1.0), 1e-9) << "120s 后 = exp(-1)";
    EXPECT_LT(h.GoalFreshness(260 * kSec), h.GoalFreshness(140 * kSec)) << "继续衰减";
}

// GH-02: 比分不变 → 新鲜度持续衰减 (last_change 不更新)
TEST(GameScoreHistory, GH02_NoChangeDecays) {
    GameScoreHistory h;
    h.Observe(10 * kSec, 1, 0);  // change
    h.Observe(70 * kSec, 1, 0);  // 同比分, last_change 仍 10s
    EXPECT_NEAR(h.GoalFreshness(70 * kSec), std::exp(-60.0 / 120.0), 1e-9);
}

// GH-03: 净动量 — score_diff 0→2 跨窗口 → +2
TEST(GameScoreHistory, GH03_NetMomentum) {
    GameScoreHistory h;
    h.Observe(10 * kSec, 0, 0);
    h.Observe(100 * kSec, 2, 0);  // diff 0→2
    EXPECT_NEAR(h.NetMomentum(300 * kSec), 2.0, 1e-9);
}

// GH-04: 空 → NaN
TEST(GameScoreHistory, GH04_Empty) {
    GameScoreHistory h;
    EXPECT_TRUE(std::isnan(h.GoalFreshness(10 * kSec)));
    EXPECT_FALSE(h.valid());
}
