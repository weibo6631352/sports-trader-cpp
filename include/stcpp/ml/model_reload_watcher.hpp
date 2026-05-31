// include/stcpp/ml/model_reload_watcher.hpp — 模型热加载触发器 (生产侧 watcher; 模块7)
//
// Owner: 老雷 (GM) — 短时套利引擎 模块7 (热加载时机/编排)
// last_review: 2026-06-01
//
// 回答"热加载时机": 后台 jthread 周期扫 blessed manifest (训练进程验证通过后写) → 检测到【验证通过且
//   id 变了】的新模型 → make_seq_arb_model 加载 → 回调热换 (SetSeqArbModelShared) → 审计。
//
// 安全门 (核心): manifest 必须 validated=true 才换 —— 绝不"训完就换", 只换【walk-forward 净胜当前的】新模型。
//   坏模型不进生产。整条仍 advisory (未开闸动真钱), 热换风险可控; 保留旧 model_id 供回滚审计。
// R-12: 独立 jthread, 加载 IO 在本线程; 决策线程只 Load() 拿 copy (ns 级), 互不阻塞。
// 红线: 训练 Python 离线产 ONNX + manifest; 生产 C++ 只读 manifest + 加载推理。
#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "stcpp/ml/seq_arb_model.hpp"

namespace stcpp::ml {

// blessed manifest 解析结果 (训练进程写; 生产读)。
struct ReloadDecision {
    std::string model_path;
    std::string model_id;
    double val_metric{0.0};  // walk-forward 验证指标 (审计用)
};

namespace detail {
// 从 manifest JSON 抽 "key":"<str>"。
[[nodiscard]] inline std::optional<std::string> ManifestStr(std::string_view j, std::string_view key) {
    std::string needle = "\"";
    needle += key;
    needle += "\":\"";
    const auto k = j.find(needle);
    if (k == std::string_view::npos) return std::nullopt;
    const auto vs = k + needle.size();
    const auto ve = j.find('"', vs);
    if (ve == std::string_view::npos) return std::nullopt;
    return std::string(j.substr(vs, ve - vs));
}
[[nodiscard]] inline bool ManifestTrue(std::string_view j, std::string_view key) {
    std::string n1 = "\"";
    n1 += key;
    n1 += "\":true";
    std::string n2 = "\"";
    n2 += key;
    n2 += "\":1";
    return j.find(n1) != std::string_view::npos || j.find(n2) != std::string_view::npos;
}
[[nodiscard]] inline double ManifestNum(std::string_view j, std::string_view key, double dflt) {
    std::string needle = "\"";
    needle += key;
    needle += "\":";
    const auto k = j.find(needle);
    if (k == std::string_view::npos) return dflt;
    std::size_t p = k + needle.size();
    while (p < j.size() && (j[p] == ' ' || j[p] == '"')) ++p;
    const char* s = j.data() + p;
    char* e = nullptr;
    const double v = std::strtod(s, &e);
    return (e == s) ? dflt : v;
}
}  // namespace detail

// ParseReloadManifest — 纯函数核 (可单测)。决定该不该热换:
//   validated=true (安全门!) ∧ model_id 非空 ∧ 与 current 不同 → 返回 decision; 否则 nullopt。
[[nodiscard]] inline std::optional<ReloadDecision> ParseReloadManifest(std::string_view manifest_json,
                                                                       std::string_view current_model_id) {
    if (!detail::ManifestTrue(manifest_json, "validated")) return std::nullopt;  // 未验证 → 绝不换
    const auto id = detail::ManifestStr(manifest_json, "model_id");
    const auto path = detail::ManifestStr(manifest_json, "model_path");
    if (!id || !path || id->empty() || path->empty()) return std::nullopt;
    if (*id == current_model_id) return std::nullopt;  // 没变 → 不换
    ReloadDecision d;
    d.model_id = *id;
    d.model_path = *path;
    d.val_metric = detail::ManifestNum(manifest_json, "val_metric", 0.0);
    return d;
}

struct ModelReloadConfig {
    std::string manifest_path;       // blessed manifest (训练进程写)
    int poll_interval_sec = 60;
    std::size_t expected_feature_count = 110;  // = kMlFeatureCount
};

// ModelReloadWatcher — 后台 jthread: 周期检测 blessed 新模型 → 加载 → 回调热换 → 审计。
class ModelReloadWatcher {
public:
    using ReloadCallback = std::function<void(std::shared_ptr<const SeqArbModel>)>;

    ModelReloadWatcher(ModelReloadConfig cfg, ReloadCallback on_reload)
        : cfg_(std::move(cfg)), on_reload_(std::move(on_reload)) {}
    ~ModelReloadWatcher() { Stop(); }
    ModelReloadWatcher(const ModelReloadWatcher&) = delete;
    ModelReloadWatcher& operator=(const ModelReloadWatcher&) = delete;

    void Start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this] { Run(); });
    }
    void Stop() noexcept {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
    }
    [[nodiscard]] std::uint64_t reloads() const noexcept { return reloads_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::string current_model_id() const { return current_id_; }

    // CheckOnce — 单次检测 (可单测/手动触发)。读 manifest → ParseReloadManifest → 加载 → 回调 → 审计。
    //   返回是否换了。
    bool CheckOnce() {
        std::ifstream in(cfg_.manifest_path);
        if (!in.is_open()) return false;
        std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto d = ParseReloadManifest(json, current_id_);
        if (!d) return false;
        if (!std::filesystem::exists(d->model_path)) return false;
        OnnxSeqArbConfig oc;
        oc.model_path = d->model_path;
        oc.expected_feature_count = cfg_.expected_feature_count;
        oc.model_id = d->model_id;
        auto model = make_seq_arb_model(oc);  // ONNX 加载 (本 watcher 线程, 非决策线程)
        if (!model || !model->ready()) {
            std::fprintf(stderr, "[reload] WARN: 加载失败 %s (id=%s) — 不换, 留旧模型\n",
                         d->model_path.c_str(), d->model_id.c_str());
            return false;
        }
        const std::string old_id = current_id_;
        on_reload_(std::shared_ptr<const SeqArbModel>(std::move(model)));  // 热换
        current_id_ = d->model_id;
        reloads_.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[reload] 热换模型: %s → %s (val_metric=%.4f)\n",
                     old_id.empty() ? "(none)" : old_id.c_str(), d->model_id.c_str(), d->val_metric);
        return true;
    }

private:
    void Run() {
        while (running_.load(std::memory_order_relaxed)) {
            CheckOnce();
            const int slices = cfg_.poll_interval_sec * 10;  // 100ms 粒度响应 Stop
            for (int i = 0; i < slices && running_.load(std::memory_order_relaxed); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    ModelReloadConfig cfg_;
    ReloadCallback on_reload_;
    std::string current_id_;  // 当前生产模型 id (回滚审计锚)
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::atomic<std::uint64_t> reloads_{0};
};

}  // namespace stcpp::ml
