// stcpp/observability/audit_emitter.hpp — AuditEmitter v1.2 (W7 Wave 33)
//
// 落:
//   laotang-audit-schema-v1.1.md §5 (emit_* API + hash chain owner)
//   laowang-wal-framework-cpp-interface-v1.md §8 (WalWriter<R>)
//   laohan-rm-v0.3.1 §6 (RiskDecision 联动)
//   老周 Smell #A (W6 Wave 29): 5 上游各自独立 emitter instance → AuditEmitterPool
//   老韩 Smell #2 (W6 Wave 29): BLAKE3_REAL build-time enforce, stub 禁进 live
//
// v1.1 变更 (W6 Wave 29):
//   1. 引入 blake3_hash.hpp — apply_hash_chain 切真 BLAKE3 (BLAKE3_REAL)
//   2. hash_chain_verify() — M1-A06 acceptance gate 链式完整性校验
//   3. AuditEmitterPool — 5 上游各自独立 emitter + 共享全局 hash chain
//   4. ABI lock: RiskDecisionInput + emit_* 接口不变 (5 上游无缝切换)
//
// v1.2 变更 (W7 Wave 33):
//   5. 删 friend class AuditEmitterPool (老高 H-07 + 老周 C-06 review ack)
//   6. 新增 emit_with_injected_chain() public API — Pool 注入 chain 参数后调用
//      (替代 friend 访问 private build_record / write 路径)
//
// 红线:
//   R-1   只一处 emit 路径 (本类), 全仓 grep audit_event 唯此一处
//   R-7   BLAKE3_REAL live/paper 都启用 (M1-A06 前置 — 老韩 Smell #2)
//   R-11  pool 5 emitter 按 origin 路由不同 WAL kind 路径 (pool 注入 writer 时 kind 已锁)
//   R-20  4 ts 透传; PIT 失败 → WalError::PitViolation, emit_recon_drift 兜底
//
// 不耻下问:
//   BLAKE3 官方 test vector @老孙
//   SPSC WALQueue 接入 @小石 W6 v0.1 framework
//   CI grep BLAKE3_STUB 拦截 @老高 PR v1.2

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>

#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/observability/blake3_hash.hpp"
#include "stcpp/risk/reject_enum.hpp"

namespace stcpp::observability {

// ---------- RiskDecisionInput (ABI locked W6) --------------------------------

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

    AuditEventType                       event_type{AuditEventType::OrderApproved};
    stcpp::risk::RejectCode              reject_code{stcpp::risk::RejectCode::INTERNAL_ERROR};
    stcpp::risk::InvalidIntentSubReason  sub_reason{stcpp::risk::InvalidIntentSubReason::NONE};
};

// ---------- AuditEmitter (单 instance, pool 的原子单元) ----------------------
//
// W6 变更: apply_hash_chain 内部切真 BLAKE3 (由 blake3_hash.hpp 提供).
// 单独使用时 chain 起点 = 全 0; pool 场景由 set_chain_state() 注入外部 chain.

class AuditEmitter {
 public:
    using WriterT = stcpp::infra::wal::WalWriter<AuditRecord>;
    using ResultT = stcpp::infra::wal::WalResult<std::uint64_t>;
    using Hash256 = Blake3Hasher::Hash256;

    explicit AuditEmitter(WriterT* writer) noexcept : writer_(writer) {}

    // --- emit 接口 (ABI locked, 5 上游不变) ---

    [[nodiscard]] ResultT emit_decision(const RiskDecisionInput& in) noexcept;

    [[nodiscard]] ResultT emit_safe_mode_enter(const RiskDecisionInput& ctx,
                                               std::string_view reason) noexcept;
    [[nodiscard]] ResultT emit_safe_mode_exit(const RiskDecisionInput& ctx) noexcept;
    [[nodiscard]] ResultT emit_state_transition(const RiskDecisionInput& ctx,
                                                std::string_view from_to) noexcept;
    [[nodiscard]] ResultT emit_unlock(const RiskDecisionInput& ctx,
                                      std::string_view operator_id) noexcept;
    [[nodiscard]] ResultT emit_strategy_decayed(const RiskDecisionInput& ctx) noexcept;
    [[nodiscard]] ResultT emit_recon_drift(const RiskDecisionInput& ctx,
                                           std::string_view diag) noexcept;

    // --- hash chain verify (M1-A06) ---
    //
    // 在本 emitter 范围内验证 [start_seq, end_seq] 的 chain 完整性.
    // W6: 基于 chain_snapshots_ 内存快照; Sprint-3 接 WAL replay.
    // 返回 true = chain 连续; false = 断裂 (seq gap / hash mismatch).
    [[nodiscard]] bool hash_chain_verify(std::uint64_t start_seq,
                                         std::uint64_t end_seq) const noexcept;

    // --- 观测 ---

    [[nodiscard]] Hash256 last_hash() const noexcept { return last_hash_; }
    [[nodiscard]] std::uint64_t emitted_count() const noexcept {
        return seq_counter_.load(std::memory_order_acquire);
    }

    // pool 注入: 在 pool 分配全局 seq + 更新共享 chain 后, 将外部 prev_hash + seq 注入.
    // 调用方 (AuditEmitterPool) 持有 chain_mutex_ 期间调用, 保证原子性.
    void inject_chain_state(const Hash256& prev_hash, std::uint64_t seq) noexcept {
        last_hash_ = prev_hash;
        seq_counter_.store(seq, std::memory_order_release);
    }

    // Pool 专用: 注入 chain 状态后直接 build + 写 WAL (锁外调用).
    // prev_hash / seq 由 AuditEmitterPool 在 chain_mutex_ 锁内已算好后传入.
    // 单独 emit 不走此 API; 只有 AuditEmitterPool 调用.
    [[nodiscard]] ResultT emit_with_injected_chain(
        const RiskDecisionInput& in,
        AuditEventType type_override,
        const Hash256& prev_hash,
        const Hash256& payload_hash,
        const Hash256& current_hash,
        std::uint64_t seq) noexcept;

    AuditEmitter(const AuditEmitter&)            = delete;
    AuditEmitter& operator=(const AuditEmitter&) = delete;

 private:
    [[nodiscard]] AuditRecord build_record(const RiskDecisionInput& in,
                                            AuditEventType type_override) const noexcept;

    // W6 切真 BLAKE3: payload_hash = Blake3(seq||type||ts), current = Blake3(prev||payload)
    void apply_hash_chain(AuditRecord& rec, std::uint64_t seq) noexcept;

    [[nodiscard]] bool ts_chain_ok(const RiskDecisionInput& in) const noexcept;
    [[nodiscard]] ResultT write(const AuditRecord& rec) noexcept;

    WriterT* writer_{nullptr};
    std::atomic<std::uint64_t> seq_counter_{0};
    Hash256 last_hash_{};   // chain 起点 = 全 0

    // hash_chain_verify 快照: 按 seq 顺序记录 (seq → current_hash)
    // 容量限制 = kVerifySnapshotCap; 满后最旧条目被覆盖 (ring buffer 模式)
    static constexpr std::size_t kVerifySnapshotCap = 1024;
    struct ChainSnap {
        std::uint64_t seq{0};
        Hash256       current_hash{};
    };
    // 简单 vector (测试 + M1 gate 场景, 非 hot path)
    // Sprint-3 接 WAL replay 时此内存结构废弃
    mutable std::array<ChainSnap, kVerifySnapshotCap> snapshots_{};
    std::uint64_t snap_count_{0};   // 已记录快照数 (≤ kVerifySnapshotCap)
};

// ---------- AuditOrigin (5 上游来源标识) ------------------------------------

enum class AuditOrigin : std::uint8_t {
    Risk      = 0,   // RiskGateway → risk_audit.wal / paper_audit.wal
    Signer    = 1,   // PaperSigner → paper_audit.wal
    Ml        = 2,   // MLHook      → shadow_audit.wal
    Stats     = 3,   // GateEvaluator → paper_audit.wal
    Strategy  = 4,   // SignalEngine   → paper_audit.wal
};

inline constexpr std::size_t kAuditOriginCount = 5;

// ---------- AuditEmitterPool — 5 上游独立 emitter, 共享全局 hash chain --------
//
// 老周 Smell #A 解法: 5 上游各自独立 emitter instance (独立 writer, 独立 ring).
// 但 hash chain 全局唯一 — 按全局 global_seq_ 顺序串联, 不分 origin.
//
// chain 串联时序保证:
//   chain_mutex_ 极短锁区间 (仅 seq alloc + BLAKE3 compute + chain update, < 1us)
//   WAL write 在锁外 (writer->Append 可能 > 1us, 不阻塞其他 origin)
//   顺序性: emit 入口 chain_mutex_ 保证 seq n 的 chain state 在 seq n+1 之前更新

class AuditEmitterPool {
 public:
    using WriterT = stcpp::infra::wal::WalWriter<AuditRecord>;
    using ResultT = stcpp::infra::wal::WalResult<std::uint64_t>;
    using Hash256 = Blake3Hasher::Hash256;

    // 构造: 5 个 writer, 一一对应 5 个 origin (可 nullptr — 该 origin 返 WalError::Io)
    // R-11: 调用方负责保证 writer[Risk] kind 与 build mode 一致
    AuditEmitterPool(WriterT* risk_writer,
                     WriterT* signer_writer,
                     WriterT* ml_writer,
                     WriterT* stats_writer,
                     WriterT* strategy_writer) noexcept;

    // pool 主 emit (代理到对应 origin 的 emitter, 保证全局 chain 串联)
    [[nodiscard]] ResultT emit(AuditOrigin origin,
                               const RiskDecisionInput& in) noexcept;
    [[nodiscard]] ResultT emit_safe_mode_enter(AuditOrigin origin,
                                               const RiskDecisionInput& ctx,
                                               std::string_view reason) noexcept;
    [[nodiscard]] ResultT emit_safe_mode_exit(AuditOrigin origin,
                                              const RiskDecisionInput& ctx) noexcept;
    [[nodiscard]] ResultT emit_state_transition(AuditOrigin origin,
                                                const RiskDecisionInput& ctx,
                                                std::string_view from_to) noexcept;
    [[nodiscard]] ResultT emit_unlock(AuditOrigin origin,
                                      const RiskDecisionInput& ctx,
                                      std::string_view operator_id) noexcept;
    [[nodiscard]] ResultT emit_strategy_decayed(AuditOrigin origin,
                                                const RiskDecisionInput& ctx) noexcept;
    [[nodiscard]] ResultT emit_recon_drift(AuditOrigin origin,
                                           const RiskDecisionInput& ctx,
                                           std::string_view diag) noexcept;

    // 全局 chain 完整性验证 (M1-A06)
    [[nodiscard]] bool hash_chain_verify_global(std::uint64_t start_seq,
                                                std::uint64_t end_seq) const noexcept;

    // 观测
    [[nodiscard]] Hash256 global_last_hash() const noexcept;
    [[nodiscard]] std::uint64_t global_seq() const noexcept {
        return global_seq_.load(std::memory_order_acquire);
    }

    // 获取单个 origin 的 emitter 指针 (5 上游依赖注入用, ABI lock)
    [[nodiscard]] AuditEmitter* get_emitter(AuditOrigin origin) noexcept;

    AuditEmitterPool(const AuditEmitterPool&)            = delete;
    AuditEmitterPool& operator=(const AuditEmitterPool&) = delete;

 private:
    // 全局 chain state — 跨 5 origin 串联
    std::atomic<std::uint64_t> global_seq_{0};
    mutable std::mutex         chain_mutex_;
    Hash256                    shared_last_hash_{};

    // 5 个独立 emitter (存储在 unique_ptr, 因 AuditEmitter 禁 copy)
    std::unique_ptr<AuditEmitter> emitters_[kAuditOriginCount];

    // 全局 chain 快照 (verify_global 用, 与各 emitter 快照并存)
    static constexpr std::size_t kGlobalSnapCap = 4096;
    struct GlobalSnap {
        AuditOrigin origin{AuditOrigin::Risk};
        std::uint64_t seq{0};
        Blake3Hasher::Hash256 current_hash{};
    };
    std::array<GlobalSnap, kGlobalSnapCap> global_snaps_{};
    std::uint64_t global_snap_count_{0};   // 总计 (mod kGlobalSnapCap = ring 位置)

    // 内部: 在 chain_mutex_ 锁内完成 seq 分配 + chain 计算 + 快照记录
    // 返回 { seq, payload_hash, current_hash, prev_hash }
    struct ChainStep {
        std::uint64_t seq{0};
        Hash256 prev_hash{};
        Hash256 payload_hash{};
        Hash256 current_hash{};
    };
    [[nodiscard]] ChainStep alloc_chain_step_locked(
        const RiskDecisionInput& in, AuditOrigin origin) noexcept;
};

}  // namespace stcpp::observability
