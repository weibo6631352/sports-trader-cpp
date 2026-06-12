// include/stcpp/risk/live_risk_profile.hpp — 实盘微注影子期 RM 档 (2026-06-12 实盘准备 v1)
//
// owner: 老雷 (GM) | last_review: 2026-06-12
// 状态: live 装配取用 (开闸授权 = 老板一句话, 2026-06-13 会签规则废除)。
// 2026-06-13 老板「不要乱加封控」: 日损/连亏额外熔断已删, 与 paper 同构only注码等比。
#pragma once

#include "stcpp/risk/risk_gateway.hpp"

namespace stcpp::risk {

// 实盘 100u 档 (2026-06-13 老板: 入金 100u +「不要乱加封控, 2u一单不现实 — CLOB 最小买 5 股」):
//   注码与 paper 同构按本金等比 (paper 25u/1000 → live 按可成交性取 10u/100); 不加 paper 没有的
//   额外熔断 (日损/连亏 halt 全删 — 老板「不要乱加封控」; 出问题人来停)。
[[nodiscard]] inline RiskConfig LiveRiskProfile() noexcept {
    RiskConfig c;
    c.per_order_cap_usdc = domain::MicroPUSD::from_pusd(25.0);       // = paper 同款; Kelly 主导, cap 仅防极端单笔
    c.per_outcome_cap_usdc = domain::MicroPUSD::from_pusd(40.0);
    c.market_exposure_cap_usdc = domain::MicroPUSD::from_pusd(50.0);
    c.event_exposure_cap_usdc = domain::MicroPUSD::from_pusd(60.0);  // 同事件 (paper 同绝对值)
    c.bankroll_usdc = domain::MicroPUSD::from_pusd(150.0);          // = 实际钱包; Kelly 用真资金跑
    c.edge_ci_lower_floor = 0.0;
    return c;
}

// (旧名兼容: 装配处若仍引用 ShadowProfile)
[[nodiscard]] inline RiskConfig LiveShadowRiskProfile() noexcept { return LiveRiskProfile(); }

}  // namespace stcpp::risk
