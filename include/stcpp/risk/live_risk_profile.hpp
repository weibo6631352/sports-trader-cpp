// include/stcpp/risk/live_risk_profile.hpp — 实盘微注影子期 RM 档 (2026-06-12 实盘准备 v1)
//
// owner: 老雷 (GM) | last_review: 2026-06-12
// 状态: live 装配取用 (开闸授权 = 老板一句话, 2026-06-13 会签规则废除)。
// 2026-06-13 老板「不要乱加封控」: 日损/连亏额外熔断已删, 与 paper 同构only注码等比。
#pragma once

#include "stcpp/risk/risk_gateway.hpp"

namespace stcpp::risk {

// 实盘档 (2026-06-13 #4 注码定稿, 老板批: caps 按 $154 真本金等比收口 + paper 镜像同款 = live 精确彩排):
//   per_order $10(6.5%) / outcome $12(8%) / market $15(10%=单场保命线) / event $20(13%)。
//   Kelly 自缩(分母=实时净值)是主 sizing, cap 只防极端单笔; min_order $5 不动 (老板「体育 min 5 单」)。
//   体感: 典型仓 $5-10, 最坏单场 −10%, 连崩 4 场 ≈ −17% (对照旧 caps 可达 −52%)。
//   不加 paper 没有的额外熔断 (日损/连亏 halt 全删 — 老板「不要乱加封控」; 出问题人来停)。
[[nodiscard]] inline RiskConfig LiveRiskProfile() noexcept {
    RiskConfig c;
    c.per_order_cap_usdc = domain::MicroPUSD::from_pusd(10.0);
    c.per_outcome_cap_usdc = domain::MicroPUSD::from_pusd(12.0);
    c.market_exposure_cap_usdc = domain::MicroPUSD::from_pusd(15.0);
    c.event_exposure_cap_usdc = domain::MicroPUSD::from_pusd(20.0);
    c.bankroll_usdc = domain::MicroPUSD::from_pusd(150.0);  // 兜底; live 启动链上实读替代, paper 直接用
    c.edge_ci_lower_floor = 0.0;
    return c;
}

// (旧名兼容: 装配处若仍引用 ShadowProfile)
[[nodiscard]] inline RiskConfig LiveShadowRiskProfile() noexcept { return LiveRiskProfile(); }

}  // namespace stcpp::risk
