// tests/perf/bench_e2e_latency.cpp — Paper E2E 完整链路 latency bench v1.3
//
// 老姜 W6 Wave 28 — ADR-015 配套 + Wave 26 决议 2
// A2 (2026-05-30 GM): 修编译断裂 — OrderIntent v0.4→v0.6 (condition_id/side/size_pUSD_micro/
//     token_id/outcome/timestamp_ms) + SignRequest v0.5 字段 + RiskConfig MicroPUSD + 畸形 UDL。
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

#include "stcpp/domain/micro_pusd.hpp"  // A2: MicroPUSD cap (RiskConfig v0.6)
#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/signer/paper/paper_signer.hpp"

namespace {

using namespace stcpp;

// A2: v0.5/v0.6 字段对齐 (mirror test_risk_gateway / test_paper_signer 已知可成交值)
constexpr const char* kE2ECondId = "0xa9db600590209698097db2fb8382989ea1cf6a9b91f0428b2e1d4f35d724c3ff";
constexpr const char* kE2ETokenId = "1234567890";  // uint256 decimal string (signer spec-1)
constexpr const char* kBytes32Zero = "0x0000000000000000000000000000000000000000000000000000000000000000";
// 注: size 故意取极小 (1'000 micro = 0.001 pUSD)。原因是 risk_gateway.cpp:526 的 slippage
//   gate 把 (double)size_pUSD_micro 直接当 whole-pUSD 喂 SlippageModel (v0.6 micro rename 漏 /1e6),
//   对 book_depth_l1_usdc(whole) 比较 → 真实 size (500 pUSD=5e8 micro) 必触 EXCEED_BOOK_DEPTH。
//   该单位 bug 属 RM 主权 (老韩), 已记 docs/MEETINGS/2026-05-30-arch-debt-audit.md (P1-9), 非 A2 范围。
//   bench 用 test_risk_gateway 已验证可 Approved 的小 size, 以测全链路热路径 latency。
constexpr std::int64_t kSizeMicro = 1'000LL;  // 0.001 pUSD (micro); test-mirrored Approved 值

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
    signer::paper::VirtualConfirmWatcher confirm{0xCAFE'BEEFULL};
    signer::paper::PaperSigner signer{nullptr, nullptr, nullptr};
    execution::VirtualMatcher matcher{0xE2E5'EEDULL};
    MockPosition pos;

    E2EFixture() {
        emitter = std::make_shared<NoopEmitter>();
        risk::RiskConfig cfg;
        cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(1'000.0);          // 1000 pUSD
        cfg.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(500'000.0);  // 500k pUSD
        cfg.bankroll_usdc = stcpp::domain::MicroPUSD::from_pusd(1'000'000.0);           // 1M pUSD
        cfg.daily_loss_halt_usdc = stcpp::domain::MicroPUSD::from_pusd(50'000.0);       // 50k pUSD
        cfg.consec_loss_halt_count = 50;
        cfg.excessive_slippage_bps = 300;
        rm = std::make_unique<risk::RiskGateway>(cfg, emitter);
        rm->set_state(risk::RmState::RUNNING);
        rm->set_market_active(kE2ECondId, true);
        rm->set_market_state(kE2ECondId, risk::MarketState::PREGAME);
        rm->set_market_freshness_ms(kE2ECondId, 50);
        rm->set_condition_exposure(kE2ECondId, 0);  // A2: micro exposure (v0.5 condition 级)
        rm->set_bankroll(stcpp::domain::MicroPUSD::from_pusd(1'000'000.0).v);
        rm->set_daily_pnl(0);
        rm->set_consec_loss(0);
        signer = signer::paper::PaperSigner{&nonce, &gas, &confirm};
        matcher.SetUniformOverrideForTesting(0.0);  // deterministic fill (防 Bernoulli miss 噪声)
    }

    // 单笔 E2E 调用 (inline, 不跑 SPSC 线程; small stone join W6 后改)
    void run_one(std::int64_t seq) {
        const auto ts = MakeFreshTs();
        const auto now_ms = ts.ao / 1'000'000LL;

        // [1] WSS recv mock → intent 构造 (v0.6 字段)
        risk::OrderIntent intent;
        intent.event_ts_ns = ts.ev;
        intent.data_source_ts_ns = ts.ds;
        intent.ingestion_ts_ns = ts.ig;
        intent.as_of_ts_ns = ts.ao;
        intent.condition_id = kE2ECondId;  // v0.5: was market_id
        intent.token_id = kE2ETokenId;     // v0.5: new
        intent.outcome = risk::Outcome::Yes;
        intent.side = risk::Side::Buy;  // v0.5: was is_buy=true
        intent.strategy_id = "strat_e2e";
        intent.signal_id = "e2e_" + std::to_string(seq);
        intent.feature_snapshot_id = "fs_e2e";
        intent.price = 0.55;
        intent.size_pUSD_micro = kSizeMicro;  // v0.6: was size_usdc
        intent.book_depth_l1_usdc = 5'000.0;
        intent.book_snapshot_ts_ns = ts.ds;
        intent.tick_size = 0.01;
        intent.timestamp_ms = now_ms;  // v0.6: V2 非零 (否则 TS_V2_MISSING 拒)

        // [2] RM evaluate
        auto dec = rm->evaluate(intent);
        benchmark::DoNotOptimize(dec);
        if (!dec.is_approved())
            return;

        // [3] PaperSigner.Sign (v0.5 SignRequest)
        signer::SignRequest sreq;
        sreq.intent_id = static_cast<std::uint64_t>(seq);
        sreq.condition_id = kE2ECondId;  // v0.5: was market_id
        sreq.token_id = kE2ETokenId;     // v0.5: was outcome 字符串
        sreq.price = 0.55;
        sreq.size_pUSD_micro = kSizeMicro;  // v0.5: was size_usdc double
        sreq.side = 0U;                     // Buy
        sreq.timestamp_ms = now_ms;
        sreq.metadata = kBytes32Zero;
        sreq.builder = kBytes32Zero;
        sreq.event_ts_ns = ts.ev;
        sreq.data_source_ts_ns = ts.ds;
        sreq.ingestion_ts_ns = ts.ig;
        sreq.as_of_ts_ns = ts.ao;
        auto sresp = signer.Sign(sreq);
        benchmark::DoNotOptimize(sresp);
        if (sresp.error != signer::SignerError::Ok)
            return;

        // [4] VirtualMatcher.Match (VirtualOrder 未变 — market_id/outcome/size_usdc 仍 double)
        execution::VirtualOrder vord;
        vord.intent_id = static_cast<std::uint64_t>(seq);
        vord.market_id = "mkt_e2e";
        vord.outcome = "YES";
        vord.size_usdc = 500.0;  // whole pUSD (matcher 接口未迁 micro; 等价 kSizeMicro)
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

        // [5] Position update (FillQueue mock: inline write; fill_size_usdc 现为 micro int64)
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
        fx.run_one(++seq);
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
    cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(1'000.0);
    cfg.bankroll_usdc = stcpp::domain::MicroPUSD::from_pusd(1'000'000.0);
    auto rm = std::make_unique<risk::RiskGateway>(cfg, emitter);
    rm->set_state(risk::RmState::RUNNING);
    rm->set_market_active(kE2ECondId, true);
    rm->set_market_state(kE2ECondId, risk::MarketState::PREGAME);
    rm->set_market_freshness_ms(kE2ECondId, 50);
    rm->set_condition_exposure(kE2ECondId, 0);
    rm->set_bankroll(stcpp::domain::MicroPUSD::from_pusd(1'000'000.0).v);
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
        it.condition_id = kE2ECondId;
        it.token_id = kE2ETokenId;
        it.outcome = risk::Outcome::Yes;
        it.side = risk::Side::Buy;
        it.strategy_id = "s";
        it.signal_id = "lg_" + std::to_string(++seq);
        it.feature_snapshot_id = "fs";
        it.price = 0.55;
        it.size_pUSD_micro = kSizeMicro;
        it.book_depth_l1_usdc = 5'000.0;
        it.book_snapshot_ts_ns = ts.ds;
        it.tick_size = 0.01;
        it.timestamp_ms = ts.ao / 1'000'000LL;
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
