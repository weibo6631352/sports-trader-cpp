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
//   channel="clob|user" (低基数; sports_api 通道已删 2026-06-02 — Goalserve 走 HTTP REST 非 WSS)。mode 附每条。

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

        // wss 连接: channel 枚举 label (低基数; clob 实用 / user 预留)
        out += "# HELP stcpp_wss_connected WSS channel connected (1=up 0=down)\n";
        out += "# TYPE stcpp_wss_connected gauge\n";
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
        // 韧性 watchdog: loop_thread_ 心跳停摆 (老郭). 正常 ≈ tick 间隔内; 飙升 = loop 卡死. -1 = 未跑过.
        metric_line(out, "stcpp_loop_tick_staleness_ms", "gauge", "loop_thread tick heartbeat staleness ms",
                    ml + " " + json::num(m.loop_tick_staleness_ms));
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

        // ---- 覆盖率/识别率 metric (ADR-038 小卢 2026-05-30; 低基数 mode label only) ----
        //
        // 1. 盘口类型识别率 (从 market catalog 算)
        //    recognized: sports_market_type 非空非 "unknown"
        //    unknown:    sports_market_type == "" 或 "unknown"
        //    派生率 = recognized / (recognized + unknown) (由 ops 层 PromQL 算)
        //    当前 outright 类 sportsMarketType 字段缺失 → unknown 桶非零, 识别率 < 100%
        metric_line(out, "stcpp_market_type_recognized_total", "gauge",
                    "Markets in catalog with recognized sports_market_type (non-empty, non-unknown)",
                    ml + " " + json::i64(m.market_type_recognized_total));
        metric_line(out, "stcpp_market_type_unknown_total", "gauge",
                    "Markets in catalog with unrecognized sports_market_type (empty or 'unknown')",
                    ml + " " + json::i64(m.market_type_unknown_total));

        // 2. 市场覆盖 (从 catalog + hub 算)
        //    markets_discovered_total  — gamma /events 发现并入 catalog 的市场总数
        //    markets_subscribed_total  (上方已输出) — hub 双 token 口径
        //    tokens_subscribed_total   (上方已输出) — hub token_count 口径
        //    覆盖率 = subscribed_markets / discovered (PromQL 算)
        metric_line(out, "stcpp_markets_discovered_total", "gauge",
                    "Total markets discovered from gamma /events and loaded into catalog",
                    ml + " " + json::i64(m.markets_discovered_total));

        // 3. 直播员/比分匹配率 (从 ScoreSnapshotStore + catalog 算)
        //    score_matched_total — catalog 中能在 score_store 找到对应 event_id 比分的 condition 数
        //    匹配率 = score_matched / markets_discovered (PromQL 算)
        //    当前 outright 无 inplay → score_matched 偏低 (诚实暴露)
        metric_line(out, "stcpp_score_matched_total", "gauge",
                    "Conditions in catalog whose event_id has a live Goalserve score snapshot",
                    ml + " " + json::i64(m.score_matched_total));
        // markets_live: gamma live=true 的市场数 = 比分匹配率真分母 (老板 2026-06-02: 直播比分只覆盖
        //   in-play, 匹配率 = score_matched / markets_live, 不是 / markets_discovered 全市场)
        metric_line(out, "stcpp_markets_live_total", "gauge",
                    "Markets in catalog flagged live=true (game in-play) — denominator for score match rate",
                    ml + " " + json::i64(m.markets_live_total));

        res.set_content(out, "text/plain; version=0.0.4; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
