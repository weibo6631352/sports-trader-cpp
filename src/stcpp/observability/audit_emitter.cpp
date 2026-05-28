// stcpp/observability/audit_emitter.cpp — AuditEmitter v0.1 实现 (W4 Wave 19)
// 落: laotang v1.1 §5 + 老王 WalWriter<AuditRecord>
// R-20 4 ts emit 前必校 (ts_chain_ok), framework PIT 再校一次, 双闭环.
// Sprint-3 切真 BLAKE3: 只动 stub_digest / stub_chain.

#include "stcpp/observability/audit_emitter.hpp"

#include <algorithm>
#include <cstring>

namespace stcpp::observability {

namespace {

// stub BLAKE3 (Sprint-3 老孙真算). digest = seq(8) | AET(1) | decision_ts(8) | 0.
[[nodiscard]] std::array<std::uint8_t, kHashBytes>
stub_digest(std::uint64_t seq, AuditEventType type, std::int64_t decision_ts) noexcept {
    std::array<std::uint8_t, kHashBytes> out{};
    std::memcpy(out.data(),     &seq,         sizeof(seq));
    out[8] = static_cast<std::uint8_t>(type);
    std::memcpy(out.data() + 9, &decision_ts, sizeof(decision_ts));
    return out;
}

// stub chain: current = prev XOR payload (Sprint-3 切 BLAKE3(prev||payload)).
[[nodiscard]] std::array<std::uint8_t, kHashBytes>
stub_chain(const std::array<std::uint8_t, kHashBytes>& prev,
           const std::array<std::uint8_t, kHashBytes>& payload) noexcept {
    std::array<std::uint8_t, kHashBytes> out{};
    for (std::size_t i = 0; i < kHashBytes; ++i) {
        out[i] = static_cast<std::uint8_t>(prev[i] ^ payload[i]);
    }
    return out;
}

// fixed-buf 拷贝 (string_view → POD char[N]); 截断, 不抛.
inline void copy_fixed(std::span<char> dst, std::string_view src) noexcept {
    const std::size_t n = std::min(dst.size(), src.size());
    if (n > 0) std::memcpy(dst.data(), src.data(), n);
    if (n < dst.size()) dst[n] = '\0';
}

// 老韩 invariant: code != INVALID_INTENT ⟹ sub_reason == NONE
[[nodiscard]] bool reject_invariant_ok(stcpp::risk::RejectCode c,
                                       stcpp::risk::InvalidIntentSubReason s) noexcept {
    return c == stcpp::risk::RejectCode::INVALID_INTENT
        || s == stcpp::risk::InvalidIntentSubReason::NONE;
}

}  // namespace

// ---------- 内部辅助 -----------------------------------------------------

bool AuditEmitter::ts_chain_ok(const RiskDecisionInput& in) const noexcept {
    // R-20: event ≤ data_source ≤ ingestion ≤ as_of. 5 条不等式 framework 也校,
    // 这里早返 (caller 自检). 再保 decision_ts ≥ as_of (RM 决策晚于 ingest).
    return (in.event_ts        >  0)
        && (in.data_source_ts >= in.event_ts)
        && (in.ingestion_ts   >= in.data_source_ts)
        && (in.as_of_ts       >= in.ingestion_ts)
        && (in.decision_ts    >= in.as_of_ts);
}

AuditRecord AuditEmitter::build_record(const RiskDecisionInput& in,
                                        AuditEventType type_override) const noexcept {
    AuditRecord r{};
    r.event_ts        = in.event_ts;
    r.data_source_ts  = in.data_source_ts;
    r.ingestion_ts    = in.ingestion_ts;
    r.as_of_ts        = in.as_of_ts;
    r.decision_ts     = in.decision_ts;
    r.audit_id_bytes  = in.audit_id_bytes;

    r.event_type      = type_override;
    r.reject_code     = in.reject_code;
    r.sub_reason      = in.sub_reason;

    copy_fixed(std::span<char>{r.market_id.data(),   r.market_id.size()},   in.market_id);
    copy_fixed(std::span<char>{r.strategy_id.data(), r.strategy_id.size()}, in.strategy_id);

    r.size_usdc = in.size_usdc;
    r.price     = in.price;
    r.is_buy    = in.is_buy;

    // crc32c stub — framework 帧尾还会算一份. 这里 payload 内嵌 sanity (Sprint-3 切真 CRC32C 表算).
    r.crc32c = 0;
    return r;
}

void AuditEmitter::apply_hash_chain(AuditRecord& rec, std::uint64_t seq) noexcept {
    rec.prev_hash    = last_hash_;
    rec.payload_hash = stub_digest(seq, rec.event_type, rec.decision_ts);
    rec.current_hash = stub_chain(rec.prev_hash, rec.payload_hash);
    last_hash_       = rec.current_hash;
}

AuditEmitter::ResultT AuditEmitter::write(const AuditRecord& rec) noexcept {
    if (writer_ == nullptr) {
        return stcpp::infra::wal::WalError::Io;
    }
    return writer_->Append(rec);
}

// ---------- 主入口 -------------------------------------------------------

AuditEmitter::ResultT AuditEmitter::emit_decision(const RiskDecisionInput& in) noexcept {
    if (!ts_chain_ok(in))                                return stcpp::infra::wal::WalError::PitViolation;
    if (!reject_invariant_ok(in.reject_code, in.sub_reason))
                                                          return stcpp::infra::wal::WalError::Io;

    AuditRecord r = build_record(in, in.event_type);

    // 主入口只接 6 AET (Approved/Rejected/Deferred/Filled/Cancelled/TimedOut).
    // 状态机 / 衰减 / 对账 不走这里 — 各自有 emit_*().
    switch (r.event_type) {
        case AuditEventType::OrderApproved:
        case AuditEventType::OrderRejected:
        case AuditEventType::OrderDeferred:
        case AuditEventType::OrderFilled:
        case AuditEventType::OrderCancelled:
        case AuditEventType::OrderTimedOut:
            break;
        default:
            // 误用 — 走专门入口
            return stcpp::infra::wal::WalError::Io;
    }

    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

// ---------- 状态机 -------------------------------------------------------

AuditEmitter::ResultT AuditEmitter::emit_safe_mode_enter(
    const RiskDecisionInput& ctx, std::string_view reason) noexcept {
    if (!ts_chain_ok(ctx)) return stcpp::infra::wal::WalError::PitViolation;
    AuditRecord r = build_record(ctx, AuditEventType::SafeModeEnter);
    copy_fixed(std::span<char>{r.market_id.data(), r.market_id.size()}, reason);  // 复用 market_id 字段载 note
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

AuditEmitter::ResultT AuditEmitter::emit_safe_mode_exit(const RiskDecisionInput& ctx) noexcept {
    if (!ts_chain_ok(ctx)) return stcpp::infra::wal::WalError::PitViolation;
    AuditRecord r = build_record(ctx, AuditEventType::SafeModeExit);
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

AuditEmitter::ResultT AuditEmitter::emit_state_transition(
    const RiskDecisionInput& ctx, std::string_view from_to) noexcept {
    if (!ts_chain_ok(ctx)) return stcpp::infra::wal::WalError::PitViolation;
    AuditRecord r = build_record(ctx, AuditEventType::StateTransition);
    copy_fixed(std::span<char>{r.market_id.data(), r.market_id.size()}, from_to);
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

AuditEmitter::ResultT AuditEmitter::emit_unlock(
    const RiskDecisionInput& ctx, std::string_view operator_id) noexcept {
    if (!ts_chain_ok(ctx)) return stcpp::infra::wal::WalError::PitViolation;
    AuditRecord r = build_record(ctx, AuditEventType::Unlock);
    copy_fixed(std::span<char>{r.strategy_id.data(), r.strategy_id.size()}, operator_id);
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

// ---------- 衰减 / 对账 --------------------------------------------------

AuditEmitter::ResultT AuditEmitter::emit_strategy_decayed(const RiskDecisionInput& ctx) noexcept {
    if (!ts_chain_ok(ctx)) return stcpp::infra::wal::WalError::PitViolation;
    AuditRecord r = build_record(ctx, AuditEventType::StrategyDecayed);
    // 老韩 #19: STRATEGY_DECAYED 关联 reject_code (caller 已填)
    r.reject_code = stcpp::risk::RejectCode::STRATEGY_DECAYED;
    r.sub_reason  = stcpp::risk::InvalidIntentSubReason::NONE;
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

AuditEmitter::ResultT AuditEmitter::emit_recon_drift(
    const RiskDecisionInput& ctx, std::string_view diag) noexcept {
    // 注意: recon_drift 本身可能因 PIT 失败触发 → 这里 *不* 拒 ts 不一致, 但落盘时
    // framework PIT 仍可能拒 (R-20 不可绕过). 真正 PIT 违规走专门兜底路径 (W5 接 RM 慢路径).
    AuditRecord r = build_record(ctx, AuditEventType::ReconDrift);
    copy_fixed(std::span<char>{r.market_id.data(), r.market_id.size()}, diag);
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

}  // namespace stcpp::observability

// ---------- 模板显式实例化 -----------------------------------------------
//
// 老唐 owner: WalWriter<AuditRecord>. framework skeleton 没在自己 .cpp 实例化
// (避免循环依赖), 由 record owner 在自己 module 实例化.

#include "src/stcpp/infra/wal/wal_writer.cpp"  // 取模板定义 (skeleton 单文件)

namespace stcpp::infra::wal {
template class WalWriter<stcpp::observability::AuditRecord>;
}  // namespace stcpp::infra::wal
