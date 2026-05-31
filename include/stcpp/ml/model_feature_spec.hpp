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
#include "stcpp/sizing/quote_snapshot_hub.hpp"  // v0.3: QuoteFeatures 源 (双边时序/持仓特征)

namespace stcpp::ml {

// ---------------------------------------------------------------------------
// kSpecVersion — 抽取契约版本. 列顺序 / 数量变更 → bump (ADR + 训练侧 retrain).
// ---------------------------------------------------------------------------
inline constexpr std::string_view kSpecVersion = "ml-feature-spec-v0.7";
//   v0.1 → v0.2 (2026-05-31, 老雷): append 6 列 (18..23) — inplay bet365 de-vig 赔率 +
//     5 live_stats 差 (危险进攻/射正/控球/红牌/角球)。源全在 FeatureStoreGameRow。
//   v0.2 → v0.3 (2026-05-31, 老雷): append 30 列 (24..53) — 双边时序微结构 (YES 24-33 +
//     NO 34-43) + 双边 L1 (44-47) + 双边持仓 (48-53)。源在 QuoteFeatures → extract_from_quote。
//   v0.3 → v0.4 (2026-05-31, 老雷): append 21 列 (54..74) — 剩余捕获信号全进 (老板「都要进」):
//     fee/数据质量/生命周期/cross-log-odds/sports 动态。排除决策输出/模型自身输出/provenance/
//     原始 ts/字符串主键/重复列。捕获层信号 = 契约层, 一个不剩。

// ---------------------------------------------------------------------------
// MlFeature — 抽取出来的 feature 列 (顺序锁死 = 训练 column index).
//
//   game 侧 (0..7)  : 来自 FeatureStoreGameRow (Goalserve 归一化)
//   book 侧 (8..15) : 来自 FeatureStoreBookRow  (Polymarket 归一化)
//   cross (16..17)  : game/book join 派生 (de-vig fair vs market mid 偏离)
//   v0.2  (18..23)  : inplay bet365 de-vig 赔率 + 5 live_stats 差 (game 侧; -1→NaN)
//
//   新增 feature: 只在末尾 append, 不插中间; 同步 bump kSpecVersion + 训练侧 retrain.
// ---------------------------------------------------------------------------
enum class MlFeature : std::uint8_t {
    // ---- game 侧 (Goalserve 归一化中间表示) ----
    g_score_diff = 0,        // home_total - away_total
    g_score_total = 1,       // home_total + away_total
    g_period = 2,            // 当前 period (0=未开始)
    g_elapsed_sec = 3,       // 当前节已用秒 (-1 -> NaN)
    g_time_status = 4,       // TimeStatus enum cast (NotStarted=0 ...)
    g_bm_devig_p_yes = 5,    // 跨 bookmaker de-vig 隐含 YES fair prob (均值)
    g_bm_overround_avg = 6,  // 跨 bookmaker overround 均值 (vig 强度)
    g_valid_bm_count = 7,    // 有效 bookmaker 报价家数 (信息量代理)

    // ---- book 侧 (Polymarket 归一化中间表示) ----
    b_mid = 8,                 // 订单簿 mid (dollar prob)
    b_microprice = 9,          // size-weighted microprice
    b_imbalance = 10,          // L1 imbalance (bid - ask) / (bid + ask)
    b_spread_bps = 11,         // (ask0 - bid0)/mid*10000
    b_top3_depth_usdc = 12,    // top-3 levels 总 USDC
    b_best_bid = 13,           // L1 best bid
    b_best_ask = 14,           // L1 best ask
    b_book_levels_valid = 15,  // 有效 (bid+ask) level 计数

    // ---- cross (game/book join 派生) ----
    x_devig_minus_mid = 16,       // g_bm_devig_p_yes - b_mid (模型 / 市场偏离)
    x_microprice_minus_mid = 17,  // b_microprice - b_mid (短期方向压力)

    // ---- v0.2 append (game 侧, FeatureStoreGameRow 源; -1→NaN; append-only 列序锁) ----
    g_bm_inplay_fair = 18,        // inplay bet365 单源 de-vig YES-canonical 胜率 (sharp live 锚)
    g_danger_attack_diff = 19,    // 危险进攻差 home-away (xG 代理; 待 livescore client)
    g_shot_on_target_diff = 20,   // 射正差 home-away
    g_possession_home = 21,       // 主队控球率 0-100
    g_red_card_diff = 22,         // 红牌差 home-away (红牌后胜率剧变)
    g_corner_diff = 23,           // 角球差 home-away

    // ---- v0.3 append (双边时序微结构 + 双边持仓; QuoteFeatures 源 → extract_from_quote) ----
    //   老板「双边信息都要有 / 各边买了多少」: YES+NO 时序微结构 + 双边持仓全进契约。
    //   YES 边时序微结构 (24-33; ts_history_ 派生)
    b_mp_roc_per_sec = 24,    // YES 微价变化率 (prob/sec)
    b_realized_vol = 25,      // YES realized vol
    b_bid_absence_frac = 26,  // YES「卖不出」占比
    b_exit_depth_mean = 27,   // YES 退出流动性 (best_bid_size 均值)
    b_amihud = 28,            // YES Amihud 流动性冲击
    b_bid_depth_vol = 29,     // YES bid 深度波动
    b_ofi = 30,               // YES order flow imbalance
    b_vol_ratio = 31,         // YES 短/长窗 vol 比 (制度切换)
    b_mp_roc_30s = 32,        // YES 30s 动量
    b_mp_roc_5m = 33,         // YES 5min 趋势
    //   NO 边时序微结构 (34-43; ts_history_no_ 派生; 独立信号非 YES 镜像)
    no_b_mp_roc_per_sec = 34,
    no_b_realized_vol = 35,
    no_b_bid_absence_frac = 36,
    no_b_exit_depth_mean = 37,
    no_b_amihud = 38,
    no_b_bid_depth_vol = 39,
    no_b_ofi = 40,
    no_b_vol_ratio = 41,
    no_b_mp_roc_30s = 42,
    no_b_mp_roc_5m = 43,
    //   双边 L1 微结构 (44-47)
    b_no_microprice = 44,  // NO 边 microprice
    b_cross_spread = 45,   // YES_ask + NO_ask − 1 (=vig; 双边定价健康度)
    b_yes_imbalance = 46,  // YES L1 簿口失衡
    b_no_imbalance = 47,   // NO  L1 簿口失衡
    //   双边持仓 (48-53; 库存感知; 各边各量, 可能两边都持)
    pos_yes_qty = 48,            // YES token 持仓量 (whole pUSD, signed)
    pos_no_qty = 49,             // NO  token 持仓量
    pos_yes_avg_entry = 50,      // YES 加权平均入场价
    pos_no_avg_entry = 51,       // NO  加权平均入场价
    pos_net_qty = 52,            // 净 YES 方向 = yes − no
    pos_condition_exposure = 53,  // 本 condition 净敞口 (whole pUSD)

    // ---- v0.4 append (剩余捕获信号特征全进; QuoteFeatures 源; 老板「都要进」) ----
    //   排除: 决策输出(kelly/notional/target/reservation)、模型自身输出(ml_advisory)、provenance、
    //   原始时间戳、字符串主键、重复列(market_mid/fair_value/edge_bps) — 见 commit 分类。
    fee_rate_coef = 54,            // per-market 手续费系数 (净 edge 影响, 各市场异)
    devig_ok = 55,                 // de-vig 成功 (0/1; 数据质量 → 双边 fair 可靠性)
    ts_window_samples = 56,        // YES 时序样本数 (质量代理)
    no_ts_window_samples = 57,     // NO 时序样本数
    time_to_resolution_frac = 58,  // 结算临近度 [0,1] (1=刚开赛 0=已结算)
    resolution_status = 59,        // 市场结算状态 0Open/1Resolving/2Resolved
    x_log_odds_fair = 60,          // logit(fair) (基线 fair 变换; 残差/ensemble 框架, 训练侧防泄漏)
    x_log_odds_edge = 61,          // logit(fair) − logit(mid) (log-odds 空间 edge)
    x_pin_risk = 62,               // min(fair, 1−fair) (距单边距离)
    x_pin_x_expiry = 63,           // pin_risk × time_to_resolution (归零陷阱)
    b_dislocation = 64,            // microprice − mid (买卖压力)
    g_time_x_lead = 65,            // score_diff × (1−time_frac) (时间感知领先)
    g_fld_signal = 66,             // devig_mult − devig_power (favorite-longshot 偏差)
    g_remaining_sec = 67,          // 剩余秒 (时间特征分母)
    g_periods_won_home = 68,       // 已完成节中主队领先节数
    g_periods_won_away = 69,
    g_game_phase = 70,             // 0早/1中/2末段 (非线性分段)
    g_garbage_time = 71,           // 垃圾时间 flag
    g_clutch = 72,                 // 关键时段 flag (末段比分接近)
    g_goal_freshness = 73,         // 进球新鲜度 exp(−Δt/120s)
    g_net_momentum_5m = 74,        // 最近 5min 净进球 (势头)

    // ---- v0.5 append (数据延迟/新鲜度, 双边独立; extract_full 从 4ts 派生 — 数据可信度感知) ----
    //   老板「数据是几秒/几毫秒前的」+「订单簿双边时间独立 (YES/NO 各自更新, 不同步)」。
    //   龄 = as_of(决策时刻) − data_source(上游采集)。R-20 守法: data_source 仍真上游, as_of 决策锚。
    b_book_age_sec = 75,           // YES book 数据龄秒 = as_of − yes_book.data_source_ts
    no_b_book_age_sec = 76,        // NO  book 数据龄秒 (双边独立! YES/NO WSS 各自推送, 龄不同)
    g_score_age_sec = 77,          // 比分数据龄秒 = as_of − score.data_source_ts
    x_yes_no_book_skew_sec = 78,   // YES vs NO book 新鲜度错位 = yes.ds − no.ds (双边更新不同步信号)
    b_ingestion_lag_ms = 79,       // YES book 传输延迟毫秒 = ingestion − data_source (跨洋链路)
    no_b_ingestion_lag_ms = 80,    // NO  book 传输延迟毫秒 (双边独立)
    x_joint_staleness_sec = 81,    // 联合最旧 = max(yes_age, no_age, score_age) (整体最弱环节)

    // ---- v0.6 append (类别上下文 categorical; 让单模型适配多盘口/多运动/多资产 — 老板 2026-05-31) ----
    //   老板「体育/加密/政治大类 + 篮球 NBA/CBA + 大小分/谁赢盘口类型 也传进去, 适配更好」。
    //   ⚠ categorical 非 ordinal: 训练侧须声明 LightGBM categorical_feature(见 train_fair_value.py),
    //      整数码仅作 level 标识 (Basketball=1 ≠ "比 Soccer=0 大"); unknown=-1 作独立 level。
    //   现状 (MVP 单盘口/体育): cat_sport 在 in-play 比分匹配时活, cat_asset_class 恒 Sports(占位扩展),
    //      cat_market_type 待 book market_type 注入接通前为 -1(占位)。append 占位 → 多盘口上线即生效,
    //      历史数据天然带列 (列序锁 append-only, 免日后回填)。
    cat_asset_class = 82,          // 资产大类: 0=Sports/1=Crypto/2=Politics/3=Esports (现恒 Sports)
    cat_sport = 83,                // 粗运动家族: soccer=0/basket=1/tennis=2/baseball=3/hockey=4/.../-1
    cat_market_type = 84,          // 盘口类型: moneyline=0/spread=1/totals=2/outright=3/prop=4/series=5/-1
    // v0.7: 细联赛级 (真实 Polymarket sport.id) — 老板「篮球?NBA 还是 CBA?」要的就是这粒度。
    cat_league = 85,               // 联赛 = Polymarket sport.id (nba=34/bkcba=104=CBA/atp=45/wta=46; -1 unk)
};

inline constexpr std::size_t kMlFeatureCount = 86;

[[nodiscard]] constexpr std::string_view to_string(MlFeature f) noexcept {
    switch (f) {
        case MlFeature::g_score_diff:
            return "g_score_diff";
        case MlFeature::g_score_total:
            return "g_score_total";
        case MlFeature::g_period:
            return "g_period";
        case MlFeature::g_elapsed_sec:
            return "g_elapsed_sec";
        case MlFeature::g_time_status:
            return "g_time_status";
        case MlFeature::g_bm_devig_p_yes:
            return "g_bm_devig_p_yes";
        case MlFeature::g_bm_overround_avg:
            return "g_bm_overround_avg";
        case MlFeature::g_valid_bm_count:
            return "g_valid_bm_count";
        case MlFeature::b_mid:
            return "b_mid";
        case MlFeature::b_microprice:
            return "b_microprice";
        case MlFeature::b_imbalance:
            return "b_imbalance";
        case MlFeature::b_spread_bps:
            return "b_spread_bps";
        case MlFeature::b_top3_depth_usdc:
            return "b_top3_depth_usdc";
        case MlFeature::b_best_bid:
            return "b_best_bid";
        case MlFeature::b_best_ask:
            return "b_best_ask";
        case MlFeature::b_book_levels_valid:
            return "b_book_levels_valid";
        case MlFeature::x_devig_minus_mid:
            return "x_devig_minus_mid";
        case MlFeature::x_microprice_minus_mid:
            return "x_microprice_minus_mid";
        case MlFeature::g_bm_inplay_fair:
            return "g_bm_inplay_fair";
        case MlFeature::g_danger_attack_diff:
            return "g_danger_attack_diff";
        case MlFeature::g_shot_on_target_diff:
            return "g_shot_on_target_diff";
        case MlFeature::g_possession_home:
            return "g_possession_home";
        case MlFeature::g_red_card_diff:
            return "g_red_card_diff";
        case MlFeature::g_corner_diff:
            return "g_corner_diff";
        case MlFeature::b_mp_roc_per_sec: return "b_mp_roc_per_sec";
        case MlFeature::b_realized_vol: return "b_realized_vol";
        case MlFeature::b_bid_absence_frac: return "b_bid_absence_frac";
        case MlFeature::b_exit_depth_mean: return "b_exit_depth_mean";
        case MlFeature::b_amihud: return "b_amihud";
        case MlFeature::b_bid_depth_vol: return "b_bid_depth_vol";
        case MlFeature::b_ofi: return "b_ofi";
        case MlFeature::b_vol_ratio: return "b_vol_ratio";
        case MlFeature::b_mp_roc_30s: return "b_mp_roc_30s";
        case MlFeature::b_mp_roc_5m: return "b_mp_roc_5m";
        case MlFeature::no_b_mp_roc_per_sec: return "no_b_mp_roc_per_sec";
        case MlFeature::no_b_realized_vol: return "no_b_realized_vol";
        case MlFeature::no_b_bid_absence_frac: return "no_b_bid_absence_frac";
        case MlFeature::no_b_exit_depth_mean: return "no_b_exit_depth_mean";
        case MlFeature::no_b_amihud: return "no_b_amihud";
        case MlFeature::no_b_bid_depth_vol: return "no_b_bid_depth_vol";
        case MlFeature::no_b_ofi: return "no_b_ofi";
        case MlFeature::no_b_vol_ratio: return "no_b_vol_ratio";
        case MlFeature::no_b_mp_roc_30s: return "no_b_mp_roc_30s";
        case MlFeature::no_b_mp_roc_5m: return "no_b_mp_roc_5m";
        case MlFeature::b_no_microprice: return "b_no_microprice";
        case MlFeature::b_cross_spread: return "b_cross_spread";
        case MlFeature::b_yes_imbalance: return "b_yes_imbalance";
        case MlFeature::b_no_imbalance: return "b_no_imbalance";
        case MlFeature::pos_yes_qty: return "pos_yes_qty";
        case MlFeature::pos_no_qty: return "pos_no_qty";
        case MlFeature::pos_yes_avg_entry: return "pos_yes_avg_entry";
        case MlFeature::pos_no_avg_entry: return "pos_no_avg_entry";
        case MlFeature::pos_net_qty: return "pos_net_qty";
        case MlFeature::pos_condition_exposure: return "pos_condition_exposure";
        case MlFeature::fee_rate_coef: return "fee_rate_coef";
        case MlFeature::devig_ok: return "devig_ok";
        case MlFeature::ts_window_samples: return "ts_window_samples";
        case MlFeature::no_ts_window_samples: return "no_ts_window_samples";
        case MlFeature::time_to_resolution_frac: return "time_to_resolution_frac";
        case MlFeature::resolution_status: return "resolution_status";
        case MlFeature::x_log_odds_fair: return "x_log_odds_fair";
        case MlFeature::x_log_odds_edge: return "x_log_odds_edge";
        case MlFeature::x_pin_risk: return "x_pin_risk";
        case MlFeature::x_pin_x_expiry: return "x_pin_x_expiry";
        case MlFeature::b_dislocation: return "b_dislocation";
        case MlFeature::g_time_x_lead: return "g_time_x_lead";
        case MlFeature::g_fld_signal: return "g_fld_signal";
        case MlFeature::g_remaining_sec: return "g_remaining_sec";
        case MlFeature::g_periods_won_home: return "g_periods_won_home";
        case MlFeature::g_periods_won_away: return "g_periods_won_away";
        case MlFeature::g_game_phase: return "g_game_phase";
        case MlFeature::g_garbage_time: return "g_garbage_time";
        case MlFeature::g_clutch: return "g_clutch";
        case MlFeature::g_goal_freshness: return "g_goal_freshness";
        case MlFeature::g_net_momentum_5m: return "g_net_momentum_5m";
        case MlFeature::b_book_age_sec: return "b_book_age_sec";
        case MlFeature::no_b_book_age_sec: return "no_b_book_age_sec";
        case MlFeature::g_score_age_sec: return "g_score_age_sec";
        case MlFeature::x_yes_no_book_skew_sec: return "x_yes_no_book_skew_sec";
        case MlFeature::b_ingestion_lag_ms: return "b_ingestion_lag_ms";
        case MlFeature::no_b_ingestion_lag_ms: return "no_b_ingestion_lag_ms";
        case MlFeature::x_joint_staleness_sec: return "x_joint_staleness_sec";
        case MlFeature::cat_asset_class: return "cat_asset_class";
        case MlFeature::cat_sport: return "cat_sport";
        case MlFeature::cat_market_type: return "cat_market_type";
        case MlFeature::cat_league: return "cat_league";
    }
    return "unknown";
}

namespace detail {

inline constexpr float kNaNf = std::numeric_limits<float>::quiet_NaN();

[[nodiscard]] inline bool is_finite_f(float v) noexcept {
    return v == v;
}  // NaN != NaN

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
    if (!(odds_yes > 1.0) || !(odds_no > 1.0))
        return r;  // decimal odds 必 > 1
    const double py = 1.0 / odds_yes;
    const double pn = 1.0 / odds_no;
    const double over = py + pn;
    if (!(over > 0.0))
        return r;
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

    put(MlFeature::g_score_diff, static_cast<float>(g.score_home_total - g.score_away_total));
    put(MlFeature::g_score_total, static_cast<float>(g.score_home_total + g.score_away_total));
    put(MlFeature::g_period, static_cast<float>(g.period));
    put(MlFeature::g_elapsed_sec, (g.elapsed_sec >= 0) ? static_cast<float>(g.elapsed_sec) : kNaNf);
    put(MlFeature::g_time_status, static_cast<float>(static_cast<std::uint8_t>(g.time_status)));

    // 跨 bookmaker de-vig fair prob 均值 + overround 均值 (vendor-agnostic: 走归一化 bm_slots).
    double fair_sum = 0.0, over_sum = 0.0;
    std::size_t n = 0;
    for (const auto& sl : g.bm_slots) {
        if (!sl.is_present())
            continue;
        const auto d = detail::devig_one(sl.odds_yes, sl.odds_no);
        if (!d.ok)
            continue;
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

    // ---- v0.2: inplay 赔率 + live_stats 差 (语义逐位对齐 paper_loop SportsFeatures; -1→NaN) ----
    //   sdiff(h,a) = (h>=0 && a>=0) ? h-a : NaN — 任一缺数据则该差 NaN (与捕获列一致)。
    auto sdiff = [](std::int32_t h, std::int32_t a) noexcept -> float {
        return (h >= 0 && a >= 0) ? static_cast<float>(h - a) : kNaNf;
    };
    put(MlFeature::g_bm_inplay_fair,
        (g.inplay_bet365_home_fair >= 0.0) ? static_cast<float>(g.inplay_bet365_home_fair) : kNaNf);
    put(MlFeature::g_danger_attack_diff,
        sdiff(g.soccer_dangerous_attacks_home, g.soccer_dangerous_attacks_away));
    put(MlFeature::g_shot_on_target_diff,
        sdiff(g.soccer_shots_on_target_home, g.soccer_shots_on_target_away));
    put(MlFeature::g_possession_home,
        (g.soccer_possession_home_pct >= 0) ? static_cast<float>(g.soccer_possession_home_pct) : kNaNf);
    put(MlFeature::g_red_card_diff, sdiff(g.soccer_red_cards_home, g.soccer_red_cards_away));
    put(MlFeature::g_corner_diff, sdiff(g.soccer_corners_home, g.soccer_corners_away));
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
        if (b.bid_level_valid(i))
            ++valid_levels;
        if (b.ask_level_valid(i))
            ++valid_levels;
    }
    put(MlFeature::b_book_levels_valid, static_cast<float>(valid_levels));
}

// ---------------------------------------------------------------------------
// fill_cross_features — 填 cross feature (索引 16..17) 基于已填的 game/book 列.
//   out 中 g_bm_devig_p_yes / b_mid / b_microprice 必须已填; 任一 NaN → cross NaN.
// ---------------------------------------------------------------------------
inline void fill_cross_features(std::vector<float>& out) noexcept {
    using detail::is_finite_f;
    using detail::kNaNf;
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

// ---------------------------------------------------------------------------
// extract_from_quote — 填 v0.3 列 (24..53) 从 QuoteFeatures (双边时序微结构 + 双边持仓)。
//   源是在线决策快照 QuoteFeatures (FeatureRecorder 落盘同源 → 训练料=推理向量, 零漂移)。
//   out 须已 resize 到 kMlFeatureCount。NaN 透传。
// ---------------------------------------------------------------------------
inline void extract_from_quote(const stcpp::sizing::QuoteFeatures& q, std::vector<float>& out) noexcept {
    using detail::kNaNf;
    auto put = [&out](MlFeature f, double v) noexcept {
        out[static_cast<std::size_t>(f)] = (v == v) ? static_cast<float>(v) : kNaNf;  // NaN 透传
    };
    // YES 边时序微结构 (24-33)
    put(MlFeature::b_mp_roc_per_sec, q.mp_roc_per_sec);
    put(MlFeature::b_realized_vol, q.realized_vol);
    put(MlFeature::b_bid_absence_frac, q.bid_absence_frac);
    put(MlFeature::b_exit_depth_mean, q.exit_depth_mean);
    put(MlFeature::b_amihud, q.b_amihud);
    put(MlFeature::b_bid_depth_vol, q.b_bid_depth_vol);
    put(MlFeature::b_ofi, q.b_ofi);
    put(MlFeature::b_vol_ratio, q.b_vol_ratio);
    put(MlFeature::b_mp_roc_30s, q.b_mp_roc_30s);
    put(MlFeature::b_mp_roc_5m, q.b_mp_roc_5m);
    // NO 边时序微结构 (34-43)
    put(MlFeature::no_b_mp_roc_per_sec, q.no_mp_roc_per_sec);
    put(MlFeature::no_b_realized_vol, q.no_realized_vol);
    put(MlFeature::no_b_bid_absence_frac, q.no_bid_absence_frac);
    put(MlFeature::no_b_exit_depth_mean, q.no_exit_depth_mean);
    put(MlFeature::no_b_amihud, q.no_b_amihud);
    put(MlFeature::no_b_bid_depth_vol, q.no_b_bid_depth_vol);
    put(MlFeature::no_b_ofi, q.no_b_ofi);
    put(MlFeature::no_b_vol_ratio, q.no_b_vol_ratio);
    put(MlFeature::no_b_mp_roc_30s, q.no_b_mp_roc_30s);
    put(MlFeature::no_b_mp_roc_5m, q.no_b_mp_roc_5m);
    // 双边 L1 微结构 (44-47)
    put(MlFeature::b_no_microprice, q.no_microprice);
    put(MlFeature::b_cross_spread, q.cross_spread);
    put(MlFeature::b_yes_imbalance, q.yes_imbalance);
    put(MlFeature::b_no_imbalance, q.no_imbalance);
    // 双边持仓 (48-53)
    put(MlFeature::pos_yes_qty, q.pos_yes_qty);
    put(MlFeature::pos_no_qty, q.pos_no_qty);
    put(MlFeature::pos_yes_avg_entry, q.pos_yes_avg_entry);
    put(MlFeature::pos_no_avg_entry, q.pos_no_avg_entry);
    put(MlFeature::pos_net_qty, q.pos_net_qty);
    put(MlFeature::pos_condition_exposure, q.pos_condition_exposure_usdc);
    // v0.4: 剩余捕获信号 (手续费/数据质量/生命周期/cross-log-odds/sports 动态)。
    put(MlFeature::fee_rate_coef, q.fee_rate_coef);
    put(MlFeature::devig_ok, q.devig_ok ? 1.0 : 0.0);
    put(MlFeature::ts_window_samples, static_cast<double>(q.ts_window_samples));
    put(MlFeature::no_ts_window_samples, static_cast<double>(q.no_ts_window_samples));
    put(MlFeature::time_to_resolution_frac, q.time_to_resolution_frac);
    put(MlFeature::resolution_status, static_cast<double>(q.resolution_status));
    put(MlFeature::x_log_odds_fair, q.x_log_odds_fair);
    put(MlFeature::x_log_odds_edge, q.x_log_odds_edge);
    put(MlFeature::x_pin_risk, q.x_pin_risk);
    put(MlFeature::x_pin_x_expiry, q.x_pin_x_expiry);
    put(MlFeature::b_dislocation, q.b_dislocation);
    put(MlFeature::g_time_x_lead, q.g_time_x_lead);
    put(MlFeature::g_fld_signal, q.g_fld_signal);
    put(MlFeature::g_remaining_sec, q.g_remaining_sec);
    put(MlFeature::g_periods_won_home, static_cast<double>(q.g_periods_won_home));
    put(MlFeature::g_periods_won_away, static_cast<double>(q.g_periods_won_away));
    put(MlFeature::g_game_phase, q.g_game_phase);
    put(MlFeature::g_garbage_time, q.g_garbage_time);
    put(MlFeature::g_clutch, q.g_clutch);
    put(MlFeature::g_goal_freshness, q.g_goal_freshness);
    put(MlFeature::g_net_momentum_5m, q.g_net_momentum_5m);
}

// ---------------------------------------------------------------------------
// fill_latency_features — 数据延迟/新鲜度列 (75-81; 双边 book 独立)。
//   龄 = as_of(决策时刻) − data_source(上游采集)。R-20 守法 (data_source 真上游, as_of 决策锚)。
//   YES book ts ← book_row; NO book ts ← qf (双边 WSS 各自推送, 时间独立); score ts ← game_row。
// ---------------------------------------------------------------------------
inline void fill_latency_features(const stcpp::data::feature_store::FeatureStoreGameRow& g,
                                  const stcpp::data::feature_store::FeatureStoreBookRow& b,
                                  const stcpp::sizing::QuoteFeatures& q,
                                  std::vector<float>& out) noexcept {
    const float nanf = detail::kNaNf;
    const double dnan = std::numeric_limits<double>::quiet_NaN();
    auto put = [&out, nanf](MlFeature f, double v) noexcept {
        out[static_cast<std::size_t>(f)] = (v == v) ? static_cast<float>(v) : nanf;
    };
    const std::int64_t as_of = q.as_of_ts_ns;
    auto age_s = [as_of, dnan](std::int64_t ds) -> double {
        return (as_of > 0 && ds > 0 && as_of >= ds) ? static_cast<double>(as_of - ds) / 1e9 : dnan;
    };
    auto lag_ms = [dnan](std::int64_t ing, std::int64_t ds) -> double {
        return (ing > 0 && ds > 0 && ing >= ds) ? static_cast<double>(ing - ds) / 1e6 : dnan;
    };
    const double yes_age = age_s(b.data_source_ts_ns);          // YES book (book_row)
    const double no_age = age_s(q.no_book_data_source_ts_ns);   // NO book (经 qf 传, 双边独立)
    const double score_age = age_s(g.data_source_ts_ns);        // 比分 (game_row)
    put(MlFeature::b_book_age_sec, yes_age);
    put(MlFeature::no_b_book_age_sec, no_age);
    put(MlFeature::g_score_age_sec, score_age);
    put(MlFeature::x_yes_no_book_skew_sec,
        (b.data_source_ts_ns > 0 && q.no_book_data_source_ts_ns > 0)
            ? static_cast<double>(b.data_source_ts_ns - q.no_book_data_source_ts_ns) / 1e9
            : dnan);  // 正 = YES book 更旧 / NO 更新 (双边更新不同步)
    put(MlFeature::b_ingestion_lag_ms, lag_ms(b.ingestion_ts_ns, b.data_source_ts_ns));
    put(MlFeature::no_b_ingestion_lag_ms, lag_ms(q.no_book_ingestion_ts_ns, q.no_book_data_source_ts_ns));
    double joint = -1.0;  // 联合最旧 = max(三者中有限的)
    for (double a : {yes_age, no_age, score_age}) {
        if (a == a && a > joint) joint = a;
    }
    put(MlFeature::x_joint_staleness_sec, joint >= 0.0 ? joint : dnan);
}

// ---------------------------------------------------------------------------
// fill_categorical_context — 类别上下文列 (82-85; 让单模型适配多盘口/运动/联赛/资产)。
//   v0.7: 码来自 QuoteFeatures 载体 (真实 Polymarket 市场结构, paper_loop 从 per-condition map 查填;
//   映射逻辑在 data/market_taxonomy.hpp app 层算一次)。categorical 非 ordinal, unknown=-1 独立 level。
//   cat_league = Polymarket sport.id (nba=34/bkcba=104), 天然区分 NBA vs CBA — 老板要的细粒度。
// ---------------------------------------------------------------------------
inline void fill_categorical_context(const stcpp::sizing::QuoteFeatures& q,
                                     std::vector<float>& out) noexcept {
    out[static_cast<std::size_t>(MlFeature::cat_asset_class)] = static_cast<float>(q.cat_asset_class_id);
    out[static_cast<std::size_t>(MlFeature::cat_sport)] = static_cast<float>(q.cat_sport_family_id);
    out[static_cast<std::size_t>(MlFeature::cat_market_type)] = static_cast<float>(q.cat_market_type_id);
    out[static_cast<std::size_t>(MlFeature::cat_league)] = static_cast<float>(q.cat_league_id);
}

// ---------------------------------------------------------------------------
// extract_full — game_row + book_row (0..23) + QuoteFeatures (24..53) → 完整 FeatureVector。
//   在线推理用 (paper_loop): qf 含双边时序/持仓, game_row/book_row 含原始 game/book 列。
// ---------------------------------------------------------------------------
[[nodiscard]] inline FeatureVector extract_full(
    const stcpp::data::feature_store::FeatureStoreGameRow& g,
    const stcpp::data::feature_store::FeatureStoreBookRow& b,
    const stcpp::sizing::QuoteFeatures& q) noexcept {
    FeatureVector fv;
    fv.spec_version = kSpecVersion;
    fv.values.assign(kMlFeatureCount, detail::kNaNf);
    extract_from_game_row(g, fv.values);   // 0-7, 18-23
    extract_from_book_row(b, fv.values);   // 8-15
    fill_cross_features(fv.values);        // 16-17
    extract_from_quote(q, fv.values);      // 24-74 (双边时序/持仓/cross/sports/fee/resolution)
    fill_latency_features(g, b, q, fv.values);  // 75-81 (数据延迟/新鲜度, 双边 book 独立)
    fill_categorical_context(q, fv.values);     // 82-85 (类别上下文: 资产/运动/盘口/联赛)
    fv.as_of_ts_ns = (g.as_of_ts_ns > b.as_of_ts_ns) ? g.as_of_ts_ns : b.as_of_ts_ns;
    return fv;
}

// ---- 编译期列序锁 ----
static_assert(kMlFeatureCount == 86, "MlFeature count must be 86 (v0.7; append + bump spec)");
static_assert(static_cast<std::size_t>(MlFeature::cat_league) == kMlFeatureCount - 1,
              "最后一列必须是 cat_league (append-only 约束; v0.7 末列)");
static_assert(static_cast<std::size_t>(MlFeature::cat_market_type) == 84,
              "cat_market_type 必须恒为 84 (v0.6 末列, append 后不得移位)");
static_assert(static_cast<std::size_t>(MlFeature::x_joint_staleness_sec) == 81,
              "x_joint_staleness_sec 必须恒为 81 (v0.5 末列, append 后不得移位)");
// append-only 不变量: 旧列 index 永不变 (训练 column index 锁死)。
static_assert(static_cast<std::size_t>(MlFeature::x_microprice_minus_mid) == 17,
              "x_microprice_minus_mid 必须恒为 17 (v0.1 末列, append 后不得移位)");
static_assert(static_cast<std::size_t>(MlFeature::g_corner_diff) == 23,
              "g_corner_diff 必须恒为 23 (v0.2 末列, append 后不得移位)");
static_assert(static_cast<std::size_t>(MlFeature::pos_condition_exposure) == 53,
              "pos_condition_exposure 必须恒为 53 (v0.3 末列, append 后不得移位)");
static_assert(static_cast<std::size_t>(MlFeature::g_net_momentum_5m) == 74,
              "g_net_momentum_5m 必须恒为 74 (v0.4 末列, append 后不得移位)");

}  // namespace stcpp::ml
