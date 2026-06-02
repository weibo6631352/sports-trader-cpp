// tests/unit/test_onnx_fair_value.cpp — OnnxFairValueModel round-trip (Python 训练→ONNX→C++ 推理)
//   fixture: tests/fixtures/fair_value_selftest.onnx (scripts/ml/train_fair_value.py --selftest 生成)
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "stcpp/ml/fair_value_model.hpp"
#include "stcpp/ml/model_feature_spec.hpp"

using stcpp::ml::CalibMethod;
using stcpp::ml::FeatureVector;
using stcpp::ml::kMlFeatureCount;
using stcpp::ml::make_onnx_fair_value_model;
using stcpp::ml::ModelKind;
using stcpp::ml::OnnxModelConfig;

namespace {
// [[maybe_unused]]: onnxruntime 未启用时 (gcc + 无 onnxruntime) 不被引用 → 避 -Werror=unused-function。
[[maybe_unused]] std::string FixturePath() {
    // __FILE__ = .../tests/unit/test_onnx_fair_value.cpp → .../tests/fixtures/...
    const auto p = std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" /
                   "fair_value_selftest.onnx";
    return p.string();
}
}  // namespace

#ifdef STCPP_ONNX_ENABLED

// ON01: 加载 fixture ONNX → predict 完整 75 列向量 → ok + p_yes∈[0,1]
TEST(OnnxFairValue, ON01_RoundTrip) {
    if (!std::filesystem::exists(FixturePath())) {
        GTEST_SKIP() << "fixture ONNX 缺失: " << FixturePath();
    }
    OnnxModelConfig cfg;
    cfg.onnx_path = FixturePath();
    cfg.expected_feature_count = kMlFeatureCount;  // 75
    cfg.output_outcome_count = 2;
    cfg.model_id = "selftest-onnx";

    auto m = make_onnx_fair_value_model(cfg);
    ASSERT_NE(m, nullptr) << "ONNX 加载失败";
    EXPECT_TRUE(m->ready());
    EXPECT_EQ(m->kind(), ModelKind::Onnx);
    EXPECT_EQ(m->expected_feature_count(), kMlFeatureCount);
    EXPECT_EQ(m->model_id(), "selftest-onnx");

    FeatureVector fv;
    fv.values.assign(kMlFeatureCount, 0.5f);
    fv.spec_version = stcpp::ml::kSpecVersion;
    fv.as_of_ts_ns = 12345;
    const auto p = m->predict(fv);
    EXPECT_TRUE(p.ok) << "推理成功";
    EXPECT_GE(p.probs[0], 0.0);
    EXPECT_LE(p.probs[0], 1.0) << "回归输出 clamp 到 [0,1] = p_yes";
    EXPECT_NEAR(p.probs[0] + p.probs[1], 1.0, 1e-9) << "binary 互余";
    EXPECT_EQ(p.input_feature_count, kMlFeatureCount);
}

// ON02: 维度不符 → ok=false (宁可空不可假)
TEST(OnnxFairValue, ON02_WrongDim) {
    if (!std::filesystem::exists(FixturePath())) GTEST_SKIP();
    OnnxModelConfig cfg;
    cfg.onnx_path = FixturePath();
    cfg.expected_feature_count = kMlFeatureCount;
    auto m = make_onnx_fair_value_model(cfg);
    ASSERT_NE(m, nullptr);
    FeatureVector fv;
    fv.values.assign(10, 0.5f);  // 错误维度
    EXPECT_FALSE(m->predict(fv).ok);
}

// ON03: 空路径 / 不存在文件 → nullptr (stub fallback)
TEST(OnnxFairValue, ON03_GracefulFallback) {
    OnnxModelConfig empty;
    EXPECT_EQ(make_onnx_fair_value_model(empty), nullptr) << "空路径 → nullptr";
    OnnxModelConfig missing;
    missing.onnx_path = "/tmp/does_not_exist_xyz.onnx";
    EXPECT_EQ(make_onnx_fair_value_model(missing), nullptr) << "无文件 → nullptr";
}

// ON04: 有校准 sidecar (<onnx>.meta.json) → predict 填 confidence/calibrated/CI (前端 modelReady 点亮)。
//   fixture 自带 .meta.json (selftest 产: calibrated=true, confidence>0, ci_halfwidth>0)。
TEST(OnnxFairValue, ON04_SidecarCalibration) {
    if (!std::filesystem::exists(FixturePath())) GTEST_SKIP();
    ASSERT_TRUE(std::filesystem::exists(FixturePath() + ".meta.json")) << "fixture 应带 sidecar";
    OnnxModelConfig cfg;
    cfg.onnx_path = FixturePath();
    cfg.expected_feature_count = kMlFeatureCount;
    auto m = make_onnx_fair_value_model(cfg);
    ASSERT_NE(m, nullptr);
    FeatureVector fv;
    fv.values.assign(kMlFeatureCount, 0.5f);
    fv.spec_version = stcpp::ml::kSpecVersion;
    const auto p = m->predict(fv);
    ASSERT_TRUE(p.ok);
    EXPECT_TRUE(p.calibrated) << "sidecar calibrated=true → 报告已校准";
    EXPECT_GT(p.confidence, 0.0) << "sidecar confidence>0 → modelReady 条件满足";
    EXPECT_LE(p.confidence, 1.0);
    EXPECT_EQ(p.calib_method, CalibMethod::Conformal) << "calib_method=conformal";
    // CI: ci_low <= probs[0] <= ci_high, 且非零宽 (sidecar ci_halfwidth>0)
    EXPECT_LE(p.ci_low, p.probs[0]);
    EXPECT_GE(p.ci_high, p.probs[0]);
    EXPECT_GT(p.ci_high - p.ci_low, 0.0) << "有 sidecar → 非零宽预测区间";
    EXPECT_GE(p.ci_low, 0.0);
    EXPECT_LE(p.ci_high, 1.0) << "CI clamp 到 [0,1]";
}

// ON05: 无 sidecar (临时复制 onnx 到无 meta 的路径) → 降级 (calibrated=false, conf=0, 零宽 CI)。
//   fail-safe: 模型能推理但不自称校准 → 前端保持"占位" (无校准证据不点亮)。
TEST(OnnxFairValue, ON05_NoSidecarDegraded) {
    if (!std::filesystem::exists(FixturePath())) GTEST_SKIP();
    namespace fs = std::filesystem;
    const auto tmp = fs::temp_directory_path() / "stcpp_onnx_nosidecar_test.onnx";
    std::error_code ec;
    fs::remove(tmp, ec);
    fs::remove(fs::path(tmp.string() + ".meta.json"), ec);  // 确保无 meta
    fs::copy_file(FixturePath(), tmp, fs::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec) << "复制 fixture 到 temp 失败";
    OnnxModelConfig cfg;
    cfg.onnx_path = tmp.string();
    cfg.expected_feature_count = kMlFeatureCount;
    auto m = make_onnx_fair_value_model(cfg);
    ASSERT_NE(m, nullptr);
    FeatureVector fv;
    fv.values.assign(kMlFeatureCount, 0.5f);
    const auto p = m->predict(fv);
    ASSERT_TRUE(p.ok) << "无 sidecar 仍能推理 (prob 不受影响)";
    EXPECT_FALSE(p.calibrated) << "无 sidecar → 未校准";
    EXPECT_DOUBLE_EQ(p.confidence, 0.0) << "无 sidecar → conf=0 (降级占位)";
    EXPECT_EQ(p.calib_method, CalibMethod::None);
    EXPECT_DOUBLE_EQ(p.ci_low, p.probs[0]) << "无 sidecar → 零宽 CI (同旧行为)";
    EXPECT_DOUBLE_EQ(p.ci_high, p.probs[0]);
    fs::remove(tmp, ec);
}

#else  // onnxruntime 未链

TEST(OnnxFairValue, Skipped_NoOnnxRuntime) {
    EXPECT_EQ(make_onnx_fair_value_model(OnnxModelConfig{}), nullptr) << "未链 onnxruntime → 恒 nullptr";
}

#endif
