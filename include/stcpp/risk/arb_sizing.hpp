// include/stcpp/risk/arb_sizing.hpp — 短时套利专用 sizing (连续型 Kelly; Phase 3; 主计划 §4)
//
// Owner: 老雷 (GM) — 短时套利引擎 模块3/5
// last_review: 2026-06-01
//
// 与结算 Kelly (sizing_calculator: f*=net_ci_edge/(1−c), 赢赔 $1) 物理分开 —— 那是对赔付下注,
//   套利是对【mid 移动】下注, 结构不同。连续型 Kelly: f* = edge_arb / σ²_Δmid (老韩 RM 联签)。
//
// 设计哲学 (老板): 模型出信号 (edge/σ), sizing 用 CI 下界 (非点估计 — 微利高频靠点估计必死)。
//   λ=0.10 起 (估计误差占微利比例高), 稳定后升。深度封顶 = 套利第一杀手"进得去出不来"的 sizing 兜底。
#pragma once

#include <algorithm>
#include <cmath>

namespace stcpp::risk {

struct ArbSizingInput {
    double edge_arb{0.0};         // E[Δmid] − fee_roundtrip − E[adverse_slip] (净边际, 用 CI 下界算更稳)
    double sigma_dmid{0.0};       // 预测 Δmid 标准差 (来自模型 CI: (ci_high−ci_low)/(2·z))
    double bankroll_usdc{0.0};    // 账户规模
    double lambda{0.10};          // Kelly 分数 (0.10 起; 老韩 RM 联签)
    double max_notional_usdc{0.0};  // per-order 硬上限 (RM cap)
    double exit_depth_usdc{0.0};  // L1-L5 可平仓累计深度 (出不来约束)
};

struct ArbSizingOutput {
    bool valid{false};            // false = 不发单
    double f_star{0.0};           // Kelly 分数 (λ·edge/σ², clamp)
    double notional_usdc{0.0};    // 建议下单额
    const char* reason{""};       // 不发原因 (诊断)
};

// 连续型 Kelly + 多重封顶。edge_arb≤0 (不够本) / σ≤0 (无方差估计) → 不发。
[[nodiscard]] inline ArbSizingOutput ComputeArbSizing(const ArbSizingInput& in) noexcept {
    ArbSizingOutput out;
    if (!(in.edge_arb > 0.0)) {
        out.reason = "edge_arb<=0 (不够本)";
        return out;
    }
    if (!(in.sigma_dmid > 0.0) || !std::isfinite(in.sigma_dmid)) {
        out.reason = "sigma<=0 (无方差估计, 禁裸点估计 sizing)";
        return out;
    }
    if (!(in.bankroll_usdc > 0.0)) {
        out.reason = "bankroll<=0";
        return out;
    }
    // 连续型 Kelly: f* = edge / σ²; 乘 λ 分数。clamp [0,1]。
    const double var = in.sigma_dmid * in.sigma_dmid;
    double f = in.lambda * in.edge_arb / var;
    f = std::clamp(f, 0.0, 1.0);
    double notional = f * in.bankroll_usdc;
    // 硬封顶: per-order cap + 退出深度 (进得去出不来 → 不超可平深度)。
    if (in.max_notional_usdc > 0.0) notional = std::min(notional, in.max_notional_usdc);
    if (in.exit_depth_usdc > 0.0) notional = std::min(notional, in.exit_depth_usdc);
    if (!(notional > 0.0)) {
        out.reason = "notional<=0 (封顶后)";
        return out;
    }
    out.valid = true;
    out.f_star = f;
    out.notional_usdc = notional;
    return out;
}

}  // namespace stcpp::risk
