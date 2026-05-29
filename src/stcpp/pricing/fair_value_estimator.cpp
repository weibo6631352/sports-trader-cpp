// stcpp/pricing/fair_value_estimator.cpp — BaselineFairValueModel 实现
//
// Owner: 小肖 (numerical-algorithms)
// last_review: 2026-05-29
//
// 算法说明:
//   BaselineFairValueModel::estimate() 三步:
//     1) 比分/时钟 → sigmoid 先验
//     2) 订单簿 microprice Bayesian 混合 (可选)
//     3) normalize2 Kahan 归一化 + clamp
//
// 数值稳定性决策日志:
//   D1: safe_sigmoid 用分支: x>=0 → 1/(1+exp(-x)), x<0 → exp(x)/(1+exp(x))
//       原因: 避免 exp(|x|) 溢出或 catastrophic cancellation (两路都接近 1+eps 问题)
//   D2: time_fraction clamp 到 [0, 1] — elapsed_sec 可为 -1 (未知), 视作 0
//   D3: normalize2 Kahan sum: a+b 两个正数相加理论安全, 但 Kahan 做防御性保证
//       因为 a, b 都是 clamp_prob 后的值, 量级一致 (无 catastrophic cancellation)
//   D4: microprice 边界: ∈ (kProbEps, kProbMax) 才认为有效; NaN → kappa=0
//   D5: book_blend 混合权重固定 0.20 防止 microprice 噪声主导 (订单簿薄时)

#include "stcpp/pricing/fair_value_estimator.hpp"

#include <cmath>
#include <optional>

namespace stcpp::pricing {

// ---------------------------------------------------------------------------
// 私有辅助: 时钟分数 [0, 1]
// ---------------------------------------------------------------------------
double BaselineFairValueModel::time_fraction_(FeatureStoreGameRow const& row) noexcept {
    using stcpp::data::goalserve::TimeStatus;

    // 赛前 / 未开始 → 0
    if (row.time_status == TimeStatus::NotStarted) {
        return 0.0;
    }
    // 终态 → 1 (Ended / Walkover / Abandoned / Retired / Removed 等)
    if (stcpp::data::goalserve::IsTerminal(row.time_status)) {
        return 1.0;
    }
    // elapsed_sec == -1 表示 "无法获知" (见 goalserve_record.hpp 注释) → 0
    if (row.elapsed_sec < 0) {
        return 0.0;
    }
    // 运动总秒数 (0 = 无时钟运动, e.g., 棒球/网球)
    int const total_sec = total_game_seconds(row.sport);
    if (total_sec <= 0) {
        return 0.0;
    }
    // 安全除法: elapsed_sec 不超过 total_sec 上 clip
    double const elapsed = static_cast<double>(row.elapsed_sec);
    double const total = static_cast<double>(total_sec);
    double frac = elapsed / total;
    if (!std::isfinite(frac))
        return 0.0;
    if (frac < 0.0)
        return 0.0;
    if (frac > 1.0)
        return 1.0;
    return frac;
}

// ---------------------------------------------------------------------------
// 私有辅助: 从 book_row 提取 YES side microprice
// ---------------------------------------------------------------------------
std::optional<double> BaselineFairValueModel::extract_microprice_(
    FeatureStoreBookRow const* book_row) noexcept {
    if (book_row == nullptr)
        return std::nullopt;
    // 只接受 YES side (订单簿 microprice 是 YES token 的成交概率估计)
    if (book_row->token_side != "YES")
        return std::nullopt;
    double const mp = book_row->microprice;
    // NaN/Inf 防护
    if (!std::isfinite(mp))
        return std::nullopt;
    // 有效范围: (kProbEps, kProbMax) — 极端值视为无效
    if (mp <= kProbEps || mp >= kProbMax)
        return std::nullopt;
    return mp;
}

// ---------------------------------------------------------------------------
// 主算法: BaselineFairValueModel::estimate
// ---------------------------------------------------------------------------
FairValueResult BaselineFairValueModel::estimate(FeatureStoreGameRow const& game_row,
                                                 FeatureStoreBookRow const* book_row) const noexcept {
    FairValueResult result{};

    // ---- Step 1: 比分/时钟先验 ----
    // score_diff = home - away  (整数差, 安全转换)
    // 注: int32_t 相减, 最大差约 ±200 (篮球), 不会溢出
    int const score_diff_i =
        static_cast<int>(game_row.score_home_total) - static_cast<int>(game_row.score_away_total);
    double const score_diff = static_cast<double>(score_diff_i);

    // time_fraction [0, 1]
    double const time_frac = time_fraction_(game_row);

    // log-odds 先验: alpha * score_diff + beta * time_fraction
    // 数值: |alpha * score_diff| 最大 ~0.30 * 200 = 60, safe_sigmoid 安全处理
    double const log_odds_prior = params_.alpha * score_diff + params_.beta * time_frac;

    double p_yes_prior = safe_sigmoid(log_odds_prior);
    // clamp 到合法范围
    p_yes_prior = clamp_prob(p_yes_prior);
    result.prior_yes = p_yes_prior;

    // ---- Step 2: 订单簿微调 (Bayesian 混合) ----
    // p_yes_adj = kappa * microprice + (1 - kappa) * p_yes_prior
    // kappa = book_blend_ 如果 microprice 有效, 否则 0 (纯先验)
    double p_yes_adj = p_yes_prior;
    double kappa_used = 0.0;

    std::optional<double> const microprice = extract_microprice_(book_row);
    if (microprice.has_value()) {
        kappa_used = book_blend_;
        // 线性混合 (加权平均)
        p_yes_adj = kappa_used * microprice.value() + (1.0 - kappa_used) * p_yes_prior;
        // 防御性检查: 混合结果仍需 finite
        if (!std::isfinite(p_yes_adj)) {
            // fallback: 纯先验
            p_yes_adj = p_yes_prior;
            kappa_used = 0.0;
        }
    }
    result.book_blend = kappa_used;

    // ---- Step 3: 归一化 + clamp ----
    // p_no_adj = 1 - p_yes_adj (互补)
    double const p_no_adj = 1.0 - p_yes_adj;

    // normalize2 Kahan: 处理浮点误差累积
    std::array<double, 2> const normed = normalize2(p_yes_adj, p_no_adj);
    result.probs[0] = normed[0];  // YES
    result.probs[1] = normed[1];  // NO

    // valid = probs 均 finite + 在 [kProbEps, kProbMax] 内
    // 注: clamp_prob 保证输出在 [kProbEps, kProbMax], 所以 valid 检查 finite + 非 NaN 即可
    bool const p0_ok = std::isfinite(result.probs[0]) && !std::isnan(result.probs[0]);
    bool const p1_ok = std::isfinite(result.probs[1]) && !std::isnan(result.probs[1]);
    result.valid = p0_ok && p1_ok;

    return result;
}

}  // namespace stcpp::pricing
