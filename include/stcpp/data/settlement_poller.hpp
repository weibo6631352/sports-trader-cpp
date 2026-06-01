// include/stcpp/data/settlement_poller.hpp — 收盘/结算 REST 轮询线程
//
// Owner: 老雷 (GM) — 成果方案 M2 (小余 build-ready 设计落地)
// last_review: 2026-05-31
//
// 单 jthread 轮询 clob /markets/{cid} → ParseMarketJson → SettlementStore。daemon 提供真 fetcher
//   (popen curl), 测试注入 fake fetcher。R-12: 独立线程, popen 阻塞 IO 在本线程, 不进 WSS event loop。
//   已结算 (closed) 的 cid 不再轮询 (acc_ 携带前值)。close_fair 不在此 (M3 CLVTracker 从 tick 抓 mid)。
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "stcpp/data/settlement_record.hpp"
#include "stcpp/data/settlement_store.hpp"

namespace stcpp::data {

class SettlementPoller {
public:
    // fetch(cid) → clob /markets/{cid} 的 JSON body (空 = 拉取失败, 本轮跳过该 cid)。
    using FetchFn = std::function<std::string(const std::string&)>;

    SettlementPoller(SettlementStore& store, std::vector<std::string> condition_ids, FetchFn fetch,
                     std::int64_t poll_interval_ms = 60'000) noexcept
        : store_(store),
          cids_(std::move(condition_ids)),
          fetch_(std::move(fetch)),
          poll_interval_ms_(poll_interval_ms) {}

    SettlementPoller(const SettlementPoller&) = delete;
    SettlementPoller& operator=(const SettlementPoller&) = delete;
    ~SettlementPoller() { Stop(); }

    // SetConditionIds — 动态更新轮询 condition 集 (2026-06-01: 修"启动时设死永不更新"bug)。
    //   RediscoverOnce 每周期把新市场集注入 → 新出现的比赛得到结算轮询 (否则永不结算 → PnL 链断);
    //   线程安全: pending 锁保护, poller 线程在 PollAllOnce 起点 swap (照抄 CommentariesPoller 范式)。
    void SetConditionIds(std::vector<std::string> cids) noexcept {
        std::lock_guard<std::mutex> lk(cids_mu_);
        cids_pending_ = std::move(cids);
        has_pending_cids_.store(true, std::memory_order_release);
    }

    // PollAllOnce — fetch+parse 全部未结算 cid → 合并 acc_ → publish 快照。测试可直接调 (无线程)。
    void PollAllOnce() noexcept {
        // 起点 swap 待更新的 cid 集 (poller 线程独占 cids_, 无并发读)。
        if (has_pending_cids_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lk(cids_mu_);
            cids_ = std::move(cids_pending_);
            cids_pending_.clear();
            has_pending_cids_.store(false, std::memory_order_release);
        }
        for (const auto& cid : cids_) {
            auto ait = acc_.find(cid);
            if (ait != acc_.end() && ait->second.closed) continue;  // 已结算不再轮询
            std::string json = fetch_(cid);
            if (json.empty()) continue;
            SettlementRecord r = ParseMarketJson(json, cid);
            if (r.valid) acc_[cid] = std::move(r);
        }
        store_.Publish(std::make_shared<SettlementMap>(acc_));
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
            // 可中断 sleep (粗粒度 100ms 检查 stop)。
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(poll_interval_ms_);
            while (!st.stop_requested() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    SettlementStore& store_;
    std::vector<std::string> cids_;
    std::mutex cids_mu_;                       // 保护 cids_pending_ (动态更新)
    std::vector<std::string> cids_pending_;
    std::atomic<bool> has_pending_cids_{false};
    FetchFn fetch_;
    std::int64_t poll_interval_ms_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> poll_count_{0};
    std::unordered_map<std::string, SettlementRecord> acc_;  // poller 线程独占 (携带已结算前值)
};

}  // namespace stcpp::data
