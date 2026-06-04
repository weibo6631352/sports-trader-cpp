// include/stcpp/app/event_matcher.hpp — condition_id ↔ Goalserve event 映射桥 (A0)
//
// Owner: 老雷 (GM) — M1 路线评审会决议 A0 (docs/MEETINGS/2026-05-30-m1-route-review.md)
// last_review: 2026-05-30
//
// 归属: app 编排层 (stcpp_paper_app 库). 消费 EventScore (debug_api) + market 队名/kickoff,
//   app → debug_api 单向 (老周架构裁定 B1)。
//
// 职责: 把 Polymarket gamma market (两队名 outcomes + gameStartTime) 锚定到 Goalserve
//   in-play event (EventScore: home/away/kickoff/league)。无公共 ID (小段口径报告:
//   Goalserve inplay id 134xxx 与 Polymarket condition_id 无对照表), 只能语义锚定:
//   归一化队名 overlap + kickoff 时间窗口。
//
// **fail-closed (红线):** 匹配不上 → matched=false (上游退回 has_real_fair=false, 不产 intent)。
//   错配下单 = 张冠李戴比分 → 假 fair → 真亏 (老周风险点)。宁可不匹配, 绝不猜。
//
// 算法 (小段匹配策略):
//   1. 队名归一化: lowercase + alnum tokenize → token 集合。
//   2. 队相似度: overlap coefficient = |A∩B| / min(|A|,|B|) (比 Jaccard 宽容名长差异,
//      如 "LA Lakers" vs "Los Angeles Lakers" = 0.5)。
//   3. 双向分配: market{t0,t1} 对 event{home,away} 取 max(直配, 交叉配)。
//   4. 合格条件: 双队各自 overlap ≥ team_sim_threshold (默认 0.5) **且** (两侧 kickoff 均已知时)
//      时间差 ≤ kickoff_window_sec (默认 ±15min)。
//   5. 多候选取 team_score 最高。无合格 → fail-closed。
//
// 已知限制 (小段): 队名缩写完全不同 (Man Utd vs Manchester United) → overlap=0 → 不匹配
//   (fail-closed, 该盘不交易); 网球运动员名格式差异大 (匹配率 ~70%)。M1 可接受。

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "src/stcpp/debug_api/state_provider.hpp"  // EventScore

namespace stcpp::app {

// market 侧锚定输入 (与 DiscoveredMarket 解耦, 便于单测)
struct EventMatchInput {
    std::string team0;               // market outcome0 (moneyline 队名)
    std::string team1;               // market outcome1
    std::int64_t kickoff_ts_sec{0};  // gameStartTime Unix 秒; 0 = 未知 (跳过时间窗口检查)
    std::string sport;               // 可选提示 (当前未强制用)
    // 3-way 足球: YES 代表「平局」盘 (gi="Draw(...)") → 定价用 inplay_bet365_draw_fair 而非 home/away。
    //   仅锚定 (取分源) 不受影响; 影响下游 sharp fair 选哪一边 (老雷 2026-06-01 盈利修复)。
    bool is_draw{false};
    // A-step-2 分局盘 (2026-06-04 老板「第一局/第二局」): 此盘 segment 序号 (tennis 盘号 1-5; 0=全场盘)。
    //   仅锚定 (取分源) 不受影响; 透传到 EventMapEntry.seg_index, 下游 paper_loop 用当前段 fair。
    int seg_index{0};
};

struct EventMatchResult {
    bool matched{false};
    std::string inplay_match_id;  // = EventScore.event_id (Goalserve inplay id)
    double team_score{0.0};       // 双队 overlap 之和 (诊断/择优用)
    // orientation (正确性命门, 老周张冠李戴): market YES token(=team0) 对应 EventScore 的 home 还是 away.
    //   true  = 直配 (team0→home): YES 队即 home. FairValue score_diff=home-away 直接成立.
    //   false = 交叉 (team0→away): YES 队即 away. 消费侧须把 away_score 填进 score_home_total
    //           (令 score_diff = YES队 - 对手), 否则 prior_yes 方向反 → 下错单.
    bool yes_is_home{true};
};

class EventMatcher {
public:
    struct Config {
        double team_sim_threshold{0.50};       // 每队 overlap 系数下界 (fail-closed: 偏精度)
        std::int64_t kickoff_window_sec{900};  // kickoff 容差 ±15min
        // P2-1 (老郭): 直配/交叉两分配都过门且分差 < 此值 → orientation 模糊 → fail-closed.
        //   防比分方向接反 (镜像 fair → 反向下单). 默认 0.10.
        double orientation_margin{0.10};
    };

    EventMatcher() noexcept = default;
    explicit EventMatcher(Config cfg) noexcept : cfg_(cfg) {}

    // 从候选 EventScore 列表找最佳匹配. fail-closed: 无合格返 matched=false.
    [[nodiscard]] EventMatchResult Match(const EventMatchInput& in,
                                         const std::vector<debug_api::EventScore>& candidates) const;

    // ---- 纯 helper (暴露供单测) ----
    // 队名归一化: lowercase + alnum token 集合 (去重, 排序).
    [[nodiscard]] static std::vector<std::string> NormalizeTeamTokens(const std::string& name);

    // overlap 系数: |A∩B| / min(|A|,|B|). 任一空 → 0.
    [[nodiscard]] static double TeamSimilarity(const std::string& a, const std::string& b);

    // ---- 覆盖率诊断 (2026-06-03, 老板「覆盖率低是不是名字不匹配」) ----
    // 只统计【名字过了 threshold 却被后续门拒掉】的 market = 可恢复缺口 + 凶手门。
    //   threshold 拒绝(错选手)不计 — 那是正常的, 会淹没信号。
    struct Diag {
        std::int64_t calls{0};                   // Match 调用总数 (= 跑匹配的 market 数)
        std::int64_t matched{0};                 // 成功匹配
        std::int64_t namematch_found{0};          // ≥1 候选过了 threshold (名字配上了)
        std::int64_t namematch_but_unmatched{0};  // 名字配上却最终没匹配 (被门拒) ← 核心
        std::int64_t rej_orientation{0};          // (仅未匹配 market) 名字候选被 orientation 门拒次数
        std::int64_t rej_kickoff{0};              // (仅未匹配 market) 名字候选被 kickoff 门拒次数
    };
    [[nodiscard]] Diag DiagSnapshot() const noexcept {
        return {diag_calls_.load(std::memory_order_relaxed),
                diag_matched_.load(std::memory_order_relaxed),
                diag_namematch_found_.load(std::memory_order_relaxed),
                diag_namematch_but_unmatched_.load(std::memory_order_relaxed),
                diag_rej_orientation_.load(std::memory_order_relaxed),
                diag_rej_kickoff_.load(std::memory_order_relaxed)};
    }
    void DiagReset() const noexcept {
        diag_calls_.store(0, std::memory_order_relaxed);
        diag_matched_.store(0, std::memory_order_relaxed);
        diag_namematch_found_.store(0, std::memory_order_relaxed);
        diag_namematch_but_unmatched_.store(0, std::memory_order_relaxed);
        diag_rej_orientation_.store(0, std::memory_order_relaxed);
        diag_rej_kickoff_.store(0, std::memory_order_relaxed);
    }

private:
    Config cfg_{};

    // 诊断计数器 (mutable: Match 是 const; relaxed atomic, 非热路径精度无碍)
    mutable std::atomic<std::int64_t> diag_calls_{0};
    mutable std::atomic<std::int64_t> diag_matched_{0};
    mutable std::atomic<std::int64_t> diag_namematch_found_{0};
    mutable std::atomic<std::int64_t> diag_namematch_but_unmatched_{0};
    mutable std::atomic<std::int64_t> diag_rej_orientation_{0};
    mutable std::atomic<std::int64_t> diag_rej_kickoff_{0};
};

}  // namespace stcpp::app
