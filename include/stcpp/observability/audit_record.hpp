// stcpp/observability/audit_record.hpp — laotang audit envelope v0.1 (W4 Wave 19)
//
// 落:
//   laotang-audit-schema-v1.1.md §2.x (4 ts + 12 AET + reject + sub_reason + chain)
//   laowang-wal-framework-cpp-interface-v1.md §6 (WalRecord concept)
//   laohan-rm-v0.3.1 §5  (21 reject + INVALID_INTENT.sub_reason 字段化)
//   laosun-blake3-v5.1 §3 (Sprint-3 真上线, 此处 stub digest)
//
// 红线:
//   R-11 paper mode → WalKind::PaperAudit (/var/lib/stcpp/paper/), 同进程不出现 RiskAudit symbol
//        live  mode → WalKind::RiskAudit  (/var/lib/stcpp/audit/)
//        build-time switch (STCPP_EXEC_MODE), 不动态切.
//   R-20 4 ts (event / data_source / ingestion / as_of) 全程透传, emit 前必校 PIT
//        decision_ts_ns 单独存, 不参与 R-20 链 (它是 as_of 之后的本地决策时刻).
//
// AuditRecord 满足 infra::wal::WalRecord concept — 接老王 WalWriter<AuditRecord>.
//
// 不耻下问: BLAKE3 真算 @老孙 Sprint-3, 4 ts canon @老练, RM 决策语义 @老韩

#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/risk/reject_enum.hpp"

namespace stcpp::observability {

// ---------- 12 AuditEventType (老唐 schema v1.1 §3) ----------------------
//
// 1..6  RM 决策路径 (老韩 R-1)
// 7..9  状态机切换 (HALT / DRAIN / SAFE_MODE / UNLOCK)
// 10    策略衰减 kill switch (老韩 v0.3 #19 STRATEGY_DECAYED, OQ-D13 Bayesian)
// 11    对账漂移 (老周 position WAL vs RM ledger)
// 12    PIT 链失败 (R-20 慢路径诊断, 框架 PitViolation → audit)

enum class AuditEventType : std::uint8_t {
    Unknown            = 0,
    OrderApproved      = 1,
    OrderRejected      = 2,
    OrderDeferred      = 3,
    OrderFilled        = 4,
    OrderCancelled     = 5,
    OrderTimedOut      = 6,
    SafeModeEnter      = 7,
    SafeModeExit       = 8,
    StateTransition    = 9,    // HALT / DRAIN / 等 — payload.note 记 from→to
    StrategyDecayed    = 10,   // 老韩 #19
    Unlock             = 11,   // 人工解除 HALT/SAFE_MODE
    ReconDrift         = 12,   // 对账漂移 / PIT 违规 / framework 错误兜底
};

inline constexpr std::size_t kAuditEventTypeCount = 12;

[[nodiscard]] constexpr std::string_view ToString(AuditEventType t) noexcept {
    switch (t) {
        case AuditEventType::Unknown:         return "UNKNOWN";
        case AuditEventType::OrderApproved:   return "ORDER_APPROVED";
        case AuditEventType::OrderRejected:   return "ORDER_REJECTED";
        case AuditEventType::OrderDeferred:   return "ORDER_DEFERRED";
        case AuditEventType::OrderFilled:     return "ORDER_FILLED";
        case AuditEventType::OrderCancelled:  return "ORDER_CANCELLED";
        case AuditEventType::OrderTimedOut:   return "ORDER_TIMED_OUT";
        case AuditEventType::SafeModeEnter:   return "SAFE_MODE_ENTER";
        case AuditEventType::SafeModeExit:    return "SAFE_MODE_EXIT";
        case AuditEventType::StateTransition: return "STATE_TRANSITION";
        case AuditEventType::StrategyDecayed: return "STRATEGY_DECAYED";
        case AuditEventType::Unlock:          return "UNLOCK";
        case AuditEventType::ReconDrift:      return "RECON_DRIFT";
    }
    return "unknown";
}

// ---------- AuditRecord (POD payload, 满足 WalRecord concept) -------------
//
// Layout (logical, framework header 在外层, 这里只是 payload):
//   - 4 ts (R-20)
//   - decision_ts_ns (本地 RM 决策时刻, 在 as_of_ts 之后)
//   - audit_id (16B ULID, 与 framework header 同步, caller 填)
//   - AET / RejectCode / InvalidIntentSubReason
//   - market_id / strategy_id (固定 32B, 不存指针)
//   - prev_hash / payload_hash / current_hash (BLAKE3 32B, Sprint-3 真算, 此处 stub)
//   - crc32c (4B, framework 写帧时算; 这里仅 payload 内嵌 — framework 帧尾 CRC 独立)

inline constexpr std::size_t kHashBytes      = 32;   // BLAKE3-256
inline constexpr std::size_t kMarketIdMax    = 32;
inline constexpr std::size_t kStrategyIdMax  = 32;

struct AuditRecord {
    // ---- R-20 4 ts (与 framework header v2 offset 16/24/32/40 一致) ----
    std::int64_t event_ts        = 0;
    std::int64_t data_source_ts  = 0;
    std::int64_t ingestion_ts    = 0;
    std::int64_t as_of_ts        = 0;

    // 本地 RM 决策时刻 — > as_of_ts. 单独存, 不进 PIT 链 (R-20 只管 4 ts).
    std::int64_t decision_ts     = 0;

    // ULID — framework header.audit_id 同源 (caller emit_*() 填)
    std::array<std::uint8_t, 16> audit_id_bytes{};

    // 12 AET (老唐 schema v1.1 §3)
    AuditEventType event_type = AuditEventType::Unknown;

    // 21 reject + 8 sub_reason (老韩 v0.3.1)
    // event_type != OrderRejected ⟹ reject_code 视作 INTERNAL_ERROR (兜底 enum 默认)
    // reject_code != INVALID_INTENT ⟹ sub_reason == NONE  (老韩 invariant)
    stcpp::risk::RejectCode             reject_code{stcpp::risk::RejectCode::INTERNAL_ERROR};
    stcpp::risk::InvalidIntentSubReason sub_reason{stcpp::risk::InvalidIntentSubReason::NONE};

    // 业务键 (固定长度, 不存指针 — POD 落盘 + 不可篡改)
    std::array<char, kMarketIdMax>   market_id{};
    std::array<char, kStrategyIdMax> strategy_id{};

    std::int64_t size_usdc = 0;
    double       price     = 0.0;
    bool         is_buy    = true;

    // ---- BLAKE3 hash chain (老唐 v1.1 §4) ------------------------------
    // prev_hash         = 上一条 current_hash (chain 起点 = 全 0)
    // payload_hash      = BLAKE3(payload bytes ex hash region)        — Sprint-3 真算
    // current_hash      = BLAKE3(prev_hash || payload_hash)            — Sprint-3 真算
    // W4 stub: payload_hash = u64 counter + len, current = prev XOR payload (链式可校验)
    std::array<std::uint8_t, kHashBytes> prev_hash{};
    std::array<std::uint8_t, kHashBytes> payload_hash{};
    std::array<std::uint8_t, kHashBytes> current_hash{};

    // framework 帧尾还有 CRC32C (framework 算); 这里 payload 内嵌一份 sanity check.
    std::uint32_t crc32c = 0;

    // ---- WalRecord concept 适配 (4 ts getters + ulid + serialize) ----
    [[nodiscard]] std::int64_t event_ts_ns()       const noexcept { return event_ts; }
    [[nodiscard]] std::int64_t data_source_ts_ns() const noexcept { return data_source_ts; }
    [[nodiscard]] std::int64_t ingestion_ts_ns()   const noexcept { return ingestion_ts; }
    [[nodiscard]] std::int64_t as_of_ts_ns()       const noexcept { return as_of_ts; }
    [[nodiscard]] std::array<std::uint8_t, 16> audit_id() const noexcept { return audit_id_bytes; }

    // serialize_into: POD memcpy. W4 stub — 不做 endian / pack, 留 Sprint-3 切真编码.
    [[nodiscard]] std::size_t serialize_into(std::span<std::byte> out) const noexcept {
        const std::size_t n = sizeof(AuditRecord);
        if (out.size() < n) return 0;
        std::memcpy(out.data(), this, n);
        return n;
    }
    static constexpr std::size_t max_serialized_size() noexcept {
        return sizeof(AuditRecord);
    }
};

static_assert(sizeof(AuditRecord) <= 65535, "AuditRecord 单条 ≤ u16 LEN (framework 约束)");

// ---------- R-11 build-time WAL kind ------------------------------------
//
// 同进程不混 paper / live audit. CI 已硬约束 STCPP_EXEC_MODE ∈ {live, paper, backtest}.
// backtest 不 emit audit (走 mock), 这里 fallback PaperAudit (沙箱).

[[nodiscard]] constexpr stcpp::infra::wal::WalKind AuditWalKindForBuild() noexcept {
#if defined(STCPP_EXEC_MODE_live)
    return stcpp::infra::wal::WalKind::RiskAudit;
#elif defined(STCPP_EXEC_MODE_paper) || defined(STCPP_EXEC_MODE_backtest)
    return stcpp::infra::wal::WalKind::PaperAudit;
#else
    // 缺定义视作 paper (开发期默认, CI 必 -D 之一)
    return stcpp::infra::wal::WalKind::PaperAudit;
#endif
}

[[nodiscard]] constexpr std::string_view AuditWalPathRootForBuild() noexcept {
    return stcpp::infra::wal::PathRootOf(AuditWalKindForBuild());
}

// concept 静态自检 — 编译期保证 AuditRecord 接老王 framework
static_assert(stcpp::infra::wal::WalRecord<AuditRecord>,
              "AuditRecord 必须满足 infra::wal::WalRecord (4 ts + ulid + serialize)");

}  // namespace stcpp::observability
