// stcpp/polymarket/wss/wss_event.hpp — PM sports channel event POD (v0.1)
// Owner: 小冯 (#34)  spec: 小余 W5-D-02 + 老周 W5-A-02. W5 Wave 24.
// 落: laoli-polymarket-backend-requirements-v1.md §2.2 (8 sub topic) + §4 (4 ts)
//     laozhou-architecture-v0.6-e2e.md §3.1; ADR-2026-05-28-gm-redline-data-source-timestamping.md
// 红线: R-20 4 ts 不等式; R-12 POD 设计 (vCPU0 → vCPU1 SPSC ring 零拷贝).
// 不耻下问: 4 ts 字段位置 @老李 §4.1; FeatureSnapshot 字段需求 @老周 v0.6 §3.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace stcpp::polymarket::wss {

// 8 sub topic enum (老李 §2.2 全表)
enum class SubTopic : std::uint8_t {
    kMarket = 0,  kGame = 1,  kOutcomes = 2,  kBook = 3,
    kPriceChange = 4,  kLastTradePrice = 5,  kTickSizeChange = 6,  kSystemStatus = 7,
};
constexpr std::size_t kNumSubTopics = 8;

[[nodiscard]] constexpr std::string_view SubTopicName(SubTopic t) noexcept {
    switch (t) {
        case SubTopic::kMarket:         return "market";
        case SubTopic::kGame:           return "game";
        case SubTopic::kOutcomes:       return "outcomes";
        case SubTopic::kBook:           return "book";
        case SubTopic::kPriceChange:    return "price_change";
        case SubTopic::kLastTradePrice: return "last_trade_price";
        case SubTopic::kTickSizeChange: return "tick_size_change";
        case SubTopic::kSystemStatus:   return "system";
    }
    return "";
}

// R-20 §2.2 enum (subset for PM)
enum class DataSourceTsOrigin : std::uint8_t {
    kUpstreamPayload    = 0,  // payload.timestamp 直采 (期望 ≥ 95%)
    kUpstreamHeader     = 1,  // response header Date (≤ 5%)
    kInferredFromDsTs   = 2,  // 同 batch 推断 (< 1%)
    kInferredFromIngest = 3,  // fallback 本地 now (R-20 违规, 月度 sweep 报警)
};

// R-20 4 时间戳契约: event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts ≤ now()
struct FourTs {
    std::int64_t       event_ts_ns       = 0;
    std::int64_t       data_source_ts_ns = 0;
    std::int64_t       ingestion_ts_ns   = 0;
    std::int64_t       as_of_ts_ns       = 0;
    DataSourceTsOrigin ds_origin         = DataSourceTsOrigin::kUpstreamPayload;

    [[nodiscard]] constexpr bool IsMonotonic() const noexcept {
        return event_ts_ns       <= data_source_ts_ns
            && data_source_ts_ns <= ingestion_ts_ns
            && ingestion_ts_ns   <= as_of_ts_ns;
    }
};

// `price_change` topic changes[] 单元 (v0.1 不填字段值, W6 simdjson)
struct PriceLevelDelta {
    std::uint32_t price_bps   = 0;   std::uint64_t size_micro  = 0;
    std::uint8_t  side        = 0;   std::int64_t  level_ts_ns = 0;
};
inline constexpr std::size_t kMaxDeltaPerMsg = 16;

// `market` / `book` / `price_change` 统一形态 (v0.1 顶层标量, L2 levels 待 W6 simdjson)
struct OrderBookL2Update {
    FourTs        ts{};                                       // R-20
    std::uint64_t market_id            = 0;                   // condition_id 截断
    std::uint64_t token_id_yes         = 0;
    std::uint64_t token_id_no          = 0;
    std::uint32_t mid_bps              = 0;                   // mid × 10000
    std::uint32_t spread_bps           = 0;
    std::uint32_t last_trade_price_bps = 0;
    std::uint32_t tick_size_bps        = 0;                   // 1 / 10 / 100 (bps)
    std::uint8_t  neg_risk             = 0;
    std::uint8_t  num_changes          = 0;
    std::uint8_t  is_snapshot          = 0;                   // 1 = full snapshot
    SubTopic      topic                = SubTopic::kMarket;
    std::array<PriceLevelDelta, kMaxDeltaPerMsg> changes{};
};

// `game:{event_id}` (period / score / state / time_remaining_s)
enum class GameState : std::uint8_t {
    kScheduled = 0, kPregame = 1, kLive = 2, kHalftime = 3,
    kEnded     = 4, kCancelled = 5, kPostponed = 6,
};

struct GameStateUpdate {
    FourTs        ts{};
    std::uint64_t event_id         = 0;
    std::uint64_t market_id        = 0;
    std::uint16_t sport            = 0;
    std::uint16_t period           = 0;
    std::int32_t  time_remaining_s = 0;
    std::int32_t  score_home       = 0;
    std::int32_t  score_away       = 0;
    GameState     game_state       = GameState::kScheduled;
};

struct LastTradePriceUpdate {
    FourTs ts{};
    std::uint64_t token_id = 0,   market_id = 0,   size_micro = 0;
    std::uint32_t price_bps = 0;  std::uint8_t side = 0;
};
struct TickSizeChangeUpdate {
    FourTs ts{};
    std::uint64_t token_id = 0,    market_id = 0;
    std::uint32_t old_tick_bps = 0, new_tick_bps = 0;
};

enum class ResolutionStatus : std::uint8_t { kOpen = 0, kResolving = 1, kResolved = 2 };
struct OutcomesUpdate {
    FourTs ts{};
    std::uint64_t market_id = 0;
    ResolutionStatus resolution_status = ResolutionStatus::kOpen;
    std::array<std::uint32_t, 2> outcome_price_bps = {0, 0};  // [Yes, No]
    std::array<std::uint32_t, 2> last_price_bps    = {0, 0};
    std::array<std::uint64_t, 2> volume_24h_micro  = {0, 0};
};

enum class SystemHealth : std::uint8_t { kHealthy = 0, kDegraded = 1, kMaintenance = 2 };
struct SystemStatusUpdate { FourTs ts{}; SystemHealth health = SystemHealth::kHealthy; };

// Discriminated union (vCPU0 → vCPU1 SPSC ring 零拷贝; 不用 std::variant)
struct WssEvent {
    SubTopic topic = SubTopic::kMarket;
    union Payload {
        OrderBookL2Update    book;
        GameStateUpdate      game;
        LastTradePriceUpdate trade;
        TickSizeChangeUpdate tick;
        OutcomesUpdate       outcomes;
        SystemStatusUpdate   system;
        Payload() : book{} {}
        ~Payload() = default;
    } payload;

    [[nodiscard]] const FourTs& ts() const noexcept {
        switch (topic) {
            case SubTopic::kGame:           return payload.game.ts;
            case SubTopic::kLastTradePrice: return payload.trade.ts;
            case SubTopic::kTickSizeChange: return payload.tick.ts;
            case SubTopic::kOutcomes:       return payload.outcomes.ts;
            case SubTopic::kSystemStatus:   return payload.system.ts;
            default:                        return payload.book.ts;
        }
    }
};

static_assert(std::is_standard_layout_v<OrderBookL2Update>);
static_assert(std::is_trivially_copyable_v<OrderBookL2Update>);
static_assert(std::is_trivially_copyable_v<GameStateUpdate>);

}  // namespace stcpp::polymarket::wss
