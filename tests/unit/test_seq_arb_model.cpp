// test_seq_arb_model.cpp — 短时套利序列模型接口单测 (stub 永不产信号 / 工厂 fallback)。
#include <gtest/gtest.h>

#include <cmath>

#include "stcpp/ml/seq_arb_model.hpp"

using stcpp::ml::FeatureVector;
using stcpp::ml::kArbHorizonCount;
using stcpp::ml::make_seq_arb_model;
using stcpp::ml::ModelKind;
using stcpp::ml::OnnxSeqArbConfig;
using stcpp::ml::StubSeqArbModel;

TEST(SeqArbModel, HorizonCount) {
    EXPECT_EQ(kArbHorizonCount, 8u) << "对应 kWallHorizonsSec {2,3,5,10,12,15,30,60}";
}

TEST(SeqArbModel, StubNeverProducesSignal) {
    StubSeqArbModel m(110);
    FeatureVector fv;
    fv.values.assign(110, 0.5f);
    fv.as_of_ts_ns = 123456;
    const auto p = m.predict(fv);
    EXPECT_FALSE(p.ok) << "stub 恒 ok=false (无真模型不发任何套利单 — 安全)";
    EXPECT_EQ(p.as_of_ts_ns, 123456) << "ML-R8: 带 as_of_ts";
    for (const auto& h : p.wall) {
        EXPECT_TRUE(std::isnan(h.dmid));
        EXPECT_TRUE(std::isnan(h.ci_low));
    }
    EXPECT_EQ(m.kind(), ModelKind::Stub);
    EXPECT_TRUE(m.ready());
    EXPECT_EQ(m.expected_feature_count(), 110u);
}

TEST(SeqArbModel, FactoryFallsBackToStub) {
    OnnxSeqArbConfig cfg;
    cfg.model_path = "";  // 无模型路径 → stub
    cfg.expected_feature_count = 110;
    auto m = make_seq_arb_model(cfg);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->kind(), ModelKind::Stub) << "无 ONNX → fallback stub";
    EXPECT_TRUE(m->ready());
}

TEST(SeqArbModel, FactoryNonexistentPathFallsBackToStub) {
    OnnxSeqArbConfig cfg;
    cfg.model_path = "/nonexistent/model.onnx";  // 不存在 → make_onnx 返 nullptr → stub
    cfg.expected_feature_count = 110;
    auto m = make_seq_arb_model(cfg);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->kind(), ModelKind::Stub);
}
