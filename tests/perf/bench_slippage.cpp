// tests/perf/bench_slippage.cpp — M1 SlippageModel p99 (W4 Wave 21 老姜复测)
//
// 关联:
//   docs/RESEARCH/xiaoxiao-slippage-model-lib-v1.md §3 (小肖 W3 原 bench)
//   docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md §2 M1
//
// 预算 (p99):
//   OneLevel              ≤ 50 ns   (小肖 W3 实测 6.5 ns ✓, 我们 +30% headroom)
//   MultiLevel            ≤ 80 ns
//   Reject_InvalidIntent  ≤ 50 ns
//   Reject_Nan            ≤ 50 ns
//   Reject_BookTsStale    ≤ 50 ns
//
// 输出 JSON 由 --benchmark_out=*.json 写出, scripts/perf_compare.py 比对 baseline.

#include <cstdint>
#include <cmath>

#include <benchmark/benchmark.h>

#include "stcpp/numerical/slippage_model.hpp"

namespace {

using stcpp::numerical::SlippageInput;
using stcpp::numerical::SlippageModel;

constexpr std::int64_t kWallNow        = 1'700'000'000'000'000'000LL;
constexpr std::int64_t kBookFreshTs    = kWallNow - 200'000'000LL;   // 200 ms ago
constexpr std::int64_t kBookStaleTs    = kWallNow - 90'000'000'000LL; // 90 s ago (> 60s STALE_MAX_NS)

// ---------- OneLevel (ρ < 1, 单层吃饱) ----------
void BM_Slippage_OneLevel(benchmark::State& state) {
    SlippageInput const in{
        /*order_size_usdc*/ 2'000.0,
        /*quote_price*/      0.50,
        /*book_depth_l1*/   2'500.0,
        /*book_snapshot_ts*/ kBookFreshTs,
        /*wall_now*/         kWallNow,
        /*tick_size*/        0.01,
    };
    for (auto _ : state) {
        auto out = SlippageModel::compute(in);
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Slippage_OneLevel);

// ---------- MultiLevel (ρ > 1, 多层 VWAP) ----------
void BM_Slippage_MultiLevel(benchmark::State& state) {
    // ρ = order/L1 = 6000/5000 = 1.2 → 多层, fill_rate ≈ 0.58 (在 floor 0.50 之上)
    SlippageInput const in{6'000.0, 0.55, 5'000.0, kBookFreshTs, kWallNow, 0.01};
    for (auto _ : state) {
        auto out = SlippageModel::compute(in);
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Slippage_MultiLevel);

// ---------- Reject path: book_ts == 0 (fast-fail INVALID_INTENT) ----------
void BM_Slippage_Reject_BookTsZero(benchmark::State& state) {
    SlippageInput const in{2'000.0, 0.50, 2'500.0, 0, kWallNow, 0.01};
    for (auto _ : state) {
        auto out = SlippageModel::compute(in);
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Slippage_Reject_BookTsZero);

// ---------- Reject path: NaN ----------
void BM_Slippage_Reject_Nan(benchmark::State& state) {
    SlippageInput const in{
        std::nan(""), 0.50, 2'500.0, kBookFreshTs, kWallNow, 0.01,
    };
    for (auto _ : state) {
        auto out = SlippageModel::compute(in);
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Slippage_Reject_Nan);

// ---------- Reject path: book_ts 过陈 (> 60s STALE_MAX_NS) ----------
void BM_Slippage_Reject_BookTsStale(benchmark::State& state) {
    SlippageInput const in{2'000.0, 0.50, 2'500.0, kBookStaleTs, kWallNow, 0.01};
    for (auto _ : state) {
        auto out = SlippageModel::compute(in);
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Slippage_Reject_BookTsStale);

// ---------- Reject path: illegal tick (不在 {0.001, 0.01}) ----------
void BM_Slippage_Reject_IllegalTick(benchmark::State& state) {
    SlippageInput const in{2'000.0, 0.50, 2'500.0, kBookFreshTs, kWallNow, /*tick=*/0.005};
    for (auto _ : state) {
        auto out = SlippageModel::compute(in);
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Slippage_Reject_IllegalTick);

}  // namespace

BENCHMARK_MAIN();
