// include/stcpp/polymarket/live/live_user_fill_feed.hpp
//
// Owner: GM (老雷) 2026-06-13  | Phase 2 — live 成交异步入账 (决策环零网络)
//
// LiveUserFillFeed — Polymarket CLOB user 频道【自己的成交回执】异步接收器。
//
// 背景 (真钱事故配套): live 下单同步 HTTP POST 阻塞决策环 ~14ms/单; 且成交回执若只走同步回执,
//   一旦下单异步化就拿不到成交。正解 = 订阅 CLOB user 频道 (wss://.../ws/user), 异步收【我们自己的】
//   trade 事件 → 解析 → 去重 → 非阻塞队列, 决策环 (loop_thread) 每 tick 排空 → apply_fill 登持仓。
//   网络 + 解析全在 transport io_thread; loop_thread 只 Pop (mutex 锁 < 1us, 成交低频, R-12 合规)。
//
// 官方 schema (docs.polymarket.com/.../websocket/user-channel, 2026-06-13 核实):
//   trade 字段: id(UUID), status, asset_id(token_id), market(condition_id), outcome, side(BUY/SELL),
//               size, price, taker_order_id, maker_orders[], timestamp, event_type:"trade"
//   status 生命周期: MATCHED → MINED → CONFIRMED(终态) / FAILED(终态); MINED↔RETRYING。
//   ⚠ 同一 trade 随 status 变化【多次推送】→ 必须按 id 去重 + 【只在 CONFIRMED 入账】
//     (终态=链上已确认=匹配链真相, 永不需要 unwind; MATCHED 早但可能 FAILED)。
//   我们永远是 taker (FOK marketable) → 全额按本边 size 记。
//
// auth (spec laoli-w9 §3.1, 对照 py-clob-client 核实): payload 内明文 apiKey/secret/passphrase,
//   【非 HMAC】, 走 TLS 不降级。P-09: subscribe frame 严禁落日志 (含凭证)。
//
// 线程模型:
//   - producer: transport io_thread → OnFrame (解析 + 去重) → 入 mutex 队列。seen-set 仅 producer 访问无锁。
//   - consumer: loop_thread → Pop (排空队列, 登持仓)。
//   R-12: OnFrame 同步路径无阻塞 IO / 锁 < 100us (一次 mutex push + 有界 seen-set)。
#pragma once

#include <atomic>
#include <charconv>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "stcpp/polymarket/wss/pm_wss_subscriber.hpp"  // IWssTransport

namespace stcpp::polymarket {

// 一笔【已确认 (CONFIRMED)】的自有成交 (从 user 频道 trade 事件解析)。
struct UserFill {
    std::string condition_id;  // "market"
    std::string token_id;      // "asset_id"
    std::string order_id;      // "taker_order_id" (我们是 taker)
    std::string trade_id;      // "id" (UUID; 去重键)
    bool is_buy{false};        // side == "BUY"
    bool is_yes{false};        // outcome == "YES"/"Yes"
    double price{0.0};         // 成交价 (0..1)
    double size{0.0};          // 股数 (shares)
    double fee{0.0};           // USDC fee (有则记, 无则 0)
    std::int64_t data_source_ts_ns{0};  // "timestamp"(ms) × 1e6 (R-20 上游 ts)
    std::int64_t recv_ts_ns{0};         // transport 收帧本地 ts
};

class LiveUserFillFeed {
public:
    LiveUserFillFeed(std::unique_ptr<polymarket::wss::IWssTransport> transport, std::string api_key,
                     std::string api_secret, std::string api_passphrase)
        : transport_(std::move(transport)),
          api_key_(std::move(api_key)),
          api_secret_(std::move(api_secret)),
          api_passphrase_(std::move(api_passphrase)) {}

    LiveUserFillFeed(const LiveUserFillFeed&) = delete;
    LiveUserFillFeed& operator=(const LiveUserFillFeed&) = delete;

    // 连接 + 订阅初始 condition_ids (auth 在 OnConnected 重发, 含重连 replay)。
    bool Start(const std::string& user_url, const std::vector<std::string>& condition_ids) {
        if (!transport_)
            return false;
        url_ = user_url;
        {
            std::lock_guard<std::mutex> lk(sub_mu_);
            for (const auto& c : condition_ids)
                sub_conditions_.insert(c);
        }
        transport_->SetOnTextFrame([this](std::string_view p, std::int64_t ts) { OnFrame(p, ts); });
        transport_->SetOnConnected([this]() { OnConnected(); });
        transport_->SetOnDisconnected([this](std::string_view) { connected_.store(false, std::memory_order_release); });
        return transport_->AsyncConnect(url_);
    }

    // 心跳 (user 频道无事件时静默, 10s PING 维持存活感知; daemon watchdog 驱动)。
    void Ping() {
        if (transport_)
            transport_->AsyncSendText("PING");
    }
    // 重连 (daemon watchdog: 断开/半死时调; transport.AsyncConnect 自 join 旧 io_thread; OnConnected 重发订阅)。
    void Reconnect() {
        if (transport_ && !url_.empty())
            transport_->AsyncConnect(url_);
    }

    // 动态追加订阅 (新持仓市场未订阅时)。立即发追加帧 + 记录供重连 replay。
    void SubscribeMarkets(std::span<const std::string> condition_ids) {
        std::vector<std::string> fresh;
        {
            std::lock_guard<std::mutex> lk(sub_mu_);
            for (const auto& c : condition_ids) {
                if (sub_conditions_.size() >= kSubCap)
                    break;  // L4: 防多周长跑无界增长 (实际不可达, 几百盘/会话); 不淘汰旧订阅 → 不丢持仓盘成交
                if (sub_conditions_.insert(c).second)
                    fresh.push_back(c);
            }
        }
        if (!fresh.empty() && transport_ && transport_->IsConnected())
            transport_->AsyncSendText(MakeSubscribeFrame(fresh));
    }

    // 决策环排空 (loop_thread)。返回 false = 空。
    [[nodiscard]] bool Pop(UserFill& out) noexcept {
        std::lock_guard<std::mutex> lk(q_mu_);
        if (queue_.empty())
            return false;
        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void Stop() noexcept {
        if (transport_)
            transport_->Close();
        connected_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool connected() const noexcept { return connected_.load(std::memory_order_acquire); }
    [[nodiscard]] std::int64_t last_msg_ts_ns() const noexcept {
        return last_msg_ts_ns_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t booked_count() const noexcept { return booked_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t dup_count() const noexcept { return dups_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t dropped_count() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    // ---- 可测试纯解析 (静态): 仅 status==CONFIRMED 的 trade 返回 true + 填 out; 其余 (中间态/FAILED/
    //      非 trade/字段缺失) 返回 false。无副作用, 不去重 (去重在 OnFrame)。 ----
    [[nodiscard]] static bool ParseConfirmedTrade(std::string_view body, std::int64_t recv_ts_ns,
                                                  UserFill& out) noexcept {
        // 必须是 trade 事件 (event_type 或 type)。
        const std::string_view etype = ExtractStr(body, "event_type");
        const std::string_view type = ExtractStr(body, "type");
        const bool is_trade = (etype == "trade") || (type == "TRADE") || (type == "trade");
        if (!is_trade)
            return false;
        // 只认终态 CONFIRMED (同一 trade 多次推送; MATCHED/MINED/RETRYING 跳过, FAILED 永不入账)。
        if (ExtractStr(body, "status") != "CONFIRMED")
            return false;

        UserFill f;
        f.recv_ts_ns = recv_ts_ns;
        f.trade_id = std::string(ExtractStr(body, "id"));
        f.condition_id = std::string(ExtractStr(body, "market"));
        f.token_id = std::string(ExtractStr(body, "asset_id"));
        f.order_id = std::string(ExtractStr(body, "taker_order_id"));
        const std::string_view side = ExtractStr(body, "side");
        f.is_buy = (side == "BUY" || side == "buy");
        const std::string_view oc = ExtractStr(body, "outcome");
        f.is_yes = (oc == "YES" || oc == "Yes" || oc == "yes");
        f.price = ExtractNum(body, "price");
        f.size = ExtractNum(body, "size");
        f.fee = ExtractNum(body, "fee");  // 缺则 0
        // R-20: data_source_ts = timestamp(ms) × 1e6 (上游 ts, 非本地 now)。
        const double ts_ms = ExtractNum(body, "timestamp");
        f.data_source_ts_ns =
            (ts_ms > 0.0) ? static_cast<std::int64_t>(ts_ms * 1'000'000.0) : recv_ts_ns;

        // 必要字段健全性: token/condition/id 非空, size>0, price∈(0,1)。
        if (f.trade_id.empty() || f.condition_id.empty() || f.token_id.empty())
            return false;
        if (!(f.size > 0.0) || !(f.price > 0.0 && f.price < 1.0))
            return false;
        out = std::move(f);
        return true;
    }

private:
    void OnConnected() {
        connected_.store(true, std::memory_order_release);
        // P-08: 重连重发 subscribe (含 auth + 全部已订 condition)。
        std::vector<std::string> all;
        {
            std::lock_guard<std::mutex> lk(sub_mu_);
            all.assign(sub_conditions_.begin(), sub_conditions_.end());
        }
        if (!all.empty() && transport_)
            transport_->AsyncSendText(MakeSubscribeFrame(all));
    }

    void OnFrame(std::string_view payload, std::int64_t recv_ts_ns) {
        last_msg_ts_ns_.store(recv_ts_ns, std::memory_order_relaxed);
        if (payload == "PONG" || payload == "pong" || payload.empty())
            return;
        UserFill f;
        if (!ParseConfirmedTrade(payload, recv_ts_ns, f))
            return;  // 非 CONFIRMED trade / 字段不全 → 忽略 (无副作用)
        // 去重 (seen-set 仅 producer 访问, 无锁): 同一 trade id 多次推送只入账一次。
        if (seen_.find(f.trade_id) != seen_.end()) {
            dups_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        RememberSeen(f.trade_id);
        {
            std::lock_guard<std::mutex> lk(q_mu_);
            if (queue_.size() >= kMaxQueue) {
                dropped_.fetch_add(1, std::memory_order_relaxed);  // 永不该发生 (loop 每 tick 排空)
                return;
            }
            queue_.push(std::move(f));
        }
        booked_.fetch_add(1, std::memory_order_relaxed);
    }

    // 有界去重: 保留最近 kSeenCap 个 trade_id (FIFO 淘汰)。成交低频 → cap 足够大覆盖数天。
    void RememberSeen(const std::string& id) {
        seen_.insert(id);
        seen_fifo_.push_back(id);
        if (seen_fifo_.size() > kSeenCap) {
            seen_.erase(seen_fifo_.front());
            seen_fifo_.pop_front();
        }
    }

    std::string MakeSubscribeFrame(const std::vector<std::string>& cids) const {
        // spec §3.1: {"type":"User","auth":{apiKey,secret,passphrase},"markets":[cid...]} (P-09 不落日志)
        std::string out;
        out.reserve(256 + cids.size() * 72);
        out.append(R"({"type":"User","auth":{"apiKey":")");
        out.append(api_key_);
        out.append(R"(","secret":")");
        out.append(api_secret_);
        out.append(R"(","passphrase":")");
        out.append(api_passphrase_);
        out.append(R"("},"markets":[)");
        bool first = true;
        for (const auto& c : cids) {
            if (!first)
                out.push_back(',');
            out.push_back('"');
            out.append(c);
            out.push_back('"');
            first = false;
        }
        out.append("]}");
        return out;
    }

    // ---- JSON 顶层字段抽取 (S2 修: brace-depth 感知, 只取 depth==1 的 key) ----
    //   防 maker_orders[] 等嵌套对象内的同名字段 (price/asset_id/outcome/fee...) 污染顶层抽取。
    //   不依赖 key 顺序 (JSON 不保证); 跳过嵌套对象/数组 + 字符串值。P-02: 数字由 ExtractNum 走 from_chars。
    //   返回顶层 "key" 的值 string_view (字符串去引号 / 裸值原样)。无则空。
    [[nodiscard]] static std::string_view ExtractStr(std::string_view body, std::string_view key) noexcept {
        const std::size_t n = body.size();
        int depth = 0;
        std::size_t i = 0;
        while (i < n) {
            const char c = body[i];
            if (c == '{' || c == '[') {
                ++depth;
                ++i;
                continue;
            }
            if (c == '}' || c == ']') {
                --depth;
                ++i;
                continue;
            }
            if (c == '"') {
                // 扫一个字符串 token [ks, j)
                const std::size_t ks = i + 1;
                std::size_t j = ks;
                while (j < n) {
                    if (body[j] == '\\') {
                        j += 2;
                        continue;
                    }
                    if (body[j] == '"')
                        break;
                    ++j;
                }
                // 仅 depth==1 且其后紧跟 ':' → 顶层 key (值后跟 , } 不会误判)。
                if (depth == 1) {
                    std::size_t p = (j < n) ? j + 1 : n;
                    while (p < n && (body[p] == ' ' || body[p] == '\t'))
                        ++p;
                    if (p < n && body[p] == ':' && body.substr(ks, j - ks) == key) {
                        ++p;  // 跳 ':'
                        while (p < n && (body[p] == ' ' || body[p] == '\t'))
                            ++p;
                        if (p >= n)
                            return {};
                        if (body[p] == '"') {
                            const std::size_t vs = p + 1;
                            std::size_t q = vs;
                            while (q < n) {
                                if (body[q] == '\\') {
                                    q += 2;
                                    continue;
                                }
                                if (body[q] == '"')
                                    break;
                                ++q;
                            }
                            return body.substr(vs, (q < n ? q : n) - vs);
                        }
                        const std::size_t vs = p;
                        while (p < n && body[p] != ',' && body[p] != '}' && body[p] != ']' && body[p] != ' ')
                            ++p;
                        return body.substr(vs, p - vs);
                    }
                }
                i = (j < n) ? j + 1 : n;  // 跳过本字符串 token (非目标 key / 值 / 嵌套)
                continue;
            }
            ++i;
        }
        return {};
    }

    [[nodiscard]] static double ExtractNum(std::string_view body, std::string_view key) noexcept {
        const std::string_view sv = ExtractStr(body, key);  // 字符串或裸值均已去引号
        if (sv.empty())
            return 0.0;
        double v = 0.0;
        const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        (void)ptr;
        return (ec == std::errc{}) ? v : 0.0;
    }

    static constexpr std::size_t kMaxQueue = 4096;  // loop 每 tick 排空, 永不该满
    static constexpr std::size_t kSeenCap = 20000;  // 去重窗 (成交低频 → 覆盖数天)
    static constexpr std::size_t kSubCap = 5000;    // 订阅集上限 (防多周长跑无界; 几百盘/会话远不可达)

    std::unique_ptr<polymarket::wss::IWssTransport> transport_;
    const std::string api_key_, api_secret_, api_passphrase_;
    std::string url_;  // user 频道 URL (Start 时记录, Reconnect 复用)

    std::atomic<bool> connected_{false};
    std::atomic<std::int64_t> last_msg_ts_ns_{0};
    std::atomic<std::uint64_t> booked_{0}, dups_{0}, dropped_{0};

    std::mutex q_mu_;
    std::queue<UserFill> queue_;

    std::mutex sub_mu_;
    std::unordered_set<std::string> sub_conditions_;

    // 去重 (仅 producer/io_thread 访问, 无锁)
    std::unordered_set<std::string> seen_;
    std::deque<std::string> seen_fifo_;
};

}  // namespace stcpp::polymarket
