// include/stcpp/backtest/replay_decision_input_provider.hpp — 回测多输入回放 (红线#3 闭合 P2)
//
// Owner: 小蒋 (quant-backtest, C 量化研究部 IC #20) + 老雷 (GM 起骨架)
// last_review: 2026-06-01
//
// 背景 (docs/RESEARCH/laolei-backtest-equivalence-spec-v1.md):
//   红线#3「回测=实盘」当前 ReplayDriver 只回放 book (6 决策输入里的 1 个) → 回测全程
//   has_real_fair=false → 零成交、永不结算。本 provider 是闭合方案的 ReplayImpl:
//   读 ScoreFrameRecorder 落的 scores.jsonl (→ ScoreMap 有序帧) + SettlementRecorder 落的
//   settlements.jsonl (→ ResolutionMap), 按 frame_ts_ns 时间轴, 每 tick 经 BuildAt(ts) 产
//   PaperLoop::DecisionInputSnapshot, 由回测驱动器 SetReplayInputs() 注入 → 解锁 in-play 分支/下单/结算。
//   book(#1) 由 ReplayDriver 在同一时间轴回放; catalog(#6)/event_map(映射桥) 当前由 harness 注入
//   (P3 捕获后改为加载)。
//
// 设计要点:
//   - 纯回放 (R-11: 不写 ledger/pnl), 离线 (非热路径, 不碰 R-12)。
//   - R-20: 透传帧自带 4ts (event/data_source/ingestion/as_of), 不用 now() 替代上游 ts。
//   - JSON 手解析 (项目约定零外部依赖, 同 inplay_score_parser; 无 simdjson/nlohmann)。
//   - BuildAt(ts): score 取 frame_ts_ns <= ts 的最近帧 (PIT: 不看未来帧)。
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "stcpp/data/score_snapshot_store.hpp"  // data::ScoreMap / debug_api::EventScore
#include "stcpp/paper/paper_loop.hpp"            // PaperLoop::DecisionInputSnapshot / ResolutionMap / PaperCatalog / ConditionEventMap

namespace stcpp::backtest {

class ReplayDecisionInputProvider {
public:
    using DecisionInputSnapshot = stcpp::paper::PaperLoop::DecisionInputSnapshot;
    using ResolutionMap = stcpp::paper::PaperLoop::ResolutionMap;

    // ---- 内存帧 API (P2 核心; 单测 + harness 直接喂, 不经 jsonl) ----
    // 加帧: frame_ts_ns 应单调递增喂入 (内部 Finalize 时再排序, 容错乱序)。
    void AddScoreFrame(std::int64_t frame_ts_ns, stcpp::data::ScoreMap score) {
        frames_.push_back({frame_ts_ns, std::make_shared<const stcpp::data::ScoreMap>(std::move(score))});
        sorted_ = false;
    }
    void SetResolution(ResolutionMap res) {
        resolution_ = std::make_shared<const ResolutionMap>(std::move(res));
    }
    void SetCatalog(std::shared_ptr<const stcpp::paper::PaperCatalog> cat) {
        catalog_ = std::move(cat);
    }
    void SetEventMap(std::shared_ptr<const stcpp::paper::ConditionEventMap> em) {
        event_map_ = std::move(em);
    }

    // ---- jsonl 加载 (ScoreFrameRecorder / SettlementRecorder 落盘格式) ----
    // scores.jsonl: 每行 {"frame_ts_ns":N,"n":K,"scores":[{EventScore 全字段+4ts+inplay}, ...]}
    [[nodiscard]] bool LoadScoreFramesJsonl(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;
        std::string line;
        std::size_t before = frames_.size();
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            ParseScoreFrameLine(line);
        }
        sorted_ = false;
        return frames_.size() >= before;  // 至少没倒退 (空文件 → true 但 0 帧)
    }
    // settlements.jsonl: 每行 {"condition_id":"..","closed":1,"settlement_value":N,"end_date_ts_ns":N}
    //   → ResolutionEntry{status = (value>=0?2:1), winner = value}。终态聚合 (后出现覆盖先前)。
    [[nodiscard]] bool LoadResolutionJsonl(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;
        ResolutionMap res;
        if (resolution_) res = *resolution_;  // 累加到已有
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::string cid = ExtractStr(line, "\"condition_id\":");
            if (cid.empty()) continue;
            const std::int64_t val = ExtractInt(line, "\"settlement_value\":", -1);
            stcpp::paper::ResolutionEntry e;
            e.winner = static_cast<std::int8_t>(val);
            e.status = (val >= 0) ? std::uint8_t{2} : std::uint8_t{1};
            res[cid] = e;
        }
        resolution_ = std::make_shared<const ResolutionMap>(std::move(res));
        return true;
    }

    // ---- 时间轴查询 ----
    // 某 tick 时刻的决策输入快照: score 取 frame_ts <= ts 的最近帧 (PIT); resolution/catalog/event_map 固定。
    [[nodiscard]] DecisionInputSnapshot BuildAt(std::int64_t ts_ns) const {
        EnsureSorted();
        DecisionInputSnapshot s;
        s.event_map = event_map_;
        s.catalog = catalog_;
        s.resolution = resolution_;
        s.live_stats = nullptr;  // P3 补 live_stats 帧
        s.score = ScoreAt(ts_ns);
        return s;
    }

    [[nodiscard]] std::vector<std::int64_t> frame_timestamps() const {
        EnsureSorted();
        std::vector<std::int64_t> ts;
        ts.reserve(frames_.size());
        for (const auto& f : frames_) ts.push_back(f.frame_ts_ns);
        return ts;
    }
    [[nodiscard]] std::size_t frame_count() const noexcept { return frames_.size(); }
    [[nodiscard]] std::shared_ptr<const ResolutionMap> resolution() const noexcept { return resolution_; }

private:
    struct Frame {
        std::int64_t frame_ts_ns;
        std::shared_ptr<const stcpp::data::ScoreMap> score;
    };

    void EnsureSorted() const {
        if (sorted_) return;
        std::sort(frames_.begin(), frames_.end(),
                  [](const Frame& a, const Frame& b) { return a.frame_ts_ns < b.frame_ts_ns; });
        sorted_ = true;
    }

    [[nodiscard]] std::shared_ptr<const stcpp::data::ScoreMap> ScoreAt(std::int64_t ts_ns) const {
        if (frames_.empty()) return nullptr;
        // 最近 frame_ts <= ts (PIT)。全部 > ts → 无可用帧 (回测尚未到首帧) → nullptr。
        std::shared_ptr<const stcpp::data::ScoreMap> best;
        for (const auto& f : frames_) {
            if (f.frame_ts_ns <= ts_ns) best = f.score;
            else break;  // 已排序, 后续都 > ts
        }
        return best;
    }

    // ---- 手写 JSON 抽取 (项目约定; 容错: 找不到键 → 默认值) ----
    // 在 hay 中找 needle (键含冒号), 返回其后第一个数值 (整数) 或 def。
    [[nodiscard]] static std::int64_t ExtractInt(std::string_view hay, std::string_view needle,
                                                 std::int64_t def = 0) {
        const std::size_t k = hay.find(needle);
        if (k == std::string_view::npos) return def;
        std::size_t i = k + needle.size();
        while (i < hay.size() && (hay[i] == ' ' || hay[i] == '\t')) ++i;
        if (i >= hay.size()) return def;
        if (hay.compare(i, 4, "null") == 0) return def;
        bool neg = false;
        if (hay[i] == '-') { neg = true; ++i; }
        std::int64_t v = 0;
        bool any = false;
        while (i < hay.size() && hay[i] >= '0' && hay[i] <= '9') {
            v = v * 10 + (hay[i] - '0');
            any = true;
            ++i;
        }
        if (!any) return def;
        return neg ? -v : v;
    }
    // 浮点抽取 (含小数/负号/科学计数; null → def)。
    [[nodiscard]] static double ExtractDouble(std::string_view hay, std::string_view needle,
                                              double def = -1.0) {
        const std::size_t k = hay.find(needle);
        if (k == std::string_view::npos) return def;
        std::size_t i = k + needle.size();
        while (i < hay.size() && (hay[i] == ' ' || hay[i] == '\t')) ++i;
        if (i >= hay.size()) return def;
        if (hay.compare(i, 4, "null") == 0) return def;
        std::size_t j = i;
        while (j < hay.size() &&
               (hay[j] == '-' || hay[j] == '+' || hay[j] == '.' || hay[j] == 'e' || hay[j] == 'E' ||
                (hay[j] >= '0' && hay[j] <= '9'))) {
            ++j;
        }
        if (j == i) return def;
        return std::strtod(std::string(hay.substr(i, j - i)).c_str(), nullptr);
    }
    // 字符串抽取: needle 后应为 "..."; 处理 \" \\ \n \r \t \uXXXX 转义。找不到 → "".
    [[nodiscard]] static std::string ExtractStr(std::string_view hay, std::string_view needle) {
        const std::size_t k = hay.find(needle);
        if (k == std::string_view::npos) return {};
        std::size_t i = k + needle.size();
        while (i < hay.size() && (hay[i] == ' ' || hay[i] == '\t')) ++i;
        if (i >= hay.size() || hay[i] != '"') return {};
        ++i;  // 跳开引号
        std::string out;
        while (i < hay.size()) {
            const char c = hay[i];
            if (c == '"') break;
            if (c == '\\' && i + 1 < hay.size()) {
                const char e = hay[i + 1];
                switch (e) {
                    case '"': out.push_back('"'); i += 2; continue;
                    case '\\': out.push_back('\\'); i += 2; continue;
                    case 'n': out.push_back('\n'); i += 2; continue;
                    case 'r': out.push_back('\r'); i += 2; continue;
                    case 't': out.push_back('\t'); i += 2; continue;
                    case 'u':
                        if (i + 5 < hay.size()) {
                            // 仅还原 ASCII (\u00XX); 非 ASCII 保留占位 '?' (回测决策不依赖队名字形)。
                            const auto hexv = [](char h) -> int {
                                if (h >= '0' && h <= '9') return h - '0';
                                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                                return 0;
                            };
                            const int cp = (hexv(hay[i + 2]) << 12) | (hexv(hay[i + 3]) << 8) |
                                           (hexv(hay[i + 4]) << 4) | hexv(hay[i + 5]);
                            out.push_back(cp < 0x80 ? static_cast<char>(cp) : '?');
                            i += 6;
                            continue;
                        }
                        break;
                    default: break;
                }
            }
            out.push_back(c);
            ++i;
        }
        return out;
    }

    // 把 scores 数组拆成各对象子串 (深度 1 的 {...}, 尊重字符串/转义)。
    [[nodiscard]] static std::vector<std::string_view> SplitArrayObjects(std::string_view body) {
        std::vector<std::string_view> objs;
        int depth = 0;
        bool in_str = false;
        std::size_t obj_start = 0;
        for (std::size_t i = 0; i < body.size(); ++i) {
            const char c = body[i];
            if (in_str) {
                if (c == '\\') { ++i; continue; }  // 跳转义符
                if (c == '"') in_str = false;
                continue;
            }
            if (c == '"') { in_str = true; continue; }
            if (c == '{') {
                if (depth == 0) obj_start = i;
                ++depth;
            } else if (c == '}') {
                --depth;
                if (depth == 0) objs.push_back(body.substr(obj_start, i - obj_start + 1));
            }
        }
        return objs;
    }

    void ParseScoreFrameLine(const std::string& line) {
        const std::int64_t frame_ts = ExtractInt(line, "\"frame_ts_ns\":", 0);
        const std::size_t arr_k = line.find("\"scores\":[");
        if (arr_k == std::string::npos) return;
        std::string_view body(line);
        body = body.substr(arr_k + std::string_view("\"scores\":[").size());
        // 截到匹配的 ] (用 SplitArrayObjects 不需精确结尾, 它按 brace 深度收对象)
        stcpp::data::ScoreMap m;
        for (const std::string_view obj : SplitArrayObjects(body)) {
            stcpp::debug_api::EventScore es;
            const std::string key = ExtractStr(obj, "\"key\":");
            es.found = ExtractInt(obj, "\"found\":", 0) != 0;
            es.event_id = ExtractStr(obj, "\"event_id\":");
            es.sport = ExtractStr(obj, "\"sport\":");
            es.status = ExtractStr(obj, "\"status\":");
            es.period = ExtractStr(obj, "\"period\":");
            es.clock_sec = ExtractInt(obj, "\"clock_sec\":", 0);
            es.home = ExtractStr(obj, "\"home\":");
            es.away = ExtractStr(obj, "\"away\":");
            es.home_score = static_cast<int>(ExtractInt(obj, "\"home_score\":", 0));
            es.away_score = static_cast<int>(ExtractInt(obj, "\"away_score\":", 0));
            es.ts.event_ts_ns = ExtractInt(obj, "\"event_ts_ns\":", 0);
            es.ts.data_source_ts_ns = ExtractInt(obj, "\"data_source_ts_ns\":", 0);
            es.ts.ingestion_ts_ns = ExtractInt(obj, "\"ingestion_ts_ns\":", 0);
            es.ts.as_of_ts_ns = ExtractInt(obj, "\"as_of_ts_ns\":", 0);
            es.source = ExtractStr(obj, "\"source\":");
            es.league_id = ExtractStr(obj, "\"league_id\":");
            es.kickoff_ts_sec = ExtractInt(obj, "\"kickoff_ts_sec\":", 0);
            es.inplay_bet365_home_fair = ExtractDouble(obj, "\"ip_home\":", -1.0);
            es.inplay_bet365_away_fair = ExtractDouble(obj, "\"ip_away\":", -1.0);
            es.inplay_bet365_draw_fair = ExtractDouble(obj, "\"ip_draw\":", -1.0);
            if (!key.empty()) m[key] = std::move(es);
        }
        frames_.push_back({frame_ts, std::make_shared<const stcpp::data::ScoreMap>(std::move(m))});
    }

    mutable std::vector<Frame> frames_;
    mutable bool sorted_{true};
    std::shared_ptr<const ResolutionMap> resolution_;
    std::shared_ptr<const stcpp::paper::PaperCatalog> catalog_;
    std::shared_ptr<const stcpp::paper::ConditionEventMap> event_map_;
};

}  // namespace stcpp::backtest
