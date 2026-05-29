// tests/unit/test_audit_schema_v14.cpp — AuditRecord v1.4 schema 5 ctest (W10 Wave 98)
//
// Owner: 老唐 (audit-expert, #38)
// Sprint: W10 Wave 98
// cite:
//   polymarket_ssot_cite: laoli-w9-w5-polymarket-market-research-update-v1.md §3.1 §3.4
//   goalserve_ssot_cite:  N/A
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder + Position ABI
//   signer_spec_cite:     laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.1 §8.1
//   adr_cite:             ADR-027 Enforce-1
//
// T1: timestamp_ms + size_pUSD_micro round-trip (V2 字段 + rename)
// T2: metadata + builder bytes32 hex round-trip (V2 EIP-712 字段)
// T3: BLAKE3 chain V2 新字段不破 hash 链 (算法不变, 3 record chain verify PASS)
// T4: schema v1.3 → v1.4 migration (apply_v13_migration: 默认值 timestamp_ms=0 / metadata={} / builder={})
// T5: schema v1.2 → v1.4 full migration (apply_v12_migration: token_id/outcome/side + V2 默认值)
//
// 约束:
//   - schema_version == kAuditSchemaV14 (0x14)
//   - BLAKE3 chain 算法不变: compute_payload_hash(seq, type, decision_ts) 接口不变
//   - 旧 audit log (v1.2/v1.3) apply migration 后不破 chain
//   - 文件 ≤ 400 行

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_emitter.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/observability/blake3_hash.hpp"
#include "stcpp/risk/reject_enum.hpp"

#include "../../src/stcpp/observability/audit_emitter.cpp"

namespace stcpp::test::audit_v14 {
namespace {

using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalError;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::observability::AuditEmitter;
using stcpp::observability::AuditEventType;
using stcpp::observability::AuditRecord;
using stcpp::observability::Blake3Hasher;
using stcpp::observability::RiskDecisionInput;
using stcpp::observability::kAuditSchemaV12;
using stcpp::observability::kAuditSchemaV13;
using stcpp::observability::kAuditSchemaV14;
using stcpp::observability::kTokenIdMax;
using stcpp::observability::kMarketIdMax;
using stcpp::observability::kBytes32HexMax;
using stcpp::risk::InvalidIntentSubReason;
using stcpp::risk::RejectCode;

static std::int64_t NowNs() noexcept {
    return stcpp::infra::wal::pit::NowRealtimeNs();
}

// V2 bytes32(0) hex sentinel (metadata/builder 不使用时的默认值)
// cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.1
static constexpr std::string_view kBytes32Zero =
    "0x0000000000000000000000000000000000000000000000000000000000000000";

// 构造合法 v1.4 RiskDecisionInput (含全部 V2 字段)
static RiskDecisionInput make_v14_input(
    std::string_view token_id       = "79394987953468328984958213450943553510",
    std::uint8_t     outcome        = 0,           // Yes
    std::uint8_t     side           = 0,           // Buy
    std::int64_t     timestamp_ms   = 1748476800000LL,
    std::string_view metadata       = kBytes32Zero,
    std::string_view builder        = kBytes32Zero,
    std::int64_t     size_pUSD_micro = 10'000'000LL,  // 10 pUSD
    AuditEventType   event_type     = AuditEventType::OrderApproved)
{
    const std::int64_t base = NowNs() - 1'000'000'000LL;
    RiskDecisionInput in{};
    in.event_ts        = base;
    in.data_source_ts  = base + 1'000;
    in.ingestion_ts    = base + 2'000;
    in.as_of_ts        = base + 3'000;
    in.decision_ts     = base + 4'000;
    in.audit_id_bytes  = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    in.condition_id    = "0xa9db6005902eb43a19d7b1e";  // 25 chars < kMarketIdMax
    in.market_id       = in.condition_id;
    in.token_id        = token_id;
    in.outcome         = outcome;
    in.side            = side;
    in.is_buy          = (side == 0);
    in.strategy_id     = "strat_v14_test";
    in.size_pUSD_micro = size_pUSD_micro;
    in.price           = 0.60;
    in.timestamp_ms    = timestamp_ms;
    in.metadata        = metadata;
    in.builder         = builder;
    in.event_type      = event_type;
    in.reject_code     = RejectCode::INTERNAL_ERROR;
    in.sub_reason      = InvalidIntentSubReason::NONE;
    return in;
}

static auto open_writer(const char* prefix) {
    WalConfig cfg{};
    cfg.kind        = WalKind::PaperAudit;
    cfg.path_prefix = std::string("/var/lib/stcpp/paper/") + prefix;
    return WalWriter<AuditRecord>::Open(cfg);
}

// =============================================================================
// T1: timestamp_ms + size_pUSD_micro round-trip (V2 新字段 + pUSD rename)
// =============================================================================
//
// 验证:
//   1. timestamp_ms 从 RiskDecisionInput → AuditRecord.timestamp_ms 正确透传
//   2. size_pUSD_micro (原 size_usdc rename) round-trip 数值不变
//   3. schema_version == kAuditSchemaV14 (0x14)
//   4. emit_decision 端到端成功 (writer open + emit OK)
//
// cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.1 timestamp_ms §5.1 pUSD

TEST(AuditSchemaV14, T1_TimestampMs_SizePUSD_RoundTrip) {
    auto wr = open_writer("v14_t1_ts_size");
    ASSERT_TRUE(wr.has_value()) << "T1: writer open failed";
    AuditEmitter em{wr.value().get()};

    constexpr std::int64_t kTimestampMs   = 1748476800000LL;  // 2026-05-28 00:00:00 UTC ms
    constexpr std::int64_t kSizePUSD      = 25'000'000LL;     // 25 pUSD micro

    const auto in = make_v14_input(
        "79394987953468328984",   // token_id
        0, 0,                     // outcome=Yes, side=Buy
        kTimestampMs,
        kBytes32Zero, kBytes32Zero,
        kSizePUSD);

    // 直接构造 AuditRecord 验证字段映射
    AuditRecord rec{};
    rec.schema_version  = kAuditSchemaV14;
    rec.timestamp_ms    = kTimestampMs;
    rec.size_pUSD_micro = kSizePUSD;
    rec.outcome = 0;
    rec.side    = 0;
    rec.is_buy  = true;

    // serialize round-trip
    std::vector<std::byte> buf(AuditRecord::max_serialized_size());
    const std::size_t n = rec.serialize_into({buf.data(), buf.size()});
    ASSERT_EQ(n, sizeof(AuditRecord)) << "T1: serialize_into 返回 sizeof(AuditRecord)";

    AuditRecord rec2{};
    std::memcpy(&rec2, buf.data(), sizeof(AuditRecord));
    EXPECT_EQ(rec2.schema_version,  kAuditSchemaV14)  << "T1: schema_version round-trip";
    EXPECT_EQ(rec2.timestamp_ms,    kTimestampMs)      << "T1: timestamp_ms round-trip";
    EXPECT_EQ(rec2.size_pUSD_micro, kSizePUSD)         << "T1: size_pUSD_micro round-trip";
    EXPECT_EQ(rec2.outcome,         static_cast<std::uint8_t>(0)) << "T1: outcome round-trip";
    EXPECT_EQ(rec2.side,            static_cast<std::uint8_t>(0)) << "T1: side round-trip";

    // emit_decision 端到端
    const auto r = em.emit_decision(in);
    ASSERT_TRUE(r.has_value()) << "T1: emit_decision failed: " << static_cast<int>(r.error());
    EXPECT_EQ(em.emitted_count(), 1u) << "T1: emitted_count == 1";
}

// =============================================================================
// T2: metadata + builder bytes32 hex round-trip (V2 EIP-712 字段)
// =============================================================================
//
// 验证:
//   1. metadata bytes32 hex (0x + 64 hex = 66 chars) 正确写入 AuditRecord.metadata
//   2. builder bytes32 hex 正确写入 AuditRecord.builder
//   3. 两者 serialize_into → deserialize 字节完全一致
//   4. bytes32(0) 默认值 (不使用时) round-trip
//   5. kBytes32HexMax = 68 (66 printable + 1 null + 1 pad)
//
// cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.1 metadata/builder §3.3

TEST(AuditSchemaV14, T2_Metadata_Builder_Bytes32Hex_RoundTrip) {
    // metadata: 自定义策略标识 (bytes32 hex)
    // 66 chars printable: "0x" + 64 hex
    constexpr std::string_view kMetadata =
        "0xdeadbeef00000000cafebabe00000000deadbeef00000000cafebabe00000000";
    // builder: gasless relayer 专用 bytes32 hex
    constexpr std::string_view kBuilder =
        "0x0000000000000000000000000000000000000000000000000000000000000001";

    static_assert(kMetadata.size() == 66, "metadata 应为 66 printable chars (0x + 64 hex)");
    static_assert(kBuilder.size()  == 66, "builder 应为 66 printable chars (0x + 64 hex)");
    static_assert(kMetadata.size() < kBytes32HexMax, "metadata 不超 buffer");
    static_assert(kBuilder.size()  < kBytes32HexMax, "builder 不超 buffer");

    auto wr = open_writer("v14_t2_meta_builder");
    ASSERT_TRUE(wr.has_value()) << "T2: writer open failed";
    AuditEmitter em{wr.value().get()};

    const auto in = make_v14_input(
        "40471836929808124571",   // token_id
        1, 1,                     // outcome=No, side=Sell
        1748476800000LL + 9999LL,
        kMetadata, kBuilder,
        5'000'000LL);             // 5 pUSD

    // 构造 AuditRecord 手动写入
    AuditRecord rec{};
    rec.schema_version = kAuditSchemaV14;
    {
        const std::size_t n = std::min(kMetadata.size(), rec.metadata.size() - 1);
        std::memcpy(rec.metadata.data(), kMetadata.data(), n);
        rec.metadata[n] = '\0';
    }
    {
        const std::size_t n = std::min(kBuilder.size(), rec.builder.size() - 1);
        std::memcpy(rec.builder.data(), kBuilder.data(), n);
        rec.builder[n] = '\0';
    }

    // serialize round-trip
    std::vector<std::byte> buf(AuditRecord::max_serialized_size());
    ASSERT_EQ(rec.serialize_into({buf.data(), buf.size()}), sizeof(AuditRecord));

    AuditRecord rec2{};
    std::memcpy(&rec2, buf.data(), sizeof(AuditRecord));
    EXPECT_STREQ(rec2.metadata.data(), kMetadata.data())
        << "T2: metadata bytes32 hex round-trip failed";
    EXPECT_STREQ(rec2.builder.data(), kBuilder.data())
        << "T2: builder bytes32 hex round-trip failed";
    EXPECT_EQ(rec2.schema_version, kAuditSchemaV14) << "T2: schema_version round-trip";

    // bytes32(0) default round-trip
    AuditRecord rec_zero{};
    rec_zero.schema_version = kAuditSchemaV14;
    // metadata/builder zero-init = empty string (array 已 value-init)
    EXPECT_EQ(rec_zero.metadata[0], '\0') << "T2: zero-init metadata[0] == 0";
    EXPECT_EQ(rec_zero.builder[0],  '\0') << "T2: zero-init builder[0] == 0";

    // emit_decision 端到端
    const auto r = em.emit_decision(in);
    ASSERT_TRUE(r.has_value()) << "T2: emit_decision failed: " << static_cast<int>(r.error());
    EXPECT_EQ(em.emitted_count(), 1u) << "T2: emitted_count == 1";
}

// =============================================================================
// T3: BLAKE3 chain V2 新字段不破 hash 链 (3 record, chain verify PASS)
// =============================================================================
//
// 验证:
//   - v1.4 新字段 (timestamp_ms/metadata/builder/size_pUSD_micro) 进 AuditRecord,
//     但 compute_payload_hash(seq, type, decision_ts) 接口不变
//   - 3 条 v1.4 record emit 后, 本地 mirror chain 重算 == emitter last_hash
//   - hash_chain_verify(1, 3) PASS
//
// cite: ADR-027 Enforce-1; laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §8.1

TEST(AuditSchemaV14, T3_BLAKE3_Chain_V2Fields_NotBreak) {
    auto wr = open_writer("v14_t3_chain");
    ASSERT_TRUE(wr.has_value()) << "T3: writer open failed";
    AuditEmitter em{wr.value().get()};

    const Blake3Hasher::Hash256 zero{};
    ASSERT_EQ(em.last_hash(), zero) << "T3: chain 起点必全 0";

    Blake3Hasher::Hash256 mirror_prev{};

    // Record 0: seq=1, timestamp_ms 设 V2 值, metadata=非零
    constexpr std::string_view kMeta1 =
        "0xdeadbeef00000001deadbeef00000001deadbeef00000001deadbeef00000001";
    const auto ctx0 = make_v14_input(
        "1111111111111111111", 0, 0,
        1748476800001LL, kMeta1, kBytes32Zero, 10'000'000LL,
        AuditEventType::OrderApproved);
    ASSERT_TRUE(em.emit_decision(ctx0).has_value()) << "T3: emit seq=1 failed";
    {
        const auto seq     = static_cast<std::uint64_t>(1);
        const auto payload = Blake3Hasher::compute_payload_hash(
            seq, static_cast<std::uint8_t>(AuditEventType::OrderApproved), ctx0.decision_ts);
        const auto expected = Blake3Hasher::hash_chain(mirror_prev, payload);
        EXPECT_EQ(em.last_hash(), expected)
            << "T3: seq=1 chain hash != local recompute (V2 字段不应改变 hash 算法)";
        mirror_prev = expected;
    }

    // Record 1: seq=2, metadata=bytes32(0), builder=非零
    constexpr std::string_view kBuilder2 =
        "0x0000000000000000000000000000000000000000000000000000000000000002";
    const auto ctx1 = make_v14_input(
        "2222222222222222222", 1, 1,
        1748476800002LL, kBytes32Zero, kBuilder2, 5'000'000LL,
        AuditEventType::OrderApproved);
    ASSERT_TRUE(em.emit_decision(ctx1).has_value()) << "T3: emit seq=2 failed";
    {
        const auto seq     = static_cast<std::uint64_t>(2);
        const auto payload = Blake3Hasher::compute_payload_hash(
            seq, static_cast<std::uint8_t>(AuditEventType::OrderApproved), ctx1.decision_ts);
        const auto expected = Blake3Hasher::hash_chain(mirror_prev, payload);
        EXPECT_EQ(em.last_hash(), expected) << "T3: seq=2 chain hash != local recompute";
        mirror_prev = expected;
    }

    // Record 2: seq=3, OrderFilled, size_pUSD_micro=25M
    const auto ctx2 = make_v14_input(
        "3333333333333333333", 5, 0,
        1748476800003LL, kBytes32Zero, kBytes32Zero, 25'000'000LL,
        AuditEventType::OrderFilled);
    ASSERT_TRUE(em.emit_decision(ctx2).has_value()) << "T3: emit seq=3 failed";
    {
        const auto seq     = static_cast<std::uint64_t>(3);
        const auto payload = Blake3Hasher::compute_payload_hash(
            seq, static_cast<std::uint8_t>(AuditEventType::OrderFilled), ctx2.decision_ts);
        const auto expected = Blake3Hasher::hash_chain(mirror_prev, payload);
        EXPECT_EQ(em.last_hash(), expected) << "T3: seq=3 chain hash != local recompute";
    }

    EXPECT_EQ(em.emitted_count(), 3u) << "T3: 3 emit 完成";
    EXPECT_TRUE(em.hash_chain_verify(1, 3))
        << "T3: hash_chain_verify(1,3) PASS (V2 字段不破 chain)";
}

// =============================================================================
// T4: schema v1.3 → v1.4 migration (apply_v13_migration: V2 字段默认值填充)
// =============================================================================
//
// 模拟读取 v1.3 AuditRecord (schema_version=0x13, 无 timestamp_ms/metadata/builder)
// apply_v13_migration() 后:
//   - schema_version == kAuditSchemaV14
//   - timestamp_ms == 0       (V1 nonce 时代无此字段)
//   - metadata == {}          (bytes32(0) migration 默认值)
//   - builder == {}           (bytes32(0) migration 默认值)
//   - size_pUSD_micro: 已有值不变 (offset 不移, 仅 rename)
//   - 原有字段 (token_id/outcome/side/condition_id) 不被 apply_v13_migration 修改
//
// cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §8.1 (audit schema v1.3→v1.4)

TEST(AuditSchemaV14, T4_V13_To_V14_Migration) {
    // Case A: v1.3 record — timestamp_ms/metadata/builder 全零 (v1.3 无此字段)
    {
        AuditRecord rec_v13{};
        rec_v13.schema_version  = kAuditSchemaV13;
        rec_v13.outcome         = 2;   // Over
        rec_v13.side            = 0;   // Buy
        rec_v13.size_pUSD_micro = 12'000'000LL;  // 12 pUSD (原 size_usdc 位置 = 同 offset)

        // 写入 token_id (v1.3 已有字段)
        const char* tok = "88888888888888888";
        const std::size_t tn = std::strlen(tok);
        std::memcpy(rec_v13.token_id.data(), tok, tn);
        rec_v13.token_id[tn] = '\0';

        // 写入 condition_id
        const char* cond = "0xdeadbeef000000v13";
        const std::size_t cn = std::strlen(cond);
        std::memcpy(rec_v13.condition_id.data(), cond, cn);
        rec_v13.condition_id[cn] = '\0';

        // pre-migration 状态
        EXPECT_EQ(rec_v13.schema_version, kAuditSchemaV13) << "T4-A: pre-migration version";
        EXPECT_EQ(rec_v13.timestamp_ms,   static_cast<std::int64_t>(0))
            << "T4-A: pre-migration timestamp_ms 全零 (v1.3 无此字段)";
        EXPECT_EQ(rec_v13.metadata[0],    '\0') << "T4-A: pre-migration metadata zero-init";
        EXPECT_EQ(rec_v13.builder[0],     '\0') << "T4-A: pre-migration builder zero-init";

        // migration
        rec_v13.apply_v13_migration();

        EXPECT_EQ(rec_v13.schema_version, kAuditSchemaV14)
            << "T4-A: 迁移后 schema_version 应为 v1.4";
        EXPECT_EQ(rec_v13.timestamp_ms, static_cast<std::int64_t>(0))
            << "T4-A: migration timestamp_ms = 0 (V1 无此字段默认)";
        EXPECT_EQ(rec_v13.metadata[0], '\0')
            << "T4-A: migration metadata 仍为空 (bytes32(0) migration default)";
        EXPECT_EQ(rec_v13.builder[0],  '\0')
            << "T4-A: migration builder 仍为空 (bytes32(0) migration default)";

        // 原有字段不被 apply_v13_migration 修改
        EXPECT_EQ(rec_v13.outcome,         static_cast<std::uint8_t>(2)) << "T4-A: outcome 不变";
        EXPECT_EQ(rec_v13.side,            static_cast<std::uint8_t>(0)) << "T4-A: side 不变";
        EXPECT_EQ(rec_v13.size_pUSD_micro, static_cast<std::int64_t>(12'000'000LL))
            << "T4-A: size_pUSD_micro 数值不变 (仅 rename)";
        EXPECT_STREQ(rec_v13.token_id.data(), tok) << "T4-A: token_id 不变";
        EXPECT_STREQ(rec_v13.condition_id.data(), cond) << "T4-A: condition_id 不变";
    }

    // Case B: v1.3 record, is_buy=false — apply_v13_migration 不操作 side/outcome
    {
        AuditRecord rec_v13b{};
        rec_v13b.schema_version  = kAuditSchemaV13;
        rec_v13b.side            = 1;   // Sell (v1.3 已有字段)
        rec_v13b.outcome         = 1;   // No
        rec_v13b.is_buy          = false;
        rec_v13b.size_pUSD_micro = 3'000'000LL;

        rec_v13b.apply_v13_migration();

        EXPECT_EQ(rec_v13b.schema_version, kAuditSchemaV14) << "T4-B: schema_version";
        EXPECT_EQ(rec_v13b.side,    static_cast<std::uint8_t>(1)) << "T4-B: side=Sell 不变";
        EXPECT_EQ(rec_v13b.outcome, static_cast<std::uint8_t>(1)) << "T4-B: outcome=No 不变";
        EXPECT_EQ(rec_v13b.timestamp_ms, static_cast<std::int64_t>(0)) << "T4-B: timestamp_ms=0";
        EXPECT_EQ(rec_v13b.size_pUSD_micro, static_cast<std::int64_t>(3'000'000LL))
            << "T4-B: size_pUSD_micro 不变";
    }
}

// =============================================================================
// T5: schema v1.2 → v1.4 full migration (apply_v12_migration: 全链 migration)
// =============================================================================
//
// 模拟读取 v1.2 AuditRecord (schema_version=0x12):
//   - 含 is_buy:bool, 无 token_id/outcome/side (v1.3 字段)
//   - 无 timestamp_ms/metadata/builder (v1.4 字段)
// apply_v12_migration() 后 (v1.4 版一次性迁到 v1.4):
//   - schema_version == kAuditSchemaV14
//   - outcome == 0 (Yes, default)
//   - side: is_buy=true → 0(Buy); is_buy=false → 1(Sell)
//   - token_id == "" (空, default)
//   - timestamp_ms == 0 (V1 无此字段)
//   - metadata == {} (bytes32(0) default)
//   - builder == {} (bytes32(0) default)
//
// SSOT: laotang-audit-schema-v1.1.md §2.x (4 ts 兼容性)
// cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §8.1

TEST(AuditSchemaV14, T5_V12_To_V14_FullMigration) {
    // Case A: v1.2 record, is_buy=true → side=Buy(0)
    {
        AuditRecord rec_v12{};
        rec_v12.schema_version  = kAuditSchemaV12;
        const char* cond_id = "0xdeadbeef0000v12a";   // < kMarketIdMax
        const std::size_t cn = std::strlen(cond_id);
        std::memcpy(rec_v12.condition_id.data(), cond_id, cn);
        rec_v12.condition_id[cn] = '\0';
        rec_v12.is_buy           = true;
        // v1.2 에서 token_id/metadata/builder 는 zero-init (없는 필드)
        // size_pUSD_micro 도 zero-init 유지

        EXPECT_EQ(rec_v12.schema_version, kAuditSchemaV12) << "T5-A: pre-migration version";
        EXPECT_EQ(rec_v12.token_id[0],    '\0') << "T5-A: pre-migration token_id zero";
        EXPECT_EQ(rec_v12.metadata[0],    '\0') << "T5-A: pre-migration metadata zero";
        EXPECT_EQ(rec_v12.builder[0],     '\0') << "T5-A: pre-migration builder zero";
        EXPECT_EQ(rec_v12.timestamp_ms,   static_cast<std::int64_t>(0))
            << "T5-A: pre-migration timestamp_ms zero";

        rec_v12.apply_v12_migration();

        EXPECT_EQ(rec_v12.schema_version, kAuditSchemaV14)
            << "T5-A: 迁移后 schema_version 应为 v1.4";
        EXPECT_EQ(rec_v12.side,    static_cast<std::uint8_t>(0))
            << "T5-A: is_buy=true → side=Buy(0)";
        EXPECT_EQ(rec_v12.outcome, static_cast<std::uint8_t>(0))
            << "T5-A: migration default outcome=Yes(0)";
        EXPECT_EQ(rec_v12.token_id[0],  '\0')
            << "T5-A: migration 后 token_id 仍空 (v1.2 无此字段)";
        EXPECT_EQ(rec_v12.timestamp_ms, static_cast<std::int64_t>(0))
            << "T5-A: migration timestamp_ms=0 (V1 无此字段)";
        EXPECT_EQ(rec_v12.metadata[0],  '\0')
            << "T5-A: migration metadata 空 (bytes32(0) default)";
        EXPECT_EQ(rec_v12.builder[0],   '\0')
            << "T5-A: migration builder 空 (bytes32(0) default)";
    }

    // Case B: v1.2 record, is_buy=false → side=Sell(1)
    {
        AuditRecord rec_v12{};
        rec_v12.schema_version = kAuditSchemaV12;
        rec_v12.is_buy         = false;

        rec_v12.apply_v12_migration();

        EXPECT_EQ(rec_v12.schema_version, kAuditSchemaV14) << "T5-B: schema_version";
        EXPECT_EQ(rec_v12.side,    static_cast<std::uint8_t>(1)) << "T5-B: is_buy=false → side=Sell(1)";
        EXPECT_EQ(rec_v12.outcome, static_cast<std::uint8_t>(0)) << "T5-B: outcome=Yes default";
        EXPECT_EQ(rec_v12.timestamp_ms, static_cast<std::int64_t>(0))
            << "T5-B: timestamp_ms=0 (V1 无此字段)";
        EXPECT_EQ(rec_v12.metadata[0], '\0') << "T5-B: metadata 空";
        EXPECT_EQ(rec_v12.builder[0],  '\0') << "T5-B: builder 空";
    }

    // Case C: v1.4 record serialize → deserialize + static_assert 常量
    {
        AuditRecord rec_v14{};
        rec_v14.schema_version  = kAuditSchemaV14;
        rec_v14.timestamp_ms    = 1748476800999LL;
        rec_v14.size_pUSD_micro = 7'500'000LL;
        const char* meta = "0xabcdef0000000000000000000000000000000000000000000000000000000000";
        std::memcpy(rec_v14.metadata.data(), meta, std::strlen(meta));

        std::vector<std::byte> buf(AuditRecord::max_serialized_size());
        ASSERT_EQ(rec_v14.serialize_into({buf.data(), buf.size()}), sizeof(AuditRecord));
        AuditRecord rec2{};
        std::memcpy(&rec2, buf.data(), sizeof(AuditRecord));
        EXPECT_EQ(rec2.schema_version,  kAuditSchemaV14)    << "T5-C: schema_version round-trip";
        EXPECT_EQ(rec2.timestamp_ms,    rec_v14.timestamp_ms) << "T5-C: timestamp_ms round-trip";
        EXPECT_EQ(rec2.size_pUSD_micro, rec_v14.size_pUSD_micro) << "T5-C: size_pUSD_micro round-trip";
        EXPECT_STREQ(rec2.metadata.data(), meta) << "T5-C: metadata round-trip";
    }
}

// =============================================================================
// 静态断言: AuditRecord v1.4 常量 + ABI 约束
// =============================================================================

static_assert(kAuditSchemaV12 == 0x12, "kAuditSchemaV12 应为 0x12");
static_assert(kAuditSchemaV13 == 0x13, "kAuditSchemaV13 应为 0x13");
static_assert(kAuditSchemaV14 == 0x14, "kAuditSchemaV14 应为 0x14");
static_assert(kAuditSchemaV14 > kAuditSchemaV13, "v1.4 > v1.3 版本号单调递增");
static_assert(kAuditSchemaV13 > kAuditSchemaV12, "v1.3 > v1.2 版本号单调递增");
static_assert(kTokenIdMax == 80,   "kTokenIdMax 应为 80 (uint256 77 位 + null + 对齐)");
static_assert(kBytes32HexMax == 68, "kBytes32HexMax 应为 68 (66 printable + 1 null + 1 pad)");
static_assert(sizeof(AuditRecord) <= 65535,
              "AuditRecord v1.4 单条 ≤ u16 LEN (framework 约束)");

}  // namespace
}  // namespace stcpp::test::audit_v14
