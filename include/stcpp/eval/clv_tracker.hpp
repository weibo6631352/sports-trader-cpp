// include/stcpp/eval/clv_tracker.hpp — CLV (Closing Line Value) 测量 harness
//
// Owner: 老雷 (GM) — 成果方案 v1 (docs/RESEARCH/laolei-results-plan-v1.md) M3
// last_review: 2026-05-31
//
// 成果尺子 (全员收敛: CLV = 长期盈利第一预测指标, edge 验证最快最低方差):
//   CLV = 参考公平价 − 入场价  (买被低估边 long 视角; 正 = 入场价优于市场最终收敛 = 有 edge)。
//   两个参考价:
//     - close_mid: 市场收盘前最后 mid (低方差, 小蒋首选口径; 比 realized PnL 方差小 1/5~1/10)。
//     - settle:    0/1 结算值 (winner=1/loser=0; ≈ realized PnL, 高方差, 对照用)。
//
// 红线 (小蒋, 前视隔离): CLV 需「未来参考价」→ **只能离线评估, 绝禁进特征/实时推理/决策路径**。
//   本组件只读已观测的成交 + 事后用收盘/结算价算 CLV, 不回喂任何决策。
//
// 用法 (trading_loop, loop_thread_ 单 writer):
//   RecordFill(token, 成交价, 入场mid, size, ts)   — 每笔买入(建仓)成交后
//   UpdateMid(token, 市场mid)                        — 每 tick (收盘 mid 取最后值)
//   OnSettle(token, 结算值0/1)                       — 结算时, 算该 token 所有 fills 的 CLV
//   report()                                          — 聚合: CLV 均值 / 命中率 / 名义加权
#pragma once

#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace stcpp::eval {

class CLVTracker {
public:
    struct FillRec {
        double entry_price{0.0};     // 成交价 (我们买入价)
        double entry_mid{0.0};       // 入场时市场 mid (上下文/诊断)
        double size_pusd{0.0};       // 名义 (CLV 名义加权用)
        std::int64_t entry_ts_ns{0};
    };

    struct Report {
        std::uint64_t n_fills{0};                  // 已结算并计入 CLV 的成交笔数
        double clv_close_mean{0.0};                // 平均 CLV (vs 收盘 mid; 低方差口径)
        double clv_settle_mean{0.0};               // 平均 CLV (vs 0/1 结算; ≈ realized)
        double clv_close_positive_rate{0.0};       // CLV_close > 0 命中率 ∈[0,1]
        double notional_weighted_clv_close{0.0};   // 名义加权 CLV_close (大单更重要)
        std::uint64_t n_pending_fills{0};          // 已记录未结算的成交 (in-flight)
    };

    CLVTracker() = default;

    // 记一笔买入(建仓)成交。被选边 token at entry_price (限价撮合实际成交价)。
    void RecordFill(const std::string& token_id, double entry_price, double entry_mid, double size_pusd,
                    std::int64_t entry_ts_ns) noexcept {
        if (!(entry_price > 0.0) || !(size_pusd > 0.0)) return;  // 脏样本拒
        std::lock_guard<std::mutex> lk(mu_);  // 2026-06-11: HTTP 线程读 (last_mid_for/report) 并发保护
        fills_[token_id].push_back(FillRec{entry_price, entry_mid, size_pusd, entry_ts_ns});
        ++n_pending_;
    }

    // 每 tick 更新该 token 市场 mid (收盘参考价 = 结算前最后一次 mid)。
    void UpdateMid(const std::string& token_id, double market_mid) noexcept {
        if (market_mid > 0.0 && market_mid < 1.0) {
            std::lock_guard<std::mutex> lk(mu_);
            last_mid_[token_id] = market_mid;
        }
    }

    // 末次观测 mid (2026-06-11 老板「盯盘页面也不知道盈亏」): 终局盘无活簿时的估值锚。NaN=无记录。线程安全。
    [[nodiscard]] double last_mid_for(const std::string& token_id) const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        const auto it = last_mid_.find(token_id);
        return it != last_mid_.end() ? it->second : std::numeric_limits<double>::quiet_NaN();
    }

    // 结算: settle_value = 该 token 结算值 (winner=1.0 / loser=0.0)。
    //   算该 token 全部 pending fills 的 CLV (close + settle), 累加聚合, 清该 token。
    void OnSettle(const std::string& token_id, double settle_value) noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = fills_.find(token_id);
        if (it == fills_.end()) return;
        // 收盘参考价: 结算前最后 mid; 无记录则回退结算值 (退化)。
        double close_ref = settle_value;
        auto mit = last_mid_.find(token_id);
        if (mit != last_mid_.end()) close_ref = mit->second;
        for (const auto& f : it->second) {
            const double clv_close = close_ref - f.entry_price;     // 收盘 mid − 入场价
            const double clv_settle = settle_value - f.entry_price; // 0/1 结算 − 入场价
            sum_clv_close_ += clv_close;
            sum_clv_settle_ += clv_settle;
            if (clv_close > 0.0) ++n_positive_close_;
            sum_notional_ += f.size_pusd;
            sum_notional_clv_close_ += f.size_pusd * clv_close;
            ++n_settled_;
        }
        n_pending_ -= it->second.size();
        fills_.erase(it);
        last_mid_.erase(token_id);
    }

    [[nodiscard]] Report report() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        Report r;
        r.n_fills = n_settled_;
        r.n_pending_fills = n_pending_;
        if (n_settled_ > 0) {
            const double n = static_cast<double>(n_settled_);
            r.clv_close_mean = sum_clv_close_ / n;
            r.clv_settle_mean = sum_clv_settle_ / n;
            r.clv_close_positive_rate = static_cast<double>(n_positive_close_) / n;
        }
        if (sum_notional_ > 0.0) {
            r.notional_weighted_clv_close = sum_notional_clv_close_ / sum_notional_;
        }
        return r;
    }

    void Reset() noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        fills_.clear();
        last_mid_.clear();
        n_settled_ = 0;
        n_pending_ = 0;
        n_positive_close_ = 0;
        sum_clv_close_ = 0.0;
        sum_clv_settle_ = 0.0;
        sum_notional_ = 0.0;
        sum_notional_clv_close_ = 0.0;
    }

    // ---- 账本持久化 (2026-06-11 老板「迭代部署 vs 攒数据」根治): 聚合量导出/恢复 + pending 遍历 ----
    //   pending fills 经 RecordFill 重放恢复; 已结算聚合经 RestoreAggregates 整体恢复 (重启不丢 clv_n)。
    struct Aggregates {
        std::uint64_t n_settled{0};
        std::uint64_t n_positive_close{0};
        double sum_clv_close{0.0};
        double sum_clv_settle{0.0};
        double sum_notional{0.0};
        double sum_notional_clv_close{0.0};
    };
    [[nodiscard]] Aggregates aggregates() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return {n_settled_, n_positive_close_, sum_clv_close_, sum_clv_settle_, sum_notional_, sum_notional_clv_close_};
    }
    void RestoreAggregates(const Aggregates& a) noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        n_settled_ = a.n_settled;
        n_positive_close_ = a.n_positive_close;
        sum_clv_close_ = a.sum_clv_close;
        sum_clv_settle_ = a.sum_clv_settle;
        sum_notional_ = a.sum_notional;
        sum_notional_clv_close_ = a.sum_notional_clv_close;
    }
    template <typename F>
    void ForEachPendingFill(F&& fn) const {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& [tok, recs] : fills_) {
            for (const auto& r : recs) fn(tok, r);
        }
    }
    template <typename F>
    void ForEachLastMid(F&& fn) const {  // 持久化 L 行 (终局仓估值锚跨重启, 2026-06-11)
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& [tok, mid] : last_mid_) fn(tok, mid);
    }

private:
    mutable std::mutex mu_;  // 2026-06-11: loop 写 × HTTP 读 (report/last_mid_for) 并发保护 (同 PortfolioMetrics 教训)
    std::unordered_map<std::string, std::vector<FillRec>> fills_;  // 未结算成交 (per token)
    std::unordered_map<std::string, double> last_mid_;             // 各 token 最后市场 mid
    std::uint64_t n_settled_{0};
    std::uint64_t n_pending_{0};
    std::uint64_t n_positive_close_{0};
    double sum_clv_close_{0.0};
    double sum_clv_settle_{0.0};
    double sum_notional_{0.0};
    double sum_notional_clv_close_{0.0};
};

}  // namespace stcpp::eval
