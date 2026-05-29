// stcpp/backtest/walk_forward.hpp — Walk-forward splitter v0.2
//
// Owner: 小蒋 (quant-backtest)
// Last review: 2026-05-29
//
// 落: xiaojiang-backtest-framework-v0.2-cpp.md §3 (walk-forward 设计)
//     xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md §3.2 (purged k-fold 切分)
//
// 红线:
//   BR-3: OOS 数据严禁参与任何阈值调参 (DataAccessGuard 编译期强制)
//   R-20: 4 ts 单调链检验 (trade_ts_ok)
//
// 设计:
//   1. WalkForwardSplitter — 生成滚动 IS/OOS 时间窗序列
//   2. DataAccessGuard<Phase> — 编译期 IS/OOS 物理隔离 (只允许 IS phase 调参)
//   3. PurgedKFold — 按 game_id 分组 + 7 天 embargo (防 game 内泄漏)

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "stcpp/backtest/types.hpp"

namespace stcpp::backtest {

// ---------------------------------------------------------------------------
// 1. Phase 枚举 (DataAccessGuard 模板参数)
// ---------------------------------------------------------------------------

enum class Phase : std::uint8_t {
    InSample     = 0,  // IS 阶段: 允许参数调优
    OutOfSample  = 1,  // OOS 阶段: 禁止调参, 只允许评估
    Frozen       = 2,  // Frozen: 参数已锁定, 仅 OOS 评估
};

// ---------------------------------------------------------------------------
// 2. WalkForwardSplitter — 生成滚动 IS/OOS 分割序列
// ---------------------------------------------------------------------------
//
// 对应 §3.1 rolling walk-forward:
//   IS-1 | Embargo | OOS-1 |
//         IS-2 | Embargo | OOS-2 |
//               IS-3 | Embargo | OOS-3 |
//

struct WalkForwardConfig {
    std::int64_t  train_duration_ns;     // IS 窗口长度 (ns)
    std::int64_t  oos_duration_ns;       // OOS 窗口长度 (ns)
    std::int64_t  embargo_duration_ns;   // embargo 长度 (ns, 默认 7 天)
    std::int64_t  step_duration_ns;      // 滚动步长 (ns, 默认 = oos_duration)
    std::uint32_t max_folds;             // 最大 fold 数 (0 = 无限制)

    // 常用默认值 (§3.2 P0-02 pregame 建议窗口)
    static constexpr std::int64_t kDayNs  = 86'400'000'000'000LL;
    static constexpr std::int64_t k7DaysNs  = 7LL  * kDayNs;
    static constexpr std::int64_t k30DaysNs = 30LL * kDayNs;
    static constexpr std::int64_t k90DaysNs = 90LL * kDayNs;

    // P0-02 建议: IS=150d, OOS=120d, embargo=7d
    static WalkForwardConfig default_p0_02() {
        WalkForwardConfig cfg;
        cfg.train_duration_ns    = 150LL * kDayNs;
        cfg.oos_duration_ns      = 120LL * kDayNs;
        cfg.embargo_duration_ns  = k7DaysNs;
        cfg.step_duration_ns     = 120LL * kDayNs;  // 不重叠滚动
        cfg.max_folds            = 4;
        return cfg;
    }
};

class WalkForwardSplitter {
public:
    explicit WalkForwardSplitter(WalkForwardConfig cfg) : cfg_(cfg) {}

    // 生成所有 WalkForwardWindow (按时序排列)
    // data_start / data_end: 整个数据集的时间范围 (ns epoch)
    [[nodiscard]] std::vector<WalkForwardWindow> generate(
        std::int64_t data_start_ns,
        std::int64_t data_end_ns) const {

        std::vector<WalkForwardWindow> windows;

        if (data_end_ns <= data_start_ns) {
            return windows;
        }

        std::int64_t cursor = data_start_ns;
        std::uint32_t fold  = 0;

        while (true) {
            std::int64_t const is_end      = cursor + cfg_.train_duration_ns;
            std::int64_t const emb_end     = is_end + cfg_.embargo_duration_ns;
            std::int64_t const oos_end     = emb_end + cfg_.oos_duration_ns;

            // OOS 结束超出数据范围 → 停止
            if (oos_end > data_end_ns) {
                break;
            }

            WalkForwardWindow w;
            w.is_start_ns    = cursor;
            w.is_end_ns      = is_end;
            w.embargo_end_ns = emb_end;
            w.oos_start_ns   = emb_end;
            w.oos_end_ns     = oos_end;
            w.fold_index     = fold;
            windows.push_back(w);

            ++fold;
            cursor += cfg_.step_duration_ns;

            if (cfg_.max_folds > 0 && fold >= cfg_.max_folds) {
                break;
            }
        }

        return windows;
    }

private:
    WalkForwardConfig cfg_;
};

// ---------------------------------------------------------------------------
// 3. DataAccessGuard<Phase> — 编译期 IS/OOS 物理隔离
// ---------------------------------------------------------------------------
//
// 关键约束:
//   - freeze_params: 仅在 IS phase 可调用 (OOS/Frozen phase 编译期拒绝)
//   - get_trades_in_window: 运行期校验 as_of_ts 不超出本 guard 允许窗口
//

template <Phase P>
class DataAccessGuard {
public:
    DataAccessGuard(std::int64_t window_start_ns, std::int64_t window_end_ns)
        : window_start_ns_(window_start_ns), window_end_ns_(window_end_ns) {}

    // 运行期 PIT 校验: 请求的 trade 的 as_of_ts 必须在本 guard 窗口内
    [[nodiscard]] bool is_trade_accessible(TradeRecord const& t) const noexcept {
        return t.as_of_ts_ns >= window_start_ns_
            && t.as_of_ts_ns <= window_end_ns_
            && trade_ts_ok(t);
    }

    // 过滤出本 guard 窗口内的 trade (含 PIT 校验)
    [[nodiscard]] std::vector<TradeRecord> filter_trades(
        std::vector<TradeRecord> const& trades) const {

        std::vector<TradeRecord> out;
        out.reserve(trades.size());
        for (auto const& t : trades) {
            if (is_trade_accessible(t)) {
                out.push_back(t);
            }
        }
        return out;
    }

    // 冻结参数 — 仅 IS phase 可调
    // OOS / Frozen phase 调用此函数 → 编译错误
    template <Phase Q = P>
    std::enable_if_t<Q == Phase::InSample, void>
    freeze_params(ParamSet const& params) {
        frozen_params_ = params;
        params_frozen_ = true;
    }

    // 读取已冻结的参数 (OOS/Frozen 阶段用)
    [[nodiscard]] ParamSet const& get_frozen_params() const noexcept {
        return frozen_params_;
    }

    [[nodiscard]] bool has_frozen_params() const noexcept {
        return params_frozen_;
    }

    [[nodiscard]] std::int64_t window_start_ns() const noexcept { return window_start_ns_; }
    [[nodiscard]] std::int64_t window_end_ns()   const noexcept { return window_end_ns_;   }

private:
    std::int64_t window_start_ns_{0};
    std::int64_t window_end_ns_{0};
    ParamSet     frozen_params_{};
    bool         params_frozen_{false};
};

// ---------------------------------------------------------------------------
// 4. PurgedKFold — 按 game_id 分组 + embargo 防泄漏 (§3.2)
// ---------------------------------------------------------------------------
//
// 设计:
//   1. 按 game_id 分组, 同一场比赛所有 tick 只能进同一个 fold
//   2. embargo: 同 game_id 的最晚 as_of_ts 往后 embargo_duration_ns 内的 trade 也排除
//
struct FoldSplit {
    std::vector<TradeRecord> train;      // IS fold (本 fold 用于训练)
    std::vector<TradeRecord> val;        // validation fold (本 fold 用于 inner CV)
    std::uint32_t            fold_index{0};
};

class PurgedKFold {
public:
    explicit PurgedKFold(std::uint32_t n_folds,
                         std::int64_t embargo_duration_ns = WalkForwardConfig::k7DaysNs)
        : n_folds_(n_folds), embargo_ns_(embargo_duration_ns) {}

    // 生成 n_folds 个 FoldSplit (按 game_id 分组)
    [[nodiscard]] std::vector<FoldSplit> split(
        std::vector<TradeRecord> const& trades) const {

        if (trades.empty() || n_folds_ == 0) {
            return {};
        }

        // 收集所有唯一 game_id
        std::vector<std::string> game_ids;
        {
            std::unordered_set<std::string> seen;
            for (auto const& t : trades) {
                if (seen.insert(t.game_id).second) {
                    game_ids.push_back(t.game_id);
                }
            }
        }

        // 按 game_id 分配 fold (轮询分配)
        std::unordered_map<std::string, std::uint32_t> game_to_fold;
        {
            std::uint32_t i = 0;
            for (auto const& gid : game_ids) {
                game_to_fold[gid] = i % n_folds_;
                ++i;
            }
        }

        // 对每个 fold 建 train/val
        std::vector<FoldSplit> result;
        result.resize(n_folds_);
        for (std::uint32_t k = 0; k < n_folds_; ++k) {
            result[k].fold_index = k;
        }

        // 对每个 fold: val = 本 fold game_id 的 trade, train = 其他 fold 且不在 embargo 内
        for (std::uint32_t k = 0; k < n_folds_; ++k) {
            // 先收集 val fold 内所有 as_of_ts 的范围 [val_start, val_end]
            std::int64_t val_max_ts = std::numeric_limits<std::int64_t>::min();
            std::int64_t val_min_ts = std::numeric_limits<std::int64_t>::max();

            for (auto const& t : trades) {
                auto it = game_to_fold.find(t.game_id);
                if (it != game_to_fold.end() && it->second == k) {
                    result[k].val.push_back(t);
                    if (t.as_of_ts_ns > val_max_ts) val_max_ts = t.as_of_ts_ns;
                    if (t.as_of_ts_ns < val_min_ts) val_min_ts = t.as_of_ts_ns;
                }
            }

            // train: 非本 fold, 且 as_of_ts 不在 embargo 窗口内
            // embargo: [val_min_ts - embargo_ns, val_max_ts + embargo_ns]
            std::int64_t const embargo_lo = val_min_ts - embargo_ns_;
            std::int64_t const embargo_hi = val_max_ts + embargo_ns_;

            for (auto const& t : trades) {
                auto it = game_to_fold.find(t.game_id);
                if (it == game_to_fold.end() || it->second == k) {
                    continue;  // val fold 本身跳过
                }
                // embargo 检查
                if (t.as_of_ts_ns >= embargo_lo && t.as_of_ts_ns <= embargo_hi) {
                    continue;  // 在 embargo 内, 不进 train
                }
                result[k].train.push_back(t);
            }
        }

        return result;
    }

private:
    std::uint32_t n_folds_{5};
    std::int64_t  embargo_ns_{WalkForwardConfig::k7DaysNs};
};

}  // namespace stcpp::backtest
