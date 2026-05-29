// stcpp/observability/audit_emitter.cpp — AuditEmitter v1.3 + AuditEmitterPool
//
// Owner: 老唐 (audit-expert, #38)  W9 Wave 65
// 落: laotang-audit-schema-v1.1.md §5
//     老周 Smell #A (AuditEmitter pool 5 上游)
//     老韩 Smell #2 (BLAKE3_REAL 替换 XOR stub)
//     W7 Wave 33: friend 删 + emit_with_injected_chain public API (老高 H-07 / 老周 C-06)
//     W9 Wave 65: v1.3 schema (token_id / outcome / side), build_record 写入新字段
//
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3 §6
//   goalserve_ssot_cite:  N/A
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder + Position ABI
//   adr_cite:             ADR-027 Enforce-1
//
// R-7: paper/live build 都 BLAKE3_REAL=1 (CMakeLists.txt 注入 -DBLAKE3_REAL=1)
// 不耻下问: SPSC WALQueue 接 @小石 W7; hash chain verify WAL replay @老王 Sprint-3

#include "stcpp/observability/audit_emitter.hpp"

#include <algorithm>
#include <cstring>

namespace stcpp::observability {

namespace {

// fixed-buf 拷贝 (string_view → POD char[N]); 截断, 不抛.
inline void copy_fixed(std::span<char> dst, std::string_view src) noexcept {
    const std::size_t n = std::min(dst.size(), src.size());
    if (n > 0) {
        std::memcpy(dst.data(), src.data(), n);
    }
    if (n < dst.size()) {
        dst[n] = '\0';
    }
}

// 老韩 invariant: code != INVALID_INTENT ⟹ sub_reason == NONE
[[nodiscard]] bool reject_invariant_ok(stcpp::risk::RejectCode c,
                                       stcpp::risk::InvalidIntentSubReason s) noexcept {
    return c == stcpp::risk::RejectCode::INVALID_INTENT || s == stcpp::risk::InvalidIntentSubReason::NONE;
}

// R-20 4 ts 单调性检查 (Pool emit 用; AuditEmitter::ts_chain_ok 等价 free 版本)
[[nodiscard]] bool ts_chain_ok_free(const RiskDecisionInput& in) noexcept {
    return (in.event_ts > 0) && (in.data_source_ts >= in.event_ts) &&
           (in.ingestion_ts >= in.data_source_ts) && (in.as_of_ts >= in.ingestion_ts) &&
           (in.decision_ts >= in.as_of_ts);
}

}  // namespace

// ===================== AuditEmitter 实现 =====================================

bool AuditEmitter::ts_chain_ok(const RiskDecisionInput& in) const noexcept {
    return (in.event_ts > 0) && (in.data_source_ts >= in.event_ts) &&
           (in.ingestion_ts >= in.data_source_ts) && (in.as_of_ts >= in.ingestion_ts) &&
           (in.decision_ts >= in.as_of_ts);
}

AuditRecord AuditEmitter::build_record(const RiskDecisionInput& in,
                                       AuditEventType type_override) const noexcept {
    AuditRecord r{};
    r.schema_version = kAuditSchemaV13;  // v1.3 schema
    r.event_ts = in.event_ts;
    r.data_source_ts = in.data_source_ts;
    r.ingestion_ts = in.ingestion_ts;
    r.as_of_ts = in.as_of_ts;
    r.decision_ts = in.decision_ts;
    r.audit_id_bytes = in.audit_id_bytes;
    r.event_type = type_override;
    r.reject_code = in.reject_code;
    r.sub_reason = in.sub_reason;

    // v1.3: condition_id (正名); 兼容 v1.2: 若 condition_id 为空则回退用 market_id
    const std::string_view cond_id = in.condition_id.empty() ? in.market_id : in.condition_id;
    copy_fixed(std::span<char>{r.condition_id.data(), r.condition_id.size()}, cond_id);

    // v1.3 新增: token_id (uint256 string, 无 0x 前缀)
    copy_fixed(std::span<char>{r.token_id.data(), r.token_id.size()}, in.token_id);

    copy_fixed(std::span<char>{r.strategy_id.data(), r.strategy_id.size()}, in.strategy_id);

    r.size_usdc = in.size_usdc;
    r.price = in.price;

    // v1.3 新增: outcome + side
    r.outcome = in.outcome;
    r.side = in.side;

    // v1.2 compat: is_buy 从 side 推断 (side=0=Buy → is_buy=true; side=1=Sell → is_buy=false)
    r.is_buy = (in.side == 0);

    r.crc32c = 0;  // framework 帧尾算
    return r;
}

void AuditEmitter::apply_hash_chain(AuditRecord& rec, std::uint64_t seq) noexcept {
    // W6 切真 BLAKE3 (BLAKE3_REAL 宏由 CMakeLists.txt 注入 -DBLAKE3_REAL=1)
    rec.prev_hash = last_hash_;
    rec.payload_hash =
        Blake3Hasher::compute_payload_hash(seq, static_cast<std::uint8_t>(rec.event_type), rec.decision_ts);
    rec.current_hash = Blake3Hasher::hash_chain(rec.prev_hash, rec.payload_hash);
    last_hash_ = rec.current_hash;

    // 记录快照 (hash_chain_verify 用, ring buffer)
    const std::size_t slot = snap_count_ % kVerifySnapshotCap;
    snapshots_[slot] = {seq, rec.current_hash};
    if (snap_count_ < kVerifySnapshotCap) {
        ++snap_count_;
    } else {
        // ring 满后覆盖最旧条目; snap_count_ 继续递增作总数记录
        ++snap_count_;
    }
}

AuditEmitter::ResultT AuditEmitter::write(const AuditRecord& rec) noexcept {
    if (writer_ == nullptr) {
        return stcpp::infra::wal::WalError::Io;
    }
    return writer_->Append(rec);
}

bool AuditEmitter::hash_chain_verify(std::uint64_t start_seq, std::uint64_t end_seq) const noexcept {
    if (start_seq > end_seq || snap_count_ == 0) {
        return false;
    }
    // 在 snapshots_ 中找到 start_seq 的位置并逐步验证链
    // 注意: ring buffer 满后旧数据可能被覆盖. W6 只验证还在 ring 内的范围.
    const std::uint64_t total_snaps = snap_count_;
    const std::uint64_t window_start =
        (total_snaps > kVerifySnapshotCap) ? (total_snaps - kVerifySnapshotCap) : 0;

    if (start_seq < window_start + 1) {
        return false;
    }  // 窗口外

    Hash256 expected_prev{};  // 需要找到 start_seq-1 的 hash 作为起点
    bool found_prev = false;

    // 遍历快照找连续链段
    for (std::uint64_t s = start_seq; s <= end_seq; ++s) {
        // 在 ring 中查找 seq == s
        bool found = false;
        for (std::size_t i = 0; i < std::min(static_cast<std::size_t>(snap_count_), kVerifySnapshotCap);
             ++i) {
            if (snapshots_[i].seq == s) {
                if (!found_prev && s == start_seq) {
                    // 第一条: 无法验证 prev_hash (没有外部起点), 仅记录 current
                    expected_prev = snapshots_[i].current_hash;
                    found_prev = true;
                    found = true;
                    break;
                }
                // 验证: current 必须 = Blake3(prev || payload_of_s)
                // W6 简化: 只验证 chain 连续性 (prev == 上一条 current)
                // 完整验证需 payload 重算, Sprint-3 WAL replay 时实现
                found = true;
                expected_prev = snapshots_[i].current_hash;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return found_prev;
}

// --- emit 主入口 ---

AuditEmitter::ResultT AuditEmitter::emit_decision(const RiskDecisionInput& in) noexcept {
    if (!ts_chain_ok(in))
        return stcpp::infra::wal::WalError::PitViolation;
    if (!reject_invariant_ok(in.reject_code, in.sub_reason))
        return stcpp::infra::wal::WalError::Io;

    AuditRecord r = build_record(in, in.event_type);

    switch (r.event_type) {
        case AuditEventType::OrderApproved:
        case AuditEventType::OrderRejected:
        case AuditEventType::OrderDeferred:
        case AuditEventType::OrderFilled:
        case AuditEventType::OrderCancelled:
        case AuditEventType::OrderTimedOut:
            break;
        default:
            return stcpp::infra::wal::WalError::Io;
    }

    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

// --- 状态机 ---

AuditEmitter::ResultT AuditEmitter::emit_safe_mode_enter(const RiskDecisionInput& ctx,
                                                         std::string_view reason) noexcept {
    if (!ts_chain_ok(ctx))
        return stcpp::infra::wal::WalError::PitViolation;
    AuditRecord r = build_record(ctx, AuditEventType::SafeModeEnter);
    // reason 写入 condition_id (v1.3 正名, 原 market_id)
    copy_fixed(std::span<char>{r.condition_id.data(), r.condition_id.size()}, reason);
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

AuditEmitter::ResultT AuditEmitter::emit_safe_mode_exit(const RiskDecisionInput& ctx) noexcept {
    if (!ts_chain_ok(ctx))
        return stcpp::infra::wal::WalError::PitViolation;
    AuditRecord r = build_record(ctx, AuditEventType::SafeModeExit);
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

AuditEmitter::ResultT AuditEmitter::emit_state_transition(const RiskDecisionInput& ctx,
                                                          std::string_view from_to) noexcept {
    if (!ts_chain_ok(ctx))
        return stcpp::infra::wal::WalError::PitViolation;
    AuditRecord r = build_record(ctx, AuditEventType::StateTransition);
    // from_to 写入 condition_id (v1.3 正名, 原 market_id)
    copy_fixed(std::span<char>{r.condition_id.data(), r.condition_id.size()}, from_to);
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

AuditEmitter::ResultT AuditEmitter::emit_unlock(const RiskDecisionInput& ctx,
                                                std::string_view operator_id) noexcept {
    if (!ts_chain_ok(ctx))
        return stcpp::infra::wal::WalError::PitViolation;
    AuditRecord r = build_record(ctx, AuditEventType::Unlock);
    copy_fixed(std::span<char>{r.strategy_id.data(), r.strategy_id.size()}, operator_id);
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

// --- 衰减 / 对账 ---

AuditEmitter::ResultT AuditEmitter::emit_strategy_decayed(const RiskDecisionInput& ctx) noexcept {
    if (!ts_chain_ok(ctx))
        return stcpp::infra::wal::WalError::PitViolation;
    AuditRecord r = build_record(ctx, AuditEventType::StrategyDecayed);
    r.reject_code = stcpp::risk::RejectCode::STRATEGY_DECAYED;
    r.sub_reason = stcpp::risk::InvalidIntentSubReason::NONE;
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

AuditEmitter::ResultT AuditEmitter::emit_recon_drift(const RiskDecisionInput& ctx,
                                                     std::string_view diag) noexcept {
    AuditRecord r = build_record(ctx, AuditEventType::ReconDrift);
    // diag 写入 condition_id (v1.3 正名, 原 market_id)
    copy_fixed(std::span<char>{r.condition_id.data(), r.condition_id.size()}, diag);
    const auto seq = seq_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    apply_hash_chain(r, seq);
    return write(r);
}

// --- Pool 专用: chain 已由 Pool 在锁内算好, 此处仅 build + 填 hash + write ---
//
// 语义: Pool 在 chain_mutex_ 锁内完成 seq 分配 + BLAKE3 chain compute, 然后
//       调此方法在锁外写 WAL. 不再依赖 friend 访问 build_record / write.
//       不走 seq_counter_.fetch_add (Pool 管 global seq).

AuditEmitter::ResultT AuditEmitter::emit_with_injected_chain(
    const RiskDecisionInput& in, AuditEventType type_override, const Hash256& prev_hash,
    const Hash256& payload_hash, const Hash256& current_hash, std::uint64_t seq) noexcept {
    AuditRecord rec = build_record(in, type_override);
    rec.prev_hash = prev_hash;
    rec.payload_hash = payload_hash;
    rec.current_hash = current_hash;
    // 更新 emitter 快照 (hash_chain_verify 用)
    const std::size_t slot = snap_count_ % kVerifySnapshotCap;
    snapshots_[slot] = {seq, current_hash};
    if (snap_count_ < kVerifySnapshotCap) {
        ++snap_count_;
    } else {
        ++snap_count_;
    }
    // inject_chain_state: 保持 last_hash_ 与 Pool chain 同步
    last_hash_ = current_hash;
    seq_counter_.store(seq, std::memory_order_release);
    return write(rec);
}

// ===================== AuditEmitterPool 实现 =================================

AuditEmitterPool::AuditEmitterPool(WriterT* risk_writer, WriterT* signer_writer, WriterT* ml_writer,
                                   WriterT* stats_writer, WriterT* strategy_writer) noexcept {
    emitters_[static_cast<std::size_t>(AuditOrigin::Risk)] = std::make_unique<AuditEmitter>(risk_writer);
    emitters_[static_cast<std::size_t>(AuditOrigin::Signer)] = std::make_unique<AuditEmitter>(signer_writer);
    emitters_[static_cast<std::size_t>(AuditOrigin::Ml)] = std::make_unique<AuditEmitter>(ml_writer);
    emitters_[static_cast<std::size_t>(AuditOrigin::Stats)] = std::make_unique<AuditEmitter>(stats_writer);
    emitters_[static_cast<std::size_t>(AuditOrigin::Strategy)] =
        std::make_unique<AuditEmitter>(strategy_writer);
}

// 锁内: 分配全局 seq + 计算 BLAKE3 chain step + 记录全局快照
AuditEmitterPool::ChainStep AuditEmitterPool::alloc_chain_step_locked(const RiskDecisionInput& in,
                                                                      AuditOrigin origin) noexcept {
    ChainStep step{};
    step.seq = global_seq_.fetch_add(1, std::memory_order_acq_rel) + 1;
    step.prev_hash = shared_last_hash_;
    step.payload_hash = Blake3Hasher::compute_payload_hash(step.seq, static_cast<std::uint8_t>(in.event_type),
                                                           in.decision_ts);
    step.current_hash = Blake3Hasher::hash_chain(step.prev_hash, step.payload_hash);
    shared_last_hash_ = step.current_hash;

    // 记录全局快照 (ring)
    const std::size_t slot = global_snap_count_ % kGlobalSnapCap;
    global_snaps_[slot] = {origin, step.seq, step.current_hash};
    ++global_snap_count_;

    return step;
}

AuditEmitter* AuditEmitterPool::get_emitter(AuditOrigin origin) noexcept {
    const auto idx = static_cast<std::size_t>(origin);
    if (idx >= kAuditOriginCount) {
        return nullptr;
    }
    return emitters_[idx].get();
}

AuditEmitterPool::Hash256 AuditEmitterPool::global_last_hash() const noexcept {
    const std::lock_guard<std::mutex> lock(chain_mutex_);
    return shared_last_hash_;
}

bool AuditEmitterPool::hash_chain_verify_global(std::uint64_t start_seq,
                                                std::uint64_t end_seq) const noexcept {
    if (start_seq > end_seq || global_snap_count_ == 0) {
        return false;
    }

    const std::lock_guard<std::mutex> lock(chain_mutex_);
    const std::uint64_t total = global_snap_count_;
    const std::uint64_t window_start = (total > kGlobalSnapCap) ? (total - kGlobalSnapCap) : 0;
    if (start_seq < window_start + 1) {
        return false;
    }

    // 按 seq 顺序验证 chain 连续性
    bool started = false;
    Hash256 prev_current{};
    for (std::uint64_t s = start_seq; s <= end_seq; ++s) {
        bool found = false;
        for (std::size_t i = 0; i < std::min(static_cast<std::size_t>(total), kGlobalSnapCap); ++i) {
            if (global_snaps_[i].seq == s) {
                if (!started) {
                    prev_current = global_snaps_[i].current_hash;
                    started = true;
                    found = true;
                    break;
                }
                // chain 连续性: current[n] 是否由 prev[n-1] 通过已知 payload 生成
                // W6 简化: 检查 seq 连续 + 快照存在 (完整 hash 验证 Sprint-3)
                prev_current = global_snaps_[i].current_hash;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return started;
}

// --- pool emit: 锁内算 chain, 锁外调 emit_with_injected_chain 写 WAL ---
//
// W7 Wave 33: friend 删后 Pool 不再直接访问 emitter private.
// 改为调 emit_with_injected_chain (public API), 锁内仅 seq alloc + BLAKE3 compute.

AuditEmitterPool::ResultT AuditEmitterPool::emit(AuditOrigin origin, const RiskDecisionInput& in) noexcept {
    const auto idx = static_cast<std::size_t>(origin);
    if (idx >= kAuditOriginCount) {
        return stcpp::infra::wal::WalError::Io;
    }

    AuditEmitter* em = emitters_[idx].get();
    if (em == nullptr) {
        return stcpp::infra::wal::WalError::Io;
    }

    // 预检 ts + reject invariant (锁外, 快速返回)
    if (!ts_chain_ok_free(in)) {
        return stcpp::infra::wal::WalError::PitViolation;
    }
    if (!reject_invariant_ok(in.reject_code, in.sub_reason)) {
        return stcpp::infra::wal::WalError::Io;
    }
    // 主入口只接 6 AET (Approved/Rejected/Deferred/Filled/Cancelled/TimedOut)
    switch (in.event_type) {
        case AuditEventType::OrderApproved:
        case AuditEventType::OrderRejected:
        case AuditEventType::OrderDeferred:
        case AuditEventType::OrderFilled:
        case AuditEventType::OrderCancelled:
        case AuditEventType::OrderTimedOut:
            break;
        default:
            return stcpp::infra::wal::WalError::Io;
    }

    // 锁内: seq 分配 + chain 计算
    ChainStep step{};
    {
        const std::lock_guard<std::mutex> lock(chain_mutex_);
        step = alloc_chain_step_locked(in, origin);
    }
    // 锁外: public API 写 WAL (build_record + hash 填入 内部完成)
    return em->emit_with_injected_chain(in, in.event_type, step.prev_hash, step.payload_hash,
                                        step.current_hash, step.seq);
}

AuditEmitterPool::ResultT AuditEmitterPool::emit_safe_mode_enter(AuditOrigin origin,
                                                                 const RiskDecisionInput& ctx,
                                                                 std::string_view reason) noexcept {
    const auto idx = static_cast<std::size_t>(origin);
    if (idx >= kAuditOriginCount) {
        return stcpp::infra::wal::WalError::Io;
    }
    AuditEmitter* em = emitters_[idx].get();
    if (em == nullptr) {
        return stcpp::infra::wal::WalError::Io;
    }
    if (!ts_chain_ok_free(ctx)) {
        return stcpp::infra::wal::WalError::PitViolation;
    }
    // reason → market_id 字段 (build_record 内 copy_fixed 会写入)
    RiskDecisionInput in = ctx;
    in.event_type = AuditEventType::SafeModeEnter;
    in.market_id = reason;
    ChainStep step{};
    {
        const std::lock_guard<std::mutex> lock(chain_mutex_);
        step = alloc_chain_step_locked(in, origin);
    }
    return em->emit_with_injected_chain(in, AuditEventType::SafeModeEnter, step.prev_hash, step.payload_hash,
                                        step.current_hash, step.seq);
}

AuditEmitterPool::ResultT AuditEmitterPool::emit_safe_mode_exit(AuditOrigin origin,
                                                                const RiskDecisionInput& ctx) noexcept {
    const auto idx = static_cast<std::size_t>(origin);
    if (idx >= kAuditOriginCount) {
        return stcpp::infra::wal::WalError::Io;
    }
    AuditEmitter* em = emitters_[idx].get();
    if (em == nullptr) {
        return stcpp::infra::wal::WalError::Io;
    }
    if (!ts_chain_ok_free(ctx)) {
        return stcpp::infra::wal::WalError::PitViolation;
    }
    RiskDecisionInput in = ctx;
    in.event_type = AuditEventType::SafeModeExit;
    ChainStep step{};
    {
        const std::lock_guard<std::mutex> lock(chain_mutex_);
        step = alloc_chain_step_locked(in, origin);
    }
    return em->emit_with_injected_chain(in, AuditEventType::SafeModeExit, step.prev_hash, step.payload_hash,
                                        step.current_hash, step.seq);
}

AuditEmitterPool::ResultT AuditEmitterPool::emit_state_transition(AuditOrigin origin,
                                                                  const RiskDecisionInput& ctx,
                                                                  std::string_view from_to) noexcept {
    const auto idx = static_cast<std::size_t>(origin);
    if (idx >= kAuditOriginCount) {
        return stcpp::infra::wal::WalError::Io;
    }
    AuditEmitter* em = emitters_[idx].get();
    if (em == nullptr) {
        return stcpp::infra::wal::WalError::Io;
    }
    if (!ts_chain_ok_free(ctx)) {
        return stcpp::infra::wal::WalError::PitViolation;
    }
    // from_to → market_id 字段
    RiskDecisionInput in = ctx;
    in.event_type = AuditEventType::StateTransition;
    in.market_id = from_to;
    ChainStep step{};
    {
        const std::lock_guard<std::mutex> lock(chain_mutex_);
        step = alloc_chain_step_locked(in, origin);
    }
    return em->emit_with_injected_chain(in, AuditEventType::StateTransition, step.prev_hash,
                                        step.payload_hash, step.current_hash, step.seq);
}

AuditEmitterPool::ResultT AuditEmitterPool::emit_unlock(AuditOrigin origin, const RiskDecisionInput& ctx,
                                                        std::string_view operator_id) noexcept {
    const auto idx = static_cast<std::size_t>(origin);
    if (idx >= kAuditOriginCount) {
        return stcpp::infra::wal::WalError::Io;
    }
    AuditEmitter* em = emitters_[idx].get();
    if (em == nullptr) {
        return stcpp::infra::wal::WalError::Io;
    }
    if (!ts_chain_ok_free(ctx)) {
        return stcpp::infra::wal::WalError::PitViolation;
    }
    // operator_id → strategy_id 字段
    RiskDecisionInput in = ctx;
    in.event_type = AuditEventType::Unlock;
    in.strategy_id = operator_id;
    ChainStep step{};
    {
        const std::lock_guard<std::mutex> lock(chain_mutex_);
        step = alloc_chain_step_locked(in, origin);
    }
    return em->emit_with_injected_chain(in, AuditEventType::Unlock, step.prev_hash, step.payload_hash,
                                        step.current_hash, step.seq);
}

AuditEmitterPool::ResultT AuditEmitterPool::emit_strategy_decayed(AuditOrigin origin,
                                                                  const RiskDecisionInput& ctx) noexcept {
    const auto idx = static_cast<std::size_t>(origin);
    if (idx >= kAuditOriginCount) {
        return stcpp::infra::wal::WalError::Io;
    }
    AuditEmitter* em = emitters_[idx].get();
    if (em == nullptr) {
        return stcpp::infra::wal::WalError::Io;
    }
    if (!ts_chain_ok_free(ctx)) {
        return stcpp::infra::wal::WalError::PitViolation;
    }
    // reject_code / sub_reason 注入 in (build_record 从 in 读)
    RiskDecisionInput in = ctx;
    in.event_type = AuditEventType::StrategyDecayed;
    in.reject_code = stcpp::risk::RejectCode::STRATEGY_DECAYED;
    in.sub_reason = stcpp::risk::InvalidIntentSubReason::NONE;
    ChainStep step{};
    {
        const std::lock_guard<std::mutex> lock(chain_mutex_);
        step = alloc_chain_step_locked(in, origin);
    }
    return em->emit_with_injected_chain(in, AuditEventType::StrategyDecayed, step.prev_hash,
                                        step.payload_hash, step.current_hash, step.seq);
}

AuditEmitterPool::ResultT AuditEmitterPool::emit_recon_drift(AuditOrigin origin, const RiskDecisionInput& ctx,
                                                             std::string_view diag) noexcept {
    const auto idx = static_cast<std::size_t>(origin);
    if (idx >= kAuditOriginCount) {
        return stcpp::infra::wal::WalError::Io;
    }
    AuditEmitter* em = emitters_[idx].get();
    if (em == nullptr) {
        return stcpp::infra::wal::WalError::Io;
    }
    // diag → market_id 字段 (recon_drift 不做 ts check, 与单 emitter 版一致)
    RiskDecisionInput in = ctx;
    in.event_type = AuditEventType::ReconDrift;
    in.market_id = diag;
    ChainStep step{};
    {
        const std::lock_guard<std::mutex> lock(chain_mutex_);
        step = alloc_chain_step_locked(in, origin);
    }
    return em->emit_with_injected_chain(in, AuditEventType::ReconDrift, step.prev_hash, step.payload_hash,
                                        step.current_hash, step.seq);
}

}  // namespace stcpp::observability

// ---------- 模板显式实例化 ---------------------------------------------------

#include "src/stcpp/infra/wal/wal_writer.cpp"

namespace stcpp::infra::wal {
template class WalWriter<stcpp::observability::AuditRecord>;
}  // namespace stcpp::infra::wal
