// src/stcpp/strategy/live_section_classifier.cpp — LiveSection v0.1 impl
//
// 落: 小程 P0-01 spec v0.1 §LiveSection

#include "stcpp/strategy/live_section_classifier.hpp"

namespace stcpp::strategy {

LiveSection classify(GameState const& g, std::int64_t now_ns) noexcept {
    // 1. Delayed 凌驾 (即便 live=true 也算 Delayed)
    if (g.delayed) {
        return LiveSection::Delayed;
    }
    // 2. Closed (终态)
    if (g.ended) {
        return LiveSection::Closed;
    }
    // 3. Live (live==true && ended==false; ended 已 short-circuit)
    if (g.live) {
        return LiveSection::Live;
    }
    // 4. Soon vs Future (按 kickoff 距 now)
    std::int64_t const delta_ns = g.kickoff_ts_ns - now_ns;
    if (delta_ns > 0 && delta_ns < SIX_HOURS_NS) {
        return LiveSection::Soon;
    }
    // 5. Future (kickoff_ts - now >= 6h, 含 == 6h)
    //    注: delta_ns <= 0 (kickoff 已过 但 live=false ended=false delayed=false) →
    //    属边界 (e.g., 待开赛源数据未更新). spec 未定义, 归 Future 兜底待源刷新.
    return LiveSection::Future;
}

}  // namespace stcpp::strategy
