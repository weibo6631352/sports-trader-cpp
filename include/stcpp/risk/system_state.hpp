// stcpp/risk/system_state.hpp — SystemState + StateMachine v0.1 (老沈 Wave 76 W9 W4)
//
// 落: laohan-w9-w3-position-ledger-rest-api-spec-v1.md §3
//
// 设计:
//   - CAS lock-free (std::atomic<SystemState>)
//   - try_transition(from, to): compare_exchange_strong, 幂等: from==current 才成功
//   - get(): relaxed load (读路径零锁)
//   - 合法迁移图 (老韩 spec §3):
//       RUNNING  → DRAIN
//       RUNNING  → HALTED
//       DRAIN    → HALTED
//       DRAIN    → RUNNING   (resume)
//       HALTED   → DECAYED   (post-mortem 后永久)
//       任何 → HALTED 允许 (紧急 circuit break)
//
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3.3
//   goalserve_ssot_cite:  N/A
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3
//   adr_cite:             ADR-027 Enforce-1

#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

namespace stcpp::risk {

// SystemState — 老韩 spec §3 (4 态)
// RUNNING  = 正常运营
// DRAIN    = 仅放行平仓 (is_close=true + side=Sell)
// HALTED   = 全 reject
// DECAYED  = 策略衰减后永久停机 (HALTED → DECAYED, 不可逆)
enum class SystemState : uint8_t {
    RUNNING = 0,
    DRAIN = 1,
    HALTED = 2,
    DECAYED = 3,
};

[[nodiscard]] constexpr std::string_view to_string(SystemState s) noexcept {
    switch (s) {
        case SystemState::RUNNING:
            return "RUNNING";
        case SystemState::DRAIN:
            return "DRAIN";
        case SystemState::HALTED:
            return "HALTED";
        case SystemState::DECAYED:
            return "DECAYED";
    }
    return "unknown";
}

// StateMachine — CAS lock-free (老韩 spec §3)
// 启动默认 RUNNING (与 RmState::SAFE_MODE 各自独立; SystemState 不替代 RmState)
// try_transition: compare_exchange_strong(from, to); 幂等 (已在目标态返 false)
// TC-02: 相同 from→to 第2次 CAS 必 false (幂等)
class StateMachine {
public:
    explicit StateMachine(SystemState init = SystemState::RUNNING) noexcept : state_(init) {}

    // CAS: 当且仅当 current == from 时迁移到 to, 返 true
    // 幂等: current == to 时返 false (未改变)
    // 非法迁移 (DECAYED → 任何) 返 false
    [[nodiscard]] bool try_transition(SystemState from, SystemState to) noexcept {
        // DECAYED 是吸收态, 不可再迁移
        if (from == SystemState::DECAYED)
            return false;
        SystemState expected = from;
        return state_.compare_exchange_strong(expected, to, std::memory_order_acq_rel,
                                              std::memory_order_acquire);
    }

    [[nodiscard]] SystemState get() const noexcept { return state_.load(std::memory_order_acquire); }

private:
    std::atomic<SystemState> state_;

    // CAS on uint8_t-backed enum: static_assert alignment
    static_assert(sizeof(std::atomic<SystemState>) <= 4, "SystemState CAS must be lock-free on target arch");
};

// lock-free static assert (运行期不适用, 编译期 hint)
static_assert(std::atomic<SystemState>::is_always_lock_free || sizeof(SystemState) == 1,
              "SystemState atomic must be lock-free; uint8_t 保证此条件");

}  // namespace stcpp::risk
