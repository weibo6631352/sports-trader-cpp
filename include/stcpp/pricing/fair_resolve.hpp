// include/stcpp/pricing/fair_resolve.hpp — 决策 fair 解析 (纯函数, 可单测)
//
// Owner: 老雷 (GM) — 架构评审 R-2 (老周/老郭 2026-06-01): 把 TickOne 里散落的 fair 优先级
//   抽成一处显式声明的纯函数。输入全是已算好的标量, 输出 p_fair + provenance。
// last_review: 2026-06-01
//
// 优先级 (显式钉死, 一处可读 — 原散在 trading_loop TickOne ~571-696 的隐式 if 链):
//   1. derivative (totals/spreads 专属定价) → 覆盖, 不叠 sharp/score/ML (派生解析模型即该盘 fair)
//   2. sharp-anchor (Goalserve bet365 in-play de-vig 共识) → sharp 有效即锚 (2026-06-12 删比分门: 赔率
//      本身即完整 fair, 不需 has_real_fair; 进场认赔率不认比分)
//   3. score-prior blend (FairValueEstimator 先验 × 市场 de-vig, 置信加权) → 无赔率 + 有真比分时回落 (备用)
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
enum class FairSrc { kMarketDevig, kDerivative, kSharpInplay, kScorePriorBlend };

[[nodiscard]] inline const char* to_string(FairSrc s) noexcept {
    switch (s) {
        case FairSrc::kMarketDevig: return "market_devig";
        case FairSrc::kDerivative: return "derivative";
        case FairSrc::kSharpInplay: return "sharp_inplay";
        case FairSrc::kScorePriorBlend: return "score_prior_blend";
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
    bool sharp_frozen{false};                    // sharp 源盘冻结 (Goalserve core.stopped/blocked/finished:
                                                 //   停表/封盘/完赛 → 赔率是死值)。true → sharp 失格, 不当锚,
                                                 //   自然回落 score-prior/市场。2026-06-14 老板「冻结要在赔率
                                                 //   策略引擎里就考虑, 不在事后 gate」。时间戳救不了(停盘 ts 照常重盖)。
    // (大模型 ml_p_yes / ml_blend_weight 已砍 2026-06-05「砍掉大模型训练功能」: fair 不再有 ONNX blend)
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

    // 2. sharp 优先 (老板 2026-06-12「两个盈利引擎都不硬依赖比分, 删比分门」): 有效 bet365 in-play 赔率
    //    即用, 不再被 has_real_fair(比分) 门锁 —— 赔率本身就是完整 fair, 进场认赔率不认比分。
    //    ⚠ 但【冻结盘】(core.stopped/blocked/finished) 的赔率是死值 → sharp_frozen 时 sharp 失格,
    //      不当锚 (2026-06-14 老板「在赔率策略引擎里就考虑」); 落到下方 score-prior/市场 → fair_src 自然非 sharp。
    if (in.sharp_yes >= 0.0 && in.sharp_yes <= 1.0 && !in.sharp_frozen) {
        p = in.sharp_yes;
        src = FairSrc::kSharpInplay;
    } else if (in.has_real_fair) {
        // 3. 备用: 赔率消失但还有比分 → score-prior blend (它本身需要比分; 老板「比分作为备用 sharp」)。
        p = blend_prob(in.score_prior_yes, in.p_market_devig, in.prior_conf);
        src = FairSrc::kScorePriorBlend;
    }

    // (4. ML-blend 已砍 2026-06-05「砍掉大模型训练功能」: fair 不再有 ONNX 推理 blend。
    //  kMlBlend 枚举值 2026-06-12 治理删除 — 不可达即删, git 史可考。)

    return FairResult{p, src};
}

}  // namespace stcpp::pricing
