// stcpp/data/score_snapshot_store.cpp — ScoreSnapshotStore 实现
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
//
// 实现: std::shared_ptr<const ScoreMap> + std::mutex
//   读锁持有时间 = shared_ptr copy (~5ns), hash lookup 在锁外完成
//   R-12 合规: 无 IO, 锁持有远 < 100us
//
// 注: 原设计为 C++20 atomic<shared_ptr> (RCU-lite), 但 Apple libc++ 当前版本
//   对 atomic<shared_ptr> 要求 trivially_copyable, 不满足.
//   改用 mutex + shared_ptr swap, 语义等价, 读侧仍为锁外 O(1) hash lookup.
//   生产节点 (Linux glibc++ / libc++ 新版) 可改回 atomic<shared_ptr>.

#include "stcpp/data/score_snapshot_store.hpp"

namespace stcpp::data {

void ScoreSnapshotStore::Publish(std::shared_ptr<const ScoreMap> next) noexcept {
    // 构建新 map → swap → 锁外释放旧 map (避免在锁内 delete)
    std::shared_ptr<const ScoreMap> old_map;
    {
        std::lock_guard<std::mutex> lk(mu_);
        old_map = std::move(front_);
        front_ = std::move(next);
    }
    // old_map 在锁外析构 — 引用计数减 1 (若无其他持有者则释放)
}

std::optional<debug_api::EventScore> ScoreSnapshotStore::Get(const std::string& event_id) const noexcept {
    // 只在 shared_ptr copy 期间持锁 (~5ns)
    std::shared_ptr<const ScoreMap> snap;
    {
        std::lock_guard<std::mutex> lk(mu_);
        snap = front_;
    }
    if (!snap)
        return std::nullopt;

    // hash lookup 在锁外完成
    const auto it = snap->find(event_id);
    if (it == snap->end())
        return std::nullopt;

    return it->second;
}

std::shared_ptr<const ScoreMap> ScoreSnapshotStore::GetSnapshot() const noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    return front_;
}

std::size_t ScoreSnapshotStore::Size() const noexcept {
    std::shared_ptr<const ScoreMap> snap;
    {
        std::lock_guard<std::mutex> lk(mu_);
        snap = front_;
    }
    if (!snap)
        return 0;
    return snap->size();
}

}  // namespace stcpp::data
