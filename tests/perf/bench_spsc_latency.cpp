// tests/perf/bench_spsc_latency.cpp — SPSC queue push/pop latency bench v1.2
//
// 老姜 W6 Wave 28 — 小石 W6 SPSC framework 联调准备
// 落: docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md §2 M2 (enqueue budget 1us)
//     docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md §6 Ask 5 (小石 SPSC 约束)
//     2026-06-01-adr-015-vcpu-pin.md (W6 起 SPSC 接 vCPU 隔离后实测)
//
// 5 capacity: 64 / 256 / 1024 / 4096 / 16384 (全部 2^n, 小石 Ask5 约束)
//
// upper guard: p99 < 100 ns (老周 v0.6 §4 设计目标, SPSC enqueue < 1us)
//
// 注: 当前 infra/spsc/ 为空 (小石 W6 框架未落). 本文件用 std::atomic<int64_t> 模拟
//     最简 SPSC 语义测 overhead lower bound. 小石 W6 SPSC 就位后:
//     1) 替换 MockSPSC → stcpp::infra::spsc::SPSCQueue<T, N>
//     2) 保留相同 bench 函数签名 (名字不变, capacity 不变)
//     3) 老姜 re-run 出真实 baseline 与本 mock 比较
//
// 编译: 无需额外 lib link (纯 header + std::atomic)

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

#include <benchmark/benchmark.h>

#include "stcpp/infra/wal/pit.hpp"

namespace {

// ---- MockSPSC: 最简 lock-free ring (producer/consumer 各独占 head/tail) -----
// cache-line align (Ask5: false sharing 防护)
// capacity 必须 2^n (Ask5 bitmask 优化)
// 仅用于 bench overhead baseline; 小石 W6 替换

template <std::size_t N>
class MockSPSC {
    static_assert(N > 0 && (N & (N - 1)) == 0, "capacity must be 2^n");

public:
    struct alignas(64) Slot {
        std::int64_t value{0};
    };

    // 入队: 成功返 true; 队满返 false
    [[nodiscard]] bool push(std::int64_t v) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & (N - 1);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        slots_[head].value = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // 出队: 成功返 true, 写入 v; 队空返 false
    [[nodiscard]] bool pop(std::int64_t& v) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        v = slots_[tail].value;
        tail_.store((tail + 1) & (N - 1), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }

private:
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::array<Slot, N> slots_{};
};

// ---- 单线程 push/pop latency (同线程; 测纯 ring 开销 vs 跨线程 cache ping-pong) ----

template <std::size_t N>
void BM_SPSC_PushPop_SameThread(benchmark::State& state) {
    MockSPSC<N> q;
    std::int64_t v = 0;
    std::int64_t in = 1;

    for (auto _ : state) {
        const bool pushed = q.push(in++);
        benchmark::DoNotOptimize(pushed);
        const bool popped = q.pop(v);
        benchmark::DoNotOptimize(popped);
        benchmark::DoNotOptimize(v);
    }
}

BENCHMARK_TEMPLATE(BM_SPSC_PushPop_SameThread, 64)
    ->Name("BM_SPSC_PushPop_SameThread/cap64")
    ->MinTime(1.0)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_TEMPLATE(BM_SPSC_PushPop_SameThread, 256)
    ->Name("BM_SPSC_PushPop_SameThread/cap256")
    ->MinTime(1.0)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_TEMPLATE(BM_SPSC_PushPop_SameThread, 1024)
    ->Name("BM_SPSC_PushPop_SameThread/cap1024")
    ->MinTime(1.0)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_TEMPLATE(BM_SPSC_PushPop_SameThread, 4096)
    ->Name("BM_SPSC_PushPop_SameThread/cap4096")
    ->MinTime(1.0)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_TEMPLATE(BM_SPSC_PushPop_SameThread, 16384)
    ->Name("BM_SPSC_PushPop_SameThread/cap16384")
    ->MinTime(1.0)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

// ---- 跨线程 RTT (producer thread push → consumer thread pop, 测 cache ping-pong) ----
// 更接近真实 SPSC 使用场景 (WSS thread → decision thread)
// 注: 跨线程 bench 用 state.threads() 不适合此场景 (需 push/pop 分离 thread)
//     改为 benchmark::State 外起 producer thread, 用 atomic flag 同步

template <std::size_t N>
void BM_SPSC_CrossThread_RTT(benchmark::State& state) {
    MockSPSC<N> q;
    std::atomic<bool> stop{false};
    std::atomic<std::int64_t> producer_seq{0};

    // Producer thread: 持续 push
    std::thread producer([&] {
        std::int64_t v = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            while (!q.push(++v)) {
                // spin on full (rare in bench; capacity >> batch)
            }
            producer_seq.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::int64_t consumed = 0;
    std::int64_t v = 0;

    for (auto _ : state) {
        // consumer: pop one item (spin until available)
        while (!q.pop(v)) {
            // yield to let producer fill
        }
        benchmark::DoNotOptimize(v);
        ++consumed;
    }

    stop.store(true, std::memory_order_release);
    producer.join();

    state.counters["consumed"] =
        benchmark::Counter(static_cast<double>(consumed), benchmark::Counter::kAvgIterations);
}

BENCHMARK_TEMPLATE(BM_SPSC_CrossThread_RTT, 256)
    ->Name("BM_SPSC_CrossThread_RTT/cap256")
    ->MinTime(1.0)
    ->Repetitions(3)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_TEMPLATE(BM_SPSC_CrossThread_RTT, 1024)
    ->Name("BM_SPSC_CrossThread_RTT/cap1024")
    ->MinTime(1.0)
    ->Repetitions(3)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_TEMPLATE(BM_SPSC_CrossThread_RTT, 4096)
    ->Name("BM_SPSC_CrossThread_RTT/cap4096")
    ->MinTime(1.0)
    ->Repetitions(3)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

}  // namespace

BENCHMARK_MAIN();
