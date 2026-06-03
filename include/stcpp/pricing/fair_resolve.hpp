// include/stcpp/pricing/fair_resolve.hpp — 决策 fair 解析 (纯函数, 可单测)
//
// Owner: 老雷 (GM) — 架构评审 R-2 (老周/老郭 2026-06-01): 把 TickOne 里散落的 fair 优先级
//   抽成一处显式声明的纯函数。输入全是已算好的标量, 输出 p_fair + provenance。
// last_review: 2026-06-01
//
// 优先级 (显式钉死, 一处可读 — 原散在 paper_loop TickOne ~571-696 的隐式 if 链):
//   1. derivative (totals/spreads 专属定价) → 覆盖, 不叠 sharp/score/ML (派生解析模型即该盘 fair)
//   2. sharp-anchor (Goalserve bet365 in-play de-vig 共识) → 有真比分 + sharp 有效时锚 sharp
//   3. score-prior blend (FairValueEstimator 先验 × 市场 de-vig, 置信加权) → sharp 无效时回落
//   4. ML-blend (真 ONNX) → 仅非 derivative 时叠加在上述结果上 (有效 ml_p 才动)
//   默认 (无真比分/无 derivative): p_fair = 市场 de-vig (自己跟自己比, edge≈0, has_real_fair gate 兜底)
//
// 纯函数: 无 map/hub/model 依赖。ML 推理 (非纯) 在 TickOne 算好 ml_p_yes 传入。BR-1: 回测=实盘同函数。

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include "stcpp/pricing/fair_value_estimator.hpp"  // blend_prob

namespace stcpp::pricing {

// fair 来源 (provenance; 观测/调试: 这盘 fair 是哪层定的)。
enum class FairSrc { kMarketDevig, kDerivative, kSharpInplay, kScorePriorBlend, kMlBlend };

[[nodiscard]] inline const char* to_string(FairSrc s) noexcept {
    switch (s) {
        case FairSrc::kMarketDevig: return "market_devig";
        case FairSrc::kDerivative: return "derivative";
        case FairSrc::kSharpInplay: return "sharp_inplay";
        case FairSrc::kScorePriorBlend: return "score_prior_blend";
        case FairSrc::kMlBlend: return "ml_blend";
    }
    return "?";
}

struct FairInputs {
    double p_market_devig{0.5};                  // 市场 de-vig YES (默认锚)
    std::optional<double> derivative_p_yes;      // totals/spreads 专属定价 (有=覆盖)
    double sharp_yes{-1.0};                      // bet365 in-play de-vig YES (draw盘=draw概率); <0 或越界 = 无
    double score_prior_yes{0.5};                 // FairValueEstimator 先验
    double prior_conf{0.0};                      // 先验置信 (随时钟升)
    bool has_real_fair{false};                   // 有真实 in-play Goalserve 比分?
    std::optional<double> ml_p_yes;              // ONNX YES 预测 (仅适用时; 非 derivative)
    double ml_blend_weight{0.0};                 // ML blend 权重 (cfg)
};

struct FairResult {
    double p_fair{0.5};
    FairSrc src{FairSrc::kMarketDevig};
};

// ResolveFair — 按显式优先级解析决策 fair (BR-1 纯函数; 逐位等价原 TickOne inline 逻辑)。
[[nodiscard]] inline FairResult ResolveFair(const FairInputs& in) noexcept {
    // 1. derivative 覆盖 → 不叠 sharp/score/ML (派生定价即该盘 fair)。
    if (in.derivative_p_yes.has_value()) {
        return FairResult{*in.derivative_p_yes, FairSrc::kDerivative};
    }

    double p = in.p_market_devig;
    FairSrc src = FairSrc::kMarketDevig;

    // 2/3. 有真比分: sharp 优先, 无效回落 score-prior blend。
    if (in.has_real_fair) {
        if (in.sharp_yes >= 0.0 && in.sharp_yes <= 1.0) {
            p = in.sharp_yes;
            src = FairSrc::kSharpInplay;
        } else {
            p = blend_prob(in.score_prior_yes, in.p_market_devig, in.prior_conf);
            src = FairSrc::kScorePriorBlend;
        }
    }

    // 4. ML-blend (仅非 derivative + 仅【无 in-play 信号时】= src 仍 kMarketDevig = 纯 pre-game)。
    //   2026-06-03 治本 (in-play 套利转向): 原 ML weight=1.0 会【覆盖】sharp/score_prior — pregame ML
    //   (薄 alpha, 只买 YES) 盖掉真正的 in-play 套利信号 (sharp 随进球跳变 = 真 edge)。实测 in-play 路径
    //   每 tick 触发 2772/24000 但全被 ML 盖成 ml_blend → sharp_inplay 决策 0 个。改: 有 in-play 信号
    //   (sharp/score_prior, src≠market_devig) 时 ML 绝不动, in-play 权威; ML 只驱动纯 pre-game (无比分)。
    //   src→kMlBlend 让 ML 实际驱动可观测 (原无此枚举值 → ML 的 fair 日志显 market_devig 误导根因定位)。
    if (in.ml_p_yes.has_value() && in.ml_blend_weight > 0.0 && src == FairSrc::kMarketDevig) {
        const double ml = *in.ml_p_yes;
        if (std::isfinite(ml) && ml > 0.0 && ml < 1.0) {
            const double w = std::clamp(in.ml_blend_weight, 0.0, 1.0);
            p = (1.0 - w) * p + w * ml;
            if (w > 0.0) src = FairSrc::kMlBlend;
        }
    }

    return FairResult{p, src};
}

}  // namespace stcpp::pricing
