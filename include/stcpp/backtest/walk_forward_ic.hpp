// include/stcpp/backtest/walk_forward_ic.hpp — book-only walk-forward IC 评估 (小蒋 P0)
//
// Owner: 老雷 (GM) — Phase 2 联合评审 (小蒋: book-only Tier-1 walk-forward, IC 是地基指标)
// last_review: 2026-05-31
//
// 职责: 给定带标签的样本 (as_of_ts + 预测量 + label), 按 rolling walk-forward window (purged+embargo)
//   切 OOS, 每折算 Spearman rank-IC (预测量 vs label), 聚合成 IC 均值 + IC-IR (跨折稳定性)。
//   "预测量" = baseline fair / 任一特征 / 模型输出 —— 评估它 OOS 是否真预测结算结果。
//   纯函数 header-only; 复用 WalkForwardSplitter (purged k-fold + embargo) + stats::rank_ic。
//   红线: 离线评估, 不回喂决策; IC 用结算 label (offline), 非前视特征 (CLV 红线一致)。
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "stcpp/backtest/stats.hpp"         // rank_ic
#include "stcpp/backtest/walk_forward.hpp"  // WalkForwardSplitter / WalkForwardConfig

namespace stcpp::backtest {

// 一个带标签样本 (一行特征快照 + 其 condition 的结算 label)。
struct LabeledSample {
    std::int64_t as_of_ts_ns{0};  // 采样时刻 (落 walk-forward window 用)
    double predictor{0.0};        // 评估的预测量 (baseline fair / 特征 / 模型输出)
    double label{0.0};            // 结算 outcome (1=YES赢/0=NO赢)
};

struct FoldIC {
    std::uint32_t fold_index{0};
    double oos_ic{std::numeric_limits<double>::quiet_NaN()};
    std::size_t oos_n{0};
};

struct WalkForwardICReport {
    std::vector<FoldIC> folds;
    double mean_ic{std::numeric_limits<double>::quiet_NaN()};  // 跨折 IC 均值
    double ic_ir{std::numeric_limits<double>::quiet_NaN()};    // IC 信息比 = mean/std (跨折稳定性)
    std::size_t valid_folds{0};
    std::size_t total_oos{0};
};

// evaluate_walk_forward_ic — 主入口。
//   samples: 全量带标签样本 (任意时序; 内部按 as_of join 到各折 OOS)。
//   cfg: walk-forward 切分 (train/embargo/oos duration + step + max_folds)。
//   data_start/end: 数据时间范围 (生成 window 用)。
[[nodiscard]] inline WalkForwardICReport evaluate_walk_forward_ic(
    std::vector<LabeledSample> const& samples, WalkForwardConfig const& cfg, std::int64_t data_start_ns,
    std::int64_t data_end_ns) {
    WalkForwardICReport rep;
    WalkForwardSplitter splitter(cfg);
    const auto windows = splitter.generate(data_start_ns, data_end_ns);

    std::vector<double> fold_ics;
    for (auto const& w : windows) {
        std::vector<double> pred, lab;
        for (auto const& s : samples) {
            if (s.as_of_ts_ns >= w.oos_start_ns && s.as_of_ts_ns < w.oos_end_ns) {
                pred.push_back(s.predictor);
                lab.push_back(s.label);
            }
        }
        FoldIC f;
        f.fold_index = w.fold_index;
        f.oos_n = pred.size();
        f.oos_ic = stats::rank_ic(pred, lab);  // <2 或常数 → NaN
        rep.folds.push_back(f);
        rep.total_oos += f.oos_n;
        if (std::isfinite(f.oos_ic)) fold_ics.push_back(f.oos_ic);
    }

    rep.valid_folds = fold_ics.size();
    if (!fold_ics.empty()) {
        double sum = 0.0;
        for (double x : fold_ics) sum += x;
        rep.mean_ic = sum / static_cast<double>(fold_ics.size());
        if (fold_ics.size() >= 2) {
            double var = 0.0;
            for (double x : fold_ics) var += (x - rep.mean_ic) * (x - rep.mean_ic);
            var /= static_cast<double>(fold_ics.size() - 1);
            const double sd = std::sqrt(var);
            rep.ic_ir = (sd > 0.0) ? (rep.mean_ic / sd) : std::numeric_limits<double>::quiet_NaN();
        }
    }
    return rep;
}

}  // namespace stcpp::backtest
