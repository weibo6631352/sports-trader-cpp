// include/stcpp/ml/seq_arb_model.hpp — 短时套利序列模型接口 (Phase 2; 主计划 §3)
//
// Owner: 老雷 (GM) — 短时套利引擎 模块2/5
// last_review: 2026-06-01
//
// 与 FairValueModel (结算胜率) 物理并列、严禁 blend (主计划 §5.2; paper_loop:651 同教训)。
//   输入: 同源 110 列 FeatureVector (extract_full, BR-1)。
//   输出: per-horizon 预测 mid 移动 {dmid, ci_low, ci_high, confidence} (墙钟 8 horizon)。
//
// 设计哲学 (老板 2026-06-01): 模型出【信号】(dmid + CI + confidence), 决策层用 CI 下界过滤
//   (ci_low − BE > 0 才发; 点估计禁直接 sizing — 微利高频必死)。模型不内嵌硬阈值, 阈值在调用方。
//
// 安全: StubSeqArbModel 恒 ok=false (永不产可操作信号) — 真 ONNX 模型上线前, 套利分支不发任何单。
//   ML-R: stub 永不驱动决策 (同 FairValueModel stub 纪律)。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "stcpp/ml/fair_value_model.hpp"  // FeatureVector, ModelKind

namespace stcpp::ml {

// 墙钟 horizon 数 (对应 mid_return_label.hpp kWallHorizonsSec {2,3,5,10,12,15,30,60}s)。
inline constexpr std::size_t kArbHorizonCount = 8;

// 单 horizon 预测。NaN = 不可用 (stub / 推理失败)。
struct ArbHorizonPred {
    double dmid{std::numeric_limits<double>::quiet_NaN()};        // 预测 mid 移动 (signed)
    double ci_low{std::numeric_limits<double>::quiet_NaN()};      // CI 下界 (信号过滤: ci_low−BE>0 才发)
    double ci_high{std::numeric_limits<double>::quiet_NaN()};     // CI 上界
    double confidence{std::numeric_limits<double>::quiet_NaN()};  // 校准置信度 [0,1] (stub=NaN)
};

// 一次推理的全 horizon 预测 + 元数据 (ML-R8: 带 model_id + as_of_ts)。
struct ArbPrediction {
    bool ok{false};                  // false = 无可操作信号 (stub / 失败 / 特征数不符)
    std::string_view model_id{""};
    std::int64_t as_of_ts_ns{0};
    std::array<ArbHorizonPred, kArbHorizonCount> wall{};  // 默认全 NaN
};

// ---------------------------------------------------------------------------
// SeqArbModel — 抽象接口 (predict() const + 可重入, 热路径旁路多线程调用)。
// ---------------------------------------------------------------------------
class SeqArbModel {
public:
    virtual ~SeqArbModel() = default;
    [[nodiscard]] virtual ArbPrediction predict(const FeatureVector& fv) const noexcept = 0;
    [[nodiscard]] virtual std::size_t expected_feature_count() const noexcept = 0;
    [[nodiscard]] virtual ModelKind kind() const noexcept = 0;
    [[nodiscard]] virtual std::string_view model_id() const noexcept = 0;
    [[nodiscard]] virtual bool ready() const noexcept = 0;
};

// StubSeqArbModel — 占位 (真 ONNX 上线前)。恒 ok=false → 套利分支不产任何信号 (fail-safe)。
class StubSeqArbModel final : public SeqArbModel {
public:
    explicit StubSeqArbModel(std::size_t feature_count = 110,
                             std::string model_id_str = "stub-seq-arb-v0.1") noexcept
        : feature_count_(feature_count), model_id_(std::move(model_id_str)) {}

    [[nodiscard]] ArbPrediction predict(const FeatureVector& fv) const noexcept override {
        ArbPrediction p;
        p.model_id = model_id_;
        p.as_of_ts_ns = fv.as_of_ts_ns;
        p.ok = false;  // stub 永不产可操作信号 (安全: 无真模型不发单)
        return p;
    }
    [[nodiscard]] std::size_t expected_feature_count() const noexcept override { return feature_count_; }
    [[nodiscard]] ModelKind kind() const noexcept override { return ModelKind::Stub; }
    [[nodiscard]] std::string_view model_id() const noexcept override { return model_id_; }
    [[nodiscard]] bool ready() const noexcept override { return true; }

private:
    std::size_t feature_count_;
    std::string model_id_;
};

// ---------------------------------------------------------------------------
// OnnxSeqArbModel 工厂 — ONNX 多输出推理 (镜像 make_onnx_fair_value_model)。
//   模型输出 = [kArbHorizonCount × {dmid, ci_low, ci_high}] (confidence 由 conformal 校准或模型头)。
//   STCPP_ONNX_ENABLED + 文件存在 → 真 OnnxSeqArbModel; 否则 nullptr (调用方 fallback stub)。
//   实现在 seq_arb_model.cpp (Ort::Session, 与 OnnxFairValueModel 同封装; 装机后启用)。
// ---------------------------------------------------------------------------
struct OnnxSeqArbConfig {
    std::string model_path;
    std::size_t expected_feature_count = 110;  // = kMlFeatureCount (ml-feature-spec v0.12)
    std::string model_id;  // = onnx 文件 hash (复盘锚)
};

[[nodiscard]] std::unique_ptr<SeqArbModel> make_onnx_seq_arb_model(const OnnxSeqArbConfig& cfg) noexcept;

// make_seq_arb_model — 总工厂: ONNX 可用则用, 否则 stub (套利分支安全降级不发单)。
[[nodiscard]] inline std::unique_ptr<SeqArbModel> make_seq_arb_model(const OnnxSeqArbConfig& cfg) {
    if (!cfg.model_path.empty()) {
        if (auto m = make_onnx_seq_arb_model(cfg)) return m;
    }
    return std::make_unique<StubSeqArbModel>(cfg.expected_feature_count);
}

}  // namespace stcpp::ml
