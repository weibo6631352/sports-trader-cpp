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
#include <vector>

#include <benchmark/benchmark.h>

#include "stcpp/domain/micro_pusd.hpp"  // A2: MicroPUSD cap 字段 (OrderIntent v0.6 + RiskConfig)
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/risk_gateway.hpp"

namespace {

using namespace stcpp::risk;

class NoopEmitter : public AuditEmitter {
public:
    [[nodiscard]] bool emit(AuditRecord const& r) noexcept override {
        // 不调 benchmark::DoNotOptimize(r) — Google Benchmark v1.8 标 const-ref 版 deprecated.
        // 只读字段防被优化掉 (A2: v0.4 market_id → v0.5 condition_id):
        last_market_size_ = r.condition_id.size();
        ++count_;
        return true;
    }
    std::uint64_t count() const noexcept { return count_; }

private:
    std::uint64_t count_{0};
    std::size_t last_market_size_{0};
};

constexpr std::int64_t NS_PER_MS = 1'000'000LL;

// A2: v0.5/v0.6 OrderIntent 字段对齐 (mirror tests/unit/test_risk_gateway.cpp make_ok_intent)
//   market_id→condition_id, is_buy→side, size_usdc→size_pUSD_micro (micro), +token_id/outcome/timestamp_ms
constexpr const char* kMockTokenId = "1234567890";  // uint256 string, 纯数字 ≤77 位
constexpr const char* kMockConditionId =
    "0xa9db600590209698097db2fb8382989ea1cf6a9b91f0428b2e1d4f35d724c3ff";  // bytes32 hex

// 合法 4 ts intent (R-20 单调) + book fresh + 价格 / size 合理 + V2 timestamp_ms 非零
OrderIntent make_ok_intent(std::int64_t now, std::string sig) {
    OrderIntent it;
    it.event_ts_ns = now - 500 * NS_PER_MS;
    it.data_source_ts_ns = now - 400 * NS_PER_MS;
    it.ingestion_ts_ns = now - 100 * NS_PER_MS;
    it.as_of_ts_ns = now - 10 * NS_PER_MS;
    it.condition_id = kMockConditionId;  // v0.5: was market_id
    it.token_id = kMockTokenId;          // v0.5: new
    it.outcome = Outcome::Yes;           // v0.5: new
    it.side = Side::Buy;                 // v0.5: was is_buy=true
    it.strategy_id = "strat_a";
    it.signal_id = std::move(sig);
    it.feature_snapshot_id = "fs_01H";
    it.price = 0.50;
    it.size_pUSD_micro = 1'000;  // 0.001 pUSD micro < per_order_cap (0.01 pUSD), 热路径 Approved
    it.book_depth_l1_usdc = 5'000;
    it.book_snapshot_ts_ns = now - 200 * NS_PER_MS;
    it.tick_size = 0.01;
    it.is_close = false;
    // v0.6 Wave 3: V2 CLOB timestamp_ms 必须非零且在 [now_ms-60s, now_ms+5s] 窗口 (否则 TS_V2_MISSING 拒)
    it.timestamp_ms = now / NS_PER_MS;
    return it;
}

RiskConfig make_cfg() {
    RiskConfig c;
    c.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(10'000);
    c.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_micro(50'000);
    c.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_micro(25'000);
    c.bankroll_usdc = stcpp::domain::MicroPUSD::from_pusd(100'000.0);       // c2b 100k pUSD
    c.daily_loss_halt_usdc = stcpp::domain::MicroPUSD::from_pusd(5'000.0);  // c2b 5k pUSD
    c.consec_loss_halt_count = 5;
    c.excessive_slippage_bps = 200;
    return c;
}

// 准备 fixture (单次 ctor + 状态 set, 之后 bench loop 复用)
struct Fx {
    std::shared_ptr<NoopEmitter> emitter;
    std::unique_ptr<RiskGateway> rm;

    explicit Fx(std::string const& cid = kMockConditionId) {
        emitter = std::make_shared<NoopEmitter>();
        rm = std::make_unique<RiskGateway>(make_cfg(), emitter);
        rm->set_state(RmState::RUNNING);
        rm->set_market_active(cid, true);
        rm->set_market_state(cid, MarketState::PREGAME);
        rm->set_market_freshness_ms(cid, 100);  // < 5000 warn
        rm->set_condition_exposure(cid, 0);     // A2: micro exposure (v0.5 condition 级)
        rm->set_bankroll(stcpp::domain::MicroPUSD::from_pusd(100'000.0).v);  // A2: micro
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
    fx.rm->set_market_state(kMockConditionId, MarketState::INPLAY_HOT_CRIT);
    fx.rm->set_market_freshness_ms(kMockConditionId, 1'500);  // > 800ms halt for HOT_CRIT
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
        it.size_pUSD_micro = 50'000;  // > per_order_cap 10'000 (micro)
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

// ---------- 8. BM_RmFeed: FeedRiskGateway O(N) 喂数基线 (synthesis §3) --------
// trading_loop FeedRiskGateway 每 tick 把 N 个仓位的 condition/outcome exposure 灌进 RM
// (set_condition_exposure + set_outcome_exposure)。每次调用走 s_->mu 锁, 故 O(N) × 锁。
// 此 bench 量 N 次 set_*_exposure 的纯成本 (不含 ledger 遍历), 给 N 退化基线。
// MVP N<20 可忽略 (~10-20us/500ms tick); N=500 看锁累积。Range: 1 / 20 / 100 / 500。
void BM_RmFeed_NPositions(benchmark::State& state) {
    Fx fx;
    const auto n = static_cast<int>(state.range(0));
    // 预生成 N 个不同 condition_id / token_id (避免 loop 内 string 构造污染计时)
    std::vector<std::string> cids, tids;
    cids.reserve(static_cast<std::size_t>(n));
    tids.reserve(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        cids.push_back("0xcond" + std::to_string(k));
        tids.push_back(std::to_string(1'000'000 + k));
    }
    std::int64_t v = 0;
    for (auto _ : state) {
        for (int k = 0; k < n; ++k) {
            // micro exposure (A1: 直喂 micro, 无 ×1e6)
            fx.rm->set_condition_exposure(cids[static_cast<std::size_t>(k)], v + k);
            fx.rm->set_outcome_exposure(tids[static_cast<std::size_t>(k)], v + k);
        }
        ++v;
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_RmFeed_NPositions)->Arg(1)->Arg(20)->Arg(100)->Arg(500);

}  // namespace

BENCHMARK_MAIN();
