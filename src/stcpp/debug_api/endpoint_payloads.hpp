// src/stcpp/debug_api/endpoint_payloads.hpp — 通道 JSON payload 共享 builder
// Owner: 老雷 (GM)  2026-06-02 (SSE 推送 v1 配套)
//
// 目的: 各观测数据的【唯一序列化数据源】。REST 端点 (endpoint_status/account/events/
//   positions/pnl/gate/risk/grid.cpp) 与 SSE /stream 各通道【都调用本 builder】, 无重复、不漂移。
//   - REST 调用传 as_of_ns = now_epoch_ns() → 输出顶层 as_of_ts (保持 REST 契约)。
//   - SSE 调用用默认 as_of_ns = -1 → 省略顶层 as_of_ts (信封已带 transport ts;
//     且让 on-change 字符串比对生效, 不因 as_of 每次变而误判"变化")。
//   - 改任何字段只改这里一处, REST 与 SSE 自动一致。
//
// 约束: 纯只读 (R-12) — 仅读 const StateProvider 快照 + json_writer 拼装, 不碰热路径。
//       返回的 JSON 顶层含 mode (R-11) + as_of_ts epoch_ns (R-20, 仅快照读取时刻)。
//       数据新鲜度由各 payload 内的上游 event_ts/data_source_ts 透传 (R-20: 禁本地 now 替代上游)。

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api::payload {

// ---- status (= GET /status) ----
// as_of_ns: >=0 → 输出顶层 as_of_ts (REST 传 now); <0 → 省略 (SSE: 信封已带 transport ts)
inline std::string status(const HttpServer& hs, std::int64_t as_of_ns = -1) {
    const StateProvider& sp = hs.provider();
    const MetricsSnapshot m = sp.metrics();
    const std::int64_t uptime = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - hs.start_time()).count());
    std::string b;
    b.reserve(512);
    b += R"({"state":"RUNNING","mode":")";
    b += STCPP_EXEC_MODE_STR;
    b += R"(","wss_connected":{"clob":)";
    b += json::boolean(m.wss_clob_connected);
    b += R"(,"user_channel":)";
    b += json::boolean(m.wss_user_channel_connected);
    b += R"(},"signals_active_count":0,"positions_count":0,"rm_rejects_last_60s":0,"uptime_sec":)";
    b += json::i64(uptime);
    if (as_of_ns >= 0) { b += R"(,"as_of_ts":)"; b += json::i64(as_of_ns); }
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
inline std::string events(const StateProvider& sp, std::int64_t as_of_ns = -1) {
    const std::vector<EventInfo> evs = sp.events();
    std::string b;
    b.reserve(512 + evs.size() * 256);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
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
        b += ",\"icon_url\":";
        b += json::str(ev.icon_url);
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
inline std::string positions(const StateProvider& sp, std::int64_t as_of_ns = -1) {
    const std::vector<HoldingView> rows = sp.positions();
    std::string b;
    b.reserve(256 + rows.size() * 256);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
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

// ---- fills (= GET /api/v1/fills[?market=X]) — 成交流水 (2026-06-04 老板「看懂买卖价」) ----
inline std::string fills(const StateProvider& sp, const std::string& market = "") {
    const std::vector<FillView> rows = sp.fills(market);
    std::string b;
    b.reserve(256 + rows.size() * 200);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    b += ",\"fills\":[";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const FillView& r = rows[i];
        if (i) b += ',';
        b += "{\"market_id\":";
        b += json::str(r.market_id);
        b += ",\"side\":";
        b += json::str(r.is_buy ? "buy" : "sell");
        b += ",\"outcome\":";
        b += json::str(r.is_yes ? "YES" : "NO");
        b += ",\"is_close\":";
        b += (r.is_close ? "true" : "false");
        b += ",\"price\":";
        b += json::num(r.price);
        b += ",\"size_usdc\":";
        b += json::num(r.size_usdc);
        b += ",\"realized\":";
        b += json::num(r.realized);
        b += ",\"cum_realized\":";
        b += json::num(r.cum_realized);
        b += ",\"fair\":";
        b += json::num(r.fair);
        b += ",\"mark\":";
        b += json::num(r.mark);
        b += ",\"as_of_ts\":";
        b += json::i64(r.as_of_ts_ns);
        b += '}';
    }
    b += "]}";
    return b;
}

// ---- pnl attribution (= GET /api/v1/pnl/attribution) ----
inline std::string pnl_attribution(const StateProvider& sp, std::int64_t as_of_ns = -1) {
    const PnlAttribution a = sp.pnl_attribution();
    std::string b;
    b.reserve(256);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
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
inline std::string gate(const StateProvider& sp, std::int64_t as_of_ns = -1) {
    const PaperGate g = sp.paper_gate();
    std::string b;
    b.reserve(384);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
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
inline std::string rejects(const StateProvider& sp, std::int64_t as_of_ns = -1) {
    const std::vector<RiskRejectRow> rows = sp.risk_rejects();
    std::string b;
    b.reserve(256 + rows.size() * 192);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
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

// ---- grid 单盘顶档对象 (唯一数据源: payload::grid 全量 / endpoint_grid / SSE grid delta 共用) ----
inline std::string grid_market_obj(const StateProvider& sp, const std::string& cid) {
    const BinaryMarketBookView bv = sp.book_pair(cid);
    const QuoteParams q = sp.quote_params(cid);
    std::string o;
    o.reserve(256);
    o += "{\"condition_id\":";
    o += json::str(cid);
    o += ",\"book_found\":";
    o += json::boolean(bv.token0.found);
    if (bv.token0.found) {
        o += ",\"best_bid\":";
        o += json::num(bv.token0.best_bid);
        o += ",\"best_ask\":";
        o += json::num(bv.token0.best_ask);
        o += ",\"cross_spread\":";
        o += json::num(bv.cross_spread);
        o += ",\"event_ts\":";
        o += json::i64(bv.token0.ts.event_ts_ns);
        o += ",\"ingestion_ts\":";
        o += json::i64(bv.token0.ts.ingestion_ts_ns);
    }
    o += ",\"quote_found\":";
    o += json::boolean(q.found);
    if (q.found) {
        o += ",\"fair\":";
        o += json::num(q.fair_value);
        o += ",\"market_mid\":";
        o += json::num(q.market_mid);
        o += ",\"edge_bps\":";
        o += json::num(q.edge_bps);
        o += ",\"sharp_fair\":";
        o += json::num(q.sharp_fair);
        // (大模型 model_confidence/advisory 已砍 2026-06-05「砍掉大模型训练功能」)
    }
    // 比赛时间 + 状态 (2026-06-02 老板「前端要看几点开赛 / 是否进行中」):
    //   kickoff_ts/end_ts 用 ns (与 event_ts 同口径, 前端 fmtTs 直接用); game_state 后端派生单一口径。
    //   派生: resolved/closed (Polymarket 生命周期) 优先, 否则按 now vs 开赛/结束时间。
    //   注: 时间型 inplay 是估计 (kickoff<now<end); 已匹配盘前端会用 Goalserve 比分 status 精化为权威。
    const MarketInfo mi = sp.market(cid);
    const std::int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
    const char* game_state = "unknown";
    if (mi.resolved) {
        game_state = "resolved";
    } else if (mi.closed) {
        game_state = "ended";
    } else if (mi.sports_market_type.find("completed") != std::string::npos) {
        game_state = "ended";  // 已完赛盘 (tennis_completed_match 等): 比赛结束等结算, 时间估可能误标 inplay
    } else if (mi.game_start_ts_sec <= 0) {
        game_state = "unknown";
    } else if (now_sec < mi.game_start_ts_sec) {
        game_state = "pregame";
    } else if (mi.end_ts_sec <= 0 || now_sec < mi.end_ts_sec) {
        game_state = "inplay";
    } else {
        game_state = "ended";
    }
    o += ",\"kickoff_ts\":";
    o += json::i64(mi.game_start_ts_sec > 0 ? mi.game_start_ts_sec * 1'000'000'000LL : 0);
    o += ",\"end_ts\":";
    o += json::i64(mi.end_ts_sec > 0 ? mi.end_ts_sec * 1'000'000'000LL : 0);
    o += ",\"game_state\":";
    o += json::str(game_state);
    // 可读盘口名 + 两边 outcome (2026-06-02 老板「别显示 hash; 订单簿标清哪边」):
    //   title = gamma groupItemTitle (如 "Game 1 Winner"/"O/U 2.5 Games"); 缺则用盘口类型。
    //   outcome0/1 = 两 token 的 outcome 名 (球员/队名 或 Over/Under 或 Yes/No), 供订单簿两边标注。
    o += ",\"title\":";
    o += json::str(!mi.group_item_title.empty() ? mi.group_item_title : mi.sports_market_type);
    if (mi.tokens.size() >= 2) {
        o += ",\"outcome0\":";
        o += json::str(mi.tokens[0].outcome);
        o += ",\"outcome1\":";
        o += json::str(mi.tokens[1].outcome);
    }
    o += '}';
    return o;
}

// ---- grid (= GET /api/v1/grid) — 全市场顶档摘要 ----
inline std::string grid(const StateProvider& sp, std::int64_t as_of_ns = -1) {
    const std::vector<EventInfo> evs = sp.events();
    std::string b;
    b.reserve(64 * 1024);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
    b += ",\"markets\":[";
    std::size_t count = 0;
    bool first = true;
    std::unordered_set<std::string> seen;
    for (const auto& ev : evs) {
        for (const auto& cid : ev.condition_ids) {
            if (cid.empty() || !seen.insert(cid).second) continue;
            if (!first) b += ',';
            first = false;
            ++count;
            b += grid_market_obj(sp, cid);
        }
    }
    b += "],\"count\":";
    b += json::i64(static_cast<std::int64_t>(count));
    b += '}';
    return b;
}

// ---- scores (SSE 专用: live 赛事比分数组; REST 是 per-event /api/v1/score/{id}) ----
//   字段与 endpoint_score 单条一致, 包成数组供 SSE scores 通道。
inline std::string scores(const StateProvider& sp, std::int64_t as_of_ns = -1) {
    // 2026-06-03 (老板「人盯盘看比分/赛点/进度」+ WSS 实时): 改用 scores_all() 直接枚举 score_store —
    //   原 events()→score(event_id) 按 PM event_id 查, 但 store 按 Goalserve inplay_match_id 做 key →
    //   永远 found=false → 通道空。scores_all 绕开 key 不匹配, 全 live 赛事比分都推。新增 games(网球
    //   赛点/进度) + sharp_fair(in-play bet365 de-vig, 看板显套利信号: sharp vs PM mid 的差就是机会)。
    const std::vector<EventScore> all = sp.scores_all();
    const std::int64_t now_ns = now_epoch_ns();
    // 新鲜度上限 (与 paper_loop score_staleness_limit_ns 同 120s): 超此未更新 = 比赛已结束/掉出 feed
    //   (Goalserve 停更 data_source_ts) → 不推, 防前端残留显示"已结束比赛仍进行中" (老板 2026-06-03)。
    constexpr std::int64_t kScoreStaleNs = 120'000'000'000LL;
    std::string b;
    b.reserve(4096);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
    b += ",\"scores\":[";
    bool first = true;
    for (const auto& s : all) {
        if (!s.found) continue;
        if (s.status == "final" || s.status == "pregame" || s.status == "NotStarted") continue;  // 只推进行中
        // 陈旧 (停更 > 120s) → 视为已结束/断流, 不推 (防显示已结束比赛)。
        if (s.ts.data_source_ts_ns > 0 && (now_ns - s.ts.data_source_ts_ns) > kScoreStaleNs) continue;
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
        b += ",\"games_home\":";       // 网球: 全场已打局数 (totals 用); 非网球=0
        b += json::i64(s.games_home);
        b += ",\"games_away\":";
        b += json::i64(s.games_away);
        b += ",\"set_summary\":";      // 网球逐盘比分 "6-4 3-2" (实时比分; 非网球空)
        b += json::str(s.set_summary);
        b += ",\"pts_home\":";         // 当前局分 0/15/30/40/AD
        b += json::str(s.pts_home);
        b += ",\"pts_away\":";
        b += json::str(s.pts_away);
        b += ",\"serving\":";          // 发球方 0=home/1=away/-1
        b += json::i64(s.serving);
        b += ",\"gs_state_code\":";    // v3 事件套利: Goalserve 瞬时事件码 (11003进球等; 空=无); 观测事件流
        b += json::str(s.gs_state_code);
        b += ",\"sharp_home_fair\":";  // in-play bet365 de-vig home 胜率 (-1=无 odds); 看板套利信号锚
        b += json::num(s.inplay_bet365_home_fair);
        b += ",\"sharp_away_fair\":";
        b += json::num(s.inplay_bet365_away_fair);
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

// ---- 单边 BookSnapshot 序列化 (book_pair / book-by-token / SSE book 通道 单一数据源) ----
inline std::string book_snapshot(const BookSnapshot& b) {
    std::string s;
    s.reserve(512);
    s += "{\"found\":";
    s += json::boolean(b.found);
    s += ",\"token_id\":";
    s += json::str(b.token_id);
    s += ",\"condition_id\":";
    s += json::str(b.condition_id);
    s += ",\"outcome\":";
    s += json::str(b.outcome);
    s += ",\"market_id\":";
    s += json::str(b.market_id);
    s += ",\"wss_state\":";
    s += json::str(b.wss_state);
    s += ",\"source\":";
    s += json::str(b.source);
    if (b.found) {
        s += ",\"best_bid\":";
        s += json::num(b.best_bid);
        s += ",\"best_ask\":";
        s += json::num(b.best_ask);
        s += ",\"microprice\":";
        s += json::num(b.microprice);
        s += ",\"spread\":";
        s += json::num(b.spread);
        s += ",\"imbalance\":";
        s += json::num(b.imbalance);
        s += ",\"sequence_no\":";
        s += json::i64(b.sequence_no);
        s += ",\"gap_count\":";
        s += json::i64(b.gap_count);
        s += ",\"event_ts\":";
        s += json::i64(b.ts.event_ts_ns);
        s += ",\"data_source_ts\":";
        s += json::i64(b.ts.data_source_ts_ns);
        s += ",\"ingestion_ts\":";
        s += json::i64(b.ts.ingestion_ts_ns);
        s += ",\"book_as_of_ts\":";
        s += json::i64(b.ts.as_of_ts_ns);
        s += ",\"bids\":[";
        for (std::size_t i = 0; i < b.bids.size(); ++i) {
            if (i) s += ',';
            s += "{\"price\":";
            s += json::num(b.bids[i].price);
            s += ",\"size\":";
            s += json::num(b.bids[i].size);
            s += '}';
        }
        s += "],\"asks\":[";
        for (std::size_t i = 0; i < b.asks.size(); ++i) {
            if (i) s += ',';
            s += "{\"price\":";
            s += json::num(b.asks[i].price);
            s += ",\"size\":";
            s += json::num(b.asks[i].size);
            s += '}';
        }
        s += "]";
    }
    s += '}';
    return s;
}

// ---- book_pair (= GET /api/v1/book_pair/{cid}) — 全档 BinaryMarketBookView ----
inline std::string book_pair(const StateProvider& sp, const std::string& condition_id,
                             std::int64_t as_of_ns = -1) {
    const BinaryMarketBookView bv = sp.book_pair(condition_id);
    std::string b;
    b.reserve(1536);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
    b += ",\"found\":";
    b += json::boolean(bv.found);
    b += ",\"condition_id\":";
    b += json::str(bv.condition_id);
    if (bv.found) {
        b += ",\"cross_spread\":";
        b += json::num(bv.cross_spread);
        b += ",\"event_ts\":";
        b += json::i64(bv.ts.event_ts_ns);
        b += ",\"data_source_ts\":";
        b += json::i64(bv.ts.data_source_ts_ns);
        b += ",\"ingestion_ts\":";
        b += json::i64(bv.ts.ingestion_ts_ns);
        b += ",\"as_of_ts_ns\":";
        b += json::i64(bv.ts.as_of_ts_ns);
        b += ",\"token0\":";
        b += book_snapshot(bv.token0);
        b += ",\"token1\":";
        b += book_snapshot(bv.token1);
    }
    b += '}';
    return b;
}

// ---- quote (= GET /api/v1/quote/{cid}) — 全 QuoteParams ----
inline std::string quote(const StateProvider& sp, const std::string& condition_id,
                         std::int64_t as_of_ns = -1) {
    const QuoteParams q = sp.quote_params(condition_id);
    std::string b;
    b.reserve(512);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
    b += ",\"found\":";
    b += json::boolean(q.found);
    b += ",\"market_id\":";
    b += json::str(q.market_id);
    if (q.found) {
        b += ",\"fair_value\":";
        b += json::num(q.fair_value);
        b += ",\"market_mid\":";
        b += json::num(q.market_mid);
        b += ",\"edge_bps\":";
        b += json::num(q.edge_bps);
        b += ",\"kelly_fraction\":";
        b += json::num(q.kelly_fraction);
        b += ",\"suggested_notional\":";
        b += json::num(q.suggested_notional);
        b += ",\"signal_strength\":";
        b += json::num(q.signal_strength);
        b += ",\"quote_as_of_ts\":";
        b += json::i64(q.as_of_ts_ns);
        // (大模型 provenance model_id/model_kind/spec_version/model_confidence/model_calibrated/
        //  fair_ci/advisory/model_conf 已砍 2026-06-05「砍掉大模型训练功能」)
        b += ",\"predict_ok\":";
        b += json::boolean(q.predict_ok);
        b += ",\"model_as_of_ts\":";
        b += json::i64(q.model_as_of_ts_ns);
        b += ",\"sharp_fair\":";
        b += json::num(q.sharp_fair);
        b += ",\"devig_ok\":";
        b += json::boolean(q.devig_ok);
        b += ",\"g_time_x_lead\":";
        b += json::num(q.g_time_x_lead);
        b += ",\"joint_as_of_ts\":";
        b += json::i64(q.joint_as_of_ts_ns);
        b += ",\"data_source_ts\":";  // PM 订单簿赔率新鲜度 (book WSS 版本时刻; now−它 = 市场赔率多旧)
        b += json::i64(q.data_source_ts_ns);
        b += ",\"sharp_data_source_ts\":";  // GS sharp 赔率新鲜度 (inplay 赔率版本时刻; now−它 = sharp 多旧)
        b += json::i64(q.sharp_data_source_ts_ns);
        // sharp fair 时序 (老板 2026-06-05 line movement): velocity/收敛率/抖动/样本数 (前端 gate sharp_samples≥2)
        b += ",\"sharp_velocity\":";
        b += json::num(q.sharp_velocity);
        b += ",\"sharp_conv_rate\":";
        b += json::num(q.sharp_conv_rate);
        b += ",\"sharp_vol\":";
        b += json::num(q.sharp_vol);
        b += ",\"sharp_samples\":";
        b += json::i64(q.sharp_samples);
        // 持仓管理 Stage 2 sizing 乘子 (观测): 实际乘到 |target| 的值 (盯盘看"信息优势怎么调仓")。
        b += ",\"lifecycle_mult\":";
        b += json::num(q.lifecycle_mult);
        b += ",\"clv_mult\":";
        b += json::num(q.clv_mult);
        b += ",\"rolling_clv_mean\":";
        b += json::num(q.rolling_clv_mean);
        b += ",\"rolling_clv_n\":";
        b += json::i64(q.rolling_clv_n);
    }
    b += '}';
    return b;
}

// ---- healthz (= GET /healthz) ----
inline std::string healthz(const HttpServer& hs, std::int64_t as_of_ns = -1) {
    const std::int64_t uptime = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - hs.start_time()).count());
    std::string b = R"({"ok":true,"threads":{"ingest_reactor":"alive","signal_engine":"alive",)"
                    R"("risk_manager":"alive","paper_signer":"alive","api_server":"alive"},"uptime_sec":)";
    b += json::i64(uptime);
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
    b += '}';
    return b;
}

// ---- features/health (= GET /api/v1/features/health) ----
inline std::string features_health(const StateProvider& sp, std::int64_t as_of_ns = -1) {
    const FeatureHealthReport rep = sp.feature_health();
    std::string b;
    b.reserve(8192);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
    b += ",\"n_records\":";
    b += json::i64(rep.n_records);
    b += ",\"dead\":";
    b += json::i64(rep.dead);
    b += ",\"const\":";
    b += json::i64(rep.constant);
    b += ",\"healthy\":";
    b += json::i64(rep.healthy);
    b += ",\"total\":";
    b += json::i64(static_cast<std::int64_t>(rep.rows.size()));
    b += ",\"rows\":[";
    bool first = true;
    for (const auto& r : rep.rows) {
        if (!first) b += ',';
        first = false;
        b += "{\"i\":";
        b += json::i64(r.index);
        b += ",\"name\":";
        b += json::str(r.name);
        b += ",\"populated\":";
        b += json::i64(r.populated);
        b += ",\"nonzero\":";
        b += json::i64(r.nonzero);
        b += ",\"min\":";
        b += json::num(r.min);
        b += ",\"max\":";
        b += json::num(r.max);
        b += ",\"mean\":";
        b += json::num(r.mean);
        b += ",\"status\":";
        b += json::str(r.status);
        b += '}';
    }
    b += "]}";
    return b;
}

// ---- mapping/status (= GET /api/v1/mapping/status) ----
inline std::string mapping_status(const StateProvider& sp, std::int64_t as_of_ns = -1) {
    const MappingStatusReport rep = sp.mapping_status();
    std::string b;
    b.reserve(4096);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
    b += ",\"total_markets\":";
    b += json::i64(rep.total_markets);
    b += ",\"matched\":";
    b += json::i64(rep.matched);
    b += ",\"live_games\":";
    b += json::i64(rep.live_games);
    b += ",\"matched_with_sharp\":";
    b += json::i64(rep.matched_with_sharp);
    b += ",\"matched_no_sharp\":";
    b += json::i64(rep.matched_no_sharp);
    b += ",\"no_match\":";
    b += json::i64(rep.no_match);
    b += ",\"markets\":[";
    bool first = true;
    for (const auto& r : rep.markets) {
        if (!first) b += ',';
        first = false;
        b += "{\"condition_id\":";
        b += json::str(r.condition_id);
        b += ",\"team0\":";
        b += json::str(r.team0);
        b += ",\"team1\":";
        b += json::str(r.team1);
        b += ",\"is_draw\":";
        b += json::boolean(r.is_draw);
        b += ",\"matched\":";
        b += json::boolean(r.matched);
        b += ",\"inplay_match_id\":";
        b += json::str(r.inplay_match_id);
        b += ",\"match_confidence\":";
        b += json::num(r.match_confidence);
        b += '}';
    }
    b += "],\"games\":[";
    first = true;
    for (const auto& g : rep.games) {
        if (!first) b += ',';
        first = false;
        b += "{\"event_id\":";
        b += json::str(g.event_id);
        b += ",\"home\":";
        b += json::str(g.home);
        b += ",\"away\":";
        b += json::str(g.away);
        b += ",\"sport\":";
        b += json::str(g.sport);
        b += ",\"status\":";
        b += json::str(g.status);
        b += ",\"home_score\":";
        b += json::i64(g.home_score);
        b += ",\"away_score\":";
        b += json::i64(g.away_score);
        b += '}';
    }
    // 无赔率源明细 (源头 pass 清单, 2026-06-04 老板「匹配不上的记录, api可查」)。
    b += "],\"no_sharp\":[";
    first = true;
    for (const auto& n : rep.no_sharp) {
        if (!first) b += ',';
        first = false;
        b += "{\"condition_id\":";
        b += json::str(n.condition_id);
        b += ",\"team0\":";
        b += json::str(n.team0);
        b += ",\"team1\":";
        b += json::str(n.team1);
        b += ",\"sport\":";
        b += json::str(n.sport);
        b += ",\"reason\":";
        b += json::str(n.reason);
        b += ",\"best_home\":";
        b += json::str(n.best_home);
        b += ",\"best_away\":";
        b += json::str(n.best_away);
        b += ",\"best_score\":";
        b += json::num(n.best_score);
        b += ",\"kickoff_state\":";
        b += json::str(n.kickoff_state);
        b += ",\"matched_event_id\":";
        b += json::str(n.matched_event_id);
        b += '}';
    }
    b += "]}";
    return b;
}

// ---- pnl/timeseries (= GET /api/v1/pnl/timeseries) ----
inline std::string pnl_timeseries(const StateProvider& sp, std::int64_t window_sec,
                                  std::int64_t bucket_sec, std::int64_t as_of_ns = -1) {
    const std::vector<PnlBucket> buckets = sp.pnl_timeseries(window_sec, bucket_sec);
    std::string b;
    b.reserve(256 + buckets.size() * 192);
    b += "{\"mode\":";
    b += json::str(exec_mode_str(sp.mode()));
    b += ",\"window_sec\":";
    b += json::i64(window_sec);
    b += ",\"bucket_sec\":";
    b += json::i64(bucket_sec);
    if (as_of_ns >= 0) { b += ",\"as_of_ts\":"; b += json::i64(as_of_ns); }
    b += ",\"buckets\":[";
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        const PnlBucket& bk = buckets[i];
        if (i) b += ',';
        b += "{\"bucket_start_ts\":";
        b += json::i64(bk.bucket_start_ts_ns);
        b += ",\"cum_net_pnl\":";
        b += json::num(bk.cum_net_pnl);
        b += ",\"realized\":";
        b += json::num(bk.realized);
        b += ",\"unrealized\":";
        b += json::num(bk.unrealized);
        b += ",\"fee\":";
        b += json::num(bk.fee);
        b += ",\"gas\":";
        b += json::num(bk.gas);
        b += ",\"n_trades\":";
        b += json::i64(bk.n_trades);
        b += '}';
    }
    b += "]}";
    return b;
}

}  // namespace stcpp::debug_api::payload
