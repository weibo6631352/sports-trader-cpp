// stcpp/strategy/live_section_classifier.hpp — LiveSection 5 分类 v0.1
//
// 落: 小程 P0-01 spec v0.1 §LiveSection (老李 GM 逆向反推)
//
// 5 enum:
//   Live    : game.live == true  && game.ended == false
//   Soon    : now < kickoff_ts && (kickoff_ts - now) < 6h
//   Delayed : game.delayed == true (覆盖 Live/Soon 之后判定)
//   Closed  : game.ended  == true
//   Future  : kickoff_ts - now >= 6h
//
// 红线:
//   R-20  classify(state, now_ns) 由调用方注入 now_ns (deterministic 单测)
//   严格按 spec 序判定 (Delayed > Closed > Live > Soon > Future 互斥树)

#pragma once

#include <cstdint>
#include <string_view>

namespace stcpp::strategy {

enum class LiveSection : std::uint8_t {
    Live    = 0,
    Soon    = 1,
    Delayed = 2,
    Closed  = 3,
    Future  = 4,
};

[[nodiscard]] constexpr std::string_view to_string(LiveSection s) noexcept {
    switch (s) {
        case LiveSection::Live:    return "Live";
        case LiveSection::Soon:    return "Soon";
        case LiveSection::Delayed: return "Delayed";
        case LiveSection::Closed:  return "Closed";
        case LiveSection::Future:  return "Future";
    }
    return "unknown";
}

// Goalserve / Polymarket GameState minimal projection.
// W5 接老彭 Goalserve client → 真 model; 当前 POD 给单测 + signal layer.
struct GameState {
    std::int64_t kickoff_ts_ns{0};
    bool         live{false};
    bool         ended{false};
    bool         delayed{false};
};

inline constexpr std::int64_t NS_PER_S    = 1'000'000'000LL;
inline constexpr std::int64_t SIX_HOURS_S = 6 * 60 * 60;
inline constexpr std::int64_t SIX_HOURS_NS = SIX_HOURS_S * NS_PER_S;

// 严格按 spec 序: Delayed → Closed → Live → Soon → Future.
// 注: spec 文字定义 Live/Soon/Delayed 并列,
// 但 delayed=true 时 (e.g., 雨天延迟) live 可能仍 true (一档 transient),
// 实际语义 Delayed 凌驾, 故先判.
[[nodiscard]] LiveSection classify(GameState const& g, std::int64_t now_ns) noexcept;

}  // namespace stcpp::strategy
