// src/stcpp/debug_api/endpoint_payloads.hpp — 通道 JSON payload 共享 builder
// Owner: 老雷 (GM)  2026-06-02 (SSE 推送 v1 配套)
//
// 目的: SSE /stream 各通道的 data 序列化, 字段与对应 REST 端点【严格对齐】(前端同一套 type 解析)。
//
// ⚠ 技术债: 现阶段这些 builder 的字段是从各 endpoint_*.cpp 的内联序列化【复制对齐】而来
//   (REST 端点暂未改造为调用本 builder, 避免一次性改 8 个在跑的端点)。
//   → 改某端点字段时, 必须同步改这里对应 builder (否则 SSE 与 REST 漂移)。
//   后续可把各 endpoint 改为调用本 builder 以彻底去重 (低风险机械重构, 留待 SSE 稳定后)。
//
// 约束: 纯只读 (R-12) — 仅读 const StateProvider 快照 + json_writer 拼装, 不碰热路径。
//       返回的 JSON 顶层含 mode (R-11) + as_of_ts epoch_ns (R-20, 仅快照读取时刻)。
//       数据新鲜度由各 payload 内的上游 event_ts/data_source_ts 透传 (R-20: 禁本地 now 替代上游)。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api::payload {

// ---- status (= GET /status) ----
inline std::string status(const HttpServer& hs) {
    const StateProvider& sp = hs.provider();
    const MetricsSnapshot m = sp.metrics();
    const std::int64_t uptime = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - hs.start_time()).count());
    std::string b;
    b.reserve(512);
    b += R"({"state":"RUNNING","mode":")";
    b += STCPP_EXEC_MODE_STR;
    b += R"(","wss_connected":{"sports_api":)";
    b += json::boolean(m.wss_sports_api_connected);
    b += R"(,"clob":)";
    b += json::boolean(m.wss_clob_connected);
    b += R"(,"user_channel":)";
    b += json::boolean(m.wss_user_channel_connected);
    b += R"(},"signals_active_count":0,"positions_count":0,"rm_rejects_last_60s":0,"uptime_sec":)";
    b += json::i64(uptime);
    b += R"(,"as_of_ts":)";
    b += json::i64(now_epoch_ns());
    b += R"(,"data_source":")";
    b += sp.data_source();
    b += "\"}";
    return b;
}

// ---- account (= GET /api/v1/account) ----
inline std::string account(const StateProvider& sp) {
    const AccountSnapshot a = sp.account_snapshot();
    std::string b;
    b.reserve(640);
    b += "{\"mode\":";
    b += json::str(a.mode.empty() ? exec_mode_str(sp.mode()) : a.mode);
    b += ",\"has_data\":";
    b += json::boolean(a.has_data);
    b += ",\"as_of_ts\":";
    b += json::i64(a.as_of_ts_ns);
    b += ",\"account\":{\"bankroll_initial\":";
    b += json::num(a.bankroll_initial);
    b += ",\"cash_available\":";
    b += json::num(a.cash_available);
    b += ",\"position_mtm\":";
    b += json::num(a.position_mtm);
    b += ",\"equity\":";
    b += json::num(a.equity_mark);
    b += ",\"equity_conservative\":";
    b += json::num(a.equity_conservative);
    b += ",\"cum_realized_pnl\":";
    b += json::num(a.cum_realized_pnl);
    b += ",\"cum_unrealized_pnl\":";
    b += json::num(a.cum_unrealized_pnl);
    b += ",\"cum_fee_paid\":";
    b += json::num(a.cum_fee_paid);
    b += ",\"net_pnl\":";
    b += json::num(a.net_pnl);
    b += ",\"return_pct\":";
    b += json::num(a.return_pct);
    b += ",\"max_drawdown\":";
    b += json::num(a.max_drawdown);
    b += ",\"sharpe\":";
    b += json::num(a.sharpe);
    b += ",\"kelly_bankroll\":";
    b += json::num(a.kelly_bankroll);
    b += ",\"kelly_bankroll_basis\":";
    b += json::str(a.kelly_bankroll_basis);
    b += ",\"open_positions\":";
    b += json::i64(a.open_positions);
    b += "}}";
    return b;
}

// ---- events (= GET /api/v1/events) ----
inline std::string events(const StateProvider& sp) {
    const std::vector<EventInfo> evs = sp.events();
    std::string b;
    b.reserve(512 + evs.size() * 256);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    b += ",\"as_of_ts\":";
    b += json::i64(now_epoch_ns());
    b += ",\"data_source\":";
    b += json::str(sp.data_source());
    b += ",\"events\":[";
    for (std::size_t i = 0; i < evs.size(); ++i) {
        if (i) b += ',';
        const EventInfo& ev = evs[i];
        b += "{\"event_id\":";
        b += json::str(ev.event_id);
        b += ",\"slug\":";
        b += json::str(ev.slug);
        b += ",\"title\":";
        b += json::str(ev.title);
        b += ",\"sport\":";
        b += json::str(ev.sport);
        b += ",\"neg_risk_market_id\":";
        b += json::str(ev.neg_risk_market_id);
        b += ",\"live\":";
        b += json::boolean(ev.live);
        b += ",\"condition_ids\":[";
        for (std::size_t j = 0; j < ev.condition_ids.size(); ++j) {
            if (j) b += ',';
            b += json::str(ev.condition_ids[j]);
        }
        b += "]}";
    }
    b += "]}";
    return b;
}

// ---- positions (= GET /api/v1/positions) ----
inline std::string positions(const StateProvider& sp) {
    const std::vector<HoldingView> rows = sp.positions();
    std::string b;
    b.reserve(256 + rows.size() * 256);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    b += ",\"as_of_ts\":";
    b += json::i64(now_epoch_ns());
    b += ",\"positions\":[";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const HoldingView& r = rows[i];
        if (i) b += ',';
        b += "{\"market_id\":";
        b += json::str(r.market_id);
        b += ",\"outcome\":";
        b += json::str(r.outcome);
        b += ",\"net_qty\":";
        b += json::num(r.net_qty);
        b += ",\"avg_entry_price\":";
        b += json::num(r.avg_entry_price);
        b += ",\"mark_price\":";
        b += json::num(r.mark_price);
        b += ",\"pnl_realized\":";
        b += json::num(r.pnl_realized);
        b += ",\"pnl_unrealized\":";
        b += json::num(r.pnl_unrealized);
        b += ",\"as_of_ts\":";
        b += json::i64(r.as_of_ts_ns);
        b += '}';
    }
    b += "]}";
    return b;
}

// ---- pnl attribution (= GET /api/v1/pnl/attribution) ----
inline std::string pnl_attribution(const StateProvider& sp) {
    const PnlAttribution a = sp.pnl_attribution();
    std::string b;
    b.reserve(256);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    b += ",\"as_of_ts\":";
    b += json::i64(now_epoch_ns());
    b += ",\"waterfall\":{\"gross\":";
    b += json::num(a.gross);
    b += ",\"fee\":";
    b += json::num(a.fee);
    b += ",\"gas\":";
    b += json::num(a.gas);
    b += ",\"slippage\":";
    b += json::num(a.slippage);
    b += ",\"spread\":";
    b += json::num(a.spread);
    b += ",\"net\":";
    b += json::num(a.net);
    b += "},\"per_market\":[";
    for (std::size_t i = 0; i < a.per_market.size(); ++i) {
        if (i) b += ',';
        b += "{\"market_id\":";
        b += json::str(a.per_market[i].market_id);
        b += ",\"net_pnl\":";
        b += json::num(a.per_market[i].net_pnl);
        b += '}';
    }
    b += "]}";
    return b;
}

// ---- gate (= GET /api/v1/gate/paper) ----
inline std::string gate(const StateProvider& sp) {
    const PaperGate g = sp.paper_gate();
    std::string b;
    b.reserve(384);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    b += ",\"as_of_ts\":";
    b += json::i64(now_epoch_ns());
    b += ",\"window_days\":";
    b += json::i64(g.window_days);
    b += ",\"has_data\":";
    b += json::boolean(g.has_data);
    b += ",\"n_trades\":";
    b += json::i64(g.n_trades);
    b += ",\"positive_day_ratio\":";
    b += json::num(g.positive_day_ratio);
    b += ",\"sharpe\":";
    b += json::num(g.sharpe);
    b += ",\"sharpe_se\":";
    b += json::num(g.sharpe_se);
    b += ",\"p_value\":";
    b += json::num(g.p_value);
    b += ",\"hit_rate\":";
    b += json::num(g.hit_rate);
    b += ",\"max_drawdown\":";
    b += json::num(g.max_drawdown);
    b += ",\"prelim_pass\":";
    b += json::boolean(g.prelim_pass);
    b += ",\"confirm_pass\":";
    b += json::boolean(g.confirm_pass);
    b += '}';
    return b;
}

// ---- rejects (= GET /api/v1/risk/rejects) ----
inline std::string rejects(const StateProvider& sp) {
    const std::vector<RiskRejectRow> rows = sp.risk_rejects();
    std::string b;
    b.reserve(256 + rows.size() * 192);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    b += ",\"as_of_ts\":";
    b += json::i64(now_epoch_ns());
    b += ",\"rejects\":[";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const RiskRejectRow& r = rows[i];
        if (i) b += ',';
        b += "{\"reason_code\":";
        b += json::str(r.reason_code);
        b += ",\"market_id\":";
        b += json::str(r.market_id);
        b += ",\"intent_ref\":";
        b += json::str(r.intent_ref);
        b += ",\"side\":";
        b += json::str(r.side);
        b += ",\"size\":";
        b += json::num(r.size);
        b += ",\"price\":";
        b += json::num(r.price);
        b += ",\"rejected_ts\":";
        b += json::i64(r.rejected_ts_ns);
        b += '}';
    }
    b += "]}";
    return b;
}

// ---- grid (= GET /api/v1/grid) — 全市场顶档摘要 ----
inline std::string grid(const StateProvider& sp) {
    const std::vector<EventInfo> evs = sp.events();
    std::string b;
    b.reserve(64 * 1024);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    b += ",\"as_of_ts\":";
    b += json::i64(now_epoch_ns());
    b += ",\"markets\":[";
    std::size_t count = 0;
    bool first = true;
    // 注: 与 endpoint_grid 共享同一逻辑; 去重交由 condition 天然单一归属 (防御性此处不再 set)
    for (const auto& ev : evs) {
        for (const auto& cid : ev.condition_ids) {
            if (cid.empty()) continue;
            const BinaryMarketBookView bv = sp.book_pair(cid);
            const QuoteParams q = sp.quote_params(cid);
            if (!first) b += ',';
            first = false;
            ++count;
            b += "{\"condition_id\":";
            b += json::str(cid);
            b += ",\"book_found\":";
            b += json::boolean(bv.token0.found);
            if (bv.token0.found) {
                b += ",\"best_bid\":";
                b += json::num(bv.token0.best_bid);
                b += ",\"best_ask\":";
                b += json::num(bv.token0.best_ask);
                b += ",\"cross_spread\":";
                b += json::num(bv.cross_spread);
                b += ",\"event_ts\":";
                b += json::i64(bv.token0.ts.event_ts_ns);
                b += ",\"ingestion_ts\":";
                b += json::i64(bv.token0.ts.ingestion_ts_ns);
            }
            b += ",\"quote_found\":";
            b += json::boolean(q.found);
            if (q.found) {
                b += ",\"fair\":";
                b += json::num(q.fair_value);
                b += ",\"market_mid\":";
                b += json::num(q.market_mid);
                b += ",\"edge_bps\":";
                b += json::num(q.edge_bps);
                b += ",\"sharp_fair\":";
                b += json::num(q.sharp_fair);
                b += ",\"model_confidence\":";
                b += json::num(q.model_confidence);
                b += ",\"advisory\":";
                b += json::boolean(q.advisory);
            }
            b += '}';
        }
    }
    b += "],\"count\":";
    b += json::i64(static_cast<std::int64_t>(count));
    b += '}';
    return b;
}

// ---- scores (SSE 专用: live 赛事比分数组; REST 是 per-event /api/v1/score/{id}) ----
//   字段与 endpoint_score 单条一致, 包成数组供 SSE scores 通道。
inline std::string scores(const StateProvider& sp) {
    const std::vector<EventInfo> evs = sp.events();
    std::string b;
    b.reserve(2048);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    b += ",\"as_of_ts\":";
    b += json::i64(now_epoch_ns());
    b += ",\"scores\":[";
    bool first = true;
    for (const auto& ev : evs) {
        if (!ev.live) continue;  // 只推 live 赛事比分
        const EventScore s = sp.score(ev.event_id);
        if (!s.found) continue;
        if (!first) b += ',';
        first = false;
        b += "{\"event_id\":";
        b += json::str(s.event_id);
        b += ",\"sport\":";
        b += json::str(s.sport);
        b += ",\"status\":";
        b += json::str(s.status);
        b += ",\"period\":";
        b += json::str(s.period);
        b += ",\"clock_sec\":";
        b += json::i64(s.clock_sec);
        b += ",\"home\":";
        b += json::str(s.home);
        b += ",\"away\":";
        b += json::str(s.away);
        b += ",\"home_score\":";
        b += json::i64(s.home_score);
        b += ",\"away_score\":";
        b += json::i64(s.away_score);
        b += ",\"source\":";
        b += json::str(s.source);
        b += ",\"event_ts\":";
        b += json::i64(s.ts.event_ts_ns);
        b += ",\"data_source_ts\":";
        b += json::i64(s.ts.data_source_ts_ns);
        b += ",\"ingestion_ts\":";
        b += json::i64(s.ts.ingestion_ts_ns);
        b += ",\"found\":true}";
    }
    b += "]}";
    return b;
}

}  // namespace stcpp::debug_api::payload
