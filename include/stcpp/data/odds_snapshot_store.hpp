#pragma once
// stcpp/data/odds_snapshot_store.hpp — 跨庄家赔率快照 store (RCU-lite, 镜像 ScoreSnapshotStore)
//
// Owner: 老雷 (GM) | last_review: 2026-06-02
//
// 目的 (bm_slots 管线):
//   OddsFeedThread (per-sport, getodds 周期轮询) Publish(map) → paper_loop tick 读 GetSnapshot()
//   按 match_id (getodds/pregame 空间) 查跨庄家赔率, 经 bm_slots_fill 定向填 game_row.bm_slots。
//
// 读写模型 (同 ScoreSnapshotStore): shared_ptr<const OddsMap> + mutex (持锁仅 shared_ptr 拷贝 ~5ns,
//   find 在锁外)。R-12 合规 (无 IO, 锁 << 100us)。Publish 非热路径 (getodds 轮询 ~分钟级)。
//   key = getodds <match id> (Goalserve pregame 空间)。

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "stcpp/data/odds_feed_parser.hpp"  // MatchResultOdds

namespace stcpp::data {

// 快照 map: getodds match_id → MatchResultOdds。一旦 publish 不可变, 消费侧无锁并发读。
using OddsMap = std::unordered_map<std::string, goalserve::MatchResultOdds>;

class OddsSnapshotStore {
public:
    OddsSnapshotStore() = default;
    OddsSnapshotStore(const OddsSnapshotStore&) = delete;
    OddsSnapshotStore& operator=(const OddsSnapshotStore&) = delete;
    OddsSnapshotStore(OddsSnapshotStore&&) = delete;
    OddsSnapshotStore& operator=(OddsSnapshotStore&&) = delete;
    ~OddsSnapshotStore() = default;

    // Publish — 采集线程调用 (非热路径)。整体替换 map (每 sport 一个 store 实例, 或合并后 Publish)。
    void Publish(std::shared_ptr<const OddsMap> next) noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        front_ = std::move(next);
    }

    // GetSnapshot — 取当前快照 (shared_ptr 拷贝, 锁外 find)。可能为 nullptr (未 publish)。
    [[nodiscard]] std::shared_ptr<const OddsMap> GetSnapshot() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return front_;
    }

    // Get — 按 match_id 查 (拷贝出值; 未命中 → nullopt)。锁外 find。
    [[nodiscard]] std::optional<goalserve::MatchResultOdds> Get(const std::string& match_id) const {
        std::shared_ptr<const OddsMap> snap;
        {
            std::lock_guard<std::mutex> lk(mu_);
            snap = front_;
        }
        if (!snap)
            return std::nullopt;
        const auto it = snap->find(match_id);
        if (it == snap->end())
            return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return front_ ? front_->size() : 0U;
    }

private:
    mutable std::mutex mu_;
    std::shared_ptr<const OddsMap> front_;
};

}  // namespace stcpp::data
