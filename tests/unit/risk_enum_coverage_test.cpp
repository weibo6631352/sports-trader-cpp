// tests/unit/risk_enum_coverage_test.cpp — 21 reject enum × 1 placeholder unit case
// Owner: 小宋  W3 Wave 18
// CI grep: tests/ci_grep/risk_enum_coverage.py 扫本文件, 缺一 enum exit 1
// W4+ : 老韩 commit 真 RM 后, fixture 切真路径 (本文件 case 不变)

#include "tests/unit/risk_manager_fixture.hpp"

namespace stcpp::test {

using risk::InvalidIntentSubReason;
using risk::RejectCode;

#define CASE(name, code)                            \
    TEST_F(RiskManagerFixture, name) {              \
        gw_->prime_reject(#name, RejectCode::code); \
        auto it = make_intent_pit_ok(#name);        \
        expect_reject(it, RejectCode::code);        \
    }

// 状态机
CASE(STATE_HALTED, STATE_HALTED)
CASE(STATE_DRAIN, STATE_DRAIN)
CASE(STATE_SAFE_MODE, STATE_SAFE_MODE)
// 重复 / 数据
CASE(DUPLICATE_INTENT, DUPLICATE_INTENT)
CASE(STALE_DATA, STALE_DATA)
// 仓位 / 资金
CASE(EXCEED_PER_ORDER_CAP, EXCEED_PER_ORDER_CAP)
CASE(EXCEED_MARKET_EXPOSURE, EXCEED_MARKET_EXPOSURE)
CASE(DAILY_LOSS_HALT, DAILY_LOSS_HALT)
CASE(CONSEC_LOSS_HALT, CONSEC_LOSS_HALT)
CASE(INSUFFICIENT_BANKROLL, INSUFFICIENT_BANKROLL)
// 信号
CASE(EDGE_CI_NEGATIVE, EDGE_CI_NEGATIVE)
CASE(EDGE_NEGATED_BY_SLIPPAGE, EDGE_NEGATED_BY_SLIPPAGE)
// 市场
CASE(MARKET_TYPE_NOT_ENABLED, MARKET_TYPE_NOT_ENABLED)
CASE(MARKET_NOT_ACTIVE, MARKET_NOT_ACTIVE)
// 流动性 / 滑点 (小肖 v1)
CASE(LOW_FILL_RATE, LOW_FILL_RATE)
CASE(EXCESSIVE_SLIPPAGE, EXCESSIVE_SLIPPAGE)
CASE(EXCEED_BOOK_DEPTH, EXCEED_BOOK_DEPTH)
// 系统
CASE(AUDIT_WAL_BACKPRESSURE, AUDIT_WAL_BACKPRESSURE)
CASE(STRATEGY_DECAYED, STRATEGY_DECAYED)  // 老韩 v0.3 §16 OQ-D13
CASE(INTERNAL_ERROR, INTERNAL_ERROR)

#undef CASE

// INVALID_INTENT 单独 case: 验证 sub_reason 联签 (老韩 v0.3.1)
TEST_F(RiskManagerFixture, INVALID_INTENT_with_sub_reason_TS_ORDER_VIOLATED) {
    gw_->prime_reject("s_inv", RejectCode::INVALID_INTENT, InvalidIntentSubReason::TS_ORDER_VIOLATED);
    auto it = make_intent_pit_violated();
    it.strategy_id = "s_inv";
    expect_reject(it, RejectCode::INVALID_INTENT);
}

}  // namespace stcpp::test
