// include/stcpp/data/settlement_recorder.hpp — 结算落盘 (Phase 2 缺口E: 离线 label 的 y 来源)
//
// Owner: 老雷 (GM) — Phase 2 标签管道 y 持久化
// last_review: 2026-05-31
//
// SettlementStore 只在内存 (SettlementPoller 填)。本 recorder 独立线程读 SettlementStore 快照,
//   把已结算 condition 落 settlement.jsonl (回测等价数据 / 离线对账 / 观测的 y 来源)。
//   去重: 每 condition 首次 closed 落一行 (结算是终态, 不变)。IO 离决策线程 (R-12)。
//   输出行: {"condition_id":"..","closed":1,"settlement_value":N,"end_date_ts_ns":N}
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_set>

#include "stcpp/data/settlement_store.hpp"

namespace stcpp::data {

class SettlementRecorder {
public:
    struct Config {
        std::string output_path = "data/ml_capture/settlements.jsonl";
        int poll_interval_sec = 30;  // 结算稀疏, 慢轮询足够
    };

    SettlementRecorder(const SettlementStore& store, Config cfg) : store_(store), cfg_(std::move(cfg)) {}
    ~SettlementRecorder() { Stop(); }
    SettlementRecorder(const SettlementRecorder&) = delete;
    SettlementRecorder& operator=(const SettlementRecorder&) = delete;

    void Start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this] { Run(); });
    }
    void Stop() noexcept {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
    }
    [[nodiscard]] std::uint64_t records_written() const noexcept {
        return written_.load(std::memory_order_relaxed);
    }

private:
    void Run() {
        std::error_code ec;
        const std::filesystem::path p(cfg_.output_path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream out(cfg_.output_path, std::ios::app);
        if (!out.is_open()) {
            std::fprintf(stderr, "[settle_recorder] WARN: 无法打开 %s, 结算落盘禁用\n",
                         cfg_.output_path.c_str());
            running_.store(false, std::memory_order_relaxed);
            return;
        }
        std::fprintf(stderr, "[settle_recorder] 结算落盘启动 -> %s (label y 来源)\n",
                     cfg_.output_path.c_str());
        while (running_.load(std::memory_order_relaxed)) {
            const auto snap = store_.GetSnapshot();
            if (snap) {
                for (const auto& [cid, rec] : *snap) {
                    if (!rec.closed) continue;                  // 只落已结算 (终态)
                    if (written_cids_.count(cid)) continue;     // 去重 (结算不变, 落一次)
                    written_cids_.insert(cid);
                    // parse_ok (2026-06-14): settlement_value!=-1 = winner 解析成功。-1 = 解析失败 (非平局,
                    //   PM 二元市场无平局) → 离线分析必 drop, 否则污染正负样本比。
                    out << "{\"condition_id\":\"" << cid << "\",\"closed\":1,\"settlement_value\":"
                        << static_cast<int>(rec.settlement_value)
                        << ",\"parse_ok\":" << (rec.settlement_value != -1 ? 1 : 0)
                        << ",\"end_date_ts_ns\":" << rec.end_date_ts_ns << "}\n";
                    written_.fetch_add(1, std::memory_order_relaxed);
                }
                out.flush();
            }
            const int slices = cfg_.poll_interval_sec * 10;
            for (int i = 0; i < slices && running_.load(std::memory_order_relaxed); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        out.flush();
    }

    const SettlementStore& store_;
    Config cfg_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::unordered_set<std::string> written_cids_;
    std::atomic<std::uint64_t> written_{0};
};

}  // namespace stcpp::data
