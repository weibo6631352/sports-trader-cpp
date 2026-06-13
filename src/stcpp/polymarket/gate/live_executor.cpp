// src/stcpp/polymarket/gate/live_executor.cpp — 实盘执行器
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 接线。
#include "stcpp/polymarket/live_executor.hpp"

#include <cstdio>

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
    // 2026-06-13 硬化: 无条件回填 CLOB 回执诊断 (此前只在 matched 时填 → daemon 对 400 失明 → 风暴根因之一)。
    rep.http_status = gr.order.http_status;
    rep.clob_status = gr.order.status;
    rep.clob_error = gr.order.error;

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
    } else if (gr.submitted) {
        // 真触达但同步回执未成交。三态区分 (2026-06-13 据实修, 抓到真回执定性):
        //   ① status="delayed" = Polymarket【异步撮合延迟】: 订单已被接受(success:true)但撮合异步,
        //      同步回执 making/takingAmount 为空 (此刻不知成交量) → 真成交稍后由 WSS user 频道 CONFIRMED
        //      送达入账。【物理上无法同步入账】(CLOB 没回量), 非 miss 非错 → 不重试, 待 WSS (调用方 #3 冷却)。
        //   ② 硬拒 4xx (精度/最小额/余额): 重发必再拒 → 非重试 (冷却)。
        //   ③ FOK 无对手 (unmatched 等): 可重试。
        rep.hard_reject = (gr.order.http_status >= 400) || (!gr.order.success && !gr.order.error.empty());
        rep.order_id = gr.order.order_id;  // 2026-06-14: delayed/未成交也回传 order_id → 调用方 stash 决策上下文,
                                           //   WSS 该 order CONFIRMED 时取出富化入账 (live 行与 paper 一样富)。
        if (rep.hard_reject) {
            std::fprintf(stderr, "[live_exec] ⚠ CLOB 硬拒 http=%d err=\"%s\" token=%s side=%s → 非重试(冷却)\n",
                         gr.order.http_status, gr.order.error.c_str(), intent.token_id.c_str(),
                         intent.side == stcpp::risk::Side::Buy ? "BUY" : "SELL");
        } else if (gr.order.status == "delayed") {
            std::fprintf(stderr,
                         "[live_exec] FOK 异步撮合 (status=delayed, Polymarket 撮合延迟) → 同步回执无成交量, "
                         "待 WSS user 频道 CONFIRMED 入账 (order=%s token=%s)\n",
                         gr.order.order_id.c_str(), intent.token_id.c_str());
        }
    }
    return rep;
}

}  // namespace stcpp::polymarket
