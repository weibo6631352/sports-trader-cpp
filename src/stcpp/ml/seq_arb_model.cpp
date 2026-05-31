// src/stcpp/ml/seq_arb_model.cpp — OnnxSeqArbModel (多输出 ONNX 推理; 镜像 OnnxFairValueModel)
//
// Owner: 老雷 (GM) — 短时套利引擎 模块2/5
// last_review: 2026-06-01
//
// STCPP_ONNX_ENABLED 开 + 文件存在 → Ort::Session 多输出推理; 否则 make_onnx_seq_arb_model 返 nullptr
//   (调用方 make_seq_arb_model fallback StubSeqArbModel — 套利分支安全降级不发单)。
//
// 模型输出契约 (train_seq_arb.py 导出):
//   flat float[kArbHorizonCount] = 各 horizon 预测 dmid; 或 flat[kArbHorizonCount*3] = {dmid,ci_low,ci_high}
//   per horizon (confidence 留 NaN, 由 conformal 离线校准, runtime 不算)。
#include "stcpp/ml/seq_arb_model.hpp"

#ifdef STCPP_ONNX_ENABLED

#include <onnxruntime_cxx_api.h>

#include <filesystem>
#include <string>
#include <vector>

namespace stcpp::ml {

class OnnxSeqArbModel final : public SeqArbModel {
public:
    explicit OnnxSeqArbModel(const OnnxSeqArbConfig& cfg)
        : env_(ORT_LOGGING_LEVEL_WARNING, "seq_arb"),
          mem_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
          feature_count_(cfg.expected_feature_count),
          model_id_(cfg.model_id) {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);  // 热路径低延迟 (主计划 §5.3)
        session_ = std::make_unique<Ort::Session>(env_, cfg.model_path.c_str(), opts);
        Ort::AllocatorWithDefaultOptions alloc;
        in_name_ = session_->GetInputNameAllocated(0, alloc).get();
        out_name_ = session_->GetOutputNameAllocated(0, alloc).get();
        ready_ = true;
    }

    [[nodiscard]] ArbPrediction predict(const FeatureVector& fv) const noexcept override {
        ArbPrediction p;
        p.model_id = model_id_;
        p.as_of_ts_ns = fv.as_of_ts_ns;
        if (!ready_ || fv.values.size() != feature_count_) return p;  // ok=false fail-closed
        try {
            std::vector<float> input(fv.values.begin(), fv.values.end());
            const std::array<std::int64_t, 2> shape{1, static_cast<std::int64_t>(input.size())};
            Ort::Value in_tensor = Ort::Value::CreateTensor<float>(mem_, input.data(), input.size(),
                                                                   shape.data(), shape.size());
            const char* in_names[] = {in_name_.c_str()};
            const char* out_names[] = {out_name_.c_str()};
            auto outs = session_->Run(Ort::RunOptions{nullptr}, in_names, &in_tensor, 1, out_names, 1);
            const auto info = outs[0].GetTensorTypeAndShapeInfo();
            const std::size_t cnt = info.GetElementCount();
            const float* data = outs[0].GetTensorData<float>();
            if (cnt == kArbHorizonCount) {  // 仅 dmid
                for (std::size_t h = 0; h < kArbHorizonCount; ++h)
                    p.wall[h].dmid = static_cast<double>(data[h]);
                p.ok = true;
            } else if (cnt == kArbHorizonCount * 3) {  // {dmid,ci_low,ci_high}×H
                for (std::size_t h = 0; h < kArbHorizonCount; ++h) {
                    p.wall[h].dmid = static_cast<double>(data[h * 3 + 0]);
                    p.wall[h].ci_low = static_cast<double>(data[h * 3 + 1]);
                    p.wall[h].ci_high = static_cast<double>(data[h * 3 + 2]);
                }
                p.ok = true;
            }  // 其它形状 → ok=false (契约不符, fail-closed)
        } catch (...) {
            p.ok = false;  // 推理异常 → 不产信号 (fail-closed)
        }
        return p;
    }
    [[nodiscard]] std::size_t expected_feature_count() const noexcept override { return feature_count_; }
    [[nodiscard]] ModelKind kind() const noexcept override { return ModelKind::Onnx; }
    [[nodiscard]] std::string_view model_id() const noexcept override { return model_id_; }
    [[nodiscard]] bool ready() const noexcept override { return ready_; }

private:
    Ort::Env env_;
    Ort::MemoryInfo mem_;
    std::unique_ptr<Ort::Session> session_;
    std::string in_name_, out_name_;
    std::size_t feature_count_;
    std::string model_id_;
    bool ready_{false};
};

std::unique_ptr<SeqArbModel> make_onnx_seq_arb_model(const OnnxSeqArbConfig& cfg) noexcept {
    try {
        if (cfg.model_path.empty() || !std::filesystem::exists(cfg.model_path)) return nullptr;
        return std::make_unique<OnnxSeqArbModel>(cfg);
    } catch (...) {
        return nullptr;  // 装载失败 → nullptr (调用方 fallback stub)
    }
}

}  // namespace stcpp::ml

#else  // ! STCPP_ONNX_ENABLED — onnxruntime 未链, fallback stub

namespace stcpp::ml {
std::unique_ptr<SeqArbModel> make_onnx_seq_arb_model(const OnnxSeqArbConfig&) noexcept {
    return nullptr;  // 调用方 make_seq_arb_model → StubSeqArbModel
}
}  // namespace stcpp::ml

#endif
