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

#ifdef STCPP_ONNX_ENABLED

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace stcpp::ml {

// ---------------------------------------------------------------------------
// OnnxFairValueModel — ONNXRuntime C++ session 推理 (Phase 2 残差/分类模型)。
//   输入: float tensor [1, expected_feature_count] = FeatureVector.values (列序 = MlFeature enum)。
//   输出: 读首个输出 tensor 的 float —— 1 值=回归 (p_yes / 残差); ≥outcome 值=per-outcome prob。
//   红线: 只读推理不下单 (ML-R1); model_id 携带 (ML-R8); 旁路 advisory (ML-R2)。R-5 走 ONNX 非 Python。
// ---------------------------------------------------------------------------
class OnnxFairValueModel final : public FairValueModel {
public:
    explicit OnnxFairValueModel(const OnnxModelConfig& cfg)
        : env_(ORT_LOGGING_LEVEL_WARNING, "stcpp_fv"),
          mem_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
          feature_count_(cfg.expected_feature_count),
          outcome_count_(cfg.output_outcome_count == 0 ? 2 : cfg.output_outcome_count),
          model_id_(cfg.model_id.empty() ? "onnx-fair-value" : cfg.model_id) {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(cfg.intra_op_threads > 0 ? cfg.intra_op_threads : 1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_ = std::make_unique<Ort::Session>(env_, cfg.onnx_path.c_str(), opts);
        Ort::AllocatorWithDefaultOptions alloc;  // 动态取 IO 名 (导出名各异)
        in_name_ = session_->GetInputNameAllocated(0, alloc).get();
        const std::size_t n_out = session_->GetOutputCount();
        for (std::size_t i = 0; i < n_out; ++i) {
            out_names_.push_back(session_->GetOutputNameAllocated(i, alloc).get());
        }
        ready_ = (session_ != nullptr) && !in_name_.empty() && !out_names_.empty();
    }

    [[nodiscard]] ModelPrediction predict(const FeatureVector& fv) const noexcept override {
        ModelPrediction p;
        p.model_id = model_id_;
        p.as_of_ts_ns = fv.as_of_ts_ns;
        p.input_feature_count = fv.size();
        p.spec_version = fv.spec_version;
        p.num_outcomes = outcome_count_;
        p.calib_method = CalibMethod::None;  // 校准在训练侧; runtime 不改
        if (!ready_ || fv.size() != feature_count_) {
            p.ok = false;
            return p;
        }
        try {
            std::vector<float> input(fv.values.begin(), fv.values.end());
            const std::array<std::int64_t, 2> shape{1, static_cast<std::int64_t>(feature_count_)};
            Ort::Value in_tensor = Ort::Value::CreateTensor<float>(mem_, input.data(), input.size(),
                                                                   shape.data(), shape.size());
            const char* in_names[] = {in_name_.c_str()};
            std::vector<const char*> out_names;
            out_names.reserve(out_names_.size());
            for (const auto& s : out_names_) out_names.push_back(s.c_str());
            auto outs = session_->Run(Ort::RunOptions{nullptr}, in_names, &in_tensor, 1,
                                      out_names.data(), out_names.size());
            if (outs.empty() || !outs[0].IsTensor()) {
                p.ok = false;
                return p;
            }
            const auto info = outs[0].GetTensorTypeAndShapeInfo();
            const std::size_t cnt = info.GetElementCount();
            const float* data = outs[0].GetTensorData<float>();
            if (cnt == 1) {
                // 回归: 单值 = p_yes (或残差; 调用方按 model 语义 blend)。clamp 到 [0,1]。
                const double v = std::clamp(static_cast<double>(data[0]), 0.0, 1.0);
                p.probs[0] = v;
                if (outcome_count_ >= 2) p.probs[1] = 1.0 - v;
                p.normalized = (outcome_count_ == 2);
            } else {
                double sum = 0.0;
                const std::size_t take = std::min<std::size_t>(cnt, outcome_count_);
                for (std::size_t i = 0; i < take; ++i) {
                    p.probs[i] = static_cast<double>(data[i]);
                    sum += p.probs[i];
                }
                if (sum > 0.0) {
                    for (std::size_t i = 0; i < take; ++i) p.probs[i] /= sum;
                    p.normalized = true;
                }
            }
            p.ci_low = p.probs[0];
            p.ci_high = p.probs[0];
            p.ok = true;
        } catch (...) {
            p.ok = false;  // Run 异常 → fail-closed
        }
        return p;
    }

    [[nodiscard]] std::size_t expected_feature_count() const noexcept override { return feature_count_; }
    [[nodiscard]] std::size_t output_outcome_count() const noexcept override { return outcome_count_; }
    [[nodiscard]] ModelKind kind() const noexcept override { return ModelKind::Onnx; }
    [[nodiscard]] std::string_view model_id() const noexcept override { return model_id_; }
    [[nodiscard]] bool ready() const noexcept override { return ready_; }

private:
    Ort::Env env_;
    Ort::MemoryInfo mem_;
    std::unique_ptr<Ort::Session> session_;
    std::string in_name_;
    std::vector<std::string> out_names_;
    std::size_t feature_count_;
    std::size_t outcome_count_;
    std::string model_id_;
    bool ready_{false};
};

std::unique_ptr<FairValueModel> make_onnx_fair_value_model(const OnnxModelConfig& cfg) noexcept {
    if (cfg.onnx_path.empty()) return nullptr;
    std::error_code ec;
    if (!std::filesystem::exists(cfg.onnx_path, ec)) return nullptr;  // 无文件 → stub fallback
    try {
        auto m = std::make_unique<OnnxFairValueModel>(cfg);
        if (!m->ready()) return nullptr;
        return m;
    } catch (...) {
        return nullptr;  // 加载失败 → stub fallback
    }
}

}  // namespace stcpp::ml

#else  // ! STCPP_ONNX_ENABLED — onnxruntime 未链, stub fallback

namespace stcpp::ml {
std::unique_ptr<FairValueModel> make_onnx_fair_value_model(const OnnxModelConfig& cfg) noexcept {
    (void)cfg;
    return nullptr;
}
}  // namespace stcpp::ml

#endif
