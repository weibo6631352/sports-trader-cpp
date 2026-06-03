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

// ResolveEdgeCiLower — 源感知 edge 下界 (老板 2026-06-03「sharp 路径纯 net-EV 门」)。
//
// 根因 (实测 decision-diag, 2026-06-03): sharp_inplay 源 = bet365 de-vig 共识【点估计】,
//   不是 n_eff 个 Bernoulli 样本的均值。对它套用二项抽样惩罚 z·√(p(1−p)/n_eff) 是错误模型:
//   实测 n_eff≈6 → 惩罚 ~0.22, 而真实 in-play sharp↔PM 错价仅 ~0.02–0.05 → edge_ci_lower 恒负
//   → sizing 永远 NO_EDGE → sharp 套利【一笔都不成交】(这正是「为什么有 sharp 进不了可下单侧」)。
//
// 修法 (老板拍板「纯 net-EV 门」): sharp 共识源【不扣样本噪声】, edge_ci_lower = raw_edge − sharp_margin。
//   经济过滤交给下游既有门: sizing 内 slippage 门 (edge_bps ≥ slippage_bps) + fee 门
//   (net_ci_edge = edge − fee > floor) + paper_loop 外层 net_ev_ok (edge ≥ 2×fee + slippage,
//   体育 fee 0.03 → 中价处约需 ~1.6% 错价才放行)。sharp_margin 默认 0 = 纯 net-EV (margin 由上述门承担)。
//   非 sharp 源 (score-prior / ML 统计估计) 仍走二项 CI — 它们【确实】是噪声估计, 需抽样惩罚。
//
// 红线 §8 (回测=实盘同逻辑): 本函数是源感知 gate 的唯一实现; backtest event_replayer 当前事件模型
//   不带 fair 源 (无法区分 sharp/score-prior), 仍走 ComputeEdgeCiLower。在 sharp-arb 策略上线前
//   必须让 replay 事件携带 fair 源并改用本函数 (parity follow-up; 当前 paper 验证期无 live 失配)。
[[nodiscard]] inline double ResolveEdgeCiLower(bool is_sharp_consensus, double p_fair, double p_ask,
                                               int n_eff, double z, double sharp_margin = 0.0) noexcept {
    if (is_sharp_consensus) {
        if (!std::isfinite(p_fair) || !std::isfinite(p_ask) || !std::isfinite(sharp_margin)) {
            return -1.0;  // fail-closed
        }
        const double raw = (p_fair - p_ask) - sharp_margin;
        return raw < -1.0 ? -1.0 : (raw > 1.0 ? 1.0 : raw);
    }
    return ComputeEdgeCiLower(p_fair, p_ask, n_eff, z);
}

}  // namespace stcpp::strategy
