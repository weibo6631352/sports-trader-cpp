// tests/bench/bench_slippage_model.cpp — 小肖 W3 Wave 18 perf 回归
// 关联: docs/RESEARCH/xiaoxiao-slippage-model-lib-v1.md §3
//
// 验收门槛 (p99):
//   OneLevel             median ≤ 50ns  p99 ≤ 200ns
//   MultiLevel           median ≤ 80ns  p99 ≤ 200ns
//   Reject_InvalidIntent median ≤ 20ns  p99 ≤ 50ns

#include <cstdint>

#include <benchmark/benchmark.h>

#include "stcpp/numerical/slippage_model.hpp"

namespace {

using stcpp::numerical::SlippageInput;
using stcpp::numerical::SlippageModel;

constexpr std::int64_t WALL_NOW = 1'700'000'000'000'000'000LL;
constexpr std::int64_t BOOK_TS_RECENT = WALL_NOW - 200'000'000LL;  // 200ms ago

void BM_Slippage_OneLevel(benchmark::State& state) {
    SlippageInput const in{2000.0, 0.50, 2500.0, BOOK_TS_RECENT, WALL_NOW, 0.01};
    for (auto _ : state) {
        auto out = SlippageModel::compute(in);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_Slippage_OneLevel);

void BM_Slippage_MultiLevel(benchmark::State& state) {
    // ρ = 1.2 multi-level success path (fill_rate ≈ 0.58 > floor 0.50)
    // pf = 0.55 + 0.01·(0.5 + 0.2·1.0) = 0.557
    SlippageInput const in{6000.0, 0.55, 5000.0, BOOK_TS_RECENT, WALL_NOW, 0.01};
    for (auto _ : state) {
        auto out = SlippageModel::compute(in);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_Slippage_MultiLevel);

void BM_Slippage_Reject_InvalidIntent(benchmark::State& state) {
    // book_snapshot_ts == 0 → fast-fail path
    SlippageInput const in{2000.0, 0.50, 2500.0, 0, WALL_NOW, 0.01};
    for (auto _ : state) {
        auto out = SlippageModel::compute(in);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_Slippage_Reject_InvalidIntent);

}  // namespace

BENCHMARK_MAIN();
