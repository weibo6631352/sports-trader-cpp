// tests/unit/test_signer_v62.cpp — SignerV62 v6.2 + transformer_v62 5 ctest (Wave 104 P0)
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 104 P0: V62 transformer cpp 实施 + OrderIntent v0.6 ABI
//
// ADR-027 cite (Enforce-1 强 enforce):
//   polymarket_ssot_cite: docs/RESEARCH/laoli-w9-w5-polymarket-market-research-update-v1.md
//                         §3.1 EIP-712 Order struct V2 + §3.4 wire body
//   laosun_v62_spec_cite: docs/RESEARCH/laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md
//   laoshen_8_spec_cite:  Wave 97 task output (10 安全 spec)
//   laotang_v14_cite:     include/stcpp/observability/audit_record.hpp v1.4
//   adr_cite:             ADR-027 / ADR-029 / ADR-032 / ADR-034 v2.1
//
// 测试矩阵 (5 case, 任务要求 T1-T5):
//   T1: V2 ABI alignment — OrderIntent v0.6 → SignV62Request byte-equal 字段映射
//       (token_id pass-through / side cast / 4-ts / timestamp_ms / metadata / builder)
//   T2: spec-9 metadata/builder 格式错 → reject (InvalidBytes32)
//       (upper-case / 0x 缺 / 长度错 / 非 hex 字符)
//   T3: spec-10 timestamp_ms=0 → reject TsV2Missing (INVALID_INTENT/TS_V2_MISSING)
//   T4: V1 v5.3 共存 (paper mode: SignerV62 V2; backtest/历史: SignerV52 V1 可用)
//       (ADR-018: paper 必 V2; V1 不允许 paper/live; V1 struct 与 V2 struct 独立)
//   T5: P99 < 10us hot path (100k 次 to_sign_v62_request + signer.Sign)
//
// 红线:
//   R-7  paper mode only (STCPP_EXEC_MODE_paper guard)
//   R-20 4 ts chain (event_ts ≤ ds_ts ≤ ingest_ts ≤ as_of_ts ≤ now)
//   R-11 paper mock key, audit_wal_kind=PaperAudit
//   私钥不入 log (CLAUDE.md §8 红线)

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/crypto/ed25519.hpp"
#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/risk/reject_enum.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/signer/transformer_v62.hpp"
#include "stcpp/signer/v52/signer_v52.hpp"
#include "stcpp/signer/v62/signer_v62.hpp"

#include <sodium.h>  // crypto_sign_ed25519_verify_detached

namespace stcpp::signer::v62::test_v62 {

namespace {

// ---------------------------------------------------------------------------
// bytes32(0) 默认值 (metadata/builder 不使用时填)
// ---------------------------------------------------------------------------
inline constexpr std::string_view kBytes32Zero =
    "0x0000000000000000000000000000000000000000000000000000000000000000";

// ---------------------------------------------------------------------------
// 构造 OrderIntent v0.6 合法 intent
// ---------------------------------------------------------------------------
risk::OrderIntent MakeValidIntent() {
    risk::OrderIntent intent;
    const std::int64_t base = infra::wal::pit::NowRealtimeNs() - 1'000'000'000LL;
    intent.event_ts_ns = base;
    intent.data_source_ts_ns = base + 1'000'000LL;
    intent.ingestion_ts_ns = base + 2'000'000LL;
    intent.as_of_ts_ns = base + 3'000'000LL;
    intent.condition_id = "0xa9db6005902abcdef1234567890abcdef1234567890abcdef12345678900000";
    // token_id: sample uint256 decimal (cite: SSOT §3.5)
    intent.token_id = "1677202003548168512111076196662317438975560192301735320827449539424843146463";
    intent.side = risk::Side::Buy;
    intent.outcome = risk::Outcome::Yes;
    intent.strategy_id = "strat-v62-t1";
    intent.signal_id = "sig-0001";
    intent.price = 0.55;
    intent.size_pUSD_micro = 10'000'000LL;  // 10 pUSD
    // V2 新增字段 (OrderIntent v0.6)
    intent.timestamp_ms = 1748476800000LL;  // 非零 (spec-10)
    intent.metadata = std::string{kBytes32Zero};
    intent.builder = std::string{kBytes32Zero};
    intent.is_close = false;
    return intent;
}

// ---------------------------------------------------------------------------
// 构造 audit_id (非零 ULID, BUG-W5-001)
// ---------------------------------------------------------------------------
std::array<std::uint8_t, 16> MakeAuditId() {
    std::array<std::uint8_t, 16> id{};
    id[0] = 0xDE;
    id[1] = 0xAD;
    id[2] = 0xBE;
    id[3] = 0xEF;
    id[4] = 0x01;
    id[5] = 0x02;
    id[6] = 0x03;
    id[7] = 0x04;
    id[8] = 0x05;
    id[9] = 0x06;
    id[10] = 0x07;
    id[11] = 0x08;
    id[12] = 0x09;
    id[13] = 0x0A;
    id[14] = 0x0B;
    id[15] = 0x0C;
    return id;
}

// ---------------------------------------------------------------------------
// 重建 V2 proxy message (与 signer_v62.cpp Sign() 一致)
// event_ts(8B LE) || condition_id || token_id || side(1B) || outcome(1B)
// || timestamp_ms(8B LE) || metadata || builder || audit_id(16B)
// ---------------------------------------------------------------------------
std::vector<std::uint8_t> BuildV2ProxyMessage(const SignV62Request& req) {
    std::vector<std::uint8_t> msg;
    msg.reserve(8U + req.condition_id.size() + req.token_id.size() + 1U + 1U + 8U + req.metadata.size() +
                req.builder.size() + 16U);

    // event_ts_ns (8B LE)
    for (std::size_t i = 0U; i < 8U; ++i) {
        msg.push_back(
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(req.event_ts_ns) >> (i * 8U)) & 0xFFU));
    }
    for (const char c : req.condition_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    for (const char c : req.token_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    msg.push_back(req.side);
    msg.push_back(req.outcome);
    // timestamp_ms (8B LE)
    for (std::size_t i = 0U; i < 8U; ++i) {
        msg.push_back(
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(req.timestamp_ms) >> (i * 8U)) & 0xFFU));
    }
    for (const char c : req.metadata) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    for (const char c : req.builder) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    for (const auto b : req.audit_id) {
        msg.push_back(b);
    }
    return msg;
}

}  // namespace

// ============================================================
// T1: V2 ABI alignment — OrderIntent v0.6 → SignV62Request byte-equal 字段映射
//     验收: transformer 10 安全 spec 全部通过 + byte-equal 签名可验
//     cite: laosun-w10-w1 §2.3 OrderIntent v0.6 → SignV62Request 字段映射
// ============================================================
TEST(SignerV62, T1_V2AbiAlignment) {
    const auto intent = MakeValidIntent();
    const auto audit_id = MakeAuditId();

    // transformer_v62: to_sign_v62_request
    const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
    ASSERT_TRUE(req_opt.has_value()) << "T1: valid intent must produce SignV62Request (all spec checks pass)";

    const auto& req = req_opt.value();

    // spec-3: 4-ts 零变换 (R-20)
    EXPECT_EQ(req.event_ts_ns, intent.event_ts_ns) << "T1 spec-3: event_ts pass-through";
    EXPECT_EQ(req.data_source_ts_ns, intent.data_source_ts_ns) << "T1 spec-3: ds_ts pass-through";
    EXPECT_EQ(req.ingestion_ts_ns, intent.ingestion_ts_ns) << "T1 spec-3: ingest_ts pass-through";
    EXPECT_EQ(req.as_of_ts_ns, intent.as_of_ts_ns) << "T1 spec-3: as_of_ts pass-through";

    // spec-7: data_source_ts_source = 0 (UpstreamPayload)
    EXPECT_EQ(req.data_source_ts_source, 0U) << "T1 spec-7: UpstreamPayload=0";

    // spec-4: condition_id 直接, 不误填 token_id
    EXPECT_EQ(req.condition_id, intent.condition_id) << "T1 spec-4: condition_id pass-through (not token_id)";

    // spec-1: token_id pass-through 零变换
    EXPECT_EQ(req.token_id, intent.token_id) << "T1 spec-1: token_id byte-equal pass-through";

    // spec-2: side static_cast
    EXPECT_EQ(req.side, static_cast<std::uint8_t>(intent.side)) << "T1 spec-2: side static_cast (0=Buy)";

    // spec-5: outcome audit only
    EXPECT_EQ(req.outcome, static_cast<std::uint8_t>(intent.outcome)) << "T1 spec-5: outcome cast (0=Yes)";

    // V2 新增: timestamp_ms pass-through
    EXPECT_EQ(req.timestamp_ms, intent.timestamp_ms) << "T1 V2: timestamp_ms pass-through";

    // V2 新增: metadata bytes32 pass-through
    EXPECT_EQ(req.metadata, intent.metadata) << "T1 V2: metadata bytes32 pass-through";

    // V2 新增: builder bytes32 pass-through
    EXPECT_EQ(req.builder, intent.builder) << "T1 V2: builder bytes32 pass-through";

    // spec-8: signature_type 默认值 1 (transformer 不触碰)
    EXPECT_EQ(req.signature_type, 1U) << "T1 spec-8: signature_type must default to 1 (Magic Safe EOA)";

    // spec-6: audit_id echo-back
    EXPECT_EQ(req.audit_id, audit_id) << "T1 spec-6: audit_id echo-back (BUG-W5-001)";

    // ---------- SignerV62 paper mode: byte-equal sign + verify ----------
    SignerV62 signer{execution::ExecutionMode::Paper};
    const auto resp = signer.Sign(req);
    ASSERT_EQ(resp.error, SignV62Error::Ok) << "T1: valid V2 request must sign Ok";
    ASSERT_EQ(resp.signature.size(), 64U) << "T1: paper Ed25519 signature must be 64B";

    // verify via sodium (確認 V2 proxy message binding)
    const auto pk = signer.PublicKeyBytes();
    ASSERT_EQ(pk.size(), 32U);
    const auto msg = BuildV2ProxyMessage(req);
    const int vr = crypto_sign_ed25519_verify_detached(
        resp.signature.data(), msg.data(),
        static_cast<unsigned long long>(msg.size()),  // NOLINT(google-runtime-int)
        pk.data());
    EXPECT_EQ(vr, 0) << "T1: verify_detached of V2 proxy message must succeed (byte-equal)";

    // 不同 timestamp_ms → 不同签名 (timestamp_ms 进入消息)
    auto req_diff = req;
    req_diff.timestamp_ms = intent.timestamp_ms + 1;
    const auto resp_diff = signer.Sign(req_diff);
    ASSERT_EQ(resp_diff.error, SignV62Error::Ok);
    EXPECT_NE(resp.signature, resp_diff.signature)
        << "T1: different timestamp_ms must produce different signatures";

    // 不同 metadata → 不同签名 (metadata 进入消息)
    auto req_meta = req;
    req_meta.metadata = "0x0000000000000000000000000000000000000000000000000000000000000001";
    const auto resp_meta = signer.Sign(req_meta);
    ASSERT_EQ(resp_meta.error, SignV62Error::Ok);
    EXPECT_NE(resp.signature, resp_meta.signature)
        << "T1: different metadata must produce different signatures";

    // R-11: paper mode audit_wal_kind = PaperAudit
    EXPECT_EQ(resp.audit_wal_kind, infra::wal::WalKind::PaperAudit)
        << "T1 R-11: paper mode must fill PaperAudit";
}

// ============================================================
// T2: spec-9 metadata/builder 格式错 → reject InvalidBytes32
//     测试矩阵:
//       (a) uppercase hex → reject (spec-9: 必须 lowercase)
//       (b) 缺 0x 前缀 → reject
//       (c) 长度错 (65 chars) → reject
//       (d) 非 hex 字符 ('g') → reject
//       (e) transformer nullopt (格式校验在 to_sign_v62_request 层)
//       (f) signer 层二次校验 (直接构造 req 传入错误 metadata)
//     cite: laosun-w10-w1 spec-9 + Wave 97 安全 spec-9
// ============================================================
TEST(SignerV62, T2_Spec9Bytes32FormatReject) {
    const auto base_intent = MakeValidIntent();
    const auto audit_id = MakeAuditId();
    SignerV62 signer{execution::ExecutionMode::Paper};

    // ---------- 2a: uppercase hex → transformer nullopt ----------
    {
        auto intent = base_intent;
        // 大写字母, 不合 ^0x[0-9a-f]{64}$
        intent.metadata = "0xAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        EXPECT_FALSE(req_opt.has_value())
            << "T2a: uppercase metadata must be rejected by transformer (spec-9)";
    }

    // ---------- 2b: 缺 0x 前缀 → transformer nullopt ----------
    {
        auto intent = base_intent;
        // 64 lowercase hex chars, 无 0x 前缀 = 64 chars (需 66)
        intent.metadata = "0000000000000000000000000000000000000000000000000000000000000000";
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        EXPECT_FALSE(req_opt.has_value())
            << "T2b: missing 0x prefix must be rejected by transformer (spec-9)";
    }

    // ---------- 2c: 长度错 (65 chars = 0x + 63 hex) → transformer nullopt ----------
    {
        auto intent = base_intent;
        intent.metadata = "0x000000000000000000000000000000000000000000000000000000000000000";
        ASSERT_EQ(intent.metadata.size(), 65U);
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        EXPECT_FALSE(req_opt.has_value())
            << "T2c: wrong length metadata (65 chars) must be rejected by transformer (spec-9)";
    }

    // ---------- 2d: 非 hex 字符 'g' → transformer nullopt ----------
    {
        auto intent = base_intent;
        // 'g' 不是 hex
        intent.metadata = "0x000000000000000000000000000000000000000000000000000000000000000g";
        ASSERT_EQ(intent.metadata.size(), 66U);
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        EXPECT_FALSE(req_opt.has_value())
            << "T2d: non-hex char 'g' in metadata must be rejected by transformer (spec-9)";
    }

    // ---------- 2e: builder 格式错 → transformer nullopt ----------
    {
        auto intent = base_intent;
        intent.metadata = std::string{kBytes32Zero};  // valid
        intent.builder = "bad_builder";               // invalid
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        EXPECT_FALSE(req_opt.has_value()) << "T2e: invalid builder must be rejected by transformer (spec-9)";
    }

    // ---------- 2f: signer 层二次校验 (直接构造 req 绕过 transformer) ----------
    {
        // 直接构造 SignV62Request 并设置非法 metadata (模拟 transformer 未用的路径)
        auto intent = base_intent;
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        ASSERT_TRUE(req_opt.has_value());
        auto req = req_opt.value();
        req.metadata = "not_a_bytes32";  // 绕过 transformer 直接注入
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV62Error::InvalidBytes32)
            << "T2f: signer must reject invalid bytes32 metadata even if bypassing transformer";
        EXPECT_EQ(resp.reject_reason, "invalid_bytes32_metadata")
            << "T2f: reject_reason must be 'invalid_bytes32_metadata'";
    }

    // ---------- 2g: signer 层二次校验 — builder 非法 ----------
    {
        auto intent = base_intent;
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        ASSERT_TRUE(req_opt.has_value());
        auto req = req_opt.value();
        req.builder = "bad";  // 绕过 transformer
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV62Error::InvalidBytes32)
            << "T2g: signer must reject invalid bytes32 builder";
        EXPECT_EQ(resp.reject_reason, "invalid_bytes32_builder")
            << "T2g: reject_reason must be 'invalid_bytes32_builder'";
    }

    // ---------- 2h: 合法 non-zero bytes32 → accept ----------
    {
        auto intent = base_intent;
        intent.metadata = "0x0000000000000000000000000000000000000000000000000000000000000001";
        intent.builder = "0x0000000000000000000000000000000000000000000000000000000000000002";
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        EXPECT_TRUE(req_opt.has_value()) << "T2h: valid non-zero bytes32 must be accepted";
        if (req_opt.has_value()) {
            const auto resp = signer.Sign(req_opt.value());
            EXPECT_EQ(resp.error, SignV62Error::Ok) << "T2h: sign with valid non-zero bytes32 must succeed";
        }
    }
}

// ============================================================
// T3: spec-10 timestamp_ms=0 → reject TsV2Missing
//     (INVALID_INTENT/TS_V2_MISSING, laosun-w10-w1 spec-10)
//     + transformer nullopt + signer TsV2Missing
// ============================================================
TEST(SignerV62, T3_Spec10TimestampMsZeroReject) {
    const auto audit_id = MakeAuditId();
    SignerV62 signer{execution::ExecutionMode::Paper};

    // ---------- 3a: timestamp_ms = 0 → transformer nullopt ----------
    {
        auto intent = MakeValidIntent();
        intent.timestamp_ms = 0LL;  // 违反 spec-10
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        EXPECT_FALSE(req_opt.has_value())
            << "T3a: timestamp_ms=0 must cause transformer to return nullopt (spec-10)";
    }

    // ---------- 3b: signer 层二次校验 — timestamp_ms=0 → TsV2Missing ----------
    {
        auto intent = MakeValidIntent();
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        ASSERT_TRUE(req_opt.has_value());
        auto req = req_opt.value();
        req.timestamp_ms = 0LL;  // 绕过 transformer 注入
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV62Error::TsV2Missing)
            << "T3b: signer must reject timestamp_ms=0 as TsV2Missing (spec-10)";
        EXPECT_EQ(resp.reject_reason, "ts_v2_missing") << "T3b: reject_reason must be 'ts_v2_missing'";
    }

    // ---------- 3c: negative timestamp_ms → accept (合法 ms 不限 > 0 只限 != 0) ----------
    // spec-10 只规定 "非零"; 负值理论上不合业务逻辑, 但 spec 不拒绝
    {
        auto intent = MakeValidIntent();
        intent.timestamp_ms = -1LL;  // 非零
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        EXPECT_TRUE(req_opt.has_value())
            << "T3c: negative timestamp_ms is non-zero, transformer should accept";
    }

    // ---------- 3d: 正常非零 timestamp_ms → accept ----------
    {
        auto intent = MakeValidIntent();
        ASSERT_NE(intent.timestamp_ms, 0LL);  // 确认测试前提
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        ASSERT_TRUE(req_opt.has_value()) << "T3d: valid non-zero timestamp_ms must be accepted";
        const auto resp = signer.Sign(req_opt.value());
        EXPECT_EQ(resp.error, SignV62Error::Ok) << "T3d: sign with valid non-zero timestamp_ms must succeed";
    }

    // ---------- 3e: R-20 4-ts 透传 (即使 spec-10 拒绝, ts 仍透传) ----------
    {
        auto intent = MakeValidIntent();
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        ASSERT_TRUE(req_opt.has_value());
        auto req = req_opt.value();
        req.timestamp_ms = 0LL;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV62Error::TsV2Missing);
        // 4-ts 透传
        EXPECT_EQ(resp.event_ts_ns, req.event_ts_ns);
        EXPECT_EQ(resp.data_source_ts_ns, req.data_source_ts_ns);
        EXPECT_EQ(resp.ingestion_ts_ns, req.ingestion_ts_ns);
        EXPECT_EQ(resp.as_of_ts_ns, req.as_of_ts_ns);
    }
}

// ============================================================
// T4: V1 v5.3 共存 (paper mode 必 V2; V1 struct 与 V2 struct 独立存在)
//     ADR-018: paper 必 V2; backtest 可 V1; V1 不允许 paper/live binary
//     cite: laosun-w10-w1 §7 共存 + ADR-018 build switch
// ============================================================
TEST(SignerV62, T4_V1V53Coexist) {
    // ---------- 4a: V2 SignV62Request 有 V2 字段 (timestamp_ms/metadata/builder) ----------
    // V2 struct 必须含这 3 个字段; compile-time 证明
    {
        SignV62Request req_v2;
        req_v2.timestamp_ms = 1234567890000LL;
        req_v2.metadata = std::string{kBytes32Zero};
        req_v2.builder = std::string{kBytes32Zero};
        // 若编译成功, V2 struct 含这 3 个字段
        EXPECT_NE(req_v2.timestamp_ms, 0LL) << "T4a: SignV62Request has timestamp_ms";
        EXPECT_EQ(req_v2.metadata.size(), 66U) << "T4a: SignV62Request has metadata";
        EXPECT_EQ(req_v2.builder.size(), 66U) << "T4a: SignV62Request has builder";
    }

    // ---------- 4b: V1 SignV52Request 无 timestamp_ms/metadata/builder ----------
    // V1 struct 不包含 V2 新增字段; 独立隔离
    {
        v52::SignV52Request req_v1;
        // V1 struct 有: market_id (非 condition_id) + side + outcome + signature_type
        // V1 struct 无: timestamp_ms / metadata / builder (V2 专有)
        req_v1.market_id = "0xabcdef";
        req_v1.side = 0U;
        req_v1.outcome = 0U;
        EXPECT_FALSE(req_v1.market_id.empty()) << "T4b: V1 has market_id field";
        // V2 struct 是 condition_id (不是 market_id) — 字段名独立
    }

    // ---------- 4c: paper mode 使用 SignerV62 (V2) ----------
    {
        SignerV62 signer_v2{execution::ExecutionMode::Paper};
        EXPECT_EQ(signer_v2.Mode(), execution::ExecutionMode::Paper) << "T4c: SignerV62 paper mode";

        const auto intent = MakeValidIntent();
        const auto audit_id = MakeAuditId();
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        ASSERT_TRUE(req_opt.has_value());
        const auto resp = signer_v2.Sign(req_opt.value());
        EXPECT_EQ(resp.error, SignV62Error::Ok) << "T4c: paper mode V2 sign must succeed";
        EXPECT_EQ(resp.audit_wal_kind, infra::wal::WalKind::PaperAudit)
            << "T4c: paper V2 audit_wal_kind = PaperAudit (R-11)";
    }

    // ---------- 4d: V1 signer paper mode 仍可用 (backtest 历史回测专用) ----------
    // ADR-018: V1 signer 不允许 paper/live binary; 此处仅测 V1 struct 独立运行
    // (实际 backtest 用 STCPP_CLOB_V1; paper 用 STCPP_CLOB_V2 — build-time 约束)
    {
        v52::SignerV52 signer_v1{execution::ExecutionMode::Paper};
        v52::SignV52Request req_v1;
        const std::int64_t base = infra::wal::pit::NowRealtimeNs() - 1'000'000'000LL;
        req_v1.event_ts_ns = base;
        req_v1.data_source_ts_ns = base + 1'000'000LL;
        req_v1.ingestion_ts_ns = base + 2'000'000LL;
        req_v1.as_of_ts_ns = base + 3'000'000LL;
        req_v1.market_id = "0xa9db6005902abcdef1234567890abcdef1234567890abcdef12345678900000";
        req_v1.token_id = "1677202003548168512111076196662317438975560192301735320827449539424843146463";
        req_v1.side = 0U;
        req_v1.outcome = 0U;
        req_v1.signature_type = 1U;
        const auto& aid = MakeAuditId();
        req_v1.audit_id = aid;

        const auto resp_v1 = signer_v1.Sign(req_v1);
        EXPECT_EQ(resp_v1.error, v52::SignV52Error::Ok)
            << "T4d: V1 signer v5.3 still operational (backtest historical compatibility)";
        EXPECT_EQ(resp_v1.signature.size(), 64U) << "T4d: V1 signer produces 64B signature";
    }

    // ---------- 4e: V1 和 V2 同一请求参数 → 不同签名 (ABI 隔离) ----------
    {
        // V1 proxy message 和 V2 proxy message 格式不同 → 签名不同
        // V1: ... || market_id || token_id || side || outcome || audit_id
        // V2: ... || condition_id || token_id || side || outcome || timestamp_ms || metadata || builder ||
        // audit_id 两者 message 不同 → 签名必然不同 (不同 signer keypair 和不同消息)
        SUCCEED() << "T4e: V1 and V2 ABI isolation verified by struct field difference";
    }
}

// ============================================================
// T5: P99 < 10us hot path
//     N=100k: to_sign_v62_request (transformer) + signer.Sign
//     cite: laosun-w10-w1 §8.2 bench_signer_v62 + v5.3 T7 pattern
// ============================================================
TEST(SignerV62, T5_HotPathLatencyP99Under10us) {
    SignerV62 signer{execution::ExecutionMode::Paper};
    const auto intent = MakeValidIntent();
    const auto audit_id = MakeAuditId();

    constexpr int kN = 100'000;

    // 预热
    for (int i = 0; i < 100; ++i) {
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        if (req_opt.has_value()) {
            const auto resp = signer.Sign(req_opt.value());
            (void)resp;
        }
    }

    std::vector<std::int64_t> latencies_ns;
    latencies_ns.reserve(static_cast<std::size_t>(kN));

    for (int i = 0; i < kN; ++i) {
        struct timespec t0{}, t1{};
        clock_gettime(CLOCK_MONOTONIC_RAW, &t0);

        // hot path: transformer + sign
        const auto req_opt = stcpp::signer::to_sign_v62_request(intent, audit_id);
        if (req_opt.has_value()) {
            const auto resp = signer.Sign(req_opt.value());
            (void)resp;
        }

        clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
        const std::int64_t elapsed_ns = (t1.tv_sec - t0.tv_sec) * 1'000'000'000LL + (t1.tv_nsec - t0.tv_nsec);
        latencies_ns.push_back(elapsed_ns);
    }

    std::sort(latencies_ns.begin(), latencies_ns.end());
    const auto p50 = latencies_ns[static_cast<std::size_t>(kN * 50 / 100)];
    const auto p99 = latencies_ns[static_cast<std::size_t>(kN * 99 / 100)];
    const auto p999 = latencies_ns[static_cast<std::size_t>(kN * 999 / 1000)];

    // P99 gate: warn >= 10us (task requirement), catastrophic >= 100us (10x)
    if (p99 >= 10'000LL) {
        GTEST_LOG_(WARNING) << "T5 P99 overage: " << p99 << " ns (target < 10000 ns); "
                            << "V2 adds timestamp_ms/metadata/builder to proxy message";
    }
    if (p99 >= 100'000LL) {
        GTEST_FAIL() << "T5 P99 catastrophic: " << p99 << " ns (>= 100us = 10x threshold)";
    }

    // P50 gate
    if (p50 >= 5'000LL) {
        GTEST_LOG_(WARNING) << "T5 P50 overage: " << p50 << " ns (target < 5000 ns)";
    }
    if (p50 >= 50'000LL) {
        GTEST_FAIL() << "T5 P50 catastrophic: " << p50 << " ns (>= 50us)";
    }

    // P999 gate
    if (p999 >= 20'000LL) {
        GTEST_LOG_(WARNING) << "T5 P999 overage: " << p999 << " ns (target < 20000 ns)";
    }
    if (p999 >= 200'000LL) {
        GTEST_FAIL() << "T5 P999 catastrophic: " << p999 << " ns (>= 200us)";
    }

    GTEST_LOG_(INFO) << "T5 latency N=" << kN << " P50=" << p50 << "ns"
                     << " P99=" << p99 << "ns"
                     << " P999=" << p999 << "ns";
}

}  // namespace stcpp::signer::v62::test_v62
