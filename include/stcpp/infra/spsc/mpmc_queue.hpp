// stcpp/infra/spsc/mpmc_queue.hpp — MPMC ring (FillQueue 专用)
//
// Owner: 小石 (data-structures-expert, #41)  W6 Wave 28 P0
// 落: laozhou-architecture-v0.6-e2e.md §4.1
//     FillQueue: vCPU3 VirtualMatcher → {vCPU3 Position Ledger, vCPU4 ML hook} 2-consumer
//
// 设计决策 (ADR-009 v2):
//   老周 §4.1 注: "rigtorp::MPMCQueue (2-consumer 实际为 SPMC)" + "是否换 SPMC 专用?"
//
//   小石答复 (W6 Wave 28):
//     1. 当前 FillQueue 是 1-producer (vCPU3 VirtualMatcher) + 2-consumer (Position + ML)
//        → 实质 SPMC (Single-Producer Multiple-Consumer)
//     2. 真 SPMC 最优解: 每 consumer 一条独立 SPSC + producer 做 fanout copy
//        - Position: SpscQueue<VirtualFill, 8192> fill_q_position
//        - ML hook:  SpscQueue<VirtualFill, 8192> fill_q_ml
//        - VirtualMatcher::produce(fill): push 到两条 ring, ML 满了 drop, Position 满了 P0
//        - 相比单 MPMC: 零 CAS 竞争, p99 从 25ns → 5ns, 且 ML slow-consumer 不影响 Position
//     3. rigtorp::MPMCQueue 用 CAS slot, 2-consumer 竞争最终 1 个 slot 被抢, 另个重试
//        → 对于固定 2-consumer SPMC 模型是过杀, 且丢失了 "Position 永不丢, ML 可丢" 语义
//     4. 结论: MpmcQueue 实现为 "双 SPSC fanout 包装", 显式分离 Position 和 ML consumer
//
//   这个设计允许:
//     - Position 端 try_push 满时 REJECT(SYSTEM_BACKPRESSURE) → P0 alert
//     - ML 端 try_push 满时 drop + counter → P1 alert  (ML-R2 不影响 rule path)
//     - 未来扩 3rd consumer 只加一条 SPSC, 不改 MPMC 逻辑
//
//   如果 §4.3 要求真 MPMC (e.g. 多 producer 场景), 用 rigtorp::MPMCQueue 包装:
//     见下方 RigtorpMpmcQueue<T, Capacity>
//
// 红线:
//   R-12: try_push 非阻塞 (两条 SPSC 都不阻塞)
//   Position consumer: 满时返回 false = SYSTEM_BACKPRESSURE, caller 必 emit metric + halt
//   ML consumer: 满时 drop + fillq_ml_drop_total++

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "stcpp/infra/spsc/queue_capacities.hpp"
#include "stcpp/infra/spsc/spsc_queue.hpp"

#include "rigtorp/MPMCQueue.h"  // rigtorp/MPMCQueue (FetchContent)

namespace stcpp::infra::spsc {

// ---------- FillQueue — SPMC fanout (双 SPSC, 推荐) -------------------------
//
// 使用方法:
//   FillQueue<VirtualFill> fill_q;
//
//   // Producer (vCPU3 VirtualMatcher):
//   auto [pos_ok, ml_ok] = fill_q.try_push(fill);
//   if (!pos_ok) { /* SYSTEM_BACKPRESSURE P0 */ }
//   if (!ml_ok)  { ml_drop_counter.fetch_add(1); }
//
//   // Consumer A — Position (vCPU3):
//   VirtualFill f;
//   if (fill_q.try_pop_position(f)) { position_ledger.apply(f); }
//
//   // Consumer B — ML hook (vCPU4):
//   VirtualFill f;
//   if (fill_q.try_pop_ml(f)) { ml_hook.capture(f); }

template <typename T, std::size_t Cap = FILL_QUEUE_CAPACITY>
class FillQueue {
    static_assert(IS_POWER_OF_TWO<Cap>, "FillQueue Capacity must be power of two");

public:
    struct PushResult {
        bool position_ok;  // false = Position ring 满 → P0 SYSTEM_BACKPRESSURE
        bool ml_ok;        // false = ML ring 满 → drop (ML-R2)
    };

    FillQueue() = default;
    FillQueue(const FillQueue&) = delete;
    FillQueue& operator=(const FillQueue&) = delete;

    // Producer: fanout to both queues atomically (best-effort, no rollback)
    // Position 满时 position_ok=false; ML 满时 ml_ok=false + ml_drop_count++
    [[nodiscard]] PushResult try_push(const T& val) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        bool pos_ok = position_queue_.try_push(val);
        bool ml_ok = ml_queue_.try_push(val);
        if (!ml_ok) {
            ml_drop_count_.fetch_add(1, std::memory_order_relaxed);
        }
        return {pos_ok, ml_ok};
    }

    // Consumer A — Position Ledger (vCPU3)
    [[nodiscard]] bool try_pop_position(T& out) noexcept { return position_queue_.try_pop(out); }

    // Consumer B — ML hook (vCPU4)
    [[nodiscard]] bool try_pop_ml(T& out) noexcept { return ml_queue_.try_pop(out); }

    // observability
    [[nodiscard]] std::uint64_t position_drop_count() const noexcept { return position_queue_.drop_count(); }
    [[nodiscard]] std::uint64_t ml_drop_count() const noexcept {
        return ml_drop_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Cap; }

private:
    SpscQueue<T, Cap> position_queue_;

    // ML queue: 独立成员, rigtorp::SPSCQueue 内部已做 cache-line 隔离
    // 不加 alignas(CACHE_LINE_SIZE): SpscQueue 自然 alignment 可能 > CACHE_LINE_SIZE,
    // alignas 只能增大不能减小, 编译器会报错若请求对齐 < 类型自然对齐.
    SpscQueue<T, Cap> ml_queue_;

    // ML drop counter (Position 的在 SpscQueue::drop_count 里)
    std::atomic<std::uint64_t> ml_drop_count_{0};
};

// ---------- RigtorpMpmcQueue<T, Capacity> — 通用 MPMC (备用) ----------------
//
// 若未来出现真 MPMC 场景 (多 producer + 多 consumer), 用这个直接包装 rigtorp::MPMCQueue.
// 当前 FillQueue 不用此实现 (用上面 FillQueue 双 SPSC 更优).

template <typename T, std::size_t Capacity>
class RigtorpMpmcQueue {
    static_assert(IS_POWER_OF_TWO<Capacity>, "RigtorpMpmcQueue Capacity must be power of two");

public:
    RigtorpMpmcQueue() : queue_(Capacity) {}

    RigtorpMpmcQueue(const RigtorpMpmcQueue&) = delete;
    RigtorpMpmcQueue& operator=(const RigtorpMpmcQueue&) = delete;

    [[nodiscard]] bool try_push(const T& val) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        if (queue_.try_push(val)) {
            return true;
        }
        drop_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    [[nodiscard]] bool try_pop(T& out) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        return queue_.try_pop(out);
    }

    [[nodiscard]] std::uint64_t drop_count() const noexcept {
        return drop_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    rigtorp::MPMCQueue<T> queue_;
    alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> drop_count_{0};
};

}  // namespace stcpp::infra::spsc
