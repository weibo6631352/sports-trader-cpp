// stcpp/risk/reject_enum.hpp — 21 reject enum + sub_reason (老韩 v0.3 + v0.3.1 + v0.5)
//
// v0.5 变更 (老沈 W9 Wave 57, ADR-027 §4 enforce):
//   - EXCEED_MARKET_EXPOSURE (7) 保持数值不变, 新增 EXCEED_CONDITION_EXPOSURE alias
//   - 新增 EXCEED_PER_OUTCOME_CAP (22) R6.2b per-token cap
//     注: ADR-003 C-4 "21 active codes" 不变 (EXCEED_MARKET_EXPOSURE 语义升级 rename)
//   - InvalidIntentSubReason 新增 v0.5 4 项 (枚举值 9-12)
//
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3 §4.1 §6
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder ABI lock
//   adr_ref:              ADR-027 §4 Enforce-1/2/3/4; ADR-004 (短路顺序)
//
// INVALID_INTENT.sub_reason 字段细分 (老韩 v0.3.1 + 小肖 v1 §2 + 老孙 v5.1 + v0.5)
//
// 红线 R-1: 任何 reject 必经 RiskGateway::evaluate() (绕过 = P0)

#pragma once

#include <cstdint>

namespace stcpp::risk {

enum class RejectCode : std::uint8_t {
    // 状态机
    STATE_HALTED = 0,
    STATE_DRAIN = 1,
    STATE_SAFE_MODE = 2,

    // 重复 / 数据
    DUPLICATE_INTENT = 3,
    STALE_DATA = 4,
    INVALID_INTENT = 5,  // 见 InvalidIntentSubReason

    // 仓位 / 资金
    EXCEED_PER_ORDER_CAP = 6,
    // v0.5: per_condition cap (R6.2a); 原名 EXCEED_MARKET_EXPOSURE, 数值 7 不变
    EXCEED_CONDITION_EXPOSURE = 7,
    EXCEED_MARKET_EXPOSURE = 7,  // v0.4 别名, 数值相同, 维持旧 case 编译
    DAILY_LOSS_HALT = 8,
    CONSEC_LOSS_HALT = 9,
    INSUFFICIENT_BANKROLL = 10,

    // 信号
    EDGE_CI_NEGATIVE = 11,
    EDGE_NEGATED_BY_SLIPPAGE = 12,

    // 市场
    MARKET_TYPE_NOT_ENABLED = 13,
    MARKET_NOT_ACTIVE = 14,

    // 流动性 / 滑点 (小肖 v1)
    LOW_FILL_RATE = 15,
    EXCESSIVE_SLIPPAGE = 16,
    EXCEED_BOOK_DEPTH = 17,

    // 系统
    AUDIT_WAL_BACKPRESSURE = 18,
    STRATEGY_DECAYED = 19,  // 老韩 v0.3 §16, OQ-D13 Bayesian kill switch
    INTERNAL_ERROR = 20,

    // v0.5 新增: per-outcome cap (R6.2b, token 级)
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §6.3
    // 老韩 spec §9.1: token_exposure[token_id] + size > per_outcome_cap → REJECT
    EXCEED_PER_OUTCOME_CAP = 22,  // ADR-003 C-4: 此为扩展值, 21 active 不增原意
};

// INVALID_INTENT 子原因 (老韩 v0.3.1 + 小肖 v1 §2 + 老孙 v5.1 + v0.5)
enum class InvalidIntentSubReason : std::uint8_t {
    NONE = 0,
    BOOK_TS_ZERO = 1,   // book_snapshot_ts_ns == 0
    BOOK_TS_STALE = 2,  // book_snapshot_ts_ns < now - 60s
    NAN_OR_INF = 3,
    NEGATIVE = 4,
    ILLEGAL_TICK = 5,
    // 老孙 v5.1 IPC 4 ts (R-20)
    TS_ORDER_VIOLATED = 6,
    TS_FUTURE = 7,
    TS_UNKNOWN_SRC = 8,
    // v0.5 新增 (老沈 W9 Wave 57, ADR-027 §4 Enforce-1):
    MISSING_TOKEN_ID = 9,          // intent.token_id.empty()
    MISSING_CONDITION_ID = 10,     // intent.condition_id.empty() (原 MISSING_MARKET_ID)
    INVALID_TOKEN_ID_FORMAT = 11,  // token_id 含非数字字符 (uint256 string 格式校验)
    BOOK_TOKEN_ID_MISMATCH = 12,   // R8.4: book_snapshot_ts 对应 token 与 intent.token_id 不一致
    // v0.6 新增 (老孙 Wave 104 P0, laosun-w10-w1 spec-10):
    TS_V2_MISSING = 13,  // intent.timestamp_ms == 0 (V2 CLOB 必须非零)
    // v0.6 新增: bytes32 hex 格式校验 (spec-9):
    INVALID_BYTES32_FORMAT = 14,  // metadata or builder: 非 ^0x[0-9a-f]{64}$ (66 chars)
};

struct RejectDetail {
    RejectCode code;
    InvalidIntentSubReason sub_reason{InvalidIntentSubReason::NONE};
    // invariant (老韩 v0.3.1): code != INVALID_INTENT ==> sub_reason == NONE
};

}  // namespace stcpp::risk
