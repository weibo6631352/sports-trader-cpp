// tests/perf/bench_virtual_matcher.cpp — M5 VirtualMatcher::Match() p99
//
// 落: docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md §2 M5
// 预算: p99 ≤ 30us (Slippage compute + clamp + Bernoulli draw + R-20 透传)
//
// 路径覆盖:
//   OneLevel_Fill        : ρ < 1, p_fill > 0.50, 概率走 fill
//   MultiLevel_Fill      : ρ > 1 多层 VWAP, p_fill 落 floor/cap 之间
//   Reject_NaN           : SlippageModel reject 透传, fast-fail
//   Reject_BookDepth     : depth 极小 → ExceedBookDepth, fast-fail
//   Bernoulli_OverrideHit  : SetUniformOverrideForTesting(0.0) → 永远 hit (deterministic)
//   Bernoulli_OverrideMiss : SetUniformOverrideForTesting(0.99) → 永远 miss
//
// 注: 不写 WAL (VirtualFill 仅返回 struct, caller 落 paper_audit. emit 部分由 M3 测).

#include <cmath>
#include <cstdint>

#include <benchmark/benchmark.h>

#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/wal/pit.hpp"

namespace {

using namespace stcpp;

execution::VirtualOrder make_order(double size, double price,
                                   double depth, double tick = 0.01) {
    execution::VirtualOrder o;
    o.intent_id          = 7;
    o.market_id          = "0xMatch";
    o.outcome            = "YES";
    o.size_usdc          = size;
    o.quote_price        = price;
    o.book_depth_l1_usdc = depth;
    o.tick_size          = tick;
    const std::int64_t t = infra::wal::pit::NowRealtimeNs();
    o.event_ts_ns        = t - 10'000'000;
    o.data_source_ts_ns  = t -  8'000'000;
    o.ingestion_ts_ns    = t -  4'000'000;
    o.as_of_ts_ns        = t;
    o.wall_now_ns        = t;
    return o;
}

// ---------- 1. OneLevel_Fill (ρ < 1, 单层吃饱) ----------
void BM_Matcher_OneLevel_Fill(benchmark::State& state) {
    execution::VirtualMatcher m{0xBE'EFCAFEULL};
    auto ord = make_order(/*size=*/100.0, /*price=*/0.55, /*depth=*/1'000.0);
    // Bernoulli draw 随机 — 测平均 latency 包括 hit + miss 两路径
    for (auto _ : state) {
        auto fill = m.Match(ord);
        benchmark::DoNotOptimize(fill);
    }
}
BENCHMARK(BM_Matcher_OneLevel_Fill);

// ---------- 2. MultiLevel_Fill (ρ > 1, 多层 VWAP) ----------
void BM_Matcher_MultiLevel_Fill(benchmark::State& state) {
    execution::VirtualMatcher m{0xC0FFEE'BEEFULL};
    // size=6000, depth=5000 → ρ=1.2 → fill_rate ≈ 0.58 (clamp 0.50/0.65 之间)
    auto ord = make_order(6'000.0, 0.55, 5'000.0);
    for (auto _ : state) {
        auto fill = m.Match(ord);
        benchmark::DoNotOptimize(fill);
    }
}
BENCHMARK(BM_Matcher_MultiLevel_Fill);

// ---------- 3. Reject_NaN (SlippageModel fast-fail) ----------
void BM_Matcher_Reject_Nan(benchmark::State& state) {
    execution::VirtualMatcher m{0x1};
    auto ord = make_order(100.0, 0.55, 1'000.0);
    ord.book_depth_l1_usdc = std::nan("");
    for (auto _ : state) {
        auto fill = m.Match(ord);
        benchmark::DoNotOptimize(fill);
    }
}
BENCHMARK(BM_Matcher_Reject_Nan);

// ---------- 4. Reject_BookDepth (ρ > RHO_MAX = 3) ----------
void BM_Matcher_Reject_BookDepth(benchmark::State& state) {
    execution::VirtualMatcher m{0x2};
    // size=10000, depth=100 → ρ=100 >> 3 → ExceedBookDepth
    auto ord = make_order(10'000.0, 0.55, 100.0);
    for (auto _ : state) {
        auto fill = m.Match(ord);
        benchmark::DoNotOptimize(fill);
    }
}
BENCHMARK(BM_Matcher_Reject_BookDepth);

// ---------- 5. Bernoulli_OverrideHit (deterministic uniform=0.0) ----------
void BM_Matcher_Bernoulli_Hit(benchmark::State& state) {
    execution::VirtualMatcher m{0x3};
    m.SetUniformOverrideForTesting(0.0);  // 永远 < p_fill → hit
    auto ord = make_order(100.0, 0.55, 1'000.0);
    for (auto _ : state) {
        auto fill = m.Match(ord);
        benchmark::DoNotOptimize(fill);
    }
}
BENCHMARK(BM_Matcher_Bernoulli_Hit);

// ---------- 6. Bernoulli_OverrideMiss (deterministic uniform=0.99) ----------
void BM_Matcher_Bernoulli_Miss(benchmark::State& state) {
    execution::VirtualMatcher m{0x4};
    m.SetUniformOverrideForTesting(0.99);  // 永远 > p_fill → miss
    auto ord = make_order(100.0, 0.55, 1'000.0);
    for (auto _ : state) {
        auto fill = m.Match(ord);
        benchmark::DoNotOptimize(fill);
    }
}
BENCHMARK(BM_Matcher_Bernoulli_Miss);

}  // namespace

BENCHMARK_MAIN();
