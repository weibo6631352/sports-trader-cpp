// tests/perf/bench_goalserve_parse.cpp — M6 Goalserve client parse helpers p99
//
// 落: docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md §2 M6
// 预算:
//   BuildUrl           p99 ≤ 5us  (5 host × 8 sport × N endpoint, 纯 string concat)
//   ParseLastUpdateNs  p99 ≤ 2us  ("dd.MM.yyyy HH:mm" + ISO 8601 两种)
//   TsMsToNs           p99 ≤ 50ns (constexpr-foldable u64 乘法)
//   parse_batch (W5 真接 HTTP body parse 时) p99 ≤ 1ms

#include <cstdint>
#include <string>
#include <string_view>

#include <benchmark/benchmark.h>

#include "stcpp/data/goalserve_client.hpp"

namespace {

using namespace stcpp::data::goalserve;

GoalserveClient make_client() {
    GoalserveClient::Config cfg;
    cfg.key          = "perf_bench_key_dummy_32_chars_ok";
    cfg.prefer_https = true;
    cfg.gzip         = true;
    return GoalserveClient{cfg};
}

// ---------- 1. BuildUrl: 5 host × 8 sport × InplayOdds ----------
void BM_Goalserve_BuildUrl_InplayOdds(benchmark::State& state) {
    auto client = make_client();
    UrlSpec spec;
    spec.host     = GoalserveHost::Inplay;
    spec.endpoint = GoalserveEndpoint::InplayOdds;
    spec.json     = true;
    std::size_t i = 0;
    for (auto _ : state) {
        spec.sport = static_cast<GoalserveSport>(i % kNumSports);
        ++i;
        auto url = client.BuildUrl(spec);
        benchmark::DoNotOptimize(url);
    }
}
BENCHMARK(BM_Goalserve_BuildUrl_InplayOdds);

// ---------- 2. BuildUrl: PregameOdds (含 cat={X}_10 ?json=1 拼接) ----------
void BM_Goalserve_BuildUrl_PregameOdds(benchmark::State& state) {
    auto client = make_client();
    UrlSpec spec;
    spec.host     = GoalserveHost::Www;
    spec.endpoint = GoalserveEndpoint::PregameOdds;
    spec.json     = true;
    spec.bookmaker = "bet365,pinnacle";
    std::size_t i = 0;
    for (auto _ : state) {
        spec.sport = static_cast<GoalserveSport>(i % kNumSports);
        ++i;
        auto url = client.BuildUrl(spec);
        benchmark::DoNotOptimize(url);
    }
}
BENCHMARK(BM_Goalserve_BuildUrl_PregameOdds);

// ---------- 3. BuildUrl: InplayResults (含 yyyymm + match_id) ----------
void BM_Goalserve_BuildUrl_InplayResults(benchmark::State& state) {
    auto client = make_client();
    UrlSpec spec;
    spec.host     = GoalserveHost::Inplay;
    spec.endpoint = GoalserveEndpoint::InplayResults;
    spec.sport    = GoalserveSport::Baseball;
    spec.yyyymm   = "202604";
    spec.match_id = "12345678";
    for (auto _ : state) {
        auto url = client.BuildUrl(spec);
        benchmark::DoNotOptimize(url);
    }
}
BENCHMARK(BM_Goalserve_BuildUrl_InplayResults);

// ---------- 4. ParseLastUpdateNs: ISO 8601 ----------
void BM_Goalserve_ParseLastUpdate_Iso8601(benchmark::State& state) {
    std::string_view sv = "2026-05-28T14:23:45Z";
    for (auto _ : state) {
        auto ns = GoalserveClient::ParseLastUpdateNs(sv);
        benchmark::DoNotOptimize(ns);
    }
}
BENCHMARK(BM_Goalserve_ParseLastUpdate_Iso8601);

// ---------- 5. ParseLastUpdateNs: "dd.MM.yyyy HH:mm" ----------
void BM_Goalserve_ParseLastUpdate_DDMMYYYY(benchmark::State& state) {
    std::string_view sv = "28.05.2026 14:23";
    for (auto _ : state) {
        auto ns = GoalserveClient::ParseLastUpdateNs(sv);
        benchmark::DoNotOptimize(ns);
    }
}
BENCHMARK(BM_Goalserve_ParseLastUpdate_DDMMYYYY);

// ---------- 6. ParseLastUpdateNs: parse-failure path (告警 fallback) ----------
void BM_Goalserve_ParseLastUpdate_Garbage(benchmark::State& state) {
    std::string_view sv = "not-a-timestamp";
    for (auto _ : state) {
        auto ns = GoalserveClient::ParseLastUpdateNs(sv);
        benchmark::DoNotOptimize(ns);
    }
}
BENCHMARK(BM_Goalserve_ParseLastUpdate_Garbage);

// ---------- 7. TsMsToNs (constexpr; 应 ~0ns) ----------
void BM_Goalserve_TsMsToNs(benchmark::State& state) {
    std::int64_t ms = 1'748'426'625'123LL;  // 2026-05-28 14:23:45.123Z 量级
    for (auto _ : state) {
        auto ns = GoalserveClient::TsMsToNs(ms);
        benchmark::DoNotOptimize(ns);
        ++ms;
    }
}
BENCHMARK(BM_Goalserve_TsMsToNs);

// ---------- 8. NowIngestionNs (本地 CLOCK_MONOTONIC_RAW 采集) ----------
void BM_Goalserve_NowIngestionNs(benchmark::State& state) {
    for (auto _ : state) {
        auto ns = GoalserveClient::NowIngestionNs();
        benchmark::DoNotOptimize(ns);
    }
}
BENCHMARK(BM_Goalserve_NowIngestionNs);

}  // namespace

BENCHMARK_MAIN();
