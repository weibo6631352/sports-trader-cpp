// stcpp/polymarket/wss/pm_wss_subscriber.cpp — PM WSS subscriber v0.1 实现
//
// Owner: 小冯 (#34)  spec: 小余 W5-D-02 + 老周 W5-A-02. W5 Wave 24.
//
// 红线 enforce:
//   R-12: callback 同步路径 ≤ 100us (字段抽取 + SPSC TryPush, 无阻塞 IO)
//   R-20: payload.timestamp ms × 1e6 → ns 作 data_source_ts (UPSTREAM_PAYLOAD)
//         ingestion_ts = NowNs() at recv. event_ts 优先 game.last_update,
//         无 event_ts 时 fallback = data_source_ts (标 ds_origin = kInferredFromDsTs)
//   R-33: 默认 URL = 第 5 host wss://sports-api.polymarket.com/ws
//
// 不耻下问:
//   - parser 升级 simdjson → @老李 (W6 之后, v0.1 minimal 字段抽取够 paper)
//   - boost.beast transport 实现 → @老周 (本 .cpp transport-agnostic, beast 在另一 TU)

#include "stcpp/polymarket/wss/pm_wss_subscriber.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <string>
#include <utility>

namespace stcpp::polymarket::wss {

namespace {

// minimal JSON 字段抽取 — v0.1 paper. W6 切 simdjson.
// 字段名不嵌字面值里冲突, 直接 find("key") 子串.
constexpr std::int64_t kMsToNs = 1'000'000LL;

std::size_t FindKey(std::string_view body, std::string_view key) noexcept {
    std::string needle;
    needle.reserve(key.size() + 2);
    needle.push_back('"');
    needle.append(key.data(), key.size());
    needle.push_back('"');
    return body.find(needle);
}

std::size_t SkipColonSpace(std::string_view body, std::size_t pos) noexcept {
    while (pos < body.size() && (body[pos] == ':' || body[pos] == ' ' || body[pos] == '\t'))
        ++pos;
    return pos;
}

bool ExtractInt64(std::string_view body, std::string_view key, std::int64_t& out) noexcept {
    std::size_t pk = FindKey(body, key);
    if (pk == std::string_view::npos)
        return false;
    std::size_t pos = SkipColonSpace(body, pk + key.size() + 2);
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

bool ExtractDoubleAsBps(std::string_view body, std::string_view key, std::uint32_t& out_bps) noexcept {
    std::size_t pk = FindKey(body, key);
    if (pk == std::string_view::npos)
        return false;
    std::size_t pos = SkipColonSpace(body, pk + key.size() + 2);
    char buf[32];
    std::size_t n = 0;
    while (pos < body.size() && n + 1 < sizeof(buf) &&
           (std::isdigit(static_cast<unsigned char>(body[pos])) || body[pos] == '.' || body[pos] == '-' ||
            body[pos] == '+' || body[pos] == 'e' || body[pos] == 'E')) {
        buf[n++] = body[pos++];
    }
    if (n == 0)
        return false;
    buf[n] = '\0';
    char* eptr = nullptr;
    double v = std::strtod(buf, &eptr);
    if (eptr == buf)
        return false;
    if (v < 0)
        v = 0;
    if (v > 1.0)
        v = 1.0;
    out_bps = static_cast<std::uint32_t>(v * 10000.0 + 0.5);
    return true;
}

// hex id "0x..." → u64 (取末 16 hex char)
bool ExtractHexId(std::string_view body, std::string_view key, std::uint64_t& out) noexcept {
    std::size_t pk = FindKey(body, key);
    if (pk == std::string_view::npos)
        return false;
    std::size_t pos = SkipColonSpace(body, pk + key.size() + 2);
    if (pos >= body.size() || body[pos] != '"')
        return false;
    ++pos;
    std::size_t e = body.find('"', pos);
    if (e == std::string_view::npos)
        return false;
    std::string_view sv = body.substr(pos, e - pos);
    if (sv.size() >= 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X'))
        sv.remove_prefix(2);
    std::size_t take = std::min<std::size_t>(sv.size(), 16);
    std::string_view tail = sv.substr(sv.size() - take);
    std::uint64_t v = 0;
    for (char c : tail) {
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= static_cast<std::uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= static_cast<std::uint64_t>(c - 'A' + 10);
        else
            return false;
    }
    out = v;
    return true;
}

GameState ParseGameStateStr(std::string_view body) noexcept {
    if (body.find("\"game_state\":\"live\"") != std::string_view::npos)
        return GameState::kLive;
    if (body.find("\"game_state\":\"pregame\"") != std::string_view::npos)
        return GameState::kPregame;
    if (body.find("\"game_state\":\"halftime\"") != std::string_view::npos)
        return GameState::kHalftime;
    if (body.find("\"game_state\":\"ended\"") != std::string_view::npos)
        return GameState::kEnded;
    if (body.find("\"game_state\":\"cancelled\"") != std::string_view::npos)
        return GameState::kCancelled;
    if (body.find("\"game_state\":\"postponed\"") != std::string_view::npos)
        return GameState::kPostponed;
    return GameState::kScheduled;
}

}  // namespace

// ===========================================================================
// PMWssSubscriber 实现
// ===========================================================================

PMWssSubscriber::PMWssSubscriber(std::unique_ptr<IWssTransport> transport,
                                 std::shared_ptr<ISpscEventSink> sink, PMWssSubscriberConfig cfg)
    : transport_(std::move(transport)), sink_(std::move(sink)), cfg_(std::move(cfg)), now_fn_([]() {
          return std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::system_clock::now().time_since_epoch())  // UTC epoch_ns
              .count();
      }) {
    transport_->SetOnTextFrame([this](std::string_view p, std::int64_t ts) { OnTextFrame(p, ts); });
    transport_->SetOnConnected([this]() { OnTransportConnected(); });
    transport_->SetOnDisconnected([this](std::string_view r) { OnTransportDisconnected(r); });
}

PMWssSubscriber::~PMWssSubscriber() {
    Stop();
}

bool PMWssSubscriber::Start() {
    state_.store(WssTransportState::kConnecting);
    metrics_.state.store(static_cast<std::uint8_t>(WssTransportState::kConnecting));
    return transport_->AsyncConnect(cfg_.url);
}

void PMWssSubscriber::Stop() noexcept {
    if (transport_)
        transport_->Close();
    state_.store(WssTransportState::kDisconnected);
    metrics_.state.store(static_cast<std::uint8_t>(WssTransportState::kDisconnected));
}

bool PMWssSubscriber::Subscribe(SubTopic topic, std::string_view target_id) {
    return transport_->AsyncSendText(MakeSubscribeFrame(topic, target_id));
}

std::string PMWssSubscriber::MakeSubscribeFrame(SubTopic topic, std::string_view target_id) const {
    std::string out;
    out.reserve(64 + target_id.size());
    out.append(R"({"action":"subscribe","topic":")");
    out.append(SubTopicName(topic));
    out.append(R"(","id":")");
    out.append(target_id.data(), target_id.size());
    out.append("\"}");
    return out;
}

void PMWssSubscriber::OnTransportConnected() {
    state_.store(WssTransportState::kConnected);
    metrics_.state.store(static_cast<std::uint8_t>(WssTransportState::kConnected));
    reconnect_attempt_.store(0);
    // 重连后重发初始订阅
    for (const auto& cid : cfg_.initial_condition_ids) {
        transport_->AsyncSendText(MakeSubscribeFrame(SubTopic::kMarket, cid));
        transport_->AsyncSendText(MakeSubscribeFrame(SubTopic::kOutcomes, cid));
    }
    for (const auto& eid : cfg_.initial_event_ids) {
        transport_->AsyncSendText(MakeSubscribeFrame(SubTopic::kGame, eid));
    }
}

void PMWssSubscriber::OnTransportDisconnected(std::string_view /*reason*/) {
    state_.store(WssTransportState::kReconnecting);
    metrics_.state.store(static_cast<std::uint8_t>(WssTransportState::kReconnecting));
    ScheduleReconnect();
}

void PMWssSubscriber::ScheduleReconnect() {
    // exp backoff cap 30s — 真 timer 排队在 BoostBeastTransport 内做 (io_context.steady_timer).
    // v0.1 仅累加 attempt counter + 立即 AsyncConnect (transport 内部决定何时真发起).
    reconnect_attempt_.fetch_add(1, std::memory_order_relaxed);
    metrics_.reconnect_attempts_total.fetch_add(1, std::memory_order_relaxed);
    transport_->AsyncConnect(cfg_.url);
}

void PMWssSubscriber::OnTextFrame(std::string_view payload, std::int64_t recv_ts_ns) {
    metrics_.frames_received_total.fetch_add(1, std::memory_order_relaxed);
    last_msg_ts_ns_.store(recv_ts_ns, std::memory_order_relaxed);
    metrics_.last_msg_ts_ns.store(recv_ts_ns, std::memory_order_relaxed);
    // 应用层 pong 不入 audit (老李 §8.2 heartbeat 仅 metric)
    if (payload == "PONG" || payload == "pong")
        return;
    ParseAndDispatch(payload, recv_ts_ns);
}

bool PMWssSubscriber::ParseAndDispatch(std::string_view payload, std::int64_t recv_ts_ns) {
    // 路由: 顶层 topic 字符串关键字
    WssEvent ev;
    bool ok = false;
    if (payload.find("\"topic\":\"market\"") != std::string_view::npos ||
        (payload.find("\"market\"") != std::string_view::npos &&
         payload.find("\"yes_bids\"") != std::string_view::npos)) {
        ev.topic = SubTopic::kMarket;
        ok = ParseMarket(payload, recv_ts_ns, ev);
    } else if (payload.find("\"topic\":\"game\"") != std::string_view::npos ||
               payload.find("\"game_state\"") != std::string_view::npos) {
        ev.topic = SubTopic::kGame;
        ok = ParseGame(payload, recv_ts_ns, ev);
    } else if (payload.find("\"topic\":\"outcomes\"") != std::string_view::npos ||
               payload.find("\"resolution_status\"") != std::string_view::npos) {
        ev.topic = SubTopic::kOutcomes;
        ok = ParseOutcomes(payload, recv_ts_ns, ev);
    } else if (payload.find("\"topic\":\"book\"") != std::string_view::npos) {
        ev.topic = SubTopic::kBook;
        ok = ParseBook(payload, recv_ts_ns, ev);
    } else if (payload.find("\"topic\":\"price_change\"") != std::string_view::npos ||
               payload.find("\"changes\"") != std::string_view::npos) {
        ev.topic = SubTopic::kPriceChange;
        ok = ParsePriceChange(payload, recv_ts_ns, ev);
    } else if (payload.find("\"topic\":\"last_trade_price\"") != std::string_view::npos) {
        ev.topic = SubTopic::kLastTradePrice;
        ok = ParseLastTrade(payload, recv_ts_ns, ev);
    } else if (payload.find("\"topic\":\"tick_size_change\"") != std::string_view::npos ||
               (payload.find("\"old_tick\"") != std::string_view::npos &&
                payload.find("\"new_tick\"") != std::string_view::npos)) {
        ev.topic = SubTopic::kTickSizeChange;
        ok = ParseTickSize(payload, recv_ts_ns, ev);
    } else if (payload.find("\"topic\":\"system\"") != std::string_view::npos ||
               payload.find("\"status\":\"healthy\"") != std::string_view::npos ||
               payload.find("\"status\":\"degraded\"") != std::string_view::npos ||
               payload.find("\"status\":\"maintenance\"") != std::string_view::npos) {
        ev.topic = SubTopic::kSystemStatus;
        ok = ParseSystemStatus(payload, recv_ts_ns, ev);
    }

    if (!ok) {
        metrics_.frames_parse_error_total.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    metrics_.by_topic_frames[static_cast<std::size_t>(ev.topic)].fetch_add(1, std::memory_order_relaxed);
    if (!sink_->TryPush(ev)) {
        // back-pressure: SPSC 满 → drop + metric (R-12 不阻 event loop)
        metrics_.frames_dropped_total.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

// 4 ts 共享填法: payload.timestamp → data_source_ts (UPSTREAM_PAYLOAD), recv_ts → ingest/as_of.
// event_ts 默认 = data_source_ts; game.last_update 存在时覆盖.
namespace {
bool FillBaseTs(std::string_view body, std::int64_t recv_ts_ns, FourTs& ts,
                DataSourceTsOrigin origin = DataSourceTsOrigin::kUpstreamPayload) {
    std::int64_t ts_ms = 0;
    if (!ExtractInt64(body, "timestamp", ts_ms))
        return false;
    ts.data_source_ts_ns = ts_ms * kMsToNs;
    ts.event_ts_ns = ts.data_source_ts_ns;  // 多数 topic 无独立 event_ts
    ts.ingestion_ts_ns = recv_ts_ns;
    ts.as_of_ts_ns = recv_ts_ns;
    ts.ds_origin = origin;
    return true;
}
}  // namespace

bool PMWssSubscriber::ParseMarket(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev) {
    auto& u = ev.payload.book;
    u.topic = SubTopic::kMarket;
    if (!FillBaseTs(body, recv_ts_ns, u.ts))
        return false;
    ExtractHexId(body, "condition_id", u.market_id);
    ExtractDoubleAsBps(body, "mid", u.mid_bps);
    ExtractDoubleAsBps(body, "spread", u.spread_bps);
    ExtractDoubleAsBps(body, "last_trade_price", u.last_trade_price_bps);
    ExtractDoubleAsBps(body, "tick_size", u.tick_size_bps);
    std::int64_t nr = 0;
    if (ExtractInt64(body, "neg_risk", nr))
        u.neg_risk = static_cast<std::uint8_t>(nr ? 1 : 0);
    u.is_snapshot = 1;
    return u.ts.IsMonotonic();
}

bool PMWssSubscriber::ParseGame(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev) {
    auto& g = ev.payload.game;
    if (!FillBaseTs(body, recv_ts_ns, g.ts))
        return false;
    std::int64_t last_upd_ms = 0;
    if (ExtractInt64(body, "last_update", last_upd_ms)) {
        g.ts.event_ts_ns = last_upd_ms * kMsToNs;  // R-20 UPSTREAM_PAYLOAD: 比赛事件 ts
    } else {
        g.ts.ds_origin = DataSourceTsOrigin::kInferredFromDsTs;
    }
    ExtractHexId(body, "event_id", g.event_id);
    std::int64_t period = 0, tr = 0, sh = 0, sa = 0, sport = 0;
    if (ExtractInt64(body, "period", period))
        g.period = static_cast<std::uint16_t>(period);
    if (ExtractInt64(body, "time_remaining_s", tr))
        g.time_remaining_s = static_cast<std::int32_t>(tr);
    if (ExtractInt64(body, "score_home", sh))
        g.score_home = static_cast<std::int32_t>(sh);
    if (ExtractInt64(body, "score_away", sa))
        g.score_away = static_cast<std::int32_t>(sa);
    if (ExtractInt64(body, "sport", sport))
        g.sport = static_cast<std::uint16_t>(sport);
    g.game_state = ParseGameStateStr(body);
    return g.ts.IsMonotonic();
}

bool PMWssSubscriber::ParseOutcomes(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev) {
    auto& o = ev.payload.outcomes;
    if (!FillBaseTs(body, recv_ts_ns, o.ts, DataSourceTsOrigin::kInferredFromDsTs))
        return false;
    ExtractHexId(body, "condition_id", o.market_id);
    if (body.find("\"resolution_status\":\"resolved\"") != std::string_view::npos)
        o.resolution_status = ResolutionStatus::kResolved;
    else if (body.find("\"resolution_status\":\"resolving\"") != std::string_view::npos)
        o.resolution_status = ResolutionStatus::kResolving;
    else
        o.resolution_status = ResolutionStatus::kOpen;
    return o.ts.IsMonotonic();
}

bool PMWssSubscriber::ParseBook(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev) {
    auto& b = ev.payload.book;
    b.topic = SubTopic::kBook;
    if (!FillBaseTs(body, recv_ts_ns, b.ts, DataSourceTsOrigin::kInferredFromDsTs))
        return false;
    ExtractHexId(body, "market", b.market_id);
    ExtractHexId(body, "asset_id", b.token_id_yes);
    b.is_snapshot = 1;
    return b.ts.IsMonotonic();
}

bool PMWssSubscriber::ParsePriceChange(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev) {
    auto& b = ev.payload.book;
    b.topic = SubTopic::kPriceChange;
    if (!FillBaseTs(body, recv_ts_ns, b.ts))
        return false;
    ExtractHexId(body, "asset_id", b.token_id_yes);
    ExtractHexId(body, "market", b.market_id);
    b.is_snapshot = 0;
    b.num_changes = 0;  // v0.1: changes[] array parse 待 W6 simdjson
    return b.ts.IsMonotonic();
}

bool PMWssSubscriber::ParseLastTrade(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev) {
    auto& t = ev.payload.trade;
    if (!FillBaseTs(body, recv_ts_ns, t.ts))
        return false;
    ExtractHexId(body, "asset_id", t.token_id);
    ExtractHexId(body, "market", t.market_id);
    ExtractDoubleAsBps(body, "price", t.price_bps);
    std::int64_t sz = 0;
    if (ExtractInt64(body, "size", sz))
        t.size_micro = static_cast<std::uint64_t>(sz);
    return t.ts.IsMonotonic();
}

bool PMWssSubscriber::ParseTickSize(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev) {
    auto& ti = ev.payload.tick;
    if (!FillBaseTs(body, recv_ts_ns, ti.ts))
        return false;
    ExtractHexId(body, "asset_id", ti.token_id);
    ExtractHexId(body, "market", ti.market_id);
    ExtractDoubleAsBps(body, "old_tick", ti.old_tick_bps);
    ExtractDoubleAsBps(body, "new_tick", ti.new_tick_bps);
    return ti.ts.IsMonotonic();
}

bool PMWssSubscriber::ParseSystemStatus(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev) {
    auto& s = ev.payload.system;
    if (!FillBaseTs(body, recv_ts_ns, s.ts))
        return false;
    if (body.find("\"healthy\"") != std::string_view::npos)
        s.health = SystemHealth::kHealthy;
    else if (body.find("\"degraded\"") != std::string_view::npos)
        s.health = SystemHealth::kDegraded;
    else if (body.find("\"maintenance\"") != std::string_view::npos)
        s.health = SystemHealth::kMaintenance;
    return s.ts.IsMonotonic();
}

void PMWssSubscriber::TickHeartbeatNow() {
    transport_->AsyncSendText(cfg_.heartbeat_ping_text);
    last_ping_sent_ts_ns_.store(NowNs(), std::memory_order_relaxed);
    metrics_.heartbeat_pings_sent_total.fetch_add(1, std::memory_order_relaxed);
}

void PMWssSubscriber::CheckHeartbeatTimeoutNow() {
    const std::int64_t now = NowNs();
    const std::int64_t last = last_msg_ts_ns_.load(std::memory_order_relaxed);
    const std::int64_t to_ns = cfg_.heartbeat_timeout.count() * kMsToNs;
    if (last != 0 && now - last > to_ns) {
        metrics_.heartbeat_timeouts_total.fetch_add(1, std::memory_order_relaxed);
        transport_->Close();  // 强重连: 关 socket → OnTransportDisconnected → ScheduleReconnect
    }
}

}  // namespace stcpp::polymarket::wss
