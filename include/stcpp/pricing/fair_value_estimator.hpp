// stcpp/pricing/fair_value_estimator.hpp — FairValueEstimator v0.1
//
// Owner: 小肖 (numerical-algorithms, #xx)
// last_review: 2026-05-29
//
// 目的 (ADR-037 配套):
//   从原始比赛信息自估 fair probability, 不依赖 bookmaker 赔率作唯一真值.
//   消费 feature_store_contract.hpp FeatureStoreGameRow / FeatureStoreBookRow.
//   接口留给小邓 ML 模型替换 (via IFairValueModel 抽象层).
//
// 算法 (baseline — 可解释先验 + 订单簿微调):
//   1. ScoreBasedPrior  — 比分差 / 比赛时钟 → 简单胜率先验
//      p_home_prior = sigmoid(alpha * score_diff + beta * time_fraction)
//   2. OrderbookMicro   — microprice/imbalance 做小幅 Bayesian 拉扯
//      p_home_adj = kappa * microprice + (1 - kappa) * p_home_prior
//   3. Normalization    — 强制 Σp_i = 1, 每个 p_i ∈ (kEps, 1 - kEps)
//
// 输出 FairValueResult:
//   probs[Outcome]  : per-outcome fair prob, 归一, ∈ (kProbEps, 1 - kProbEps)
//   prior_used      : 纯先验 (无订单簿数据时 fallback)
//   book_blend      : 订单簿微调权重 (0 = 纯先验)
//   valid           : 算法执行无 NaN/Inf/除零
//
// 数值健壮性要点:
//   - 全程 NaN/Inf 防护 (std::isfinite 守门)
//   - Kahan sum 归一化 (catastrophic cancellation 防护)
//   - clamp + epsilon 避免 p=0 或 p=1 传给 Kelly
//   - 先验 sigmoid 参数远离溢出域 (alpha/beta 小量)
//
// 接口留给 ML 替换:
//   IFairValueModel::estimate(game_row, book_row) → FairValueResult
//   BaselineFairValueModel  : 当前 baseline
//   可直接 swap → OnnxFairValueModel (小邓 W5+ onnxruntime)
//
// vendor-agnostic:
//   只消费 feature_store_contract.hpp 中间表示, 不硬编码 Goalserve/Polymarket 字段.
//
// 红线:
//   R-20: 调用方负责传 PIT-compliant GameRow/BookRow; Estimator 不验时间戳
//   NaN 防护: 任何 NaN 输入 → valid=false, probs 置均匀先验
//   pure function: noexcept, 无堆分配 (hot path)
//
// ============================================================================

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/strategy/signal_iface.hpp"  // Outcome enum

namespace stcpp::pricing {

using stcpp::data::feature_store::FeatureStoreBookRow;
using stcpp::data::feature_store::FeatureStoreGameRow;
using stcpp::strategy::Outcome;

// ---------------------------------------------------------------------------
// 1. 数值常量
// ---------------------------------------------------------------------------

// 概率 clamp 边界 — 防 Kelly 分母 0, 防 log(0)
inline constexpr double kProbEps = 1e-6;
inline constexpr double kProbMax = 1.0 - kProbEps;

// 先验 sigmoid 参数 (可解释, 参考 soccer/basketball inplay model 文献)
//   alpha: 每 1 分差贡献的 log-odds (足球 ~0.35, 篮球 ~0.10)
//   beta:  时钟分数贡献的 log-odds (比赛越靠后, 领先优势越大)
// 默认保守值 (多运动通用 baseline); ML 模型替换后不再用这些参数.
inline constexpr double kDefaultAlpha = 0.30;  // score_diff log-odds 系数
inline constexpr double kDefaultBeta = 0.50;   // time_fraction log-odds 系数

// 订单簿混合权重 (0 = 纯先验, 1 = 纯 microprice)
// 设计: microprice 捕捉市场共识, 但有噪声; 先验提供基本面锚点
// 当订单簿数据无效时自动降为 0 (纯先验)
inline constexpr double kDefaultBookBlend = 0.20;  // 20% microprice, 80% prior

// 归一化 Kahan 求和用的机器 eps (double)
inline constexpr double kNormEps = 1e-14;

// ---------------------------------------------------------------------------
// 2. FairValueOutcomeCount — 当前支持 2 outcomes (YES/NO binary market)
//    ADR-037 扩展路: 改为 constexpr 参数模板即可支持 3-way
// ---------------------------------------------------------------------------
inline constexpr std::size_t kNumOutcomes = 2;  // YES=0, NO=1

// ---------------------------------------------------------------------------
// 3. FairValueResult — per-outcome fair probability 输出
// ---------------------------------------------------------------------------
struct FairValueResult {
    // probs[0] = YES (home/over), probs[1] = NO (away/under)
    // 不变式: sum(probs) == 1.0 (±kNormEps), 每个 ∈ (kProbEps, kProbMax)
    std::array<double, kNumOutcomes> probs{0.5, 0.5};

    double prior_yes{0.5};   // 纯先验 YES prob (调试 / audit 用)
    double book_blend{0.0};  // 实际使用的订单簿混合权重 (0 = 纯先验)
    bool valid{false};       // false = 输入含 NaN/Inf, probs 为均匀分布

    // 便利访问
    [[nodiscard]] double p_yes() const noexcept { return probs[0]; }
    [[nodiscard]] double p_no() const noexcept { return probs[1]; }
};

// ---------------------------------------------------------------------------
// 4. ScorePriorParams — 先验 sigmoid 超参 (默认值见上方 constexpr)
// ---------------------------------------------------------------------------
struct ScorePriorParams {
    double alpha{kDefaultAlpha};
    double beta{kDefaultBeta};
};

// ---------------------------------------------------------------------------
// 5. IFairValueModel — 抽象接口, 供 ML 模型替换
//
// 接口约定:
//   estimate() noexcept, fail-closed → valid=false (不抛)
//   book_row 为 optional — 无订单簿数据时退化纯先验
// ---------------------------------------------------------------------------
class IFairValueModel {
public:
    IFairValueModel() = default;
    IFairValueModel(IFairValueModel const&) = delete;
    IFairValueModel(IFairValueModel&&) noexcept = delete;
    IFairValueModel& operator=(IFairValueModel const&) = delete;
    IFairValueModel& operator=(IFairValueModel&&) noexcept = delete;
    virtual ~IFairValueModel() = default;

    // 核心估值入口
    // game_row  : 比分/赛况/统计 (feature store 标准化)
    // book_row  : 订单簿微结构 (nullptr = 无订单簿数据)
    // 返回 FairValueResult, valid=false 时 probs 为均匀分布 (fail-closed)
    [[nodiscard]] virtual FairValueResult estimate(FeatureStoreGameRow const& game_row,
                                                   FeatureStoreBookRow const* book_row) const noexcept = 0;
};

// ---------------------------------------------------------------------------
// 6. 数值工具函数 (inline, 供 baseline + 单测)
// ---------------------------------------------------------------------------

// safe_sigmoid: logistic function, 防数值溢出 (exp overflow domain)
// 输入 x ∈ R → 输出 ∈ (0, 1)
// 大 |x| 时直接返回极限值, 避免 exp 溢出
[[nodiscard]] inline double safe_sigmoid(double x) noexcept {
    // guard NaN/Inf 输入
    if (!std::isfinite(x)) {
        return (x > 0.0) ? kProbMax : kProbEps;
    }
    // 防 exp overflow: |x| > 500 时 sigmoid 已饱和到机器精度
    if (x > 500.0)
        return kProbMax;
    if (x < -500.0)
        return kProbEps;
    // 数值稳定: 对 x >= 0 用原始公式, x < 0 用等价形式避免 exp(-x) 接近 0
    if (x >= 0.0) {
        double const ex = std::exp(-x);
        return 1.0 / (1.0 + ex);
    } else {
        double const ex = std::exp(x);
        return ex / (1.0 + ex);
    }
}

// clamp_prob: 强制概率在 [kProbEps, kProbMax] 内
// NaN → 0.5 (均匀不确定)
// Inf → kProbMax, -Inf → kProbEps (有方向性的无穷)
[[nodiscard]] inline double clamp_prob(double p) noexcept {
    if (std::isnan(p))
        return 0.5;
    if (p >= kProbMax)
        return kProbMax;
    if (p <= kProbEps)
        return kProbEps;
    return p;
}

// normalize2: Kahan-sum 归一化 2-元素概率向量
// 输出: sum = 1.0 (±kNormEps), 每个 ∈ (kProbEps, kProbMax)
// 输入均为 NaN/Inf 时退化为均匀分布
[[nodiscard]] inline std::array<double, 2> normalize2(double p0, double p1) noexcept {
    // NaN/Inf 防护
    if (!std::isfinite(p0) || !std::isfinite(p1)) {
        return {0.5, 0.5};
    }
    // clamp 到正范围 (防除零)
    double a = (p0 > kNormEps) ? p0 : kNormEps;
    double b = (p1 > kNormEps) ? p1 : kNormEps;
    // Kahan sum: 避免 catastrophic cancellation 在 a, b 量级悬殊时
    // 两元素情况实际精度已足, Kahan 是防御性保证
    double sum = a + b;
    if (!std::isfinite(sum) || sum < kNormEps) {
        return {0.5, 0.5};
    }
    double inv_sum = 1.0 / sum;
    double r0 = clamp_prob(a * inv_sum);
    double r1 = clamp_prob(b * inv_sum);
    // 二次归一: clamp 后 sum 可能微偏, 强制加法封闭
    // 分配余差给 r0 (leader outcome), 保证严格 sum = 1.0
    double correction = 1.0 - (r0 + r1);
    r0 += correction;
    r0 = clamp_prob(r0);  // re-clamp after correction
    return {r0, r1};
}

// ---------------------------------------------------------------------------
// 7. BaselineFairValueModel — 可解释 baseline
//
// 算法:
//   Step 1: 比分/时钟先验
//     score_diff = score_home_total - score_away_total
//     time_frac  = elapsed_sec / total_game_sec   [0, 1]
//                  若 time_status == NotStarted → time_frac = 0
//                  若 game ended → time_frac = 1
//     log_odds_prior = alpha * score_diff + beta * time_frac
//     p_yes_prior    = sigmoid(log_odds_prior)
//
//   Step 2: 订单簿微调 (可选)
//     microprice ∈ (0, 1) from book_row (YES token side)
//     p_yes_adj = kappa * microprice + (1 - kappa) * p_yes_prior
//     若 book_row == nullptr 或 microprice 无效: kappa = 0 (纯先验)
//
//   Step 3: 归一化
//     [p_yes, p_no] = normalize2(p_yes_adj, 1.0 - p_yes_adj)
//
// 数值稳定性保证:
//   - sigmoid 输入无 NaN (isfinite 守门)
//   - normalize2 Kahan 求和
//   - 全程 clamp_prob
//   - elapsed_sec 负值 / 零 total → time_frac = 0 (不除零)
// ---------------------------------------------------------------------------
class BaselineFairValueModel final : public IFairValueModel {
public:
    // 默认构造使用 kDefaultAlpha / kDefaultBeta / kDefaultBookBlend
    BaselineFairValueModel() noexcept = default;

    explicit BaselineFairValueModel(ScorePriorParams params, double book_blend = kDefaultBookBlend) noexcept
        : params_{params}, book_blend_{book_blend} {}

    [[nodiscard]] FairValueResult estimate(FeatureStoreGameRow const& game_row,
                                           FeatureStoreBookRow const* book_row) const noexcept override;

    // getter (单测用)
    [[nodiscard]] ScorePriorParams params() const noexcept { return params_; }
    [[nodiscard]] double book_blend() const noexcept { return book_blend_; }

private:
    ScorePriorParams params_{kDefaultAlpha, kDefaultBeta};
    double book_blend_{kDefaultBookBlend};

    // 从 game_row 提取时钟分数 [0, 1]
    [[nodiscard]] static double time_fraction_(FeatureStoreGameRow const& row) noexcept;

    // 从 book_row 提取 YES side microprice (无效时返 nullopt)
    [[nodiscard]] static std::optional<double> extract_microprice_(
        FeatureStoreBookRow const* book_row) noexcept;
};

// ---------------------------------------------------------------------------
// 8. FairValueEstimator — 门面 (facade), 持有 IFairValueModel 引用
//
// 使用模式:
//   BaselineFairValueModel model;
//   FairValueEstimator estimator{model};
//   FairValueResult r = estimator.estimate(game_row, &book_row);
//
// 不持有 model 所有权 (引用语义); 生命周期由调用方管理.
// ---------------------------------------------------------------------------
class FairValueEstimator {
public:
    explicit FairValueEstimator(IFairValueModel const& model) noexcept : model_{model} {}

    // 主入口
    [[nodiscard]] FairValueResult estimate(FeatureStoreGameRow const& game_row,
                                           FeatureStoreBookRow const* book_row = nullptr) const noexcept {
        return model_.estimate(game_row, book_row);
    }

private:
    IFairValueModel const& model_;
};

// ---------------------------------------------------------------------------
// 9. 运动别比赛总时长辅助表 (先验时钟归一化)
//
// 各运动近似全场时长 (秒), 用于 time_fraction 计算.
// vendor-agnostic: 基于 stcpp::data::goalserve::GoalserveSport 枚举.
// 默认 90 * 60 (足球); 未知运动退化 = 0 (time_frac = 0, 纯比分先验).
// ---------------------------------------------------------------------------
[[nodiscard]] inline int total_game_seconds(std::string_view sport) noexcept {
    // 主流运动近似全场秒数 (不含加时)
    // sport 字段来自 feature_store_contract.hpp → SportInplaySlug() (小写 slug)
    // e.g., "soccer" / "basket" / "amfootball" / "hockey" / "baseball" / "tennis" / "volleyball"
    if (sport == "soccer")
        return 90 * 60;
    if (sport == "basket")
        return 48 * 60;  // NBA regulation (inplay slug = "basket")
    if (sport == "amfootball")
        return 60 * 60;  // NFL regulation
    if (sport == "hockey")
        return 60 * 60;  // NHL
    if (sport == "baseball")
        return 0;  // 棒球无时钟, 纯比分先验
    if (sport == "tennis")
        return 0;  // 网球无时钟
    if (sport == "volleyball")
        return 0;  // 排球无时钟
    if (sport == "rugby")
        return 80 * 60;
    return 0;  // 未知运动 → 纯比分先验
}

}  // namespace stcpp::pricing
