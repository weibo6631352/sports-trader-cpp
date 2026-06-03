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
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace stcpp::ml {

namespace {
// ---- 极简 sidecar JSON 标量解析 (我们自己训练侧产的受控格式 <onnx>.meta.json; 无三方依赖) ----
//   只取顶层标量 key (confidence/ci_halfwidth/calibrated/calib_method); 容错: 缺/坏 → 返 false。
[[nodiscard]] bool JsonNum(const std::string& s, const char* key, double& out) noexcept {
    const std::string k = std::string("\"") + key + "\"";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p = s.find(':', p + k.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    if (s.compare(p, 4, "null") == 0) return false;  // JSON null (如 auc 缺)
    const char* start = s.c_str() + p;
    char* end = nullptr;
    const double v = std::strtod(start, &end);
    if (end == start) return false;
    out = v;
    return true;
}
[[nodiscard]] bool JsonBool(const std::string& s, const char* key, bool& out) noexcept {
    const std::string k = std::string("\"") + key + "\"";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p = s.find(':', p + k.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    if (s.compare(p, 4, "true") == 0) { out = true; return true; }
    if (s.compare(p, 5, "false") == 0) { out = false; return true; }
    return false;
}
[[nodiscard]] bool JsonStr(const std::string& s, const char* key, std::string& out) noexcept {
    const std::string k = std::string("\"") + key + "\"";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p = s.find(':', p + k.size());
    if (p == std::string::npos) return false;
    const auto q = s.find('"', p + 1);
    if (q == std::string::npos) return false;
    const auto e = s.find('"', q + 1);
    if (e == std::string::npos) return false;
    out = s.substr(q + 1, e - q - 1);
    return true;
}
}  // namespace

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
        LoadMeta(cfg.onnx_path);  // 校准 sidecar <onnx>.meta.json (训练侧产) → confidence/calibrated/CI
    }

    // LoadMeta — 读 <onnx>.meta.json (训练侧 holdout 评估产), 填校准元数据。
    //   fail-safe: 无文件/解析失败/无 confidence 字段 → meta_loaded_=false → 推理报 conf=0/未校准
    //   (降级语义同 stub, 前端保持"占位"; 模型纯 advisory, 不影响 prob 输出 / 不 gate 交易)。
    void LoadMeta(const std::string& onnx_path) noexcept {
        try {
            const std::string meta_path = onnx_path + ".meta.json";
            std::ifstream f(meta_path);
            if (!f) return;
            const std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (s.empty()) return;
            double conf = 0.0;
            if (!JsonNum(s, "confidence", conf)) return;  // 无 confidence → 视为无效 meta, 降级
            double ci = 0.0;
            (void)JsonNum(s, "ci_halfwidth", ci);  // 可选: 缺 → 0 (零宽区间)
            bool cal = false;
            (void)JsonBool(s, "calibrated", cal);  // 可选: 缺 → false
            std::string method;
            (void)JsonStr(s, "calib_method", method);  // 可选: 缺 → None
            // Platt 校准 (a,b): calibrated_p = sigmoid(a·logit(raw_p)+b)。治模型过度自信 (优化模型,
            //   非缩减场景)。缺 → 单位映射 (a=1,b=0 = 不变, 向后兼容老 sidecar)。
            double pa = 1.0, pb = 0.0;
            (void)JsonNum(s, "platt_a", pa);
            (void)JsonNum(s, "platt_b", pb);
            // residual 模式 (2026-06-03 治"只买 YES"): mode=="residual" → 模型输出对 baseline(b_mid 市价,
            //   特征索引 baseline_idx, 默认 F_MID=8)的增量 delta, predict() 做 fair=clip(b_mid+delta)。
            //   缺 mode 字段 → regress (向后兼容老 sidecar + selftest 回归 fixture)。
            std::string mode;
            (void)JsonStr(s, "mode", mode);
            const bool residual = (mode == "residual");
            double bidx = 8.0;
            (void)JsonNum(s, "baseline_idx", bidx);
            meta_confidence_ = std::clamp(conf, 0.0, 1.0);
            meta_ci_hw_ = std::clamp(ci, 0.0, 1.0);
            meta_calibrated_ = cal;
            meta_residual_ = residual;
            meta_baseline_idx_ = (bidx >= 0.0 && bidx < 1024.0) ? static_cast<std::size_t>(bidx) : 8;
            meta_platt_a_ = std::isfinite(pa) ? pa : 1.0;
            meta_platt_b_ = std::isfinite(pb) ? pb : 0.0;
            meta_calib_ = (method.rfind("platt", 0) == 0) ? CalibMethod::Conformal
                          : (method == "conformal") ? CalibMethod::Conformal
                          : (method == "isotonic") ? CalibMethod::Isotonic
                          : (method == "ensemble") ? CalibMethod::Ensemble
                                                   : CalibMethod::None;
            meta_loaded_ = true;
        } catch (...) {
            meta_loaded_ = false;  // 任何异常 → fail-safe 降级
        }
    }

    [[nodiscard]] ModelPrediction predict(const FeatureVector& fv) const noexcept override {
        ModelPrediction p;
        p.model_id = model_id_;
        p.as_of_ts_ns = fv.as_of_ts_ns;
        p.input_feature_count = fv.size();
        p.spec_version = fv.spec_version;
        p.num_outcomes = outcome_count_;
        // 校准元数据 (sidecar): 有则填真值, 无则降级 (conf=0/未校准, 同 stub; 前端据此"占位")。
        p.confidence = meta_loaded_ ? meta_confidence_ : 0.0;
        p.calibrated = meta_loaded_ ? meta_calibrated_ : false;
        p.calib_method = meta_loaded_ ? meta_calib_ : CalibMethod::None;
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
                double v;
                if (meta_loaded_ && meta_residual_) {
                    // residual 模式 (2026-06-03 治"只买 YES"): 模型输出 = 对 baseline(b_mid 市价)的【增量
                    //   delta】(可负)。fair = clip(fv[baseline_idx] + delta, 0, 1)。edge = fair − 市价 = delta
                    //   天然两边 (delta>0 买 YES / <0 买 NO / ≈0 无 edge 不交易), 根治 regress+Platt 全局
                    //   把准确低预测往 0.5 抬 → fair 系统性 > 市价 → 只买 YES 的退化。baseline 从特征向量
                    //   自取 (训练 y=label−feats[F_MID] 与推理同源 b_mid, BR-1 零漂移); delta 非概率, 不套 Platt。
                    const double base = (meta_baseline_idx_ < fv.values.size())
                                            ? static_cast<double>(fv.values[meta_baseline_idx_])
                                            : 0.5;
                    v = std::clamp(base + static_cast<double>(data[0]), 0.0, 1.0);
                } else {
                    // 回归: 单值 = p_yes。clamp [0,1] → Platt 校准 (治过度自信) → 最终 p_yes。
                    v = std::clamp(static_cast<double>(data[0]), 0.0, 1.0);
                    if (meta_loaded_ && (meta_platt_a_ != 1.0 || meta_platt_b_ != 0.0)) {
                        // calibrated_p = sigmoid(a·logit(v)+b); v clamp 防 logit 发散。
                        const double vc = std::clamp(v, 1e-4, 1.0 - 1e-4);
                        const double logit = std::log(vc / (1.0 - vc));
                        v = 1.0 / (1.0 + std::exp(-(meta_platt_a_ * logit + meta_platt_b_)));
                    }
                }
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
            // 预测区间 (conformal 半宽来自 sidecar; 无 meta → 零宽, 同旧行为)。
            const double hw = meta_loaded_ ? meta_ci_hw_ : 0.0;
            p.ci_low = std::clamp(p.probs[0] - hw, 0.0, 1.0);
            p.ci_high = std::clamp(p.probs[0] + hw, 0.0, 1.0);
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
    // 校准 sidecar (<onnx>.meta.json) 加载状态 — fail-safe: 未加载 → conf=0/未校准。
    bool meta_loaded_{false};
    bool meta_calibrated_{false};
    double meta_confidence_{0.0};
    double meta_ci_hw_{0.0};
    double meta_platt_a_{1.0};  // Platt 校准斜率 (logit 空间); 1.0 = 不变
    double meta_platt_b_{0.0};  // Platt 校准截距; 0.0 = 不变
    bool meta_residual_{false};        // residual 模式: 模型输出 delta, fair=clip(b_mid+delta) (治"只买YES")
    std::size_t meta_baseline_idx_{8}; // residual baseline 特征索引 (F_MID/b_mid=8; 训练=推理同源)
    CalibMethod meta_calib_{CalibMethod::None};
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
