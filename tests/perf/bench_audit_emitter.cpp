// tests/perf/bench_audit_emitter.cpp — M3 AuditEmitter::emit_decision() p99
//
// 落: docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md §2 M3 + §6 Ask 2
// 预算: p99 ≤ 50us (含 PIT 100ns + hash chain stub 数十 ns + WalAppend 大头)
//
// 路径覆盖 6 个 (单 record per iter):
//   OrderApproved / OrderRejected / OrderFilled / OrderCancelled
//   SafeModeEnter / StrategyDecayed (走专门 emit_*)
//
// 注: WalConfig.path_prefix = "/tmp/stcpp-perf/audit_..." (R-11 校验: paper kind 需以
//      /var/lib/stcpp/paper/ 开头). bench 时把 STCPP_WAL_TEST_PATH_PREFIX_OVERRIDE override
//      到 tmpfs 是 framework 的事; 当前 wal_writer skeleton 仍允 in-mem ring write, 不真 fsync.
//      若 framework Open 严校 path prefix → fallback: 用 in-tree path 起 "/var/lib/stcpp/paper/perf_*"
//      (CI 写权限由 mkdir -p 准备, 见 .github/workflows/perf-regression.yml).

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <benchmark/benchmark.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_emitter.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/risk/reject_enum.hpp"

namespace {

using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::observability::AuditEmitter;
using stcpp::observability::AuditEventType;
using stcpp::observability::AuditRecord;
using stcpp::observability::RiskDecisionInput;
using stcpp::risk::InvalidIntentSubReason;
using stcpp::risk::RejectCode;

RiskDecisionInput make_valid_input(AuditEventType type, std::int64_t now) {
    const std::int64_t base = now - 1'000'000'000LL;  // 1s ago, 4ts + decision_ts 全在 now 前
    RiskDecisionInput in{};
    in.event_ts = base;
    in.data_source_ts = base + 1'000;
    in.ingestion_ts = base + 2'000;
    in.as_of_ts = base + 3'000;
    in.decision_ts = base + 4'000;
    in.audit_id_bytes = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    in.market_id = "mkt_bench";
    in.strategy_id = "strat_alpha";
    in.size_usdc = 1000;
    in.price = 0.55;
    in.is_buy = true;
    in.event_type = type;
    in.reject_code = RejectCode::INTERNAL_ERROR;
    in.sub_reason = InvalidIntentSubReason::NONE;
    return in;
}

// 起 1 个 WalWriter (paper kind, fixture 共享, bench loop 复用)
std::unique_ptr<WalWriter<AuditRecord>> open_writer(std::string_view tag) {
    WalConfig cfg{};
    cfg.kind = WalKind::PaperAudit;
    cfg.path_prefix = std::string("/var/lib/stcpp/paper/perf_audit_") + std::string(tag);
    auto r = WalWriter<AuditRecord>::Open(cfg);
    if (!r.has_value()) {
        return nullptr;
    }
    // WalResult.value() rvalue overload 返 T&&, std::move(r) 触发 (老王 v1 §6).
    return std::move(r).value();
}

// ---------- 1. OrderApproved (热路径 — 决策通过, audit emit) ----------
void BM_Audit_Emit_OrderApproved(benchmark::State& state) {
    auto w = open_writer("approved");
    if (!w) {
        state.SkipWithError("WalWriter Open failed");
        return;
    }
    AuditEmitter em{w.get()};
    for (auto _ : state) {
        const std::int64_t now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto in = make_valid_input(AuditEventType::OrderApproved, now);
        auto r = em.emit_decision(in);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Audit_Emit_OrderApproved);

// ---------- 2. OrderRejected (with sub_reason payload) ----------
void BM_Audit_Emit_OrderRejected(benchmark::State& state) {
    auto w = open_writer("rejected");
    if (!w) {
        state.SkipWithError("WalWriter Open failed");
        return;
    }
    AuditEmitter em{w.get()};
    for (auto _ : state) {
        const std::int64_t now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto in = make_valid_input(AuditEventType::OrderRejected, now);
        in.reject_code = RejectCode::INVALID_INTENT;
        in.sub_reason = InvalidIntentSubReason::TS_ORDER_VIOLATED;
        auto r = em.emit_decision(in);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Audit_Emit_OrderRejected);

// ---------- 3. OrderFilled ----------
void BM_Audit_Emit_OrderFilled(benchmark::State& state) {
    auto w = open_writer("filled");
    if (!w) {
        state.SkipWithError("WalWriter Open failed");
        return;
    }
    AuditEmitter em{w.get()};
    for (auto _ : state) {
        const std::int64_t now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto in = make_valid_input(AuditEventType::OrderFilled, now);
        auto r = em.emit_decision(in);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Audit_Emit_OrderFilled);

// ---------- 4. OrderCancelled ----------
void BM_Audit_Emit_OrderCancelled(benchmark::State& state) {
    auto w = open_writer("cancelled");
    if (!w) {
        state.SkipWithError("WalWriter Open failed");
        return;
    }
    AuditEmitter em{w.get()};
    for (auto _ : state) {
        const std::int64_t now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto in = make_valid_input(AuditEventType::OrderCancelled, now);
        auto r = em.emit_decision(in);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Audit_Emit_OrderCancelled);

// ---------- 5. SafeModeEnter (state-machine 专门入口) ----------
void BM_Audit_Emit_SafeModeEnter(benchmark::State& state) {
    auto w = open_writer("safemode");
    if (!w) {
        state.SkipWithError("WalWriter Open failed");
        return;
    }
    AuditEmitter em{w.get()};
    for (auto _ : state) {
        const std::int64_t now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto in = make_valid_input(AuditEventType::SafeModeEnter, now);
        auto r = em.emit_safe_mode_enter(in, "perf bench");
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Audit_Emit_SafeModeEnter);

// ---------- 6. StrategyDecayed (策略衰减专门入口) ----------
void BM_Audit_Emit_StrategyDecayed(benchmark::State& state) {
    auto w = open_writer("decay");
    if (!w) {
        state.SkipWithError("WalWriter Open failed");
        return;
    }
    AuditEmitter em{w.get()};
    for (auto _ : state) {
        const std::int64_t now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto in = make_valid_input(AuditEventType::StrategyDecayed, now);
        auto r = em.emit_strategy_decayed(in);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Audit_Emit_StrategyDecayed);

}  // namespace

BENCHMARK_MAIN();
