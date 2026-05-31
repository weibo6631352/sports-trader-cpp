// include/stcpp/ml/feature_vector_recorder.hpp — 完整 75 列向量落盘 (Phase 2 项6)
//
// Owner: 老雷 (GM) — Phase 2 完整训练 X
// last_review: 2026-05-31
//
// 独立线程读 FeatureVectorHub 快照 → 落 JSONL (完整 75 列, 列序 = MlFeature enum)。
//   IO 离决策线程 (同 FeatureRecorder 范式)。去重 by (condition_id, as_of_ts_ns)。
//   输出行: {"condition_id":"..","as_of_ts_ns":N,"spec_version":"..","f0":v,...,"f74":v}
//   label_pipeline.ExtractConditionId 直接可 join (首字段 condition_id)。
#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>

#include "stcpp/ml/feature_vector_hub.hpp"
#include "stcpp/ml/model_feature_spec.hpp"  // to_string(MlFeature) 列名 (可选)

namespace stcpp::ml {

class FeatureVectorRecorder {
public:
    struct Config {
        std::string output_path = "data/ml_capture/feature_vectors.jsonl";
        int poll_interval_sec = 5;
    };

    FeatureVectorRecorder(const FeatureVectorHub& hub, Config cfg)
        : hub_(hub), cfg_(std::move(cfg)) {}

    ~FeatureVectorRecorder() { Stop(); }
    FeatureVectorRecorder(const FeatureVectorRecorder&) = delete;
    FeatureVectorRecorder& operator=(const FeatureVectorRecorder&) = delete;

    void Start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this] { Run(); });
    }
    void Stop() noexcept {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
    }
    [[nodiscard]] std::uint64_t records_written() const noexcept {
        return records_written_.load(std::memory_order_relaxed);
    }

private:
    void Run() {
        std::error_code ec;
        const std::filesystem::path p(cfg_.output_path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream out(cfg_.output_path, std::ios::app);
        if (!out.is_open()) {
            std::fprintf(stderr, "[fv_recorder] WARN: 无法打开 %s, 完整向量采集禁用\n",
                         cfg_.output_path.c_str());
            running_.store(false, std::memory_order_relaxed);
            return;
        }
        std::fprintf(stderr, "[fv_recorder] 完整 %zu 列向量采集启动 -> %s (每 %ds 去重落盘)\n",
                     static_cast<std::size_t>(kMlFeatureCount), cfg_.output_path.c_str(),
                     cfg_.poll_interval_sec);

        while (running_.load(std::memory_order_relaxed)) {
            for (const auto& r : hub_.SnapshotAll()) {
                if (!r.valid) continue;
                const std::string cond(r.condition_id);
                auto it = last_ts_.find(cond);
                if (it != last_ts_.end() && it->second >= r.as_of_ts_ns) continue;  // 去重
                last_ts_[cond] = r.as_of_ts_ns;
                WriteLine(out, r);
                records_written_.fetch_add(1, std::memory_order_relaxed);
            }
            out.flush();
            const int slices = cfg_.poll_interval_sec * 10;
            for (int i = 0; i < slices && running_.load(std::memory_order_relaxed); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        out.flush();
    }

    // 数值 → JSON 安全串。浮点 NaN/Inf → "null" (C++ << 对 NaN 输出小写 "nan" 是非法 JSON, 破坏 Python
    //   json.loads 整行)。values[] 经 extract_full 常含 NaN (缺失特征), 全过此杜绝 nan 泄漏。整数全精度。
    template <class T>
    static std::string JsonNum(T v) {
        char buf[40];
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(static_cast<double>(v)))
                return "null";
            std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
        } else {
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        }
        return buf;
    }

    static void WriteLine(std::ofstream& out, const FeatureVectorRecord& r) {
        out << "{\"condition_id\":\"" << r.condition_id << "\",\"as_of_ts_ns\":" << JsonNum(r.as_of_ts_ns)
            << ",\"spec_version\":\"" << r.spec_version << "\",\"fair_value\":" << JsonNum(r.baseline_fair)
            << ",\"line\":" << JsonNum(r.line);
        for (std::uint16_t i = 0; i < r.count; ++i) {
            out << ",\"f" << i << "\":" << JsonNum(r.values[i]);  // f0..f85 (含 cat 82-85), NaN→null
        }
        out << "}\n";  // fair_value = 残差训练 baseline 锚 (非 X 列; 训练侧 y=label−fair_value)
    }

    const FeatureVectorHub& hub_;
    Config cfg_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::unordered_map<std::string, std::int64_t> last_ts_;
    std::atomic<std::uint64_t> records_written_{0};
};

}  // namespace stcpp::ml
