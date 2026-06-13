// tests/unit/test_position_controller.cpp — 目标仓位控制器纯函数穷举单测
//
// Owner: 老雷 (GM) — spec docs/RESEARCH/laolei-target-position-controller-spec-v1.md §11 步骤4
// 覆盖: 买增 / 卖减 / 死区防抖 / 限价不追 / 空头clamp / 不超持仓 / cap / 零gap / 非法输入

#include <cmath>
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

// PC-05b: 强制穿越 (小梁 Q-梁-2) — |gap| < 死区, 但 force_cross=true (fair 大跳) → 绕死区下单
TEST(PositionController, PC05b_ForceCrossBypassesDeadZone) {
    auto in = base();
    in.target_pusd = 10.5;
    in.current_pusd = 10.0;  // gap=0.5 < 1.0 死区
    in.force_cross = true;   // 进球/fair 大跳 → 强制穿越
    const auto a = Decide(in);
    EXPECT_TRUE(a.act) << "force_cross 应绕过死区";
    EXPECT_EQ(a.side, Side::Buy);
    EXPECT_DOUBLE_EQ(a.size_pusd, 0.5);  // 小额买增 (死区内但强制穿越)
    // force_cross 不绕限价门: ask 仍须 ≤ reservation_buy (只绕防抖, 不绕限价不追)
    auto in2 = in;
    in2.best_ask = 0.99;  // 远高于 reservation_buy 0.53
    const auto a2 = Decide(in2);
    EXPECT_FALSE(a2.act) << "force_cross 只绕死区, 不绕限价不追门";
    EXPECT_EQ(a2.reason, NoActReason::NotMarketable);
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

// PC-06b: 最小买单【凑整】(2026-06-13 老板「这不是门, 是凑够 5 股」) — Kelly < 5 股的钱 → 凑够下单 (非跳过)
TEST(PositionController, PC06b_MinOrderRoundsUpNotSkip) {
    auto in = base();
    in.target_pusd = 1.5;        // Kelly 只分到 $1.5 (> 死区 1.0, 过防抖)
    in.current_pusd = 0.0;       // gap=1.5
    in.min_order_pusd = 4.0;     // 5 股 × 0.80 = $4 (PM CLOB 下限)
    const auto a = Decide(in);
    EXPECT_TRUE(a.act) << "Kelly < 5 股不应跳过, 应凑整下单";
    EXPECT_EQ(a.side, Side::Buy);
    EXPECT_DOUBLE_EQ(a.size_pusd, 4.0) << "$1.5 凑够 5 股的 $4";
}

// PC-06c: cap 撑不起 5 股 → 不下废单 (M3 修, 2026-06-13): cap < min_order 时凑整也是 <5 股, CLOB 必拒,
//   故不下 (BelowThreshold), 而非下个注定被拒的单。(生产 per_order_cap $10 > 5股$5 不可达; 防隐雷。)
TEST(PositionController, PC06c_NarrowCapBelowMinShareSkips) {
    auto in = base();
    in.target_pusd = 1.5;
    in.current_pusd = 0.0;
    in.min_order_pusd = 4.0;
    in.per_order_cap_pusd = 3.0;  // cap < 5 股的钱 (极端窄 cap)
    const auto a = Decide(in);
    EXPECT_FALSE(a.act) << "cap 撑不起 5 股不应下废单";
    EXPECT_EQ(a.reason, NoActReason::BelowThreshold);
}

// PC-06d: 存量残差不凑整 (防过冲) — 已持仓 ≥5 股, 残差 gap < 5 股 → 跳过 (收敛在 5 股粒度, 不过冲 target)
TEST(PositionController, PC06d_HeldResidualBelowMinSkipsNoOvershoot) {
    auto in = base();
    in.target_pusd = 10.0;
    in.current_pusd = 9.0;       // 已持仓 9 (≥ 5 股的钱); 残差 gap=1.0
    in.min_order_pusd = 4.0;     // 5 股 × 0.80; 残差 1.0 < 4.0
    in.min_rebalance_pusd = 0.5; // 放低死区, 让残差越过死区直抵 min_order 判定
    const auto a = Decide(in);
    EXPECT_FALSE(a.act) << "存量仓残差 < 5 股不应凑整 (否则 9→13 过冲 target 10)";
    EXPECT_EQ(a.reason, NoActReason::BelowThreshold);
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

// PC-15: 预测驱动平仓 — target<current 且 bid 可成交 → 平仓 (即便 bid < reservation_sell「卖高」价)。
//   老板「双边预测给出的双边仓位管理」: 仓位随预测回 flat (收敛兑现), 不死等卖高 → 解「只买不卖持到结算」。
TEST(PositionController, PC15_PredictiveUnwindSellsAtBid) {
    auto in = base();
    in.target_pusd = 0.0;        // 预测说 flat (edge 收敛/没了)
    in.current_pusd = 20.0;      // 持 20
    in.best_bid = 0.55;          // bid < reservation_sell(0.57) → 默认语义卖不掉
    in.predictive_unwind = true;
    const auto a = Decide(in);
    ASSERT_TRUE(a.act);
    EXPECT_EQ(a.side, Side::Sell) << "预测说减 → 平仓, 不死等卖高";
    EXPECT_TRUE(a.is_close);
    EXPECT_DOUBLE_EQ(a.limit_price, 0.55) << "marketable at best_bid";
    EXPECT_DOUBLE_EQ(a.size_pusd, 20.0);  // 全平
}

// PC-16: 默认 (predictive_unwind=false) → 维持做市「卖高」语义 (bid<reservation_sell → 卖不掉, 契约不变)。
TEST(PositionController, PC16_DefaultKeepsSellHigh) {
    auto in = base();
    in.target_pusd = 0.0;
    in.current_pusd = 20.0;
    in.best_bid = 0.55;          // < reservation_sell(0.57)
    // predictive_unwind 默认 false
    const auto a = Decide(in);
    EXPECT_FALSE(a.act) << "默认: bid 未到卖高价 → 不卖 (NotMarketable)";
    EXPECT_EQ(a.reason, stcpp::control::NoActReason::NotMarketable);
}

// ===========================================================================
// ComputeReservation — reservation 公式纯函数 (小梁 Q-梁-1)
//   required_margin = max(margin_floor, z×sqrt(fair(1−fair)/n))
//   reservation_buy  = fair − fee×ask(1−ask) − margin;  sell 对称 (+fee×bid(1−bid) + margin)
// ===========================================================================

// CR-01: 正常路径 — buy < fair < sell, margin = CI 半宽 (> floor 时)
TEST(ReservationFormula, CR01_Symmetric) {
    ReservationInput in;
    in.fair = 0.50;
    in.exec_ask = 0.51;
    in.exec_bid = 0.49;
    in.fee_coef = 0.03;
    in.margin_floor = 0.0;
    in.z = 1.645;
    in.n_eff = 200;
    const auto r = ComputeReservation(in);
    const double sigma = std::sqrt(0.50 * 0.50 / 200.0);
    EXPECT_DOUBLE_EQ(r.required_margin, 1.645 * sigma);
    const double fee_buy = 0.03 * 0.51 * 0.49;
    const double fee_sell = 0.03 * 0.49 * 0.51;
    EXPECT_DOUBLE_EQ(r.buy_px, 0.50 - fee_buy - r.required_margin);
    EXPECT_DOUBLE_EQ(r.sell_px, 0.50 + fee_sell + r.required_margin);
    EXPECT_LT(r.buy_px, 0.50);   // 买保留价 < fair (净 edge>0 才买)
    EXPECT_GT(r.sell_px, 0.50);  // 卖保留价 > fair
}

// CR-02: margin_floor 主导 — floor 高于 CI 半宽时取 floor (小梁: edge_ci_lower_floor 同源)
TEST(ReservationFormula, CR02_FloorDominates) {
    ReservationInput in;
    in.fair = 0.50;
    in.exec_ask = 0.50;
    in.exec_bid = 0.50;
    in.fee_coef = 0.0;  // 隔离 fee, 只看 margin
    in.margin_floor = 0.10;
    in.z = 1.645;
    in.n_eff = 200;  // CI 半宽 ≈ 0.058 < 0.10 floor
    const auto r = ComputeReservation(in);
    EXPECT_DOUBLE_EQ(r.required_margin, 0.10);
    EXPECT_DOUBLE_EQ(r.buy_px, 0.40);
    EXPECT_DOUBLE_EQ(r.sell_px, 0.60);
}

// CR-03: fail-closed — 退化 fair → buy_px=0 (永不可买) + sell_px=1 (永不可卖)
TEST(ReservationFormula, CR03_FailClosed) {
    for (double bad : {0.0, 1.0, -0.1, 1.5, std::numeric_limits<double>::quiet_NaN()}) {
        ReservationInput in;
        in.fair = bad;
        const auto r = ComputeReservation(in);
        EXPECT_DOUBLE_EQ(r.buy_px, 0.0) << "fair=" << bad;
        EXPECT_DOUBLE_EQ(r.sell_px, 1.0) << "fair=" << bad;
    }
    // n_eff<=0 同样 fail-closed
    ReservationInput in;
    in.fair = 0.5;
    in.n_eff = 0;
    const auto r = ComputeReservation(in);
    EXPECT_DOUBLE_EQ(r.buy_px, 0.0);
    EXPECT_DOUBLE_EQ(r.sell_px, 1.0);
}

// CR-04: n 越大 margin 越小 → reservation 越宽 (买保留价升, 越易成交)
TEST(ReservationFormula, CR04_MarginShrinksWithN) {
    ReservationInput lo;
    lo.fair = 0.5;
    lo.exec_ask = 0.5;
    lo.exec_bid = 0.5;
    lo.fee_coef = 0.0;
    lo.n_eff = 50;
    ReservationInput hi = lo;
    hi.n_eff = 500;
    EXPECT_GT(ComputeReservation(hi).buy_px, ComputeReservation(lo).buy_px);
    EXPECT_LT(ComputeReservation(hi).required_margin, ComputeReservation(lo).required_margin);
}

// CR-05: noise_free 跳二项 z×σ → buy_px 跨过 ask (复现 0xd6e1fbe628 sharp 进不了可下单侧 + 修复)。
//   2026-06-04 老板「跑通赔率 edge 线」: sharp fair=0.783, ask=0.74, n_eff 小 (in-play 稀疏样本)。
//   未修 (noise_free=false): z×σ≈0.10 → buy_px≈0.68 < ask 0.74 → NotMarketable → 永不成交。
//   修后 (noise_free=true): 仅留 margin_floor(半 vig) → buy_px≈0.76 ≥ ask → marketable → 成交。
TEST(ReservationFormula, CR05_NoiseFreeCrossesAsk) {
    ReservationInput in;
    in.fair = 0.783;
    in.exec_ask = 0.74;
    in.exec_bid = 0.70;
    in.fee_coef = 0.03;
    in.margin_floor = 0.02;  // 半 vig (0.5×0.04 cross_spread)
    in.z = 1.645;
    in.n_eff = 12;  // in-play 稀疏 → 大 σ

    in.noise_free = false;
    const auto noisy = ComputeReservation(in);
    EXPECT_GT(noisy.required_margin, 0.05) << "二项 z×σ 应给出大 margin";
    EXPECT_LT(noisy.buy_px, in.exec_ask) << "未修: buy_px < ask → NotMarketable (sharp 进不了可下单侧)";

    in.noise_free = true;
    const auto clean = ComputeReservation(in);
    EXPECT_DOUBLE_EQ(clean.required_margin, 0.02) << "noise_free: 仅留 margin_floor (半 vig)";
    EXPECT_GE(clean.buy_px, in.exec_ask) << "修后: buy_px ≥ ask → marketable → sharp 信号成交";
    EXPECT_LT(clean.buy_px, in.fair) << "仍 < fair (净 edge>0 才买, 不亏)";
}

// ---- edge-生命周期乘子 (持仓管理 Stage 2) ----
// 守架构界线: 恒 ∈[floor,1], 绝不 >1 (不放大过 Kelly), 不碰符号 (方向归 sharp)。

static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
static LifecycleConfig lc_cfg_default() {
    return LifecycleConfig{true, 0.02, 0.5, 0.01, 0.5, 0.3, 3};
}

TEST(LifecycleMultiplier, LC01_InsufficientSamples_FailOpen) {
    auto cfg = lc_cfg_default();
    // 样本 < min_samples → 1.0 (fail-open, 不改基线)
    EXPECT_DOUBLE_EQ(ComputeLifecycleMultiplier({0.05, 0.005, 2}, cfg), 1.0);
}

TEST(LifecycleMultiplier, LC02_NaN_FailOpen) {
    auto cfg = lc_cfg_default();
    EXPECT_DOUBLE_EQ(ComputeLifecycleMultiplier({kNaN, 0.0, 10}, cfg), 1.0);
    EXPECT_DOUBLE_EQ(ComputeLifecycleMultiplier({0.01, kNaN, 10}, cfg), 1.0);
}

TEST(LifecycleMultiplier, LC03_Disabled_ReturnsOne) {
    auto cfg = lc_cfg_default();
    cfg.enabled = false;
    EXPECT_DOUBLE_EQ(ComputeLifecycleMultiplier({0.10, 0.05, 10}, cfg), 1.0);
}

TEST(LifecycleMultiplier, LC04_Calm_Converging_FullSize) {
    auto cfg = lc_cfg_default();
    // 低 vol (0) + 收敛 (conv<0) → 不缩 → 1.0
    EXPECT_DOUBLE_EQ(ComputeLifecycleMultiplier({0.0, -0.005, 10}, cfg), 1.0);
}

TEST(LifecycleMultiplier, LC05_HighVol_Reduces) {
    auto cfg = lc_cfg_default();
    // vol = vol_ref(0.02) → m_stab = 1 − 0.5×1 = 0.5; 收敛 → m_regime=1 → m=0.5
    EXPECT_NEAR(ComputeLifecycleMultiplier({0.02, -0.001, 10}, cfg), 0.5, 1e-9);
}

TEST(LifecycleMultiplier, LC06_Diverging_Reduces) {
    auto cfg = lc_cfg_default();
    // 低 vol(0, m_stab=1) + 发散 conv=div_ref(0.01) → m_regime = 1 − 0.5×1 = 0.5 → m=0.5
    EXPECT_NEAR(ComputeLifecycleMultiplier({0.0, 0.01, 10}, cfg), 0.5, 1e-9);
}

TEST(LifecycleMultiplier, LC07_NeverBelowFloor) {
    auto cfg = lc_cfg_default();
    // 极端 vol + 极端发散 → m_stab/m_regime 都被 clamp 到 floor; 乘积仍夹到 floor
    const double m = ComputeLifecycleMultiplier({1.0, 1.0, 10}, cfg);
    EXPECT_GE(m, cfg.floor);
    EXPECT_LE(m, 1.0);
}

TEST(LifecycleMultiplier, LC08_NeverAmplifies) {
    auto cfg = lc_cfg_default();
    // 任意输入恒 ≤ 1.0 (绝不放大过 Kelly 上界, 守架构界线)
    for (double v : {0.0, 0.001, 0.05, 0.5}) {
        for (double c : {-0.5, -0.001, 0.0, 0.001, 0.5}) {
            EXPECT_LE(ComputeLifecycleMultiplier({v, c, 10}, cfg), 1.0);
        }
    }
}

// ---- CLV sizing 乘子 (持仓管理 Stage 2, 老板「CLV 好就实时放大」) ----
// 可 >1 放大 (老板授权), 封顶 max_mult; 负 CLV 收缩到 floor; 样本不足 fail-open。
static ClvSizingConfig clv_cfg_default() {
    return ClvSizingConfig{true, 0.01, 0.5, 1.0, 1.5, 0.3, 20};
}

TEST(ClvMultiplier, CV01_InsufficientSamples_FailOpen) {
    auto cfg = clv_cfg_default();
    EXPECT_DOUBLE_EQ(ComputeClvMultiplier(0.02, 19, cfg), 1.0);  // n<min_samples
}

TEST(ClvMultiplier, CV02_NaN_FailOpen) {
    auto cfg = clv_cfg_default();
    EXPECT_DOUBLE_EQ(ComputeClvMultiplier(kNaN, 50, cfg), 1.0);
}

TEST(ClvMultiplier, CV03_Disabled_ReturnsOne) {
    auto cfg = clv_cfg_default();
    cfg.enabled = false;
    EXPECT_DOUBLE_EQ(ComputeClvMultiplier(0.05, 50, cfg), 1.0);
}

TEST(ClvMultiplier, CV04_ZeroClv_NoChange) {
    auto cfg = clv_cfg_default();
    EXPECT_DOUBLE_EQ(ComputeClvMultiplier(0.0, 50, cfg), 1.0);
}

TEST(ClvMultiplier, CV05_PositiveClv_Amplifies) {
    auto cfg = clv_cfg_default();
    // clv_mean = clv_ref(0.01) → m = 1 + 0.5×1 = 1.5 (= max_mult)
    EXPECT_NEAR(ComputeClvMultiplier(0.01, 50, cfg), 1.5, 1e-9);
    // clv_mean = 0.5×clv_ref → m = 1.25
    EXPECT_NEAR(ComputeClvMultiplier(0.005, 50, cfg), 1.25, 1e-9);
}

TEST(ClvMultiplier, CV06_AmplifyCappedAtMax) {
    auto cfg = clv_cfg_default();
    // 极大正 CLV → 封顶 max_mult(1.5), 绝不超 (护栏)
    EXPECT_DOUBLE_EQ(ComputeClvMultiplier(1.0, 50, cfg), 1.5);
}

TEST(ClvMultiplier, CV07_NegativeClv_Reduces) {
    auto cfg = clv_cfg_default();
    // clv_mean = −0.005, k_cut=1.0 → m = 1 − 1.0×0.5 = 0.5
    EXPECT_NEAR(ComputeClvMultiplier(-0.005, 50, cfg), 0.5, 1e-9);
}

TEST(ClvMultiplier, CV08_ReduceFlooredAtFloor) {
    auto cfg = clv_cfg_default();
    // 极大负 CLV → 夹到 floor(0.3), 绝不更低
    EXPECT_DOUBLE_EQ(ComputeClvMultiplier(-1.0, 50, cfg), 0.3);
}

TEST(ClvMultiplier, CV09_AlwaysInBounds) {
    auto cfg = clv_cfg_default();
    for (double m : {-1.0, -0.01, -0.001, 0.0, 0.001, 0.01, 1.0}) {
        const double r = ComputeClvMultiplier(m, 50, cfg);
        EXPECT_GE(r, cfg.floor);
        EXPECT_LE(r, cfg.max_mult);
    }
}

// ---- DD→target 乘子 (持仓管理 Stage 2, 老板「回撤大只停加仓 + hysteresis」) ----
static DrawdownConfig dd_cfg_default() {
    return DrawdownConfig{true, 0.05, 0.10, 0.15, 0.5, 0.25, 0.02};
}

TEST(DrawdownMultiplier, DD01_Disabled_ReturnsOne) {
    auto cfg = dd_cfg_default();
    cfg.enabled = false;
    EXPECT_DOUBLE_EQ(DrawdownTierMultiplier(0.20, cfg), 1.0);
}

TEST(DrawdownMultiplier, DD02_Tiers) {
    auto cfg = dd_cfg_default();
    EXPECT_DOUBLE_EQ(DrawdownTierMultiplier(0.00, cfg), 1.0);   // 无回撤
    EXPECT_DOUBLE_EQ(DrawdownTierMultiplier(0.049, cfg), 1.0);  // <t1
    EXPECT_DOUBLE_EQ(DrawdownTierMultiplier(0.05, cfg), 0.5);   // t1
    EXPECT_DOUBLE_EQ(DrawdownTierMultiplier(0.099, cfg), 0.5);  // <t2
    EXPECT_DOUBLE_EQ(DrawdownTierMultiplier(0.10, cfg), 0.25);  // t2
    EXPECT_DOUBLE_EQ(DrawdownTierMultiplier(0.149, cfg), 0.25); // <halt
    EXPECT_DOUBLE_EQ(DrawdownTierMultiplier(0.15, cfg), 0.0);   // halt → 0 (只持不加)
    EXPECT_DOUBLE_EQ(DrawdownTierMultiplier(0.30, cfg), 0.0);
}

// hysteresis: 模拟 trading_loop 成员逻辑 (drop 用 dd, restore 用 dd+band)。
TEST(DrawdownMultiplier, DD03_Hysteresis_DropImmediate_RestoreSticky) {
    auto cfg = dd_cfg_default();
    double m = 1.0;
    auto update = [&](double dd) {
        const double drop = DrawdownTierMultiplier(dd, cfg);
        const double restore = DrawdownTierMultiplier(dd + cfg.hysteresis_band, cfg);
        if (drop < m) m = drop;
        else if (restore > m) m = restore;
    };
    update(0.06);  // 进 t1 → 立即降到 0.5
    EXPECT_DOUBLE_EQ(m, 0.5);
    update(0.04);  // 回落到 0.04: restore=tier(0.06)=0.5 → 不恢复 (黏滞, 需 <0.03)
    EXPECT_DOUBLE_EQ(m, 0.5);
    update(0.02);  // 回落到 0.02: restore=tier(0.04)=1.0 → 恢复满仓
    EXPECT_DOUBLE_EQ(m, 1.0);
}

TEST(DrawdownMultiplier, DD04_DeepDrop_HoldOnly) {
    auto cfg = dd_cfg_default();
    double m = 1.0;
    const double drop = DrawdownTierMultiplier(0.16, cfg);  // ≥halt
    if (drop < m) m = drop;
    EXPECT_DOUBLE_EQ(m, 0.0);  // m=0 → 只持不加 (砍仓交保命门)
}

// ============================================================================
//   全部默认 OFF → 行为等于现状; 开启则更被动 (买压低/卖抬高) / 死区放宽。BR-1 纯函数。
// ============================================================================





// DZ-01: fee_k=0 (默认) → 死区 == max(floor, pct×|target|) 逐位不变 (向后兼容)。



// ============================================================================
// ============================================================================

// ============================================================================
// 相关性折扣乘子 (§4.1 规模层) — ComputeCorrelationMultiplier。∈[floor,1], gross 加权, fail-open。
// ============================================================================
static CorrelationConfig corr_cfg_default() { return CorrelationConfig{true, 0.50, 0.30, 0.70}; }

TEST(CorrelationMultiplier, CM01_DisabledReturnsOne) {
    CorrelationConfig cfg = corr_cfg_default();
    cfg.enabled = false;
    EXPECT_DOUBLE_EQ(ComputeCorrelationMultiplier({500.0, 9000.0, 10000.0, 0.95}, cfg), 1.0);
}
TEST(CorrelationMultiplier, CM02_FirstMarketNoDiscount) {
    auto cfg = corr_cfg_default();
    // existing_event_gross=0 (赛事第一个盘) → u=0 → 不削
    EXPECT_DOUBLE_EQ(ComputeCorrelationMultiplier({500.0, 0.0, 10000.0, 0.95}, cfg), 1.0);
}
TEST(CorrelationMultiplier, CM03_BelowTaperStart) {
    auto cfg = corr_cfg_default();
    // ρ=0.3, gross=5000, cap=10000 → u=0.3×0.5=0.15 < taper_start 0.5 → 不削
    EXPECT_DOUBLE_EQ(ComputeCorrelationMultiplier({500.0, 5000.0, 10000.0, 0.30}, cfg), 1.0);
}
TEST(CorrelationMultiplier, CM04_CapFullHitsFloor) {
    auto cfg = corr_cfg_default();
    // ρ=1, gross=cap → u=1.0 → t=1 → m=floor
    EXPECT_DOUBLE_EQ(ComputeCorrelationMultiplier({500.0, 10000.0, 10000.0, 1.0}, cfg), cfg.floor);
}
TEST(CorrelationMultiplier, CM05_MonotoneInOccupancy) {
    auto cfg = corr_cfg_default();
    // 占用率越高 → 乘子越小 (单调)
    const double m_lo = ComputeCorrelationMultiplier({500.0, 6000.0, 10000.0, 1.0}, cfg);  // u=0.6
    const double m_hi = ComputeCorrelationMultiplier({500.0, 9000.0, 10000.0, 1.0}, cfg);  // u=0.9
    EXPECT_GT(m_lo, m_hi);
    EXPECT_LE(m_hi, 1.0);
    EXPECT_GE(m_hi, cfg.floor);
}
TEST(CorrelationMultiplier, CM06_LowerRhoLessDiscount) {
    auto cfg = corr_cfg_default();
    const double m_hi_rho = ComputeCorrelationMultiplier({500.0, 8000.0, 10000.0, 0.95}, cfg);
    const double m_lo_rho = ComputeCorrelationMultiplier({500.0, 8000.0, 10000.0, 0.40}, cfg);
    EXPECT_LT(m_hi_rho, m_lo_rho) << "高 ρ (强相关) 削更多";
}
TEST(CorrelationMultiplier, CM07_FailOpenDegenerate) {
    auto cfg = corr_cfg_default();
    EXPECT_DOUBLE_EQ(ComputeCorrelationMultiplier({0.0, 9000.0, 10000.0, 0.95}, cfg), 1.0) << "本笔无新增";
    EXPECT_DOUBLE_EQ(ComputeCorrelationMultiplier({500.0, 9000.0, 0.0, 0.95}, cfg), 1.0) << "cap=0 禁用";
    EXPECT_DOUBLE_EQ(ComputeCorrelationMultiplier({500.0, kNaN, 10000.0, 0.95}, cfg), 1.0) << "NaN gross";
}
TEST(CorrelationMultiplier, CM08_NoSelfExcitation_ProspectiveSizeIrrelevant) {
    auto cfg = corr_cfg_default();
    // 防自激核心: prospective_notional 只判 >0, 不入公式量级 → 本笔大小不改乘子 (∂m/∂target_self=0)。
    const double m_small = ComputeCorrelationMultiplier({10.0, 8000.0, 10000.0, 0.95}, cfg);
    const double m_large = ComputeCorrelationMultiplier({9999.0, 8000.0, 10000.0, 0.95}, cfg);
    EXPECT_DOUBLE_EQ(m_small, m_large) << "本笔 target 大小不反馈回本盘乘子 (环切断)";
}
TEST(CorrelationMultiplier, CM09_NaNRhoUsesDefault) {
    auto cfg = corr_cfg_default();  // rho_default=0.70
    const double m_nan = ComputeCorrelationMultiplier({500.0, 8000.0, 10000.0, kNaN}, cfg);
    const double m_explicit = ComputeCorrelationMultiplier({500.0, 8000.0, 10000.0, 0.70}, cfg);
    EXPECT_DOUBLE_EQ(m_nan, m_explicit) << "ρ NaN → rho_default";
}
