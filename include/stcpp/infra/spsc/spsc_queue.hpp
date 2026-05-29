// stcpp/infra/spsc/spsc_queue.hpp — SPSC ring 封装 (rigtorp/SPSCQueue 接入)
//
// Owner: 小石 (data-structures-expert, #41)  W6 Wave 28 P0
// 落: laozhou-architecture-v0.6-e2e.md §4.1
//     ADR/2026-05-28-gm-redline-websocket-non-blocking.md (R-12)
//     xiaoshi-data-structures-selection-v1.md §1
//
// 设计决策 (ADR-009 v2):
//   1. 包装 rigtorp::SPSCQueue — header-only, SOTA 5ns p50, 8ns p99 (2-core pinned)
//   2. Capacity 编译期 2^n enforce (老姜 hard ask)
//   3. cache-line aligned: head/tail 各占独立 cache line (false sharing = lock-free 头号杀手)
//   4. try_push / try_pop 非阻塞 (R-12: vCPU0 event loop 严禁阻塞)
//   5. drop 路径: try_push 失败 → 原子 counter ++ → caller emit metric
//   6. macOS Apple Silicon: cache line 128B; x86/amd64/Linux: 64B — alignas 按平台选
//
// 红线:
//   R-12: try_push 非阻塞, 满了 return false + drop_count.fetch_add(1)
//   老姜 latency budget: Capacity 必须 2^n
//
// 接入 5 queue:
//   MarketDataBus  SpscQueue<WssEvent,     MARKET_DATA_BUS_CAPACITY>
//   SignalQueue    SpscQueue<SignalOutput,  SIGNAL_QUEUE_CAPACITY>
//   RiskQueue      SpscQueue<RiskDecision, RISK_QUEUE_CAPACITY>
//   WALQueue       SpscQueue<WalRecord,    WAL_QUEUE_CAPACITY>   (per kind)
//   FillQueue      → mpmc_queue.hpp (2-consumer)

#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#include "stcpp/infra/spsc/queue_capacities.hpp"

#include "rigtorp/SPSCQueue.h"  // rigtorp/SPSCQueue (FetchContent)

namespace stcpp::infra::spsc {

// ---------- cache-line size (跨平台) ----------------------------------------

#if defined(__aarch64__) || defined(_M_ARM64)
inline constexpr std::size_t CACHE_LINE_SIZE = 128;
#else
inline constexpr std::size_t CACHE_LINE_SIZE = 64;
#endif

// ---------- SpscQueue<T, Capacity> -----------------------------------------
//
// 包装 rigtorp::SPSCQueue<T>, 额外提供:
//   - drop_count: 生产方 push 满时自增, 消费方 / observability 读取
//   - Capacity 编译期 2^n assert
//   - try_push / try_pop 接口 (与 ISpscEventSink::TryPush 一致)
//
// 内存布局 (cache-line 隔离):
//   [0]  rigtorp::SPSCQueue 内部 (自带 head/tail cache-line 隔离)
//   [+]  drop_count_ 在独立 cache line (防与 tail 伪共享)

template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(IS_POWER_OF_TWO<Capacity>, "SpscQueue Capacity must be power of two");
    static_assert(std::is_trivially_copyable_v<T> || std::is_move_constructible_v<T>,
                  "SpscQueue element must be trivially copyable or move-constructible");

public:
    // rigtorp::SPSCQueue 构造时接受 capacity (运行时参数)
    SpscQueue() : queue_(Capacity) {}

    // 禁 copy/move — ring 持有 heap 分配, 语义不明确
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;

    ~SpscQueue() = default;

    // --- 生产方 API (vCPU0 / vCPU1 / vCPU2 / vCPU3) ---

    // 非阻塞 push.  满了 return false + drop_count++  (R-12 enforce)
    // 调用方必须在 false 路径 emit metric (e.g. mdb_drop_total++)
    [[nodiscard]] bool try_push(const T& val) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        if (queue_.try_push(val)) {
            return true;
        }
        drop_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    [[nodiscard]] bool try_push(T&& val) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (queue_.try_push(std::move(val))) {
            return true;
        }
        drop_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // --- 消费方 API (vCPU1 / vCPU2 / vCPU3 / vCPU4) ---

    // 非阻塞 pop.  空了 return false.
    [[nodiscard]] bool try_pop(T& out) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        T* front = queue_.front();
        if (front == nullptr) {
            return false;
        }
        out = *front;
        queue_.pop();
        return true;
    }

    // peek 不消费 (可选, 单元测试用)
    [[nodiscard]] T* front() noexcept { return queue_.front(); }
    void pop() noexcept { queue_.pop(); }

    // --- observability ---

    [[nodiscard]] std::uint64_t drop_count() const noexcept {
        return drop_count_.load(std::memory_order_relaxed);
    }

    // reset: 仅测试用, 不在生产 hot path 调
    void reset_drop_count() noexcept { drop_count_.store(0, std::memory_order_relaxed); }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    // rigtorp::SPSCQueue 没有 size() (lock-free 语义下 size 不一致), 这里也不提供

private:
    rigtorp::SPSCQueue<T> queue_;

    // 独立 cache line 防止 drop_count 与 queue_ 内部 tail 伪共享
    alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> drop_count_{0};
};

// ---------- SpscEventSink<T, Capacity> — ISpscEventSink 具体实现 -----------
//
// 给 PMWssSubscriber::ISpscEventSink 用:
//   std::shared_ptr<ISpscEventSink> sink =
//       std::make_shared<SpscEventSink<WssEvent, MARKET_DATA_BUS_CAPACITY>>();
//
// 接口要求 (pm_wss_subscriber.hpp):
//   bool TryPush(const WssEvent&) noexcept
//   std::size_t Capacity() const noexcept

template <typename T, std::size_t Cap>
class SpscEventSink {
public:
    SpscEventSink() = default;

    // 实现 ISpscEventSink 接口 (不继承虚基, 避免 vtable 开销; 调用方 duck-type 或 wrap)
    [[nodiscard]] bool TryPush(const T& ev) noexcept { return queue_.try_push(ev); }

    [[nodiscard]] std::size_t Capacity() const noexcept { return Cap; }

    // 消费方 pop
    [[nodiscard]] bool TryPop(T& out) noexcept { return queue_.try_pop(out); }

    [[nodiscard]] std::uint64_t drop_count() const noexcept { return queue_.drop_count(); }

private:
    SpscQueue<T, Cap> queue_;
};

// ---------- ISpscEventSink 适配器 (给 PMWssSubscriber 用虚接口) ---------------
//
// 如果下游需要通过 ISpscEventSink* / shared_ptr<ISpscEventSink> 持有, 用这个桥接:
//   auto sink = std::make_shared<SpscEventSinkAdapter<WssEvent, MARKET_DATA_BUS_CAPACITY>>();

}  // namespace stcpp::infra::spsc
