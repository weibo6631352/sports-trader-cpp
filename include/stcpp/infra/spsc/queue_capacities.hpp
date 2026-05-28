// stcpp/infra/spsc/queue_capacities.hpp — 5 SPSC/MPMC ring capacity 常量
//
// Owner: 小石 (data-structures-expert, #41)  W6 Wave 28 P0
// 落: laozhou-architecture-v0.6-e2e.md §4.1  5 queue 拓扑
//     2026-06-01-input-laozhou-arch-deviation.md §4  偏离 #4 P0
//
// 红线:
//   老姜 W5 latency budget hard ask: Capacity 必须 2^n
//   编译期 enforce — 所有 Capacity 违规 = static_assert 死亡
//
// 5 queue 一览:
//   MarketDataBus  65536  vCPU0 → vCPU1  SPSC
//   SignalQueue     8192  vCPU1 → vCPU2  SPSC
//   RiskQueue       4096  vCPU2 → vCPU3  SPSC (Allowed only)
//   FillQueue       8192  vCPU3 → {vCPU3 Position + vCPU4 ML}  MPMC 2-consumer
//   WALQueue       65536  multi-producer → vCPU3 group commit  SPSC-per-kind

#pragma once

#include <cstddef>
#include <type_traits>

namespace stcpp::infra::spsc {

// ---------- 编译期 2^n 校验 helper ----------------------------------------

template <std::size_t N>
struct IsPowerOfTwo : std::bool_constant<(N > 0) && ((N & (N - 1)) == 0)> {};

template <std::size_t N>
inline constexpr bool IS_POWER_OF_TWO = IsPowerOfTwo<N>::value;

// 在 constexpr 上下文安全调用 — capacity 非 2^n 直接编译失败
template <std::size_t Capacity>
consteval std::size_t enforce_power_of_two() noexcept {
    static_assert(IS_POWER_OF_TWO<Capacity>,
                  "SPSC/MPMC capacity must be a power of two (老姜 latency budget hard ask)");
    return Capacity;
}

// ---------- 5 queue capacity (老周 v0.6 §4.1) --------------------------------

// vCPU0 Ingest → vCPU1 Signal Engine
// Polymarket WSS + Goalserve 全盘口峰值 ~5000 evt/s → 13s buffer 防 vCPU1 stall
inline constexpr std::size_t MARKET_DATA_BUS_CAPACITY = enforce_power_of_two<65536>();

// vCPU1 Signal Engine → vCPU2 RiskGateway
// 策略带门槛, signal 远少于 market evt (~500/s), 16s buffer
inline constexpr std::size_t SIGNAL_QUEUE_CAPACITY = enforce_power_of_two<8192>();

// vCPU2 RiskGateway → vCPU3 PaperSigner  (Allowed only, Reject 走 AuditEmitter)
// RM 大量 Reject 不入此 ring, 4096 ≈ 5s buffer
inline constexpr std::size_t RISK_QUEUE_CAPACITY = enforce_power_of_two<4096>();

// vCPU3 VirtualMatcher → {vCPU3 Position Ledger, vCPU4 ML hook}  MPMC 2-consumer
// 1 Allowed ≈ 1 Fill, 与 RiskQueue 同量 + 2x margin
inline constexpr std::size_t FILL_QUEUE_CAPACITY = enforce_power_of_two<8192>();

// per-kind WALQueue: group commit batch 64 OR 1ms
// 65536 × 1ms tick = 极端峰值保证不丢 (WAL 永不丢是 R-11 基础)
inline constexpr std::size_t WAL_QUEUE_CAPACITY = enforce_power_of_two<65536>();

// ---------- back-pressure 策略说明 (v0.6 §4.2) ---------------------------
//
//   MarketDataBus : drop newest  → counter mdb_drop_total         (R-12 vCPU0 不阻)
//   SignalQueue   : drop newest  → counter signal_drop_total       (P1 alert > 0)
//   RiskQueue     : REJECT(SYSTEM_BACKPRESSURE) + audit emit       (P0 alert)
//   FillQueue     : Position 端永不丢; ML 端 drop → fillq_ml_drop_total
//   WALQueue      : P0 (永不丢), overflow → walq_overflow_total   (P0 alert)

}  // namespace stcpp::infra::spsc
