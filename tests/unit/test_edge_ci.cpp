// test_edge_ci.cpp — ComputeEdgeCiLower + ResolveEdgeCiLower 纯函数单测
//
// ResolveEdgeCiLower 背景 (老板 2026-06-03「sharp 路径纯 net-EV 门」):
//   sharp_inplay 源 = bet365 de-vig 共识【点估计】, 不是 n_eff 样本均值。对它套二项抽样惩罚
//   z·√(p(1−p)/n_eff) (实测 n_eff≈10 → 惩罚 ~0.26) 会把全部 in-play 套利 (~0.02–0.05) 砍成负
//   → sizing 永远 NO_EDGE → sharp 套利一笔不成交。修法: sharp 源走纯 net-EV (raw_edge − margin),
//   非 sharp 源仍走二项 CI。本测锁定两条分支行为, 防回归。
#include <gtest/gtest.h>

#include <cmath>

#include "stcpp/strategy/edge_ci.hpp"

using stcpp::strategy::ComputeEdgeCiLower;
using stcpp::strategy::ResolveEdgeCiLower;

namespace {

// ---- ComputeEdgeCiLower (既有二项 CI, 行为不变保护) ----

TEST(ComputeEdgeCiLower, BinomialPenaltyShrinksEdge) {
    // p≈0.5, n_eff=10, z=1.645 → sigma=√(0.25/10)=0.1581, 惩罚=0.260。
    //   raw=0.51−0.49=0.02 → ci=0.02−0.260≈−0.240 (深负 = sharp 被错杀的根因)。
    const double ci = ComputeEdgeCiLower(0.51, 0.49, 10, 1.645);
    EXPECT_NEAR(ci, 0.02 - 1.645 * std::sqrt(0.51 * 0.49 / 10.0), 1e-9);
    EXPECT_LT(ci, 0.0);  // 真实 in-play 微小错价被二项惩罚砍成负
}

TEST(ComputeEdgeCiLower, FailClosedOnBadInput) {
    EXPECT_DOUBLE_EQ(ComputeEdgeCiLower(0.5, 0.5, 0, 1.645), -1.0);                       // n_eff<=0
    EXPECT_DOUBLE_EQ(ComputeEdgeCiLower(std::nan(""), 0.5, 10, 1.645), -1.0);             // NaN
}

// ---- ResolveEdgeCiLower (源感知 — 本次新增) ----

// sharp 源: 不扣二项噪声 = raw_edge − margin (margin 默认 0 = 纯 net-EV)。
TEST(ResolveEdgeCiLower, SharpUsesRawEdgeNoBinomialPenalty) {
    // 同样 p=0.51/0.49, n_eff=10 — 二项分支会得 ~−0.24; sharp 分支应得 raw=+0.02。
    const double ci = ResolveEdgeCiLower(/*is_sharp=*/true, 0.51, 0.49, 10, 1.645, /*margin=*/0.0);
    EXPECT_NEAR(ci, 0.02, 1e-12);  // 纯 raw_edge, 无样本惩罚 → 正 → 下游 net-EV 门接管
    EXPECT_GT(ci, 0.0);
}

// sharp margin>0: 扣固定安全带, 不扣 n_eff 噪声。
TEST(ResolveEdgeCiLower, SharpSubtractsFixedMargin) {
    const double ci = ResolveEdgeCiLower(/*is_sharp=*/true, 0.60, 0.50, 10, 1.645, /*margin=*/0.03);
    EXPECT_NEAR(ci, 0.10 - 0.03, 1e-12);  // raw 0.10 − margin 0.03 = 0.07
}

// 非 sharp 源: 回退到既有二项 CI (逐位等于 ComputeEdgeCiLower)。
TEST(ResolveEdgeCiLower, NonSharpFallsBackToBinomial) {
    const double want = ComputeEdgeCiLower(0.51, 0.49, 10, 1.645);
    const double got = ResolveEdgeCiLower(/*is_sharp=*/false, 0.51, 0.49, 10, 1.645, /*margin=*/0.0);
    EXPECT_DOUBLE_EQ(got, want);
    EXPECT_LT(got, 0.0);  // 统计估计仍受抽样惩罚 (正确: 它们确是噪声估计)
}

// sharp 分支 fail-closed: NaN 输入 → −1。
TEST(ResolveEdgeCiLower, SharpFailClosedOnNaN) {
    EXPECT_DOUBLE_EQ(ResolveEdgeCiLower(true, std::nan(""), 0.5, 10, 1.645, 0.0), -1.0);
    EXPECT_DOUBLE_EQ(ResolveEdgeCiLower(true, 0.5, 0.5, 10, 1.645, std::nan("")), -1.0);
}

// sharp 分支 clamp 到 [−1, 1]。
TEST(ResolveEdgeCiLower, SharpClampsRange) {
    EXPECT_DOUBLE_EQ(ResolveEdgeCiLower(true, 0.99, 0.01, 10, 1.645, 0.0), 0.98);   // 正常范围内
    EXPECT_DOUBLE_EQ(ResolveEdgeCiLower(true, 0.5, 0.5, 10, 1.645, 5.0), -1.0);     // margin 过大 → clamp −1
}

}  // namespace
