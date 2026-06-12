// include/stcpp/risk/live_risk_profile.hpp — 实盘微注影子期 RM 档 (2026-06-12 实盘准备 v1)
//
// owner: 老雷 (GM) | last_review: 2026-06-12
// 状态: live 装配取用 (开闸授权 = 老板一句话, 2026-06-13 会签规则废除)。
// 红线: paper 期关闭的日损熔断/连亏 halt 在此档全部重开且更紧 (docs/RESEARCH/live-readiness-plan-v1.md §2/§4)。
#pragma once

#include "stcpp/risk/risk_gateway.hpp"

namespace stcpp::risk {

// 实盘初始档 (2026-06-12 老板「不需要影子系统」: 开闸即实注; 数值开闸时按老板注码改, 结构即保命门重开)。
[[nodiscard]] inline RiskConfig LiveShadowRiskProfile() noexcept {  // TODO 开闸时更名 LiveRiskProfile + 注码按老板
    RiskConfig c;
    c.per_order_cap_usdc = domain::MicroPUSD::from_pusd(2.0);        // $2/单 (CLOB min $1)
    c.per_outcome_cap_usdc = domain::MicroPUSD::from_pusd(4.0);
    c.market_exposure_cap_usdc = domain::MicroPUSD::from_pusd(6.0);
    c.event_exposure_cap_usdc = domain::MicroPUSD::from_pusd(10.0);
    c.bankroll_usdc = domain::MicroPUSD::from_pusd(50.0);            // 总敞口 $50
    c.daily_loss_soft_pct = 10.0;                                    // −$5 软 (拒新仓)
    c.daily_loss_hard_pct = 20.0;                                    // −$10 硬熔断 (paper 期关闭, live 重开)
    c.daily_loss_halt_usdc = domain::MicroPUSD::from_pusd(10.0);
    c.consec_loss_halt_count = 5;                                    // 连亏 5 halt
    c.edge_ci_lower_floor = 0.0;                                     // CI 门重开 (paper 放宽 −1)
    return c;
}

}  // namespace stcpp::risk
