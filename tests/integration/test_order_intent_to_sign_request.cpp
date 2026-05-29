// tests/integration/test_order_intent_to_sign_request.cpp
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 72 P0 — OrderIntent → SignV52Request transformer integration ctest
//
// 老沈 W9 W3 安全 spec §3 安全验收 (3 integration ctest):
//   T1: YES Buy, token_id=X → sign → Ed25519 verify (非仅 error==Ok)
//       verify 用 SignerV52::PublicKeyBytes() + Ed25519::verify (libsodium)
//   T2: NO Sell → side=1
//       断言 SignV52Request.side == 1 + response.error == Ok
//   T3: token_id byte-equal
//       断言 request.token_id == intent.token_id string-level
//
// ADR-027 cite (Enforce-1 强 enforce):
//   polymarket_ssot_cite: docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md
//                         §3.5 SignedOrder 字段表 (11 字段)
//   handshake_cite:       docs/RESEARCH/laoli-laoSun-handshake-v1.md
//                         §84 SignedOrder ABI Hash (11 字段顺序锁定)
//   goalserve_ssot_cite:  N/A (signer 不消费 Goalserve 数据)
//   adr_cite:             docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md
//                         Enforce-1 (SSOT cite 强 enforce)
//
// 8 安全 spec 验收覆盖:
//   spec-1 (token_id 零变换): T3 string-level byte-equal
//   spec-2 (side static_cast): T2 side == 1 断言
//   spec-3 (4-ts 零变换):  T1/T2/T3 隐含 (ts 透传验证)
//   spec-4 (condition_id → market_id): T1/T2 隐含 (market_id 填充正确)
//   spec-5 (outcome audit only): T2 side==1 不影响 outcome 进 EIP-712
//   spec-6 (audit_id 来源): T1/T2/T3 隐含 (audit_id 来自外部传入非零 ULID)
//   spec-7 (data_source_ts_source=0): T1/T2/T3 隐含 (默认 0 不触碰)
//   spec-8 (signature_type=1): T1 verify 成功隐含 sigType 默认 1 正确
//
// 红线:
//   R-7  paper mode only (STCPP_EXEC_MODE_paper guard)
//   R-20 4 ts chain (pit::NowRealtimeNs() 构造合法 ts)
//   BUG-W5-001 audit_id 非零 (来自 RiskDecision.audit_id 模拟)

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/crypto/ed25519.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/signer/transformer.hpp"
#include "stcpp/signer/v52/signer_v52.hpp"
#include "stcpp/strategy/signal_iface.hpp"

namespace stcpp::signer::test {

namespace {

// ---------- 测试辅助: 构造合法 OrderIntent ----------
//
// 构造 R-20 合规的 4 ts (全来自 pit::NowRealtimeNs(), 不用 now() 替代上游 ts)
// as_of_ts = now - 1s + 3ms 保证 ≤ now (SIGNER-01 修 教训)
risk::OrderIntent MakeValidIntent(const std::string& condition_id, const std::string& token_id,
                                  strategy::Side side, strategy::Outcome outcome) {
    risk::OrderIntent intent;
    const std::int64_t base = infra::wal::pit::NowRealtimeNs() - 1'000'000'000LL;  // 1s ago
    intent.event_ts_ns = base;
    intent.data_source_ts_ns = base + 1'000'000LL;  // +1ms
    intent.ingestion_ts_ns = base + 2'000'000LL;    // +2ms
    intent.as_of_ts_ns = base + 3'000'000LL;        // +3ms (≤ now since base = now-1s)

    intent.condition_id = condition_id;
    intent.token_id = token_id;
    intent.side = side;
    intent.outcome = outcome;

    intent.strategy_id = "P0_01_PinnacleNoVig";
    intent.signal_id = "signal-test-001";
    intent.feature_snapshot_id = "snap-test-001";
    intent.price = 0.55;
    intent.size_pUSD_micro = 100'000'000LL;  // 100 pUSD micro (v0.6 rename)

    intent.book_depth_l1_usdc = 5000.0;
    intent.book_snapshot_ts_ns = base;
    intent.tick_size = 0.01;
    intent.is_close = false;

    return intent;
}

// ---------- 构造非零 audit_id (BUG-W5-001 防御, 模拟 RiskDecision.audit_id) ----------
std::array<std::uint8_t, 16> MakeNonZeroAuditId() noexcept {
    std::array<std::uint8_t, 16> id{};
    // 模拟 ULID 随机段 (非全零)
    id[0] = 0x01U;
    id[1] = 0xA2U;
    id[2] = 0xB3U;
    id[3] = 0xC4U;
    id[4] = 0xD5U;
    id[5] = 0xE6U;
    id[6] = 0xF7U;
    id[7] = 0x08U;
    id[8] = 0x19U;
    id[9] = 0x2AU;
    id[10] = 0x3BU;
    id[11] = 0x4CU;
    id[12] = 0x5DU;
    id[13] = 0x6EU;
    id[14] = 0x7FU;
    id[15] = 0x80U;
    return id;
}

// ---------- 重建签名消息 (与 signer_v52.cpp Sign() 一致, v5.3 格式) ----------
//
// v5.3 消息格式:
//   event_ts(8B LE) || market_id || token_id || side(1B) || outcome(1B) || audit_id(16B)
// cite: laosun-w9-signer-v53-abi-align-spec-v1.md §3.2
std::vector<std::uint8_t> RebuildSignMsg(const v52::SignV52Request& req) {
    std::vector<std::uint8_t> msg;
    msg.reserve(8U + req.market_id.size() + req.token_id.size() + 1U + 1U + 16U);

    // event_ts_ns (8B little-endian)
    for (std::size_t i = 0U; i < 8U; ++i) {
        msg.push_back(
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(req.event_ts_ns) >> (i * 8U)) & 0xFFU));
    }
    // market_id (condition_id UTF-8 bytes)
    for (const char c : req.market_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // token_id (uint256 decimal string bytes)
    for (const char c : req.token_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // side (1B)
    msg.push_back(req.side);
    // outcome (1B, audit only)
    msg.push_back(req.outcome);
    // audit_id (16B)
    for (const auto b : req.audit_id) {
        msg.push_back(b);
    }
    return msg;
}

}  // namespace

// ============================================================
// T1: YES Buy, token_id=X → to_sign_request → Sign → Ed25519 verify
//
// 老沈 spec §3 安全验收 T1:
//   - to_sign_request() 生成 SignV52Request
//   - SignerV52::Sign() 签名 → error == Ok
//   - 用 SignerV52::PublicKeyBytes() + Ed25519::verify 验证 64B 签名
//   - verify 必须返回 true (非仅 error==Ok)
//
// 覆盖:
//   spec-1 (token_id 零变换): 签名消息含原始 token_id bytes
//   spec-4 (condition_id → market_id): market_id 来自 condition_id
//   spec-8 (signature_type=1): sigType 默认 1, verify 成功证明 sign 路径通
// ============================================================
TEST(OrderIntentToSignRequest, T1_YesBuy_SignAndVerify) {
    const std::string condition_id = "0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    const std::string token_id =
        "1677202003548168512111076196662317438975560192301735320827449539424843146463";
    const auto audit_id = MakeNonZeroAuditId();

    // 构造 YES Buy intent
    const auto intent = MakeValidIntent(condition_id, token_id, strategy::Side::Buy, strategy::Outcome::Yes);

    // --- transformer: OrderIntent → SignV52Request ---
    const v52::SignV52Request req = to_sign_request(intent, audit_id);

    // spec-4: market_id 必须是 condition_id, 不是 token_id
    EXPECT_EQ(req.market_id, condition_id)
        << "T1 spec-4: market_id must come from condition_id, not token_id";
    EXPECT_NE(req.market_id, token_id) << "T1 spec-4: market_id must NOT be token_id (rename spec)";

    // spec-1: token_id 原样传入签名器
    EXPECT_EQ(req.token_id, token_id) << "T1 spec-1: token_id must pass through unchanged";

    // spec-2: side=Buy → 0
    EXPECT_EQ(req.side, 0U) << "T1 spec-2: Buy side must be 0";

    // spec-8: signature_type 必须 = 1 (默认值, 不触碰)
    EXPECT_EQ(req.signature_type, 1U) << "T1 spec-8: signature_type must be 1 (HMAC bug #2 防御)";

    // spec-6: audit_id echo 正确
    EXPECT_EQ(req.audit_id, audit_id) << "T1 spec-6: audit_id must come from caller (RiskDecision.audit_id)";

    // --- Sign ---
    v52::SignerV52 signer{execution::ExecutionMode::Paper};
    const auto resp = signer.Sign(req);

    ASSERT_EQ(resp.error, v52::SignV52Error::Ok)
        << "T1: Sign must succeed for valid YES Buy intent, got: " << v52::ToString(resp.error);
    ASSERT_EQ(resp.signature.size(), 64U) << "T1: Ed25519 detached signature must be 64B";

    // --- Ed25519 verify (非仅 error==Ok, 老沈 spec T1 硬要求) ---
    const auto pk = signer.PublicKeyBytes();
    ASSERT_EQ(pk.size(), 32U) << "T1: PublicKeyBytes must return 32B for paper mode";

    // 重建签名消息 (与 signer_v52.cpp Sign() 一致)
    const auto msg = RebuildSignMsg(req);

    std::array<std::uint8_t, crypto::kEd25519PublicKeyBytes> pk_arr{};
    std::copy(pk.begin(), pk.end(), pk_arr.begin());

    std::array<std::uint8_t, crypto::kEd25519SignatureBytes> sig_arr{};
    std::copy(resp.signature.begin(), resp.signature.end(), sig_arr.begin());

    const bool verify_ok =
        crypto::Ed25519::verify(std::span<const std::uint8_t, crypto::kEd25519PublicKeyBytes>{pk_arr},
                                std::span<const std::uint8_t>{msg.data(), msg.size()},
                                std::span<const std::uint8_t, crypto::kEd25519SignatureBytes>{sig_arr});
    EXPECT_TRUE(verify_ok) << "T1: Ed25519::verify of to_sign_request + Sign must return true "
                              "(老沈 spec T1: verify 非仅 error==Ok)";

    // R-11: paper 硬填 PaperAudit
    EXPECT_EQ(resp.audit_wal_kind, infra::wal::WalKind::PaperAudit)
        << "T1: R-11 paper signer must fill PaperAudit";
}

// ============================================================
// T2: NO Sell → side=1
//
// 老沈 spec §3 安全验收 T2:
//   断言 SignV52Request.side == 1 (static_cast<uint8_t>(Side::Sell))
//   断言 response.error == Ok
//
// 覆盖:
//   spec-2 (side static_cast): Sell → side=1 零误映射
//   spec-5 (outcome audit only): outcome=No → req.outcome=1, 不影响 side
//   spec-7 (data_source_ts_source=0): 验证默认值
// ============================================================
TEST(OrderIntentToSignRequest, T2_NoSell_SideIsOne_ErrorIsOk) {
    const std::string condition_id = "0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
    const std::string token_id = "9876543210987654321098765432109876543210987654321098765432109876543";
    const auto audit_id = MakeNonZeroAuditId();

    // 构造 NO Sell intent
    const auto intent = MakeValidIntent(condition_id, token_id, strategy::Side::Sell, strategy::Outcome::No);

    // --- transformer ---
    const v52::SignV52Request req = to_sign_request(intent, audit_id);

    // spec-2: side=Sell → side=1 (static_cast<uint8_t>(Side::Sell))
    // 禁布尔运算 / 条件重映射
    ASSERT_EQ(req.side, 1U) << "T2 spec-2: Sell side must be exactly 1 (static_cast<uint8_t>(Side::Sell))";

    // spec-5: outcome=No → req.outcome=1 (audit only, 不进 EIP-712)
    EXPECT_EQ(req.outcome, static_cast<std::uint8_t>(strategy::Outcome::No))
        << "T2 spec-5: outcome must be static_cast<uint8_t>(No)=1, audit only";

    // spec-7: data_source_ts_source 必须 = 0 (UpstreamPayload), 禁默认 3
    EXPECT_EQ(req.data_source_ts_source, 0U)
        << "T2 spec-7: data_source_ts_source must be 0 (UpstreamPayload), not 3";

    // spec-4: market_id 来自 condition_id
    EXPECT_EQ(req.market_id, condition_id) << "T2 spec-4: market_id must be condition_id";

    // spec-1: token_id 零变换
    EXPECT_EQ(req.token_id, token_id) << "T2 spec-1: token_id must pass through unchanged";

    // spec-8: signature_type = 1 (默认, 不触碰)
    EXPECT_EQ(req.signature_type, 1U) << "T2 spec-8: signature_type must be 1 (HMAC bug #2 防御, 不触碰)";

    // spec-6: audit_id echo
    EXPECT_EQ(req.audit_id, audit_id) << "T2 spec-6: audit_id must come from caller input";

    // --- Sign 必须 Ok (老沈 spec T2 硬要求: response.error == Ok) ---
    v52::SignerV52 signer{execution::ExecutionMode::Paper};
    const auto resp = signer.Sign(req);

    EXPECT_EQ(resp.error, v52::SignV52Error::Ok) << "T2: Sign must return Ok for valid NO Sell intent";
    EXPECT_EQ(resp.signature.size(), 64U) << "T2: signature must be 64B when Ok";
}

// ============================================================
// T3: token_id byte-equal
//
// 老沈 spec §3 安全验收 T3:
//   断言 request.token_id == intent.token_id (string-level byte-equal)
//
// 覆盖:
//   spec-1: token_id pass-through 零变换, 任何 token_id 原样 copy
//   长字符串 (77 chars) + 短字符串 + 边界 token_id 三种情况
// ============================================================
TEST(OrderIntentToSignRequest, T3_TokenId_ByteEqual) {
    const std::string condition_id = "0xdeadbeefcafe1234deadbeefcafe1234deadbeefcafe1234deadbeefcafe1234";
    const auto audit_id = MakeNonZeroAuditId();

    // 测试向量: 多种 token_id 格式 (都应 byte-equal pass-through)
    const std::vector<std::string> token_ids = {
        // 典型 77 位 uint256 decimal string (最长 case)
        "1677202003548168512111076196662317438975560192301735320827449539424843146463",
        // 中等长度
        "9876543210987654321098765432109876543210987654321098765432109876543",
        // 短字符串 (小 token_id)
        "42",
        // 最小值 "1"
        "1",
        // 全数字, 不含 0x 前缀 (SSOT §3.5 约定: uint256 decimal, 无 0x)
        "12345678901234567890123456789012345678901234567890",
    };

    for (const auto& tid : token_ids) {
        const auto intent = MakeValidIntent(condition_id, tid, strategy::Side::Buy, strategy::Outcome::Yes);

        // --- transformer ---
        const v52::SignV52Request req = to_sign_request(intent, audit_id);

        // spec-1: token_id byte-equal (string-level, 禁 trim/normalize/pad)
        EXPECT_EQ(req.token_id, intent.token_id)
            << "T3 spec-1: token_id byte-equal FAILED for token_id='" << tid
            << "' "
               "(禁止 trim/normalize/pad)";

        // 双向确认: intent.token_id == request.token_id (等价)
        EXPECT_EQ(intent.token_id, req.token_id)
            << "T3: intent.token_id == request.token_id must hold (string-level)";

        // 长度不变 (byte-equal 隐含)
        EXPECT_EQ(req.token_id.size(), tid.size())
            << "T3: token_id length must be unchanged after transformer";
    }
}

}  // namespace stcpp::signer::test
