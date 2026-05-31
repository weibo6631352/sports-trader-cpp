// include/stcpp/data/settlement_store.hpp — 收盘/结算快照 store (shared_ptr swap)
//
// Owner: 老雷 (GM) — 成果方案 M2 (照抄 ScoreSnapshotStore 范式)
// last_review: 2026-05-31
//
// condition_id → SettlementRecord 的 in-mem 快照 store。poller 线程 Publish, daemon refresh 线程 Get。
// shared_ptr<const map> + mutex 短锁 swap (Apple libc++ atomic<shared_ptr> 不全, 同 ScoreSnapshotStore)。
// R-12: 无 IO, 锁持有 = shared_ptr 拷贝 (~5ns), hash lookup 锁外。
#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "stcpp/data/settlement_record.hpp"

namespace stcpp::data {

using SettlementMap = std::unordered_map<std::string, SettlementRecord>;

class SettlementStore {
public:
    SettlementStore() = default;
    SettlementStore(const SettlementStore&) = delete;
    SettlementStore& operator=(const SettlementStore&) = delete;
    SettlementStore(SettlementStore&&) = delete;
    SettlementStore& operator=(SettlementStore&&) = delete;
    ~SettlementStore() = default;

    // Publish — poller 线程调用 (全量替换快照; ~60s 周期)。
    void Publish(std::shared_ptr<const SettlementMap> next) noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        front_ = std::move(next);
    }

    // Get — 读侧 (daemon refresh 线程)。nullopt = cid 不存在。
    [[nodiscard]] std::optional<SettlementRecord> Get(const std::string& condition_id) const noexcept {
        std::shared_ptr<const SettlementMap> snap;
        {
            std::lock_guard<std::mutex> lk(mu_);
            snap = front_;
        }
        if (!snap) return std::nullopt;
        auto it = snap->find(condition_id);
        if (it == snap->end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::shared_ptr<const SettlementMap> GetSnapshot() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return front_;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return front_ ? front_->size() : 0;
    }

private:
    mutable std::mutex mu_;
    std::shared_ptr<const SettlementMap> front_;
};

}  // namespace stcpp::data
