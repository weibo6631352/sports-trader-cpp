// src/stcpp/debug_api/endpoint_stream.cpp
//   GET /api/v1/stream  — SSE 服务端推增量 (看板 9 通道一条长连接)
// Owner: 老雷 (GM)  2026-06-02
// 设计: docs/RESEARCH/laolei-sse-push-design-v1.md (v1 + 老王/老郭轻评审修订)
//
// 通道(9): status account grid scores events positions pnl gate rejects + 控制(hello/heartbeat)
// 信封: {v,seq,as_of_ts(仅transport时刻),mode(snapshot|delta|append),data}  event: 通道名  id: seq
// 节奏: 连上→padding+hello+全量snapshot; 每 tick(1s) 变化即推(grid 做 per-cid delta, 余 on-change 全量);
//       每 10s heartbeat; 每 30s 全量 keyframe 自愈。
// 评审修订: set_write_timeout(防死连接占线程) + set_tcp_nodelay(防缓冲) + 连接上限发 retry 非裸503 +
//           seq per-server 单调(仅去重不回放) + as_of_ts 仅 transport(数据新鲜度用 payload 内 event_ts)。
// R-12: provider 全程只读 const 快照, 不碰热路径/RM/signer/WSS。

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"
#include "version_generated.hpp"  // STCPP_GIT_HASH_STR (CMake configure_file 注入)

namespace stcpp::debug_api {

namespace {

constexpr int kMaxSseClients = 8;
constexpr int kTickMs = 1000;
constexpr int kHeartbeatTicks = 10;  // 10s
constexpr int kKeyframeTicks = 30;   // 30s

std::atomic<int> g_sse_clients{0};
std::atomic<std::uint64_t> g_seq{0};

// 一帧 SSE 消息: id + event + data(信封)。返回 false = 写失败(客户端断开)。
bool send_frame(httplib::DataSink& sink, const char* channel, const char* mode,
                const std::string& payload) {
    const std::uint64_t seq = g_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    std::string frame;
    frame.reserve(payload.size() + 128);
    frame += "id: ";
    frame += json::i64(static_cast<std::int64_t>(seq));
    frame += "\nevent: ";
    frame += channel;
    frame += "\ndata: {\"v\":1,\"seq\":";
    frame += json::i64(static_cast<std::int64_t>(seq));
    frame += ",\"as_of_ts\":";
    frame += json::i64(now_epoch_ns());  // 仅 transport 时刻 (R-20: 数据新鲜度在 payload 内 event_ts)
    frame += ",\"mode\":\"";
    frame += mode;
    frame += "\",\"data\":";
    frame += payload;
    frame += "}\n\n";
    return sink.write(frame.data(), frame.size());
}

// 当前 grid 各盘口序列化为 cid → 单盘 JSON 对象串 (供 delta diff)。
// 与 payload::grid 同字段 (顶档摘要); 这里按 cid 拆开以算增量。
std::vector<std::pair<std::string, std::string>> grid_markets(const StateProvider& sp) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::vector<EventInfo> evs = sp.events();
    std::unordered_set<std::string> seen;
    for (const auto& ev : evs) {
        for (const auto& cid : ev.condition_ids) {
            if (cid.empty() || !seen.insert(cid).second) continue;
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
                o += ",\"model_confidence\":";
                o += json::num(q.model_confidence);
                o += ",\"advisory\":";
                o += json::boolean(q.advisory);
            }
            o += '}';
            out.emplace_back(cid, std::move(o));
        }
    }
    return out;
}

}  // namespace

void register_stream(httplib::Server& svr, const HttpServer& hs) {
    // 评审修订(老王): 防死连接占线程 + 防中间节点缓冲。服务器全局生效, 对短请求无害。
    svr.set_write_timeout(3, 0);
    svr.set_tcp_nodelay(true);

    svr.Get("/api/v1/stream", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        // 连接上限: 超了写一帧 retry(让 EventSource 30s 退避重连, 非裸 503 触发 3s 风暴)。
        if (g_sse_clients.fetch_add(1) >= kMaxSseClients) {
            g_sse_clients.fetch_sub(1);
            res.status = 503;
            res.set_content("retry: 30000\n\n", "text/event-stream; charset=utf-8");
            return;
        }
        res.set_header("Cache-Control", "no-cache, no-store");
        res.set_header("X-Accel-Buffering", "no");  // 防 nginx/反代缓冲(将来上反代关键)
        res.set_header("Connection", "keep-alive");

        res.set_chunked_content_provider(
            "text/event-stream; charset=utf-8",
            [&hs](std::size_t /*offset*/, httplib::DataSink& sink) -> bool {
                const StateProvider& sp = hs.provider();

                // 0. padding 注释(256B)触发首次 flush, 击穿中间节点缓冲
                {
                    std::string pad = ":";
                    pad.append(256, ' ');
                    pad += "\n\n";
                    if (!sink.write(pad.data(), pad.size())) return true;
                }

                // 1. hello: 协议版本/节奏/stream_id/每通道 delta 语义
                {
                    std::string hello = "{\"v\":1,\"server\":\"";
                    hello += STCPP_GIT_HASH_STR;
                    hello += "\",\"stream_id\":\"";
                    hello += json::i64(now_epoch_ns());  // 简易唯一 id (v2 focus 副 POST 回传用)
                    hello += "\",\"tick_ms\":";
                    hello += json::i64(kTickMs);
                    hello += ",\"keyframe_ms\":";
                    hello += json::i64(kKeyframeTicks * kTickMs);
                    hello += ",\"replay\":\"none\",\"compress\":[],\"channels\":[";
                    hello += "{\"name\":\"status\",\"delta\":\"snapshot\"},";
                    hello += "{\"name\":\"account\",\"delta\":\"snapshot\"},";
                    hello += "{\"name\":\"grid\",\"delta\":\"delta\"},";
                    hello += "{\"name\":\"scores\",\"delta\":\"snapshot\"},";
                    hello += "{\"name\":\"events\",\"delta\":\"snapshot\"},";
                    hello += "{\"name\":\"positions\",\"delta\":\"snapshot\"},";
                    hello += "{\"name\":\"pnl\",\"delta\":\"snapshot\"},";
                    hello += "{\"name\":\"gate\",\"delta\":\"snapshot\"},";
                    hello += "{\"name\":\"rejects\",\"delta\":\"full\"}]}";
                    if (!send_frame(sink, "hello", "snapshot", hello)) return true;
                }

                // 2. 各通道初始全量 snapshot + 建立 change-detection 基线
                std::string last_status = payload::status(hs);
                std::string last_account = payload::account(sp);
                std::string last_events = payload::events(sp);
                std::string last_positions = payload::positions(sp);
                std::string last_pnl = payload::pnl_attribution(sp);
                std::string last_gate = payload::gate(sp);
                std::string last_rejects = payload::rejects(sp);
                std::string last_scores = payload::scores(sp);
                std::unordered_map<std::string, std::string> last_grid;
                {
                    auto gm = grid_markets(sp);
                    std::string snap = "{\"markets\":[";
                    for (std::size_t i = 0; i < gm.size(); ++i) {
                        if (i) snap += ',';
                        snap += gm[i].second;
                        last_grid.emplace(gm[i].first, gm[i].second);
                    }
                    snap += "],\"count\":";
                    snap += json::i64(static_cast<std::int64_t>(gm.size()));
                    snap += '}';
                    if (!send_frame(sink, "status", "snapshot", last_status)) return true;
                    if (!send_frame(sink, "account", "snapshot", last_account)) return true;
                    if (!send_frame(sink, "events", "snapshot", last_events)) return true;
                    if (!send_frame(sink, "grid", "snapshot", snap)) return true;
                    if (!send_frame(sink, "scores", "snapshot", last_scores)) return true;
                    if (!send_frame(sink, "positions", "snapshot", last_positions)) return true;
                    if (!send_frame(sink, "pnl", "snapshot", last_pnl)) return true;
                    if (!send_frame(sink, "gate", "snapshot", last_gate)) return true;
                    if (!send_frame(sink, "rejects", "full", last_rejects)) return true;
                }

                // 3. tick 循环
                int tick = 0;
                int since_send = 0;  // 距上次发帧的 tick 数 (心跳判据)
                while (hs.is_running() && sink.is_writable()) {
                    // 1s tick 分 5×200ms, 便于及时响应 shutdown / 断连
                    for (int k = 0; k < 5; ++k) {
                        if (!hs.is_running() || !sink.is_writable()) return true;
                        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs / 5));
                    }
                    ++tick;
                    const bool keyframe = (tick % kKeyframeTicks == 0);
                    bool sent = false;

                    // --- 8 个 on-change 全量通道 ---
                    auto push_if_changed = [&](const char* ch, const char* mode, std::string& last,
                                               std::string cur) -> bool {
                        if (keyframe || cur != last) {
                            last = std::move(cur);
                            if (!send_frame(sink, ch, mode, last)) return false;
                            sent = true;
                        }
                        return true;
                    };
                    if (!push_if_changed("status", "snapshot", last_status, payload::status(hs))) return true;
                    if (!push_if_changed("account", "snapshot", last_account, payload::account(sp))) return true;
                    if (!push_if_changed("events", "snapshot", last_events, payload::events(sp))) return true;
                    if (!push_if_changed("scores", "snapshot", last_scores, payload::scores(sp))) return true;
                    if (!push_if_changed("positions", "snapshot", last_positions, payload::positions(sp))) return true;
                    if (!push_if_changed("pnl", "snapshot", last_pnl, payload::pnl_attribution(sp))) return true;
                    if (!push_if_changed("gate", "snapshot", last_gate, payload::gate(sp))) return true;
                    if (!push_if_changed("rejects", "full", last_rejects, payload::rejects(sp))) return true;

                    // --- grid: per-cid delta (或 keyframe 全量) ---
                    {
                        auto gm = grid_markets(sp);
                        if (keyframe) {
                            std::string snap = "{\"markets\":[";
                            last_grid.clear();
                            for (std::size_t i = 0; i < gm.size(); ++i) {
                                if (i) snap += ',';
                                snap += gm[i].second;
                                last_grid.emplace(gm[i].first, gm[i].second);
                            }
                            snap += "],\"count\":";
                            snap += json::i64(static_cast<std::int64_t>(gm.size()));
                            snap += '}';
                            if (!send_frame(sink, "grid", "snapshot", snap)) return true;
                            sent = true;
                        } else {
                            std::string changed = "[";
                            std::unordered_set<std::string> cur_cids;
                            bool first = true;
                            for (auto& [cid, obj] : gm) {
                                cur_cids.insert(cid);
                                auto it = last_grid.find(cid);
                                if (it == last_grid.end() || it->second != obj) {
                                    if (!first) changed += ',';
                                    first = false;
                                    changed += obj;
                                    last_grid[cid] = obj;
                                }
                            }
                            changed += ']';
                            std::string removed = "[";
                            first = true;
                            for (auto it = last_grid.begin(); it != last_grid.end();) {
                                if (cur_cids.find(it->first) == cur_cids.end()) {
                                    if (!first) removed += ',';
                                    first = false;
                                    removed += json::str(it->first);
                                    it = last_grid.erase(it);
                                } else {
                                    ++it;
                                }
                            }
                            removed += ']';
                            // 有变化才推 grid delta
                            if (changed.size() > 2 || removed.size() > 2) {
                                std::string d = "{\"changed\":";
                                d += changed;
                                d += ",\"removed\":";
                                d += removed;
                                d += '}';
                                if (!send_frame(sink, "grid", "delta", d)) return true;
                                sent = true;
                            }
                        }
                    }

                    // --- heartbeat: 距上次发帧 ≥ kHeartbeatTicks 则发 ---
                    since_send = sent ? 0 : (since_send + 1);
                    if (since_send >= kHeartbeatTicks) {
                        since_send = 0;
                        if (!send_frame(sink, "heartbeat", "delta", "{}")) return true;
                    }
                }
                return true;
            },
            [](bool /*success*/) { g_sse_clients.fetch_sub(1); });  // resource releaser: 释放连接计数
    });
}

}  // namespace stcpp::debug_api
