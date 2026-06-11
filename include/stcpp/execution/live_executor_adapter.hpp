// include/stcpp/execution/live_executor_adapter.hpp — IOrderExecutor 缝上的 live 适配
//
// owner: 老雷 (GM) | last_review: 2026-06-12 (实盘准备 v1 缺口 #5: 单参数切换核心件)
// 链: trading_loop executor_ 缝 (VirtualOrder) → 本适配 → LiveExecutor (LiveOrderGate fail-closed
//     → OrderSinkFn = LiveOrderSubmitter::Submit) → ExecReport → VirtualFill (mode_tag=1)。
// 安全: gate 默认 disarmed → 任何 Execute 都拿不到成交 (reject=InvalidConfig), fail-closed;
//      Arm() 仅老板同意后 (LIVE_ARMED=1, 2026-06-12 老板「到时候我同意就行」)。
#pragma once

#include <cmath>
#include <string>

#include "stcpp/execution/order_executor.hpp"
#include "stcpp/polymarket/live_executor.hpp"
#include "stcpp/risk/risk_gateway.hpp"

namespace stcpp::polymarket {

class LiveExecutorAdapter final : public execution::IOrderExecutor {
 public:
    explicit LiveExecutorAdapter(LiveExecutor& live) noexcept : live_(live) {}

    [[nodiscard]] execution::VirtualFill Execute(const execution::VirtualOrder& o) noexcept override {
        execution::VirtualFill f;
        f.mode_tag = 1;  // live (paper=0; R-11 调用方按编译模式断言)
        f.audit_wal_kind = infra::wal::WalKind::RiskAudit;
        // 4ts 透传 (R-20)
        f.event_ts_ns = o.event_ts_ns;
        f.data_source_ts_ns = o.data_source_ts_ns;
        f.ingestion_ts_ns = o.ingestion_ts_ns;
        f.as_of_ts_ns = o.as_of_ts_ns;

        risk::OrderIntent it;
        it.condition_id = std::string(o.market_id);
        it.token_id = std::string(o.token_id);
        it.side = o.is_buy ? strategy::Side::Buy : strategy::Side::Sell;
        it.outcome = (o.outcome == "YES") ? strategy::Outcome::Yes : strategy::Outcome::No;
        it.price = o.quote_price;
        it.size_pUSD_micro = static_cast<std::int64_t>(std::llround(o.size_usdc * 1'000'000.0));
        it.book_depth_l1_usdc = o.book_depth_l1_usdc * 1'000'000.0;
        it.tick_size = o.tick_size;
        it.event_ts_ns = o.event_ts_ns;
        it.data_source_ts_ns = o.data_source_ts_ns;
        it.ingestion_ts_ns = o.ingestion_ts_ns;
        it.as_of_ts_ns = o.as_of_ts_ns;
        it.timestamp_ms = o.as_of_ts_ns / 1'000'000LL;

        const ExecReport r = live_.Execute(it, o.neg_risk);
        if (!r.submitted) {
            // gate 拦 (disarmed/ratecap/RM) 或提交失败 → 不可重试语义 (调用方计 missed, 不重试轰炸)
            f.reject = execution::MatchReject::InvalidConfig;
            return f;
        }
        if (!r.filled || r.filled_shares <= 0.0) {
            f.reject = execution::MatchReject::BernoulliMissed;  // FOK 未成交 → 可重试语义
            return f;
        }
        f.reject = execution::MatchReject::Ok;
        f.fill_price = r.fill_price;
        f.fill_size_usdc = static_cast<std::int64_t>(std::llround(r.filled_shares * 1'000'000.0));
        f.expected_fill_rate = 1.0;
        return f;
    }

 private:
    LiveExecutor& live_;
};

}  // namespace stcpp::polymarket
