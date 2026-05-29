// include/stcpp/polymarket/clob_wss/polymarket_clob_subscriber.hpp
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
//   R-12: transport callback 同步路径 ≤ 100us，无阻塞 IO / 锁
//   R-20: data_source_ts = frame.timestamp (ms) × 1e6 ns (UPSTREAM_PAYLOAD)
//         禁止用本地 now() 替代上游 timestamp; frame 缺 timestamp → drop
//   R-33: endpoint: wss://ws-subscriptions-clob.polymarket.com/ws/market
//   P-09: user channel auth (apiKey/secret/passphrase) 严禁落日志
//
// 与 PMWssSubscriber 区分:
//   PMWssSubscriber     → wss://sports-api.polymarket.com/ws (第 5 host, sports inplay)
//   PolymarketCLOBSubscriber (本类) → ws-subscriptions-clob.polymarket.com/ws/{market,user}
//
// 不耻下问:
//   parser 升 simdjson → @老李 W10
//   boost.beast 真 transport → @老周 W10+
//   hot/cold token 分类 worker → @小余 D主管 W9 W4

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "stcpp/polymarket/wss/pm_wss_subscriber.hpp"
#include "stcpp/polymarket/wss/wss_event.hpp"

namespace stcpp::polymarket::clob_wss {

using stcpp::polymarket::wss::ISpscEventSink;
using stcpp::polymarket::wss::IWssTransport;
using stcpp::polymarket::wss::SubscriberMetrics;
using stcpp::polymarket::wss::WssEvent;
using stcpp::polymarket::wss::WssTransportState;

// PolymarketCLOBSubscriberConfig -----------------------------------------------
//
// 老周 v0.4 §17 拓扑对应:
//   market_url → T0a poly_market_hot_reactor (300-500 token)
//              + T0b poly_market_cold_reactor (2000-3500 token)
//   user_url   → T0c poly_user_reactor (N condition_id)
//
// v0.1 实施: 单 market conn (hot path). cold conn + user conn 在 v0.2+ 扩展.
struct PolymarketCLOBSubscriberConfig {
    // market channel (老李 spec §2 — token_id 粒度, 无鉴权)
    std::string market_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
    // user channel (老李 spec §3 — condition_id 粒度, payload 内鉴权)
    std::string user_url   = "wss://ws-subscriptions-clob.polymarket.com/ws/user";

    // reconnect exp backoff (老李 spec §5.1)
    std::chrono::milliseconds reconnect_initial    {1000};
    std::chrono::milliseconds reconnect_cap        {30000};
    double                    reconnect_multiplier {2.0};

    // heartbeat (老李 spec §5.2): 10s PING / 30s 无帧 → 强重连
    std::chrono::milliseconds heartbeat_interval   {10000};
    std::chrono::milliseconds heartbeat_timeout    {30000};
    std::string               heartbeat_ping_text  = "PING";

    // user channel 凭证 (从 .env 读, 严禁落日志 P-09)
    std::string api_key;
    std::string api_secret;
    std::string api_passphrase;

    // 启动后自动订阅 (老李 spec §2.1 双 token 规则)
    std::vector<std::string> initial_market_token_ids;    // token_id, market channel
    std::vector<std::string> initial_user_condition_ids;  // condition_id, user channel
};

// PolymarketCLOBSubscriber -----------------------------------------------------
//
// 职责:
//   1. market channel: subscribe assets_ids (token_id 粒度, 双 token 同订)
//      解析 book / price_change / last_trade_price / tick_size_change
//      push WssEvent 到 market SPSC sink
//   2. user channel: subscribe markets (condition_id 粒度) + auth payload (payload 内鉴权)
//      解析 trade / order 事件, push 到 user SPSC sink
//   3. sequence_no gap 检测 → trigger resubscribe (老李 spec §4)
//   4. reconnect exp backoff (§5.1), heartbeat (§5.2)
//   5. R-20 4-ts: data_source_ts = frame.timestamp(ms) × 1e6 (禁 now() 替代)
//   6. 暴露 last_market_msg_ts_ns() / last_user_msg_ts_ns() 给 RM STALE 检测
//
// 线程模型 (R-12):
//   transport callback 线程 (vCPU0) 同步路径严禁 block > 100us
//   SPSC TryPush 非阻塞; 满则 drop + metric
//   所有 market 状态 (token_seq_map_, hot/cold set) 仅 vCPU0 访问, 无锁
class PolymarketCLOBSubscriber {
public:
    PolymarketCLOBSubscriber(
        std::unique_ptr<IWssTransport>  market_transport,
        std::unique_ptr<IWssTransport>  user_transport,
        std::shared_ptr<ISpscEventSink> market_sink,
        std::shared_ptr<ISpscEventSink> user_sink,
        PolymarketCLOBSubscriberConfig  cfg);

    PolymarketCLOBSubscriber(const PolymarketCLOBSubscriber&) = delete;
    PolymarketCLOBSubscriber& operator=(const PolymarketCLOBSubscriber&) = delete;
    PolymarketCLOBSubscriber(PolymarketCLOBSubscriber&&) = delete;
    PolymarketCLOBSubscriber& operator=(PolymarketCLOBSubscriber&&) = delete;

    ~PolymarketCLOBSubscriber();

    // 启动: market + user transport 各自 AsyncConnect
    bool Start();
    void Stop() noexcept;

    // 动态追加订阅 (market channel: 无需重连, 发追加 payload 即可)
    // 老李 spec §7: 追加后立即收到新 token 的 book snapshot
    bool SubscribeMarketTokens(std::span<const std::string> token_ids);

    // user channel 动态追加 condition_id (需重连重订)
    bool SubscribeUserMarkets(std::span<const std::string> condition_ids);

    // 心跳 / 超时检查 (供外部 timer 驱动, 或单测直接调)
    void TickMarketHeartbeatNow();
    void TickUserHeartbeatNow();
    void CheckMarketHeartbeatTimeoutNow();
    void CheckUserHeartbeatTimeoutNow();

    [[nodiscard]] std::int64_t last_market_msg_ts_ns() const noexcept {
        return last_market_msg_ts_ns_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::int64_t last_user_msg_ts_ns() const noexcept {
        return last_user_msg_ts_ns_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] const SubscriberMetrics& market_metrics() const noexcept { return market_metrics_; }
    [[nodiscard]] const SubscriberMetrics& user_metrics()   const noexcept { return user_metrics_; }
    [[nodiscard]] WssTransportState market_state() const noexcept { return market_state_.load(); }
    [[nodiscard]] WssTransportState user_state()   const noexcept { return user_state_.load(); }

    // 测试可注入时钟 (默认 system_clock UTC epoch_ns)
    using NowFn = std::function<std::int64_t()>;
    void SetNowFnForTest(NowFn fn) { now_fn_ = std::move(fn); }

private:
    // --- market channel -------------------------------------------------------
    void OnMarketConnected();
    void OnMarketDisconnected(std::string_view reason);
    void OnMarketFrame(std::string_view payload, std::int64_t recv_ts_ns);
    bool ParseBook(std::string_view body,        std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParsePriceChange(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseLastTrade(std::string_view body,   std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseTickChange(std::string_view body,  std::int64_t recv_ts_ns, WssEvent& ev);
    void ScheduleMarketReconnect();

    // --- user channel ---------------------------------------------------------
    void OnUserConnected();
    void OnUserDisconnected(std::string_view reason);
    void OnUserFrame(std::string_view payload,   std::int64_t recv_ts_ns);
    bool ParseTrade(std::string_view body,       std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseOrder(std::string_view body,       std::int64_t recv_ts_ns, WssEvent& ev);
    void ScheduleUserReconnect();

    // --- subscribe frame builders (老李 spec §2.1 / §3.1) --------------------
    // P-01: assets_ids 必须包含双 token (type "Market" 大写 M)
    // P-07: user channel markets 用 condition_id, 不是 token_id
    [[nodiscard]] std::string MakeMarketSubscribeFrame(
        std::span<const std::string> token_ids) const;
    [[nodiscard]] std::string MakeUserSubscribeFrame(
        std::span<const std::string> condition_ids) const;

    // --- sequence_no gap detection (老李 spec §4) ----------------------------
    // vCPU0 单线程访问 token_seq_map_, 无锁
    bool CheckSequenceGap(std::string_view token_id, std::uint64_t seq) noexcept;
    void TriggerResubscribe(std::string_view token_id);

    [[nodiscard]] std::int64_t NowNs() const noexcept {
        return now_fn_ ? now_fn_() : 0;
    }

    // --- data members ---------------------------------------------------------
    std::unique_ptr<IWssTransport>  market_transport_;
    std::unique_ptr<IWssTransport>  user_transport_;
    std::shared_ptr<ISpscEventSink> market_sink_;
    std::shared_ptr<ISpscEventSink> user_sink_;
    PolymarketCLOBSubscriberConfig  cfg_;

    SubscriberMetrics               market_metrics_;
    SubscriberMetrics               user_metrics_;

    std::atomic<WssTransportState>  market_state_{WssTransportState::kDisconnected};
    std::atomic<WssTransportState>  user_state_{WssTransportState::kDisconnected};

    std::atomic<std::int64_t>       last_market_msg_ts_ns_{0};
    std::atomic<std::int64_t>       last_user_msg_ts_ns_{0};
    std::atomic<std::int64_t>       last_market_ping_ts_ns_{0};
    std::atomic<std::int64_t>       last_user_ping_ts_ns_{0};

    std::atomic<std::uint32_t>      market_reconnect_attempt_{0};
    std::atomic<std::uint32_t>      user_reconnect_attempt_{0};

    // vCPU0 single-thread: no lock needed
    // token_id (string) → last seen sequence_no
    std::unordered_map<std::string, std::uint64_t> token_seq_map_;
    // snapshot_received flag per token (old spec §2.1 P-05 / P-10)
    std::unordered_set<std::string> snapshot_received_;
    std::unordered_set<std::string> hot_token_ids_;
    std::unordered_set<std::string> cold_token_ids_;
    // current user subscription condition_ids (for reconnect replay)
    std::vector<std::string>        user_condition_ids_;

    NowFn now_fn_;
};

}  // namespace stcpp::polymarket::clob_wss
