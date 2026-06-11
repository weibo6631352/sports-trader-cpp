// stcpp/observability/audit_record.hpp — laotang audit envelope v1.4 (W10 Wave 98)
//
// 落:
//   laotang-audit-schema-v1.1.md §2.x (4 ts + 12 AET + reject + sub_reason + chain)
//   laowang-wal-framework-cpp-interface-v1.md §6 (WalRecord concept)
//   laohan-rm-v0.3.1 §5  (21 reject + INVALID_INTENT.sub_reason 字段化)
//   laosun-blake3-v5.1 §3 (Sprint-3 真上线, 此处 stub digest)
//   laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.1 §8.1 (V2 字段 + pUSD rename)
//
// v1.3 变更 (W9 Wave 65, 老唐):
//   - 新增 token_id: char[80]  (uint256 string, 无 0x 前缀, 最多 77 位 + null)
//   - 新增 outcome: uint8_t    (Outcome enum 底层类型, 0=Yes/1=No/…)
//   - 新增 side: uint8_t       (Side enum, 0=Buy/1=Sell)
//   - market_id → condition_id rename (保留 market_id alias 兼容 v1.2 call sites)
//   - is_buy 保留 (v1.2 replay migration 读取; v1.3 写路径用 side)
//   - schema_version: 0x12 → 0x13 (WAL magic byte bump)
//   - BLAKE3 chain 兼容: 新字段进 payload, hash chain 算法不变
//
// v1.4 变更 (W10 Wave 98, 老唐):
//   - 新增 timestamp_ms: int64_t  (V2 EIP-712 Order.timestamp, ms, 替代 nonce)
//   - 新增 metadata: char[68]     (bytes32 hex 0x前缀, V2 EIP-712 Order.metadata)
//   - 新增 builder: char[68]      (bytes32 hex optional, V2 EIP-712 Order.builder)
//   - size_usdc rename → size_pUSD_micro (USDC.e → pUSD, 语义: micro = 1e-6)
//   - schema_version: 0x13 → 0x14 (WAL magic byte bump)
//   - BLAKE3 chain 兼容: 新字段进 payload, hash chain 算法不变
//   - v1.3 → v1.4 migration: apply_v13_migration() 补填 V2 字段默认值
//
// cite:
//   polymarket_ssot_cite: laoli-w9-w5-polymarket-market-research-update-v1.md §3.1 §3.4
//   goalserve_ssot_cite:  N/A
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder + Position ABI
//   adr_cite:             ADR-027 Enforce-1
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

#include "stcpp/execution/execution_mode.hpp"
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
// v1.3: token_id buffer (uint256 string, max 77 chars + null term, 80B 对齐)
// SSOT: laoli-w9-w5-polymarket-market-research-update-v1.md §3.1
inline constexpr std::size_t kTokenIdMax     = 80;
// v1.4: bytes32 hex buffer (0x 前缀 + 64 hex chars + null term = 67B, 对齐到 68B)
// cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.1 metadata/builder
inline constexpr std::size_t kBytes32HexMax  = 68;   // 66 printable + 1 null + 1 pad
// schema version magic bytes
inline constexpr std::uint8_t kAuditSchemaV12 = 0x12;
inline constexpr std::uint8_t kAuditSchemaV13 = 0x13;
inline constexpr std::uint8_t kAuditSchemaV14 = 0x14;

struct AuditRecord {
    // ---- schema_version (migration reader 用) ----------------------------
    // v1.2 = 0x12, v1.3 = 0x13, v1.4 = 0x14. 不进 BLAKE3 payload (chain 兼容).
    std::uint8_t schema_version = kAuditSchemaV14;

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

    // ---- 市场标识 (v1.3: condition_id + token_id 双主键) -----------------
    // condition_id: bytes32 hex (0x 前缀, 66 char), market 级
    //   alias: market_id 兼容 v1.2 call sites (指向同一 array)
    // SSOT: laoli-w9-w5-polymarket-market-research-update-v1.md §3.1
    std::array<char, kMarketIdMax>   condition_id{};    // v1.3 正名 (原 market_id)

    // token_id: uint256 string (无 0x 前缀, 十进制, 最多 77 位)
    // 新增 v1.3 — CLOB 下单 EIP-712 Order.tokenId 一等公民
    // SSOT: laoli-w9-w5-polymarket-market-research-update-v1.md §3.1
    // handshake: laoli-laoSun-handshake-v1.md §3 SignedOrder.token_id
    std::array<char, kTokenIdMax>    token_id{};         // v1.3 新增

    std::array<char, kStrategyIdMax> strategy_id{};

    // v1.4: size_usdc → size_pUSD_micro (USDC.e → pUSD rename; micro = 1e-6 不变)
    // cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.2 §5.1
    std::int64_t size_pUSD_micro = 0;
    double       price           = 0.0;

    // v1.3: outcome + side (替换原 is_buy: bool)
    // outcome: Outcome enum 底层 uint8 (0=Yes, 1=No, 2=Home, 3=Draw, 4=Away, 5=Over, 6=Under)
    // SSOT: laoli-w9-w5-polymarket-market-research-update-v1.md §3.1
    std::uint8_t outcome = 0;   // v1.3 新增, default=Yes(0)

    // side: Side enum 底层 uint8 (0=Buy, 1=Sell)
    // SSOT: laoli-laoSun-handshake-v1.md §3 SignedOrder.side (BUY=0/SELL=1)
    std::uint8_t side    = 0;   // v1.3 新增, default=Buy(0)

    // is_buy: 保留 v1.2 兼容 (v1.2 replay migration 读, v1.3/v1.4 写路径从 side 推断)
    // migration: is_buy=true → side=Buy(0); is_buy=false → side=Sell(1)
    bool         is_buy  = true;  // v1.2 兼容字段, v1.3+ 写路径由 side 决定

    // ---- v1.4 新增: V2 CLOB 字段 (老孙 SignerV62 §2.1) -----------------
    // timestamp_ms: V2 EIP-712 Order.timestamp (uint256, ms).
    //   替代 V1 nonce; 同地址同 ms 不可重复提交 (CLOB V2 唯一性保证).
    //   cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.1 §3.1
    std::int64_t timestamp_ms = 0;   // v1.4 新增; 0 = 未设 (migration default)

    // metadata: bytes32 hex string (0x 前缀 + 64 hex = 66 printable chars + null).
    //   V2 EIP-712 Order.metadata; 不使用时填 bytes32(0) hex.
    //   cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.1 §3.3
    std::array<char, kBytes32HexMax> metadata{};  // v1.4 新增; zero-init = empty

    // builder: bytes32 hex optional (0x 前缀 + 64 hex = 66 printable chars + null).
    //   V2 EIP-712 Order.builder; gasless relayer 专用, 不使用时填 bytes32(0) hex.
    //   cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.1 §3.3
    std::array<char, kBytes32HexMax> builder{};   // v1.4 新增; zero-init = empty

    // ---- BLAKE3 hash chain (老唐 v1.1 §4) ------------------------------
    // prev_hash         = 上一条 current_hash (chain 起点 = 全 0)
    // payload_hash      = BLAKE3(payload bytes ex hash region)        — Sprint-3 真算
    // current_hash      = BLAKE3(prev_hash || payload_hash)            — Sprint-3 真算
    // W4 stub: payload_hash = u64 counter + len, current = prev XOR payload (链式可校验)
    // v1.4: 新字段 timestamp_ms/metadata/builder/size_pUSD_micro 进 payload (不破 chain 算法)
    std::array<std::uint8_t, kHashBytes> prev_hash{};
    std::array<std::uint8_t, kHashBytes> payload_hash{};
    std::array<std::uint8_t, kHashBytes> current_hash{};

    // framework 帧尾还有 CRC32C (framework 算); 这里 payload 内嵌一份 sanity check.
    std::uint32_t crc32c = 0;

    // ---- migration helpers (旧 audit log 不破) --------------------------
    //
    // apply_v12_migration(): 读 v1.2 record (schema_version==0x12) 时调用.
    //   condition_id = market_id (直接拷贝, 同 array 无需再拷)
    //   token_id = ""  (空, migration 默认)
    //   outcome  = 0   (Yes, migration 默认)
    //   side     = is_buy ? 0 : 1  (由 is_buy 推断)
    //   v1.4 字段: timestamp_ms=0, metadata={}, builder={}
    //
    // NOTE: v1.2 AuditRecord 使用旧 size_usdc 字段.
    //       replay 路径按旧 layout 解包, 再调 apply_v12_migration()
    //       → 调用方负责把旧 size_usdc 值写入 size_pUSD_micro (本函数不知道旧值).
    void apply_v12_migration() noexcept {
        // condition_id 已是 market_id 内容 (同 array), 无需再拷贝
        // token_id 全零 = 空字符串 (array 已 zero-init)
        outcome        = 0;                       // default: Yes
        side           = is_buy ? 0u : 1u;        // Buy=0 / Sell=1
        // v1.4 V2 字段默认值
        timestamp_ms   = 0;
        metadata       = {};
        builder        = {};
        schema_version = kAuditSchemaV14;  // 升版本标记 (replay 后不再触发)
    }

    // apply_v13_migration(): 读 v1.3 record (schema_version==0x13) 时调用.
    //   v1.4 新增字段补填默认值:
    //     timestamp_ms  = 0       (V1 nonce 时代无此字段)
    //     metadata      = {}      (bytes32(0), migration 默认)
    //     builder       = {}      (bytes32(0), migration 默认)
    //     size_pUSD_micro: 原有值不变 (offset 与 v1.3 size_usdc 相同 — 仅 rename)
    // NOTE: 调用方须先按 v1.3 layout 读 size_usdc 并写入 size_pUSD_micro (仅 rename).
    void apply_v13_migration() noexcept {
        timestamp_ms   = 0;
        metadata       = {};
        builder        = {};
        schema_version = kAuditSchemaV14;  // 升版本标记
    }

    // ---- v1.2 call-site 兼容: market_id 作为 condition_id 的 alias getter ----
    // v1.2 代码访问 r.market_id → 读到 condition_id 内容 (同 array)
    // 注: 直接访问 condition_id array 成员与 market_id() 读取等价
    [[nodiscard]] std::string_view market_id_sv() const noexcept {
        return std::string_view{condition_id.data(),
                                std::min(condition_id.size(),
                                         std::strlen(condition_id.data()))};
    }
    [[nodiscard]] std::string_view token_id_sv() const noexcept {
        return std::string_view{token_id.data(),
                                std::min(token_id.size(),
                                         std::strlen(token_id.data()))};
    }

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

static_assert(sizeof(AuditRecord) <= 65535, "AuditRecord v1.4 单条 ≤ u16 LEN (framework 约束)");

// ---------- R-11 WAL kind (2026-06-12 运行时 mode 化; 名字保留 ForBuild 兼容调用方) ----

[[nodiscard]] inline stcpp::infra::wal::WalKind AuditWalKindForBuild() noexcept {
    return stcpp::execution::ExecutionContext::Mode() == stcpp::execution::ExecutionMode::Live
               ? stcpp::infra::wal::WalKind::RiskAudit
               : stcpp::infra::wal::WalKind::PaperAudit;
}

[[nodiscard]] inline std::string_view AuditWalPathRootForBuild() noexcept {
    return stcpp::infra::wal::PathRootOf(AuditWalKindForBuild());
}

// concept 静态自检 — 编译期保证 AuditRecord 接老王 framework
static_assert(stcpp::infra::wal::WalRecord<AuditRecord>,
              "AuditRecord 必须满足 infra::wal::WalRecord (4 ts + ulid + serialize)");

}  // namespace stcpp::observability
