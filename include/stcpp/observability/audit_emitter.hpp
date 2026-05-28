// stcpp/observability/audit_emitter.hpp — AuditEmitter v0.1 (W4 Wave 19)
//
// 落:
//   laotang-audit-schema-v1.1.md §5 (emit_* API + hash chain owner)
//   laowang-wal-framework-cpp-interface-v1.md §8 (WalWriter<R>)
//   laohan-rm-v0.3.1 §6 (RiskDecision 联动)
//
// 职责:
//   1. 主入口 emit_decision(RiskDecision) → 6 AET (OrderApproved/Rejected/Deferred/Filled/Cancelled/TimedOut)
//   2. 状态机入口 emit_safe_mode_enter/exit, emit_state_transition, emit_unlock
//   3. 衰减/对账入口 emit_strategy_decayed, emit_recon_drift
//   4. BLAKE3 hash chain — 维护 last_hash_, 每条 record 写入 prev/payload/current
//   5. PIT 4 ts 透传 (R-20), emit 前 framework AssertChain 必拦
//
// 红线:
//   R-1   只一处 emit 路径 (本类), grep audit_event 全仓只此一处可生成
//   R-11  WalWriter kind = AuditWalKindForBuild() (build-time, paper / live 物理隔离)
//   R-20  4 ts 透传; PIT 失败 → framework 回 WalError::PitViolation, emit_recon_drift 兜底
//
// BLAKE3 stub (Sprint-3 老孙真上线):
//   stub_digest(payload) = 32B (前 8B = u64 sequence, 后 24B = 0)
//   stub_chain(prev, payload_hash) = prev XOR payload_hash (链式可校验)
//
// 不耻下问: WalWriter Append 错误码处置 @老王, BLAKE3 真算 @老孙 Sprint-3

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>

#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/risk/reject_enum.hpp"

namespace stcpp::observability {

// 与老韩 RiskDecision 联动的最小输入 (W4 stub — W5 切真 risk::RiskDecision)
struct RiskDecisionInput {
    // 4 ts (R-20)
    std::int64_t event_ts        = 0;
    std::int64_t data_source_ts  = 0;
    std::int64_t ingestion_ts    = 0;
    std::int64_t as_of_ts        = 0;
    std::int64_t decision_ts     = 0;     // > as_of_ts, RM 决策时刻

    std::array<std::uint8_t, 16>     audit_id_bytes{};
    std::string_view                 market_id;
    std::string_view                 strategy_id;
    std::int64_t                     size_usdc{0};
    double                           price{0.0};
    bool                             is_buy{true};

    // 决策
    AuditEventType                       event_type{AuditEventType::OrderApproved};
    stcpp::risk::RejectCode              reject_code{stcpp::risk::RejectCode::INTERNAL_ERROR};
    stcpp::risk::InvalidIntentSubReason  sub_reason{stcpp::risk::InvalidIntentSubReason::NONE};
};

class AuditEmitter {
 public:
    using WriterT = stcpp::infra::wal::WalWriter<AuditRecord>;
    using ResultT = stcpp::infra::wal::WalResult<std::uint64_t>;

    // 构造: 注入 WalWriter (R-11 物理隔离由 caller Open 时 path_prefix + AuditWalKindForBuild() 保证)
    explicit AuditEmitter(WriterT* writer) noexcept : writer_(writer) {}

    // 主入口 — RM 决策 → 6 AET (Approved/Rejected/Deferred/Filled/Cancelled/TimedOut)
    [[nodiscard]] ResultT emit_decision(const RiskDecisionInput& in) noexcept;

    // 状态机
    [[nodiscard]] ResultT emit_safe_mode_enter(const RiskDecisionInput& ctx,
                                               std::string_view reason) noexcept;
    [[nodiscard]] ResultT emit_safe_mode_exit(const RiskDecisionInput& ctx) noexcept;
    [[nodiscard]] ResultT emit_state_transition(const RiskDecisionInput& ctx,
                                                std::string_view from_to) noexcept;
    [[nodiscard]] ResultT emit_unlock(const RiskDecisionInput& ctx,
                                      std::string_view operator_id) noexcept;

    // 衰减 / 对账
    [[nodiscard]] ResultT emit_strategy_decayed(const RiskDecisionInput& ctx) noexcept;
    [[nodiscard]] ResultT emit_recon_drift(const RiskDecisionInput& ctx,
                                           std::string_view diag) noexcept;

    // 调试 / 测试用 — 当前 last_hash (chain head). 不暴露写入接口.
    [[nodiscard]] std::array<std::uint8_t, kHashBytes> last_hash() const noexcept {
        return last_hash_;
    }
    [[nodiscard]] std::uint64_t emitted_count() const noexcept {
        return seq_counter_.load(std::memory_order_acquire);
    }

    AuditEmitter(const AuditEmitter&)            = delete;
    AuditEmitter& operator=(const AuditEmitter&) = delete;

 private:
    // build_record: 填 record + 4 ts 透传 + market/strategy 拷贝
    [[nodiscard]] AuditRecord build_record(const RiskDecisionInput& in,
                                            AuditEventType type_override) const noexcept;

    // hash chain stub (Sprint-3 老孙真 BLAKE3):
    //   payload_hash = stub_digest(seq, type)
    //   current_hash = stub_chain(prev_hash, payload_hash)
    void apply_hash_chain(AuditRecord& rec, std::uint64_t seq) noexcept;

    // 4 ts 入口校验 (R-20) — emit_*() 入口必跑, 比 framework PIT 早一步 (caller 错配能早返).
    [[nodiscard]] bool ts_chain_ok(const RiskDecisionInput& in) const noexcept;

    // 写 WAL — caller 已确保 PIT OK, hash chain 已填.
    [[nodiscard]] ResultT write(const AuditRecord& rec) noexcept;

    WriterT* writer_{nullptr};
    std::atomic<std::uint64_t> seq_counter_{0};
    std::array<std::uint8_t, kHashBytes> last_hash_{};  // chain 起点 = 全 0
};

}  // namespace stcpp::observability
