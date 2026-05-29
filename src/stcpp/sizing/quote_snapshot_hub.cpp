// src/stcpp/sizing/quote_snapshot_hub.cpp
//
// v0.1 — per-market double-buffer QuoteFeatures 快照实现
//
// Owner: 小石 (#41, data-structures-expert, G-LEDGER-OWNER)
// last_review: 2026-05-29
//
// 红线:
//   R-12: Publish() hot path — O(1) unordered_map lookup + atomic swap, 无 malloc
//   R-20: 4-ts 完全透传, 不本地 now() 替代

#include "stcpp/sizing/quote_snapshot_hub.hpp"

namespace stcpp::sizing {

// ===========================================================================
// Constructor
// ===========================================================================

QuoteSnapshotHub::QuoteSnapshotHub(std::size_t max_keys)
    : slots_(std::make_unique<KeySlot[]>(max_keys)), max_keys_(max_keys), slot_count_(0), publish_count_(0) {
    index_map_.reserve(max_keys);
}

// ===========================================================================
// GetOrAllocSlot — 写端 only (no concurrent writers)
// ===========================================================================

std::size_t QuoteSnapshotHub::GetOrAllocSlot(std::string_view market_key) noexcept {
    // Fast path: already registered
    {
        auto it = index_map_.find(std::string(market_key));
        if (it != index_map_.end()) {
            return it->second;
        }
    }

    // Cold path: first time we see this key → allocate a new slot
    if (slot_count_ >= max_keys_) {
        return kInvalidIdx;
    }

    const std::size_t idx = slot_count_++;
    slots_[idx].buf[0] = QuoteFeatures{};
    slots_[idx].buf[1] = QuoteFeatures{};
    slots_[idx].front.store(0, std::memory_order_relaxed);

    // Register in index map (cold path: may allocate string)
    try {
        index_map_.emplace(std::string(market_key), idx);
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

std::size_t QuoteSnapshotHub::FindSlot(std::string_view market_key) const noexcept {
    auto it = index_map_.find(std::string(market_key));
    if (it == index_map_.end()) {
        return kInvalidIdx;
    }
    return it->second;
}

// ===========================================================================
// Publish — 写端 hot path
// ===========================================================================

void QuoteSnapshotHub::Publish(std::string_view market_key, const QuoteFeatures& features) noexcept {
    const std::size_t idx = GetOrAllocSlot(market_key);
    if (idx == kInvalidIdx) {
        return;  // max_keys exceeded; drop silently (R-12: no throw)
    }

    KeySlot& slot = slots_[idx];

    // Determine back-buffer: opposite of current front
    const std::uint8_t current_front = slot.front.load(std::memory_order_relaxed);
    const std::uint8_t back = static_cast<std::uint8_t>(1u - current_front);

    // Write into back-buffer (写端 exclusive — no concurrent writer)
    slot.buf[back] = features;

    // Atomic publish: store back → new front (release fence)
    slot.front.store(back, std::memory_order_release);

    ++publish_count_;
}

// ===========================================================================
// Read — observer threads (lock-free, acquire)
// ===========================================================================

std::optional<QuoteFeatures> QuoteSnapshotHub::Read(std::string_view market_key) const noexcept {
    const std::size_t idx = FindSlot(market_key);
    if (idx == kInvalidIdx) {
        return std::nullopt;
    }

    const KeySlot& slot = slots_[idx];
    const std::uint8_t front = slot.front.load(std::memory_order_acquire);
    return slot.buf[front];  // value copy (trivially copyable)
}

// ===========================================================================
// ReadRaw — zero-copy pointer (caller beware: pointer valid until next Publish flip)
// ===========================================================================

const QuoteFeatures* QuoteSnapshotHub::ReadRaw(std::string_view market_key) const noexcept {
    const std::size_t idx = FindSlot(market_key);
    if (idx == kInvalidIdx) {
        return nullptr;
    }

    const KeySlot& slot = slots_[idx];
    const std::uint8_t front = slot.front.load(std::memory_order_acquire);
    return &slot.buf[front];
}

// ===========================================================================
// ResetKey — 写端
// ===========================================================================

void QuoteSnapshotHub::ResetKey(std::string_view market_key) noexcept {
    const std::size_t idx = FindSlot(market_key);
    if (idx == kInvalidIdx) {
        return;
    }

    KeySlot& slot = slots_[idx];
    slot.buf[0] = QuoteFeatures{};
    slot.buf[1] = QuoteFeatures{};
    slot.front.store(0, std::memory_order_release);
}

// ===========================================================================
// ResetAll — 写端 (系统重启)
// ===========================================================================

void QuoteSnapshotHub::ResetAll() noexcept {
    for (std::size_t i = 0; i < slot_count_; ++i) {
        KeySlot& slot = slots_[i];
        slot.buf[0] = QuoteFeatures{};
        slot.buf[1] = QuoteFeatures{};
        slot.front.store(0, std::memory_order_release);
    }
}

// ===========================================================================
// key_count
// ===========================================================================

std::size_t QuoteSnapshotHub::key_count() const noexcept {
    return slot_count_;
}

}  // namespace stcpp::sizing
