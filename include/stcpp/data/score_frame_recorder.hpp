// include/stcpp/data/score_frame_recorder.hpp — 比分帧落盘 (回测等价性 P0 — 红线#3 闭合数据前提)
//
// Owner: 老雷 (GM) + 小余 (ETL co-own) — 回测等价性 spec P0
// last_review: 2026-06-01
//
// 背景 (docs/RESEARCH/laolei-backtest-equivalence-spec-v1.md):
//   红线#3「回测=实盘」当前只回放 book (6 决策输入里的 1 个)。比分(#2 Goalserve)是分叉杀伤力最大的
//   缺口 — 不回放 → has_real_fair=false → 回测全程零成交、永不进 in-play 分支。本 recorder 捕获
//   ScoreMap 原始帧 (EventScore 全字段 + 4ts + inplay bet365 sharp 赔率), 是小蒋 P2 ReplayImpl 喂帧
//   解锁回测下单/结算的数据前提。resolution(#5)已由 settlement_recorder.hpp 覆盖, 故此处只补比分。
//
// 设计 (IO 离决策线程, R-12):
//   独立线程周期读 ScoreSnapshotStore::GetSnapshot() → 每帧一行 JSONL (整个 ScoreMap 快照)。
//   去重: 帧内最大 as_of_ts_ns 未推进则跳过 (只在比分数据前进时落帧, 防膨胀)。
//   replay 侧按 frame_ts_ns 排序重建 ScoreMap, 经 TradingLoop::SetReplayInputs 注入。
//   输出行: {"frame_ts_ns":N,"n":K,"scores":[{EventScore 全字段+4ts+inplay}, ...]}
//
// R-20: 透传 EventScore 自带 4ts (event_ts/data_source_ts/ingestion_ts/as_of_ts), 不用 now() 替代上游 ts。
// R-12: 独立线程, 不进 WSS event loop; 只读 RCU GetSnapshot (无锁)。
#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "stcpp/data/score_snapshot_store.hpp"  // ScoreMap / ScoreSnapshotStore / debug_api::EventScore

namespace stcpp::data {

class ScoreFrameRecorder {
public:
    struct Config {
        std::string output_path = "data/ml_capture/quotes.jsonl.scores.jsonl";
        int poll_interval_sec = 2;  // 比分变化以秒计; 2s 足够捕获前进帧
    };

    ScoreFrameRecorder(const ScoreSnapshotStore& store, Config cfg)
        : store_(store), cfg_(std::move(cfg)) {}

    ~ScoreFrameRecorder() { Stop(); }
    ScoreFrameRecorder(const ScoreFrameRecorder&) = delete;
    ScoreFrameRecorder& operator=(const ScoreFrameRecorder&) = delete;

    void Start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this] { Run(); });
    }
    void Stop() noexcept {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
    }
    [[nodiscard]] std::uint64_t frames_written() const noexcept {
        return frames_written_.load(std::memory_order_relaxed);
    }

private:
    void Run() {
        std::error_code ec;
        const std::filesystem::path p(cfg_.output_path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream out(cfg_.output_path, std::ios::app);
        if (!out.is_open()) {
            std::fprintf(stderr, "[score_recorder] WARN: 无法打开 %s, 比分帧采集禁用\n",
                         cfg_.output_path.c_str());
            running_.store(false, std::memory_order_relaxed);
            return;
        }
        std::fprintf(stderr, "[score_recorder] 比分帧采集启动 -> %s (每 %ds, 前进帧去重; 回测 P0 数据)\n",
                     cfg_.output_path.c_str(), cfg_.poll_interval_sec);

        while (running_.load(std::memory_order_relaxed)) {
            if (const auto snap = store_.GetSnapshot()) {
                std::int64_t frame_max_as_of = 0;
                for (const auto& [_k, es] : *snap) {
                    if (es.ts.as_of_ts_ns > frame_max_as_of) frame_max_as_of = es.ts.as_of_ts_ns;
                }
                if (!snap->empty() && frame_max_as_of > last_frame_max_as_of_) {
                    WriteFrame(out, *snap, frame_max_as_of);
                    last_frame_max_as_of_ = frame_max_as_of;
                    frames_written_.fetch_add(1, std::memory_order_relaxed);
                    out.flush();
                }
            }
            const int slices = cfg_.poll_interval_sec * 10;
            for (int i = 0; i < slices && running_.load(std::memory_order_relaxed); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        out.flush();
    }

    // JSON 字符串安全转义 (队名/联赛名可能含 " 或 \ 或控制符 → 破坏整行 json.loads)。
    static void WriteJsonStr(std::ofstream& out, const std::string& s) {
        out << '"';
        for (const char c : s) {
            switch (c) {
                case '"':  out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        out << buf;
                    } else {
                        out << c;
                    }
            }
        }
        out << '"';
    }

    // 浮点 → JSON 安全 (NaN/Inf → null; inplay 赔率默认 -1.0 有限值正常输出)。
    static std::string JsonF(double v) {
        if (!std::isfinite(v)) return "null";
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%g", v);
        return buf;
    }

    static void WriteEntry(std::ofstream& out, const std::string& key,
                           const debug_api::EventScore& es) {
        out << "{\"key\":";
        WriteJsonStr(out, key);
        out << ",\"found\":" << (es.found ? 1 : 0) << ",\"event_id\":";
        WriteJsonStr(out, es.event_id);
        out << ",\"sport\":";
        WriteJsonStr(out, es.sport);
        out << ",\"status\":";
        WriteJsonStr(out, es.status);
        out << ",\"period\":";
        WriteJsonStr(out, es.period);
        out << ",\"clock_sec\":" << es.clock_sec << ",\"home\":";
        WriteJsonStr(out, es.home);
        out << ",\"away\":";
        WriteJsonStr(out, es.away);
        out << ",\"home_score\":" << es.home_score << ",\"away_score\":" << es.away_score
            << ",\"event_ts_ns\":" << es.ts.event_ts_ns
            << ",\"data_source_ts_ns\":" << es.ts.data_source_ts_ns
            << ",\"ingestion_ts_ns\":" << es.ts.ingestion_ts_ns
            << ",\"as_of_ts_ns\":" << es.ts.as_of_ts_ns << ",\"source\":";
        WriteJsonStr(out, es.source);
        out << ",\"league_id\":";
        WriteJsonStr(out, es.league_id);
        out << ",\"kickoff_ts_sec\":" << es.kickoff_ts_sec
            << ",\"ip_home\":" << JsonF(es.inplay_bet365_home_fair)
            << ",\"ip_away\":" << JsonF(es.inplay_bet365_away_fair)
            << ",\"ip_draw\":" << JsonF(es.inplay_bet365_draw_fair) << "}";
    }

    static void WriteFrame(std::ofstream& out, const ScoreMap& snap, std::int64_t frame_ts_ns) {
        out << "{\"frame_ts_ns\":" << frame_ts_ns << ",\"n\":" << snap.size() << ",\"scores\":[";
        bool first = true;
        for (const auto& [k, es] : snap) {
            if (!first) out << ',';
            first = false;
            WriteEntry(out, k, es);
        }
        out << "]}\n";
    }

    const ScoreSnapshotStore& store_;
    Config cfg_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::int64_t last_frame_max_as_of_{0};
    std::atomic<std::uint64_t> frames_written_{0};
};

}  // namespace stcpp::data
