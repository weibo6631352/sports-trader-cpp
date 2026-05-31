// tests/unit/test_walk_forward_ic.cpp — book-only walk-forward IC 评估 (小蒋 P0)
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

#include "stcpp/backtest/walk_forward_ic.hpp"

using stcpp::backtest::evaluate_walk_forward_ic;
using stcpp::backtest::LabeledSample;
using stcpp::backtest::WalkForwardConfig;

namespace {
// 小窗口 config: train 10d / oos 10d / embargo 1d / step 10d。
WalkForwardConfig SmallCfg() {
    WalkForwardConfig c;
    const std::int64_t day = WalkForwardConfig::kDayNs;
    c.train_duration_ns = 10 * day;
    c.oos_duration_ns = 10 * day;
    c.embargo_duration_ns = 1 * day;
    c.step_duration_ns = 10 * day;
    c.max_folds = 0;
    return c;
}
}  // namespace

// WFIC01: 预测量与 label 完全相关 → 每折 OOS IC ≈ 1, mean_ic ≈ 1
TEST(WalkForwardIC, WFIC01_PerfectPredictor) {
    const std::int64_t day = WalkForwardConfig::kDayNs;
    std::vector<LabeledSample> s;
    // 0..60 天, 每天 5 个样本, predictor 与 label 单调一致 (perfect rank)
    for (int d = 0; d < 60; ++d) {
        for (int k = 0; k < 5; ++k) {
            LabeledSample x;
            x.as_of_ts_ns = static_cast<std::int64_t>(d) * day + k;
            x.predictor = 0.1 * k + 0.01 * d;
            x.label = x.predictor + 0.001;  // 单调一致
            s.push_back(x);
        }
    }
    const auto rep = evaluate_walk_forward_ic(s, SmallCfg(), 0, 60 * day);
    EXPECT_GT(rep.folds.size(), static_cast<std::size_t>(0)) << "应生成多折";
    EXPECT_GT(rep.valid_folds, static_cast<std::size_t>(0));
    EXPECT_NEAR(rep.mean_ic, 1.0, 1e-6) << "完美预测 → IC≈1";
    EXPECT_GT(rep.total_oos, static_cast<std::size_t>(0));
}

// WFIC02: 反向预测 → IC ≈ −1
TEST(WalkForwardIC, WFIC02_InversePredictor) {
    const std::int64_t day = WalkForwardConfig::kDayNs;
    std::vector<LabeledSample> s;
    for (int d = 0; d < 40; ++d) {
        for (int k = 0; k < 5; ++k) {
            LabeledSample x;
            x.as_of_ts_ns = static_cast<std::int64_t>(d) * day + k;
            x.predictor = 0.1 * k;
            x.label = -0.1 * k;  // 反向
            s.push_back(x);
        }
    }
    const auto rep = evaluate_walk_forward_ic(s, SmallCfg(), 0, 40 * day);
    EXPECT_NEAR(rep.mean_ic, -1.0, 1e-6) << "反向预测 → IC≈−1";
}

// WFIC03: 空 / 样本不足 → valid_folds=0, mean_ic NaN (不崩)
TEST(WalkForwardIC, WFIC03_Empty) {
    const auto rep = evaluate_walk_forward_ic({}, SmallCfg(), 0, 40 * WalkForwardConfig::kDayNs);
    EXPECT_EQ(rep.valid_folds, static_cast<std::size_t>(0));
    EXPECT_TRUE(std::isnan(rep.mean_ic));
    EXPECT_EQ(rep.total_oos, static_cast<std::size_t>(0));
}
