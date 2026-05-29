// include/stcpp/polymarket/clob_wss/orderbook_adapter.hpp
//
// Owner: 小冯 (#34)  主管: 小余 (D 数据基础设施部)
// ADR-037 data-model-strategy-vendor-agnostic 配套实施
// Wave 小余-ADR037 — Polymarket CLOB full-depth orderbook adapter v0.1
//
// 职责:
//   把 PolymarketCLOBSubscriber 产出的 WssEvent(kBook / kPriceChange) 转换成
//   vendor-agnostic OrderBookFeatures —— 下游 (小田#24 feature store / 小梁 quant /
//   老韩 RM) 不需知道 Polymarket wire format.
//
// 设计要点:
//   1. Full-depth: 最多 kAdapterBookDepth=20 档 bid/ask (Polymarket CLOB 实测 ≤20)
//   2. 微观结构特征 (派单 §1): microprice / spread_bps / imbalance / top_N_depth_usdc
//   3. reconnect chaos 健壮性:
//      - 收到 book snapshot (is_snapshot=1) → 全量替换本地 book
//      - price_change delta (is_snapshot=0) → 仅更新有变化的档位 + 合并重计算
//      - 断连 (OnAdapterReset) → 清空 book + 等新 snapshot
//      - 乱序 (sequence_no gap) → 清空 + 标记 waiting_snapshot_ → 丢弃 delta 直到新 snapshot
//   4. R-20 4 ts 透传: ingestion_ts_ns 从 WssEvent FourTs 原样透传, 不本地 now() 冒充
//   5. R-12 event loop 友好: Compute() 纯计算, 无 IO / 锁 / malloc
//   6. vendor-agnostic: OrderBookFeatures 不含 Polymarket 专有字段名 (caller 持 market_id)
//
// 红线:
//   R-12: Compute() / OnDelta() / OnSnapshot() 严禁阻塞 IO, 禁 malloc (std::array only)
//   R-20: out.ts.data_source_ts_ns 必须来自 WssEvent::ts().data_source_ts_ns (禁 now())
//         out.ts.ingestion_ts_ns 来自 WssEvent::ts().ingestion_ts_ns (transport 入队时间)
//   R-7:  header-only types; 无 ExecutionMode 依赖
//
// 不耻下问:
//   feature store schema 对齐 → @小田#24
//   RM MarketState 使用 adapter 输出 → @老韩
//   simdjson 升级 (price_change levels 数组解析) → @老李 W10
//
// ============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/polymarket/wss/wss_event.hpp"

namespace stcpp::polymarket::clob_wss {

// ---------------------------------------------------------------------------
// 1. Depth capacity
// ---------------------------------------------------------------------------

// Polymarket CLOB websocket: 实测 bid/ask 各最多 20 档 (常见 5-15 档)
inline constexpr std::size_t kAdapterBookDepth = 20;

// ---------------------------------------------------------------------------
// 2. OrderBookFeatures — vendor-agnostic adapter output (R-7 mode-agnostic)
// ---------------------------------------------------------------------------
//
// 下游消费者只需要这个结构, 不需要了解 Polymarket wire format.
// 所有 price 字段均 ∈ (0, 1) float64 (Polymarket binary-outcome probability market).
//
struct OrderBookFeatures {
    // R-20 4 ts 透传 (来自 WssEvent FourTs, 不本地 now() 替代 data_source_ts)
    stcpp::microstructure::OrderBookTs ts{};

    // --- 订单簿档位 (full depth) ---
    // bid[0] = best bid, bid[1] = second best bid, ...  (价格降序)
    // ask[0] = best ask, ask[1] = second best ask, ...  (价格升序)
    // 未填充档位: price=0.0, size_usdc=0.0
    std::array<stcpp::microstructure::OrderBookLevel, kAdapterBookDepth> bid{};
    std::array<stcpp::microstructure::OrderBookLevel, kAdapterBookDepth> ask{};
    std::uint8_t bid_depth{0};  // 实际有效 bid 档数 (≤ kAdapterBookDepth)
    std::uint8_t ask_depth{0};  // 实际有效 ask 档数 (≤ kAdapterBookDepth)

    // --- L1 微观结构特征 ---
    double microprice{0.0};    // 量加权中间价 (§2.3 cap: |micro-mid| ≤ 2 tick)
    double mid{0.0};           // (best_bid + best_ask) / 2
    double imbalance{0.0};     // (bid_qty - ask_qty) / (bid_qty + ask_qty)  ∈ [-1, 1]
    double spread{0.0};        // best_ask - best_bid  (absolute, ∈ [0, 1))
    std::int32_t spread_bps{0};  // spread × 10000

    // --- 深度累计特征 ---
    double top3_depth_usdc_bid{0.0};  // bid 侧 ≤3 档累计 notional (USD)
    double top3_depth_usdc_ask{0.0};  // ask 侧 ≤3 档累计 notional (USD)
    double total_depth_usdc_bid{0.0}; // bid 侧全档累计 notional
    double total_depth_usdc_ask{0.0}; // ask 侧全档累计 notional

    // --- 元数据 ---
    double       tick_size{stcpp::microstructure::TICK_01};  // 0.01 or 0.001
    std::int64_t last_trade_ts_ns{0};   // 最近成交时间 (上游 ts, 0 = 未知)
    double       last_trade_price{0.0}; // 最近成交价 ∈ (0,1)
    bool         is_snapshot{false};    // true = 来自 book snapshot, false = delta
    bool         valid{false};          // false = book 尚未收到 snapshot, 特征不可用
};

// ---------------------------------------------------------------------------
// 3. IOrderBookFeatureSink — 下游消费者实现此接口
// ---------------------------------------------------------------------------
//
// 和 ISpscEventSink 类似, 但面向 vendor-agnostic features 而不是 WssEvent.
// 典型实现: 小田#24 FeatureStore SPSC ring / 老韩 RM MarketState updater.
//
class IOrderBookFeatureSink {
public:
    virtual ~IOrderBookFeatureSink() = default;
    // 非阻塞 push; 满返 false (调用方决定 drop 策略, R-12)
    virtual bool OnFeatures(const OrderBookFeatures& features) noexcept = 0;
};

// ---------------------------------------------------------------------------
// 4. OrderBookAdapterState — reconnect-safe per-token state
// ---------------------------------------------------------------------------
//
// 每个 token_id 对应一个实例.
// 单线程访问 (vCPU0, CLOBSubscriber callback 线程) — 无锁.
//
// Reconnect chaos 健壮性:
//   WAITING_SNAPSHOT: 初始 / 断连 / gap 后, 丢 delta, 等 snapshot
//   LIVE:             已收到 snapshot, 接受 delta
//
enum class AdapterBookState : std::uint8_t {
    kWaitingSnapshot = 0,
    kLive = 1,
};

class OrderBookAdapterState {
public:
    OrderBookAdapterState() = default;

    // 重连或 sequence gap 后重置 (清空 book, 回到等待 snapshot 状态)
    void Reset() noexcept;

    // 处理 book snapshot (is_snapshot=1 的 WssEvent 解析结果)
    // 参数: price_levels 数组按顺序: bid[0..bid_n-1], ask[0..ask_n-1]
    //       data 来自 CLOBSubscriber ParseBook 输出的 WssEvent
    //       ts 来自 WssEvent FourTs (R-20)
    // 返回: 计算后的 OrderBookFeatures (valid=true)
    OrderBookFeatures OnSnapshot(
        const stcpp::microstructure::OrderBookLevel* bid_levels, std::uint8_t bid_n,
        const stcpp::microstructure::OrderBookLevel* ask_levels, std::uint8_t ask_n,
        const stcpp::microstructure::OrderBookTs& ts,
        double tick_size,
        double last_trade_price,
        std::int64_t last_trade_ts_ns) noexcept;

    // 处理 price_change delta (is_snapshot=0)
    // v0.1: levels 单 bid + 单 ask (simdjson 升级后支持多 level 数组, @老李 W10)
    // 返回: valid=false 如果仍在等待 snapshot
    OrderBookFeatures OnDelta(
        const stcpp::microstructure::OrderBookLevel* bid_levels, std::uint8_t bid_n,
        const stcpp::microstructure::OrderBookLevel* ask_levels, std::uint8_t ask_n,
        const stcpp::microstructure::OrderBookTs& ts,
        double tick_size) noexcept;

    // 当前订单簿是否有效 (已收到至少一个 snapshot)
    [[nodiscard]] bool IsLive() const noexcept {
        return state_ == AdapterBookState::kLive;
    }

    // 暴露当前 state (供 metrics)
    [[nodiscard]] AdapterBookState book_state() const noexcept { return state_; }

    // 当前本地存储的 book (调试 / 测试用)
    [[nodiscard]] const std::array<stcpp::microstructure::OrderBookLevel, kAdapterBookDepth>&
    raw_bid() const noexcept { return bid_; }
    [[nodiscard]] const std::array<stcpp::microstructure::OrderBookLevel, kAdapterBookDepth>&
    raw_ask() const noexcept { return ask_; }
    [[nodiscard]] std::uint8_t raw_bid_depth() const noexcept { return bid_depth_; }
    [[nodiscard]] std::uint8_t raw_ask_depth() const noexcept { return ask_depth_; }

private:
    // 内部 book 存储 (bid 价格降序, ask 价格升序)
    std::array<stcpp::microstructure::OrderBookLevel, kAdapterBookDepth> bid_{};
    std::array<stcpp::microstructure::OrderBookLevel, kAdapterBookDepth> ask_{};
    std::uint8_t bid_depth_{0};
    std::uint8_t ask_depth_{0};
    double       tick_size_{stcpp::microstructure::TICK_01};
    double       last_trade_price_{0.0};
    std::int64_t last_trade_ts_ns_{0};
    AdapterBookState state_{AdapterBookState::kWaitingSnapshot};

    // 从当前 book + ts 计算 OrderBookFeatures (pure, no IO)
    [[nodiscard]] OrderBookFeatures Compute(
        const stcpp::microstructure::OrderBookTs& ts,
        bool is_snapshot) const noexcept;

    // 将 levels 写入 side (最多 kAdapterBookDepth 档)
    static void FillSide(
        std::array<stcpp::microstructure::OrderBookLevel, kAdapterBookDepth>& dst,
        std::uint8_t& dst_n,
        const stcpp::microstructure::OrderBookLevel* src,
        std::uint8_t src_n) noexcept;

    // delta: upsert 单档 (price 精确匹配 or 追加)
    static void UpsertLevel(
        std::array<stcpp::microstructure::OrderBookLevel, kAdapterBookDepth>& side,
        std::uint8_t& depth,
        const stcpp::microstructure::OrderBookLevel& lvl,
        bool ascending) noexcept;
};

// ---------------------------------------------------------------------------
// 5. OrderBookAdapter — top-level: 多 token 管理 + sink 路由
// ---------------------------------------------------------------------------
//
// 使用方 (CLOBSubscriber 包装层) 调用流程:
//   1. 构造时注入 IOrderBookFeatureSink
//   2. 收到 WssEvent → 调用 ProcessEvent(token_id, ev)
//   3. 断连 → 调用 OnTransportReset() 清空全部 token 状态
//
// 线程安全: 同 CLOBSubscriber, 仅 vCPU0 callback 线程调用. 无锁.
// map 实现: 固定 std::array (最多 kAdapterMaxTokens token) — 避免 malloc on hot path.
//
inline constexpr std::size_t kAdapterMaxTokens = 16;  // 热 token 池上限

struct AdapterTokenEntry {
    std::string           token_id;   // token_id string
    OrderBookAdapterState state;
    bool                  active{false};
};

class OrderBookAdapter {
public:
    explicit OrderBookAdapter(IOrderBookFeatureSink* sink) noexcept;

    // 处理来自 CLOBSubscriber market channel 的 WssEvent
    //   - kBook: snapshot → OnSnapshot
    //   - kPriceChange: delta → OnDelta
    // token_id 来自 WssEvent 解析的 asset_id (调用方传入, adapter 不重解析 wire)
    void ProcessEvent(std::string_view token_id,
                      const stcpp::polymarket::wss::WssEvent& ev) noexcept;

    // 断连/重连时清空所有 token 状态 (等待新 snapshot)
    void OnTransportReset() noexcept;

    // 单 token reset (sequence gap 时由 CLOBSubscriber 触发)
    void OnTokenReset(std::string_view token_id) noexcept;

    // token 注册 (Start 前调用, 同 CLOBSubscriber initial_market_token_ids)
    // 超过 kAdapterMaxTokens 静默忽略 (metrics 计数)
    bool RegisterToken(std::string_view token_id) noexcept;

    // 已注册 token 数量
    [[nodiscard]] std::uint8_t token_count() const noexcept { return token_count_; }

    // drop 计数 (sink 满 / token 未注册 / delta before snapshot)
    [[nodiscard]] std::uint64_t drop_count() const noexcept { return drop_count_; }
    [[nodiscard]] std::uint64_t snapshot_count() const noexcept { return snapshot_count_; }
    [[nodiscard]] std::uint64_t delta_count() const noexcept { return delta_count_; }

private:
    IOrderBookFeatureSink* sink_;
    std::array<AdapterTokenEntry, kAdapterMaxTokens> tokens_{};
    std::uint8_t  token_count_{0};
    std::uint64_t drop_count_{0};
    std::uint64_t snapshot_count_{0};
    std::uint64_t delta_count_{0};

    // Find token slot by token_id (O(n), n≤16 — acceptable on hot path for ≤16 tokens)
    AdapterTokenEntry* FindToken(std::string_view token_id) noexcept;

    // 从 WssEvent payload 中提取 L2 levels (v0.1: 解析 last_trade_price / tick_size 只)
    // Full level array parsing deferred to W10 simdjson (@老李)
    // v0.1: book snapshot 提供 top-1 bid/ask (从 last_trade_price_bps 推算)
    //       price_change delta: 单 bid + 单 ask (从 mid_bps 推算)
    // 注意: v0.1 最多填 1 档; multi-level 在 W10 simdjson 升级后扩展.
    static void ExtractBookLevels(
        const stcpp::polymarket::wss::OrderBookL2Update& book,
        stcpp::microstructure::OrderBookLevel* bid_out, std::uint8_t& bid_n,
        stcpp::microstructure::OrderBookLevel* ask_out, std::uint8_t& ask_n,
        double& tick_size_out,
        double& last_trade_price_out) noexcept;

    // Convert bps → price double
    [[nodiscard]] static double BpsToPrice(std::uint32_t bps) noexcept {
        return static_cast<double>(bps) * 1e-4;
    }

    // Convert OrderBookTs from FourTs (R-20)
    [[nodiscard]] static stcpp::microstructure::OrderBookTs ToObTs(
        const stcpp::polymarket::wss::FourTs& fts) noexcept;
};

}  // namespace stcpp::polymarket::clob_wss
