// include/stcpp/data/commentaries_poller.hpp — commentaries Feed 轮询线程 (live_stats 采集)
//
// Owner: 老雷 (GM) — live_stats 采集 hop 补齐
// last_review: 2026-05-31
//
// 单 jthread 轮询 commentaries/{league}.xml → CommentariesParser → LiveStatsStore。
//   daemon 提供真 fetcher (popen curl), 测试注入 fake。范式照抄 SettlementPoller。
//   R-12: 独立线程, popen 阻塞 IO 在本线程, 不进 WSS event loop。30s 周期 (doc §6 刷新率)。
//   每轮全量重建快照 (live_stats 是当前值, 无需累积历史; 失败联赛保留上轮值在 acc_)。
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "stcpp/data/commentaries_parser.hpp"
#include "stcpp/data/live_stats_store.hpp"

namespace stcpp::data::livescore {

class CommentariesPoller {
public:
    // fetch(league_id) → commentaries/{league}.xml body (空 = 拉取失败, 本轮该联赛跳过)。
    using FetchFn = std::function<std::string(const std::string&)>;

    CommentariesPoller(LiveStatsStore& store, std::vector<std::string> league_ids, FetchFn fetch,
                       std::int64_t poll_interval_ms = 30'000) noexcept
        : store_(store),
          leagues_(std::move(league_ids)),
          fetch_(std::move(fetch)),
          poll_interval_ms_(poll_interval_ms) {}

    CommentariesPoller(const CommentariesPoller&) = delete;
    CommentariesPoller& operator=(const CommentariesPoller&) = delete;
    ~CommentariesPoller() { Stop(); }

    // SetLeagues — 动态更新轮询联赛集 (league_id 仅 feed 跑起来后才知; daemon refresh 线程从
    //   score store 收集后注入)。线程安全: pending 锁保护, poller 线程在 PollAllOnce 起点 swap。
    void SetLeagues(std::vector<std::string> leagues) noexcept {
        std::lock_guard<std::mutex> lk(leagues_mu_);
        leagues_pending_ = std::move(leagues);
        has_pending_.store(true, std::memory_order_release);
    }

    // PollAllOnce — fetch+parse 全部联赛 → 合并 join_key 快照 → publish。测试可直接调 (无线程)。
    void PollAllOnce() noexcept {
        if (has_pending_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lk(leagues_mu_);
            leagues_ = std::move(leagues_pending_);
            leagues_pending_.clear();
            has_pending_.store(false, std::memory_order_release);
        }
        for (const auto& lg : leagues_) {
            std::string xml = fetch_(lg);
            if (xml.empty()) continue;  // 失败联赛保留 acc_ 上轮值
            CommentariesParser::ParseInto(acc_, xml, lg);
        }
        store_.Publish(std::make_shared<LiveStatsMap>(acc_));
        poll_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void Start() {
        if (running_.exchange(true)) return;
        thread_ = std::jthread([this](std::stop_token st) { RunLoop(st); });
    }

    void Stop() noexcept {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        running_.store(false);
    }

    [[nodiscard]] std::uint64_t poll_count() const noexcept {
        return poll_count_.load(std::memory_order_relaxed);
    }

private:
    void RunLoop(std::stop_token st) {
        while (!st.stop_requested()) {
            PollAllOnce();
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(poll_interval_ms_);
            while (!st.stop_requested() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    LiveStatsStore& store_;
    std::vector<std::string> leagues_;  // poller 线程独占 (经 pending swap 更新)
    FetchFn fetch_;
    std::int64_t poll_interval_ms_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> poll_count_{0};
    std::atomic<bool> has_pending_{false};
    std::mutex leagues_mu_;
    std::vector<std::string> leagues_pending_;  // 锁保护 (daemon 写, poller swap)
    LiveStatsMap acc_;  // poller 线程独占 (携带失败联赛上轮值)
};

}  // namespace stcpp::data::livescore
