// stcpp/signer/transformer.hpp — OrderIntent → SignV52Request transformer
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 72 P0 (老孙 cpp 实施, 老沈 W9 W3 8 安全 spec)
//
// 独立头文件原因 (与 signer_iface.hpp 分离):
//   signer_iface.hpp 被 stcpp_signer_paper 链 (不链 stcpp_crypto_ed25519 / libsodium).
//   signer_v52.hpp 依赖 ed25519.hpp → sodium.h (libsodium ExternalProject_Add).
//   合并会导致 stcpp_signer_paper 编译链路需 sodium.h, 破坏 R-7 物理隔离.
//   解决: transformer.hpp 单独引 signer_v52.hpp + risk_gateway.hpp,
//         调用方 (integration ctest) 链 stcpp_signer_v52_paper + stcpp_risk.
//
// 签名: to_sign_request(intent, audit_id) noexcept → v52::SignV52Request
//   - intent:   OrderIntent v0.5 (老韩 risk_gateway.hpp)
//   - audit_id: 来自 RiskDecision.audit_id (非零 ULID, BUG-W5-001 教训)
//
// 老沈 W9 W3 8 安全 spec 全遵守:
//   spec-1  token_id pass-through: intent.token_id 原样 copy, 禁 trim/normalize/pad
//           cite: SSOT §3.5 token_id (uint256 decimal string, 无 0x 前缀)
//   spec-2  side: static_cast<uint8_t>(intent.side), 禁布尔运算/条件重映射
//           cite: SSOT §3.5 side (0=BUY, 1=SELL) + handshake §84 side
//   spec-3  4-ts: 原样 copy, 禁 now() 替代 (R-20 红线)
//           cite: ADR R-20 event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts
//   spec-4  condition_id → market_id rename (signer 侧历史遗留字段名)
//           intent.condition_id → req.market_id; 不能误填 intent.token_id
//   spec-5  outcome audit only: static_cast<uint8_t>(intent.outcome), 不进 EIP-712
//           cite: SSOT §2.4 outcomeIndex (audit-only payload)
//   spec-6  audit_id: 直接 copy caller 传入 (来自 RiskDecision.audit_id)
//           禁生成新 audit_id / 禁置零 (BUG-W5-001 防御)
//   spec-7  data_source_ts_source = 0 (UpstreamPayload), 禁默认 3
//           cite: handshake §4.3 DataSourceTsSource ABI lock
//   spec-8  signature_type 不触碰: 依赖 SignV52Request 默认值 1 (Magic Safe EOA)
//           HMAC bug #2 教训: v5.1 曾错填 2; v5.3 修正为 1
//
// ADR-027 cite (Enforce-1 强 enforce):
//   polymarket_ssot_cite: docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md
//                         §3.5 SignedOrder 字段表 (11 字段 + verifyingContract 选择规则)
//   handshake_cite:       docs/RESEARCH/laoli-laoSun-handshake-v1.md
//                         §84 SignedOrder ABI Hash 9c156025c5d86914 (11 字段顺序锁定)
//   goalserve_ssot_cite:  N/A (signer 不消费 Goalserve 数据)
//   adr_cite:             docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md
//                         Enforce-1 (SSOT cite 强 enforce) + Enforce-2 (FOM 4 人 approve)
//
// 红线:
//   R-1  transformer 在 RiskGateway::evaluate() APPROVED 后调用 (caller 责任)
//   R-20 4 ts 零变换 (spec-3), 禁 now() 替代上游 ts
//   BUG-W5-001 audit_id 非零 (spec-6 — caller 保证, 来自 RiskDecision.audit_id)
//   HMAC bug #2 signature_type=1 (spec-8 — SignV52Request 默认值保证)

#pragma once

#include <array>
#include <cstdint>

#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/signer/v52/signer_v52.hpp"

namespace stcpp::signer {

// ---------- to_sign_request ----------
//
// OrderIntent v0.5 + audit_id → SignV52Request (IPC v5.3 msgpack schema v1.3)
//
// 调用约束:
//   1. intent 已通过 RiskGateway::evaluate() → Decision::APPROVED (R-1)
//   2. audit_id 来自 RiskDecision.audit_id (非零 ULID, BUG-W5-001 防御)
//   3. 4 ts 来自 intent (原样, 不用 now() 替代 — spec-3 / R-20)
//
// noexcept: 所有操作为值语义 (string copy + uint8_t static_cast), 不抛
// inline: 轻量变换, 无需链接独立 .cpp

[[nodiscard]] inline v52::SignV52Request to_sign_request(
    const risk::OrderIntent& intent, const std::array<std::uint8_t, 16>& audit_id) noexcept {
    v52::SignV52Request req;

    // spec-3: 4-ts 原样 copy, 禁 now() 替代 (R-20 红线)
    // cite: ADR R-20 event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts
    req.event_ts_ns = intent.event_ts_ns;
    req.data_source_ts_ns = intent.data_source_ts_ns;
    req.ingestion_ts_ns = intent.ingestion_ts_ns;
    req.as_of_ts_ns = intent.as_of_ts_ns;

    // spec-7: data_source_ts_source = 0 (UpstreamPayload), 禁默认 3
    // cite: handshake §4.3 DataSourceTsSource ABI lock (0=UpstreamPayload)
    req.data_source_ts_source = 0U;  // UpstreamPayload — 禁 InferredFromIngestion=3

    // spec-4: condition_id → market_id rename (signer 侧历史遗留命名)
    // intent.condition_id 是 market 级 bytes32 hex; 不能误填 intent.token_id
    req.market_id = intent.condition_id;

    // spec-1: token_id pass-through 零变换, 原样 copy, 禁 trim/normalize/pad
    // cite: SSOT §3.5 token_id (uint256 decimal string, 无 0x 前缀)
    req.token_id = intent.token_id;

    // spec-2: side = static_cast<uint8_t>(intent.side), 禁布尔运算/条件重映射
    // cite: SSOT §3.5 side (0=BUY, 1=SELL) + handshake §84 side
    req.side = static_cast<std::uint8_t>(intent.side);

    // spec-5: outcome audit only, static_cast<uint8_t>(intent.outcome), 不进 EIP-712
    // cite: SSOT §2.4 outcomeIndex (audit-only, 不进 EIP-712 Order struct)
    req.outcome = static_cast<std::uint8_t>(intent.outcome);

    // spec-8: signature_type 不触碰 — 依赖 SignV52Request 结构体字段默认值 1
    // SignV52Request.signature_type{1} 已在结构体定义处初始化为 1 (Magic Safe EOA)
    // HMAC bug #2: v5.1 错填 2; v5.3 修正 → 此处永不赋值 signature_type

    // spec-6: audit_id 直接 copy caller 传入值 (来自 RiskDecision.audit_id)
    // 禁生成新 audit_id / 禁置零 (BUG-W5-001 防御)
    req.audit_id = audit_id;

    // intent_id: OrderIntent v0.5 未含 intent_id 字段; 保持默认 0
    req.intent_id = 0U;

    return req;
}

}  // namespace stcpp::signer
