// include/stcpp/polymarket/clob_wss/orderbook_adapter.hpp
//
// v0.1  — 单档合成 (L1 snapshot 合成); 完整 L2 多档待 W10 simdjson (@老李)
//
// Owner: 小冯 (#34)  spec: 老李 (#07)  feature-store contract: 小田 (#24)
// last_review: 2026-05-29
//
// GAP 表:
// +------------------+---------+--------+-----------+
// | gap              | ack 方  | 截止   | 状态      |
// +------------------+---------+--------+-----------+
// | 完整 L2 多档解析 | @老李   | W10    | 待 simdjson|
// | market_metadata  | @小田   | W10    | 待注入接口 |
// +------------------+---------+--------+-----------+
//
// 职责:
//   消费 WssEvent (kBook / kPriceChange) → 维护每 token_id 单档订单簿状态机
//   → 输出 OrderBookSnapshot + FeatureStoreBookRow
//
// 红线:
//   R-12  热路径无锁无 malloc (vCPU0 内调用, 禁止分配, 禁止 > 100us)
//   R-20  4 ts: event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts
//         data_source_ts 来自 WSS payload @timestamp (ms × 1e6), 禁本地 now()
//
// 命名映射文档 (OrderBookFeatures → FeatureStoreBookRow):
//   adapter 输出字段                → FeatureStoreBookRow 字段
//   ─────────────────────────────────────────────────────────────
//   ts.event_ts_ns                  → event_ts_ns
//   ts.data_source_ts_ns            → data_source_ts_ns      (WSS @ts × 1e6)
//   ts.ingestion_ts_ns              → ingestion_ts_ns
//   (strategy 层填写)               → as_of_ts_ns            (evaluate 时刻)
//   snap.bid[0].price               → bid_price[0]
//   snap.bid[0].size_usdc           → bid_size_usdc[0]
//   snap.ask[0].price               → ask_price[0]
//   snap.ask[0].size_usdc           → ask_size_usdc[0]
//   (bid[1..4] / ask[1..4])         → bid_price[1..4] / ask_price[1..4] (v0.1 = NaN)
//   probe.mid                       → mid
//   (ask[0]-bid[0])/mid * 10000     → spread_bps_f
//   depth_within_ticks(±TICK_WINDOW)→ top3_depth_usdc
//   snap.tick_size                  → tick_size
//   probe.microprice                → microprice
//   probe.imbalance                 → imbalance
//   snap.last_trade_ts_ns           → last_trade_ts_ns
//   (外部注入)                      → market_id / token_side  (消费方 consumer 注入)
//   (外部注入)                      → sport / event_date / market_type (小田 consumer 注入)
//
// 不耻下问:
//   完整 L2 多档 parse           → @老李 W10
//   market_metadata 注入接口     → @小田 (#24)
//   Parquet 写入                 → @小余 (#D主管)
//
// ============================================================================

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

#include "stcpp/data/feature_store_contract.hpp"       // FeatureStoreBookRow, kOrderBookLevels
#include "stcpp/microstructure/orderbook.hpp"           // OrderBookSnapshot, kBookDepthLevels
#include "stcpp/polymarket/wss/wss_event.hpp"           // WssEvent, SubTopic, FourTs

namespace stcpp::polymarket::clob_wss {

using stcpp::microstructure::kBookDepthLevels;
using stcpp::microstructure::OrderBookLevel;
using stcpp::microstructure::OrderBookSnapshot;
using stcpp::microstructure::OrderBookTs;
using stcpp::microstructure::compute_l1_probe;
using stcpp::microstructure::depth_within_ticks;
using stcpp::microstructure::TICK_01;
using stcpp::microstructure::TICK_WINDOW;
using stcpp::data::feature_store::FeatureStoreBookRow;
using stcpp::data::feature_store::kOrderBookLevels;
using stcpp::polymarket::wss::WssEvent;
using stcpp::polymarket::wss::SubTopic;
using stcpp::polymarket::wss::FourTs;

// ---------------------------------------------------------------------------
// AdapterState — per-token 状态机
// ---------------------------------------------------------------------------
enum class AdapterSnapshotState : std::uint8_t {
    kWaitingSnapshot = 0,  // 尚未收到初始 book snapshot
    kLive            = 1,  // 已有 snapshot, 接受 price_change delta
};

// ---------------------------------------------------------------------------
// OrderBookAdapterConfig — 构造时注入
// ---------------------------------------------------------------------------
struct OrderBookAdapterConfig {
    // 每 token 最大保留的内存状态 (超出 evict LRU: v0.1 简化为 hard cap)
    std::size_t max_tokens = 1024;
    // Tick size fallback (实际值来自 WSS tick_size_change event)
    double default_tick_size = TICK_01;
};

// ---------------------------------------------------------------------------
// OrderBookAdapter — v0.1
// ---------------------------------------------------------------------------
// 热路径设计 (R-12):
//   - per-token state map: std::unordered_map (构造期分配, 热路径不 grow)
//   - 单档合成: bid[0] / ask[0] 来自 WssEvent price_bps; bid[1..4] = NaN (v0.1)
//   - 无锁 (vCPU0 单线程调用 OnEvent)
//   - 无 malloc on hot path (snapshot string_view 指向 caller lifetime; 只 out 到 book_row)
//
class OrderBookAdapter {
public:
    explicit OrderBookAdapter(OrderBookAdapterConfig cfg = {});

    // Delete copy/move to prevent accidental state loss
    OrderBookAdapter(const OrderBookAdapter&)            = delete;
    OrderBookAdapter& operator=(const OrderBookAdapter&) = delete;
    OrderBookAdapter(OrderBookAdapter&&)                 = delete;
    OrderBookAdapter& operator=(OrderBookAdapter&&)      = delete;

    ~OrderBookAdapter() = default;

    // -----------------------------------------------------------------------
    // OnEvent — 主入口 (vCPU0 hot path)
    //
    //   接受 kBook (snapshot) / kPriceChange (delta) event
    //   返回 true iff 成功更新内部状态且输出了有效的 book_row
    //
    //   out_snap      : 填充 OrderBookSnapshot (v0.1: 1档合成)
    //   out_book_row  : 填充 FeatureStoreBookRow (market_id/sport/event_date/token_side
    //                   由 consumer 通过 InjectMetadata 注入后再使用)
    //   token_id      : 调用方传入 WSS asset_id (event 本身不含 string, 用 bps 字段)
    //   recv_ts_ns    : transport callback 时刻 (= ingestion_ts_ns)
    //
    // R-12: 此函数在 vCPU0 调用, 严禁阻塞
    // R-20: data_source_ts 来自 ev.ts().data_source_ts_ns (UPSTREAM_PAYLOAD)
    // -----------------------------------------------------------------------
    [[nodiscard]] bool OnEvent(const WssEvent&     ev,
                               std::string_view    token_id,
                               std::int64_t        recv_ts_ns,
                               OrderBookSnapshot&  out_snap,
                               FeatureStoreBookRow& out_book_row) noexcept;

    // -----------------------------------------------------------------------
    // InjectMetadata — 填充 FeatureStoreBookRow 中 consumer 域的分区键
    //   (market_id / token_side / sport / event_date / market_type)
    //   由 小田 (#24) consumer 在消费 book_row 前调用
    //
    // 此函数不是热路径 (仅填充字符串), 可在 vCPU1 调用
    // -----------------------------------------------------------------------
    static void InjectMetadata(FeatureStoreBookRow& row,
                               std::string_view     market_id,
                               std::string_view     token_side,
                               std::string_view     sport,
                               std::int32_t         event_date_epoch_days,
                               std::string_view     market_type) noexcept;

    // -----------------------------------------------------------------------
    // ResetToken — 强制清除某 token 状态 (reconnect / resubscribe 时调用)
    // -----------------------------------------------------------------------
    void ResetToken(std::string_view token_id) noexcept;

    // -----------------------------------------------------------------------
    // ResetAll — 清除全部 token 状态 (全量重连时调用)
    // -----------------------------------------------------------------------
    void ResetAll() noexcept;

    // -----------------------------------------------------------------------
    // GetSnapshotState — 查询 token 当前状态 (供监控 / 单测)
    // -----------------------------------------------------------------------
    [[nodiscard]] AdapterSnapshotState GetSnapshotState(std::string_view token_id) const noexcept;

    // -----------------------------------------------------------------------
    // Metrics (供 observability, R-12 原子读)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::uint64_t snapshots_received()   const noexcept { return snapshots_received_; }
    [[nodiscard]] std::uint64_t deltas_accepted()      const noexcept { return deltas_accepted_; }
    [[nodiscard]] std::uint64_t deltas_rejected_early() const noexcept { return deltas_rejected_early_; }
    [[nodiscard]] std::uint64_t ts_violations()        const noexcept { return ts_violations_; }

private:
    // -----------------------------------------------------------------------
    // TokenState — per-token 内部状态 (vCPU0 only, no lock needed)
    // -----------------------------------------------------------------------
    struct TokenState {
        AdapterSnapshotState state       = AdapterSnapshotState::kWaitingSnapshot;
        double               tick_size   = TICK_01;
        // L1 bid/ask (v0.1 单档)
        double               bid_price   = std::numeric_limits<double>::quiet_NaN();
        double               bid_size    = std::numeric_limits<double>::quiet_NaN();
        double               ask_price   = std::numeric_limits<double>::quiet_NaN();
        double               ask_size    = std::numeric_limits<double>::quiet_NaN();
        // 4 ts of last accepted event
        std::int64_t         last_event_ts_ns        = 0;
        std::int64_t         last_data_source_ts_ns  = 0;
        std::int64_t         last_ingestion_ts_ns    = 0;
        // last trade ts (from last_trade_price bps → best proxy is data_source_ts)
        std::int64_t         last_trade_ts_ns        = 0;
    };

    // -----------------------------------------------------------------------
    // Internal helpers (all noexcept, no alloc)
    // -----------------------------------------------------------------------

    // Apply book snapshot from WssEvent payload
    void ApplySnapshot(TokenState& st, const WssEvent& ev, std::int64_t recv_ts_ns) noexcept;

    // Apply price_change delta from WssEvent payload (v0.1: L1 only via mid_bps)
    void ApplyDelta(TokenState& st, const WssEvent& ev, std::int64_t recv_ts_ns) noexcept;

    // Fill OrderBookSnapshot from TokenState + 4-ts
    static void FillSnapshot(const TokenState&   st,
                              std::string_view    token_id,
                              OrderBookSnapshot&  snap) noexcept;

    // Fill FeatureStoreBookRow from OrderBookSnapshot + L1Probe
    static void FillBookRow(const OrderBookSnapshot& snap,
                            FeatureStoreBookRow&      row) noexcept;

    // Validate 4-ts monotonic chain (R-20)
    static bool TsChainOk(const FourTs& ts) noexcept;

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    OrderBookAdapterConfig cfg_;

    // token_id (string) → TokenState
    // v0.1: unordered_map (pre-sized to max_tokens in ctor to minimize rehash)
    std::unordered_map<std::string, TokenState> token_states_;

    // Counters (not atomic — vCPU0 only)
    std::uint64_t snapshots_received_    = 0;
    std::uint64_t deltas_accepted_       = 0;
    std::uint64_t deltas_rejected_early_ = 0;
    std::uint64_t ts_violations_         = 0;
};

}  // namespace stcpp::polymarket::clob_wss
