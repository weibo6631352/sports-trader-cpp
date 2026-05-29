// stcpp/net/outbound_queue.hpp — REST 出站背压队列 (OutboundSubmitQueue)
//
// Owner: 老陈 (A-NET-01, #03)  Last review: 2026-05-29
//
// 红线:
//   R-12: 非阻塞, try_push 失败立即返 false + drop_count++
//   容量 256 (2^8) — 对应设计文档 §3.1:
//     RiskQueue 4096 × 拒单率 8-20% → APPROVED ≤ 3277/tick
//     下单频率远低于行情, 256 足够 5s+ 缓冲
//   满时: drop + emit metric rest_submit_drop_total (P0 alert)
//   note: drop 在 vCPU3 Orchestrator, 不在 vCPU0 WSS loop
//
// 元素类型: OutboundSubmitItem — 含序列化后 JSON body (已复制进 buf)
// 生产方: vCPU3 Orchestrator (SubmitOrder 前)
// 消费方: HTTP/2 worker (AsyncPost 线程)
//
// 与 SpscQueue<T,N> 的关系:
//   OutboundSubmitQueue 使用 SpscQueue wrapper (小石 W6 Wave 28 rigtorp 封装)
//   额外提供语义化 metric 接口 (rest_submit_drop_total)

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "stcpp/infra/spsc/queue_capacities.hpp"  // IS_POWER_OF_TWO

namespace stcpp::net {

// OutboundSubmitQueue 容量 — 2^8 = 256 (老周 v0.6 §3.1 设计)
inline constexpr std::size_t kOutboundSubmitQueueCapacity = 256;
static_assert(stcpp::infra::spsc::IS_POWER_OF_TWO<kOutboundSubmitQueueCapacity>,
              "kOutboundSubmitQueueCapacity must be power of two (老姜 latency budget)");

// REST 出站 item: 已序列化的 JSON body + 路径
// 不含 OutboundBuffer 对象 (避免 4KB item 进 ring; 改为固定 size string copy)
// JSON body ≤ 1KB, 用 std::string 保存 (一次 heap alloc per item, 可接受 — 不在热路径)
struct OutboundSubmitItem {
    std::string path;        // e.g. "/clob/orders"
    std::string json_body;   // 已序列化 JSON (< 1KB)
    std::uint64_t seq{0};    // 单调序号 (caller 填, 用于 audit dedup)
};

// OutboundSubmitQueue — SPSC ring, 容量 256, 满时 drop
//
// 内部用 std::array 做 ring (不依赖 rigtorp, 避免额外 FetchContent 在本 target)
// 此 queue 不在 hot path (下单频率 < 行情), 简单 lock-free ring 即可
//
// 线程模型:
//   生产方: vCPU3 Orchestrator (单生产者)
//   消费方: HTTP/2 worker thread (单消费者)
//
// 注意: std::string 非 trivially_copyable, 用 move 语义避免拷贝

class OutboundSubmitQueue {
public:
    static constexpr std::size_t kCapacity = kOutboundSubmitQueueCapacity;

    OutboundSubmitQueue() noexcept = default;

    // 禁 copy/move
    OutboundSubmitQueue(const OutboundSubmitQueue&)            = delete;
    OutboundSubmitQueue& operator=(const OutboundSubmitQueue&) = delete;
    OutboundSubmitQueue(OutboundSubmitQueue&&)                 = delete;
    OutboundSubmitQueue& operator=(OutboundSubmitQueue&&)      = delete;

    // 生产方: try_push (vCPU3 Orchestrator)
    // 满时: drop_count++ + 返 false (caller emit rest_submit_drop_total P0)
    // 非阻塞 (R-12)
    //
    // ring 语义:
    //   head_ = 下一个写入位置 (生产方独写)
    //   tail_ = 下一个读取位置 (消费方独写)
    //   满条件: (head - tail) == kCapacity (即 head 追上 tail + capacity)
    //   空条件: head == tail
    //   index = pos & kMask  (kCapacity 为 2^n, kMask = kCapacity - 1)
    [[nodiscard]] bool try_push(OutboundSubmitItem item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if ((head - tail) >= kCapacity) {
            // queue full — drop
            drop_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        buf_[head & kMask] = std::move(item);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // 消费方: try_pop (HTTP/2 worker)
    // 空时返 false
    [[nodiscard]] bool try_pop(OutboundSubmitItem& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail == head) {
            return false;  // empty
        }
        out = std::move(buf_[tail & kMask]);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // --- observability ---

    // drop_count: 满时 drop 次数 (caller export → rest_submit_drop_total P0 alert)
    [[nodiscard]] std::uint64_t drop_count() const noexcept {
        return drop_count_.load(std::memory_order_relaxed);
    }

    void reset_drop_count() noexcept {
        drop_count_.store(0, std::memory_order_relaxed);
    }

    // approximate size (非精确, SPSC 语义下仅参考)
    [[nodiscard]] std::size_t approx_size() const noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        return h - t;
    }

    static constexpr std::size_t capacity() noexcept { return kCapacity; }

private:
    static constexpr std::size_t kMask = kCapacity - 1;  // 2^n → bitmask
    // ring buffer: kCapacity slots (power-of-two, index via & kMask)
    std::array<OutboundSubmitItem, kCapacity> buf_{};

    alignas(64) std::atomic<std::size_t> head_{0};  // 生产方独写 (单调递增)
    alignas(64) std::atomic<std::size_t> tail_{0};  // 消费方独写 (单调递增)
    alignas(64) std::atomic<std::uint64_t> drop_count_{0};
};

}  // namespace stcpp::net
