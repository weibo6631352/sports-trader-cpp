// include/stcpp/ml/feature_vector_hub.hpp — 完整 75 列 FeatureVector 发布器 (Phase 2 项6)
//
// Owner: 老雷 (GM) — Phase 2 完整训练 X (extract_full 75 列, 含 0-17 原始 game/book)
// last_review: 2026-05-31
//
// 背景: FeatureRecorder 读 quote_hub 落 QuoteFeatures, 缺 0-17 原始列 (score_diff/b_mid 等)。
//   完整 75 列训练 X = extract_full(game_row, book_row, qf) 输出 — PublishQuoteSnapshot 已算 fv (predict 用)。
//   本 hub 让 paper_loop 在 loop_thread_ 把 fv Publish 进来 (短锁 ~POD copy, 非 WSS event loop),
//   独立 FeatureVectorRecorder 线程读快照落盘 (IO 离决策线程) — 同 FeatureRecorder 范式, 但完整向量。
//
// R-12: Publish 短锁 = 一次 POD record copy (~320B, <1us); 非 WSS event loop (paper loop_thread_)。
//   map 按 condition_id keyed, 大小 = #conditions (bounded, 每 Publish 覆盖同 cid)。
// R-20: record 携带 as_of_ts_ns (PIT 锚, extract_full 透传 row 的 as_of; 禁本地 now())。
// 红线: 离线训练捕获, 绝不回喂决策 (CLV 前视); recorder 落独立训练文件与 live 隔离。
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "stcpp/ml/model_feature_spec.hpp"  // kMlFeatureCount

namespace stcpp::ml {

// ---------------------------------------------------------------------------
// FeatureVectorRecord — 完整 75 列 FeatureVector 的 POD 快照 (double-buffer/copy 安全)。
// ---------------------------------------------------------------------------
struct FeatureVectorRecord {
    char condition_id[72]{};            // bytes32 hex
    std::int64_t as_of_ts_ns{0};        // PIT 锚 (extract_full 透传 row as_of)
    char spec_version[32]{};            // = kSpecVersion (列序契约版本; 训练侧对齐用)
    std::uint16_t count{0};             // 有效列数 (= kMlFeatureCount)
    float values[kMlFeatureCount]{};    // 75 列 (列序 = MlFeature enum)
    double baseline_fair{0.0};          // 缺口B: baseline fair (qf.fair_value; 残差训练 y=label−baseline 用,
                                        //   非 MlFeature 列 — 是泄漏目标不入 X, 仅作残差标签锚)
    double line{std::numeric_limits<double>::quiet_NaN()};  // totals/spreads 线值 (元数据旁注; 解释派生盘口)
    bool valid{false};

    void set_condition(std::string_view cid) noexcept {
        const std::size_t n = std::min(cid.size(), sizeof(condition_id) - 1);
        std::memcpy(condition_id, cid.data(), n);
        condition_id[n] = '\0';
    }
    void set_spec(std::string_view s) noexcept {
        const std::size_t n = std::min(s.size(), sizeof(spec_version) - 1);
        std::memcpy(spec_version, s.data(), n);
        spec_version[n] = '\0';
    }
    void set_values(const std::vector<float>& v) noexcept {
        count = static_cast<std::uint16_t>(std::min<std::size_t>(v.size(), kMlFeatureCount));
        for (std::size_t i = 0; i < count; ++i) values[i] = v[i];
        for (std::size_t i = count; i < kMlFeatureCount; ++i) values[i] = 0.0f;
    }
};

// ---------------------------------------------------------------------------
// FeatureVectorHub — per-condition 完整向量快照 (单 writer paper_loop / 单 reader recorder)。
//   mutex map: Publish 短锁 copy in; SnapshotAll/Read 短锁 copy out。bounded (#conditions)。
// ---------------------------------------------------------------------------
class FeatureVectorHub {
public:
    FeatureVectorHub() = default;
    FeatureVectorHub(const FeatureVectorHub&) = delete;
    FeatureVectorHub& operator=(const FeatureVectorHub&) = delete;

    // Publish — paper_loop loop_thread_ (短锁 POD copy)。覆盖同 cid 最新快照。
    void Publish(const FeatureVectorRecord& r) noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        map_[std::string(r.condition_id)] = r;
    }

    [[nodiscard]] std::optional<FeatureVectorRecord> Read(const std::string& cid) const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        const auto it = map_.find(cid);
        if (it == map_.end()) return std::nullopt;
        return it->second;
    }

    // SnapshotAll — recorder 线程一次性取全量 (短锁 copy)。
    [[nodiscard]] std::vector<FeatureVectorRecord> SnapshotAll() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<FeatureVectorRecord> out;
        out.reserve(map_.size());
        for (const auto& [_cid, rec] : map_) out.push_back(rec);
        return out;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return map_.size();
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, FeatureVectorRecord> map_;
};

}  // namespace stcpp::ml
