// stcpp/numerical/slippage_model.hpp — SlippageModel header-only lib v0.1 stub
//
// 小肖 SlippageModel cpp lib v1 spec → 此处只写 namespace + 类型骨架
// 真实算法实现交小肖 W3 落代码 (7 单测 + Google Benchmark p99 < 200ns)
//
// 红线:
//   - constexpr 友好, noexcept, 零依赖
//   - p99 < 200ns (RM 同步路径 6us 预算的 3.3%)
//   - 命名碰撞决议: numerical::SlippageMode 区分 execution::PaperMode

#pragma once

#include <cstdint>

namespace stcpp::numerical {

enum class SlippageMode : std::uint8_t {
    Linear = 0,  // MVP, 小袁 microstructure v1 校准 KAPPA_DEPTH_GAMEDAY=1.0
    Sqrt   = 1,  // M5 后, Almgren-Chriss 触发阈 RMSE(B)/RMSE(A) < 0.85
    Clob   = 2,  // M5 后, 真实 CLOB 微观仿真
};

struct SlippageInput {
    // 4 时间戳 (R-20)
    std::int64_t event_ts_ns;
    std::int64_t data_source_ts_ns;
    std::int64_t ingestion_ts_ns;
    std::int64_t as_of_ts_ns;
    std::int64_t book_snapshot_ts_ns;  // INVALID_INTENT.BOOK_TS_* 子原因来源
    // TODO(小肖 W3): order_size / quote_price / book_depth / time_since_quote
};

struct SlippageOutput {
    double expected_fill_price{0.0};
    double expected_fill_rate{0.0};  // floor 0.50 (小袁 实测 + 老韩 v0.3 reject)
    double slippage_bps{0.0};
    // TODO(小肖 W3): confidence / sub_reason if reject
};

// 配置常量 (小肖 v1 §3.1)
inline constexpr std::int64_t T_HALFLIFE_QUOTE_MS = 30'000;
inline constexpr std::int64_t T_HALFLIFE_QUOTE_HOT_MS = 500;
inline constexpr double FILL_RATE_FLOOR = 0.50;
inline constexpr double KAPPA_DEPTH_GAMEDAY = 1.0;  // 小袁 v1 校准
inline constexpr std::int64_t STALE_MAX_NS = 60'000'000'000LL;  // 60s INVALID_INTENT

class SlippageModel {
public:
    // TODO(小肖 W3): constexpr compute(input) -> output
    // [[nodiscard]] constexpr auto compute(SlippageInput const&) const noexcept -> SlippageOutput;
};

}  // namespace stcpp::numerical
