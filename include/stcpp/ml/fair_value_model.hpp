// stcpp/ml/fair_value_model.hpp — FairValueModel 推理接口 v0.1 (ADR-037 骨架, 小邓 #31)
//
// Owner: 小邓 (ml-advisor, #31)
// last_review: 2026-05-29
//
// 目的:
//   定义 AI 量化"公允价值"模型的 *推理* 抽象 (训练侧不在本文件):
//     输入: feature_store 中间表示 (FeatureStoreGameRow / FeatureStoreBookRow)
//     输出: per-outcome fair probability (ModelPrediction)
//   ADR-037: 在 Goalserve inplay + Polymarket 订单簿特征上训练, ONNX 推理走 C++.
//   本 header 只定义接口 + ONNX 适配点 (W11+ 接真 ONNXRuntime), 当前内置 stub 实现.
//
// 关联:
//   include/stcpp/data/feature_store_contract.hpp   (FeatureStoreGameRow / BookRow — 唯一输入源)
//   include/stcpp/ml/model_feature_spec.hpp          (feature 抽取契约, 同 Wave)
//   docs/ADR/ADR-037 (AI 量化模型架构)
//
// 红线:
//   ML-R1  ML 不进 RM 决策路径 — FairValueModel 是只读推理, 输出 advisory prob, 不下 OrderIntent
//   ML-R2  paper 期不进生产决策 — 推理结果旁路消费 (回测 / 监控), 不喂 RM
//   ML-R5  推理走 ONNX/Treelite — 接口不依赖 pybind11 / Python; Python 仅离线训练导 ONNX
//   ML-R8  必带 model_id + feature 来源 ts — ModelPrediction 携带 model_id + as_of_ts_ns
//   walk-forward: 任何模型上线前必经 walk-forward backtest (ADR-037 §gate)
//   vendor-agnostic: 只消费 feature_store 中间表示, 不碰原始 Goalserve / Polymarket 字段
//
// 设计:
//   - 抽象基类 FairValueModel: 纯虚 predict(); 实现可为 StubFairValueModel (当前) /
//     OnnxFairValueModel (W11+, 加载 .onnx + ONNXRuntime C++ session).
//   - FeatureVector: 抽取层 (model_feature_spec.hpp) 产出的 flat float32 向量, 模型唯一吃这个.
//   - ModelPrediction: per-outcome fair prob + 元数据 (model_id / as_of_ts / 输入 feature 数).
//
// 不耻下问:
//   - ONNXRuntime C++ session API 集成 @老吴 (toolstack) + @老周 (热路径)
//   - walk-forward harness 与回测引擎对接 @小蒋 (#20 回测) + @小梁 (Sharpe 主权)
//   - per-outcome 标签定义 (Moneyline YES win / Totals over) @老彭 (Goalserve)

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stcpp::ml {

// ---------------------------------------------------------------------------
// kMaxOutcomes — 单 market 最大结果数.
//   Moneyline = 2 (YES/NO), 3-way (Home/Draw/Away) = 3, outright = N.
//   骨架阶段固定上界 8, 实际有效数由 ModelPrediction.num_outcomes 标记.
// ---------------------------------------------------------------------------
inline constexpr std::size_t kMaxOutcomes = 8;

// ---------------------------------------------------------------------------
// FeatureVector — 抽取层 (model_feature_spec.hpp) 输出的 flat float32 向量.
//   ONNX session 的输入 tensor 就是这个 buffer. 列顺序 = ModelFeatureSpec 锁定顺序.
//   NaN = 缺失 (训练侧 LightGBM/XGBoost 原生 missing; NN 侧需 impute, 见 spec).
// ---------------------------------------------------------------------------
struct FeatureVector {
    std::vector<float> values;                 // 长度 = spec.feature_count()
    std::int64_t as_of_ts_ns = 0;              // PIT 锚 (来自 feature_store row, ML-R8)
    std::string_view spec_version = "";        // model_feature_spec.hpp kSpecVersion

    [[nodiscard]] std::size_t size() const noexcept { return values.size(); }
    [[nodiscard]] bool empty() const noexcept { return values.empty(); }
    [[nodiscard]] std::span<const float> view() const noexcept {
        return std::span<const float>(values.data(), values.size());
    }
};

// ---------------------------------------------------------------------------
// ModelPrediction — 推理输出. per-outcome fair probability + 元数据.
//   probs[i] in [0,1]; sum over [0,num_outcomes) 应 ≈ 1 (normalized=true 时保证).
//   ok=false → 推理失败 (输入维度不匹配 / session 未加载 / NaN 阻断), probs 不可信.
// ---------------------------------------------------------------------------
struct ModelPrediction {
    std::array<double, kMaxOutcomes> probs{};   // per-outcome fair prob
    std::size_t num_outcomes = 0;               // 有效 outcome 数
    bool normalized = false;                    // probs 是否已归一化 (sum ≈ 1)
    bool ok = false;                            // 推理是否成功

    // ---- ML-R8 元数据: 可复盘 ----
    std::string_view model_id = "";             // 模型标识 (stub / onnx 文件 hash)
    std::int64_t as_of_ts_ns = 0;               // 输入 feature 的 PIT 锚
    std::size_t input_feature_count = 0;        // 实际喂入的 feature 数

    [[nodiscard]] double prob(std::size_t i) const noexcept {
        return (i < num_outcomes) ? probs[i] : std::numeric_limits<double>::quiet_NaN();
    }

    // 归一化自检: sum 在 [1-eps, 1+eps]
    [[nodiscard]] bool sum_ok(double eps = 1e-6) const noexcept {
        double s = 0.0;
        for (std::size_t i = 0; i < num_outcomes; ++i) s += probs[i];
        return (s >= 1.0 - eps) && (s <= 1.0 + eps);
    }
};

// ---------------------------------------------------------------------------
// ModelKind — 推理后端枚举 (适配点选择).
// ---------------------------------------------------------------------------
enum class ModelKind : std::uint8_t {
    Stub = 0,        // 内置确定性 stub (当前; 无外部依赖, 用于接口契约 + 回测管道打通)
    Onnx = 1,        // ONNXRuntime C++ session (W11+, 加载 .onnx)
    Treelite = 2,    // Treelite 编译树模型 (LightGBM/XGBoost 离线编译, W11+ 候选)
};

[[nodiscard]] constexpr std::string_view to_string(ModelKind k) noexcept {
    switch (k) {
        case ModelKind::Stub:     return "Stub";
        case ModelKind::Onnx:     return "Onnx";
        case ModelKind::Treelite: return "Treelite";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// FairValueModel — 推理抽象基类.
//   实现:
//     StubFairValueModel  — 当前; 确定性 sigmoid-style 映射, 无外部依赖.
//     OnnxFairValueModel  — W11+; 持 Ort::Session, predict() 跑 tensor.
//
//   线程模型: predict() 必须 const + 可重入 (热路径多线程旁路调用).
//             实现内部若持 session, 由实现保证 session run 线程安全 (ONNXRuntime
//             同一 session 多线程 Run 安全, 见 ONNXRuntime 文档).
// ---------------------------------------------------------------------------
class FairValueModel {
public:
    virtual ~FairValueModel() = default;

    // 推理: flat feature vector → per-outcome fair prob.
    //   要求 fv.size() == expected_feature_count(); 否则返回 ok=false.
    [[nodiscard]] virtual ModelPrediction predict(const FeatureVector& fv) const noexcept = 0;

    // 该模型期望的 feature 维度 (= 训练时锁定的 ModelFeatureSpec.feature_count()).
    [[nodiscard]] virtual std::size_t expected_feature_count() const noexcept = 0;

    // 该模型输出的 outcome 数 (Moneyline=2, 3-way=3, ...).
    [[nodiscard]] virtual std::size_t output_outcome_count() const noexcept = 0;

    [[nodiscard]] virtual ModelKind kind() const noexcept = 0;
    [[nodiscard]] virtual std::string_view model_id() const noexcept = 0;

    // 模型是否就绪 (session 已加载 / stub 永远 true).
    [[nodiscard]] virtual bool ready() const noexcept = 0;
};

// ---------------------------------------------------------------------------
// StubFairValueModel — 确定性 stub 推理 (无外部依赖).
//
//   目的: W11 前打通接口契约 + feature 抽取 + 回测管道, 不需要真 ONNX runtime.
//   映射: 取 feature 向量加权和 → logistic, 产出 2-outcome (YES/NO) 归一化 prob.
//         加权和确定性 (固定权重), 同输入同输出 — 满足回测可复现要求.
//   NaN 处理: 缺失 feature 视为 0 贡献 (stub 语义; 真模型走 spec 的 impute 策略).
//
//   注: stub 不是预测精度模型, 只是"接口 + 管道"占位. 真模型 W11+ 替换为 Onnx 实现,
//       接口不变 (开闭原则), 调用方零改动.
// ---------------------------------------------------------------------------
class StubFairValueModel final : public FairValueModel {
public:
    explicit StubFairValueModel(std::size_t feature_count,
                                std::size_t outcome_count = 2,
                                std::string model_id_str = "stub-fair-value-v0.1") noexcept
        : feature_count_(feature_count),
          outcome_count_(outcome_count == 0
                             ? 2
                             : (outcome_count > kMaxOutcomes ? kMaxOutcomes : outcome_count)),
          model_id_(std::move(model_id_str)) {}

    [[nodiscard]] ModelPrediction predict(const FeatureVector& fv) const noexcept override {
        ModelPrediction p;
        p.model_id = model_id_;
        p.as_of_ts_ns = fv.as_of_ts_ns;
        p.input_feature_count = fv.size();
        p.num_outcomes = outcome_count_;

        // 维度契约: 不匹配 → ok=false (调用方必须先对齐 spec).
        if (fv.size() != feature_count_) {
            p.ok = false;
            return p;
        }

        // 确定性加权和 (固定权重 = 衰减序列, NaN 视为 0).
        double acc = 0.0;
        std::size_t i = 0;
        for (float v : fv.values) {
            const double fv_d = (v == v) ? static_cast<double>(v) : 0.0;  // NaN -> 0
            const double w = 1.0 / static_cast<double>(i + 1);            // 1, 1/2, 1/3, ...
            acc += w * fv_d;
            ++i;
        }

        // logistic 映射到 (0,1) 作为 outcome[0] 的 prob.
        const double p0 = logistic(acc);

        if (outcome_count_ == 2) {
            p.probs[0] = p0;
            p.probs[1] = 1.0 - p0;
            p.normalized = true;
        } else {
            // 多 outcome: softmax over 衰减加权的 per-outcome shift (确定性占位).
            double sum = 0.0;
            for (std::size_t o = 0; o < outcome_count_; ++o) {
                const double z = acc - static_cast<double>(o);  // 确定性 shift
                const double e = std::exp(z - acc);             // 数值稳定 (减 max≈acc)
                p.probs[o] = e;
                sum += e;
            }
            if (sum > 0.0) {
                for (std::size_t o = 0; o < outcome_count_; ++o) p.probs[o] /= sum;
                p.normalized = true;
            }
        }

        p.ok = true;
        return p;
    }

    [[nodiscard]] std::size_t expected_feature_count() const noexcept override {
        return feature_count_;
    }
    [[nodiscard]] std::size_t output_outcome_count() const noexcept override {
        return outcome_count_;
    }
    [[nodiscard]] ModelKind kind() const noexcept override { return ModelKind::Stub; }
    [[nodiscard]] std::string_view model_id() const noexcept override { return model_id_; }
    [[nodiscard]] bool ready() const noexcept override { return true; }

private:
    static double logistic(double x) noexcept {
        // 数值稳定 logistic.
        if (x >= 0.0) {
            const double z = std::exp(-x);
            return 1.0 / (1.0 + z);
        }
        const double z = std::exp(x);
        return z / (1.0 + z);
    }

    std::size_t feature_count_;
    std::size_t outcome_count_;
    std::string model_id_;
};

// ---------------------------------------------------------------------------
// ONNX 适配点 (W11+ stub, 当前不编译真 runtime).
//
//   make_onnx_fair_value_model(): 工厂. 当前返回 nullptr (runtime 未集成),
//   W11+ 接 ONNXRuntime C++: 内部持 Ort::Env + Ort::Session, predict() 跑 tensor.
//   保持工厂签名稳定, 调用方 (回测 / 监控) 通过 unique_ptr<FairValueModel> 持有,
//   stub→onnx 切换零改调用方代码.
//
//   注: 真集成走 src/stcpp/ml/onnx_fair_value_model.cpp (W11+), 链 onnxruntime.
//       该文件当前不存在, 工厂在 src 实现里返回 nullptr (见 fair_value_model.cpp).
// ---------------------------------------------------------------------------
struct OnnxModelConfig {
    std::string onnx_path;             // .onnx 文件路径 (W11+ 由训练侧导出)
    std::size_t expected_feature_count = 0;
    std::size_t output_outcome_count = 2;
    std::string model_id;              // 通常 = onnx 文件 blake3 hash (复盘锚)
    int intra_op_threads = 1;          // 热路径旁路: 单线程足够, 不抢 CPU
};

// 工厂: 当前返回 nullptr (ONNXRuntime 未集成); W11+ 返回真 OnnxFairValueModel.
//   调用方判 nullptr → 回落 StubFairValueModel.
[[nodiscard]] std::unique_ptr<FairValueModel> make_onnx_fair_value_model(
    const OnnxModelConfig& cfg) noexcept;

}  // namespace stcpp::ml
