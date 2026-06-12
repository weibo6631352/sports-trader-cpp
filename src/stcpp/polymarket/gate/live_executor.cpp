// src/stcpp/polymarket/gate/live_executor.cpp — 实盘执行器
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 接线。
#include "stcpp/polymarket/live_executor.hpp"

namespace stcpp::polymarket {

ExecReport LiveExecutor::Execute(const stcpp::risk::OrderIntent& intent, bool neg_risk) noexcept {
    ExecReport rep;
    // R-20: 透传 intent 上游 4ts (非 now() 替代上游 ts; carve-out C-1)。
    rep.event_ts_ns = intent.event_ts_ns;
    rep.data_source_ts_ns = intent.data_source_ts_ns;
    rep.ingestion_ts_ns = intent.ingestion_ts_ns;
    rep.as_of_ts_ns = intent.as_of_ts_ns;
    const GateResult gr = gate_.Submit(intent, neg_risk);
    rep.gate_block = gr.gate_block;
    rep.submitted = gr.submitted;

    // FOK: 只有 status=="matched" 才算成交 (老周 C-3); 否则不写账本。
    if (gr.submitted && gr.order.success && gr.order.status == "matched") {
        rep.filled = true;
        const bool is_buy = (intent.side == stcpp::risk::Side::Buy);
        // BUY : making=USDC 实付, taking=shares 实得; SELL: making=shares 实卖, taking=USDC 实得。
        rep.filled_usdc = is_buy ? gr.order.making_amount : gr.order.taking_amount;
        rep.filled_shares = is_buy ? gr.order.taking_amount : gr.order.making_amount;
        rep.fill_price = (rep.filled_shares > 0.0) ? rep.filled_usdc / rep.filled_shares : 0.0;
        rep.order_id = gr.order.order_id;
        rep.tx_hash = gr.order.transaction_hash;
    }
    return rep;
}

}  // namespace stcpp::polymarket
