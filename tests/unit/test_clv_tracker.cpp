// tests/unit/test_clv_tracker.cpp — CLV 测量 harness 单测 (成果尺子 M3)
//
// Owner: 老雷 (GM) — results plan v1
// 覆盖: CLV 算法 (close mid / 0-1 settle) / 命中率 / 名义加权 / pending / 脏样本

#include <gtest/gtest.h>

#include "stcpp/eval/clv_tracker.hpp"

using stcpp::eval::CLVTracker;

// CL-01: 单笔正 CLV — 买 0.30, 收盘 mid 0.64, 结算 YES(1.0)
TEST(CLVTracker, CL01_SinglePositive) {
    CLVTracker t;
    t.RecordFill("1001", /*entry=*/0.30, /*mid=*/0.31, /*size=*/5.0, /*ts=*/100);
    EXPECT_EQ(t.report().n_pending_fills, 1u);
    t.UpdateMid("1001", 0.64);  // 市场收敛到 0.64
    t.OnSettle("1001", 1.0);    // YES 赢
    const auto r = t.report();
    EXPECT_EQ(r.n_fills, 1u);
    EXPECT_EQ(r.n_pending_fills, 0u);
    EXPECT_NEAR(r.clv_close_mean, 0.64 - 0.30, 1e-12);   // 收盘 mid 口径 = 0.34
    EXPECT_NEAR(r.clv_settle_mean, 1.0 - 0.30, 1e-12);   // 结算口径 = 0.70
    EXPECT_NEAR(r.clv_close_positive_rate, 1.0, 1e-12);
    EXPECT_NEAR(r.notional_weighted_clv_close, 0.34, 1e-12);
}

// CL-02: 负 CLV — 买贵了 (0.60), 收盘 mid 0.40, 结算输 (0.0)
TEST(CLVTracker, CL02_Negative) {
    CLVTracker t;
    t.RecordFill("1001", 0.60, 0.59, 10.0, 200);
    t.UpdateMid("1001", 0.40);
    t.OnSettle("1001", 0.0);
    const auto r = t.report();
    EXPECT_NEAR(r.clv_close_mean, 0.40 - 0.60, 1e-12);   // -0.20
    EXPECT_NEAR(r.clv_settle_mean, 0.0 - 0.60, 1e-12);   // -0.60
    EXPECT_NEAR(r.clv_close_positive_rate, 0.0, 1e-12);
}

// CL-03: 多笔聚合 + 命中率 + 名义加权
TEST(CLVTracker, CL03_MultiAggregate) {
    CLVTracker t;
    // token A: 两笔 (一正一负), token B: 一笔正
    t.RecordFill("A", 0.30, 0.31, 1.0, 1);   // close 0.50 → +0.20
    t.RecordFill("A", 0.55, 0.54, 9.0, 2);   // close 0.50 → -0.05 (大单 9)
    t.UpdateMid("A", 0.50);
    t.RecordFill("B", 0.20, 0.21, 1.0, 3);   // close 0.40 → +0.20
    t.UpdateMid("B", 0.40);
    t.OnSettle("A", 0.0);
    t.OnSettle("B", 1.0);
    const auto r = t.report();
    EXPECT_EQ(r.n_fills, 3u);
    // close CLV: {+0.20, -0.05, +0.20} 均值 = 0.35/3
    EXPECT_NEAR(r.clv_close_mean, (0.20 - 0.05 + 0.20) / 3.0, 1e-12);
    EXPECT_NEAR(r.clv_close_positive_rate, 2.0 / 3.0, 1e-12);
    // 名义加权: (1×0.20 + 9×(-0.05) + 1×0.20) / 11 = (0.2-0.45+0.2)/11 = -0.05/11
    EXPECT_NEAR(r.notional_weighted_clv_close, (-0.05) / 11.0, 1e-12)
        << "大单(9, 负CLV)拉低名义加权, 揭示等权均值的误导";
}

// CL-04: 无收盘 mid → 退化用结算值当 close 参考 (不崩)
TEST(CLVTracker, CL04_NoMidFallback) {
    CLVTracker t;
    t.RecordFill("1001", 0.30, 0.31, 5.0, 100);
    t.OnSettle("1001", 1.0);  // 没 UpdateMid → close_ref 退化 = settle 1.0
    const auto r = t.report();
    EXPECT_NEAR(r.clv_close_mean, 1.0 - 0.30, 1e-12);
}

// CL-05: 脏样本拒 + 空报告
TEST(CLVTracker, CL05_DirtyAndEmpty) {
    CLVTracker t;
    EXPECT_EQ(t.report().n_fills, 0u);
    t.RecordFill("1001", 0.0, 0.5, 5.0, 1);   // entry≤0 拒
    t.RecordFill("1001", 0.5, 0.5, 0.0, 1);   // size≤0 拒
    EXPECT_EQ(t.report().n_pending_fills, 0u);
    // 无成交的 token 结算 = no-op
    t.OnSettle("9999", 1.0);
    EXPECT_EQ(t.report().n_fills, 0u);
}
