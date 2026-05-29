// stcpp/ml/fair_value_model.cpp — FairValueModel ONNX 适配点实现 v0.1 (ADR-037 骨架, 小邓 #31)
//
// Owner: 小邓 (ml-advisor, #31)
// last_review: 2026-05-29
//
// 当前 (W11 前): ONNXRuntime 未集成 — make_onnx_fair_value_model() 返回 nullptr.
//   调用方判 nullptr → 回落 StubFairValueModel (见 fair_value_model.hpp).
//
// W11+ 集成计划 (ADR-037):
//   - 链 onnxruntime C++ (老吴 toolstack 装 libonnxruntime).
//   - OnnxFairValueModel : public FairValueModel, 内部持:
//       Ort::Env env_;  Ort::Session session_;  Ort::MemoryInfo mem_;
//     predict(): 用 fv.values 构造 input tensor (shape [1, expected_feature_count]),
//       session_.Run() → output tensor (shape [1, output_outcome_count]),
//       softmax / 直接读 → ModelPrediction.probs.
//   - 红线: 推理只读, 不下 OrderIntent (ML-R1); model_id = onnx 文件 blake3 (ML-R8);
//     上线前 walk-forward backtest (ADR-037 §gate).
//
// 红线 R-5: 推理走 ONNX C++; 本 TU 不引 Python.

#include "stcpp/ml/fair_value_model.hpp"

namespace stcpp::ml {

std::unique_ptr<FairValueModel> make_onnx_fair_value_model(const OnnxModelConfig& cfg) noexcept {
    // W11 前: ONNXRuntime 未集成. 返回 nullptr, 调用方回落 stub.
    // 标 cfg 已用 (避免 unused-parameter -Werror): 仅当 onnx_path 非空且未来 runtime 就绪才构造.
    (void)cfg;
    return nullptr;
}

}  // namespace stcpp::ml
