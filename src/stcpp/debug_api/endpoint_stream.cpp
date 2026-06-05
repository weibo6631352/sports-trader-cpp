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
#include <cstdio>
#include <string>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "stcpp/debug_api/frame_compress.hpp"
#include "src/stcpp/debug_api/server.hpp"
#include "version_generated.hpp"  // STCPP_GIT_HASH_STR (CMake configure_file 注入)

namespace stcpp::debug_api {

namespace {

// 连接上限抬到 64 (老板 2026-06-05「上限高一些」): 双流架构每用户开 2 条 (bulk + hot),
//   64 → ~32 用户。跨洋实测「各连接独立卡, 不是物理链路一起卡」→ 多条独立 TCP 隔离卡顿 + 各自拥塞窗口涨吞吐。
constexpr int kMaxSseClients = 64;
constexpr int kTickMs = 1000;
constexpr int kHotTickMs = 1000;     // hot 流 (focus book/quote 专用) tick; 隔离后可后续调快
constexpr int kHeartbeatTicks = 10;  // 10s
constexpr int kKeyframeTicks = 30;   // 30s
constexpr int kSlowTicks = 5;        // Ops/慢通道(healthz/features/mapping/timeseries)每 5s 才比对一次
constexpr std::int64_t kTsWindowSec = 3600;  // timeseries 默认窗口 (= 前端 sparkline '1h')
constexpr std::int64_t kTsBucketSec = 60;    // timeseries 默认桶 (= '1m')
constexpr std::size_t kMaxFocus = 32;            // 每连接 focus 盘口上限 (评审: 两侧夹)
// 单帧上限 (评审: 防大帧撞 write_timeout)。2026-06-02 提到 256KB: 全盘口期 events/grid 帧随
//   盘口数(600+)涨, 旧 64KB 把 events 帧(67KB)整帧丢 → 前端 0 赛事。256KB 给 ~2000+ 盘口余量,
//   跨洋 256KB ≈0.25s 远低于 write_timeout(3s)。on-change 推送, 大帧不频繁。
constexpr std::size_t kMaxFrameBytes = 1024 * 1024;  // 1MB: 批量 detail 帧 (≤32 盘 book+quote+fills 合一) 留头

std::atomic<int> g_sse_clients{0};
std::atomic<std::uint64_t> g_seq{0};

// ---- focus 订阅注册表 (评审修订) ----
// stream_id = boot nonce + 单调计数器 (防同毫秒撞 + 防进程重启计数归零误命中旧 id)。
// 每连接一个 FocusState; provider 线程 lock-free 读 conditions (atomic COW),
//   POST /focus 线程持 g_focus_mu 查表取句柄后原子换 conditions。
const std::string g_boot_nonce = std::to_string(now_epoch_ns());  // 进程启动一次
std::atomic<std::uint64_t> g_stream_counter{0};

// 每连接独立 mutex 守护 (atomic<shared_ptr> 非全平台可用; per-tick 一次微锁拷贝 shared_ptr,
//   非热路径, R-12 无碍; 各连接独立 mutex 无跨连接竞争)。
struct FocusState {
    std::mutex mu;
    std::shared_ptr<const std::vector<std::string>> conditions{
        std::make_shared<const std::vector<std::string>>()};
    std::int64_t focus_seq{0};

    void load(std::shared_ptr<const std::vector<std::string>>& out_conds, std::int64_t& out_seq) {
        std::lock_guard<std::mutex> lk(mu);
        out_conds = conditions;
        out_seq = focus_seq;
    }
    void store(std::shared_ptr<const std::vector<std::string>> c, std::int64_t s) {
        std::lock_guard<std::mutex> lk(mu);
        conditions = std::move(c);
        focus_seq = s;
    }
};

std::mutex g_focus_mu;
std::unordered_map<std::string, std::shared_ptr<FocusState>> g_focus_reg;  // stream_id → state

std::string make_stream_id() {
    return g_boot_nonce + ":" + std::to_string(g_stream_counter.fetch_add(1) + 1);
}

constexpr std::size_t kCompressMin = 1024;  // 仅压缩 >1KB 帧 (小帧 deflate+base64 反增, 不划算)

// 一帧 SSE 消息: id + event + data(信封)。focus_seq>=0 时信封带 focus_seq (book/quote 版本对账)。
//   compress=true (客户端 ?gz=1) 且 payload>1KB → data 段改 raw-deflate+base64 字符串 + "enc":"df" 标记
//   (老板 2026-06-05「gzip 压缩帧」; 每帧独立 Z_FINISH 不缓冲; 跑 httplib 线程非 PaperLoop, ~1帧/s, CPU 可忽略)。
// 返回 false = 写失败(客户端断开)。frame 超 kMaxFrameBytes 跳过(返 true 继续, 评审防大帧)。
bool send_frame(httplib::DataSink& sink, const char* channel, const char* mode,
                const std::string& payload, std::int64_t focus_seq = -1, bool compress = false) {
    const std::uint64_t seq = g_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    std::string enc;  // 非空 = 压缩成功的 base64
    if (compress && payload.size() > kCompressMin) enc = deflate_b64(payload);
    const bool gz = !enc.empty();
    std::string frame;
    frame.reserve((gz ? enc.size() : payload.size()) + 192);
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
    frame += "\"";
    if (focus_seq >= 0) { frame += ",\"focus_seq\":"; frame += json::i64(focus_seq); }
    if (gz) {
        // base64 字符集 (A-Za-z0-9+/=) 全 JSON-safe, 无需转义。
        frame += ",\"enc\":\"df\",\"data\":\"";
        frame += enc;
        frame += "\"}";
    } else {
        frame += ",\"data\":";
        frame += payload;
        frame += "}";
    }
    frame += "\n\n";
    if (frame.size() > kMaxFrameBytes) {
        std::fprintf(stderr, "[stream] 跳过超大帧 channel=%s size=%zu\n", channel, frame.size());
        return true;  // 跳过该帧但不断连
    }
    return sink.write(frame.data(), frame.size());
}

// 当前 grid 各盘口序列化为 cid → 单盘 JSON 对象串 (供 delta diff)。
// 单盘序列化复用 payload::grid_market_obj (与 REST /grid 单一数据源, 不重复)。
std::vector<std::pair<std::string, std::string>> grid_markets(const StateProvider& sp) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::vector<EventInfo> evs = sp.events();
    std::unordered_set<std::string> seen;
    for (const auto& ev : evs) {
        for (const auto& cid : ev.condition_ids) {
            if (cid.empty() || !seen.insert(cid).second) continue;
            out.emplace_back(cid, payload::grid_market_obj(sp, cid));
        }
    }
    return out;
}

}  // namespace

void register_stream(httplib::Server& svr, const HttpServer& hs) {
    // 评审修订(老王): 防死连接占线程 + 防中间节点缓冲。服务器全局生效, 对短请求无害。
    svr.set_write_timeout(3, 0);
    svr.set_tcp_nodelay(true);

    svr.Get("/api/v1/stream", [&hs](const httplib::Request& req, httplib::Response& res) {
        // 连接上限: 超了写一帧 retry(让 EventSource 30s 退避重连, 非裸 503 触发 3s 风暴)。
        if (g_sse_clients.fetch_add(1) >= kMaxSseClients) {
            g_sse_clients.fetch_sub(1);
            res.status = 503;
            res.set_content("retry: 30000\n\n", "text/event-stream; charset=utf-8");
            return;
        }
        const bool gz = req.has_param("gz");  // ?gz=1 → 大帧 (grid/scores/positions) raw-deflate+base64
        res.set_header("Cache-Control", "no-cache, no-store");
        res.set_header("X-Accel-Buffering", "no");  // 防 nginx/反代缓冲(将来上反代关键)
        res.set_header("Connection", "keep-alive");

        res.set_chunked_content_provider(
            "text/event-stream; charset=utf-8",
            [&hs, gz](std::size_t /*offset*/, httplib::DataSink& sink) -> bool {
                const StateProvider& sp = hs.provider();

                // focus 订阅: 本连接生成唯一 stream_id + 注册 FocusState; RAII 在 provider 退出
                //   (任意 return 路径) 时从注册表摘除, 防泄漏/悬挂 (评审)。
                const std::string stream_id = make_stream_id();
                auto focus_state = std::make_shared<FocusState>();
                {
                    std::lock_guard<std::mutex> lk(g_focus_mu);
                    g_focus_reg[stream_id] = focus_state;
                }
                struct Unreg {
                    std::string id;
                    ~Unreg() {
                        std::lock_guard<std::mutex> lk(g_focus_mu);
                        g_focus_reg.erase(id);
                    }
                } unreg{stream_id};

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
                    hello += stream_id;  // boot:counter — focus 副 POST 回传用
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
                    hello += "{\"name\":\"rejects\",\"delta\":\"full\"},";
                    hello += "{\"name\":\"book\",\"delta\":\"snapshot\"},";    // focus 订阅: 全档深度
                    hello += "{\"name\":\"quote\",\"delta\":\"snapshot\"},";   // focus 订阅: 全 quote
                    hello += "{\"name\":\"healthz\",\"delta\":\"snapshot\"},";
                    hello += "{\"name\":\"features\",\"delta\":\"snapshot\"},";
                    hello += "{\"name\":\"mapping\",\"delta\":\"snapshot\"},";
                    hello += "{\"name\":\"timeseries\",\"delta\":\"snapshot\"}]}";
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
                    if (!send_frame(sink, "events", "snapshot", last_events, -1, gz)) return true;
                    if (!send_frame(sink, "grid", "snapshot", snap, -1, gz)) return true;
                    if (!send_frame(sink, "scores", "snapshot", last_scores, -1, gz)) return true;
                    if (!send_frame(sink, "positions", "snapshot", last_positions, -1, gz)) return true;
                    if (!send_frame(sink, "pnl", "snapshot", last_pnl)) return true;
                    if (!send_frame(sink, "gate", "snapshot", last_gate)) return true;
                    if (!send_frame(sink, "rejects", "full", last_rejects)) return true;
                }

                // (focus book/quote 已拆到独立 /api/v1/stream/hot 连接 — 跨洋隔离, 见下方 handler)

                // Ops/慢通道初始 snapshot + 基线 (healthz/features/mapping/timeseries; 每 kSlowTicks 比对)
                std::string last_healthz = payload::healthz(hs);
                std::string last_features = payload::features_health(sp);
                std::string last_mapping = payload::mapping_status(sp);
                std::string last_ts = payload::pnl_timeseries(sp, kTsWindowSec, kTsBucketSec);
                if (!send_frame(sink, "healthz", "snapshot", last_healthz)) return true;
                if (!send_frame(sink, "features", "snapshot", last_features)) return true;
                if (!send_frame(sink, "mapping", "snapshot", last_mapping)) return true;
                if (!send_frame(sink, "timeseries", "snapshot", last_ts)) return true;

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
                            if (!send_frame(sink, ch, mode, last, -1, gz)) return false;  // 大帧(scores/positions)压
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
                            if (!send_frame(sink, "grid", "snapshot", snap, -1, gz)) return true;
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
                                if (!send_frame(sink, "grid", "delta", d, -1, gz)) return true;
                                sent = true;
                            }
                        }
                    }

                    // (focus book/quote 已拆到独立 /api/v1/stream/hot — 跨洋单独 TCP 管道, bulk 卡顿不波及)

                    // --- Ops/慢通道: 每 kSlowTicks 比对一次 (大 payload, 变化慢; 节流控 CPU) ---
                    if (keyframe || (tick % kSlowTicks == 0)) {
                        if (!push_if_changed("healthz", "snapshot", last_healthz, payload::healthz(hs))) return true;
                        if (!push_if_changed("features", "snapshot", last_features, payload::features_health(sp))) return true;
                        if (!push_if_changed("mapping", "snapshot", last_mapping, payload::mapping_status(sp))) return true;
                        if (!push_if_changed("timeseries", "snapshot", last_ts,
                                             payload::pnl_timeseries(sp, kTsWindowSec, kTsBucketSec))) return true;
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

    // GET /api/v1/stream/hot — focus book/quote 专用流 (2026-06-05 双流架构, 老板「并行异步」+ 跨洋实测背书)
    //   只发用户正盯的盘口 book/quote, 独立 TCP 连接 → 跨洋卡顿与 bulk 物理隔离 (实测各连接独立卡,
    //   不是物理链路一起卡; 多条独立 TCP 各自拥塞窗口 → 总吞吐随连接数涨)。focus 走同一 g_focus_reg,
    //   前端把 focus POST 指向本流的 stream_id (bulk 流不再推 book/quote)。
    svr.Get("/api/v1/stream/hot", [&hs](const httplib::Request& req, httplib::Response& res) {
        if (g_sse_clients.fetch_add(1) >= kMaxSseClients) {
            g_sse_clients.fetch_sub(1);
            res.status = 503;
            res.set_content("retry: 30000\n\n", "text/event-stream; charset=utf-8");
            return;
        }
        const bool gz = req.has_param("gz");  // ?gz=1 → 大帧 raw-deflate+base64 (老板「gzip 压缩帧」)
        res.set_header("Cache-Control", "no-cache, no-store");
        res.set_header("X-Accel-Buffering", "no");
        res.set_header("Connection", "keep-alive");

        res.set_chunked_content_provider(
            "text/event-stream; charset=utf-8",
            [&hs, gz](std::size_t /*offset*/, httplib::DataSink& sink) -> bool {
                const StateProvider& sp = hs.provider();

                // 本连接唯一 stream_id + 注册 FocusState (RAII 退出摘除, 同 bulk 流)
                const std::string stream_id = make_stream_id();
                auto focus_state = std::make_shared<FocusState>();
                {
                    std::lock_guard<std::mutex> lk(g_focus_mu);
                    g_focus_reg[stream_id] = focus_state;
                }
                struct Unreg {
                    std::string id;
                    ~Unreg() {
                        std::lock_guard<std::mutex> lk(g_focus_mu);
                        g_focus_reg.erase(id);
                    }
                } unreg{stream_id};

                // padding 击穿中间节点缓冲
                {
                    std::string pad = ":";
                    pad.append(256, ' ');
                    pad += "\n\n";
                    if (!sink.write(pad.data(), pad.size())) return true;
                }

                // hello: 标 stream:"hot" + stream_id (前端把 focus POST 指向它)
                {
                    std::string hello = "{\"v\":1,\"server\":\"";
                    hello += STCPP_GIT_HASH_STR;
                    hello += "\",\"stream_id\":\"";
                    hello += stream_id;
                    hello += "\",\"stream\":\"hot\",\"tick_ms\":";
                    hello += json::i64(kHotTickMs);
                    hello += ",\"channels\":[{\"name\":\"detail\",\"delta\":\"snapshot\"}]}";
                    if (!send_frame(sink, "hello", "snapshot", hello)) return true;
                }

                // 批量 detail 流 (2026-06-05 老板「没必要 rest 的用 sse + 全部打包一次性, 网络往返太浪费」):
                //   每 tick 把【所有 focus 盘】的 book+quote+fills 合成【一帧】detail (替代原 N×2 逐盘帧)。
                //   跨洋: 1 帧 1 信封 1 write 替 N×2 帧 → 省信封头/write/TCP 分段; 前端零 REST 轮询。
                //   focus 变化 (fseq 推进) → 提前醒来立即推 → 展开即见, 免冷启 REST 往返。
                int since_send = 0;
                std::int64_t prev_fseq = -1;

                while (hs.is_running() && sink.is_writable()) {
                    for (int k = 0; k < 5; ++k) {
                        if (!hs.is_running() || !sink.is_writable()) return true;
                        std::shared_ptr<const std::vector<std::string>> probe;
                        std::int64_t cur = 0;
                        focus_state->load(probe, cur);
                        if (cur != prev_fseq) break;  // focus 变了 → 立即推 (不等满 tick)
                        std::this_thread::sleep_for(std::chrono::milliseconds(kHotTickMs / 5));
                    }

                    std::shared_ptr<const std::vector<std::string>> conds;
                    std::int64_t fseq = 0;
                    focus_state->load(conds, fseq);
                    prev_fseq = fseq;

                    // {"<cid>":{"book":{..},"quote":{..},"fills":{..}}, ...} — 一帧含全部 focus 盘。
                    std::string batched = "{";
                    std::size_t n = 0;
                    for (const auto& cid : *conds) {
                        if (cid.empty() || n >= kMaxFocus) break;
                        if (n) batched += ',';
                        ++n;
                        batched += json::str(cid);
                        batched += ":{\"book\":";
                        batched += payload::book_pair(sp, cid);
                        batched += ",\"quote\":";
                        batched += payload::quote(sp, cid);
                        batched += ",\"fills\":";
                        batched += payload::fills(sp, cid);
                        batched += '}';
                    }
                    batched += '}';

                    bool sent = false;
                    if (n > 0) {
                        if (!send_frame(sink, "detail", "snapshot", batched, fseq, gz)) return true;
                        sent = true;
                    }
                    since_send = sent ? 0 : (since_send + 1);
                    if (since_send >= kHeartbeatTicks) {
                        since_send = 0;
                        if (!send_frame(sink, "heartbeat", "delta", "{}")) return true;
                    }
                }
                return true;
            },
            [](bool /*success*/) { g_sse_clients.fetch_sub(1); });
    });

    // POST /api/v1/stream/focus?stream_id=..&seq=..&cids=cid1,cid2 — 告知服务端"我在看哪些盘口"
    //   (SSE 单向, 订阅意图走副 POST)。用 query 参数 (无 body/无自定义头 → simple request 免 CORS 预检)。
    //   评审: 校验 boot 前缀防跨重启误命中; find 持 mutex 取句柄后原子换 COW; cap kMaxFocus。
    svr.Post("/api/v1/stream/focus", [](const httplib::Request& req, httplib::Response& res) {
        const std::string sid = req.get_param_value("stream_id");
        if (sid.empty() || sid.rfind(g_boot_nonce + ":", 0) != 0) {
            res.status = 404;
            res.set_content(R"({"ok":false,"reason":"stale_or_unknown"})", "application/json; charset=utf-8");
            return;
        }
        std::int64_t fseq = 0;
        try { fseq = std::stoll(req.get_param_value("seq")); } catch (...) { fseq = 0; }

        std::vector<std::string> cids;
        const std::string raw = req.get_param_value("cids");
        std::size_t start = 0;
        while (start <= raw.size() && cids.size() < kMaxFocus) {
            const std::size_t comma = raw.find(',', start);
            const std::string c = raw.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!c.empty()) cids.push_back(c);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        std::shared_ptr<FocusState> st;
        {
            std::lock_guard<std::mutex> lk(g_focus_mu);
            const auto it = g_focus_reg.find(sid);
            if (it != g_focus_reg.end()) st = it->second;
        }
        if (!st) {
            res.status = 404;
            res.set_content(R"({"ok":false,"reason":"no_stream"})", "application/json; charset=utf-8");
            return;
        }
        const std::size_t n = cids.size();
        // provider 下一 tick 读 → 推 focused book/quote (带此 fseq)
        st->store(std::make_shared<const std::vector<std::string>>(std::move(cids)), fseq);
        std::string body = "{\"ok\":true,\"n\":";
        body += std::to_string(n);
        body += ",\"focus_seq\":";
        body += std::to_string(fseq);
        body += '}';
        res.set_content(body, "application/json; charset=utf-8");
    });

    // POST /api/v1/detail?cids=cid1,cid2,.. — 批量取 focus 盘 book/quote/fills (一次往返替 N×3 REST)。
    //   仅 SSE 冷启/兜底用 (hot 流未连/断时); 常态 book/quote 走 SSE detail 帧, 零轮询。
    //   query 参数 comma-list (simple request 免 CORS 预检, 同 focus POST)。
    svr.Post("/api/v1/detail", [&hs](const httplib::Request& req, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::string raw = req.get_param_value("cids");
        std::string out = "{";
        std::size_t start = 0, n = 0;
        bool first = true;
        while (start <= raw.size() && n < kMaxFocus) {
            const std::size_t comma = raw.find(',', start);
            const std::string cid = raw.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!cid.empty()) {
                if (!first) out += ',';
                first = false;
                ++n;
                out += json::str(cid);
                out += ":{\"book\":";
                out += payload::book_pair(sp, cid);
                out += ",\"quote\":";
                out += payload::quote(sp, cid);
                out += ",\"fills\":";
                out += payload::fills(sp, cid);
                out += '}';
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        out += '}';
        res.set_content(out, "application/json; charset=utf-8");
    });
}

}  // namespace stcpp::debug_api
