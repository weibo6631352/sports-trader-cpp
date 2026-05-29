// stcpp/ml/model_feature_spec.hpp — ModelFeatureSpec v0.1 (ADR-037 骨架, 小邓 #31)
//
// Owner: 小邓 (ml-advisor, #31)
// last_review: 2026-05-29
//
// 目的:
//   定义"从 feature_store 中间表示 → 模型输入 FeatureVector"的抽取契约.
//   列顺序锁死 (训练侧 column index = enum 值), 与 FairValueModel 期望维度一致.
//   *vendor-agnostic*: 只读 FeatureStoreGameRow / FeatureStoreBookRow (归一化中间表示),
//   绝不碰原始 Goalserve GameRecord / Polymarket OrderBookSnapshot 字段 (红线).
//
// 关联:
//   include/stcpp/data/feature_store_contract.hpp   (唯一输入源)
//   include/stcpp/ml/fair_value_model.hpp            (FeatureVector 消费者)
//
// 红线:
//   vendor-agnostic — 仅 feature_store 中间表示; 不 #include goalserve_record / orderbook 原始字段
//   ML-R5  抽取层不依赖 Python; 纯 C++ POD 计算
//   ML-R8  FeatureVector.as_of_ts_ns 来自 row (PIT 锚, 禁本地 now())
//   PIT    抽取只读 row 已有字段, 不回填 / 不前视
//   列序锁 — MlFeature enum 值 = 训练 column index; 新增只 append, 不插中间 (bump kSpecVersion)
//
// 设计:
//   - MlFeature enum: 锁定列顺序 (game 侧 + book 侧合并 flat 向量).
//   - extract_from_game_row(): Goalserve 侧 feature (比分 / 状态 / de-vig 赔率派生).
//   - extract_from_book_row(): Polymarket 侧 feature (microprice / imbalance / spread / 深度).
//   - extract_joined(): game + book 合并成完整 FeatureVector (PIT 取较晚 as_of_ts).
//   - 缺失填 NaN (与 fair_value_model FeatureVector NaN 语义一致).
//
// 不耻下问:
//   - 跨 market_type feature 是否分模型 (Moneyline / Totals 各一模型?) @小梁
//   - 时间衰减 / rolling 窗口 feature 工程 @小程 (signal context)
//   - book row 的 sport/market_type 注入路径 @小冯 (FS-02)

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/ml/fair_value_model.hpp"

namespace stcpp::ml {

// ---------------------------------------------------------------------------
// kSpecVersion — 抽取契约版本. 列顺序 / 数量变更 → bump (ADR + 训练侧 retrain).
// ---------------------------------------------------------------------------
inline constexpr std::string_view kSpecVersion = "ml-feature-spec-v0.1";

// ---------------------------------------------------------------------------
// MlFeature — 抽取出来的 feature 列 (顺序锁死 = 训练 column index).
//
//   game 侧 (0..7)  : 来自 FeatureStoreGameRow (Goalserve 归一化)
//   book 侧 (8..15) : 来自 FeatureStoreBookRow  (Polymarket 归一化)
//   cross (16..17)  : game/book join 派生 (de-vig fair vs market mid 偏离)
//
//   新增 feature: 只在末尾 append, 不插中间; 同步 bump kSpecVersion + 训练侧 retrain.
// ---------------------------------------------------------------------------
enum class MlFeature : std::uint8_t {
    // ---- game 侧 (Goalserve 归一化中间表示) ----
    g_score_diff            = 0,   // home_total - away_total
    g_score_total           = 1,   // home_total + away_total
    g_period                = 2,   // 当前 period (0=未开始)
    g_elapsed_sec           = 3,   // 当前节已用秒 (-1 -> NaN)
    g_time_status           = 4,   // TimeStatus enum cast (NotStarted=0 ...)
    g_bm_devig_p_yes        = 5,   // 跨 bookmaker de-vig 隐含 YES fair prob (均值)
    g_bm_overround_avg      = 6,   // 跨 bookmaker overround 均值 (vig 强度)
    g_valid_bm_count        = 7,   // 有效 bookmaker 报价家数 (信息量代理)

    // ---- book 侧 (Polymarket 归一化中间表示) ----
    b_mid                   = 8,   // 订单簿 mid (dollar prob)
    b_microprice            = 9,   // size-weighted microprice
    b_imbalance             = 10,  // L1 imbalance (bid - ask) / (bid + ask)
    b_spread_bps            = 11,  // (ask0 - bid0)/mid*10000
    b_top3_depth_usdc       = 12,  // top-3 levels 总 USDC
    b_best_bid              = 13,  // L1 best bid
    b_best_ask              = 14,  // L1 best ask
    b_book_levels_valid     = 15,  // 有效 (bid+ask) level 计数

    // ---- cross (game/book join 派生) ----
    x_devig_minus_mid       = 16,  // g_bm_devig_p_yes - b_mid (模型 / 市场偏离)
    x_microprice_minus_mid  = 17,  // b_microprice - b_mid (短期方向压力)
};

inline constexpr std::size_t kMlFeatureCount = 18;

[[nodiscard]] constexpr std::string_view to_string(MlFeature f) noexcept {
    switch (f) {
        case MlFeature::g_score_diff:           return "g_score_diff";
        case MlFeature::g_score_total:          return "g_score_total";
        case MlFeature::g_period:               return "g_period";
        case MlFeature::g_elapsed_sec:          return "g_elapsed_sec";
        case MlFeature::g_time_status:          return "g_time_status";
        case MlFeature::g_bm_devig_p_yes:       return "g_bm_devig_p_yes";
        case MlFeature::g_bm_overround_avg:     return "g_bm_overround_avg";
        case MlFeature::g_valid_bm_count:       return "g_valid_bm_count";
        case MlFeature::b_mid:                  return "b_mid";
        case MlFeature::b_microprice:           return "b_microprice";
        case MlFeature::b_imbalance:            return "b_imbalance";
        case MlFeature::b_spread_bps:           return "b_spread_bps";
        case MlFeature::b_top3_depth_usdc:      return "b_top3_depth_usdc";
        case MlFeature::b_best_bid:             return "b_best_bid";
        case MlFeature::b_best_ask:             return "b_best_ask";
        case MlFeature::b_book_levels_valid:    return "b_book_levels_valid";
        case MlFeature::x_devig_minus_mid:      return "x_devig_minus_mid";
        case MlFeature::x_microprice_minus_mid: return "x_microprice_minus_mid";
    }
    return "unknown";
}

namespace detail {

inline constexpr float kNaNf = std::numeric_limits<float>::quiet_NaN();

[[nodiscard]] inline bool is_finite_f(float v) noexcept { return v == v; }  // NaN != NaN

// 单家 odds (decimal-style yes/no) → de-vig YES fair prob.
//   隐含 p_yes_raw = 1/odds_yes, p_no_raw = 1/odds_no; overround = sum.
//   de-vig (multiplicative): p_yes_fair = p_yes_raw / overround.
//   返回 {fair_yes, overround}; 任一无效 → {NaN, NaN}.
struct DevigOne {
    double fair_yes = std::numeric_limits<double>::quiet_NaN();
    double overround = std::numeric_limits<double>::quiet_NaN();
    bool ok = false;
};

[[nodiscard]] inline DevigOne devig_one(double odds_yes, double odds_no) noexcept {
    DevigOne r;
    if (!(odds_yes > 1.0) || !(odds_no > 1.0)) return r;  // decimal odds 必 > 1
    const double py = 1.0 / odds_yes;
    const double pn = 1.0 / odds_no;
    const double over = py + pn;
    if (!(over > 0.0)) return r;
    r.fair_yes = py / over;
    r.overround = over;
    r.ok = true;
    return r;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// extract_from_game_row — 填 game 侧 feature (索引 0..7) 到 out 向量.
//   out 必须已 resize 到 kMlFeatureCount. 缺失填 NaN. 不读原始 vendor 字段.
// ---------------------------------------------------------------------------
inline void extract_from_game_row(const stcpp::data::feature_store::FeatureStoreGameRow& g,
                                  std::vector<float>& out) noexcept {
    using detail::kNaNf;
    auto put = [&out](MlFeature f, float v) noexcept {
        out[static_cast<std::size_t>(f)] = v;
    };

    put(MlFeature::g_score_diff,
        static_cast<float>(g.score_home_total - g.score_away_total));
    put(MlFeature::g_score_total,
        static_cast<float>(g.score_home_total + g.score_away_total));
    put(MlFeature::g_period, static_cast<float>(g.period));
    put(MlFeature::g_elapsed_sec,
        (g.elapsed_sec >= 0) ? static_cast<float>(g.elapsed_sec) : kNaNf);
    put(MlFeature::g_time_status,
        static_cast<float>(static_cast<std::uint8_t>(g.time_status)));

    // 跨 bookmaker de-vig fair prob 均值 + overround 均值 (vendor-agnostic: 走归一化 bm_slots).
    double fair_sum = 0.0, over_sum = 0.0;
    std::size_t n = 0;
    for (const auto& sl : g.bm_slots) {
        if (!sl.is_present()) continue;
        const auto d = detail::devig_one(sl.odds_yes, sl.odds_no);
        if (!d.ok) continue;
        fair_sum += d.fair_yes;
        over_sum += d.overround;
        ++n;
    }
    if (n > 0) {
        put(MlFeature::g_bm_devig_p_yes, static_cast<float>(fair_sum / static_cast<double>(n)));
        put(MlFeature::g_bm_overround_avg, static_cast<float>(over_sum / static_cast<double>(n)));
    } else {
        put(MlFeature::g_bm_devig_p_yes, kNaNf);
        put(MlFeature::g_bm_overround_avg, kNaNf);
    }
    put(MlFeature::g_valid_bm_count, static_cast<float>(g.valid_bm_count()));
}

// ---------------------------------------------------------------------------
// extract_from_book_row — 填 book 侧 feature (索引 8..15) 到 out 向量.
//   缺失 (NaN microprice 等) 直接透传 NaN. 不读原始 OrderBookSnapshot.
// ---------------------------------------------------------------------------
inline void extract_from_book_row(const stcpp::data::feature_store::FeatureStoreBookRow& b,
                                  std::vector<float>& out) noexcept {
    using detail::kNaNf;
    auto put = [&out](MlFeature f, float v) noexcept {
        out[static_cast<std::size_t>(f)] = v;
    };
    auto f32 = [](double d) noexcept -> float {
        return (d == d) ? static_cast<float>(d) : kNaNf;  // NaN 透传
    };

    put(MlFeature::b_mid, f32(b.mid));
    put(MlFeature::b_microprice, f32(b.microprice));
    put(MlFeature::b_imbalance, f32(b.imbalance));
    put(MlFeature::b_spread_bps, f32(b.spread_bps_f));
    put(MlFeature::b_top3_depth_usdc, f32(b.top3_depth_usdc));
    put(MlFeature::b_best_bid, f32(b.best_bid()));
    put(MlFeature::b_best_ask, f32(b.best_ask()));

    std::size_t valid_levels = 0;
    for (std::size_t i = 0; i < stcpp::data::feature_store::kOrderBookLevels; ++i) {
        if (b.bid_level_valid(i)) ++valid_levels;
        if (b.ask_level_valid(i)) ++valid_levels;
    }
    put(MlFeature::b_book_levels_valid, static_cast<float>(valid_levels));
}

// ---------------------------------------------------------------------------
// fill_cross_features — 填 cross feature (索引 16..17) 基于已填的 game/book 列.
//   out 中 g_bm_devig_p_yes / b_mid / b_microprice 必须已填; 任一 NaN → cross NaN.
// ---------------------------------------------------------------------------
inline void fill_cross_features(std::vector<float>& out) noexcept {
    using detail::kNaNf;
    using detail::is_finite_f;
    const float devig = out[static_cast<std::size_t>(MlFeature::g_bm_devig_p_yes)];
    const float mid = out[static_cast<std::size_t>(MlFeature::b_mid)];
    const float micro = out[static_cast<std::size_t>(MlFeature::b_microprice)];

    out[static_cast<std::size_t>(MlFeature::x_devig_minus_mid)] =
        (is_finite_f(devig) && is_finite_f(mid)) ? (devig - mid) : kNaNf;
    out[static_cast<std::size_t>(MlFeature::x_microprice_minus_mid)] =
        (is_finite_f(micro) && is_finite_f(mid)) ? (micro - mid) : kNaNf;
}

// ---------------------------------------------------------------------------
// extract_joined — game + book 合并成完整 FeatureVector (kMlFeatureCount 维).
//   PIT 锚 as_of_ts: 取两 row 中较晚的 as_of_ts_ns (决策时刻是后到的数据决定).
//   缺失列保持 NaN (上游未填的). 用于 FairValueModel.predict() 输入.
// ---------------------------------------------------------------------------
[[nodiscard]] inline FeatureVector extract_joined(
    const stcpp::data::feature_store::FeatureStoreGameRow& g,
    const stcpp::data::feature_store::FeatureStoreBookRow& b) noexcept {
    FeatureVector fv;
    fv.spec_version = kSpecVersion;
    fv.values.assign(kMlFeatureCount, detail::kNaNf);

    extract_from_game_row(g, fv.values);
    extract_from_book_row(b, fv.values);
    fill_cross_features(fv.values);

    // PIT: 取较晚的 as_of_ts (后到数据决定决策时刻).
    fv.as_of_ts_ns = (g.as_of_ts_ns > b.as_of_ts_ns) ? g.as_of_ts_ns : b.as_of_ts_ns;
    return fv;
}

// ---------------------------------------------------------------------------
// extract_game_only — 只有 game row (pregame 无订单簿) 时的抽取. book 侧留 NaN.
// ---------------------------------------------------------------------------
[[nodiscard]] inline FeatureVector extract_game_only(
    const stcpp::data::feature_store::FeatureStoreGameRow& g) noexcept {
    FeatureVector fv;
    fv.spec_version = kSpecVersion;
    fv.values.assign(kMlFeatureCount, detail::kNaNf);
    extract_from_game_row(g, fv.values);
    fill_cross_features(fv.values);  // x_* 会因 book 列 NaN 而 NaN, 符合预期
    fv.as_of_ts_ns = g.as_of_ts_ns;
    return fv;
}

// ---- 编译期列序锁 ----
static_assert(kMlFeatureCount == 18, "MlFeature count must be 18 (列序锁; 新增 append + bump spec)");
static_assert(static_cast<std::size_t>(MlFeature::x_microprice_minus_mid) == kMlFeatureCount - 1,
              "最后一列必须是 x_microprice_minus_mid (append-only 约束)");

}  // namespace stcpp::ml
