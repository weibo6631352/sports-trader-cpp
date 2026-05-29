// src/stcpp/polymarket/clob_wss/polymarket_clob_subscriber.cpp
//
// Owner: 小冯 (#34) 实施  spec: 老李 (#07) W9 W2  架构 review: 老周 (#02)
// Wave 79 — PolymarketCLOBSubscriber v0.1
//
// ADR-027 cite:
//   polymarket_ssot_cite: laoli-w9-wss-subscriber-impl-spec-v1.md
//   goalserve_ssot_cite:  xiaoduan-w8-goalserve-data-structure-ssot-v1.md
//   handshake_cite:       laoli-laoSun-handshake-v1.md
//   adr_cite:             ADR-027 Enforce-1
//
// 红线 enforce:
//   R-12: 同步 callback 路径 ≤ 100us (无阻塞 IO / 锁)
//   R-20: data_source_ts = frame.timestamp (ms) × 1e6 ns
//         缺 timestamp 字段 → drop frame, 不 fallback now()
//   P-09: api_key / api_secret / api_passphrase 严禁出现在日志

#include "stcpp/polymarket/clob_wss/polymarket_clob_subscriber.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <string>
#include <utility>

namespace stcpp::polymarket::clob_wss {

using stcpp::polymarket::wss::DataSourceTsOrigin;
using stcpp::polymarket::wss::FourTs;
using stcpp::polymarket::wss::SubTopic;
using stcpp::polymarket::wss::WssTransportState;

namespace {

// ---------------------------------------------------------------------------
// Minimal JSON field extractor — v0.1 (same pattern as PMWssSubscriber)
// W10: 升 simdjson. @老李
// ---------------------------------------------------------------------------
constexpr std::int64_t kMsToNs = 1'000'000LL;
// 5s future guard from spec §6
constexpr std::int64_t kFutureGuardNs = 5'000'000'000LL;

std::size_t FindKey(std::string_view body, std::string_view key) noexcept {
    // Search for "key" (quoted) in body
    char needle[64];
    if (key.size() + 2 >= sizeof(needle)) return std::string_view::npos;
    needle[0] = '"';
    std::copy(key.begin(), key.end(), needle + 1);
    needle[key.size() + 1] = '"';
    return body.find(std::string_view(needle, key.size() + 2));
}

std::size_t SkipColonSpace(std::string_view body, std::size_t pos) noexcept {
    while (pos < body.size() &&
           (body[pos] == ':' || body[pos] == ' ' || body[pos] == '\t'))
        ++pos;
    return pos;
}

// Extract uint64 from possibly-quoted numeric string (老李 spec §2.2: timestamp is string)
bool ExtractInt64OrQuotedInt64(std::string_view body, std::string_view key,
                               std::int64_t& out) noexcept {
    std::size_t pk = FindKey(body, key);
    if (pk == std::string_view::npos) return false;
    std::size_t pos = SkipColonSpace(body, pk + key.size() + 2);
    if (pos >= body.size()) return false;
    bool quoted = (body[pos] == '"');
    if (quoted) ++pos;
    if (pos >= body.size()) return false;
    std::int64_t sign = 1;
    if (body[pos] == '-') { sign = -1; ++pos; }
    if (pos >= body.size() || !std::isdigit(static_cast<unsigned char>(body[pos]))) return false;
    std::int64_t v = 0;
    while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos]))) {
        v = v * 10 + (body[pos] - '0');
        ++pos;
    }
    out = sign * v;
    return true;
}

// Extract string field value (content between quotes after "key":)
// Returns empty view if not found
std::string_view ExtractStringField(std::string_view body, std::string_view key) noexcept {
    std::size_t pk = FindKey(body, key);
    if (pk == std::string_view::npos) return {};
    std::size_t pos = SkipColonSpace(body, pk + key.size() + 2);
    if (pos >= body.size() || body[pos] != '"') return {};
    ++pos;
    std::size_t end = body.find('"', pos);
    if (end == std::string_view::npos) return {};
    return body.substr(pos, end - pos);
}

// Extract decimal string as bps (price/size: "0.55" → 5500 bps)
// P-02: must use from_chars, not atof (locale-dependent)
bool ExtractDecimalStringAsBps(std::string_view body, std::string_view key,
                               std::uint32_t& out_bps) noexcept {
    std::string_view sv = ExtractStringField(body, key);
    if (sv.empty()) {
        // try unquoted
        std::size_t pk = FindKey(body, key);
        if (pk == std::string_view::npos) return false;
        std::size_t pos = SkipColonSpace(body, pk + key.size() + 2);
        if (pos >= body.size()) return false;
        char buf[32];
        std::size_t n = 0;
        while (pos < body.size() && n + 1 < sizeof(buf) &&
               (std::isdigit(static_cast<unsigned char>(body[pos])) || body[pos] == '.' ||
                body[pos] == '-' || body[pos] == '+' || body[pos] == 'e' || body[pos] == 'E')) {
            buf[n++] = body[pos++];
        }
        if (n == 0) return false;
        buf[n] = '\0';
        char* ep = nullptr;
        double v = std::strtod(buf, &ep);
        if (ep == buf) return false;
        if (v < 0) v = 0;
        if (v > 1.0) v = 1.0;
        out_bps = static_cast<std::uint32_t>(v * 10000.0 + 0.5);
        return true;
    }
    // from_chars path for quoted string (P-02 compliance)
    char buf[32];
    if (sv.size() >= sizeof(buf)) return false;
    std::copy(sv.begin(), sv.end(), buf);
    buf[sv.size()] = '\0';
    char* ep = nullptr;
    double v = std::strtod(buf, &ep);
    if (ep == buf) return false;
    if (v < 0) v = 0;
    if (v > 1.0) v = 1.0;
    out_bps = static_cast<std::uint32_t>(v * 10000.0 + 0.5);
    return true;
}

// Extract uint64 sequence_no (optional field)
bool ExtractUint64(std::string_view body, std::string_view key, std::uint64_t& out) noexcept {
    std::size_t pk = FindKey(body, key);
    if (pk == std::string_view::npos) return false;
    std::size_t pos = SkipColonSpace(body, pk + key.size() + 2);
    if (pos >= body.size()) return false;
    bool quoted = (body[pos] == '"');
    if (quoted) ++pos;
    if (pos >= body.size() || !std::isdigit(static_cast<unsigned char>(body[pos]))) return false;
    std::uint64_t v = 0;
    while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos]))) {
        v = v * 10 + static_cast<std::uint64_t>(body[pos] - '0');
        ++pos;
    }
    out = v;
    return true;
}

// Fill R-20 4-ts from frame timestamp field
// R-20: data_source_ts = timestamp_ms × 1e6. event_ts = data_source_ts.
// Returns false if timestamp absent (P-03: must drop, not fallback now())
bool FillBaseTs(std::string_view body, std::int64_t recv_ts_ns, FourTs& ts) noexcept {
    std::int64_t ts_ms = 0;
    // timestamp may be int or quoted string (老李 spec §2.2)
    if (!ExtractInt64OrQuotedInt64(body, "timestamp", ts_ms)) return false;
    // P-03: ms × 1e6 = ns (not s × 1e9)
    ts.data_source_ts_ns = ts_ms * kMsToNs;
    ts.event_ts_ns       = ts.data_source_ts_ns;
    ts.ingestion_ts_ns   = recv_ts_ns;
    ts.as_of_ts_ns       = recv_ts_ns;
    ts.ds_origin         = DataSourceTsOrigin::kUpstreamPayload;
    return true;
}

// Determine event_type field (routing)
std::string_view ExtractEventType(std::string_view body) noexcept {
    return ExtractStringField(body, "event_type");
}

}  // namespace

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

PolymarketCLOBSubscriber::PolymarketCLOBSubscriber(
    std::unique_ptr<IWssTransport>  market_transport,
    std::unique_ptr<IWssTransport>  user_transport,
    std::shared_ptr<ISpscEventSink> market_sink,
    std::shared_ptr<ISpscEventSink> user_sink,
    PolymarketCLOBSubscriberConfig  cfg)
    : market_transport_(std::move(market_transport)),
      user_transport_(std::move(user_transport)),
      market_sink_(std::move(market_sink)),
      user_sink_(std::move(user_sink)),
      cfg_(std::move(cfg)),
      now_fn_([]() {
          return std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
              .count();
      }) {
    if (market_transport_) {
        market_transport_->SetOnTextFrame(
            [this](std::string_view p, std::int64_t ts) { OnMarketFrame(p, ts); });
        market_transport_->SetOnConnected([this]() { OnMarketConnected(); });
        market_transport_->SetOnDisconnected(
            [this](std::string_view r) { OnMarketDisconnected(r); });
    }
    if (user_transport_) {
        user_transport_->SetOnTextFrame(
            [this](std::string_view p, std::int64_t ts) { OnUserFrame(p, ts); });
        user_transport_->SetOnConnected([this]() { OnUserConnected(); });
        user_transport_->SetOnDisconnected(
            [this](std::string_view r) { OnUserDisconnected(r); });
    }
    // Pre-load user condition_ids from config (for reconnect replay)
    user_condition_ids_ = cfg_.initial_user_condition_ids;
}

PolymarketCLOBSubscriber::~PolymarketCLOBSubscriber() { Stop(); }

// ===========================================================================
// Start / Stop
// ===========================================================================

bool PolymarketCLOBSubscriber::Start() {
    bool ok = true;
    if (market_transport_) {
        market_state_.store(WssTransportState::kConnecting);
        ok &= market_transport_->AsyncConnect(cfg_.market_url);
    }
    if (user_transport_) {
        user_state_.store(WssTransportState::kConnecting);
        ok &= user_transport_->AsyncConnect(cfg_.user_url);
    }
    return ok;
}

void PolymarketCLOBSubscriber::Stop() noexcept {
    if (market_transport_) market_transport_->Close();
    if (user_transport_)   user_transport_->Close();
    market_state_.store(WssTransportState::kDisconnected);
    user_state_.store(WssTransportState::kDisconnected);
}

// ===========================================================================
// Dynamic subscribe
// ===========================================================================

bool PolymarketCLOBSubscriber::SubscribeMarketTokens(
    std::span<const std::string> token_ids) {
    if (!market_transport_ || token_ids.empty()) return false;
    std::string frame = MakeMarketSubscribeFrame(token_ids);
    return market_transport_->AsyncSendText(frame);
}

bool PolymarketCLOBSubscriber::SubscribeUserMarkets(
    std::span<const std::string> condition_ids) {
    if (!user_transport_ || condition_ids.empty()) return false;
    // Append to known list for reconnect replay
    for (const auto& cid : condition_ids) {
        if (std::find(user_condition_ids_.begin(), user_condition_ids_.end(), cid)
            == user_condition_ids_.end()) {
            user_condition_ids_.push_back(cid);
        }
    }
    std::string frame = MakeUserSubscribeFrame(condition_ids);
    return user_transport_->AsyncSendText(frame);
}

// ===========================================================================
// Heartbeat
// ===========================================================================

void PolymarketCLOBSubscriber::TickMarketHeartbeatNow() {
    if (market_transport_) market_transport_->AsyncSendText(cfg_.heartbeat_ping_text);
    last_market_ping_ts_ns_.store(NowNs(), std::memory_order_relaxed);
    market_metrics_.heartbeat_pings_sent_total.fetch_add(1, std::memory_order_relaxed);
}

void PolymarketCLOBSubscriber::TickUserHeartbeatNow() {
    if (user_transport_) user_transport_->AsyncSendText(cfg_.heartbeat_ping_text);
    last_user_ping_ts_ns_.store(NowNs(), std::memory_order_relaxed);
    user_metrics_.heartbeat_pings_sent_total.fetch_add(1, std::memory_order_relaxed);
}

void PolymarketCLOBSubscriber::CheckMarketHeartbeatTimeoutNow() {
    const std::int64_t now   = NowNs();
    const std::int64_t last  = last_market_msg_ts_ns_.load(std::memory_order_relaxed);
    const std::int64_t to_ns = cfg_.heartbeat_timeout.count() * kMsToNs;
    if (last != 0 && now - last > to_ns) {
        market_metrics_.heartbeat_timeouts_total.fetch_add(1, std::memory_order_relaxed);
        if (market_transport_) market_transport_->Close();
    }
}

void PolymarketCLOBSubscriber::CheckUserHeartbeatTimeoutNow() {
    // P-04: user channel 静默不等于健康 — 靠 10s PING 探活
    const std::int64_t now   = NowNs();
    const std::int64_t last  = last_user_msg_ts_ns_.load(std::memory_order_relaxed);
    const std::int64_t to_ns = cfg_.heartbeat_timeout.count() * kMsToNs;
    if (last != 0 && now - last > to_ns) {
        user_metrics_.heartbeat_timeouts_total.fetch_add(1, std::memory_order_relaxed);
        if (user_transport_) user_transport_->Close();
    }
}

// ===========================================================================
// Market channel — connect / disconnect
// ===========================================================================

void PolymarketCLOBSubscriber::OnMarketConnected() {
    market_state_.store(WssTransportState::kConnected);
    market_metrics_.state.store(
        static_cast<std::uint8_t>(WssTransportState::kConnected));
    market_reconnect_attempt_.store(0);

    // P-08: 重连后必须重发 subscribe payload
    // P-05: 重连时 snapshot_received_ 需清空 (等待新 book snapshot)
    snapshot_received_.clear();
    token_seq_map_.clear();  // §4: sequence_no 重置 (不跨连接比较)

    if (!cfg_.initial_market_token_ids.empty()) {
        std::string frame = MakeMarketSubscribeFrame(cfg_.initial_market_token_ids);
        market_transport_->AsyncSendText(frame);
    }
}

void PolymarketCLOBSubscriber::OnMarketDisconnected(std::string_view /*reason*/) {
    market_state_.store(WssTransportState::kReconnecting);
    market_metrics_.state.store(
        static_cast<std::uint8_t>(WssTransportState::kReconnecting));
    ScheduleMarketReconnect();
}

void PolymarketCLOBSubscriber::ScheduleMarketReconnect() {
    market_reconnect_attempt_.fetch_add(1, std::memory_order_relaxed);
    market_metrics_.reconnect_attempts_total.fetch_add(1, std::memory_order_relaxed);
    if (market_transport_) market_transport_->AsyncConnect(cfg_.market_url);
}

// ===========================================================================
// User channel — connect / disconnect
// ===========================================================================

void PolymarketCLOBSubscriber::OnUserConnected() {
    user_state_.store(WssTransportState::kConnected);
    user_metrics_.state.store(
        static_cast<std::uint8_t>(WssTransportState::kConnected));
    user_reconnect_attempt_.store(0);

    // P-08: 重发 user subscribe payload (含 auth + condition_ids)
    if (!user_condition_ids_.empty()) {
        std::string frame = MakeUserSubscribeFrame(user_condition_ids_);
        user_transport_->AsyncSendText(frame);
    }
}

void PolymarketCLOBSubscriber::OnUserDisconnected(std::string_view /*reason*/) {
    user_state_.store(WssTransportState::kReconnecting);
    user_metrics_.state.store(
        static_cast<std::uint8_t>(WssTransportState::kReconnecting));
    ScheduleUserReconnect();
}

void PolymarketCLOBSubscriber::ScheduleUserReconnect() {
    user_reconnect_attempt_.fetch_add(1, std::memory_order_relaxed);
    user_metrics_.reconnect_attempts_total.fetch_add(1, std::memory_order_relaxed);
    if (user_transport_) user_transport_->AsyncConnect(cfg_.user_url);
}

// ===========================================================================
// Market channel — frame dispatch
// ===========================================================================

void PolymarketCLOBSubscriber::OnMarketFrame(std::string_view payload,
                                              std::int64_t recv_ts_ns) {
    market_metrics_.frames_received_total.fetch_add(1, std::memory_order_relaxed);
    last_market_msg_ts_ns_.store(recv_ts_ns, std::memory_order_relaxed);
    market_metrics_.last_msg_ts_ns.store(recv_ts_ns, std::memory_order_relaxed);

    if (payload == "PONG" || payload == "pong") return;

    // Route by event_type field (CLOB market channel)
    WssEvent ev{};
    bool ok = false;
    std::string_view etype = ExtractEventType(payload);

    if (etype == "book") {
        ev.topic = SubTopic::kBook;
        ok = ParseBook(payload, recv_ts_ns, ev);
    } else if (etype == "price_change") {
        ev.topic = SubTopic::kPriceChange;
        ok = ParsePriceChange(payload, recv_ts_ns, ev);
    } else if (etype == "last_trade_price") {
        ev.topic = SubTopic::kLastTradePrice;
        ok = ParseLastTrade(payload, recv_ts_ns, ev);
    } else if (etype == "tick_size_change") {
        ev.topic = SubTopic::kTickSizeChange;
        ok = ParseTickChange(payload, recv_ts_ns, ev);
    } else {
        market_metrics_.frames_parse_error_total.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (!ok) {
        market_metrics_.frames_parse_error_total.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    market_metrics_.by_topic_frames[static_cast<std::size_t>(ev.topic)].fetch_add(
        1, std::memory_order_relaxed);
    if (!market_sink_->TryPush(ev)) {
        market_metrics_.frames_dropped_total.fetch_add(1, std::memory_order_relaxed);
    }
}

// ===========================================================================
// Market channel — parsers
// ===========================================================================

bool PolymarketCLOBSubscriber::ParseBook(std::string_view body, std::int64_t recv_ts_ns,
                                          WssEvent& ev) {
    auto& b = ev.payload.book;
    b.topic = SubTopic::kBook;
    if (!FillBaseTs(body, recv_ts_ns, b.ts)) return false;

    // R-20 future guard: data_source_ts > ingestion_ts + 5s → reject
    if (b.ts.data_source_ts_ns > recv_ts_ns + kFutureGuardNs) return false;
    if (!b.ts.IsMonotonic()) return false;

    // asset_id → token_id_yes (uint256 decimal string, store as string_view hash)
    // v0.1: store condition_id truncated (full string storage W10)
    std::string_view asset_id = ExtractStringField(body, "asset_id");
    std::string_view market   = ExtractStringField(body, "market");
    (void)asset_id;
    (void)market;

    b.is_snapshot = 1;  // book event = full snapshot
    b.num_changes = 0;

    // sequence_no gap check (老李 spec §4)
    std::uint64_t seq = 0;
    if (ExtractUint64(body, "sequence_no", seq) && seq > 0) {
        std::string asset_key(asset_id);
        if (!CheckSequenceGap(asset_key, seq)) {
            // Gap detected: trigger resubscribe, accept this snapshot anyway
            TriggerResubscribe(asset_key);
        }
    }

    // Mark snapshot received for this token (P-05 / P-10)
    if (!asset_id.empty()) {
        snapshot_received_.insert(std::string(asset_id));
    }

    ExtractDecimalStringAsBps(body, "last_trade_price", b.last_trade_price_bps);
    ExtractDecimalStringAsBps(body, "tick_size",        b.tick_size_bps);

    return true;
}

bool PolymarketCLOBSubscriber::ParsePriceChange(std::string_view body,
                                                  std::int64_t recv_ts_ns, WssEvent& ev) {
    auto& b = ev.payload.book;
    b.topic = SubTopic::kPriceChange;
    if (!FillBaseTs(body, recv_ts_ns, b.ts)) return false;
    if (b.ts.data_source_ts_ns > recv_ts_ns + kFutureGuardNs) return false;
    if (!b.ts.IsMonotonic()) return false;

    std::string_view asset_id = ExtractStringField(body, "asset_id");

    // P-05: only accept price_change after book snapshot received for this token
    // In v0.1 we accept regardless but track (full guard W10)
    b.is_snapshot = 0;
    b.num_changes = 0;  // changes[] array parsing deferred to W10 simdjson

    // sequence_no gap check
    std::uint64_t seq = 0;
    if (ExtractUint64(body, "sequence_no", seq) && seq > 0) {
        std::string asset_key(asset_id);
        if (!CheckSequenceGap(asset_key, seq)) {
            TriggerResubscribe(asset_key);
        }
    }

    return true;
}

bool PolymarketCLOBSubscriber::ParseLastTrade(std::string_view body, std::int64_t recv_ts_ns,
                                               WssEvent& ev) {
    auto& t = ev.payload.trade;
    if (!FillBaseTs(body, recv_ts_ns, t.ts)) return false;
    if (t.ts.data_source_ts_ns > recv_ts_ns + kFutureGuardNs) return false;
    if (!t.ts.IsMonotonic()) return false;

    // price / size are string decimal on wire (老李 spec §2.2 P-02)
    ExtractDecimalStringAsBps(body, "price", t.price_bps);
    std::int64_t sz = 0;
    if (ExtractInt64OrQuotedInt64(body, "size", sz)) {
        t.size_micro = static_cast<std::uint64_t>(sz);
    }

    return true;
}

bool PolymarketCLOBSubscriber::ParseTickChange(std::string_view body, std::int64_t recv_ts_ns,
                                                WssEvent& ev) {
    auto& ti = ev.payload.tick;
    if (!FillBaseTs(body, recv_ts_ns, ti.ts)) return false;
    if (ti.ts.data_source_ts_ns > recv_ts_ns + kFutureGuardNs) return false;
    if (!ti.ts.IsMonotonic()) return false;

    // tick_size: new value in tick_size_change event
    ExtractDecimalStringAsBps(body, "tick_size", ti.new_tick_bps);

    return true;
}

// ===========================================================================
// User channel — frame dispatch
// ===========================================================================

void PolymarketCLOBSubscriber::OnUserFrame(std::string_view payload,
                                            std::int64_t recv_ts_ns) {
    user_metrics_.frames_received_total.fetch_add(1, std::memory_order_relaxed);
    last_user_msg_ts_ns_.store(recv_ts_ns, std::memory_order_relaxed);
    user_metrics_.last_msg_ts_ns.store(recv_ts_ns, std::memory_order_relaxed);

    if (payload == "PONG" || payload == "pong") return;

    WssEvent ev{};
    bool ok = false;
    std::string_view etype = ExtractEventType(payload);

    if (etype == "trade") {
        // P-07: user channel trade events
        ok = ParseTrade(payload, recv_ts_ns, ev);
    } else if (etype == "order") {
        ok = ParseOrder(payload, recv_ts_ns, ev);
    } else {
        user_metrics_.frames_parse_error_total.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (!ok) {
        user_metrics_.frames_parse_error_total.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    user_metrics_.by_topic_frames[static_cast<std::size_t>(ev.topic)].fetch_add(
        1, std::memory_order_relaxed);
    if (!user_sink_->TryPush(ev)) {
        user_metrics_.frames_dropped_total.fetch_add(1, std::memory_order_relaxed);
    }
}

// ===========================================================================
// User channel — parsers
// ===========================================================================

bool PolymarketCLOBSubscriber::ParseTrade(std::string_view body, std::int64_t recv_ts_ns,
                                           WssEvent& ev) {
    ev.topic = SubTopic::kLastTradePrice;  // reuse trade payload slot
    auto& t = ev.payload.trade;
    if (!FillBaseTs(body, recv_ts_ns, t.ts)) return false;
    if (t.ts.data_source_ts_ns > recv_ts_ns + kFutureGuardNs) return false;
    if (!t.ts.IsMonotonic()) return false;

    ExtractDecimalStringAsBps(body, "price", t.price_bps);
    std::int64_t sz = 0;
    if (ExtractInt64OrQuotedInt64(body, "size", sz)) {
        t.size_micro = static_cast<std::uint64_t>(sz);
    }

    return true;
}

bool PolymarketCLOBSubscriber::ParseOrder(std::string_view body, std::int64_t recv_ts_ns,
                                           WssEvent& ev) {
    ev.topic = SubTopic::kOutcomes;  // reuse outcomes payload slot for order status
    auto& o = ev.payload.outcomes;
    if (!FillBaseTs(body, recv_ts_ns, o.ts)) return false;
    if (o.ts.data_source_ts_ns > recv_ts_ns + kFutureGuardNs) return false;
    if (!o.ts.IsMonotonic()) return false;

    // status → resolution_status mapping
    std::string_view status = ExtractStringField(body, "status");
    if (status == "MATCHED" || status == "CANCELED" || status == "EXPIRED") {
        o.resolution_status = stcpp::polymarket::wss::ResolutionStatus::kResolving;
    } else {
        o.resolution_status = stcpp::polymarket::wss::ResolutionStatus::kOpen;
    }

    return true;
}

// ===========================================================================
// Subscribe frame builders
// ===========================================================================

std::string PolymarketCLOBSubscriber::MakeMarketSubscribeFrame(
    std::span<const std::string> token_ids) const {
    // 老李 spec §2.1: {"type":"Market","assets_ids":["tok1","tok2",...]}
    // P-01: 双 token 必须同时列入 (调用方保证)
    // "type" 字段: 大写 "Market" (P-01 注意: 小写不识别)
    std::string out;
    out.reserve(64 + token_ids.size() * 72);
    out.append(R"({"type":"Market","assets_ids":[)");
    bool first = true;
    for (const auto& tid : token_ids) {
        if (!first) out.push_back(',');
        out.push_back('"');
        out.append(tid);
        out.push_back('"');
        first = false;
    }
    out.append("]}");
    return out;
}

std::string PolymarketCLOBSubscriber::MakeUserSubscribeFrame(
    std::span<const std::string> condition_ids) const {
    // 老李 spec §3.1: {"type":"User","auth":{...},"markets":["cid1","cid2",...]}
    // P-09: auth payload 严禁落日志 (此处仅构造 frame, 不 log)
    // P-07: markets 字段传 condition_id, 不是 token_id
    std::string out;
    out.reserve(256 + condition_ids.size() * 72);
    out.append(R"({"type":"User","auth":{"apiKey":")");
    out.append(cfg_.api_key);
    out.append(R"(","secret":")");
    out.append(cfg_.api_secret);
    out.append(R"(","passphrase":")");
    out.append(cfg_.api_passphrase);
    out.append(R"("},"markets":[)");
    bool first = true;
    for (const auto& cid : condition_ids) {
        if (!first) out.push_back(',');
        out.push_back('"');
        out.append(cid);
        out.push_back('"');
        first = false;
    }
    out.append("]}");
    return out;
}

// ===========================================================================
// Sequence_no gap detection (老李 spec §4)
// ===========================================================================

bool PolymarketCLOBSubscriber::CheckSequenceGap(std::string_view token_id,
                                                 std::uint64_t seq) noexcept {
    if (seq == 0) return true;  // no sequence_no present or 0 = ignore
    auto it = token_seq_map_.find(std::string(token_id));
    if (it == token_seq_map_.end()) {
        // first time seeing this token: establish baseline
        token_seq_map_.emplace(std::string(token_id), seq);
        return true;
    }
    std::uint64_t expected = it->second + 1;
    it->second = seq;
    // gap detected if received seq != expected (and both > 0)
    return (seq == expected);
}

void PolymarketCLOBSubscriber::TriggerResubscribe(std::string_view token_id) {
    // P-06: sequence_no gap → resubscribe to get fresh snapshot
    // In v0.1: clear snapshot flag + re-send subscribe for this token
    snapshot_received_.erase(std::string(token_id));
    if (market_transport_) {
        std::string tid(token_id);
        std::string frame = MakeMarketSubscribeFrame(std::span<const std::string>(&tid, 1));
        market_transport_->AsyncSendText(frame);
    }
}

}  // namespace stcpp::polymarket::clob_wss
