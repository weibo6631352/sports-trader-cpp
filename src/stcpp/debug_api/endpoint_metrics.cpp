// src/stcpp/debug_api/endpoint_metrics.cpp — GET /metrics (ADR-038 §4.4 / §3)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联:
//   ADR-038 §4.4 Prometheus: 健康(uptime/wss/reconnect/loop p99)
//             + 业务(rm_decision/fill/net_edge/pnl) + 数据质量(staleness/gap/drift)
//   §3 铁律: metric 名 + label key 一次定对, 低基数 — 禁 market_id/intent_id 当 label
//   §5: /metrics 裸前缀, 无版本 (Prometheus 惯例)
//
// 输出: Prometheus text exposition format (text/plain; version=0.0.4)。
// 只读: provider.metrics()。MVP stub 返回 0/false (结构合法可被 Prometheus pull)。
//
// 低基数原则: 全部 metric 无 per-market/per-order label; wss 连接用枚举 label
//   channel="sports_api|clob|user" (固定 3 值, 低基数)。mode 作为低基数 label 附在每条。

#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

namespace {

void metric_line(std::string& out, const char* name, const char* type, const char* help,
                 const std::string& value_with_labels) {
    out += "# HELP ";
    out += name;
    out += ' ';
    out += help;
    out += '\n';
    out += "# TYPE ";
    out += name;
    out += ' ';
    out += type;
    out += '\n';
    out += name;
    out += value_with_labels;
    out += '\n';
}

}  // namespace

void register_metrics(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/metrics", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const MetricsSnapshot m = sp.metrics();
        const char* mode = exec_mode_str(sp.mode());

        // mode label (低基数: paper|live|backtest)
        std::string ml = "{mode=\"";
        ml += mode;
        ml += "\"}";

        std::string out;
        out.reserve(2048);

        // ---- 健康 ----
        metric_line(out, "stcpp_uptime_seconds", "gauge", "Process uptime in seconds",
                    ml + " " + json::i64(m.uptime_sec));

        // wss 连接: channel 枚举 label (低基数, 固定 3 值)
        out += "# HELP stcpp_wss_connected WSS channel connected (1=up 0=down)\n";
        out += "# TYPE stcpp_wss_connected gauge\n";
        out += "stcpp_wss_connected{mode=\"";
        out += mode;
        out += "\",channel=\"sports_api\"} ";
        out += (m.wss_sports_api_connected ? "1" : "0");
        out += '\n';
        out += "stcpp_wss_connected{mode=\"";
        out += mode;
        out += "\",channel=\"clob\"} ";
        out += (m.wss_clob_connected ? "1" : "0");
        out += '\n';
        out += "stcpp_wss_connected{mode=\"";
        out += mode;
        out += "\",channel=\"user\"} ";
        out += (m.wss_user_channel_connected ? "1" : "0");
        out += '\n';

        metric_line(out, "stcpp_wss_reconnect_total", "counter", "Total WSS reconnects",
                    ml + " " + json::i64(m.wss_reconnect_total));
        metric_line(out, "stcpp_loop_latency_p99_us", "gauge", "Hot loop p99 latency microseconds",
                    ml + " " + json::num(m.loop_p99_us));

        // ---- 业务 ----
        metric_line(out, "stcpp_rm_decision_total", "counter", "Total RM decisions",
                    ml + " " + json::i64(m.rm_decision_total));
        metric_line(out, "stcpp_rm_reject_total", "counter", "Total RM rejects",
                    ml + " " + json::i64(m.rm_reject_total));
        metric_line(out, "stcpp_fill_total", "counter", "Total fills", ml + " " + json::i64(m.fill_total));
        metric_line(out, "stcpp_net_edge_bps", "gauge", "Net edge basis points",
                    ml + " " + json::num(m.net_edge_bps));
        metric_line(out, "stcpp_cum_net_pnl", "gauge", "Cumulative net PnL",
                    ml + " " + json::num(m.cum_net_pnl));

        // ---- 数据质量 ----
        metric_line(out, "stcpp_data_staleness_ms_max", "gauge", "Max feed staleness milliseconds",
                    ml + " " + json::num(m.max_staleness_ms));
        metric_line(out, "stcpp_feed_gap_total", "counter", "Total detected feed sequence gaps",
                    ml + " " + json::i64(m.feed_gap_total));
        metric_line(out, "stcpp_price_drift_bps", "gauge", "Price drift vs reference basis points",
                    ml + " " + json::num(m.price_drift_bps));

        // ---- 订阅计数 (GAP-01/02/03, 低基数 mode label only; ADR-038 §3) ----
        // stcpp_subscribed_tokens_total   — hub_.token_count() (per-token slot 数)
        // stcpp_subscribed_markets_total  — tokens / 2 (双 token 规则, 老李 spec §2.1)
        // stcpp_subscribed_user_conditions_total — user channel condition_id 数
        metric_line(out, "stcpp_subscribed_tokens_total", "gauge",
                    "Number of subscribed CLOB market tokens (hub token_count)",
                    ml + " " + json::i64(m.subscribed_tokens_total));
        metric_line(out, "stcpp_subscribed_markets_total", "gauge",
                    "Number of subscribed markets (subscribed_tokens / 2, binary market rule)",
                    ml + " " + json::i64(m.subscribed_markets_total));
        metric_line(out, "stcpp_subscribed_user_conditions_total", "gauge",
                    "Number of subscribed user channel condition_ids",
                    ml + " " + json::i64(m.subscribed_user_conditions));

        // ---- 小段 score-mapping 统计 (2026-05-30) ----
        // score_matched / score_attempted / score_match_rate
        // 来源: ScoreEventMapper.LastStats() (最近 Refresh 周期)
        // 0/0/0.0 = mapper 未注入 或 无 PM event 待匹配 (outright/futures 期均为 0)
        metric_line(out, "stcpp_score_matched_total", "gauge",
                    "Goalserve inplay events matched to Polymarket events (last Refresh cycle)",
                    ml + " " + json::i64(m.score_matched_total));
        metric_line(out, "stcpp_score_attempted_total", "gauge",
                    "Polymarket events attempted for score matching (last Refresh cycle)",
                    ml + " " + json::i64(m.score_attempted_total));
        metric_line(out, "stcpp_score_match_rate", "gauge",
                    "Score match rate [0.0,1.0] (matched/attempted, last Refresh cycle)",
                    ml + " " + json::num(m.score_match_rate));

        res.set_content(out, "text/plain; version=0.0.4; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
