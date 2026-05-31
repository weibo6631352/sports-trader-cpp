// tests/unit/test_fair_value_model.cpp — FairValueModel + feature 抽取契约单测 (ADR-037, 小邓 #31)
//
// 覆盖:
//   A. ModelFeatureSpec 列序锁 + 抽取 (game / book / joined / cross)
//   B. FairValueModel 接口契约 (维度校验 / 归一化 / NaN 处理 / 确定性)
//   C. ONNX 适配点 (工厂当前返回 nullptr → 回落 stub)
//
// 红线验证:
//   ML-R8  ModelPrediction 带 model_id + as_of_ts_ns
//   PIT    extract_joined 取较晚 as_of_ts
//   vendor-agnostic 抽取只读 feature_store 中间表示

#include <cmath>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/ml/fair_value_model.hpp"
#include "stcpp/ml/model_feature_spec.hpp"
#include "stcpp/data/market_taxonomy.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"

namespace {

using stcpp::data::feature_store::BookmakerOddsOptional;
using stcpp::data::feature_store::FeatureStoreBookRow;
using stcpp::data::feature_store::FeatureStoreGameRow;
using stcpp::ml::FairValueModel;
using stcpp::ml::FeatureVector;
using stcpp::ml::kMlFeatureCount;
using stcpp::ml::MlFeature;
using stcpp::ml::ModelKind;
using stcpp::ml::ModelPrediction;
using stcpp::ml::OnnxModelConfig;
using stcpp::ml::StubFairValueModel;

constexpr double kEps = 1e-9;

// 构造一个最小有效 game row (4 ts 合法 + 比分 + 2 家赔率).
FeatureStoreGameRow make_game_row() {
    FeatureStoreGameRow g;
    g.event_ts_ns = 1'000;
    g.data_source_ts_ns = 2'000;
    g.ingestion_ts_ns = 3'000;
    g.as_of_ts_ns = 4'000;
    g.sport = "Basketball";
    g.market_type = "Moneyline";
    g.match_id = "gs-12345";
    g.home_team = "Lakers";
    g.away_team = "Celtics";
    g.score_home_total = 58;
    g.score_away_total = 51;
    g.period = 3;
    g.elapsed_sec = 240;
    // 两家 bookmaker: odds_yes / odds_no (decimal). 隐含 de-vig.
    g.bm_slots[0] = BookmakerOddsOptional{1.80, 2.10, true};
    g.bm_slots[1] = BookmakerOddsOptional{1.83, 2.05, true};
    return g;
}

// 构造一个最小有效 book row (L1 报价 + microstructure 派生).
FeatureStoreBookRow make_book_row() {
    FeatureStoreBookRow b;
    b.event_ts_ns = 1'000;
    b.data_source_ts_ns = 2'500;
    b.ingestion_ts_ns = 3'500;
    b.as_of_ts_ns = 5'000;  // 比 game 晚 → joined PIT 取这个
    b.market_id = "0xcond";
    b.token_side = "YES";
    b.bid_price[0] = 0.62;
    b.bid_size_usdc[0] = 500.0;
    b.ask_price[0] = 0.64;
    b.ask_size_usdc[0] = 450.0;
    b.bid_price[1] = 0.61;
    b.bid_size_usdc[1] = 300.0;
    b.ask_price[1] = 0.65;
    b.ask_size_usdc[1] = 320.0;
    b.mid = 0.63;
    b.spread_bps_f = (0.64 - 0.62) / 0.63 * 10000.0;
    b.top3_depth_usdc = 1570.0;
    b.microprice = 0.628;
    b.imbalance = (500.0 - 450.0) / (500.0 + 450.0);
    return b;
}

// ===========================================================================
// A. ModelFeatureSpec
// ===========================================================================

TEST(ModelFeatureSpec, ColumnOrderLock) {
    // 列序锁: enum 值 = column index, 末列固定.
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::g_score_diff), 0u);
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::b_mid), 8u);
    // append-only: 旧末列恒定, 不因后续 append 移位.
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::x_microprice_minus_mid), 17u);  // v0.1 末列
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::g_corner_diff), 23u);           // v0.2 末列
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::pos_condition_exposure), 53u);  // v0.3 末列
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::g_net_momentum_5m), 74u);       // v0.4 末列
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::x_joint_staleness_sec), 81u);   // v0.5 末列
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::cat_market_type), 84u);         // v0.6 末列
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::cat_league), 85u);              // v0.7 末列
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::cat_asset_class), 82u);
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::cat_sport), 83u);
    // v0.8 新末列 = no_b_depth_imbalance_5lvl (93; L2-L5 双边深度分布).
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::no_b_depth_imbalance_5lvl), kMlFeatureCount - 1);
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::b_bid_depth_5lvl), 86u);
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::no_b_bid_depth_5lvl), 90u);
    EXPECT_EQ(kMlFeatureCount, 94u);
    // 双边对称 + v0.5 延迟特征抽查 (双边 book 龄独立).
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::b_ofi), 30u);
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::no_b_ofi), 40u);
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::b_book_age_sec), 75u);
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::no_b_book_age_sec), 76u);  // 双边独立
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::x_yes_no_book_skew_sec), 78u);
}

TEST(ModelFeatureSpec, V03Columns_FromQuoteFeatures_DoubleSided) {
    // 双边时序微结构 + 双边持仓 (24-53) 从 QuoteFeatures 抽取 (extract_full)。
    stcpp::sizing::QuoteFeatures qf{};
    qf.b_ofi = 1.5;            // YES OFI
    qf.no_b_ofi = -0.8;        // NO OFI (独立, 非 YES 镜像)
    qf.realized_vol = 0.02;
    qf.no_realized_vol = 0.03;
    qf.cross_spread = 0.04;
    qf.yes_imbalance = 0.3;
    qf.no_imbalance = -0.2;
    qf.pos_yes_qty = 100.0;
    qf.pos_no_qty = 30.0;
    qf.pos_net_qty = 70.0;
    qf.pos_condition_exposure_usdc = 130.0;

    const FeatureVector fv = stcpp::ml::extract_full(make_game_row(), make_book_row(), qf);
    ASSERT_EQ(fv.size(), kMlFeatureCount);  // 54
    auto at = [&](MlFeature f) { return fv.values[static_cast<std::size_t>(f)]; };
    // 双边时序微结构: YES 与 NO 各自独立值都进向量。
    EXPECT_FLOAT_EQ(at(MlFeature::b_ofi), 1.5f);
    EXPECT_FLOAT_EQ(at(MlFeature::no_b_ofi), -0.8f) << "NO OFI 独立信号, 非 YES 镜像";
    EXPECT_FLOAT_EQ(at(MlFeature::b_realized_vol), 0.02f);
    EXPECT_FLOAT_EQ(at(MlFeature::no_b_realized_vol), 0.03f);
    EXPECT_FLOAT_EQ(at(MlFeature::b_yes_imbalance), 0.3f);
    EXPECT_FLOAT_EQ(at(MlFeature::b_no_imbalance), -0.2f);
    // 双边持仓: 各边各量都进 (不塌单边)。
    EXPECT_FLOAT_EQ(at(MlFeature::pos_yes_qty), 100.0f);
    EXPECT_FLOAT_EQ(at(MlFeature::pos_no_qty), 30.0f);
    EXPECT_FLOAT_EQ(at(MlFeature::pos_net_qty), 70.0f);
    EXPECT_FLOAT_EQ(at(MlFeature::pos_condition_exposure), 130.0f);
    // 0-23 仍由 game_row/book_row 填 (extract_full 含原始 game/book 列)。
    EXPECT_FLOAT_EQ(at(MlFeature::g_score_diff), 7.0f);
}

TEST(ModelFeatureSpec, V05Columns_DataLatency_DoubleSided) {
    // 数据延迟/新鲜度: 决策 as_of − 数据 data_source = 龄。YES/NO book 时间独立。
    auto g = make_game_row();
    auto b = make_book_row();
    const std::int64_t as_of = 10'000'000'000LL;  // 10s
    g.data_source_ts_ns = 7'000'000'000LL;        // 比分 3s 前
    b.data_source_ts_ns = 9'500'000'000LL;        // YES book 0.5s 前
    b.ingestion_ts_ns = 9'600'000'000LL;          // YES ingestion lag 100ms
    stcpp::sizing::QuoteFeatures qf{};
    qf.as_of_ts_ns = as_of;
    qf.no_book_data_source_ts_ns = 2'000'000'000LL;  // NO book 8s 前 (双边独立! 比 YES 旧得多)
    qf.no_book_ingestion_ts_ns = 2'050'000'000LL;    // NO ingestion lag 50ms

    const FeatureVector fv = stcpp::ml::extract_full(g, b, qf);
    auto at = [&](MlFeature f) { return fv.values[static_cast<std::size_t>(f)]; };
    EXPECT_NEAR(at(MlFeature::b_book_age_sec), 0.5, 1e-6);    // YES book 0.5s
    EXPECT_NEAR(at(MlFeature::no_b_book_age_sec), 8.0, 1e-6);  // NO book 8s (双边独立)
    EXPECT_NEAR(at(MlFeature::g_score_age_sec), 3.0, 1e-6);    // 比分 3s
    EXPECT_NEAR(at(MlFeature::x_yes_no_book_skew_sec), 7.5, 1e-6) << "YES.ds−NO.ds = 9.5−2.0";
    EXPECT_NEAR(at(MlFeature::b_ingestion_lag_ms), 100.0, 1e-3);   // YES 传输 100ms
    EXPECT_NEAR(at(MlFeature::no_b_ingestion_lag_ms), 50.0, 1e-3);  // NO 传输 50ms (双边独立)
    EXPECT_NEAR(at(MlFeature::x_joint_staleness_sec), 8.0, 1e-6) << "max(0.5,8,3)=8 最弱环节";
}

TEST(ModelFeatureSpec, V07Columns_CategoricalContext_RealStructure) {
    // v0.7: 类别码来自 QuoteFeatures 载体 (真实 Polymarket 结构, paper_loop 从 per-condition map 查填)。
    // cat_league = Polymarket sport.id (nba=34, bkcba=104=CBA), 天然区分 NBA vs CBA — 老板要的细粒度。
    auto g = make_game_row();
    auto b = make_book_row();
    stcpp::sizing::QuoteFeatures qf{};
    qf.cat_asset_class_id = 0;     // Sports
    qf.cat_sport_family_id = 1;    // 篮球家族
    qf.cat_market_type_id = 2;     // totals (大小分)
    qf.cat_league_id = 104;        // bkcba = CBA (≠ NBA 34)
    FeatureVector fv = stcpp::ml::extract_full(g, b, qf);
    auto at = [&](MlFeature f) { return fv.values[static_cast<std::size_t>(f)]; };
    EXPECT_EQ(at(MlFeature::cat_asset_class), 0.0F) << "Sports";
    EXPECT_EQ(at(MlFeature::cat_sport), 1.0F) << "篮球家族";
    EXPECT_EQ(at(MlFeature::cat_market_type), 2.0F) << "totals 大小分";
    EXPECT_EQ(at(MlFeature::cat_league), 104.0F) << "CBA (Polymarket sport.id 104, ≠ NBA 34)";
    // unknown 默认 (未注入 condition) → -1 (独立 categorical level, 非 NaN)。
    stcpp::sizing::QuoteFeatures qf2{};  // 默认: asset=0, 其余 -1
    fv = stcpp::ml::extract_full(g, b, qf2);
    EXPECT_EQ(at(MlFeature::cat_sport), -1.0F) << "未注入 → unknown=-1";
    EXPECT_EQ(at(MlFeature::cat_league), -1.0F) << "未注入 → unknown=-1";
}

TEST(DepthMetrics, FiveLevelAggregation) {
    using stcpp::polymarket::clob_wss::OrderBookFeatures;
    OrderBookFeatures f{};
    // bids 5档: 100/50/30/20/0(NaN); asks: 40/10(其余NaN) — 买深厚, L1 不撑门面。
    const double nan = std::numeric_limits<double>::quiet_NaN();
    double bsz[5] = {100, 50, 30, 20, nan};
    double asz[5] = {40, 10, nan, nan, nan};
    for (std::size_t i = 0; i < 5; ++i) {
        f.bids[i].size_usdc = bsz[i];
        f.bids[i].price = 0.5;
        f.asks[i].size_usdc = asz[i];
        f.asks[i].price = 0.51;
    }
    const auto m = stcpp::polymarket::clob_wss::compute_depth_metrics(f);
    EXPECT_DOUBLE_EQ(m.bid_depth_5lvl, 200.0) << "Σ 100+50+30+20";
    EXPECT_DOUBLE_EQ(m.ask_depth_5lvl, 50.0) << "Σ 40+10";
    EXPECT_DOUBLE_EQ(m.l1_concentration, (100.0 + 40.0) / 250.0) << "L1/总";
    EXPECT_DOUBLE_EQ(m.depth_imbalance_5lvl, (200.0 - 50.0) / 250.0) << "5档买卖失衡";
    // 空 book → 全 NaN (fail-safe)。
    OrderBookFeatures empty{};
    for (std::size_t i = 0; i < 5; ++i) {
        empty.bids[i].size_usdc = nan;
        empty.asks[i].size_usdc = nan;
    }
    const auto me = stcpp::polymarket::clob_wss::compute_depth_metrics(empty);
    EXPECT_TRUE(std::isnan(me.bid_depth_5lvl));
    EXPECT_TRUE(std::isnan(me.l1_concentration));
}

TEST(MarketTaxonomy, RealPolymarketVocabulary) {
    namespace tax = stcpp::data::taxonomy;
    // 盘口类型 (NormalizeSportsMarketType 归一值 → 码; 真实 sportsMarketType 集合)。
    EXPECT_EQ(tax::MarketTypeCode("moneyline"), 0);
    EXPECT_EQ(tax::MarketTypeCode("spread"), 1);
    EXPECT_EQ(tax::MarketTypeCode("totals"), 2);
    EXPECT_EQ(tax::MarketTypeCode("outright"), 3);
    EXPECT_EQ(tax::MarketTypeCode("series"), 5);
    EXPECT_EQ(tax::MarketTypeCode("map_handicap"), 1) << "raw 长尾 handicap → spread";
    EXPECT_EQ(tax::MarketTypeCode("kill_over_under_game"), 2) << "over_under → totals";
    EXPECT_EQ(tax::MarketTypeCode("lol_penta_kill"), 4) << "esports specials → prop";
    EXPECT_EQ(tax::MarketTypeCode(""), -1);
    // 运动家族 (真实联赛码 → 粗家族; cat_league 才是细 sport.id)。
    EXPECT_EQ(tax::SportFamilyCode("nba"), 1);
    EXPECT_EQ(tax::SportFamilyCode("bkcba"), 1) << "CBA 也是篮球家族";
    EXPECT_EQ(tax::SportFamilyCode("atp"), 2);
    EXPECT_EQ(tax::SportFamilyCode("wta"), 2) << "女子网球同家族";
    EXPECT_EQ(tax::SportFamilyCode("mlb"), 3);
    EXPECT_EQ(tax::SportFamilyCode("nhl"), 4);
    EXPECT_EQ(tax::SportFamilyCode("lol"), 6) << "电竞";
    EXPECT_EQ(tax::SportFamilyCode("dota2"), 6);
    EXPECT_EQ(tax::SportFamilyCode("ufc"), 7) << "mma";
    EXPECT_EQ(tax::SportFamilyCode("cricipl"), 8) << "板球";
    EXPECT_EQ(tax::SportFamilyCode(""), -1);
}

TEST(ModelFeatureSpec, V04Columns_RemainingSignals) {
    stcpp::sizing::QuoteFeatures qf{};
    qf.fee_rate_coef = 0.03;
    qf.devig_ok = true;
    qf.time_to_resolution_frac = 0.7;
    qf.resolution_status = 1;
    qf.x_log_odds_fair = 0.5;
    qf.x_pin_risk = 0.4;
    qf.g_clutch = 1.0;
    qf.g_goal_freshness = 0.8;
    qf.g_periods_won_home = 2;

    const FeatureVector fv = stcpp::ml::extract_full(make_game_row(), make_book_row(), qf);
    auto at = [&](MlFeature f) { return fv.values[static_cast<std::size_t>(f)]; };
    EXPECT_FLOAT_EQ(at(MlFeature::fee_rate_coef), 0.03f);
    EXPECT_FLOAT_EQ(at(MlFeature::devig_ok), 1.0f);  // bool → 1.0
    EXPECT_FLOAT_EQ(at(MlFeature::time_to_resolution_frac), 0.7f);
    EXPECT_FLOAT_EQ(at(MlFeature::resolution_status), 1.0f);
    EXPECT_FLOAT_EQ(at(MlFeature::x_log_odds_fair), 0.5f);
    EXPECT_FLOAT_EQ(at(MlFeature::x_pin_risk), 0.4f);
    EXPECT_FLOAT_EQ(at(MlFeature::g_clutch), 1.0f);
    EXPECT_FLOAT_EQ(at(MlFeature::g_goal_freshness), 0.8f);
    EXPECT_FLOAT_EQ(at(MlFeature::g_periods_won_home), 2.0f);
}

TEST(ModelFeatureSpec, V02Columns_InplayOddsAndLiveStats) {
    auto at = [](const FeatureVector& fv, MlFeature f) {
        return fv.values[static_cast<std::size_t>(f)];
    };
    // 默认 game_row: inplay 赔率/live_stats 全缺 (-1) → 6 列全 NaN.
    {
        FeatureVector fv = stcpp::ml::extract_game_only(make_game_row());
        EXPECT_TRUE(std::isnan(at(fv, MlFeature::g_bm_inplay_fair)));
        EXPECT_TRUE(std::isnan(at(fv, MlFeature::g_danger_attack_diff)));
        EXPECT_TRUE(std::isnan(at(fv, MlFeature::g_shot_on_target_diff)));
        EXPECT_TRUE(std::isnan(at(fv, MlFeature::g_possession_home)));
        EXPECT_TRUE(std::isnan(at(fv, MlFeature::g_red_card_diff)));
        EXPECT_TRUE(std::isnan(at(fv, MlFeature::g_corner_diff)));
    }
    // 填充 game_row → 6 列按 paper_loop SportsFeatures 语义 (diff=home-away; -1→NaN) 抽取.
    {
        auto g = make_game_row();
        g.inplay_bet365_home_fair = 0.62;            // de-vig home/YES fair
        g.soccer_dangerous_attacks_home = 40;
        g.soccer_dangerous_attacks_away = 43;        // diff = -3
        g.soccer_shots_on_target_home = 6;
        g.soccer_shots_on_target_away = 3;           // diff = +3
        g.soccer_possession_home_pct = 57;
        g.soccer_red_cards_home = 0;
        g.soccer_red_cards_away = 1;                 // diff = -1
        g.soccer_corners_home = 5;
        g.soccer_corners_away = 3;                   // diff = +2
        FeatureVector fv = stcpp::ml::extract_game_only(g);
        EXPECT_FLOAT_EQ(at(fv, MlFeature::g_bm_inplay_fair), 0.62f);
        EXPECT_FLOAT_EQ(at(fv, MlFeature::g_danger_attack_diff), -3.0f);
        EXPECT_FLOAT_EQ(at(fv, MlFeature::g_shot_on_target_diff), 3.0f);
        EXPECT_FLOAT_EQ(at(fv, MlFeature::g_possession_home), 57.0f);
        EXPECT_FLOAT_EQ(at(fv, MlFeature::g_red_card_diff), -1.0f);
        EXPECT_FLOAT_EQ(at(fv, MlFeature::g_corner_diff), 2.0f);
    }
    // 单边缺数据 (home 有 away 无) → diff NaN (sdiff 语义).
    {
        auto g = make_game_row();
        g.soccer_corners_home = 5;  // away 保持 -1
        FeatureVector fv = stcpp::ml::extract_game_only(g);
        EXPECT_TRUE(std::isnan(at(fv, MlFeature::g_corner_diff)));
    }
}

TEST(ModelFeatureSpec, ExtractGameRowFields) {
    const auto g = make_game_row();
    FeatureVector fv = stcpp::ml::extract_game_only(g);

    ASSERT_EQ(fv.size(), kMlFeatureCount);
    EXPECT_EQ(fv.spec_version, stcpp::ml::kSpecVersion);
    // PIT 锚 = game.as_of_ts.
    EXPECT_EQ(fv.as_of_ts_ns, 4'000);

    auto at = [&](MlFeature f) {
        return fv.values[static_cast<std::size_t>(f)];
    };
    EXPECT_FLOAT_EQ(at(MlFeature::g_score_diff), 7.0f);  // 58 - 51
    EXPECT_FLOAT_EQ(at(MlFeature::g_score_total), 109.0f);
    EXPECT_FLOAT_EQ(at(MlFeature::g_period), 3.0f);
    EXPECT_FLOAT_EQ(at(MlFeature::g_elapsed_sec), 240.0f);
    EXPECT_FLOAT_EQ(at(MlFeature::g_valid_bm_count), 2.0f);

    // de-vig YES fair prob 应 in (0,1) 且 < 0.5 偏向 (1.8 odds → ~0.55 raw 但 de-vig 后).
    const float devig = at(MlFeature::g_bm_devig_p_yes);
    EXPECT_FALSE(std::isnan(devig));
    EXPECT_GT(devig, 0.0f);
    EXPECT_LT(devig, 1.0f);
    // overround > 1 (vig 存在).
    EXPECT_GT(at(MlFeature::g_bm_overround_avg), 1.0f);

    // book 侧未填 → NaN.
    EXPECT_TRUE(std::isnan(at(MlFeature::b_mid)));
}

TEST(ModelFeatureSpec, ExtractBookRowFields) {
    const auto b = make_book_row();
    std::vector<float> out(kMlFeatureCount, std::numeric_limits<float>::quiet_NaN());
    stcpp::ml::extract_from_book_row(b, out);

    auto at = [&](MlFeature f) {
        return out[static_cast<std::size_t>(f)];
    };
    EXPECT_FLOAT_EQ(at(MlFeature::b_mid), 0.63f);
    EXPECT_FLOAT_EQ(at(MlFeature::b_microprice), 0.628f);
    EXPECT_FLOAT_EQ(at(MlFeature::b_best_bid), 0.62f);
    EXPECT_FLOAT_EQ(at(MlFeature::b_best_ask), 0.64f);
    // 2 bid + 2 ask level valid = 4.
    EXPECT_FLOAT_EQ(at(MlFeature::b_book_levels_valid), 4.0f);
}

TEST(ModelFeatureSpec, ExtractJoinedPitTakesLaterAsOf) {
    const auto g = make_game_row();  // as_of 4000
    const auto b = make_book_row();  // as_of 5000
    FeatureVector fv = stcpp::ml::extract_joined(g, b);

    ASSERT_EQ(fv.size(), kMlFeatureCount);
    EXPECT_EQ(fv.as_of_ts_ns, 5'000);  // PIT: 取较晚

    auto at = [&](MlFeature f) {
        return fv.values[static_cast<std::size_t>(f)];
    };
    // cross feature: x_devig_minus_mid = devig - mid (两者都 finite).
    const float cross = at(MlFeature::x_devig_minus_mid);
    EXPECT_FALSE(std::isnan(cross));
    // x_microprice_minus_mid = 0.628 - 0.63 = -0.002.
    EXPECT_NEAR(at(MlFeature::x_microprice_minus_mid), -0.002f, 1e-5);
}

TEST(ModelFeatureSpec, CrossFeatureNaNWhenBookMissing) {
    const auto g = make_game_row();
    FeatureVector fv = stcpp::ml::extract_game_only(g);
    auto at = [&](MlFeature f) {
        return fv.values[static_cast<std::size_t>(f)];
    };
    // book mid NaN → cross NaN.
    EXPECT_TRUE(std::isnan(at(MlFeature::x_devig_minus_mid)));
    EXPECT_TRUE(std::isnan(at(MlFeature::x_microprice_minus_mid)));
}

TEST(ModelFeatureSpec, NoBookmakerOddsGivesNaNDevig) {
    auto g = make_game_row();
    for (auto& s : g.bm_slots)
        s = BookmakerOddsOptional{};  // 全缺失
    FeatureVector fv = stcpp::ml::extract_game_only(g);
    auto at = [&](MlFeature f) {
        return fv.values[static_cast<std::size_t>(f)];
    };
    EXPECT_TRUE(std::isnan(at(MlFeature::g_bm_devig_p_yes)));
    EXPECT_TRUE(std::isnan(at(MlFeature::g_bm_overround_avg)));
    EXPECT_FLOAT_EQ(at(MlFeature::g_valid_bm_count), 0.0f);
}

// ===========================================================================
// B. FairValueModel 接口契约 (StubFairValueModel)
// ===========================================================================

TEST(StubFairValueModel, BinaryOutcomeNormalized) {
    StubFairValueModel model(kMlFeatureCount, /*outcome_count=*/2);
    EXPECT_EQ(model.kind(), ModelKind::Stub);
    EXPECT_EQ(model.expected_feature_count(), kMlFeatureCount);
    EXPECT_EQ(model.output_outcome_count(), 2u);
    EXPECT_TRUE(model.ready());
    EXPECT_EQ(model.model_id(), "stub-fair-value-v0.1");

    const auto g = make_game_row();
    const auto b = make_book_row();
    FeatureVector fv = stcpp::ml::extract_joined(g, b);

    ModelPrediction p = model.predict(fv);
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.num_outcomes, 2u);
    EXPECT_TRUE(p.normalized);
    EXPECT_TRUE(p.sum_ok());
    EXPECT_GE(p.prob(0), 0.0);
    EXPECT_LE(p.prob(0), 1.0);
    EXPECT_NEAR(p.prob(0) + p.prob(1), 1.0, kEps);

    // ML-R8: 元数据携带.
    EXPECT_EQ(p.model_id, "stub-fair-value-v0.1");
    EXPECT_EQ(p.as_of_ts_ns, fv.as_of_ts_ns);
    EXPECT_EQ(p.input_feature_count, kMlFeatureCount);
}

TEST(StubFairValueModel, DimensionMismatchReturnsNotOk) {
    StubFairValueModel model(kMlFeatureCount, 2);
    FeatureVector fv;
    fv.values.assign(kMlFeatureCount - 1, 0.0f);  // 维度不对
    ModelPrediction p = model.predict(fv);
    EXPECT_FALSE(p.ok);
}

TEST(StubFairValueModel, Deterministic) {
    // 同输入同输出 (回测可复现要求).
    StubFairValueModel model(kMlFeatureCount, 2);
    const auto g = make_game_row();
    const auto b = make_book_row();
    FeatureVector fv = stcpp::ml::extract_joined(g, b);

    ModelPrediction p1 = model.predict(fv);
    ModelPrediction p2 = model.predict(fv);
    ASSERT_TRUE(p1.ok);
    ASSERT_TRUE(p2.ok);
    EXPECT_DOUBLE_EQ(p1.prob(0), p2.prob(0));
    EXPECT_DOUBLE_EQ(p1.prob(1), p2.prob(1));
}

TEST(StubFairValueModel, NaNFeaturesHandledNoPropagation) {
    // 全 NaN 输入 → stub 视为 0 贡献, logistic(0)=0.5, 不产 NaN.
    StubFairValueModel model(kMlFeatureCount, 2);
    FeatureVector fv;
    fv.values.assign(kMlFeatureCount, std::numeric_limits<float>::quiet_NaN());
    ModelPrediction p = model.predict(fv);
    ASSERT_TRUE(p.ok);
    EXPECT_FALSE(std::isnan(p.prob(0)));
    EXPECT_NEAR(p.prob(0), 0.5, kEps);
    EXPECT_TRUE(p.sum_ok());
}

TEST(StubFairValueModel, MultiOutcomeSoftmaxNormalized) {
    StubFairValueModel model(kMlFeatureCount, /*outcome_count=*/3);  // 3-way
    EXPECT_EQ(model.output_outcome_count(), 3u);
    const auto g = make_game_row();
    FeatureVector fv = stcpp::ml::extract_game_only(g);
    fv.values.assign(kMlFeatureCount, 0.1f);  // 全 finite 避免 size 误判
    ModelPrediction p = model.predict(fv);
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.num_outcomes, 3u);
    EXPECT_TRUE(p.normalized);
    EXPECT_TRUE(p.sum_ok(1e-6));
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_GE(p.prob(i), 0.0);
        EXPECT_LE(p.prob(i), 1.0);
    }
}

TEST(StubFairValueModel, OutcomeCountClampedToMax) {
    StubFairValueModel model(kMlFeatureCount, /*outcome_count=*/100);
    EXPECT_EQ(model.output_outcome_count(), stcpp::ml::kMaxOutcomes);
}

TEST(ModelPrediction, ProbOutOfRangeReturnsNaN) {
    ModelPrediction p;
    p.num_outcomes = 2;
    p.probs[0] = 0.4;
    p.probs[1] = 0.6;
    EXPECT_TRUE(std::isnan(p.prob(5)));  // 越界
}

// ===========================================================================
// C. ONNX 适配点 (工厂当前返回 nullptr)
// ===========================================================================

TEST(OnnxAdapter, FactoryReturnsNullptrBeforeRuntimeIntegration) {
    OnnxModelConfig cfg;
    cfg.onnx_path = "/tmp/nonexistent.onnx";
    cfg.expected_feature_count = kMlFeatureCount;
    cfg.output_outcome_count = 2;
    cfg.model_id = "onnx-pending";

    std::unique_ptr<FairValueModel> m = stcpp::ml::make_onnx_fair_value_model(cfg);
    EXPECT_EQ(m, nullptr);  // W11 前未集成
}

TEST(OnnxAdapter, FallbackToStubWhenOnnxUnavailable) {
    // 调用方模式: onnx 工厂返回 nullptr → 回落 stub, 接口一致.
    OnnxModelConfig cfg;
    cfg.expected_feature_count = kMlFeatureCount;
    std::unique_ptr<FairValueModel> m = stcpp::ml::make_onnx_fair_value_model(cfg);
    if (!m) {
        m = std::make_unique<StubFairValueModel>(kMlFeatureCount, 2);
    }
    ASSERT_NE(m, nullptr);
    EXPECT_TRUE(m->ready());

    const auto g = make_game_row();
    const auto b = make_book_row();
    FeatureVector fv = stcpp::ml::extract_joined(g, b);
    ModelPrediction p = m->predict(fv);
    EXPECT_TRUE(p.ok);
    EXPECT_TRUE(p.sum_ok());
}

TEST(ModelKindToString, AllKinds) {
    EXPECT_EQ(stcpp::ml::to_string(ModelKind::Stub), "Stub");
    EXPECT_EQ(stcpp::ml::to_string(ModelKind::Onnx), "Onnx");
    EXPECT_EQ(stcpp::ml::to_string(ModelKind::Treelite), "Treelite");
}

}  // namespace
