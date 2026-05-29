// src/stcpp/debug_api/live_book_publisher.hpp — CLOB WSS book 解析 → hub.Publish()
//
// Owner: 小冯 (#34)  -- live WSS 接入 debug_server --live 模式
// last_review: 2026-05-29
//
// 用途:
//   接收 PolymarketCLOBSubscriber 推送的 WssEvent (来自 CLOB market channel),
//   解析真实 bid/ask L2 数组 (CLOB book event 格式: JSON array 不含 L2 level), 直接
//   填充 OrderBookFeatures 并调用 hub.Publish().
//
//   与 OrderBookAdapter v0.1 的区别:
//     - OrderBookAdapter v0.1 只解析 mid_bps (合成 L1), 真实 L2 array 待 W10 simdjson.
//     - LiveBookPublisher 直接从 CLOB book JSON 中解析 bids/asks array (top-5 档).
//     - 两者不冲突: LiveBookPublisher 绕过 OrderBookAdapter 直接写 hub.
//
// CLOB book 消息格式 (实测 2026-05-29):
//   [{"market":"0x..","asset_id":"..","timestamp":"1780064037997","hash":"...",
//     "bids":[{"price":"0.01","size":"123258.86"},...],
//     "asks":[{"price":"0.99","size":"..."},...],
//     "event_type":"book",...}]
//
// 或单对象形式 (非数组):
//   {"market":"0x...","asset_id":"...","timestamp":"...","bids":[...],"asks":[...],"event_type":"book"}
//
// 红线:
//   R-20: data_source_ts = timestamp(ms) × 1e6 (UPSTREAM_PAYLOAD), 禁 now() 替代.
//         timestamp 缺失 → drop (不 fallback now()).
//   R-12: OnFrame 同步路径 (on_text_frame_ callback) — 无 malloc (复用 fixed buffer),
//         所有字符串操作 O(n) 受 payload 大小限制 (< 10KB typical book frame).
//
// 线程安全: OnFrame() 由 io_thread_ (LiveWssTransport) 同步调用;
//   hub_.Publish() 是 R-12 原子写 (double-buffer swap), 无锁.

#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/polymarket/wss/wss_event.hpp"  // FourTs, DataSourceTsOrigin

namespace stcpp::debug_api {

using stcpp::microstructure::kBookDepthLevels;
using stcpp::microstructure::OrderBookLevel;
using stcpp::polymarket::clob_wss::OrderBookFeatures;
using stcpp::polymarket::clob_wss::OrderBookSnapshotHub;
using stcpp::polymarket::clob_wss::WssConnState;

// ---------------------------------------------------------------------------
// LiveBookPublisher
//
// 用法 (debug_server_main.cpp --live 模式):
//   auto pub = std::make_unique<LiveBookPublisher>(hub, token_ids, verbose);
//   // 作为 on_text_frame_ callback:
//   transport->SetOnTextFrame(
//       [&pub](std::string_view payload, int64_t recv_ts) {
//           pub->OnFrame(payload, recv_ts);
//       });
// ---------------------------------------------------------------------------
class LiveBookPublisher {
public:
    explicit LiveBookPublisher(OrderBookSnapshotHub& hub, const std::vector<std::string>& subscribed_tokens,
                               bool verbose = false)
        : hub_(hub), subscribed_tokens_(subscribed_tokens), verbose_(verbose) {}

    // -----------------------------------------------------------------------
    // OnFrame — called by io_thread_ on each received WSS text frame
    //
    // Format: may be JSON array [...] or single object {...}
    //   CLOB market channel sends array of events.
    //   We iterate, find event_type=="book", parse bids/asks, hub.Publish().
    // -----------------------------------------------------------------------
    void OnFrame(std::string_view payload, std::int64_t recv_ts_ns) {
        frames_received_.fetch_add(1, std::memory_order_relaxed);

        // Trim leading whitespace
        std::size_t start = 0;
        while (start < payload.size() && std::isspace(static_cast<unsigned char>(payload[start]))) {
            ++start;
        }
        if (start >= payload.size())
            return;

        if (payload[start] == '[') {
            // Array of events
            // Simple approach: find each '{' that starts an object in the array
            ParseArray(payload, recv_ts_ns);
        } else if (payload[start] == '{') {
            ParseObject(payload, recv_ts_ns);
        }
        // else: unexpected format, ignore
    }

    // Metrics
    [[nodiscard]] std::uint64_t frames_received() const noexcept {
        return frames_received_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t books_published() const noexcept {
        return books_published_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t frames_dropped() const noexcept {
        return frames_dropped_.load(std::memory_order_relaxed);
    }

private:
    // -----------------------------------------------------------------------
    // ParseArray — handle JSON array [...] containing one or more event objects
    // -----------------------------------------------------------------------
    void ParseArray(std::string_view payload, std::int64_t recv_ts_ns) {
        // Find each top-level '{' inside the array, extract sub-object, parse.
        // Simple depth-tracking approach (no full JSON parser needed for this structure).
        std::size_t pos = 0;
        // skip '['
        while (pos < payload.size() && payload[pos] != '[')
            ++pos;
        ++pos;

        while (pos < payload.size()) {
            // Find next '{'
            while (pos < payload.size() && payload[pos] != '{') {
                if (payload[pos] == ']')
                    return;  // end of array
                ++pos;
            }
            if (pos >= payload.size())
                return;

            // Find matching '}' (depth tracking)
            std::size_t obj_start = pos;
            int depth = 0;
            bool in_string = false;
            bool escape = false;
            std::size_t obj_end = pos;
            for (std::size_t i = pos; i < payload.size(); ++i) {
                char c = payload[i];
                if (escape) {
                    escape = false;
                    continue;
                }
                if (in_string) {
                    if (c == '\\')
                        escape = true;
                    else if (c == '"')
                        in_string = false;
                    continue;
                }
                if (c == '"') {
                    in_string = true;
                    continue;
                }
                if (c == '{')
                    ++depth;
                else if (c == '}') {
                    --depth;
                    if (depth == 0) {
                        obj_end = i;
                        break;
                    }
                }
            }
            if (obj_end <= obj_start)
                break;

            auto obj = payload.substr(obj_start, obj_end - obj_start + 1);
            ParseObject(obj, recv_ts_ns);
            pos = obj_end + 1;
        }
    }

    // -----------------------------------------------------------------------
    // ParseObject — parse single CLOB book event object
    // -----------------------------------------------------------------------
    void ParseObject(std::string_view obj, std::int64_t recv_ts_ns) {
        // Check event_type == "book"
        std::string_view etype = ExtractStringField(obj, "event_type");
        if (etype != "book") {
            // price_change, last_trade_price, etc. — not handled here (no full L2 delta)
            return;
        }

        // Extract asset_id (= token_id)
        std::string_view asset_id = ExtractStringField(obj, "asset_id");
        if (asset_id.empty()) {
            frames_dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        std::string token_id(asset_id);

        // R-20: extract timestamp (ms) → data_source_ts_ns
        std::int64_t ts_ms = 0;
        if (!ExtractInt64(obj, "timestamp", ts_ms) || ts_ms <= 0) {
            // timestamp absent → drop (R-20 red line: no fallback now())
            frames_dropped_.fetch_add(1, std::memory_order_relaxed);
            if (verbose_) {
                std::fprintf(stderr, "[live_pub] DROP: token %s missing timestamp\n", token_id.c_str());
            }
            return;
        }

        const std::int64_t data_source_ts_ns = ts_ms * 1'000'000LL;

        // R-20 future guard (5s)
        if (data_source_ts_ns > recv_ts_ns + 5'000'000'000LL) {
            frames_dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // R-20 P0-2 fix: ingestion_ts = max(local_recv, data_source_ts)
        // 跨洋部署时本地时钟可能落后 Polymarket 服务端时钟 10-30ms，
        // 导致 recv_ts_ns < data_source_ts_ns (R-20 单调链倒挂)。
        // 修法: 承认时钟偏差，ingestion_ts 不早于 data_source_ts，保证 R-20 单调链。
        const std::int64_t ingestion_ts_ns =
            (recv_ts_ns >= data_source_ts_ns) ? recv_ts_ns : data_source_ts_ns;

        // Build OrderBookFeatures
        OrderBookFeatures feat{};
        feat.event_ts_ns = data_source_ts_ns;
        feat.data_source_ts_ns = data_source_ts_ns;
        feat.ingestion_ts_ns = ingestion_ts_ns;
        feat.as_of_ts_ns = ingestion_ts_ns;  // as_of filled by consumer; placeholder = ingestion
        feat.wss_state = WssConnState::kConnected;
        feat.valid = false;  // set true after valid bid/ask extracted

        // Parse bids[] array — CLOB format: sorted ascending by price
        // Best bid = highest price = last element; we want top-5 sorted bid DESC
        ParseLevels(obj, "bids", feat.bids, /*sort_desc=*/true);

        // Parse asks[] array — sorted ascending by price
        // Best ask = lowest price = first element
        ParseLevels(obj, "asks", feat.asks, /*sort_desc=*/false);

        // Validate: need at least 1 valid bid AND 1 valid ask
        const bool have_bid = std::isfinite(feat.bids[0].price) && feat.bids[0].price > 0.0;
        const bool have_ask = std::isfinite(feat.asks[0].price) && feat.asks[0].price > 0.0;

        if (have_bid && have_ask) {
            feat.valid = true;
            // Derived: spread, mid (no microprice — sizes are present in real data)
            double b0 = feat.bids[0].price;
            double a0 = feat.asks[0].price;
            feat.spread = a0 - b0;
            feat.mid = 0.5 * (b0 + a0);
            // imbalance from L1 if sizes finite
            double bs = feat.bids[0].size_usdc;
            double as_ = feat.asks[0].size_usdc;
            if (std::isfinite(bs) && std::isfinite(as_) && bs + as_ > 0.0) {
                feat.imbalance = (bs - as_) / (bs + as_);
                // microprice
                feat.microprice = (a0 * bs + b0 * as_) / (bs + as_);
            }
        }

        // hub.Publish (even if !valid, so hub knows the token exists with invalid state)
        hub_.Publish(token_id, feat);
        books_published_.fetch_add(1, std::memory_order_relaxed);

        if (verbose_) {
            std::fprintf(stderr, "[live_pub] Publish token=%.40s bid=%.4f ask=%.4f ts_ms=%lld\n",
                         token_id.c_str(), feat.bids[0].price, feat.asks[0].price,
                         static_cast<long long>(ts_ms));
        }
    }

    // -----------------------------------------------------------------------
    // ParseLevels — extract "bids" or "asks" JSON array → top-kBookDepthLevels levels
    //
    // CLOB format: [{"price":"0.01","size":"123258.86"},...]
    //   bids: sorted ascending by price (best bid = LAST element)
    //   asks: sorted ascending by price (best ask = FIRST element)
    //
    // sort_desc=true  → bids: we want descending (best=highest first)
    // sort_desc=false → asks: already ascending (best=lowest first)
    // -----------------------------------------------------------------------
    static void ParseLevels(std::string_view obj, std::string_view field_name,
                            std::array<OrderBookLevel, kBookDepthLevels>& out, bool sort_desc) {
        // Initialize to NaN
        for (auto& lvl : out) {
            lvl.price = std::numeric_limits<double>::quiet_NaN();
            lvl.size_usdc = std::numeric_limits<double>::quiet_NaN();
        }

        // Find "bids":[ or "asks":[
        // Look for "fieldname":
        char needle[32];
        std::snprintf(needle, sizeof(needle), "\"%.*s\":", static_cast<int>(field_name.size()),
                      field_name.data());
        std::size_t arr_start = obj.find(needle);
        if (arr_start == std::string_view::npos)
            return;
        arr_start += std::strlen(needle);
        // skip whitespace
        while (arr_start < obj.size() && std::isspace(static_cast<unsigned char>(obj[arr_start]))) {
            ++arr_start;
        }
        if (arr_start >= obj.size() || obj[arr_start] != '[')
            return;
        ++arr_start;

        // Parse entries: up to 256, but store only top kBookDepthLevels
        // Use simple temp storage
        static constexpr std::size_t kMaxLevels = 256;
        struct Lvl {
            double price;
            double size;
        };
        Lvl raw[kMaxLevels];
        std::size_t raw_count = 0;

        std::size_t pos = arr_start;
        while (pos < obj.size() && raw_count < kMaxLevels) {
            // Skip to '{'
            while (pos < obj.size() && obj[pos] != '{' && obj[pos] != ']')
                ++pos;
            if (pos >= obj.size() || obj[pos] == ']')
                break;

            // Find matching '}'
            std::size_t entry_start = pos;
            int depth = 0;
            std::size_t entry_end = pos;
            bool in_str = false;
            bool esc = false;
            for (std::size_t i = pos; i < obj.size(); ++i) {
                char c = obj[i];
                if (esc) {
                    esc = false;
                    continue;
                }
                if (in_str) {
                    if (c == '\\')
                        esc = true;
                    else if (c == '"')
                        in_str = false;
                    continue;
                }
                if (c == '"') {
                    in_str = true;
                    continue;
                }
                if (c == '{')
                    ++depth;
                else if (c == '}') {
                    --depth;
                    if (depth == 0) {
                        entry_end = i;
                        break;
                    }
                }
            }
            if (entry_end <= entry_start)
                break;

            auto entry = obj.substr(entry_start, entry_end - entry_start + 1);
            double price = ParseDoubleField(entry, "price");
            double size = ParseDoubleField(entry, "size");
            if (std::isfinite(price) && price > 0.0 && std::isfinite(size) && size >= 0.0) {
                raw[raw_count++] = {price, size};
            }
            pos = entry_end + 1;
        }

        if (raw_count == 0)
            return;

        // Sort: descending for bids (best = highest), ascending for asks (best = lowest)
        // Simple insertion sort (raw_count typically small)
        for (std::size_t i = 1; i < raw_count; ++i) {
            Lvl key = raw[i];
            std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i) - 1;
            if (sort_desc) {
                while (j >= 0 && raw[static_cast<std::size_t>(j)].price < key.price) {
                    raw[static_cast<std::size_t>(j + 1)] = raw[static_cast<std::size_t>(j)];
                    --j;
                }
            } else {
                while (j >= 0 && raw[static_cast<std::size_t>(j)].price > key.price) {
                    raw[static_cast<std::size_t>(j + 1)] = raw[static_cast<std::size_t>(j)];
                    --j;
                }
            }
            raw[static_cast<std::size_t>(j + 1)] = key;
        }

        // Fill top kBookDepthLevels
        const std::size_t n = std::min(raw_count, kBookDepthLevels);
        for (std::size_t i = 0; i < n; ++i) {
            out[i].price = raw[i].price;
            out[i].size_usdc = raw[i].size;
        }
    }

    // -----------------------------------------------------------------------
    // JSON field helpers (minimal, no malloc on hot path)
    // -----------------------------------------------------------------------

    // Extract string value for "key":"value"
    static std::string_view ExtractStringField(std::string_view body, std::string_view key) noexcept {
        // find "key":
        char needle[64];
        if (key.size() + 2 >= sizeof(needle))
            return {};
        needle[0] = '"';
        std::copy(key.begin(), key.end(), needle + 1);
        needle[key.size() + 1] = '"';
        needle[key.size() + 2] = '\0';

        std::size_t pk = body.find(std::string_view(needle, key.size() + 2));
        if (pk == std::string_view::npos)
            return {};
        std::size_t pos = pk + key.size() + 2;
        // skip ":"
        while (pos < body.size() && (body[pos] == ':' || body[pos] == ' '))
            ++pos;
        if (pos >= body.size() || body[pos] != '"')
            return {};
        ++pos;
        std::size_t end = body.find('"', pos);
        if (end == std::string_view::npos)
            return {};
        return body.substr(pos, end - pos);
    }

    // Extract int64 from quoted or unquoted value
    static bool ExtractInt64(std::string_view body, std::string_view key, std::int64_t& out) noexcept {
        char needle[64];
        if (key.size() + 2 >= sizeof(needle))
            return false;
        needle[0] = '"';
        std::copy(key.begin(), key.end(), needle + 1);
        needle[key.size() + 1] = '"';
        needle[key.size() + 2] = '\0';

        std::size_t pk = body.find(std::string_view(needle, key.size() + 2));
        if (pk == std::string_view::npos)
            return false;
        std::size_t pos = pk + key.size() + 2;
        while (pos < body.size() && (body[pos] == ':' || body[pos] == ' '))
            ++pos;
        if (pos >= body.size())
            return false;
        bool quoted = (body[pos] == '"');
        if (quoted)
            ++pos;
        if (pos >= body.size())
            return false;
        std::int64_t sign = 1;
        if (body[pos] == '-') {
            sign = -1;
            ++pos;
        }
        if (pos >= body.size() || !std::isdigit(static_cast<unsigned char>(body[pos])))
            return false;
        std::int64_t v = 0;
        while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos]))) {
            v = v * 10 + (body[pos] - '0');
            ++pos;
        }
        out = sign * v;
        return true;
    }

    // Extract double from quoted string field: {"price":"0.01"} or {"price":0.01}
    static double ParseDoubleField(std::string_view entry, std::string_view key) noexcept {
        std::string_view sv = ExtractStringField(entry, key);
        if (!sv.empty()) {
            char buf[32];
            if (sv.size() >= sizeof(buf))
                return std::numeric_limits<double>::quiet_NaN();
            std::copy(sv.begin(), sv.end(), buf);
            buf[sv.size()] = '\0';
            char* ep = nullptr;
            double v = std::strtod(buf, &ep);
            if (ep == buf)
                return std::numeric_limits<double>::quiet_NaN();
            return v;
        }
        // Try unquoted
        char needle[64];
        if (key.size() + 2 >= sizeof(needle))
            return std::numeric_limits<double>::quiet_NaN();
        needle[0] = '"';
        std::copy(key.begin(), key.end(), needle + 1);
        needle[key.size() + 1] = '"';
        needle[key.size() + 2] = '\0';
        std::size_t pk = entry.find(std::string_view(needle, key.size() + 2));
        if (pk == std::string_view::npos)
            return std::numeric_limits<double>::quiet_NaN();
        std::size_t pos = pk + key.size() + 2;
        while (pos < entry.size() && (entry[pos] == ':' || entry[pos] == ' '))
            ++pos;
        if (pos >= entry.size())
            return std::numeric_limits<double>::quiet_NaN();
        char buf[32];
        std::size_t n = 0;
        while (pos < entry.size() && n + 1 < sizeof(buf) &&
               (std::isdigit(static_cast<unsigned char>(entry[pos])) || entry[pos] == '.' ||
                entry[pos] == '-' || entry[pos] == '+')) {
            buf[n++] = entry[pos++];
        }
        if (n == 0)
            return std::numeric_limits<double>::quiet_NaN();
        buf[n] = '\0';
        char* ep = nullptr;
        double v = std::strtod(buf, &ep);
        if (ep == buf)
            return std::numeric_limits<double>::quiet_NaN();
        return v;
    }

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------
    OrderBookSnapshotHub& hub_;
    std::vector<std::string> subscribed_tokens_;
    bool verbose_;

    std::atomic<std::uint64_t> frames_received_{0};
    std::atomic<std::uint64_t> books_published_{0};
    std::atomic<std::uint64_t> frames_dropped_{0};
};

}  // namespace stcpp::debug_api
