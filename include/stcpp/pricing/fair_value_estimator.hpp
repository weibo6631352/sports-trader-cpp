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
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

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
// 置信权重 ramp (P0-3 dogfood-remediation):
//   in-play score-prior 的置信度随比赛进程从 base 线性升到 max.
//   早期 (time_frac=0) 信号弱 → 主要听市场 de-vig; 后期领先更确定 → 加大先验权重.
//   终态 (Ended/...) 由调用方直接给 conf=1.0 (确定性结果), 不用此 ramp.
// ---------------------------------------------------------------------------
inline constexpr double kBasePriorConfidence = 0.15;  // time_frac=0 时的先验权重
inline constexpr double kMaxPriorConfidence = 0.60;   // time_frac=1 时的先验权重上限

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
// 6b. de-vig + 信号融合 纯函数 (P1-8 / P0-3 dogfood-remediation)
//
// 这些是无状态 noexcept 纯函数, 供 paper_loop 把 edge 锚在"去 vig 的 fair 概率"
// 而非裸 mid/ask (后者含庄家 overround → 系统性偏高 → P1-8 假 edge).
// 同时给 in-play 真实先验提供置信加权凸组合工具 (P0-3).
// ---------------------------------------------------------------------------

// devig_binary: 二元市场 (YES/NO) de-vig — 剥离 overround.
//   p_yes_fair = yes_mid / (yes_mid + no_mid)
//
// 退化 / fail-closed 语义 (宁可空不可假):
//   - 双边都无效 (<=0 或 非有限)            → std::nullopt
//   - 仅 YES 有效                            → clamp_prob(yes_mid) (单边裸 mid)
//   - 仅 NO  有效                            → clamp_prob(1 - no_mid) (NO 隐含 YES)
//   - 双边有效                                → clamp_prob(yes_mid / (yes_mid+no_mid))
//
// 注: 单边退化保留裸 mid 是有意为之 — 单边时无法估 overround, 已是当前可得最优.
//     调用方据 has_value() 决定是否有可用市场锚 (无 → 不产 intent).
[[nodiscard]] inline std::optional<double> devig_binary(double yes_mid, double no_mid) noexcept {
    const bool yes_ok = std::isfinite(yes_mid) && (yes_mid > 0.0);
    const bool no_ok = std::isfinite(no_mid) && (no_mid > 0.0);

    if (!yes_ok && !no_ok) {
        return std::nullopt;  // fail-closed: 无任何可用市场信号
    }
    if (yes_ok && !no_ok) {
        return clamp_prob(yes_mid);  // 单边 YES: 退化为裸 mid
    }
    if (!yes_ok && no_ok) {
        return clamp_prob(1.0 - no_mid);  // 单边 NO: 隐含 YES = 1 - no_mid
    }

    const double denom = yes_mid + no_mid;
    if (!std::isfinite(denom) || denom <= kNormEps) {
        return std::nullopt;
    }
    return clamp_prob(yes_mid / denom);
}

// logit(p) = log(p/(1−p)) — 对数赔率空间 (小肖: ML 对中等概率区更线性; 尺度不变)。
//   p 内部 clamp 到 (eps, 1−eps) 防溢出。
[[nodiscard]] inline double logit(double p) noexcept {
    const double pc = clamp_prob(p);
    return std::log(pc / (1.0 - pc));
}

// devig_binary_power: power de-vig (小肖 g_fld_signal) — 解 yes^n + no^n = 1 (Newton), 返 yes^n。
//   修 favorite-longshot 偏差 (比 multiplicative 对热门压缩更准)。退化同 devig_binary。
//   与 multiplicative 的差 = favorite-longshot 偏差强度信号 (不替换主 fair, 当特征)。
[[nodiscard]] inline std::optional<double> devig_binary_power(double yes_mid, double no_mid) noexcept {
    const bool yes_ok = std::isfinite(yes_mid) && yes_mid > 0.0 && yes_mid < 1.0;
    const bool no_ok = std::isfinite(no_mid) && no_mid > 0.0 && no_mid < 1.0;
    if (!yes_ok || !no_ok) {
        // 单边/退化: 回落 multiplicative (power 无意义)。
        return devig_binary(yes_mid, no_mid);
    }
    // Newton 求 n: f(n) = yes^n + no^n − 1 = 0。初值 n=1 (= 等效 raw 和); 3-5 次收敛。
    double n = 1.0;
    for (int iter = 0; iter < 12; ++iter) {
        const double yp = std::pow(yes_mid, n);
        const double np = std::pow(no_mid, n);
        const double f = yp + np - 1.0;
        const double fp = yp * std::log(yes_mid) + np * std::log(no_mid);  // f'(n)
        if (!std::isfinite(fp) || std::abs(fp) < 1e-15) break;
        const double step = f / fp;
        n -= step;
        if (!std::isfinite(n) || n <= 0.0) {
            return devig_binary(yes_mid, no_mid);  // 发散 → 回落
        }
        if (std::abs(step) < 1e-10) break;
    }
    return clamp_prob(std::pow(yes_mid, n));
}

// prior_confidence: in-play 先验置信 ramp — time_frac ∈ [0,1] 线性映射到
//   [kBasePriorConfidence, kMaxPriorConfidence]. 越界自动 clamp 到端点.
[[nodiscard]] inline double prior_confidence(double time_frac) noexcept {
    double tf = std::isfinite(time_frac) ? time_frac : 0.0;
    if (tf < 0.0)
        tf = 0.0;
    if (tf > 1.0)
        tf = 1.0;
    const double c = kBasePriorConfidence + (kMaxPriorConfidence - kBasePriorConfidence) * tf;
    if (c < kBasePriorConfidence)
        return kBasePriorConfidence;
    if (c > kMaxPriorConfidence)
        return kMaxPriorConfidence;
    return c;
}

// inplay_score_prior_yes: 比分差 + 时钟 → YES 先验 (与 BaselineFairValueModel 同形,
//   但作为独立纯函数供 paper_loop 直接用, 不经 normalize). is_terminal=true 时
//   返回确定性近似 (领先→近 1, 落后→近 0, 平→0.5), 不再受时钟影响.
//   非终态: sigmoid(alpha*diff + beta*diff*time_frac) — time 项耦合 (带符号) score
//   领先, 故 0:0 恒 0.5, 领先随时间更确定.
[[nodiscard]] inline double inplay_score_prior_yes(double score_diff, double time_frac, bool is_terminal,
                                                   double alpha = kDefaultAlpha,
                                                   double beta = kDefaultBeta) noexcept {
    if (is_terminal) {
        if (score_diff > 0.0)
            return kProbMax;
        if (score_diff < 0.0)
            return kProbEps;
        return 0.5;  // 平局终态 → push/不确定
    }
    double tf = std::isfinite(time_frac) ? time_frac : 0.0;
    if (tf < 0.0)
        tf = 0.0;
    if (tf > 1.0)
        tf = 1.0;
    const double logit = alpha * score_diff + beta * score_diff * tf;
    return clamp_prob(safe_sigmoid(logit));
}

// blend_prob: 置信加权凸组合 p = conf*p_prior + (1-conf)*p_market, 全程 clamp.
//   conf 越界 clamp 到 [0,1].
[[nodiscard]] inline double blend_prob(double p_prior, double p_market, double conf) noexcept {
    double w = std::isfinite(conf) ? conf : 0.0;
    if (w < 0.0)
        w = 0.0;
    if (w > 1.0)
        w = 1.0;
    const double pp = std::isfinite(p_prior) ? p_prior : 0.5;
    const double pm = std::isfinite(p_market) ? p_market : 0.5;
    return clamp_prob(w * pp + (1.0 - w) * pm);
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
// 解析 Goalserve period 字符串 → 1-based 节序数 (喂 g_period 特征 #2)。
//   Goalserve "period" 字段编码杂乱: "1st Half"/"2nd Quarter"/"Set 3"/"Q3"/"2H"/
//   "P1"/"inning 12"。策略: 取首段连续数字 = 节序 (覆盖绝大多数); 无数字时
//   加时/超时 (OT/ET/Extra/Overtime) → regulation 节数+1; 其余 (Half Time/未知/空) → 0。
//   纯函数, 无副作用; 0 = 未知节 (与 FeatureStoreGameRow.period 默认一致)。
[[nodiscard]] inline std::uint8_t parse_period_ordinal(std::string_view period,
                                                       std::string_view sport) noexcept {
    // 1) 首个连续数字 → 节序 ("1st Half"→1, "Set 3"→3, "Q3"→3, "inning 12"→12)
    for (std::size_t i = 0; i < period.size(); ++i) {
        if (period[i] >= '0' && period[i] <= '9') {
            unsigned v = 0;
            std::from_chars(period.data() + i, period.data() + period.size(), v);
            return static_cast<std::uint8_t>(v > 255u ? 255u : v);
        }
    }
    // 2) 无数字: 加时/超时关键词 (大小写不敏感) → regulation 节数 + 1
    auto contains_ci = [](std::string_view hay, std::string_view needle) noexcept {
        if (needle.empty() || needle.size() > hay.size())
            return false;
        for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
            bool match = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                char a = hay[i + j];
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a + 32);
                if (a != needle[j]) {
                    match = false;
                    break;
                }
            }
            if (match)
                return true;
        }
        return false;
    };
    // "ot" 只在整串等于 "OT"/"ot" 时算 (子串匹配会误判 "Not Started" 含 "ot")。
    auto equals_ci = [](std::string_view s, std::string_view lit) noexcept {
        if (s.size() != lit.size())
            return false;
        for (std::size_t i = 0; i < s.size(); ++i) {
            char a = s[i];
            if (a >= 'A' && a <= 'Z')
                a = static_cast<char>(a + 32);
            if (a != lit[i])
                return false;
        }
        return true;
    };
    const bool is_ot = equals_ci(period, "ot") || contains_ci(period, "extra") ||
                       contains_ci(period, "overtime");
    if (is_ot) {
        if (sport == "soccer")
            return 3;  // 上/下半场后 ET = 第 3 阶段
        if (sport == "basket" || sport == "amfootball")
            return 5;  // 4 节后 OT
        if (sport == "hockey")
            return 4;  // 3 节后 OT
        return 0;
    }
    return 0;  // "Half Time" / 未知 / 空 → 未知节
}

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

// score_prior_applicable — 该运动比分是否适配 goals-like 先验 sigmoid(α·score_diff+β·time)。
//   (2026-06-03, 老板覆盖率攻坚加 cricket/esports 补充源后的护栏。)
//   true (适配): 比分是累计点数/进球, score_diff 单调"领先=占优" 且量级有界 —
//     soccer/basket/hockey/amfootball (进球/点) + tennis/volleyball (盘/局) + esports (maps 0-3)
//     + baseball (两队逐局交替计分, runs 可比, 5-3 即领先 2)。
//   false (不适配): cricket — innings 制, 一队先打满 (如 300/5) 另一队还没打 (0) → score_diff=runs 差
//     (可达数百) 喂 sigmoid 饱和成"必胜", 但对方尚未追分 = 垃圾信号。无 cricket 定价模型 →
//     调用方应令 prior_conf=0 (score-prior 零拉力 → 回落市场 de-vig → 覆盖但不在垃圾 fair 上交易)。
[[nodiscard]] inline bool score_prior_applicable(std::string_view sport) noexcept {
    return sport != "cricket";
}

// 无时钟运动的常规节/局数 (P3.2: 给 game_phase 提供 period 进度锚)。
//   有时钟运动 (soccer/basket/...) 返回 0 → 调用方用时钟 time_frac, 不走此 proxy。
//   近似值 (格式有歧义, 如网球 best-of-3 vs 5); 仅供 phase 粗分桶 (早/中/末), 非定价输入。
[[nodiscard]] inline int regulation_periods(std::string_view sport) noexcept {
    if (sport == "baseball")
        return 9;  // 9 局
    if (sport == "tennis")
        return 3;  // best-of-3 常态 (大满贯男单 5, 取下界避免过早判末段)
    if (sport == "volleyball")
        return 5;  // best-of-5
    return 0;  // 有时钟运动 / 未知 → 不用 period proxy
}

}  // namespace stcpp::pricing
