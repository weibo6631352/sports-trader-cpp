// include/stcpp/ml/mid_return_label.hpp — 短时套利标签管道: 双时钟多窗 mid 收益标签 (Phase 1)
//
// Owner: 老雷 (GM) — 短时套利引擎 (主计划 §3 标签管道)
// last_review: 2026-06-01
//
// 职责: 把 FeatureRecorder/fv JSONL 的样本 (X, as_of_ts, mid) 造成【未来 mid 移动】监督标签:
//   - 墙钟 (wall clock): y_wall_Δ = mid(t+Δ) − mid(t), Δ ∈ {2,3,5,10,12,15,30,60}s — as-of prevailing。
//   - 事件钟 (event clock): y_event_N = mid(t 之后第 N 个事件) − mid(t), N ∈ {1,2,5,10,20,50}。
//   老板「触发事件驱动不规律, 标签该按事件不按秒」→ 双时钟并采, 训练侧对比哪个预测力强。
//
// PIT 铁律 (主计划 §6 / 红线):
//   - 标签是 offline, 绝不作特征 / 不回喂实时决策 (label_pipeline 同纪律)。
//   - **未来覆盖检查**: 若 horizon 超出该 condition tape 最后一个观测 (t+Δ > tape.back().ts), 我们【没有
//     未来数据】→ 标 NaN+stale, 绝不外推 (这是回测金光实盘亏穿的命门; 禁造伪未来)。
//   - 样本 mid 与未来 mid 同源同 condition; as-of prevailing = 最后一个 ts≤target 的真实观测, 不插值。
//
// 纯函数核 (ComputeMidReturnLabel, 无 IO, 可单测); 文件层是薄便利。C++ 落盘, 训练侧离线读 (§12.4)。
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "stcpp/ml/label_pipeline.hpp"  // ExtractConditionId (复用)

namespace stcpp::ml {

// 墙钟 horizon 网格 (秒 → ns); 短期密集长期稀疏 (老板 2026-06-01)。
inline constexpr std::array<std::int64_t, 8> kWallHorizonsNs = {
    2'000'000'000LL,  3'000'000'000LL,  5'000'000'000LL,  10'000'000'000LL,
    12'000'000'000LL, 15'000'000'000LL, 30'000'000'000LL, 60'000'000'000LL};
inline constexpr std::array<int, 8> kWallHorizonsSec = {2, 3, 5, 10, 12, 15, 30, 60};
// 事件钟 horizon 网格 (第 N 个事件)。
inline constexpr std::array<int, 6> kEventHorizons = {1, 2, 5, 10, 20, 50};

// tape 中一个 mid 观测点 (per-condition, 按 ts 升序)。
struct MidPoint {
    std::int64_t ts_ns{0};
    double mid{0.0};
};

// 一个样本的双时钟多窗收益标签。NaN = 无未来数据 (horizon 超出 tape / 事件不足) → 训练侧 drop。
struct MidReturnLabel {
    std::array<double, 8> y_wall{};   // mid(t+Δ) − mid(t)
    std::array<double, 6> y_event{};  // mid(第N事件) − mid(t)
    bool valid{false};                // 至少最短墙钟 horizon (2s) 有未来覆盖
    MidReturnLabel() {
        y_wall.fill(std::numeric_limits<double>::quiet_NaN());
        y_event.fill(std::numeric_limits<double>::quiet_NaN());
    }
};

// ExtractNumField — 从 JSONL 行抽 "key":<num> (支持负/小数/科学计数; null/缺失 → nullopt)。
[[nodiscard]] inline std::optional<double> ExtractNumField(std::string_view line,
                                                           std::string_view key) noexcept {
    // key 形如 "as_of_ts_ns" / "f8"; 构造 "\"key\":"
    std::string needle;
    needle.reserve(key.size() + 3);
    needle += '"';
    needle += key;
    needle += "\":";
    const auto k = line.find(needle);
    if (k == std::string_view::npos) return std::nullopt;
    std::size_t pos = k + needle.size();
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos < line.size() && line[pos] == '"') ++pos;  // 容忍引号包裹
    if (pos >= line.size()) return std::nullopt;
    if (line[pos] == 'n') return std::nullopt;  // null → 缺失
    const char* start = line.data() + pos;
    char* end = nullptr;
    const double v = std::strtod(start, &end);
    if (end == start) return std::nullopt;
    return v;
}

// ---------------------------------------------------------------------------
// ComputeMidReturnLabel — 纯函数核。tape 按 ts 升序; i = 样本在 tape 中的索引。
//   墙钟: target = tape[i].ts + Δ; mid_at(target) = 最后一个 ts≤target 的观测 (prevailing, 不插值);
//         若 target > tape.back().ts (无未来覆盖) → NaN (禁外推)。
//   事件钟: mid(tape[i+N]); 若 i+N ≥ size (事件不足) → NaN。
// ---------------------------------------------------------------------------
[[nodiscard]] inline MidReturnLabel ComputeMidReturnLabel(const std::vector<MidPoint>& tape,
                                                          std::size_t i) noexcept {
    MidReturnLabel lab;
    if (tape.empty() || i >= tape.size()) return lab;
    const std::int64_t t = tape[i].ts_ns;
    const double mid_t = tape[i].mid;
    const std::int64_t last_ts = tape.back().ts_ns;

    for (std::size_t h = 0; h < kWallHorizonsNs.size(); ++h) {
        const std::int64_t target = t + kWallHorizonsNs[h];
        if (target > last_ts) continue;  // 无未来覆盖 → 保持 NaN (禁外推, PIT)
        // prevailing: 最后一个 ts ≤ target (binary search)
        // upper_bound 找第一个 > target, 前一个即 ≤ target。
        auto it = std::upper_bound(tape.begin(), tape.end(), target,
                                   [](std::int64_t v, const MidPoint& p) { return v < p.ts_ns; });
        if (it == tape.begin()) continue;  // target 在首点前 (不应发生, i 已在 tape 内)
        const double mid_target = (it - 1)->mid;
        lab.y_wall[h] = mid_target - mid_t;
        if (h == 0) lab.valid = true;  // 最短 horizon 有覆盖 → 样本可用
    }
    for (std::size_t e = 0; e < kEventHorizons.size(); ++e) {
        const std::size_t j = i + static_cast<std::size_t>(kEventHorizons[e]);
        if (j >= tape.size()) continue;  // 事件不足 → NaN
        lab.y_event[e] = tape[j].mid - mid_t;
    }
    return lab;
}

// BuildMidTapes — 从 JSONL (fv.jsonl 等) 建 per-condition mid tape (按 ts 升序, 去重同 ts)。
//   ts_key 默认 "as_of_ts_ns"; mid_key 默认 "f8" (= MlFeature::b_mid 在 fv.jsonl 的列名)。
using MidTapeMap = std::unordered_map<std::string, std::vector<MidPoint>>;
[[nodiscard]] inline MidTapeMap BuildMidTapes(const std::string& jsonl_path,
                                              std::string_view ts_key = "as_of_ts_ns",
                                              std::string_view mid_key = "f8") {
    MidTapeMap tapes;
    std::ifstream in(jsonl_path);
    if (!in.is_open()) return tapes;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto cid = ExtractConditionId(line);
        if (!cid) continue;
        const auto ts = ExtractNumField(line, ts_key);
        const auto mid = ExtractNumField(line, mid_key);
        if (!ts || !mid) continue;
        tapes[*cid].push_back(MidPoint{static_cast<std::int64_t>(*ts), *mid});
    }
    for (auto& [cid, v] : tapes) {
        std::sort(v.begin(), v.end(), [](const MidPoint& a, const MidPoint& b) { return a.ts_ns < b.ts_ns; });
        v.erase(std::unique(v.begin(), v.end(),
                            [](const MidPoint& a, const MidPoint& b) { return a.ts_ns == b.ts_ns; }),
                v.end());
    }
    return tapes;
}

// AppendMidLabels — 把双时钟标签追加进 X 行 (插在末尾 '}' 前)。X 原样 (X-leak safe: y 列只追加, 不进 X)。
[[nodiscard]] inline std::string AppendMidLabels(std::string_view feature_line, const MidReturnLabel& lab) {
    const auto rb = feature_line.rfind('}');
    if (rb == std::string_view::npos) return std::string(feature_line);
    std::string out(feature_line.substr(0, rb));
    auto put = [&out](const std::string& k, double v) {
        out += ",\"";
        out += k;
        out += "\":";
        if (v != v) {  // NaN → null (合法 JSON)
            out += "null";
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", v);
            out += buf;
        }
    };
    for (std::size_t h = 0; h < kWallHorizonsSec.size(); ++h)
        put("y_wall_" + std::to_string(kWallHorizonsSec[h]) + "s", lab.y_wall[h]);
    for (std::size_t e = 0; e < kEventHorizons.size(); ++e)
        put("y_event_" + std::to_string(kEventHorizons[e]), lab.y_event[e]);
    out += ",\"label_valid\":";
    out += (lab.valid ? "1" : "0");
    out += "}";
    return out;
}

struct MidJoinStats {
    std::size_t total{0};
    std::size_t labeled{0};   // valid (最短 horizon 有覆盖)
    std::size_t unlabeled{0};  // 无未来覆盖 (tape 尾部样本)
    std::size_t skipped{0};
};

// JoinMidLabelsFile — 两遍流式: ①建 tape; ②逐行定位 index → 算标签 → 追加 → 写训练 JSONL。
//   drop_unlabeled=true: 仅输出 valid 样本 (最短 horizon 有未来覆盖)。
[[nodiscard]] inline MidJoinStats JoinMidLabelsFile(const std::string& feature_jsonl_path,
                                                    const std::string& out_training_path,
                                                    bool drop_unlabeled = true,
                                                    std::string_view ts_key = "as_of_ts_ns",
                                                    std::string_view mid_key = "f8") {
    MidJoinStats st;
    const MidTapeMap tapes = BuildMidTapes(feature_jsonl_path, ts_key, mid_key);
    std::ifstream in(feature_jsonl_path);
    std::ofstream out(out_training_path, std::ios::trunc);
    if (!in.is_open() || !out.is_open()) return st;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        ++st.total;
        const auto cid = ExtractConditionId(line);
        const auto ts = cid ? ExtractNumField(line, ts_key) : std::nullopt;
        if (!cid || !ts) {
            ++st.skipped;
            continue;
        }
        const auto tit = tapes.find(*cid);
        if (tit == tapes.end()) {
            ++st.skipped;
            continue;
        }
        const auto& tape = tit->second;
        // 定位该样本在 tape 中的 index (ts 唯一去重后, lower_bound 精确命中)。
        const std::int64_t tns = static_cast<std::int64_t>(*ts);
        auto pit = std::lower_bound(tape.begin(), tape.end(), tns,
                                    [](const MidPoint& p, std::int64_t v) { return p.ts_ns < v; });
        if (pit == tape.end() || pit->ts_ns != tns) {
            ++st.skipped;
            continue;
        }
        const std::size_t i = static_cast<std::size_t>(pit - tape.begin());
        const MidReturnLabel lab = ComputeMidReturnLabel(tape, i);
        if (!lab.valid && drop_unlabeled) {
            ++st.unlabeled;
            continue;
        }
        out << AppendMidLabels(line, lab) << '\n';
        if (lab.valid) {
            ++st.labeled;
        } else {
            ++st.unlabeled;
        }
    }
    return st;
}

}  // namespace stcpp::ml
