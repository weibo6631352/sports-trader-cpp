// src/stcpp/polymarket/clob_wss/orderbook_snapshot_hub.cpp
//
// v0.1 — per-token double-buffer OrderBookFeatures 快照实现
//
// Owner: 小冯 (#34)
// last_review: 2026-05-29
//
// 红线:
//   R-12: Publish() hot path — O(1) unordered_map lookup + atomic swap, 无 malloc
//   R-20: 4-ts 完全透传, 不本地 now() 替代 data_source_ts

#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"

#include <cstring>
#include <new>

namespace stcpp::polymarket::clob_wss {

// ===========================================================================
// Constructor
// ===========================================================================

OrderBookSnapshotHub::OrderBookSnapshotHub(std::size_t max_tokens)
    : slots_(std::make_unique<TokenSlot[]>(max_tokens)),
      max_tokens_(max_tokens),
      slot_count_(0),
      publish_count_(0) {
    index_map_.reserve(max_tokens);
}

// ===========================================================================
// GetOrAllocSlot — vCPU0 only (no concurrent writes)
// ===========================================================================

std::size_t OrderBookSnapshotHub::GetOrAllocSlot(std::string_view token_id) noexcept {
    // Fast path: already registered
    {
        auto it = index_map_.find(std::string(token_id));
        if (it != index_map_.end()) {
            return it->second;
        }
    }

    // Cold path: first time we see this token_id → allocate a new slot
    if (slot_count_ >= max_tokens_) {
        return kInvalidIdx;
    }

    const std::size_t idx = slot_count_++;
    // Initialize slot to default (valid=false, zero ts)
    slots_[idx].buf[0] = OrderBookFeatures{};
    slots_[idx].buf[1] = OrderBookFeatures{};
    slots_[idx].front.store(0, std::memory_order_relaxed);

    // Register in index map (cold path: may allocate string)
    try {
        index_map_.emplace(std::string(token_id), idx);
    } catch (...) {
        // string allocation failed (OOM) — roll back slot_count
        --slot_count_;
        return kInvalidIdx;
    }

    return idx;
}

// ===========================================================================
// FindSlot — read-only lookup (const, for observer threads)
// ===========================================================================

std::size_t OrderBookSnapshotHub::FindSlot(std::string_view token_id) const noexcept {
    auto it = index_map_.find(std::string(token_id));
    if (it == index_map_.end()) {
        return kInvalidIdx;
    }
    return it->second;
}

// ===========================================================================
// Publish — vCPU0 hot path
// ===========================================================================

void OrderBookSnapshotHub::Publish(std::string_view token_id, const OrderBookFeatures& features) noexcept {
    const std::size_t idx = GetOrAllocSlot(token_id);
    if (idx == kInvalidIdx) {
        return;  // max_tokens exceeded; drop silently (R-12: no throw)
    }

    TokenSlot& slot = slots_[idx];

    // Determine back-buffer: opposite of current front
    const std::uint8_t current_front = slot.front.load(std::memory_order_relaxed);
    const std::uint8_t back = static_cast<std::uint8_t>(1u - current_front);

    // Write into back-buffer (vCPU0 exclusive — no concurrent writer)
    slot.buf[back] = features;

    // Atomic publish: store back → new front (release fence)
    // After this store, any reader doing load(acquire) will see the new snapshot.
    slot.front.store(back, std::memory_order_release);

    ++publish_count_;
}

// ===========================================================================
// Read — observer threads (lock-free, acquire)
// ===========================================================================

std::optional<OrderBookFeatures> OrderBookSnapshotHub::Read(std::string_view token_id) const noexcept {
    const std::size_t idx = FindSlot(token_id);
    if (idx == kInvalidIdx) {
        return std::nullopt;
    }

    const TokenSlot& slot = slots_[idx];
    // Acquire: ensures we see the full write done before the store(release) in Publish()
    const std::uint8_t front = slot.front.load(std::memory_order_acquire);
    return slot.buf[front];  // value copy (trivially copyable)
}

// ===========================================================================
// ReadRaw — zero-copy pointer (caller beware: pointer valid until next Publish flip)
// ===========================================================================

const OrderBookFeatures* OrderBookSnapshotHub::ReadRaw(std::string_view token_id) const noexcept {
    const std::size_t idx = FindSlot(token_id);
    if (idx == kInvalidIdx) {
        return nullptr;
    }

    const TokenSlot& slot = slots_[idx];
    const std::uint8_t front = slot.front.load(std::memory_order_acquire);
    return &slot.buf[front];
}

// ===========================================================================
// ResetToken — vCPU0 (reconnect / resubscribe)
// ===========================================================================

void OrderBookSnapshotHub::ResetToken(std::string_view token_id) noexcept {
    const std::size_t idx = FindSlot(token_id);
    if (idx == kInvalidIdx) {
        return;
    }

    TokenSlot& slot = slots_[idx];
    // Reset both buffers to default (valid=false)
    slot.buf[0] = OrderBookFeatures{};
    slot.buf[1] = OrderBookFeatures{};
    // Reset front to 0 (writer picks back = 1 on next Publish)
    slot.front.store(0, std::memory_order_release);
}

// ===========================================================================
// ResetAll — vCPU0 (full reconnect)
// ===========================================================================

void OrderBookSnapshotHub::ResetAll() noexcept {
    for (std::size_t i = 0; i < slot_count_; ++i) {
        TokenSlot& slot = slots_[i];
        slot.buf[0] = OrderBookFeatures{};
        slot.buf[1] = OrderBookFeatures{};
        slot.front.store(0, std::memory_order_release);
    }
}

// ===========================================================================
// token_count
// ===========================================================================

std::size_t OrderBookSnapshotHub::token_count() const noexcept {
    return slot_count_;
}

}  // namespace stcpp::polymarket::clob_wss
