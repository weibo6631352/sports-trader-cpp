// include/stcpp/strategy/edge_ci.hpp — edge CI 下界 (实盘 + 回测单一实现)
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 量化阻塞3 修复 (回测-实盘 CI 口径统一)。
//
// 红线 §8: 回测与实盘必须用同一数据处理逻辑。本函数是 edge_ci_lower 的**唯一实现**,
//   paper_loop(实盘语义) 与 backtest event_replayer 共用, 消除两处公式漂移 (老郭 nit#2 跨模块版)。
//
// 公式: edge_ci_lower = (p_fair - p_ask) - z * sqrt(p_fair*(1-p_fair)/n_eff)
//   数值不稳 / n_eff<=0 → fail-closed 返 -1.0 (禁下单)。clamp [-1, 1]。
#pragma once

#include <cmath>

namespace stcpp::strategy {

[[nodiscard]] inline double ComputeEdgeCiLower(double p_fair, double p_ask, int n_eff, double z) noexcept {
    if (!std::isfinite(p_fair) || !std::isfinite(p_ask) || n_eff <= 0) {
        return -1.0;  // fail-closed
    }
    const double raw_edge = p_fair - p_ask;
    const double var = p_fair * (1.0 - p_fair);
    if (!std::isfinite(var) || var < 0.0) {
        return -1.0;
    }
    const double sigma = std::sqrt(var / static_cast<double>(n_eff));
    const double ci_lower = raw_edge - z * sigma;
    if (!std::isfinite(ci_lower)) {
        return -1.0;
    }
    return ci_lower < -1.0 ? -1.0 : (ci_lower > 1.0 ? 1.0 : ci_lower);
}

}  // namespace stcpp::strategy
