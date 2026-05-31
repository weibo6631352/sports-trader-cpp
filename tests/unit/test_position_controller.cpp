// tests/unit/test_position_controller.cpp — 目标仓位控制器纯函数穷举单测
//
// Owner: 老雷 (GM) — spec docs/RESEARCH/laolei-target-position-controller-spec-v1.md §11 步骤4
// 覆盖: 买增 / 卖减 / 死区防抖 / 限价不追 / 空头clamp / 不超持仓 / cap / 零gap / 非法输入

#include <limits>

#include <gtest/gtest.h>

#include "stcpp/control/position_controller.hpp"

using namespace stcpp::control;
using stcpp::strategy::Side;

namespace {

// 默认: fair 0.55, reservation_buy 0.53 (买≤它), reservation_sell 0.57 (卖≥它),
//   market best_ask 0.52 / best_bid 0.52 (买可成交; 卖需 bid≥0.57 才成交)。
ControlInput base() {
    ControlInput in;
    in.target_pusd = 0.0;
    in.current_pusd = 0.0;
    in.reservation_buy_px = 0.53;
    in.reservation_sell_px = 0.57;
    in.best_ask = 0.52;
    in.best_bid = 0.52;
    in.min_rebalance_pusd = 1.0;
    in.per_order_cap_pusd = 100.0;
    in.allow_short = false;
    return in;
}

}  // namespace

// PC-01: 买增 — target>current, ask≤reservation_buy → 买, size=gap, 限价=reservation_buy
TEST(PositionController, PC01_BuyIncrease) {
    auto in = base();
    in.target_pusd = 30.0;
    in.current_pusd = 0.0;
    const auto a = Decide(in);
    EXPECT_TRUE(a.act);
    EXPECT_EQ(a.side, Side::Buy);
    EXPECT_DOUBLE_EQ(a.size_pusd, 30.0);
    EXPECT_DOUBLE_EQ(a.limit_price, 0.53);
    EXPECT_FALSE(a.is_close);
}

// PC-02: 买增加仓 — 已有 10, 目标 30 → 买 20 (gap)
TEST(PositionController, PC02_BuyAddToExisting) {
    auto in = base();
    in.target_pusd = 30.0;
    in.current_pusd = 10.0;
    const auto a = Decide(in);
    EXPECT_TRUE(a.act);
    EXPECT_EQ(a.side, Side::Buy);
    EXPECT_DOUBLE_EQ(a.size_pusd, 20.0);
}

// PC-03: 卖减 — 已有 30, 目标 10, bid≥reservation_sell → 卖 20, is_close
TEST(PositionController, PC03_SellReduce) {
    auto in = base();
    in.target_pusd = 10.0;
    in.current_pusd = 30.0;
    in.best_bid = 0.58;  // ≥ reservation_sell 0.57 → 可成交
    const auto a = Decide(in);
    EXPECT_TRUE(a.act);
    EXPECT_EQ(a.side, Side::Sell);
    EXPECT_DOUBLE_EQ(a.size_pusd, 20.0);
    EXPECT_DOUBLE_EQ(a.limit_price, 0.57);
    EXPECT_TRUE(a.is_close);
}

// PC-04: 平仓 — 目标 0, 现仓 25 → 卖 25 全平
TEST(PositionController, PC04_SellFullClose) {
    auto in = base();
    in.target_pusd = 0.0;
    in.current_pusd = 25.0;
    in.best_bid = 0.60;
    const auto a = Decide(in);
    EXPECT_TRUE(a.act);
    EXPECT_EQ(a.side, Side::Sell);
    EXPECT_DOUBLE_EQ(a.size_pusd, 25.0);
    EXPECT_TRUE(a.is_close);
}

// PC-05: 死区防抖 — |gap| < min_rebalance → 不动
TEST(PositionController, PC05_BelowThreshold) {
    auto in = base();
    in.target_pusd = 10.5;
    in.current_pusd = 10.0;  // gap=0.5 < 1.0
    const auto a = Decide(in);
    EXPECT_FALSE(a.act);
    EXPECT_EQ(a.reason, NoActReason::BelowThreshold);
}

// PC-06: 限价不追 (买) — ask > reservation_buy → 不买 (价格涨了不硬追)
TEST(PositionController, PC06_BuyNotMarketable) {
    auto in = base();
    in.target_pusd = 30.0;
    in.best_ask = 0.55;  // > reservation_buy 0.53 → 价涨, 不追
    const auto a = Decide(in);
    EXPECT_FALSE(a.act);
    EXPECT_EQ(a.reason, NoActReason::NotMarketable);
}

// PC-07: 限价不追 (卖) — bid < reservation_sell → 不卖
TEST(PositionController, PC07_SellNotMarketable) {
    auto in = base();
    in.target_pusd = 0.0;
    in.current_pusd = 20.0;
    in.best_bid = 0.50;  // < reservation_sell 0.57 → 不贱卖
    const auto a = Decide(in);
    EXPECT_FALSE(a.act);
    EXPECT_EQ(a.reason, NoActReason::NotMarketable);
}

// PC-08: 空头 clamp — target<0, allow_short=false → target→0 → 不开空 (现仓0 → 零gap)
TEST(PositionController, PC08_ShortClampedNoPosition) {
    auto in = base();
    in.target_pusd = -30.0;  // 空头目标
    in.current_pusd = 0.0;
    in.best_bid = 0.60;
    const auto a = Decide(in);
    EXPECT_FALSE(a.act);  // target clamp 0, gap=0
}

// PC-09: 空头 clamp + 有多仓 — target<0 clamp 0, 现仓 20 → 卖 20 平 (不开空)
TEST(PositionController, PC09_ShortClampReducesToZero) {
    auto in = base();
    in.target_pusd = -30.0;  // clamp → 0
    in.current_pusd = 20.0;
    in.best_bid = 0.60;
    const auto a = Decide(in);
    EXPECT_TRUE(a.act);
    EXPECT_EQ(a.side, Side::Sell);
    EXPECT_DOUBLE_EQ(a.size_pusd, 20.0);  // 只平到 0, 不开空
    EXPECT_TRUE(a.is_close);
}

// PC-10: per_order_cap clamp — gap 巨大 → size clamp 到 cap
TEST(PositionController, PC10_CapClamp) {
    auto in = base();
    in.target_pusd = 500.0;
    in.current_pusd = 0.0;
    in.per_order_cap_pusd = 100.0;
    const auto a = Decide(in);
    EXPECT_TRUE(a.act);
    EXPECT_DOUBLE_EQ(a.size_pusd, 100.0);  // clamp
}

// PC-11: 零 gap — target==current → 不动
TEST(PositionController, PC11_ZeroGap) {
    auto in = base();
    in.target_pusd = 20.0;
    in.current_pusd = 20.0;
    const auto a = Decide(in);
    EXPECT_FALSE(a.act);
}

// PC-12: 非法输入 (NaN) → fail-closed 不动
TEST(PositionController, PC12_InvalidInput) {
    auto in = base();
    in.target_pusd = std::numeric_limits<double>::quiet_NaN();
    const auto a = Decide(in);
    EXPECT_FALSE(a.act);
    EXPECT_EQ(a.reason, NoActReason::InvalidInput);
}

// PC-13: 卖减不超过持仓 — 目标 0 现仓 20 但 gap 算出 -20, 卖量=min(20,持仓20)=20
//   (验证 no-short: 即使 |gap| > 持仓也不卖超)
TEST(PositionController, PC13_SellNotExceedPosition) {
    auto in = base();
    in.target_pusd = -50.0;  // clamp 0
    in.current_pusd = 20.0;
    in.best_bid = 0.60;
    const auto a = Decide(in);
    EXPECT_TRUE(a.act);
    EXPECT_DOUBLE_EQ(a.size_pusd, 20.0);  // 不超持仓
}

// PC-14: allow_short=true 时空头目标不 clamp (M2 行为预验; v1 不走)
TEST(PositionController, PC14_AllowShortOpensSell) {
    auto in = base();
    in.target_pusd = -30.0;
    in.current_pusd = 0.0;
    in.best_bid = 0.60;
    in.allow_short = true;  // M2
    const auto a = Decide(in);
    EXPECT_TRUE(a.act);
    EXPECT_EQ(a.side, Side::Sell);
    EXPECT_DOUBLE_EQ(a.size_pusd, 30.0);  // 开空 30 (无持仓约束)
}
