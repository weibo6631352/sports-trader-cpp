// include/stcpp/ml/label_pipeline.hpp — Phase 2 标签管道 (condition → resolved_outcome join)
//
// Owner: 老雷 (GM) — Phase 2 联合评审 (2026-05-31, 小邓 P0: 标签管道先行, 所有建模前提)
// last_review: 2026-05-31
//
// 职责: 把 FeatureRecorder 落的特征行 (X) 与结算结果 (y) 按 condition_id 对齐成监督训练集 (X,y)。
//   y = 该 condition 结算 outcome (YES 赢=1 / NO 赢=0)，来自 SettlementRecord.settlement_value。
//   标签是 condition-level: 一场一个 y, join 到该 condition 结算前的所有 X 行 (标准监督标注)。
//
// 红线纪律:
//   - CLV/前视 (小蒋): y 是 **offline 标签**, 绝不作特征 / 不回喂实时决策 / 不进 QuoteFeatures。
//     本管道只产独立训练文件, 与 live 路径物理隔离 (R-11 同精神)。
//   - 监督集只含已结算 (closed) + 明确赢家 (settlement_value∈{0,1}) 的 condition; 未结算行可排除或标 invalid。
//   - source-agnostic: 按 condition_id join, 不关心 X 行有哪些列 (FeatureRecorder 落什么就标什么)。
//
// 纯函数核 (无 IO, 可单测); 文件流式 join 是薄便利层。C++ 落盘, 训练侧 (Python LightGBM) 离线读 (§12.4)。
//
// ⚠ X 完整性 (后续): FeatureRecorder 当前落 QuoteFeatures (含列 18-74), 缺 0-17 原始 game/book 列
//   (score_diff/period/b_mid 等)。完整 75 列训练 X 应记 extract_full 输出 (PublishQuoteSnapshot 已算 fv)。
//   本管道 source-agnostic, 两种 X 都能 join; 完整性是 recorder 增强项, 不阻塞标签 join。
#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "stcpp/data/settlement_record.hpp"
#include "stcpp/data/settlement_store.hpp"  // SettlementMap

namespace stcpp::ml {

// ---------------------------------------------------------------------------
// OutcomeLabel — 一个 condition 的监督标签。
// ---------------------------------------------------------------------------
struct OutcomeLabel {
    double y{0.0};                     // 1 = YES 赢 / 0 = NO 赢 (二分类标签)
    bool valid{false};                 // 已结算 + 明确赢家 (可入监督集)
    std::int8_t settlement_value{-1};  // 原始 (-1未知/0=NO赢/1=YES赢)
};

// DeriveOutcomeLabel — SettlementRecord → 标签。closed 且 value∈{0,1} → valid; 否则 invalid。
[[nodiscard]] inline OutcomeLabel DeriveOutcomeLabel(const data::SettlementRecord& r) noexcept {
    OutcomeLabel l;
    l.settlement_value = r.settlement_value;
    if (r.closed && (r.settlement_value == 0 || r.settlement_value == 1)) {
        l.y = static_cast<double>(r.settlement_value);  // 1=YES赢 / 0=NO赢 (YES-canonical)
        l.valid = true;
    }
    return l;
}

using LabelStore = std::unordered_map<std::string, OutcomeLabel>;

// BuildLabelStore — 从 SettlementMap 建 condition_id → 标签 (只收已结算的, 监督集)。
[[nodiscard]] inline LabelStore BuildLabelStore(const data::SettlementMap& m) noexcept {
    LabelStore s;
    s.reserve(m.size());
    for (const auto& [cid, rec] : m) {
        const OutcomeLabel l = DeriveOutcomeLabel(rec);
        if (l.valid) s.emplace(cid, l);  // 只存已结算 (未结算无 y, 不入监督集)
    }
    return s;
}

// ExtractConditionId — 从 FeatureRecorder JSONL 行抽 "condition_id":"<hex>" (首字段)。
[[nodiscard]] inline std::optional<std::string> ExtractConditionId(std::string_view line) noexcept {
    constexpr std::string_view kKey = "\"condition_id\":\"";
    const auto k = line.find(kKey);
    if (k == std::string_view::npos) return std::nullopt;
    const auto vstart = k + kKey.size();
    const auto vend = line.find('"', vstart);
    if (vend == std::string_view::npos) return std::nullopt;
    return std::string(line.substr(vstart, vend - vstart));
}

// AppendLabel — 把 label 追加进 X 行 (插在末尾 '}' 前): X + ,"label":y,"label_valid":b。
//   X 原样保留 (source-agnostic); 训练侧读 "label" 列作 y。
[[nodiscard]] inline std::string AppendLabel(std::string_view feature_line, const OutcomeLabel& l) {
    const auto rb = feature_line.rfind('}');
    if (rb == std::string_view::npos) return std::string(feature_line);  // 非 JSON 行原样返回
    std::string out(feature_line.substr(0, rb));
    out += ",\"label\":";
    out += (l.y == 1.0 ? "1" : "0");
    out += ",\"label_valid\":";
    out += (l.valid ? "1" : "0");
    out += "}";
    return out;
}

// JoinLine — 单行 join: 抽 cid → 查 store → 追加 label。
//   drop_unlabeled=true: 未结算 (store 无此 cid) → nullopt (排除出监督集)。
//   drop_unlabeled=false: 未结算 → 标 label_valid=0 保留 (供 PIT 审计 / 半监督)。
[[nodiscard]] inline std::optional<std::string> JoinLine(std::string_view feature_line,
                                                         const LabelStore& store,
                                                         bool drop_unlabeled) {
    const auto cid = ExtractConditionId(feature_line);
    if (!cid) return std::nullopt;  // 无 condition_id 的行无法 join
    const auto it = store.find(*cid);
    if (it == store.end()) {
        if (drop_unlabeled) return std::nullopt;
        return AppendLabel(feature_line, OutcomeLabel{});  // label_valid=0
    }
    return AppendLabel(feature_line, it->second);
}

// 流式 join 统计 (供文件层报告)。
struct JoinStats {
    std::size_t total{0};      // 读入特征行数
    std::size_t labeled{0};    // 成功 join 到 valid 标签
    std::size_t unlabeled{0};  // 未结算 (drop 或标 invalid)
    std::size_t skipped{0};    // 无 condition_id / 非 JSON
};

// JoinFile — 流式: 读 FeatureRecorder JSONL → 按 store join → 写训练 JSONL。
//   drop_unlabeled=true → 只输出已结算行 (监督集)。返回统计。§12.4 C++ 落盘, 训练侧离线读。
//   红线: 输出独立训练文件, 不碰 live 路径 / 不回喂决策 (CLV 前视)。
[[nodiscard]] inline JoinStats JoinFile(const std::string& feature_jsonl_path, const LabelStore& store,
                                        const std::string& out_training_path, bool drop_unlabeled) {
    JoinStats st;
    std::ifstream in(feature_jsonl_path);
    std::ofstream out(out_training_path, std::ios::trunc);
    if (!in.is_open() || !out.is_open()) return st;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        ++st.total;
        const auto cid = ExtractConditionId(line);
        if (!cid) {
            ++st.skipped;
            continue;
        }
        const auto it = store.find(*cid);
        const bool has_label = (it != store.end());
        if (!has_label && drop_unlabeled) {
            ++st.unlabeled;
            continue;
        }
        const OutcomeLabel lab = has_label ? it->second : OutcomeLabel{};
        out << AppendLabel(line, lab) << '\n';
        if (has_label) {
            ++st.labeled;
        } else {
            ++st.unlabeled;
        }
    }
    return st;
}

}  // namespace stcpp::ml
