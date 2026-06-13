// stcpp/risk/position_view.hpp — PositionView v0.1 (老沈 Wave 76 W9 W4)
//
// 落: laohan-w9-w3-position-ledger-rest-api-spec-v1.md §2
//
// 红线:
//   R-20 last_update_ts 严格透传 VirtualFill.as_of_ts_ns, 禁 now()
//   ADR-027 cite block 见 position_ledger.hpp
//
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3.3
//   goalserve_ssot_cite:  N/A
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3
//   adr_cite:             ADR-027 Enforce-1

#pragma once

#include <cstdint>
#include <string>

#include "stcpp/strategy/signal_iface.hpp"  // Outcome enum

namespace stcpp::risk {

using stcpp::strategy::Outcome;

// PositionView — 只读快照 (老韩 spec §2 字段表)
// net_shares_micro: signed 净持仓【股数】micro; 正 = 多仓, 负 = 空仓 (平仓为 0)。/1e6 = whole shares。
//   2026-06-13 单位根治正名 (原误名 size_usdc): 持仓本位=股数, 不是 USD。USD 名义 (cap/sizing/equity)
//   一律由 net_shares_micro × price 导出 (见 PositionLedger::get_*_notional)。
// avg_entry_price: ∈ (0, 1), 加权均值
// last_update_ts: 严格透传 VirtualFill.as_of_ts_ns (R-20 红线, **禁 now()**)
struct PositionView {
    std::string condition_id;  // bytes32 hex (0x 前缀, 66 char)
    std::string token_id;      // uint256 string (无 0x, 十进制, ≤78 位 = 2^256-1 位数)
    Outcome outcome{Outcome::Yes};
    std::int64_t net_shares_micro{0};  // signed 净持仓股数 micro (本位; /1e6 = whole shares)
    double avg_entry_price{0.0};
    std::int64_t last_update_ts{0};  // = VirtualFill.as_of_ts_ns (R-20)
};

}  // namespace stcpp::risk
