// trade_flow.hpp — 每笔成交方向+量 → 滚动 trade-flow 聚合 (老板 2026-06-01: 成交方向/量没进特征)。
//
// Owner: 老雷 (GM)
// last_review: 2026-06-01
//
// 背景: WSS last_trade_price 事件带 side(BUY/SELL=taker 主动方) + size, 是最干净的流量方向信号
//   (b_ofi 从快照推算; 谁主动吃单 直接从 WSS 来)。此前 LiveBookPublisher 丢弃 trade 事件。
//   per-token 滚动窗口聚合: 签名净流 (买主动−卖主动) / 买方占比 / 成交强度。纯函数, 可单测。
//
// R-20: trade ts 用上游 data_source_ts (WSS timestamp), 窗口比较用同源 now (book 帧的 as_of)。
#ifndef STCPP_MICROSTRUCTURE_TRADE_FLOW_HPP
#define STCPP_MICROSTRUCTURE_TRADE_FLOW_HPP

#include <cstddef>
#include <cstdint>
#include <limits>

namespace stcpp::microstructure {

struct TradeFlowMetrics {
    double signed_vol{std::numeric_limits<double>::quiet_NaN()};  // 窗口内 Σ(买主动 − 卖主动) notional (净方向流)
    double buy_ratio{std::numeric_limits<double>::quiet_NaN()};   // 买主动量 / 总量 ∈ [0,1] (>0.5=买压)
    double intensity{std::numeric_limits<double>::quiet_NaN()};   // 窗口内成交笔数 (活跃度)
};

// 固定容量 ring (per-token; 单写线程 = WSS OnFrame, R-12 同步)。256 笔 × per-token 内存可控。
class TradeFlowWindow {
public:
    static constexpr std::size_t kCap = 256;
    static constexpr std::int64_t kDefaultWindowNs = 300'000'000'000LL;  // 5 分钟

    // 记一笔成交。size_usdc>0; is_buy = taker 主动买 (side=="BUY")。
    void observe(std::int64_t ts_ns, double size_usdc, bool is_buy) noexcept {
        if (!(size_usdc > 0.0) || ts_ns <= 0)
            return;
        buf_[head_] = Trade{ts_ns, size_usdc, is_buy};
        head_ = (head_ + 1) % kCap;
        if (count_ < kCap)
            ++count_;
    }

    // 窗口快照 (now_ns 来自同源 book as_of; 仅算 [now−window, now] 内)。空 → 全 NaN。
    [[nodiscard]] TradeFlowMetrics snapshot(std::int64_t now_ns,
                                            std::int64_t window_ns = kDefaultWindowNs) const noexcept {
        TradeFlowMetrics m;
        if (count_ == 0 || now_ns <= 0)
            return m;
        const std::int64_t cutoff = now_ns - window_ns;
        double buy_vol = 0.0, sell_vol = 0.0;
        int n = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            const Trade& t = buf_[i];
            if (t.ts_ns < cutoff || t.ts_ns > now_ns)
                continue;  // 窗口外 (含未来守卫)
            if (t.is_buy)
                buy_vol += t.size;
            else
                sell_vol += t.size;
            ++n;
        }
        if (n == 0)
            return m;
        const double total = buy_vol + sell_vol;
        m.signed_vol = buy_vol - sell_vol;
        m.buy_ratio = (total > 0.0) ? buy_vol / total : std::numeric_limits<double>::quiet_NaN();
        m.intensity = static_cast<double>(n);
        return m;
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    struct Trade {
        std::int64_t ts_ns{0};
        double size{0.0};
        bool is_buy{false};
    };
    Trade buf_[kCap]{};
    std::size_t head_{0};
    std::size_t count_{0};
};

}  // namespace stcpp::microstructure

#endif  // STCPP_MICROSTRUCTURE_TRADE_FLOW_HPP
