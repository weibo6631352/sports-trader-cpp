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
    EXPECT_EQ(static_cast<std::size_t>(MlFeature::x_microprice_minus_mid), kMlFeatureCount - 1);
    EXPECT_EQ(kMlFeatureCount, 18u);
}

TEST(ModelFeatureSpec, ExtractGameRowFields) {
    const auto g = make_game_row();
    FeatureVector fv = stcpp::ml::extract_game_only(g);

    ASSERT_EQ(fv.size(), kMlFeatureCount);
    EXPECT_EQ(fv.spec_version, stcpp::ml::kSpecVersion);
    // PIT 锚 = game.as_of_ts.
    EXPECT_EQ(fv.as_of_ts_ns, 4'000);

    auto at = [&](MlFeature f) { return fv.values[static_cast<std::size_t>(f)]; };
    EXPECT_FLOAT_EQ(at(MlFeature::g_score_diff), 7.0f);   // 58 - 51
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

    auto at = [&](MlFeature f) { return out[static_cast<std::size_t>(f)]; };
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

    auto at = [&](MlFeature f) { return fv.values[static_cast<std::size_t>(f)]; };
    // cross feature: x_devig_minus_mid = devig - mid (两者都 finite).
    const float cross = at(MlFeature::x_devig_minus_mid);
    EXPECT_FALSE(std::isnan(cross));
    // x_microprice_minus_mid = 0.628 - 0.63 = -0.002.
    EXPECT_NEAR(at(MlFeature::x_microprice_minus_mid), -0.002f, 1e-5);
}

TEST(ModelFeatureSpec, CrossFeatureNaNWhenBookMissing) {
    const auto g = make_game_row();
    FeatureVector fv = stcpp::ml::extract_game_only(g);
    auto at = [&](MlFeature f) { return fv.values[static_cast<std::size_t>(f)]; };
    // book mid NaN → cross NaN.
    EXPECT_TRUE(std::isnan(at(MlFeature::x_devig_minus_mid)));
    EXPECT_TRUE(std::isnan(at(MlFeature::x_microprice_minus_mid)));
}

TEST(ModelFeatureSpec, NoBookmakerOddsGivesNaNDevig) {
    auto g = make_game_row();
    for (auto& s : g.bm_slots) s = BookmakerOddsOptional{};  // 全缺失
    FeatureVector fv = stcpp::ml::extract_game_only(g);
    auto at = [&](MlFeature f) { return fv.values[static_cast<std::size_t>(f)]; };
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
