// test_mid_return_label.cpp — 双时钟多窗 mid 收益标签管道单测 (PIT 禁外推 / 墙钟 prevailing / 事件钟)。
#include <gtest/gtest.h>

#include <cmath>

#include "stcpp/ml/mid_return_label.hpp"

using stcpp::ml::ComputeMidReturnLabel;
using stcpp::ml::MidPoint;

namespace {
constexpr std::int64_t S = 1'000'000'000LL;  // 1s ns
// tape: ts {0,1,2,3,5,10}s, mid {0.50,0.51,0.52,0.55,0.54,0.56}
std::vector<MidPoint> MakeTape() {
    return {{0, 0.50}, {1 * S, 0.51}, {2 * S, 0.52}, {3 * S, 0.55}, {5 * S, 0.54}, {10 * S, 0.56}};
}
}  // namespace

TEST(MidReturnLabel, WallClockPrevailing) {
    const auto tape = MakeTape();
    const auto lab = ComputeMidReturnLabel(tape, 0);  // 样本 t=0, mid=0.50
    ASSERT_TRUE(lab.valid);
    // kWallHorizonsSec = {2,3,5,10,12,15,30,60}
    EXPECT_NEAR(lab.y_wall[0], 0.02, 1e-9) << "Δ2s: prevailing(2s)=0.52 − 0.50";
    EXPECT_NEAR(lab.y_wall[1], 0.05, 1e-9) << "Δ3s: 0.55 − 0.50";
    EXPECT_NEAR(lab.y_wall[2], 0.04, 1e-9) << "Δ5s: 0.54 − 0.50";
    EXPECT_NEAR(lab.y_wall[3], 0.06, 1e-9) << "Δ10s: 0.56 − 0.50";
}

TEST(MidReturnLabel, NoFutureCoverageIsNaN_NotExtrapolated) {
    const auto tape = MakeTape();  // last point = 10s
    const auto lab = ComputeMidReturnLabel(tape, 0);
    // Δ12s/15s/30s/60s 都 > last(10s) → 无未来数据 → NaN (PIT: 禁外推)。
    EXPECT_TRUE(std::isnan(lab.y_wall[4])) << "Δ12s 超出 tape → NaN";
    EXPECT_TRUE(std::isnan(lab.y_wall[5]));
    EXPECT_TRUE(std::isnan(lab.y_wall[6]));
    EXPECT_TRUE(std::isnan(lab.y_wall[7]));
}

TEST(MidReturnLabel, EventClock) {
    const auto tape = MakeTape();
    const auto lab = ComputeMidReturnLabel(tape, 0);  // t=0
    // kEventHorizons = {1,2,5,10,20,50}
    EXPECT_NEAR(lab.y_event[0], 0.01, 1e-9) << "第1事件 tape[1]=0.51 − 0.50";
    EXPECT_NEAR(lab.y_event[1], 0.02, 1e-9) << "第2事件 tape[2]=0.52";
    EXPECT_NEAR(lab.y_event[2], 0.06, 1e-9) << "第5事件 tape[5]=0.56";
    EXPECT_TRUE(std::isnan(lab.y_event[3])) << "第10事件: 0+10≥size(6) → 事件不足 → NaN";
}

TEST(MidReturnLabel, LastSampleAllNaN) {
    const auto tape = MakeTape();
    const auto lab = ComputeMidReturnLabel(tape, 5);  // 最后一点 t=10s, 无任何未来
    EXPECT_FALSE(lab.valid) << "尾部样本无未来覆盖 → 不可用";
    for (double v : lab.y_wall) EXPECT_TRUE(std::isnan(v));
    for (double v : lab.y_event) EXPECT_TRUE(std::isnan(v));
}

TEST(MidReturnLabel, ExtractNumField) {
    using stcpp::ml::ExtractNumField;
    const std::string line = R"({"condition_id":"0xab","as_of_ts_ns":1700000000000,"f8":0.523,"f9":null})";
    EXPECT_EQ(ExtractNumField(line, "as_of_ts_ns").value(), 1700000000000.0);
    EXPECT_NEAR(ExtractNumField(line, "f8").value(), 0.523, 1e-9);
    EXPECT_FALSE(ExtractNumField(line, "f9").has_value()) << "null → nullopt";
    EXPECT_FALSE(ExtractNumField(line, "f99").has_value()) << "缺失 → nullopt";
}

TEST(MidReturnLabel, AppendMidLabelsNaNToNull) {
    const auto tape = MakeTape();
    const auto lab = ComputeMidReturnLabel(tape, 0);
    const std::string out = stcpp::ml::AppendMidLabels(R"({"condition_id":"0xab","f8":0.5})", lab);
    EXPECT_NE(out.find("\"y_wall_2s\":0.02"), std::string::npos);
    EXPECT_NE(out.find("\"y_wall_12s\":null"), std::string::npos) << "NaN→null (合法 JSON)";
    EXPECT_NE(out.find("\"label_valid\":1"), std::string::npos);
    EXPECT_EQ(out.back(), '}');
}
