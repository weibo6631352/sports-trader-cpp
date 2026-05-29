// tests/perf/bench_risk_gateway.cpp — M2 RiskGateway::evaluate() p99
//
// 落: docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md §2 M2 + §6 Ask 1
// 预算: p99 ≤ 100 us (含 21 enum short-circuit + SlippageModel + emit_audit stub)
//
// 7 主路径:
//   Approved              : 全绿通过 (热路径, 最重要 — critical path 主流)
//   Reject_State_Halted   : 状态机 fast-fail (最浅 short-circuit, 应最快)
//   Reject_Invalid_Intent : R-20 4 ts PIT 违反 (sub_reason 字段化)
//   Reject_Duplicate      : signal_id idempotency cache hit
//   Reject_Stale_Data     : MarketState 5 档阈值
//   Reject_Per_Order_Cap  : 仓位 / 资金检查
//   Reject_Low_Fill_Rate  : SlippageModel reject 透传
//
// 注: emit 用 NoopEmitter (永 true, 不写 IO). 真 emit 走 M3 bench 测.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <benchmark/benchmark.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/risk_gateway.hpp"

namespace {

using namespace stcpp::risk;

class NoopEmitter : public AuditEmitter {
public:
    [[nodiscard]] bool emit(AuditRecord const& r) noexcept override {
        // 不调 benchmark::DoNotOptimize(r) — Google Benchmark v1.8 标 const-ref 版 deprecated.
        // 只读字段防被优化掉:
        last_market_size_ = r.market_id.size();
        ++count_;
        return true;
    }
    std::uint64_t count() const noexcept { return count_; }

private:
    std::uint64_t count_{0};
    std::size_t last_market_size_{0};
};

constexpr std::int64_t NS_PER_MS = 1'000'000LL;

// 合法 4 ts intent (R-20 单调) + book fresh + 价格 / size 合理
OrderIntent make_ok_intent(std::int64_t now, std::string sig) {
    OrderIntent it;
    it.event_ts_ns = now - 500 * NS_PER_MS;
    it.data_source_ts_ns = now - 400 * NS_PER_MS;
    it.ingestion_ts_ns = now - 100 * NS_PER_MS;
    it.as_of_ts_ns = now - 10 * NS_PER_MS;
    it.market_id = "mkt_bench";
    it.strategy_id = "strat_a";
    it.signal_id = std::move(sig);
    it.feature_snapshot_id = "fs_01H";
    it.is_buy = true;
    it.price = 0.50;
    it.size_usdc = 1'000;
    it.book_depth_l1_usdc = 5'000;
    it.book_snapshot_ts_ns = now - 200 * NS_PER_MS;
    it.tick_size = 0.01;
    it.is_close = false;
    return it;
}

RiskConfig make_cfg() {
    RiskConfig c;
    c.per_order_cap_usdc = 10'000;
    c.market_exposure_cap_usdc = 50'000;
    c.bankroll_usdc = 100'000;
    c.daily_loss_halt_usdc = 5'000;
    c.consec_loss_halt_count = 5;
    c.excessive_slippage_bps = 200;
    c.enable_moneyline = true;
    return c;
}

// 准备 fixture (单次 ctor + 状态 set, 之后 bench loop 复用)
struct Fx {
    std::shared_ptr<NoopEmitter> emitter;
    std::unique_ptr<RiskGateway> rm;

    explicit Fx(std::string const& market = "mkt_bench") {
        emitter = std::make_shared<NoopEmitter>();
        rm = std::make_unique<RiskGateway>(make_cfg(), emitter);
        rm->set_state(RmState::RUNNING);
        rm->set_market_active(market, true);
        rm->set_market_state(market, MarketState::PREGAME);
        rm->set_market_freshness_ms(market, 100);  // < 5000 warn
        rm->set_market_exposure(market, 0);
        rm->set_bankroll(100'000);
        rm->set_daily_pnl(0);
        rm->set_consec_loss(0);
    }
};

// ---------- 1. Approved (热路径) ----------
void BM_RiskGateway_Approved(benchmark::State& state) {
    Fx fx;
    std::int64_t i = 0;
    for (auto _ : state) {
        // 每次换 signal_id 避免 DUPLICATE_INTENT cache hit (clear 也行, 但每次 atomic ++ 更轻)
        const auto now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto it = make_ok_intent(now, "sig_bench_" + std::to_string(i++));
        auto d = fx.rm->evaluate(it);
        benchmark::DoNotOptimize(d);
    }
}
BENCHMARK(BM_RiskGateway_Approved);

// ---------- 2. Reject: STATE_HALTED (最浅 short-circuit) ----------
void BM_RiskGateway_Reject_StateHalted(benchmark::State& state) {
    Fx fx;
    fx.rm->set_state(RmState::HALTED);
    std::int64_t i = 0;
    for (auto _ : state) {
        const auto now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto it = make_ok_intent(now, "sig_halt_" + std::to_string(i++));
        auto d = fx.rm->evaluate(it);
        benchmark::DoNotOptimize(d);
    }
}
BENCHMARK(BM_RiskGateway_Reject_StateHalted);

// ---------- 3. Reject: INVALID_INTENT (R-20 ts 顺序错) ----------
void BM_RiskGateway_Reject_InvalidIntent_TsOrder(benchmark::State& state) {
    Fx fx;
    std::int64_t i = 0;
    for (auto _ : state) {
        const auto now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto it = make_ok_intent(now, "sig_inv_" + std::to_string(i++));
        // 故意违反: event > data_source
        std::swap(it.event_ts_ns, it.data_source_ts_ns);
        auto d = fx.rm->evaluate(it);
        benchmark::DoNotOptimize(d);
    }
}
BENCHMARK(BM_RiskGateway_Reject_InvalidIntent_TsOrder);

// ---------- 4. Reject: DUPLICATE_INTENT (idempotency cache hit) ----------
void BM_RiskGateway_Reject_Duplicate(benchmark::State& state) {
    Fx fx;
    const auto now = ::stcpp::infra::wal::pit::NowRealtimeNs();
    // 第一次 emit 把 sig_dup_fixed 放进 cache, 之后 bench loop 命中
    auto warmup = make_ok_intent(now, "sig_dup_fixed");
    (void)fx.rm->evaluate(warmup);
    for (auto _ : state) {
        auto it = make_ok_intent(now, "sig_dup_fixed");
        auto d = fx.rm->evaluate(it);
        benchmark::DoNotOptimize(d);
    }
}
BENCHMARK(BM_RiskGateway_Reject_Duplicate);

// ---------- 5. Reject: STALE_DATA (market freshness > halt) ----------
void BM_RiskGateway_Reject_StaleData(benchmark::State& state) {
    Fx fx;
    fx.rm->set_market_state("mkt_bench", MarketState::INPLAY_HOT_CRIT);
    fx.rm->set_market_freshness_ms("mkt_bench", 1'500);  // > 800ms halt for HOT_CRIT
    std::int64_t i = 0;
    for (auto _ : state) {
        const auto now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto it = make_ok_intent(now, "sig_stale_" + std::to_string(i++));
        auto d = fx.rm->evaluate(it);
        benchmark::DoNotOptimize(d);
    }
}
BENCHMARK(BM_RiskGateway_Reject_StaleData);

// ---------- 6. Reject: EXCEED_PER_ORDER_CAP ----------
void BM_RiskGateway_Reject_PerOrderCap(benchmark::State& state) {
    Fx fx;
    std::int64_t i = 0;
    for (auto _ : state) {
        const auto now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto it = make_ok_intent(now, "sig_cap_" + std::to_string(i++));
        it.size_usdc = 50'000;  // > per_order_cap 10'000
        auto d = fx.rm->evaluate(it);
        benchmark::DoNotOptimize(d);
    }
}
BENCHMARK(BM_RiskGateway_Reject_PerOrderCap);

// ---------- 7. Reject: LOW_FILL_RATE (SlippageModel reject 透传) ----------
void BM_RiskGateway_Reject_LowFillRate(benchmark::State& state) {
    Fx fx;
    std::int64_t i = 0;
    for (auto _ : state) {
        const auto now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto it = make_ok_intent(now, "sig_lfr_" + std::to_string(i++));
        // depth 极小 → ρ 极大 → 多层 ExceedBookDepth / FillRateBelowFloor
        it.book_depth_l1_usdc = 10.0;
        auto d = fx.rm->evaluate(it);
        benchmark::DoNotOptimize(d);
    }
}
BENCHMARK(BM_RiskGateway_Reject_LowFillRate);

}  // namespace

BENCHMARK_MAIN();
