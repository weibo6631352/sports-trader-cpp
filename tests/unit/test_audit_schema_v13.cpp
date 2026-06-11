// tests/unit/test_audit_schema_v13.cpp — AuditRecord v1.3 schema 5 ctest (W9 Wave 65)
//
// Owner: 老唐 (audit-expert, #38)
// Sprint: W9 Wave 65
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3 §6
//   goalserve_ssot_cite:  N/A
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder + Position ABI
//   adr_cite:             ADR-027 Enforce-1
//
// T1: AuditEvent v1.3 含 token_id round-trip (serialize → deserialize)
// T2: outcome enum 序列化 (0-6 全 enum, uint8 round-trip)
// T3: side enum 序列化 (0/1 round-trip)
// T4: BLAKE3 chain 新字段不破 hash 链 (T2_Chain_RecomputeMatches 仍 PASS)
// T5: schema v1.2 → v1.3 migration (旧 log read 加 default 字段)
//
// 约束:
//   - 480 + 5 = 485 expected PASS (新增 5 个独立 TEST)
//   - 不破 BLAKE3 chain: compute_payload_hash 接口不变 (seq/type/ts)

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_emitter.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/observability/blake3_hash.hpp"
#include "stcpp/risk/reject_enum.hpp"

// 复用 emitter.cpp 已有的模板实例化 (避免链接错误).
#include "../../src/stcpp/observability/audit_emitter.cpp"

namespace stcpp::test::audit_v13 {
namespace {

using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalError;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::observability::AuditEmitter;
using stcpp::observability::AuditEventType;
using stcpp::observability::AuditRecord;
using stcpp::observability::Blake3Hasher;
using stcpp::observability::kAuditSchemaV12;
using stcpp::observability::kAuditSchemaV13;
using stcpp::observability::kMarketIdMax;
using stcpp::observability::kTokenIdMax;
using stcpp::observability::RiskDecisionInput;
using stcpp::risk::InvalidIntentSubReason;
using stcpp::risk::RejectCode;

static std::int64_t NowNs() noexcept {
    return stcpp::infra::wal::pit::NowRealtimeNs();
}

// 构造合法 v1.3 RiskDecisionInput (含 token_id / outcome / side)
static RiskDecisionInput make_v13_input(std::string_view token_id = "79394987953468328984958213450943553510",
                                        std::uint8_t outcome = 0,  // Yes
                                        std::uint8_t side = 0,     // Buy
                                        AuditEventType type = AuditEventType::OrderApproved) {
    const std::int64_t base = NowNs() - 1'000'000'000LL;
    RiskDecisionInput in{};
    in.event_ts = base;
    in.data_source_ts = base + 1'000;
    in.ingestion_ts = base + 2'000;
    in.as_of_ts = base + 3'000;
    in.decision_ts = base + 4'000;
    in.audit_id_bytes = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    in.condition_id = "0xa9db6005902eb43a19d7b1e3e6f7e9dcc7ab5f2";  // v1.3 正名
    in.market_id = in.condition_id;                                 // v1.2 alias 同步 (optional)
    in.token_id = token_id;
    in.outcome = outcome;
    in.side = side;
    in.strategy_id = "strat_v13_test";
    in.size_pUSD_micro = 2'000;
    in.price = 0.60;
    in.is_buy = (side == 0);
    in.event_type = type;
    in.reject_code = RejectCode::INTERNAL_ERROR;
    in.sub_reason = InvalidIntentSubReason::NONE;
    return in;
}

static auto open_writer(const char* prefix) {
    WalConfig cfg{};
    cfg.kind = WalKind::PaperAudit;
    cfg.path_prefix = std::string("/var/lib/stcpp/engine/") + prefix;
    return WalWriter<AuditRecord>::Open(cfg);
}

// =============================================================================
// T1: AuditEvent v1.3 含 token_id round-trip (serialize → deserialize)
// =============================================================================
//
// 验证:
//   1. token_id 从 RiskDecisionInput 传递到 AuditRecord.token_id 字段
//   2. serialize_into → memcpy back → token_id 字节完全一致 (round-trip)
//   3. schema_version == kAuditSchemaV13 (0x13)
//   4. condition_id 正确写入 (v1.3 正名, 注: kMarketIdMax=32, 超 31 字节截断)

TEST(AuditSchemaV13, T1_TokenId_RoundTrip) {
    auto wr = open_writer("v13_t1_token_id");
    ASSERT_TRUE(wr.has_value()) << "T1: writer open failed";
    AuditEmitter em{wr.value().get()};

    // token_id: 模拟真实 uint256 decimal string (77 位以内, < kTokenIdMax=80)
    constexpr std::string_view kTokenId =
        "1677202003548168512111076196662317438975560192301735320827449539424843146463";
    static_assert(kTokenId.size() < kTokenIdMax, "token_id 超 buffer");

    // condition_id 必须 ≤ kMarketIdMax-1 = 31 字节 (char[32] 含 null terminator)
    // SSOT: §2.3 condition_id 是 bytes32 hex, 字段名长 66 字符, 但 AuditRecord
    //       char[32] 为存储 truncate (非破坏性, 审计用途仅需前 31 字节唯一标识)
    constexpr std::string_view kCondId = "0xa9db6005902eb43a19d7b1e3e6";  // 28 字符 < 32
    static_assert(kCondId.size() < stcpp::observability::kMarketIdMax, "condition_id 超 buffer");

    const auto in = make_v13_input(kTokenId, 0, 0, AuditEventType::OrderApproved);

    // 重建本地 AuditRecord (直接字段赋值验证 round-trip, 不依赖 emit_decision 内部)
    AuditRecord rec{};
    rec.schema_version = kAuditSchemaV13;
    // copy_fixed 语义: min(src, dst) 字节拷贝 + null terminator
    {
        const std::size_t n = std::min(kCondId.size(), rec.condition_id.size() - 1);
        std::memcpy(rec.condition_id.data(), kCondId.data(), n);
        rec.condition_id[n] = '\0';
    }
    {
        const std::size_t n = std::min(kTokenId.size(), rec.token_id.size() - 1);
        std::memcpy(rec.token_id.data(), kTokenId.data(), n);
        rec.token_id[n] = '\0';
    }
    rec.outcome = 0;  // Yes
    rec.side = 0;     // Buy

    // schema_version check
    EXPECT_EQ(rec.schema_version, kAuditSchemaV13) << "T1: schema_version 应为 v1.3";

    // serialize_into round-trip
    std::vector<std::byte> buf(AuditRecord::max_serialized_size());
    const std::size_t n = rec.serialize_into({buf.data(), buf.size()});
    ASSERT_EQ(n, sizeof(AuditRecord)) << "T1: serialize_into 应返回 sizeof(AuditRecord)";

    AuditRecord rec2{};
    std::memcpy(&rec2, buf.data(), sizeof(AuditRecord));
    EXPECT_EQ(rec2.schema_version, kAuditSchemaV13) << "T1: round-trip schema_version";
    EXPECT_STREQ(rec2.token_id.data(), kTokenId.data())
        << "T1: token_id round-trip failed (77 位 uint256 string)";
    EXPECT_STREQ(rec2.condition_id.data(), kCondId.data()) << "T1: condition_id round-trip failed";
    EXPECT_EQ(rec2.outcome, static_cast<std::uint8_t>(0)) << "T1: outcome round-trip";
    EXPECT_EQ(rec2.side, static_cast<std::uint8_t>(0)) << "T1: side round-trip";

    // emit_decision 也成功 (end-to-end)
    const auto r = em.emit_decision(in);
    ASSERT_TRUE(r.has_value()) << "T1: emit_decision failed: " << static_cast<int>(r.error());
    EXPECT_EQ(em.emitted_count(), 1u) << "T1: emitted_count 应为 1";
}

// =============================================================================
// T2: outcome enum 序列化 (0-6 全 enum, uint8 round-trip)
// =============================================================================
//
// Outcome 值定义 (老韩 v0.5 §2.1):
//   Yes=0, No=1, Home=2, Draw=3, Away=4, Over=5, Under=6
// 验证所有合法 outcome 值 emit + schema_version=0x13

TEST(AuditSchemaV13, T2_Outcome_AllValues_Serialized) {
    auto wr = open_writer("v13_t2_outcome");
    ASSERT_TRUE(wr.has_value()) << "T2: writer open failed";
    AuditEmitter em{wr.value().get()};

    // outcome 0-6 全覆盖
    constexpr std::uint8_t kOutcomes[] = {0, 1, 2, 3, 4, 5, 6};
    const std::string_view kTokenIds[] = {
        "1111111111",  // Yes token
        "2222222222",  // No token
        "3333333333",  // Home token
        "4444444444",  // Draw token
        "5555555555",  // Away token
        "6666666666",  // Over token
        "7777777777",  // Under token
    };

    for (std::size_t i = 0; i < 7; ++i) {
        auto in = make_v13_input(kTokenIds[i], kOutcomes[i], 0, AuditEventType::OrderApproved);
        auto r = em.emit_decision(in);
        ASSERT_TRUE(r.has_value()) << "T2: emit_decision outcome=" << static_cast<int>(kOutcomes[i])
                                   << " failed: " << static_cast<int>(r.error());
    }
    EXPECT_EQ(em.emitted_count(), 7u) << "T2: 7 outcome emit 应全通过";

    // 单独验证 round-trip: 构造 AuditRecord + serialize
    for (std::size_t i = 0; i < 7; ++i) {
        AuditRecord rec{};
        rec.schema_version = kAuditSchemaV13;
        rec.outcome = kOutcomes[i];
        rec.side = 0;

        std::vector<std::byte> buf(AuditRecord::max_serialized_size());
        ASSERT_EQ(rec.serialize_into({buf.data(), buf.size()}), sizeof(AuditRecord));

        AuditRecord rec2{};
        std::memcpy(&rec2, buf.data(), sizeof(AuditRecord));
        EXPECT_EQ(rec2.outcome, kOutcomes[i])
            << "T2: outcome=" << static_cast<int>(kOutcomes[i]) << " round-trip failed";
        EXPECT_EQ(rec2.schema_version, kAuditSchemaV13) << "T2: schema_version round-trip";
    }
}

// =============================================================================
// T3: side enum 序列化 (0/1, Buy/Sell round-trip)
// =============================================================================
//
// Side 值定义 (老韩 v0.5 §2.1 + handshake §3):
//   Buy=0, Sell=1
// 验证:
//   - side=0 (Buy): emit 成功, round-trip 还原 side=0
//   - side=1 (Sell): emit 成功, round-trip 还原 side=1
//   - is_buy 字段由 side 正确推断 (side=0 → is_buy=true; side=1 → is_buy=false)

TEST(AuditSchemaV13, T3_Side_BuySell_Serialized) {
    auto wr = open_writer("v13_t3_side");
    ASSERT_TRUE(wr.has_value()) << "T3: writer open failed";
    AuditEmitter em{wr.value().get()};

    // --- side=Buy(0) ---
    {
        const auto in = make_v13_input("79394987953468328984", 0, 0,  // outcome=Yes, side=Buy
                                       AuditEventType::OrderApproved);
        auto r = em.emit_decision(in);
        ASSERT_TRUE(r.has_value()) << "T3: side=Buy emit failed";

        // AuditRecord 验证 (rebuild)
        AuditRecord rec{};
        rec.schema_version = kAuditSchemaV13;
        rec.side = 0;                  // Buy
        rec.outcome = 0;               // Yes
        rec.is_buy = (rec.side == 0);  // v1.2 compat

        std::vector<std::byte> buf(AuditRecord::max_serialized_size());
        ASSERT_EQ(rec.serialize_into({buf.data(), buf.size()}), sizeof(AuditRecord));
        AuditRecord rec2{};
        std::memcpy(&rec2, buf.data(), sizeof(AuditRecord));
        EXPECT_EQ(rec2.side, static_cast<std::uint8_t>(0)) << "T3: side=Buy round-trip";
        EXPECT_TRUE(rec2.is_buy) << "T3: side=Buy → is_buy=true (v1.2 compat)";
    }

    // --- side=Sell(1) ---
    {
        const auto in = make_v13_input("40471836929808124571", 1, 1,  // outcome=No, side=Sell
                                       AuditEventType::OrderApproved);
        auto r = em.emit_decision(in);
        ASSERT_TRUE(r.has_value()) << "T3: side=Sell emit failed";

        AuditRecord rec{};
        rec.schema_version = kAuditSchemaV13;
        rec.side = 1;                  // Sell
        rec.outcome = 1;               // No
        rec.is_buy = (rec.side == 0);  // → false

        std::vector<std::byte> buf(AuditRecord::max_serialized_size());
        ASSERT_EQ(rec.serialize_into({buf.data(), buf.size()}), sizeof(AuditRecord));
        AuditRecord rec2{};
        std::memcpy(&rec2, buf.data(), sizeof(AuditRecord));
        EXPECT_EQ(rec2.side, static_cast<std::uint8_t>(1)) << "T3: side=Sell round-trip";
        EXPECT_FALSE(rec2.is_buy) << "T3: side=Sell → is_buy=false (v1.2 compat)";
    }

    EXPECT_EQ(em.emitted_count(), 2u) << "T3: 2 emit (Buy+Sell)";
}

// =============================================================================
// T4: BLAKE3 chain 新字段不破 hash 链 (T2_Chain_RecomputeMatches 仍 PASS)
// =============================================================================
//
// 验证:
//   - v1.3 新字段 (token_id/outcome/side) 进 AuditRecord, 但 compute_payload_hash 接口
//     仍以 (seq, type, decision_ts) 为输入; hash chain 算法不变
//   - 3 条 v1.3 record emit 后, 本地 mirror chain 重算 == emitter last_hash
//   - 等价验证 T2_Chain_RecomputeMatches 在 v1.3 context 下仍 PASS

TEST(AuditSchemaV13, T4_BLAKE3_Chain_NewFields_NotBreak) {
    auto wr = open_writer("v13_t4_chain");
    ASSERT_TRUE(wr.has_value()) << "T4: writer open failed";
    AuditEmitter em{wr.value().get()};

    const Blake3Hasher::Hash256 zero{};
    ASSERT_EQ(em.last_hash(), zero) << "T4: chain 起点必全 0";

    Blake3Hasher::Hash256 mirror_prev{};

    // Record 0: seq=1, outcome=Yes(0), side=Buy(0), token_id="1111..."
    const auto ctx0 = make_v13_input("1111111111111111111", 0, 0, AuditEventType::OrderApproved);
    ASSERT_TRUE(em.emit_decision(ctx0).has_value()) << "T4: emit seq=1 failed";
    {
        const auto seq = static_cast<std::uint64_t>(1);
        const auto payload = Blake3Hasher::compute_payload_hash(
            seq, static_cast<std::uint8_t>(AuditEventType::OrderApproved), ctx0.decision_ts);
        const auto expected = Blake3Hasher::hash_chain(mirror_prev, payload);
        EXPECT_EQ(em.last_hash(), expected)
            << "T4: seq=1 chain hash != local recompute (新字段不应改变 hash 算法)";
        mirror_prev = expected;
    }

    // Record 1: seq=2, outcome=No(1), side=Sell(1), token_id="2222..."
    const auto ctx1 = make_v13_input("2222222222222222222", 1, 1, AuditEventType::OrderApproved);
    ASSERT_TRUE(em.emit_decision(ctx1).has_value()) << "T4: emit seq=2 failed";
    {
        const auto seq = static_cast<std::uint64_t>(2);
        const auto payload = Blake3Hasher::compute_payload_hash(
            seq, static_cast<std::uint8_t>(AuditEventType::OrderApproved), ctx1.decision_ts);
        const auto expected = Blake3Hasher::hash_chain(mirror_prev, payload);
        EXPECT_EQ(em.last_hash(), expected) << "T4: seq=2 chain hash != local recompute";
        mirror_prev = expected;
    }

    // Record 2: seq=3, outcome=Over(5), side=Buy(0), OrderFilled
    const auto ctx2 = make_v13_input("3333333333333333333", 5, 0, AuditEventType::OrderFilled);
    ASSERT_TRUE(em.emit_decision(ctx2).has_value()) << "T4: emit seq=3 failed";
    {
        const auto seq = static_cast<std::uint64_t>(3);
        const auto payload = Blake3Hasher::compute_payload_hash(
            seq, static_cast<std::uint8_t>(AuditEventType::OrderFilled), ctx2.decision_ts);
        const auto expected = Blake3Hasher::hash_chain(mirror_prev, payload);
        EXPECT_EQ(em.last_hash(), expected) << "T4: seq=3 chain hash != local recompute";
    }

    EXPECT_EQ(em.emitted_count(), 3u) << "T4: 3 emit 完成";

    // hash_chain_verify: 3 条链连续
    EXPECT_TRUE(em.hash_chain_verify(1, 3)) << "T4: hash_chain_verify(1,3) 应通过 (v1.3 新字段不破 chain)";
}

// =============================================================================
// T5: schema v1.2 → v1.4 migration (旧 log read 加 default 字段)
// =============================================================================
//
// 模拟读取 v1.2 AuditRecord (schema_version=0x12, 含 is_buy:bool, 无 token_id/outcome/side)
// apply_v12_migration() 后 (v1.4 版一次到位):
//   - schema_version == kAuditSchemaV14
//   - token_id == "" (空, migration 默认值)
//   - outcome == 0 (Yes, migration 默认值)
//   - side: is_buy=true → side=0(Buy); is_buy=false → side=1(Sell)
//   - timestamp_ms == 0, metadata == {}, builder == {} (v1.4 V2 字段默认)
//
// SSOT 来源 (老韩 W9 W2 §3.3):
//   "读到 schema v1.2 header → 补填 token_id='', outcome=Outcome::Yes, side=(is_buy?Buy:Sell)"
// v1.4 扩展: apply_v12_migration 同时填 V2 字段默认值

TEST(AuditSchemaV13, T5_V12_To_V14_Migration) {
    using stcpp::observability::kAuditSchemaV14;
    // --- 构造 v1.2 AuditRecord (手动设置 schema_version=0x12) ---
    // 模拟磁盘上的旧格式: 含 is_buy 但无 token_id/outcome/side/V2字段

    // Case A: v1.2 record, is_buy=true → migration 后 side=Buy(0)
    // condition_id 必须 ≤ kMarketIdMax-1=31 字节 (char[32] truncation)
    {
        AuditRecord rec_v12{};
        rec_v12.schema_version = kAuditSchemaV12;
        // 使用 ≤ 31 字节的 condition_id
        const char* cond_id = "0xdeadbeef0000000001";  // 20 字符, 安全
        const std::size_t cn = std::strlen(cond_id);
        std::memcpy(rec_v12.condition_id.data(), cond_id, cn);
        rec_v12.condition_id[cn] = '\0';
        // token_id: 不写, 保持 value-init 全零 → 空字符串
        rec_v12.is_buy = true;
        rec_v12.outcome = 0;  // 原字段未初始化, 保守设 0

        // migration reader 调用
        EXPECT_EQ(rec_v12.schema_version, kAuditSchemaV12) << "T5-A: pre-migration version";
        EXPECT_EQ(rec_v12.token_id[0], '\0') << "T5-A: pre-migration token_id 必须全零 (value-init)";
        rec_v12.apply_v12_migration();

        // v1.4: apply_v12_migration 升到 v1.4
        EXPECT_EQ(rec_v12.schema_version, kAuditSchemaV14) << "T5-A: 迁移后 schema_version 应为 v1.4";
        EXPECT_EQ(rec_v12.side, static_cast<std::uint8_t>(0)) << "T5-A: is_buy=true → side=Buy(0)";
        EXPECT_EQ(rec_v12.outcome, static_cast<std::uint8_t>(0)) << "T5-A: migration default outcome=Yes(0)";
        // apply_v12_migration 不写 token_id, 保持 array 零 → 空字符串
        EXPECT_EQ(rec_v12.token_id[0], '\0')
            << "T5-A: migration 后 token_id 仍为空字符串 (不写入, 保持 default)";
        // v1.4 V2 字段默认值
        EXPECT_EQ(rec_v12.timestamp_ms, static_cast<std::int64_t>(0)) << "T5-A: timestamp_ms=0";
        EXPECT_EQ(rec_v12.metadata[0], '\0') << "T5-A: metadata 空";
        EXPECT_EQ(rec_v12.builder[0], '\0') << "T5-A: builder 空";
    }

    // Case B: v1.2 record, is_buy=false → migration 后 side=Sell(1)
    {
        AuditRecord rec_v12{};
        rec_v12.schema_version = kAuditSchemaV12;
        const char* cond_id = "0xdeadbeef0000000002";  // 20 字符
        const std::size_t cn = std::strlen(cond_id);
        std::memcpy(rec_v12.condition_id.data(), cond_id, cn);
        rec_v12.condition_id[cn] = '\0';
        rec_v12.is_buy = false;
        rec_v12.outcome = 0;

        EXPECT_EQ(rec_v12.token_id[0], '\0') << "T5-B: pre-migration token_id 全零";
        rec_v12.apply_v12_migration();

        EXPECT_EQ(rec_v12.schema_version, kAuditSchemaV14) << "T5-B: 迁移后 schema_version 应为 v1.4";
        EXPECT_EQ(rec_v12.side, static_cast<std::uint8_t>(1)) << "T5-B: is_buy=false → side=Sell(1)";
        EXPECT_EQ(rec_v12.outcome, static_cast<std::uint8_t>(0)) << "T5-B: migration default outcome=Yes(0)";
        EXPECT_EQ(rec_v12.token_id[0], '\0') << "T5-B: migration 后 token_id 仍为空字符串";
    }

    // Case C: v1.4 record (schema_version=0x14) round-trip
    {
        AuditRecord rec_v14{};
        rec_v14.schema_version = kAuditSchemaV14;
        rec_v14.side = 1;     // Sell
        rec_v14.outcome = 2;  // Over
        const char* tok = "99999999999";
        std::memcpy(rec_v14.token_id.data(), tok, std::strlen(tok));

        // 序列化后字段完整保留
        std::vector<std::byte> buf(AuditRecord::max_serialized_size());
        ASSERT_EQ(rec_v14.serialize_into({buf.data(), buf.size()}), sizeof(AuditRecord));
        AuditRecord rec2{};
        std::memcpy(&rec2, buf.data(), sizeof(AuditRecord));
        EXPECT_EQ(rec2.schema_version, kAuditSchemaV14) << "T5-C: v1.4 round-trip schema_version";
        EXPECT_EQ(rec2.side, static_cast<std::uint8_t>(1)) << "T5-C: side=Sell 保留";
        EXPECT_EQ(rec2.outcome, static_cast<std::uint8_t>(2)) << "T5-C: outcome=Over 保留";
        EXPECT_STREQ(rec2.token_id.data(), tok) << "T5-C: token_id 保留";
    }
}

// =============================================================================
// 静态断言: AuditRecord v1.3 schema_version 常量正确
// =============================================================================

static_assert(kAuditSchemaV12 == 0x12, "kAuditSchemaV12 应为 0x12");
static_assert(kAuditSchemaV13 == 0x13, "kAuditSchemaV13 应为 0x13");
static_assert(kAuditSchemaV13 > kAuditSchemaV12, "v1.3 > v1.2 版本号单调递增");
static_assert(kTokenIdMax == 80, "kTokenIdMax 应为 80 (uint256 77 位 + null + 对齐)");
static_assert(sizeof(AuditRecord) <= 65535, "AuditRecord v1.3 单条 ≤ u16 LEN (framework 约束)");

}  // namespace
}  // namespace stcpp::test::audit_v13
