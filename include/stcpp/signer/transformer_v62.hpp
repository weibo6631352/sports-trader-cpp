// stcpp/signer/transformer_v62.hpp — OrderIntent v0.6 → SignV62Request transformer
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 104 P0: V62 transformer cpp 实施 (老沈 W97 拒 cpp 接手)
//
// 独立头文件原因 (与 transformer.hpp V52 分离):
//   transformer.hpp 对应 v5.3 SignV52Request (V1 ABI, 已废弃生产).
//   transformer_v62.hpp 对应 v6.2 SignV62Request (V2 ABI, 当前生产 + paper).
//   两者共存: backtest 可选 V1 (ADR-018 build switch).
//
// 功能: to_sign_v62_request(OrderIntent v0.6, audit_id) → SignV62Request
//   noexcept, inline, header-only.
//   产出 SignV62Request 含全部 V2 字段; transformer 不做 EIP-712 计算.
//
// 老沈 10 安全 spec 全遵守 (Wave 97 task output):
//   spec-1  token_id pass-through 零变换 (禁 trim/normalize/pad)
//           cite: SSOT §3.5 token_id (uint256 decimal string, 无 0x 前缀)
//   spec-2  side: static_cast<uint8_t>(intent.side), 禁布尔运算/条件重映射
//           cite: SSOT §3.5 side (0=BUY, 1=SELL) + handshake §84 side
//   spec-3  4-ts 原样 copy, 禁 now() 替代 (R-20 红线)
//           cite: ADR R-20 event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts
//   spec-4  condition_id 直接传入, 不能误填 token_id
//           intent.condition_id → req.condition_id (V2 无 market_id 历史遗留命名)
//   spec-5  outcome audit only: static_cast<uint8_t>(intent.outcome), 不入 EIP-712
//           cite: SSOT §2.4 outcomeIndex (audit-only payload)
//   spec-6  audit_id 直接 copy caller 传入 (来自 RiskDecision.audit_id)
//           禁生成新 audit_id / 禁置零 (BUG-W5-001 防御)
//   spec-7  data_source_ts_source = 0 (UpstreamPayload), 禁默认 3
//           cite: handshake §4.3 DataSourceTsSource ABI lock
//   spec-8  signature_type 不触碰: 依赖 SignV62Request 默认值 1 (Magic Safe EOA)
//           HMAC bug #2 教训: v5.1 曾错填 2; 维持修正不回退
//   spec-9  metadata/builder bytes32 格式校验: ^0x[0-9a-f]{64}$ (66 chars)
//           校验失败返回 nullopt (caller 须检查)
//   spec-10 timestamp_ms 非零 (= 0 → reject, INVALID_INTENT/TS_V2_MISSING)
//           校验失败返回 nullopt
//
// ADR-027 cite (Enforce-1 强 enforce):
//   polymarket_ssot_cite: docs/RESEARCH/laoli-w9-w5-polymarket-market-research-update-v1.md
//                         §3.1 EIP-712 Order struct V2 + §3.4 wire body
//   laosun_v62_spec_cite: docs/RESEARCH/laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md
//   laoshen_8_spec_cite:  Wave 97 task output (10 安全 spec) — 老沈 W9 W3 base + spec-9/10
//   laotang_v14_cite:     include/stcpp/observability/audit_record.hpp v1.4
//   adr_cite:             ADR-027 / ADR-029 / ADR-032 / ADR-034 v2.1
//
// 红线:
//   R-1  transformer 在 RiskGateway::evaluate() APPROVED 后调用 (caller 责任)
//   R-20 4 ts 零变换 (spec-3), 禁 now() 替代上游 ts
//   BUG-W5-001 audit_id 非零 (spec-6 — caller 保证, 来自 RiskDecision.audit_id)
//   HMAC bug #2 signature_type=1 (spec-8 — SignV62Request 默认值保证)
//   spec-10 timestamp_ms = 0 → nullopt

#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string_view>

#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/signer/v62/signer_v62.hpp"

namespace stcpp::signer {

// ---- bytes32 格式校验 (spec-9) -----------------------------------------------
//
// 合法格式: ^0x[0-9a-f]{64}$ (lowercase hex, 0x 前缀, 共 66 chars)
// 返回 true = 格式合法; false = 拒, 调用方应返回 nullopt.
//
// noexcept, inline (header-only 路径, 热路径外调用)

[[nodiscard]] inline bool is_valid_bytes32_hex(std::string_view s) noexcept {
    // 必须 66 chars: "0x" (2) + 64 hex digits
    if (s.size() != 66U)
        return false;
    if (s[0] != '0' || s[1] != 'x')
        return false;
    for (std::size_t i = 2U; i < 66U; ++i) {
        const char c = s[i];
        // lowercase hex only (spec-9: ^0x[0-9a-f]{64}$)
        const bool is_digit = (c >= '0' && c <= '9');
        const bool is_lower = (c >= 'a' && c <= 'f');
        if (!is_digit && !is_lower)
            return false;
    }
    return true;
}

// ---------- to_sign_v62_request ----------
//
// OrderIntent v0.6 + audit_id → SignV62Request (IPC v6.2, V2 ABI)
//
// 调用约束:
//   1. intent 已通过 RiskGateway::evaluate() → Decision::APPROVED (R-1)
//   2. audit_id 来自 RiskDecision.audit_id (非零 ULID, BUG-W5-001 防御)
//   3. 4 ts 来自 intent (原样, 不用 now() 替代 — spec-3 / R-20)
//   4. timestamp_ms != 0 (spec-10; 0 → nullopt)
//   5. metadata/builder ^0x[0-9a-f]{64}$ (spec-9; 格式错 → nullopt)
//
// 返回 std::optional<v62::SignV62Request>:
//   has_value() = true  → 所有安全 spec 通过, 可继续签名
//   has_value() = false → spec-9 或 spec-10 校验失败, caller 应 reject INVALID_INTENT
//
// noexcept: 所有操作为值语义 (string copy + uint8_t static_cast + format check), 不抛
// inline: header-only, 轻量变换

[[nodiscard]] inline std::optional<v62::SignV62Request> to_sign_v62_request(
    const risk::OrderIntent& intent, const std::array<std::uint8_t, 16>& audit_id) noexcept {
    // spec-10: timestamp_ms 非零校验 (= 0 → INVALID_INTENT/TS_V2_MISSING)
    // cite: laosun-w10-w1 §8 ABI Breaking spec-10
    if (intent.timestamp_ms == 0) {
        return std::nullopt;
    }

    // spec-9: metadata bytes32 格式校验 ^0x[0-9a-f]{64}$ (66 chars)
    // cite: laosun-w10-w1 §2.1 + Wave 97 安全 spec-9
    if (!is_valid_bytes32_hex(intent.metadata)) {
        return std::nullopt;
    }

    // spec-9: builder bytes32 格式校验 ^0x[0-9a-f]{64}$ (66 chars)
    // cite: laosun-w10-w1 §2.1 + Wave 97 安全 spec-9
    if (!is_valid_bytes32_hex(intent.builder)) {
        return std::nullopt;
    }

    v62::SignV62Request req;

    // spec-3: 4-ts 原样 copy, 禁 now() 替代 (R-20 红线)
    // cite: ADR R-20 event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts
    req.event_ts_ns = intent.event_ts_ns;
    req.data_source_ts_ns = intent.data_source_ts_ns;
    req.ingestion_ts_ns = intent.ingestion_ts_ns;
    req.as_of_ts_ns = intent.as_of_ts_ns;

    // spec-7: data_source_ts_source = 0 (UpstreamPayload), 禁默认 3
    // cite: handshake §4.3 DataSourceTsSource ABI lock (0=UpstreamPayload)
    req.data_source_ts_source = 0U;  // UpstreamPayload — 禁 InferredFromIngestion=3

    // spec-4: condition_id 直接传入, 不能误填 token_id
    // V2 SignV62Request: condition_id 字段名 (无 V1 历史遗留 market_id 命名)
    req.condition_id = intent.condition_id;

    // spec-1: token_id pass-through 零变换, 原样 copy, 禁 trim/normalize/pad
    // cite: SSOT §3.5 token_id (uint256 decimal string, 无 0x 前缀)
    req.token_id = intent.token_id;

    // spec-2: side = static_cast<uint8_t>(intent.side), 禁布尔运算/条件重映射
    // cite: SSOT §3.5 side (0=BUY, 1=SELL) + handshake §84 side
    req.side = static_cast<std::uint8_t>(intent.side);

    // spec-5: outcome audit only, static_cast<uint8_t>(intent.outcome), 不入 EIP-712
    // cite: SSOT §2.4 outcomeIndex (audit-only, 不进 EIP-712 Order struct)
    req.outcome = static_cast<std::uint8_t>(intent.outcome);

    // spec-8: signature_type 不触碰 — 依赖 SignV62Request 字段默认值 1
    // SignV62Request.signature_type{1} 已在结构体定义处初始化为 1 (Magic Safe EOA)
    // HMAC bug #2: v5.1 错填 2; 此处永不赋值 signature_type (维持修正)

    // V2 定价: limit_price_bps + size_pUSD_micro (pUSD micro)
    req.limit_price_bps = intent.price > 0.0 ? static_cast<std::int64_t>(intent.price * 10000.0 + 0.5) : 0LL;
    req.size_pUSD_micro = intent.size_pUSD_micro;

    // V2 新增: timestamp_ms (已校验非零, spec-10)
    // cite: laoli-w9-w5 §3.1 + laosun-w10-w1 §2.1 §8.4
    req.timestamp_ms = intent.timestamp_ms;

    // V2 新增: metadata (已通过 spec-9 格式校验)
    // cite: laoli-w9-w5 §3.1 + laosun-w10-w1 §2.1
    req.metadata = intent.metadata;

    // V2 新增: builder (已通过 spec-9 格式校验)
    // cite: laoli-w9-w5 §3.1 + laosun-w10-w1 §2.1
    req.builder = intent.builder;

    // 透传 maker_address (由外层 Orchestrator 填入, transformer 不生成)
    // maker_address 不来自 OrderIntent (安全隔离: intent 不含地址)
    // 调用方须在拿到 SignV62Request 后填入 maker_address + client_order_id

    // spec-6: audit_id 直接 copy caller 传入值 (来自 RiskDecision.audit_id)
    // 禁生成新 audit_id / 禁置零 (BUG-W5-001 防御)
    req.audit_id = audit_id;

    return req;
}

}  // namespace stcpp::signer
