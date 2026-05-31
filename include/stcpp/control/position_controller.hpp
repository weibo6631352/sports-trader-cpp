// include/stcpp/control/position_controller.hpp — 目标仓位控制器 (纯函数核, header-only)
//
// Owner: 老雷 (GM) — 实施 docs/RESEARCH/laolei-target-position-controller-spec-v1.md
// last_review: 2026-05-31
//
// 范式 (老板 2026-05-31): 没有进/出场/止损概念; 模型输出目标仓位 + 价格界限;
//   控制器连续把当前仓位调到目标 (order = 目标 − 现仓), 限价执行绝不追价。
//
// 主权评审 (2026-05-31):
//   老周 (架构): 抽独立纯函数, 回测/实盘共用 (BR-1); 限价 = 控制器前置门 (撮合器 ABI 零改)。
//   小梁 (Kelly): reservation 公式 + min_rebalance 死区; Kelly notional 当目标仓位理论成立。
//   老韩 (RM): v1 只做多侧 (空头 clamp 0); 减仓单不绕 RM; signed cap magnitude 修复同批落 (H-1)。
//
// v1 范围 (三主权签字): 被选边 买增 + 卖减 + 限价不追。不反向、不开空 (→ M2)。
// 纯函数纪律 (老周): 无 IO / 无锁 / 无 now(); 数据由调用方 (TickOne) 取好传入。R-12 不触碰。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "stcpp/strategy/signal_iface.hpp"  // strategy::Side

namespace stcpp::control {

// 控制器为什么不动 (act=false 时的诊断; audit/stat 用)
enum class NoActReason : std::uint8_t {
    None = 0,        // act=true
    BelowThreshold,  // |gap| < min_rebalance 死区 (防抖, 小梁 Q-梁-2)
    NotMarketable,   // 限价不可成交 (best 价劣于 reservation; 不追价, 老周 Q-周-1)
    ZeroGap,         // 目标 == 现仓
    InvalidInput,    // 非有限输入 (fail-closed)
};

// 控制器输入 (调用方在 TickOne 组装; 全 pUSD 同量纲, 被选边视角)。
struct ControlInput {
    double target_pusd{0.0};          // 目标仓位 (被选边 long; v1 ≥0, 空头由 allow_short 控制)
    double current_pusd{0.0};         // 当前持仓 (被选边 token 现有 long; v1 ≥0)
    double reservation_buy_px{0.0};   // 买入保留价上界 (best_ask ≤ 它才买; 小梁公式)
    double reservation_sell_px{1.0};  // 卖出保留价下界 (best_bid ≥ 它才卖)
    double best_ask{0.0};             // 被选边市场 best ask
    double best_bid{0.0};             // 被选边市场 best bid
    double min_rebalance_pusd{1.0};   // 防抖死区 (绝对 pUSD; 小梁 = max(1, 0.1·|target|))
    double per_order_cap_pusd{0.0};   // 单笔上限 (clamp; RM per_order_cap 同源)
    bool allow_short{false};          // v1=false (空头 clamp 0); M2 开
};

// 控制器输出 (TickOne 据此构造 OrderIntent 或 skip)。
struct ControlAction {
    bool act{false};                           // false = 本 tick 不动
    strategy::Side side{strategy::Side::Buy};  // act 时的方向
    double size_pusd{0.0};                     // 下单量 (≥0; 已 clamp cap + 不超持仓)
    double limit_price{0.0};                   // 限价 = reservation (绝不 market)
    bool is_close{false};                      // 减仓/平仓 (降敞口; RM cap 放行依据)
    NoActReason reason{NoActReason::None};
};

// Decide — 纯函数: 目标仓位 + 限价 → 控制动作。无副作用。
//   gap = target − current; 死区内不动; 限价不可成交不动; 否则 买增(gap>0)/卖减(gap<0)。
//   v1: 空头 clamp 0 (老韩 H); 卖不超过持仓 (不开空); 减仓 is_close=true (RM cap 放行)。
[[nodiscard]] inline ControlAction Decide(const ControlInput& in) noexcept {
    ControlAction a;

    // fail-closed: 非有限输入 → 不动
    if (!std::isfinite(in.target_pusd) || !std::isfinite(in.current_pusd) || !std::isfinite(in.best_ask) ||
        !std::isfinite(in.best_bid)) {
        a.reason = NoActReason::InvalidInput;
        return a;
    }

    // v1 安全: 空头目标 clamp 0 (老韩 H-1/H-2; M2 才开空)。
    double target = in.target_pusd;
    if (!in.allow_short && target < 0.0) {
        target = 0.0;
    }

    const double current = in.current_pusd;
    const double gap = target - current;
    const double abs_gap = std::abs(gap);

    // 防抖死区 (小梁 Q-梁-2): |gap| 太小不动 (避免高频小额 rebalance 被 fee 侵蚀)。
    const double threshold = (in.min_rebalance_pusd > 0.0) ? in.min_rebalance_pusd : 0.0;
    if (abs_gap < threshold) {
        a.reason = NoActReason::BelowThreshold;
        return a;
    }

    const double cap = (in.per_order_cap_pusd > 0.0) ? in.per_order_cap_pusd : abs_gap;

    if (gap > 0.0) {
        // 买增: 限价不追 — best_ask 必须 ≤ 买入保留价 (老周 Q-周-1 前置门)。
        if (!(in.best_ask > 0.0 && in.best_ask <= in.reservation_buy_px)) {
            a.reason = NoActReason::NotMarketable;
            return a;
        }
        a.act = true;
        a.side = strategy::Side::Buy;
        a.size_pusd = std::min(abs_gap, cap);
        a.limit_price = in.reservation_buy_px;
        a.is_close = false;
    } else {
        // 卖减: 限价不追 — best_bid 必须 ≥ 卖出保留价。
        if (!(in.best_bid > 0.0 && in.best_bid >= in.reservation_sell_px)) {
            a.reason = NoActReason::NotMarketable;
            return a;
        }
        // v1 不开空: 卖出量不超过当前持仓 (current ≥0)。
        double sell_sz = std::min(abs_gap, cap);
        if (!in.allow_short) {
            sell_sz = std::min(sell_sz, std::max(0.0, current));
        }
        if (sell_sz <= 0.0) {
            a.reason = NoActReason::ZeroGap;
            return a;
        }
        a.act = true;
        a.side = strategy::Side::Sell;
        a.size_pusd = sell_sz;
        a.limit_price = in.reservation_sell_px;
        a.is_close = true;  // 减仓 (降敞口 → RM cap 放行)
    }

    if (a.size_pusd <= 0.0) {
        a.act = false;
        a.reason = NoActReason::ZeroGap;
    }
    return a;
}

}  // namespace stcpp::control
