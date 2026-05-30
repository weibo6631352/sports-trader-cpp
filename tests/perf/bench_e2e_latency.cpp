// tests/perf/bench_e2e_latency.cpp — Paper E2E 完整链路 latency bench v1.2
//
// 老姜 W6 Wave 28 — ADR-015 配套 + Wave 26 决议 2
// 落: docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md §1 端到端
//     docs/ADR/2026-06-01-adr-015-vcpu-pin.md
//     2026-06-01-code-review-summit-v1.md §3.2 决议 #2
//
// E2E 链路 (paper, single-thread):
//   WSS recv (mock) → SignalQueue (mock) → RM.evaluate → PaperSigner.Sign
//     → VirtualMatcher.Match → FillQueue (mock) → Position (atomic update)
//
// guard:
//   lower guard (Wave 26 决议 2): p99 >= 100 ns — 防 dead-code eliminate
//   upper guard (M1 G1):          p99 <= 50 ms  — ADR-015 + 老周 v0.6 §2
//
// 注: SPSC mock 用 inline call (不起线程). 小石 W6 SPSC 就位后 join 改造.
//     CI guard 在 perf-regression.yml lower/upper guard steps 中检查.
//
// 输出 JSON: --benchmark_format=json --benchmark_out=perf-results/e2e_latency.json

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <benchmark/benchmark.h>

#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/signer/paper/paper_signer.hpp"

namespace {

using namespace stcpp;

// ---- NoopEmitter: 防 bench 被 audit WAL IO 干扰 ----------------------------

class NoopEmitter : public risk::AuditEmitter {
public:
    [[nodiscard]] bool emit(risk::AuditRecord const& r) noexcept override {
        last_sig_ = r.signal_id.size();  // 防 DCE
        return true;
    }

private:
    std::size_t last_sig_{0};
};

// ---- MockPosition: 原子 position ledger (防 DCE + 模拟真实写) ---------------

struct MockPosition {
    std::atomic<std::int64_t> filled_usdc{0};
    std::atomic<std::int64_t> fill_count{0};
    void apply(std::int64_t size) noexcept {  // A1: fill_size micro int64
        filled_usdc.fetch_add(size, std::memory_order_relaxed);
        fill_count.fetch_add(1, std::memory_order_relaxed);
    }
};

// ---- MakeFreshTs: R-20 4 ts (Wave 21 §7.5 约定) ----------------------------

struct FreshTs {
    std::int64_t ev, ds, ig, ao;
};
[[nodiscard]] inline FreshTs MakeFreshTs() noexcept {
    const std::int64_t t = infra::wal::pit::NowRealtimeNs();
    return {t - 10'000'000LL, t - 8'000'000LL, t - 4'000'000LL, t};
}

// ---- Fixture: shared setup (避免每个 BM 函数重复 ctor) ----------------------

struct E2EFixture {
    std::shared_ptr<NoopEmitter> emitter;
    std::unique_ptr<risk::RiskGateway> rm;
    signer::paper::VirtualNonceProvider nonce{0};
    signer::paper::VirtualGasEstimator gas;
    signer::paper::VirtualConfirmWatcher confirm{0xCAFE'BEEF_ULL};
    signer::paper::PaperSigner signer{nullptr, nullptr, nullptr};
    execution::VirtualMatcher matcher{0xE2E_SEED_ULL};
    MockPosition pos;

    E2EFixture() {
        emitter = std::make_shared<NoopEmitter>();
        risk::RiskConfig cfg;
        cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(10'000);
        cfg.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_micro(500'000);
        cfg.bankroll_usdc = 1'000'000;
        cfg.daily_loss_halt_usdc = 50'000;
        cfg.consec_loss_halt_count = 50;
        cfg.excessive_slippage_bps = 300;
        cfg.enable_moneyline = true;
        rm = std::make_unique<risk::RiskGateway>(cfg, emitter);
        rm->set_state(risk::RmState::RUNNING);
        rm->set_market_active("mkt_e2e", true);
        rm->set_market_state("mkt_e2e", risk::MarketState::PREGAME);
        rm->set_market_freshness_ms("mkt_e2e", 50);
        rm->set_market_exposure("mkt_e2e", 0);
        rm->set_bankroll(1'000'000);
        rm->set_daily_pnl(0);
        rm->set_consec_loss(0);
        signer = signer::paper::PaperSigner{&nonce, &gas, &confirm};
        matcher.SetUniformOverrideForTesting(0.0);  // deterministic fill (防 Bernoulli miss 噪声)
    }

    // 单笔 E2E 调用 (inline, 不跑 SPSC 线程; small stone join W6 后改)
    void run_one(benchmark::State& state, std::int64_t seq) {
        const auto ts = MakeFreshTs();

        // [1] WSS recv mock → intent 构造
        risk::OrderIntent intent;
        intent.event_ts_ns = ts.ev;
        intent.data_source_ts_ns = ts.ds;
        intent.ingestion_ts_ns = ts.ig;
        intent.as_of_ts_ns = ts.ao;
        intent.market_id = "mkt_e2e";
        intent.strategy_id = "strat_e2e";
        intent.signal_id = "e2e_" + std::to_string(seq);
        intent.feature_snapshot_id = "fs_e2e";
        intent.is_buy = true;
        intent.price = 0.55;
        intent.size_usdc = 500;
        intent.book_depth_l1_usdc = 5'000.0;
        intent.book_snapshot_ts_ns = ts.ds;
        intent.tick_size = 0.01;

        // [2] RM evaluate
        auto dec = rm->evaluate(intent);
        benchmark::DoNotOptimize(dec);
        if (!dec.is_approved())
            return;

        // [3] PaperSigner.Sign
        signer::SignRequest sreq;
        sreq.intent_id = static_cast<std::uint64_t>(seq);
        sreq.market_id = "mkt_e2e";
        sreq.outcome = "YES";
        sreq.price = 0.55;
        sreq.size_usdc = 500.0;
        sreq.event_ts_ns = ts.ev;
        sreq.data_source_ts_ns = ts.ds;
        sreq.ingestion_ts_ns = ts.ig;
        sreq.as_of_ts_ns = ts.ao;
        auto sresp = signer.Sign(sreq);
        benchmark::DoNotOptimize(sresp);
        if (sresp.error != signer::SignerError::Ok)
            return;

        // [4] VirtualMatcher.Match
        execution::VirtualOrder vord;
        vord.intent_id = static_cast<std::uint64_t>(seq);
        vord.market_id = "mkt_e2e";
        vord.outcome = "YES";
        vord.size_usdc = 500.0;
        vord.quote_price = 0.55;
        vord.book_depth_l1_usdc = 5'000.0;
        vord.tick_size = 0.01;
        vord.event_ts_ns = ts.ev;
        vord.data_source_ts_ns = ts.ds;
        vord.ingestion_ts_ns = ts.ig;
        vord.as_of_ts_ns = ts.ao;
        vord.wall_now_ns = ts.ao;
        auto fill = matcher.Match(vord);
        benchmark::DoNotOptimize(fill);

        // [5] Position update (FillQueue mock: inline write)
        if (fill.reject == execution::MatchReject::Ok) {
            pos.apply(fill.fill_size_usdc);
        }
    }
};

// ---- BM_E2E_SingleThread: 完整链路, upper + lower guard 均适用 --------------
// upper guard: p99 <= 50 ms (CI yaml upper guard step 检查)
// lower guard: p99 >= 100 ns (CI yaml lower guard step 检查)
void BM_E2E_SingleThread(benchmark::State& state) {
    E2EFixture fx;
    std::int64_t seq = 0;
    for (auto _ : state) {
        fx.run_one(state, ++seq);
    }
    state.counters["fill_count"] =
        benchmark::Counter(static_cast<double>(fx.pos.fill_count.load()), benchmark::Counter::kAvgIterations);
}
BENCHMARK(BM_E2E_SingleThread)
    ->MinTime(2.0)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

// ---- BM_E2E_LowerGuard: 最轻路径; 如果 < 100 ns 表示 DCE -------------------
// Wave 26 决议 2: 防过度优化掩盖回归
// CI lower guard step 检查 BM_E2E_LowerGuard_median >= 100 ns
void BM_E2E_LowerGuard(benchmark::State& state) {
    // 只跑 RM (整条链中最轻的可观测步骤)
    auto emitter = std::make_shared<NoopEmitter>();
    risk::RiskConfig cfg;
    cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(10'000);
    cfg.bankroll_usdc = 1'000'000;
    cfg.enable_moneyline = true;
    auto rm = std::make_unique<risk::RiskGateway>(cfg, emitter);
    rm->set_state(risk::RmState::RUNNING);
    rm->set_market_active("mkt_lg", true);
    rm->set_market_state("mkt_lg", risk::MarketState::PREGAME);
    rm->set_market_freshness_ms("mkt_lg", 50);
    rm->set_market_exposure("mkt_lg", 0);
    rm->set_bankroll(1'000'000);
    rm->set_daily_pnl(0);
    rm->set_consec_loss(0);

    std::int64_t seq = 0;
    for (auto _ : state) {
        const auto ts = MakeFreshTs();
        risk::OrderIntent it;
        it.event_ts_ns = ts.ev;
        it.data_source_ts_ns = ts.ds;
        it.ingestion_ts_ns = ts.ig;
        it.as_of_ts_ns = ts.ao;
        it.market_id = "mkt_lg";
        it.strategy_id = "s";
        it.is_buy = true;
        it.signal_id = "lg_" + std::to_string(++seq);
        it.feature_snapshot_id = "fs";
        it.price = 0.55;
        it.size_usdc = 500;
        it.book_depth_l1_usdc = 5'000.0;
        it.book_snapshot_ts_ns = ts.ds;
        it.tick_size = 0.01;
        auto d = rm->evaluate(it);
        benchmark::DoNotOptimize(d);
    }
}
BENCHMARK(BM_E2E_LowerGuard)
    ->MinTime(1.0)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

}  // namespace

BENCHMARK_MAIN();
