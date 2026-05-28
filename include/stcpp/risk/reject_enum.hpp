// stcpp/risk/reject_enum.hpp — 21 reject enum + sub_reason (老韩 v0.3 + v0.3.1)
//
// 21 enum 总数不增 (老郭 ADR-003 C-4 硬约束)
// INVALID_INTENT.sub_reason 字段细分 5 子原因 (老韩 v0.3.1 + 小肖 v1 §2)
//
// 红线 R-1: 任何 reject 必经 RiskGateway::evaluate() (绕过 = P0)

#pragma once

#include <cstdint>

namespace stcpp::risk {

enum class RejectCode : std::uint8_t {
    // 状态机
    STATE_HALTED              = 0,
    STATE_DRAIN               = 1,
    STATE_SAFE_MODE           = 2,

    // 重复 / 数据
    DUPLICATE_INTENT          = 3,
    STALE_DATA                = 4,
    INVALID_INTENT            = 5,   // 见 InvalidIntentSubReason

    // 仓位 / 资金
    EXCEED_PER_ORDER_CAP      = 6,
    EXCEED_MARKET_EXPOSURE    = 7,
    DAILY_LOSS_HALT           = 8,
    CONSEC_LOSS_HALT          = 9,
    INSUFFICIENT_BANKROLL     = 10,

    // 信号
    EDGE_CI_NEGATIVE          = 11,
    EDGE_NEGATED_BY_SLIPPAGE  = 12,

    // 市场
    MARKET_TYPE_NOT_ENABLED   = 13,
    MARKET_NOT_ACTIVE         = 14,

    // 流动性 / 滑点 (小肖 v1)
    LOW_FILL_RATE             = 15,
    EXCESSIVE_SLIPPAGE        = 16,
    EXCEED_BOOK_DEPTH         = 17,

    // 系统
    AUDIT_WAL_BACKPRESSURE    = 18,
    STRATEGY_DECAYED          = 19,  // 老韩 v0.3 §16, OQ-D13 Bayesian kill switch
    INTERNAL_ERROR            = 20,

    // 总数: 21 (老郭 ADR-003 C-4 不增)
};

// INVALID_INTENT 子原因 (老韩 v0.3.1 + 小肖 v1 §2)
enum class InvalidIntentSubReason : std::uint8_t {
    NONE                = 0,
    BOOK_TS_ZERO        = 1,  // book_snapshot_ts_ns == 0
    BOOK_TS_STALE       = 2,  // book_snapshot_ts_ns < now - 60s
    NAN_OR_INF          = 3,
    NEGATIVE            = 4,
    ILLEGAL_TICK        = 5,
    // 老孙 v5.1 IPC 4 ts (R-20)
    TS_ORDER_VIOLATED   = 6,
    TS_FUTURE           = 7,
    TS_UNKNOWN_SRC      = 8,
};

struct RejectDetail {
    RejectCode code;
    InvalidIntentSubReason sub_reason{InvalidIntentSubReason::NONE};
    // invariant (老韩 v0.3.1): code != INVALID_INTENT ⟹ sub_reason == NONE
};

}  // namespace stcpp::risk
