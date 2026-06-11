// tests/perf/bench_tick_one.cpp — TickAll 决策热路径延迟 vs 市场数 (老姜性能评审 P1)
//
// 三方评审一致最优先数据: "单 tick 耗时 vs 市场数曲线" (老周) / "TickAll latency budget" (老姜)。
// 它是后续所有分片/优化决策的前置数据 —— 没这条曲线, 决策线程分片/锁优化都是拍脑袋 (反过度设计)。
//
// 测的是: TradingLoop::TickAllForBench() 同步跑一次 (不经 RunLoop sleep), 遍历 N condition × TickOne。
//   stub fair 路径 (无 score_store → has_real_fair=false → 无 intent): 测的是 per-condition 基线
//   (hub Read×2 + de-vig + extract_full×2 + quote/fv publish), 即 ONNX 之外的决策成本。
//   ⚠ 本 bench 不含 ONNX predict (ml_model_=nullptr) — ONNX 延迟另测 (bench_onnx_predict, 待补)。
//
// 跑: ./tests/perf/bench_tick_one --benchmark_min_time=0.5s
#include <memory>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "stcpp/engine/trading_loop.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/pricing/fair_value_estimator.hpp"
#include "stcpp/risk/ledger_snapshot_hub.hpp"
#include "stcpp/risk/position_ledger.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/sizing/quote_snapshot_hub.hpp"

using namespace stcpp;
using namespace stcpp::engine;
using namespace stcpp::polymarket::clob_wss;
using namespace stcpp::risk;
using namespace stcpp::sizing;
using namespace stcpp::pricing;

namespace {

// 有效 L1 book, 4ts 相对 now (RM freshness 友好)。
OrderBookFeatures MakeBook(double bid, double ask, std::int64_t now_ns) {
    OrderBookFeatures f{};
    f.valid = true;
    f.event_ts_ns = now_ns - 3'000'000'000LL;
    f.data_source_ts_ns = now_ns - 2'000'000'000LL;
    f.ingestion_ts_ns = now_ns - 1'000'000'000LL;
    f.as_of_ts_ns = now_ns;
    f.bids[0].price = bid;
    f.bids[0].size_usdc = 500.0;
    f.asks[0].price = ask;
    f.asks[0].size_usdc = 500.0;
    f.microprice = (bid + ask) * 0.5;
    f.mid = (bid + ask) * 0.5;
    f.spread = ask - bid;
    f.imbalance = 0.0;
    f.wss_state = WssConnState::kConnected;
    f.sequence_no = 1;
    return f;
}

std::int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// 构建 N 市场的 TradingLoop (token_map N 项 + hub 各 2 book), 返回 loop (deps 由 out-params 保活)。
struct LoopHarness {
    std::unique_ptr<OrderBookSnapshotHub> hub;
    std::unique_ptr<LedgerSnapshotHub> ledger_hub;
    std::unique_ptr<QuoteSnapshotHub> quote_hub;
    std::unique_ptr<RmDebugSnapshot> rm_snap;
    std::unique_ptr<PositionLedger> ledger;
    std::unique_ptr<RiskGateway> rm;
    std::unique_ptr<BaselineFairValueModel> fv_model;
    std::unique_ptr<TradingLoop> loop;
};

LoopHarness BuildHarness(int n_markets) {
    LoopHarness h;
    h.hub = std::make_unique<OrderBookSnapshotHub>();
    h.ledger_hub = std::make_unique<LedgerSnapshotHub>();
    h.quote_hub = std::make_unique<QuoteSnapshotHub>();
    h.rm_snap = std::make_unique<RmDebugSnapshot>();
    h.ledger = std::make_unique<PositionLedger>();

    RiskConfig rm_cfg;
    rm_cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(10'000'000);
    rm_cfg.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_micro(25'000'000);
    rm_cfg.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_micro(100'000'000);
    rm_cfg.bankroll_usdc = stcpp::domain::MicroPUSD::from_micro(1'000'000'000);
    rm_cfg.edge_ci_lower_floor = -1.0;
    h.rm = std::make_unique<RiskGateway>(rm_cfg, nullptr);

    h.fv_model = std::make_unique<BaselineFairValueModel>(ScorePriorParams{0.30, 0.50}, 0.20);

    const std::int64_t now = NowNs();
    std::unordered_map<std::string, std::pair<std::string, std::string>> token_map;
    token_map.reserve(static_cast<std::size_t>(n_markets));
    for (int i = 0; i < n_markets; ++i) {
        const std::string yes = "Y" + std::to_string(i);
        const std::string no = "N" + std::to_string(i);
        token_map[std::string("cond-") + std::to_string(i)] = {yes, no};
        // YES book: bid 0.50 / ask 0.55; NO book: 互补。
        h.hub->Publish(yes, MakeBook(0.50, 0.55, now));
        h.hub->Publish(no, MakeBook(0.45, 0.50, now));
    }

    TradingLoopConfig cfg;
    cfg.tick_interval_ms = 50;
    cfg.bankroll_usdc = 1000.0;
    cfg.n_effective = 30;
    cfg.z_90 = 1.645;
    cfg.strategy_id = "bench";
    h.loop = std::make_unique<TradingLoop>(*h.hub, *h.rm, *h.ledger, *h.ledger_hub, *h.quote_hub,
                                         h.rm_snap.get(), *h.fv_model, std::move(token_map), cfg);
    return h;
}

void BM_TickAll_NMarkets(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    LoopHarness h = BuildHarness(n);
    for (auto _ : state) {
        h.loop->TickAllForBench();
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * n);  // condition/s 吞吐
    state.counters["markets"] = n;
    state.counters["us_per_market"] = benchmark::Counter(
        static_cast<double>(n), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}
BENCHMARK(BM_TickAll_NMarkets)->Arg(1)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Arg(1000);

}  // namespace

BENCHMARK_MAIN();
