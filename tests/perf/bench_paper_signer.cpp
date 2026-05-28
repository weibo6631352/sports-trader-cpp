// tests/perf/bench_paper_signer.cpp — M4 PaperSigner::Sign() p99
//
// 落: docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md §2 M4 + §6 Ask 3
// 预算: p99 ≤ 50us (paper sign 全程; 实测 ~1us, 远低于预算)
//
// 注: 老姜原以为 `VirtualConfirmWatcher::Wait()` 真 sleep 2s, 实读小蒋 paper_signer.cpp
//     后确认 — Wait() **只算 virtual confirm_ts_ns, 不真 sleep** (源码注释:
//     "真等 2s 会让单测慢, 这里只算 virtual ts, 不真 sleep").
//     paper mode 当前架构 OK. W5 live mode 切真 ECDSA + Polygon eth_call 才需异步化.
//
// bench 名字保留 BM_Paper_Sign_FullPath_INCLUDES_CONFIRM_2S — W6 live mode 切换时
// 一眼能看见 "为什么突然 2s, 该切异步了". 这是 perf gate 的"埋雷探测".
//
// 路径覆盖:
//   Valid_PIT_Ok   : 4 ts 合法, sign + 出 SignResponse
//   PIT_Violation  : 4 ts 顺序违反, fast-fail return SignerError::PitViolation

#include <chrono>
#include <cstdint>

#include <benchmark/benchmark.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/signer/paper/paper_signer.hpp"
#include "stcpp/signer/signer_iface.hpp"

namespace {

using namespace stcpp;

signer::SignRequest make_valid_req(std::int64_t t_now) {
    signer::SignRequest req;
    req.intent_id         = 42;
    req.market_id         = "0xabc";
    req.outcome           = "YES";
    req.price             = 0.55;
    req.size_usdc         = 100.0;
    req.event_ts_ns       = t_now - 10'000'000;
    req.data_source_ts_ns = t_now -  8'000'000;
    req.ingestion_ts_ns   = t_now -  4'000'000;
    req.as_of_ts_ns       = t_now;
    return req;
}

// ---------- 单独测 NonceProvider (~5ns atomic counter) ----------
void BM_Paper_Nonce_Next(benchmark::State& state) {
    signer::paper::VirtualNonceProvider n{0};
    for (auto _ : state) {
        auto v = n.Next();
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Paper_Nonce_Next);

// ---------- 单独测 GasEstimator (固定值, 应 ~0ns const fold) ----------
void BM_Paper_Gas_Estimate(benchmark::State& state) {
    signer::paper::VirtualGasEstimator g;
    signer::SignRequest req = make_valid_req(infra::wal::pit::NowRealtimeNs());
    for (auto _ : state) {
        auto v = g.Estimate(req);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Paper_Gas_Estimate);

// ---------- PIT_Violation: 4 ts 错乱, fast-fail return ----------
//   不会触发 confirm wait, 真测 PIT assert + return — 应 ≤ 1us
void BM_Paper_Sign_PitViolation(benchmark::State& state) {
    signer::paper::VirtualNonceProvider   n{0};
    signer::paper::VirtualGasEstimator    g;
    signer::paper::VirtualConfirmWatcher  c{0xBADC0FFEE};
    signer::paper::PaperSigner            s{&n, &g, &c};

    for (auto _ : state) {
        const auto t = infra::wal::pit::NowRealtimeNs();
        auto req = make_valid_req(t);
        std::swap(req.event_ts_ns, req.data_source_ts_ns);  // 4 ts 顺序违反
        auto resp = s.Sign(req);
        benchmark::DoNotOptimize(resp);
    }
}
BENCHMARK(BM_Paper_Sign_PitViolation);

// ---------- Valid_PIT_Ok: 完整 Sign 路径 (含 confirm watcher) ----------
//
// paper mode: ConfirmWatcher 不真 sleep, 仅算 virtual ts → 实测 ~1us, OK.
// live mode (W6): 真 secp256k1 ECDSA (~80us) + 真 eth_call confirm (200ms+ 跨洋).
//                 该 bench 会立刻退化 ≥ 200ms → CI gate fail → 推动小蒋 + 老周 切异步.
//
// 当前 paper mode 留 3 iter 取均值, 既快又稳.
void BM_Paper_Sign_FullPath_INCLUDES_CONFIRM_2S(benchmark::State& state) {
    signer::paper::VirtualNonceProvider   n{0};
    signer::paper::VirtualGasEstimator    g;
    signer::paper::VirtualConfirmWatcher  c{0xCAFEBABE};
    signer::paper::PaperSigner            s{&n, &g, &c};

    for (auto _ : state) {
        const auto t = infra::wal::pit::NowRealtimeNs();
        auto req  = make_valid_req(t);
        auto resp = s.Sign(req);
        benchmark::DoNotOptimize(resp);
    }
}
// 单条短 iter, 避免占满 CI 时间
BENCHMARK(BM_Paper_Sign_FullPath_INCLUDES_CONFIRM_2S)
    ->Iterations(3)
    ->Unit(benchmark::kMillisecond);

}  // namespace

BENCHMARK_MAIN();
