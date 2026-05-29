// stcpp/backtest/stats.hpp — Backtest 过拟合检测统计 v0.2
//
// Owner: 小蒋 (quant-backtest)
// Last review: 2026-05-29
//
// 落: xiaojiang-backtest-framework-v0.2-cpp.md §4 (过拟合检测)
//     xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md §3.2 §3.5 (Bonferroni + DSR)
//
// 红线:
//   R-7  纯函数, mode-agnostic
//   R-11 不写 ledger
//
// 包含:
//   1. bonferroni_adjust — 多重比较 Bonferroni 校正 (§3.2 N=81 参数扫描)
//   2. deflated_sharpe   — DSR = Deflated Sharpe Ratio (López de Prado 2014)
//      门槛: DSR > 1.0 (§3.5 MVP 门槛)
//
// 注: DSR 公式中的 Φ / Φ⁻¹ 用 erfc / 误差函数近似 (无 Boost 依赖).
//     精度对 MVP 门槛 (> 1.0) 足够. M5+ 可升级 Boost.Math.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "stcpp/backtest/types.hpp"

namespace stcpp::backtest::stats {

// ---------------------------------------------------------------------------
// 1. Bonferroni 多重比较校正 (§3.2)
// ---------------------------------------------------------------------------
//
// 校正后 p_i' = min(1, p_i × n_experiments)
// 门槛: 校正后 p' < 0.05 才算 IS 通过 (小程 §3.2)
//

[[nodiscard]] inline std::vector<double> bonferroni_adjust(std::vector<double> const& p_values,
                                                           std::size_t n_experiments) noexcept {
    std::vector<double> out;
    out.reserve(p_values.size());
    double const n = static_cast<double>(n_experiments);
    for (double p : p_values) {
        double const adj = p * n;
        out.push_back(adj < 1.0 ? adj : 1.0);
    }
    return out;
}

// 单值版 (简便接口)
[[nodiscard]] inline double bonferroni_adjust_single(double p_value, std::size_t n_experiments) noexcept {
    double const adj = p_value * static_cast<double>(n_experiments);
    return adj < 1.0 ? adj : 1.0;
}

// ---------------------------------------------------------------------------
// 2. Deflated Sharpe Ratio (DSR) — López de Prado 2014
// ---------------------------------------------------------------------------
//
// 公式 (小蒋 framework §4.2):
//   DSR = Z(SR_hat) - correction
//   correction = sqrt(1 - gamma) / sqrt(T - 1) * sigma_SR * Phi_inv(1 - 1/N) * sqrt(T)
//
// 其中:
//   sr_hat   = 观测 Sharpe (已年化)
//   sigma_sr = Sharpe 时序 std (样本间 Sharpe 的波动)
//   gamma    = Sharpe 时序 skewness (简化: 设 0, 保守)
//   T        = trade 数 (样本量)
//   N        = 试过的策略/参数组合数 (N=81 for P0-02 §3.2)
//
// 注: MVP 简化版: Z(SR_hat) ≈ SR_hat (已年化 Sharpe, 无量纲)
//   Phi_inv 用正态分位数近似 (Beasley-Springer-Moro 算法)
//
// 门槛: DSR > 1.0
//

namespace detail {

// 正态 CDF Φ(x) ≈ 0.5 * erfc(-x / sqrt(2))
[[nodiscard]] inline double normal_cdf(double x) noexcept {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

// 正态逆 CDF Φ^-1(p) 使用迭代法 (Brent 二分, 足够 MVP 精度)
// 范围 p ∈ (0, 1); 极端值 clip
[[nodiscard]] inline double normal_quantile(double p) noexcept {
    if (p <= 0.0)
        return -8.0;
    if (p >= 1.0)
        return 8.0;

    // 初始猜测: 常用近似 (Rational approx, NIST §26.2.22)
    // 对 p 很接近 0.5 的 case 精度足够
    double lo = -8.0, hi = 8.0;
    for (int i = 0; i < 60; ++i) {
        double const mid = 0.5 * (lo + hi);
        if (normal_cdf(mid) < p) {
            lo = mid;
        } else {
            hi = mid;
        }
        if (hi - lo < 1e-8)
            break;
    }
    return 0.5 * (lo + hi);
}

}  // namespace detail

struct DsrInput {
    double sr_hat{0.0};    // 观测 Sharpe (已年化)
    double sigma_sr{0.3};  // Sharpe std across bootstrap samples (默认 0.3 保守)
    double gamma{0.0};     // Sharpe skewness (MVP: 设 0, 保守)
    std::size_t T{100};    // trade 数 (样本量)
    std::size_t N{81};     // 参数组合数 (P0-02: 81 = 3^4)
};

struct DsrResult {
    double deflated_sharpe{0.0};  // DSR 值
    double correction{0.0};       // 修正项 (越大 → 过拟合惩罚越重)
    bool pass{false};             // DSR > 1.0
    bool valid{false};            // 输入合法
};

[[nodiscard]] inline DsrResult compute_deflated_sharpe(DsrInput const& in) noexcept {
    DsrResult out;

    // 输入校验
    if (in.T < 2 || in.N < 1 || in.sigma_sr < 0.0) {
        return out;
    }

    double const T = static_cast<double>(in.T);
    double const N = static_cast<double>(in.N);

    // Phi_inv(1 - 1/N): 当 N→1 时 = 0, 当 N=81 时 ≈ 2.20
    double const q = 1.0 - 1.0 / N;
    double const phi_inv = detail::normal_quantile(q);

    // correction = sqrt(1 - gamma) / sqrt(T - 1) * sigma_SR * phi_inv * sqrt(T)
    //           = sigma_SR * phi_inv * sqrt(1 - gamma) * sqrt(T) / sqrt(T - 1)
    double const sqrt_T = std::sqrt(T);
    double const sqrt_Tm1 = std::sqrt(T - 1.0);
    double const sqrt_1mg = std::sqrt(1.0 - in.gamma);  // gamma=0 → 1

    out.correction = in.sigma_sr * phi_inv * sqrt_1mg * sqrt_T / sqrt_Tm1;

    // DSR = SR_hat - correction
    // (简化: Z(SR_hat) ≈ SR_hat, MVP 阈值 > 1.0)
    out.deflated_sharpe = in.sr_hat - out.correction;
    out.pass = (out.deflated_sharpe > 1.0);
    out.valid = true;

    return out;
}

// ---------------------------------------------------------------------------
// 3. 参数扫描结果汇总 (§3.2 IS 阶段)
// ---------------------------------------------------------------------------

struct ScanResult {
    stcpp::backtest::ParamSet params{};
    double is_sharpe{0.0};
    double is_p_value{1.0};
    double bonferroni_p{1.0};
    DsrResult dsr{};
    std::size_t n_trades{0};
    bool is_pass{false};  // Bonferroni 校正后 p < 0.05 AND DSR > 1.0
};

// 从多个参数扫描结果中选出 IS 最优 (Bonferroni 校正后 IS Sharpe 最高且 pass)
[[nodiscard]] inline ScanResult select_best_params(std::vector<ScanResult>& results,
                                                   std::size_t n_experiments) noexcept {
    // 先对所有结果做 Bonferroni 校正
    std::vector<double> p_vals;
    p_vals.reserve(results.size());
    for (auto const& r : results) {
        p_vals.push_back(r.is_p_value);
    }
    auto const adjusted = bonferroni_adjust(p_vals, n_experiments);
    for (std::size_t i = 0; i < results.size(); ++i) {
        results[i].bonferroni_p = adjusted[i];
        results[i].is_pass = (adjusted[i] < 0.05) && results[i].dsr.pass;
    }

    // 找 pass=true 中 IS Sharpe 最高的
    ScanResult best{};
    best.is_sharpe = -1e9;
    bool found = false;
    for (auto const& r : results) {
        if (r.is_pass && r.is_sharpe > best.is_sharpe) {
            best = r;
            found = true;
        }
    }
    // 若无 pass, 退化选最高 IS Sharpe (标记 is_pass=false 让 OOS 仍评估)
    if (!found) {
        for (auto const& r : results) {
            if (r.is_sharpe > best.is_sharpe) {
                best = r;
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// 4. 辅助: 从 TradeRecord 序列提取净 PnL 序列 (供 t-test 用)
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::vector<double> extract_net_edge_series(
    std::vector<stcpp::backtest::TradeRecord> const& trades) noexcept {
    std::vector<double> out;
    out.reserve(trades.size());
    for (auto const& t : trades) {
        if (t.filtered_out)
            continue;
        if (t.outcome == stcpp::backtest::SettleOutcome::Pending)
            continue;
        double const e = (t.size_usdc > 0.0) ? t.realized_pnl_usdc / t.size_usdc : 0.0;
        out.push_back(e);
    }
    return out;
}

}  // namespace stcpp::backtest::stats
