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

namespace {
// ScoreMapSig — 赔率源内容签名 (2026-06-14 老板「赔率源状态变动也该触发决策」): 比分 + bet365 inplay 赔率
//   + 状态 (core stopped/blocked/finished + status/period) 的顺序无关哈希。两次签名相同 = 内容没变 →
//   只更新 front_ 保新鲜, 不触发决策; 任一 event 的比分/赔率/状态变 → 签名变 → 触发。
[[nodiscard]] std::uint64_t ScoreMapSig(const ScoreMap& m) noexcept {
    const std::hash<double> hd{};
    const std::hash<std::string> hs{};
    std::uint64_t acc = 0;  // 顺序无关 (per-event 哈希求和; map 迭代序不影响)
    for (const auto& [k, es] : m) {
        std::uint64_t h = 1469598103934665603ULL;  // FNV-1a 64 offset
        auto mix = [&h](std::uint64_t v) { h = (h ^ v) * 1099511628211ULL; };
        mix(hs(k));
        mix(static_cast<std::uint64_t>(es.home_score));  // 分开 mix (避免 uint64_t*ULL 在 Linux 产 sign-conversion)
        mix(static_cast<std::uint64_t>(es.away_score));
        mix(hd(es.inplay_bet365_home_fair));
        mix(hd(es.inplay_bet365_away_fair));
        mix(hd(es.inplay_bet365_draw_fair));
        mix((es.core_stopped ? 1u : 0u) | (es.core_blocked ? 2u : 0u) | (es.core_finished ? 4u : 0u));
        mix(hs(es.status));
        mix(hs(es.period));
        acc += h;
    }
    return acc;
}
}  // namespace

void ScoreSnapshotStore::Publish(std::shared_ptr<const ScoreMap> next) noexcept {
    // 变动检测 (老板「触发=变动」): 先算新 map 签名 (锁外, 在本地 next 上), 与上次比。
    const std::uint64_t sig = next ? ScoreMapSig(*next) : 0;
    // 构建新 map → swap → 锁外释放旧 map (避免在锁内 delete)。front_ 一律更新 (保新鲜)。
    std::shared_ptr<const ScoreMap> old_map;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        old_map = std::move(front_);
        front_ = std::move(next);
        changed = (sig != last_sig_);
        last_sig_ = sig;
    }
    // old_map 在锁外析构 — 引用计数减 1 (若无其他持有者则释放)。
    // 内容变了才触发决策 (老板「赔率源状态变动也该触发」: 比分/赔率/停表/封盘/完赛 任一变即唤醒)。R-12: 回调极快, 锁外调。
    if (changed && on_change_) on_change_();
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
