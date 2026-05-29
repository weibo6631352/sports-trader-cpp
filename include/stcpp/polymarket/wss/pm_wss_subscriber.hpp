// stcpp/polymarket/wss/pm_wss_subscriber.hpp — PM sports channel WSS subscriber v0.1
//
// Owner: 小冯 (api-watch-general, #34)  spec: 小余 W5-D-02 + 老周 W5-A-02
// W5 Wave 24 — PM WSS subscriber + 8 sub topic + reconnect + heartbeat + back-pressure
//
// 落: laoli-polymarket-backend-requirements-v1.md §2 第 5 host + §8 reconnect/heartbeat
//     laozhou-architecture-v0.6-e2e.md §17 PMClient WSS 4-5 conn (lib 选型 boost.beast)
//     ADR/2026-05-28-gm-redline-websocket-non-blocking.md (R-12)
//
// 红线:
//   R-12: WSS event loop 严禁同步 REST / 阻塞 IO / 锁 > 100us
//   R-20: 4 ts UPSTREAM_PAYLOAD 优先, ingestion 走本地 now() (不替代 data_source_ts)
//   R-33: 第 5 host wss://sports-api.polymarket.com/ws 公开免鉴权, paper 不走 clob 私有 WSS
//
// 不耻下问:
//   - lib (boost.beast vs websocketpp) → @老周 (v0.6 §17, 我 ack boost.beast)
//   - 4 ts 字段位置 → @老李 (backend req v1 §4.1)
//   - SPSC ring 类型 → @小石 (W5 同 wave, 我先用 ISpscEventSink 抽象)

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "stcpp/polymarket/wss/wss_event.hpp"

namespace stcpp::polymarket::wss {

// 1. WssTransportState — 4 状态 (subscriber 内部 + metric)
enum class WssTransportState : std::uint8_t {
    kDisconnected = 0,
    kConnecting = 1,
    kConnected = 2,
    kReconnecting = 3,
};

[[nodiscard]] constexpr std::string_view StateName(WssTransportState s) noexcept {
    switch (s) {
        case WssTransportState::kDisconnected:
            return "DISCONNECTED";
        case WssTransportState::kConnecting:
            return "CONNECTING";
        case WssTransportState::kConnected:
            return "CONNECTED";
        case WssTransportState::kReconnecting:
            return "RECONNECTING";
    }
    return "";
}

// 2. IWssTransport — pluggable transport (boost.beast 真实现 + mock 单测)
//   生产: BoostBeastTransport 真接 wss://sports-api.polymarket.com/ws (W6 接入)
//   单测: MockWssTransport (本头 + tests/unit/test_pm_wss_subscriber.cpp)
//   callback 由 transport io_context 线程回调, 内严禁 block (R-12)
class IWssTransport {
public:
    using OnTextFrame = std::function<void(std::string_view payload, std::int64_t recv_ts_ns)>;
    using OnConnected = std::function<void()>;
    using OnDisconnected = std::function<void(std::string_view reason)>;

    virtual ~IWssTransport() = default;

    virtual bool AsyncConnect(std::string_view url) = 0;       // 异步连, 不阻塞 (R-12)
    virtual bool AsyncSendText(std::string_view payload) = 0;  // 入 io_context queue, 不阻塞
    virtual void Close() = 0;                                  // 强重连用

    virtual void SetOnTextFrame(OnTextFrame cb) = 0;
    virtual void SetOnConnected(OnConnected cb) = 0;
    virtual void SetOnDisconnected(OnDisconnected cb) = 0;

    [[nodiscard]] virtual bool IsConnected() const noexcept = 0;
};

// 3. ISpscEventSink — back-pressure 抽象 (vCPU0 → vCPU1 SPSC ring)
//   try_push 返 false = ring 满, subscriber drop frame + emit metric (R-12 不阻 event loop)
class ISpscEventSink {
public:
    virtual ~ISpscEventSink() = default;
    [[nodiscard]] virtual bool TryPush(const WssEvent& ev) noexcept = 0;
    [[nodiscard]] virtual std::size_t Capacity() const noexcept = 0;
};

// 4. PMWssSubscriberConfig — 完整配置 (老李 v1 §8)
struct PMWssSubscriberConfig {
    std::string url = "wss://sports-api.polymarket.com/ws";  // R-33 第 5 host

    // reconnect — exp backoff (老李 v1 §8.1)
    std::chrono::milliseconds reconnect_initial = std::chrono::milliseconds(1000);
    std::chrono::milliseconds reconnect_cap = std::chrono::milliseconds(30000);
    double reconnect_multiplier = 2.0;

    // heartbeat (老李 v1 §8.2): 10s PING / 30s 无 pong → 强重连
    std::chrono::milliseconds heartbeat_interval = std::chrono::milliseconds(10000);
    std::chrono::milliseconds heartbeat_timeout = std::chrono::milliseconds(30000);
    std::string heartbeat_ping_text = "PING";

    // back-pressure (老周 v0.6 §4.2: wss_dropped_frames_total drop oldest)
    std::size_t spsc_capacity = 65536;  // 与 MarketDataBus capacity 一致

    // 启动后自动订阅
    std::vector<std::string> initial_condition_ids;
    std::vector<std::string> initial_event_ids;
};

// 5. SubscriberMetrics — 给小郑 prom exporter
struct SubscriberMetrics {
    std::atomic<std::uint64_t> frames_received_total{0};
    std::atomic<std::uint64_t> frames_dropped_total{0};  // SPSC 满
    std::atomic<std::uint64_t> frames_parse_error_total{0};
    std::atomic<std::uint64_t> reconnect_attempts_total{0};
    std::atomic<std::uint64_t> heartbeat_pings_sent_total{0};
    std::atomic<std::uint64_t> heartbeat_timeouts_total{0};
    std::atomic<std::int64_t> last_msg_ts_ns{0};  // RM STALE getter
    std::atomic<std::uint8_t> state{static_cast<std::uint8_t>(WssTransportState::kDisconnected)};

    std::array<std::atomic<std::uint64_t>, kNumSubTopics> by_topic_frames{};

    SubscriberMetrics() noexcept {
        for (auto& c : by_topic_frames)
            c.store(0);
    }
};

// 6. PMWssSubscriber — 主类
//
//   职责:
//     1. 持 IWssTransport, 异步连 wss://sports-api.polymarket.com/ws
//     2. 启动后发 8 sub topic 订阅 JSON (公开免鉴权, 无 HMAC)
//     3. 收 text frame → parse → fill WssEvent (4 ts) → ISpscEventSink::TryPush
//     4. heartbeat: 每 10s 发 PING, 30s 无 pong 强重连
//     5. reconnect: exp backoff 1s/2s/4s/cap 30s
//     6. back-pressure: SPSC 满 → drop + metric (R-12, 不阻 event loop)
//     7. 暴露 last_msg_ts_ns() 给 RM STALE 接口 (老韩 v0.3 §16)
//
//   线程模型 (R-12):
//     - transport callback 线程 = vCPU0 ingest
//     - parser 同步做 (p99 < 5us 目标, 不破 100us 阈)
//     - SPSC TryPush 非阻塞 = 不 block vCPU0
//     - 心跳 / reconnect 走 transport io_context 异步, 不开自己线程
class PMWssSubscriber {
public:
    PMWssSubscriber(std::unique_ptr<IWssTransport> transport, std::shared_ptr<ISpscEventSink> sink,
                    PMWssSubscriberConfig cfg);

    PMWssSubscriber(const PMWssSubscriber&) = delete;
    PMWssSubscriber& operator=(const PMWssSubscriber&) = delete;
    PMWssSubscriber(PMWssSubscriber&&) = delete;
    PMWssSubscriber& operator=(PMWssSubscriber&&) = delete;

    ~PMWssSubscriber();

    bool Start();                                                // 异步连接 + 自动订阅
    void Stop() noexcept;                                        // joinable cleanup
    bool Subscribe(SubTopic topic, std::string_view target_id);  // 增量订阅

    void TickHeartbeatNow();          // 单测 / 真 timer 调
    void CheckHeartbeatTimeoutNow();  // 单测 / 真 timer 调

    [[nodiscard]] const SubscriberMetrics& metrics() const noexcept { return metrics_; }
    [[nodiscard]] WssTransportState state() const noexcept { return state_.load(); }
    [[nodiscard]] std::int64_t last_msg_ts_ns() const noexcept {
        return last_msg_ts_ns_.load(std::memory_order_relaxed);
    }

    using NowFn = std::function<std::int64_t()>;
    void SetNowFnForTest(NowFn fn) { now_fn_ = std::move(fn); }

private:
    void OnTextFrame(std::string_view payload, std::int64_t recv_ts_ns);
    void OnTransportConnected();
    void OnTransportDisconnected(std::string_view reason);
    bool ParseAndDispatch(std::string_view payload, std::int64_t recv_ts_ns);

    // 8 sub topic parsers
    bool ParseMarket(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseGame(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseOutcomes(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseBook(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParsePriceChange(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseLastTrade(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseTickSize(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseSystemStatus(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev);

    [[nodiscard]] std::string MakeSubscribeFrame(SubTopic topic, std::string_view target_id) const;
    void ScheduleReconnect();
    [[nodiscard]] std::int64_t NowNs() const noexcept { return now_fn_ ? now_fn_() : 0; }

    std::unique_ptr<IWssTransport> transport_;
    std::shared_ptr<ISpscEventSink> sink_;
    PMWssSubscriberConfig cfg_;
    SubscriberMetrics metrics_;

    std::atomic<WssTransportState> state_{WssTransportState::kDisconnected};
    std::atomic<std::int64_t> last_msg_ts_ns_{0};
    std::atomic<std::int64_t> last_ping_sent_ts_ns_{0};
    std::atomic<std::uint32_t> reconnect_attempt_{0};

    NowFn now_fn_;  // 测试可注入 (default = system_clock)
};

}  // namespace stcpp::polymarket::wss
