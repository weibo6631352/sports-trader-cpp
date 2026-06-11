// include/stcpp/paper/paper_loop.hpp — PaperLoop: 最小 paper 交易循环
//
// Owner: 小肖 (numerical-algorithms, A 系统工程部)
// last_review: 2026-05-30
//
// 设计目标 (M1 路径, 参见 xiaoxiao-paper-runtime-integration-design-v1.md):
//   消费真实 live book (OrderBookSnapshotHub) → 生成 paper 成交 → 喂 LedgerSnapshotHub
//   + QuoteSnapshotHub → RealStateProvider.positions/pnl_attribution 返回真实 paper 数据
//
// 架构 (简化 vCPU 模型 — 单线程顺序执行, debug_server live 路径):
//   每 tick_interval_ms 唤醒一次:
//     1. 遍历 token_map: hub_.Read(token_id) 取真实 book
//     2. FairValueEstimator::estimate → fair_value / p_yes
//     3. SizingCalculator::compute → suggested_notional / kelly_fraction
//     4. QuoteSnapshotHub::Publish (quote 快照; P0-3: 无真实 fair 时清零 edge/kelly/notional)
//     4b. [P0-4 gate] advisory_markets_no_intent=true → return (不产生 intent)
//     4c. [P0-3 gate] has_real_fair=false → return (stub fair, 不产生 intent)
//     5. 构造 OrderIntent v0.6 (4 ts / condition_id / token_id / side / price / size_pUSD_micro)
//     6. RiskGateway::evaluate → Decision
//     7. APPROVED: PaperSigner::Sign → VirtualMatcher::MatchWithBook → PositionLedger::apply_fill
//     8. apply_fill Ok: LedgerSnapshotHub::Publish (positions/pnl 快照)
//     9. REJECTED: 仅计数 (P0-1: RM 内部已 push_reject 一次, paper_loop 不再重复 push)
//
// P0 整改 (2026-05-30, 小肖, dogfood-remediation):
//   P0-1 拒单去重: RM::evaluate() 内 reject_here() lambda 已通过 g_rm_debug_snapshot
//         全局指针调用 push_reject 一次. paper_loop 删除冗余的 rm_snap_->push_reject.
//         验证: /api/v1/risk/rejects count 从 256→128 (唯一 128 不再翻倍).
//   P0-3 fake fair gate: has_real_fair=false (time_status==NotStarted, M1 stub) 时
//         PublishQuoteSnapshot 清零 edge_bps/kelly/suggested_notional/signal/predict_ok.
//         TickOne 在 Step 4c 拦截, 不构造 intent. 宁可空不可假.
//   P0-4 advisory gate: cfg_.advisory_markets_no_intent=true (M1 默认) 时, Step 4b
//         直接 return, advisory 市场不进 RM, 不产生 intent.
//
// 红线守法:
//   R-11: paper 不污染真账本
//     - VirtualFill.mode_tag == 0 (硬填 paper 标记, VirtualMatcher 内部保证)
//     - PositionLedger 独立实例 (由调用方构建, 与 live 路径隔离)
//     - PaperSigner.audit_wal_kind = PaperAudit (signer 内部保证)
//     - PaperLoop 仅产出快照 (LedgerSnapshotHub / QuoteSnapshotHub), 不写真账本
//   R-12: PaperLoop 独立线程 (std::jthread), 不进 WSS event loop
//     - hub_.Read() 原子只读, 不阻塞 WSS io_thread_
//     - LedgerSnapshotHub::Publish / QuoteSnapshotHub::Publish 均 noexcept
//     - 线程 sleep_for tick_interval_ms, 不 spinlock
//   R-20: 4 时间戳全链路透传
//     - data_source_ts_ns 来自 OrderBookFeatures (hub 快照, 禁本地 now() 替代)
//     - event_ts_ns <= data_source_ts_ns <= ingestion_ts_ns <= as_of_ts_ns 单调链
//     - as_of_ts_ns = NowNs() (信号评估时刻, >= ingestion_ts_ns)
//     - LedgerFeatures / QuoteFeatures 4 ts 来自 fill/feat 链路透传
//
// 注意 (ToS): PaperLoop 仅产生 paper 虚拟成交 (VirtualFill), 不向 Polymarket CLOB 下单.
//   所有 OrderIntent 均经 PaperSigner (mock, 不上链), 不调用 live REST API.
//
// 线程安全:
//   - Start() / Stop() 由主线程调用 (非热路径)
//   - 内部 loop_thread_ 为 std::jthread (Stop() join 等待完成)
//   - hub_ / ledger_hub_ / quote_hub_ 均为 SWMR 快照接口 (线程安全)
//   - position_ledger_ 写端仅 loop_thread_ (单 writer, 符合 PositionLedger 设计)

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "stcpp/data/score_snapshot_store.hpp"  // A4: ScoreMap (tick-local 共享比分快照, 消 read-skew)
#include "stcpp/data/live_stats_store.hpp"      // live_stats 采集 hop: LiveStatsMap/LiveStatsFields join
#include "stcpp/data/odds_snapshot_store.hpp"   // bm_slots: OddsMap (inplay_match_id→跨庄家赔率)
#include "stcpp/data/bm_slots_fill.hpp"         // bm_slots: FillBmSlotsYesCanonical (定向 de-vig 填充)
#include "stcpp/eval/clv_tracker.hpp"            // CLV 测量 harness (结算口径, 离线评估 only)
#include "stcpp/eval/rolling_clv.hpp"            // 实时 CLV (PIT-safe) 滚动环 — sizing 用 (Stage2)
#include "stcpp/eval/portfolio_metrics.hpp"      // Phase 0 项5: Sharpe/maxDD/VaR (北极星 KPI)
#include "stcpp/ml/feature_history.hpp"          // 时序特征环形缓冲 (PIT-safe, BR-1 共用)
#include "stcpp/ml/sharp_fair_track.hpp"         // sharp fair 时序环 (line movement; velocity/收敛发散) — 量化因子
#include "stcpp/ml/game_score_history.hpp"       // 比分时序 (进球新鲜度/动量) — 量化因子
// (大模型/训练 includes 已砍 2026-06-05: fair_value_model / seq_arb_model / hot_swap_model /
//  feature_vector_hub / model_feature_spec。保留上面两个纯统计因子环。)
#include "stcpp/execution/order_executor.hpp"
#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/paper/binary_market_snapshot.hpp"  // 二元双边决策入参 (老周架构)
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/pricing/fair_value_estimator.hpp"
#include "stcpp/risk/ledger_snapshot_hub.hpp"
#include "stcpp/risk/position_ledger.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/signer/paper/paper_signer.hpp"
#include "stcpp/sizing/quote_snapshot_hub.hpp"
#include "stcpp/sizing/sizing_calculator.hpp"

// A1: ScoreSnapshotStore 前向声明 (实体在 stcpp_data_score_store; .cpp 内 include).
//   PaperLoop 仅持 const 指针 + 调 Get(), 头文件不拉 score_store 重型依赖.
namespace stcpp::data {
class ScoreSnapshotStore;
}  // namespace stcpp::data

// slice-3b: FeatureStoreGameRow 前向声明 (结算方法按 const& 取终态比分; 实体在 .cpp include)。
namespace stcpp::data::feature_store {
struct FeatureStoreGameRow;
}  // namespace stcpp::data::feature_store

namespace stcpp::paper {

// ---------------------------------------------------------------------------
// FrozenHardStopTriggered (2026-06-10 老韩 bug#2 + 老板「下行不够细致 / 两边都要考虑」)
//   纯判定: sharp 掉档冻结态下的【双边簿确认真崩盘】灾难止损。调用方保证仅在 fair_is_sharp==false 时进入,
//   故与 2026-06-09 −5.80 退化簿(sharp 仍有效)签名互斥, 不回归卖飞。本函数只判:
//     ① mark 深跌破均入 ×(1−stop_pct)  ② 双边簿存在且 ask>bid>0  ③ 价差 ≤ max_spread (簿紧 = 真崩盘非单边退化塌)。
//   退化簿是单边 bid 塌→价差极宽→被 ③ 拒; 真崩盘双边齐跌→价差窄→放行截尾。纯函数, 单测覆盖。
// ---------------------------------------------------------------------------
inline bool FrozenHardStopTriggered(double avg_entry, double mark, double best_bid,
                                    double best_ask, double stop_pct, double max_spread) {
    if (!(stop_pct > 0.0) || !(avg_entry > 0.0)) return false;
    if (!std::isfinite(mark) || !std::isfinite(best_bid) || !std::isfinite(best_ask)) return false;
    if (!(best_bid > 0.0) || !(best_ask > best_bid)) return false;            // 双边簿存在
    if (!(mark < avg_entry * (1.0 - stop_pct))) return false;                 // 深跌
    return (best_ask - best_bid) <= max_spread;                              // 簿紧 = 双边确认真崩盘
}

// ---------------------------------------------------------------------------
// BookDeteriorating (2026-06-10 老板「止盈不要了 + 只要订单簿先恶化就割肉」) —— 统一离场唯一触发。
//   本边订单簿恶化 = L1 失衡 < −imb_thr (卖压: bid_sz 远少于 ask_sz) 且 microprice < mid (方向向下)。
//   订单簿是 PM 实时流的领先信号 —— 恶化即离场(割/锁); 簿稳则持有骑到底(无止盈/无 mark 止损/无 velocity 离场)。
//   −5.80 退化簿卖飞由执行层 bid_not_degenerate(锚 fair) 防护, 不在此判。纯函数, 单测覆盖。
// ---------------------------------------------------------------------------
inline bool BookDeteriorating(double imb, double microprice, double mid, double imb_thr) {
    if (!(imb_thr > 0.0)) return false;  // 门关 (lib 默认 0) → 永不恶化判定
    if (!std::isfinite(microprice) || !std::isfinite(mid)) return false;
    return (imb < -imb_thr) && (microprice < mid);  // 卖压失衡 + 方向向下 = 恶化
}

// (RelStopShouldHoldWinner 2026-06-12 治理删: rel_stop/赢面门机器随 hold-to-settlement 整体下线。)

// ---------------------------------------------------------------------------
// EventMapEntry / ConditionEventMap (A1 映射桥消费侧契约)
//   condition_id → {Goalserve inplay_match_id, orientation}.
//   由 app 层 (PaperDaemon + EventMatcher) 解析后经 SetEventMapping() 注入 (atomic 热刷).
//   yes_is_home: market YES token 对应 EventScore 的 home(true)/away(false). 见 EventMatcher 注释.
// ---------------------------------------------------------------------------
struct EventMapEntry {
    std::string inplay_match_id;
    bool yes_is_home{true};
    // 3-way 足球: 此盘 YES=平局 → 定价 sharp fair 取 inplay_bet365_draw_fair (非 home/away)。
    bool is_draw{false};
    // A5 (小余 round-2): join 边是概率性 fuzzy 匹配, 会断会翻转 → 一等暴露质量 (观测/模型输入, 不 gate)。
    double match_confidence{0.0};    // EventMatcher team_score (双队 overlap 和; 越高越确信)
    std::int64_t match_as_of_ns{0};  // 映射上次刷新时刻 (本地 now; 数据新鲜度观测, 绝不守门)
    // A-step-2 分局盘 (2026-06-04 老板「第一局/第二局」): 此盘的 segment 序号 (tennis 当前盘号 1-5;
    //   0 = 全场盘)。>0 时 paper_loop 用 EventScore.inplay_seg_* (且 seg_index==当前段) 替全场 sharp fair;
    //   段号不符/无段赔率 → fail-closed 无 fair (绝不回退全场, 修 A-step-1 之前事故)。
    int seg_index{0};
};
using ConditionEventMap = std::unordered_map<std::string, EventMapEntry>;

// 统一数据树: 盘口 → 父级引用 (event_id / neg_risk_market_id)。app 层从 catalog 注入。
//   兄弟盘口不内嵌 (变长; 经 event_id 在目录导航)。
struct ParentRef {
    std::string event_id;
    std::string neg_risk_market_id;
};

// v0.7 类别上下文注入 (真实 Polymarket 市场结构 → categorical 码; app 层 market_taxonomy.hpp 算一次,
//   per-condition 注入, 同 fee/parent 注入范式)。喂 ML 特征 82-85 (资产/运动家族/盘口/联赛)。
//   联赛 = Polymarket sport.id (nba=34/bkcba=104), 天然区分 NBA vs CBA。unknown=-1。
struct MarketCat {
    std::int32_t asset_class_id{0};    // 0=Sports/1=Crypto/2=Politics/3=Esports
    std::int32_t sport_family_id{-1};  // 粗家族 soccer=0/basket=1/tennis=2/...
    std::int32_t league_id{-1};        // 细联赛 = Polymarket sport.id
    std::int32_t market_type_id{-1};   // moneyline=0/spread=1/totals=2/outright=3/prop=4/series=5
    double line{std::numeric_limits<double>::quiet_NaN()};  // totals/spreads 线值 (派生定价输入)
    bool yes_is_over{true};  // totals 方向: YES(token0) 是否=Over (outcomes[0]=="Over"); 否则 YES=Under
    double volume_24h{std::numeric_limits<double>::quiet_NaN()};  // 24h 成交量 (gamma; 市场活跃度→Kelly)
    double liquidity{std::numeric_limits<double>::quiet_NaN()};   // book 流动性 (gamma; 滑点代理)
};

// R-3 (老周/老郭 评审 2026-06-01): 统一 per-condition 静态元数据为一个 entry, 走单一 RCU 快照。
//   原 4 张并行 map (token/fee/cat/parent) 各自注入 → 周期重发现要原子换才不会 swap 间隙读到半新半旧
//   (TickOne 读 token 有新 cond 但 cat 还没 → 定价分派错)。打包成一个 entry 一次 swap, 消同步问题。
//   边界 (老周铁律): 只装【静态元数据】(发现期定、重发现才换); 动态运行期态 (score/resolution/
//   live_stats/event_map) 各自 RCU 高频换, 绝不并入 — 否则换 catalog 被迫连带换动态态, 反制造 read-skew。
struct PaperMarketEntry {
    std::pair<std::string, std::string> tokens;  // YES, NO token_id
    double fee_coef{0.03};                        // = kDefaultFeeCoef (gamma feeSchedule.rate)
    MarketCat cat;                                // 类别码 + line + 活跃度
    ParentRef parent;                             // event_id / neg_risk
    // 数据新鲜度 (2026-06-01 老板「每个源标时间」): gamma 发现/重建此 catalog 条目的时刻
    //   (300s 重发现 → 此源可达 300s 陈旧)。喂 g_catalog_age_sec, 模型知道 fee/line/类别元数据多老。0=未知。
    std::int64_t discovered_at_ns{0};
    // 比赛窗口 (2026-06-11 FLB 触发型: in-play 判定; 加性): gamma gameStartTime/endDate → Unix 秒, 0=缺。
    std::int64_t game_start_ts_sec{0};
    std::int64_t end_ts_sec{0};
};
using PaperCatalog = std::unordered_map<std::string, PaperMarketEntry>;

// slice-3c 结算注入 (老板 2026-05-31 摸底定论: Polymarket resolution **不在 market WSS**,
//   是 REST 字段 — gamma `closed` + clob `tokens[i].winner`)。app 层轮询 REST → SetResolutionByCondition
//   注入 (同 fee/event-mapping 注入范式), 非 WSS 解析 (market 频道只推 book/price/trade/tick)。
//   status: 0=Open / 1=Resolving / 2=Resolved (= wss::ResolutionStatus 同枚举值, 喂模型特征)。
//   winner: −1=未知 / 0=NO 赢 / 1=YES 赢 (Resolved 时填; 3b 权威结算 winner, 全 market type 通用)。
struct ResolutionEntry {
    std::uint8_t status{0};
    std::int8_t winner{-1};
    // 数据新鲜度 (2026-06-01 老板「每个源标时间, 模型学权重」): SettlementPoller 拉到此 resolution 的时刻
    //   (60s 轮询 → 此源可达 60s 陈旧)。喂 g_resolution_age_sec 特征, 让模型知道结算信息多老。0=未知。
    std::int64_t fetched_at_ns{0};
};

// 批1 体育动态特征 (TickOne 算好, 一struct 传 PublishQuoteSnapshot, 避免 param 爆炸)。
//   全部 game_row.score / live_stats 派生; 无真比分 → NaN。
struct SportsFeatures {
    double game_phase{std::numeric_limits<double>::quiet_NaN()};
    // 默认 NaN (与 game_phase/goal_freshness 一致, 2026-06-02 10-代理审计发现): 非赛中记录
    //   应输出 NaN("无比赛数据")而非 0.0(会被模型误读为"赛中但非垃圾/关键时刻")。
    //   赛中匹配记录在 has_real_fair 块内总会显式赋 0.0/1.0, 不受此默认影响。
    double garbage_time{std::numeric_limits<double>::quiet_NaN()};
    double clutch{std::numeric_limits<double>::quiet_NaN()};
    double goal_freshness{std::numeric_limits<double>::quiet_NaN()};
    double net_momentum_5m{std::numeric_limits<double>::quiet_NaN()};
    double danger_attack_diff{std::numeric_limits<double>::quiet_NaN()};
    double shot_on_target_diff{std::numeric_limits<double>::quiet_NaN()};
    double possession_home{std::numeric_limits<double>::quiet_NaN()};
    double red_card_diff{std::numeric_limits<double>::quiet_NaN()};
    double corner_diff{std::numeric_limits<double>::quiet_NaN()};
    double bm_inplay_fair{std::numeric_limits<double>::quiet_NaN()};  // inplay bet365 de-vig fair
};

// 候选 fair 全集快照 (观测: 显示所有源 + 标记正在用的, 而非只露决出值)。
//   TickOne 从 ResolveFair 的 FairInputs 捕获 (含 market_implied 屏蔽), 传 PublishQuoteSnapshot → qf。
//   -1 = 该源对此盘不适用 (无 sharp odds / 非衍生盘 / 无真比分)。
struct FairCandidates {
    double market_devig{-1.0};
    double sharp{-1.0};
    double score_prior{-1.0};
    double derivative{-1.0};
};

// ---------------------------------------------------------------------------
// PaperLoopConfig — 运行参数
// ---------------------------------------------------------------------------
struct PaperLoopConfig {
    // 每次 tick 间隔 (ms). 默认 500ms 适合 debug 观测.
    std::int64_t tick_interval_ms{500};

    // bankroll (pUSD). 用于 SizingCalculator.
    double bankroll_usdc{100'000.0};

    // 风控 caps (pUSD, 单一真值源). P0-2 单位统一 (老雷 2026-05-30, 拆 clamp 遮羞布):
    //   sizing 直接用 (pUSD); paper_daemon 装配 RM 时 × 1e6 转 micro (RM 比 size_pUSD_micro)。
    //   消除原「sizing 用 RiskConfig{} 默认 10K pUSD vs RM 10 pUSD(micro)」1000x 失配 +
    //   paper_loop `min(notional, 10.0)` clamp 遮羞布 (失配被它摁住没爆, 非真修复)。
    //   两端同源同语义 → 无需 clamp: sizing 自然受 per_order_cap 约束, ×1e6 后必 ≤ RM cap。
    double per_order_cap_usdc{10.0};
    double market_exposure_cap_usdc{50.0};
    double per_outcome_cap_usdc{25.0};
    double min_order_pusd{0.0};       // 最小买单门 (老板 2026-06-09「体育 min 5 单」; 0=关): 买单 < 它跳过

    // ---- 目标仓位控制器参数 (老雷 controller spec v1 §11 Step 3; 小梁 Q-梁-1/Q-梁-2) ----
    //   edge_ci_lower_floor: reservation required_margin 下限 (与 RM 同名门同源; 默认 0)。
    //     required_margin = max(edge_ci_lower_floor, z_90×sqrt(p(1−p)/n_eff))。
    //   min_rebalance_floor_pusd: 防抖死区绝对下限; 实际 threshold = max(floor, 0.10×|target|)。
    //     |gap| < threshold 不动 (避免高频小额 rebalance 被 fee 侵蚀)。
    double edge_ci_lower_floor{0.0};
    double min_rebalance_floor_pusd{1.0};
    //   force_cross_fair_delta: |fair_new − fair_old| 超此值 → 强制穿越死区 (小梁 Q-梁-2;
    //     比分大跳/进球令 fair 突变时不被防抖死区堵住)。YES-canonical p_fair 逐 condition 比较。
    double force_cross_fair_delta{0.02};

    //   ts_feature_window_ns: 时序特征回看窗口 (老板 2026-05-31; 微价变化率/realized vol)。
    //     默认 30s (book ~1-5s/更新 → 窗口内 ~6-30 样本)。PIT: [as_of−W, as_of] 只看过去。
    std::int64_t ts_feature_window_ns{30'000'000'000LL};

    // CI 参数 (z=1.645 = 90%)。
    // n_effective: 2026-05-31 30→200 (小程量化方案, Phase4 阻塞1)。
    //   n=30 时 CI half-width≈15¢@p=0.5 → 6-14¢ 正常 edge 全被 NO_EDGE 误杀 (系统零成交根因)。
    //   n=200 → half-width≈5.8¢, 6¢ edge 的 ci_lower≈+0.002 可放行; 与回测 ParamSet.n_effective 对齐。
    //   过渡值: 原则值应 = books_used × stability(需 books_used 透传, 后续); 待小梁终签。
    int n_effective{200};
    double z_90{1.645};

    // ---- Phase 0 联合评审 (2026-05-31, 6 团队): 动态 n_eff + margin + net-EV 门 + goal force_cross ----
    //   项1+2: 动态 reservation (n_eff = clamp(min(YES,NO 样本), min, max) + margin 接半 vig/amihud)。
    //     默认 false (lib 向后兼容: 现有契约测试用静态 n=200/static floor); daemon 生产置 true。
    //     数值小肖: samples=1 时 sigma=0.5 reservation 永不成交 → 必配 n_eff_min 下限防静默失效。
    bool dynamic_reservation{false};
    int n_eff_min{10};
    int n_eff_max{500};
    //     vig_term=0.5×cross_spread 经济地基 (至少赚回付出的半边 vig); amihud_coef 待数据校准默认 0=off。
    double amihud_margin_coef{0.0};
    //   项3 (微观/小梁): SelectSide 后 net-EV 预筛 — edge < 2×fee+slippage 不开新仓 (防 fee 流血)。
    //     默认 false (向后兼容; daemon 生产置 true)。
    bool net_ev_gate{false};
    //   项4 (微观 P1): 进球新鲜度 + OFI → force_cross 绕死区 (打通 inplay 延迟 edge 窗口)。
    double goal_freshness_force_thr{0.6};
    double ofi_force_thr{0.0};  // |OFI|≥此值 (默认 0 = 进球新鲜即触发; force_cross 仅绕死区, 仍受限价门约束)

    // sharp 路径 edge margin (老板 2026-06-03「sharp 纯 net-EV 门」, 见 edge_ci.hpp ResolveEdgeCiLower)。
    //   sharp_inplay 源 (bet365 de-vig 共识点估计) 不扣二项抽样噪声 → edge_ci_lower = raw_edge − 此 margin。
    //   默认 0 = 纯 net-EV (经济 margin 由下游 slippage/fee/net_ev_ok 门承担)。>0 = 额外保守安全带。
    double sharp_edge_margin{0.0};

    // sharp-only 门 (老板 2026-06-03「改成 sharp 驱动」, 42 万结算行回测 ≥5% 偏离站 sharp 77%/+0.20单)。
    //   true: 仅当 fair 源 = sharp_inplay 且 |sharp−市场| ≥ sharp_only_min_edge 才保留 edge;
    //         其余源 (ml_blend/score_prior/derivative/market) 回退市场价 (edge 归零, 不产单)。
    //   false (默认): 不施加此门 —— 通用 fill 管线 (契约/管线单测 + 非 sharp 策略) 照常成交。
    //   纯【策略过滤器】非管线不变量, 故默认关; 生产 daemon (sharp 驱动) 显式置 true。
    bool sharp_only_gate{false};
    double sharp_only_min_edge{0.05};  // ≥此偏离才算高置信 sharp 信号 (回测 ≥5% 拐点)
    // sharp 偏离上界 (2026-06-04 老板「这个差的太多了」): |sharp−市场| > 此值 = 不可信 (快变盘 sharp 滞后
    //   2.3s 造的假 gap / 错配 / de-vig 异常, 不是真 edge — 实测 CS2/网球 26pt/20pt gap 全是 stale-lag)。
    //   超此上界不产单 (回退市场)。1.0 = 关 (lib 默认, 契约测试不变); 生产 daemon 置 0.15。
    double sharp_max_gap{1.0};
    // 赔率源新鲜度门 (2026-06-04 老板「超过3秒的赔率源不进决策」): sharp 来自 inplay 赔率 feed
    //   (Goalserve updated_ts = data_source_ts, R-20)。feed 版本距决策刻 > 此秒数 = 赔率源陈旧
    //   (feed 停更/掉点, 可能已偏离真实) → 不用 sharp 决策, 回退市场。feed 常态 ~2s/版 (data_source_ts
    //   每版重盖), 仅 feed 真停才触发, 不误伤静默盘。0 = 关 (lib 默认); 生产 daemon 置 3.0。
    double sharp_max_staleness_sec{0.0};
    // sharp fair 速度回看窗 (feed ~2s/版 → 10s≈5样本)。velocity 喂方向门/出场判据/观测。
    //   (sharp_lag_adjust 延迟外推 3 件套 2026-06-12 治理删: 字段从未被读, 外推从未实现; git 史可考。)
    std::int64_t sharp_fair_vel_window_ns{10'000'000'000LL};
    // edge-生命周期乘子 (持仓管理 Stage 2, 老板 2026-06-05): sharp 时序状态 (Vol 稳定性 + ConvergenceRate
    //   发散谨慎) 缩 target 【量级】∈[floor,1] (抑制噪声驱动过度交易; 不碰方向/不放大)。见
    //   control::ComputeLifecycleMultiplier。组成 control::LifecycleConfig 传入。
    bool lifecycle_mult_enabled{true};
    double lifecycle_vol_ref{0.02};
    double lifecycle_k_vol{0.5};
    double lifecycle_div_ref{0.01};
    double lifecycle_k_div{0.5};
    double lifecycle_floor{0.3};
    std::int32_t lifecycle_min_samples{3};
    // CLV sizing 乘子 (持仓管理 Stage 2, 老板 2026-06-05「CLV 好就实时放大」): 滚动 CLV 均值调 target
    //   量级, 可 >1 放大 (封顶 max_mult 护栏)。见 control::ComputeClvMultiplier + eval::RollingClv。
    bool clv_mult_enabled{true};
    double clv_ref{0.01};
    double clv_k_amp{0.5};
    double clv_k_cut{1.0};
    double clv_max_mult{1.5};
    double clv_floor{0.3};
    std::int32_t clv_min_samples{20};
    // DD→target 乘子 (持仓管理 Stage 2, 老板「回撤大只停加仓 + hysteresis, 不砍现仓」): 当前回撤分档限制
    //   加仓幅度。见 control::DrawdownTierMultiplier。
    bool dd_mult_enabled{true};
    double dd_t1{0.05};
    double dd_t2{0.10};
    double dd_halt{0.15};
    double dd_m_t1{0.5};
    double dd_m_t2{0.25};
    double dd_hysteresis_band{0.02};
    // (predictive_unwind 旋钮 2026-06-12 治理删: 生产恒 false; hold-to-settlement 后「随预测回 flat」
    //  与「持到结算」直接矛盾。控制器侧能力+单测保留 (ControlInputs.predictive_unwind 默认 false)。)

    // 入场价感知平仓 (2026-06-04 老板「把持仓决策做好, 别稍微亏本就卖, 根本不考虑持仓买卖价格」):
    //   减仓卖单若 bid < 均入价 = 锁亏。仅当本边 fair 跌破均入超此 band (信号真反转 = 该止损) 才放行卖;
    //   否则 HOLD —— 不为 fair 小波动在亏损里夺路卖出 (churn 实现亏损), 等回归/结算。
    //   取利平仓 (bid ≥ 均入) 与盈利减仓不受此限。0 = 关 (旧行为); 生产 daemon 置 0.05 (5 分 band)。
    double loss_cut_fair_band{0.0};

    // 执行层 §4.1 (持仓管理 Stage 2, 老板 2026-06-05; A-S 库存项经 microstructure 裁决 SKIP, 见
    //   position_controller.hpp 注)。全部默认 OFF → 行为逐位等于现状; paper 背书后再开 (§8.1: 写默认关
    //   plumbing 不需会签)。
    //   ① p(1−p) 死区: 死区随往返费率放宽 (费贵处抑制 churn)。见 control::ComputeRebalanceDeadband。
    double deadband_fee_k{0.0};  // 往返费率倍数 (0=关; 开 e.g. 2.0 = 死区 ≥ 2×往返费)
    //   ② 动态 exec_margin: 逆选(毒性 k_tox·|OFI|/depth)+ 波动(k_vol·σ²·τ) 加性边际, 叠进 reservation
    //      required_margin (买侧压低 / 卖侧抬高 → 毒簿/高波动时更被动)。|OFI| 只用幅度 (方向归 sharp)。
    bool exec_margin_enabled{false};
    double exec_margin_k_tox{0.0};       // 毒性强度 (×|OFI|/depth)
    double exec_margin_k_vol{0.0};       // 波动强度 (×RealizedVol²×剩余期限 frac)
    double exec_margin_cap{0.05};        // exec_margin 上限 (prob; 防脏数据把价压穿)
    double exec_margin_depth_floor{50.0};  // depth 下限 (pUSD; 防除以极小 depth 爆炸)
    //   ③ 毒性冻结加仓 (硬档): |OFI|/depth 或 BidAbsence 超阈 → 暂停新增加仓 (减仓照常)。是 exec_margin
    //      (软, 压价) 的硬档配套。见 control::ToxicityFreezesAdds。force_cross (进球/必赢) 绕过。
    bool tox_gate_enabled{false};
    double tox_gate_ofi_depth_thr{0.0};    // |OFI|/depth ≥ 此 → 冻结 (0 = 该判据关)
    double tox_gate_bid_absence_thr{1.0};  // BidAbsence frac ≥ 此 → 冻结 (1.0 = 该判据关; e.g. 0.5)

    // 相关性折扣乘子 (§4.1 规模层, 小梁裁决): 同赛事已有敞口 (扣本盘) ρ 加权占用 → 缩本盘 target 量级。
    //   与 R6.2c 硬 cap 分工: cap=ρ=1 保命墙, 本乘子=ρ 加权提前 taper。见 control::ComputeCorrelationMultiplier。
    //   默认关 (改交易行为 + ρ 表未校准; 硬 cap 已 backstop)。P0 用单一 rho_default; per-type ρ 表待 paper 校准。
    bool corr_mult_enabled{false};
    double corr_taper_start{0.50};
    double corr_floor{0.30};
    double corr_rho_default{0.70};
    double corr_event_cap_pusd{10000.0};  // 须 == RM event_exposure_cap (口径一致, spec Q3); 默认匹配 RM 默认

    // 订单簿结构感知止盈 (2026-06-04 老板「买卖要考虑订单簿结构: 一直涨且能卖出去就持仓, 簿结构转向才止盈」):
    //   盈利减仓 (bid≥均入=取利) 时, 若本边订单簿仍【支撑持仓方向】(L1 失衡未明显翻负 或 microprice≥mid =
    //   上行压力仍在) → HOLD 骑住趋势, 不急于止盈; 仅当簿结构【转向】(失衡 < −book_exit_imb_thr 且
    //   microprice < mid = 卖压起) 才放行止盈卖出。亏损侧由 loss_cut_fair_band 管, 此闸只管盈利侧骑趋势。
    bool book_exit_enabled{false};        // 0/false = 关 (契约测试不变); 生产 daemon 置 true
    double book_exit_imb_thr{0.15};       // L1 失衡跌破 −此值 (且 microprice<mid) 才算簿结构转向

    // (min_buy_price 2026-06-12 治理删: 生产恒 0.0 关, 被 min_open_fair 0.65 + near_end_max_buy_price
    //  双门取代; git 史可考。)

    // 临近末尾必输买入闸 (2026-06-05 老板「临近末尾必输的那种, 还得禁止买入」): 末段 (TickOne near_end:
    //   phase_frac>0.85) 且本边新开仓买入价(exec_ask) < 此价 = 市场把该边定为近必输 (临近末尾+低胜率) →
    //   不开仓 (防买进末段 longshot 被结算归零)。窄闸: 仅【末段 + 便宜】双条件, 非广义 leaning 闸 (已撤)。
    //   用市场实时 exec_ask (非陈旧 sharp) + 分运动 phase 模型双判, 对所有运动鲁棒。减仓/平仓/中前段不受限。
    //   0 = 关 (lib 默认, 契约测试不变); 生产 daemon 置 0.15。
    double near_end_max_buy_price{0.0};

    // 必赢锁利买入 (2026-06-05 老板「必赢的, 只要除去买和卖手续费有利润就买」): 已决出且被选边是【赢方】→
    //   买价(exec_ask)买进、结算收敛到 1, 扣买+卖手续费仍净正 ((1−ask)−买费−卖费>0) → 强制买到此上限
    //   (绕模型 edge/Kelly 谨慎 + force_cross 穿价锁单)。事件延迟真 edge: 市场尚未把决出赢方收敛到 1。
    //   0 = 关 (lib 默认, 契约测试不变); 生产 daemon 置 50 (= per_order_cap, 保守起步; 受 RM market cap 约束)。
    double must_win_lock_usdc{0.0};

    // (rel_stop_pct / hold_if_winning_floor 2026-06-12 治理删: 2026-06-11 hold-to-settlement 反事实
    //  判死 mark/fair 基止损 (n=5 被割仓 60% 终赢 Δ+54), 生产恒 0 关。git 史可考。)

    // 必输方开仓护栏 (老板 2026-06-09「调试持仓逻辑, 查明真正原因」): 被选边【模型 fair】< 此值 → 不开新仓
    //   (近必输 longshot 下侧到 0 远大于 edge, 永远 −EV)。用模型 fair 非市场价地板 (老板「用模型」)。减仓/
    //   平仓/must_win 不受限。修「game_decided 必输保护对 tennis best-of-3 永不触发 (phase 边界 bug) → 买崩盘
    //   underdog 单笔 −0.87/−2.00」。0 = 关 (lib 默认, 契约测试不变); 生产 daemon 置 0.15。
    double min_open_fair{0.0};
    // 赢面稳定窗 (老板 2026-06-11「入场太早赢面不稳定」): 开新仓要求被选边 sharp 在过去此窗口内
    //   【全程】≥ min_open_fair (买稳定赢面, 不买正在经过门槛的钟摆)。0=关 (lib 默认, 契约/管线测试不变);
    //   生产 daemon 随 enable_phase0_gates 置 180s。
    std::int64_t open_stable_window_ns{0};
    // FLB-hold 引擎开关 (老板 2026-06-11 拍板「与现策略并跑」): false=关 (lib 默认, 契约不变);
    //   生产 daemon 置 true。stake/触发档为代码内常数 (kFlb*, 老板「策略系数不进配置层」)。
    bool flb_enabled{false};
    // 账本持久化路径 (2026-06-11 老板「迭代部署 vs 攒数据」根治): 每 60s 快照持仓+累计+CLV 到此文件
    //   (tmp+rename 原子写), 启动时 RestoreLedgerSnapshot 恢复 (停机期错过的结算由孤儿 sweep 自动补)。
    //   空=关 (lib 默认)。paper-only (R-11: 不碰真账本)。
    std::string ledger_snapshot_path{};

    // (reentry_cooldown_ns / rebuy_edge_premium / tp_reversal_vel_thr / vel_exit_thr /
    //  near_settle_capture_frac 2026-06-12 治理删: 全为「有卖出才有的病」(churn/rebuy/止盈时机),
    //  hold-to-settlement 后无卖出路径, 生产恒 0 关。git 史可考。)

    //   frozen_hard_stop_pct: 冻结期硬下行保护 (2026-06-10 老韩 bug#2 + 老板「下行不够细致」) —— sharp 掉档冻结态下,
    //     mark 跌破均入 ×(1−此值) 且【双边簿紧】(真崩盘非退化簿) → 灾难止损截尾。hold-to-settlement 三出口之一。
    //     仅 fair_is_sharp==false 时触发, 与 −5.80 退化簿(sharp 仍有效)签名互斥, 不回归卖飞。0 = 关; 生产 0.40 (深阈截尾)。
    double frozen_hard_stop_pct{0.0};

    // paper_no_edge_gates (老板 2026-06-03「把门都去了, 虚拟盘专门调模型, 模型自主, 识别各种情况」):
    //   虚拟盘调模型模式 — 去掉所有 edge 边门, 让模型/sharp/score-prior 的任意正净 edge 都成交:
    //     ① edge_ci_lower 全源走 raw_edge (不扣二项抽样噪声)
    //     ② net_ev_ok 强制 true (不要 2×fee+slippage 门)
    //     ③ sizing 跳过 Step1/2/3 edge 门 (sz_in.no_edge_gate)
    //     ④ target 放行模型驱动 fair (ml_blend) + sharp, 不再硬要 has_real_fair (让模型在其训练域 pre-game 也能交易)
    //   仍保: devig_ok (市场锚有效) + sizing Step5 (net 正, 不在保证亏的盘交易) + RM cap 链 (仓位上限非 edge 门)。
    //   默认 false (实盘/契约测试不变); paper daemon 显式置 true。R-11: 纯 paper VirtualFill, 不碰真钱。
    bool paper_no_edge_gates{false};

    // (ML 驱动 blend 配置 ml_fair_blend_weight / ml_drive_enabled 已砍 2026-06-05「砍掉大模型训练功能」:
    //  fair 不再有 ONNX blend, 由 sharp/derivative/score-prior 驱动 [pricing::ResolveFair]。
    //  短时套利 advisory 配置同砍: seq_arb_model 大模型旁路一并删。)

    // strategy_id / signal_id (audit / RM 去重用)
    std::string strategy_id{"paper-demo-v1"};

    // FairValue 先验参数
    double fv_alpha{0.30};
    double fv_beta{0.50};
    double fv_book_blend{0.20};

    // 启动时把 RiskGateway 从 SAFE_MODE 切到 RUNNING
    bool set_rm_running{true};

    // P0-4 advisory gate (ML-R2): paper 期所有市场均为 advisory.
    // true (默认) = advisory 市场不产生 OrderIntent, 不进 RiskGateway::evaluate.
    // false = 允许产生 intent (仅当 has_real_fair=true 且未来真实 ML 模型接入后使用).
    // 当前 M1: 恒 true. 修改此值须同步更新 advisory 字段逻辑并重走 paper gate.
    bool advisory_markets_no_intent{true};

    // A1: 真实 Goalserve 比分新鲜度上限 (ns). data_source_ts 比 now 旧超过此值 →
    //   视为陈旧, 退回 has_real_fair=false (老韩 D4 #8 + 老周 R-20: 冻结比分不当 live fair).
    //   默认 120s (feed ~1-5s 周期; 容跨洋抖动 + 短暂断流).
    std::int64_t score_staleness_limit_ns{120'000'000'000LL};
};

// ---------------------------------------------------------------------------
// PaperLoopStats — 可观测计数器 (atomic, 只增)
// ---------------------------------------------------------------------------
struct PaperLoopStats {
    std::atomic<std::uint64_t> ticks_total{0};
    // R-6/老郭 韧性: loop_thread_ 每 tick 末更新心跳 (epoch_ns)。观测端 (healthz/metrics) 比对
    //   now-last_tick 超阈 → loop 卡死告警 (老郭: loop 单点无存活监控, 卡死=静默停摆无人知)。
    std::atomic<std::int64_t> last_tick_ts_ns{0};
    std::atomic<std::uint64_t> orders_attempted{0};
    std::atomic<std::uint64_t> orders_approved{0};
    std::atomic<std::uint64_t> orders_rejected{0};
    std::atomic<std::uint64_t> fills_completed{0};
    std::atomic<std::uint64_t> fills_missed{0};
    // 目标仓位控制器 (老雷 spec v1): 控制器决定本 tick 不动 (死区/限价不可成交/已达目标)。
    std::atomic<std::uint64_t> orders_held{0};
    // 执行层 §4.1: 毒性硬档冻结加仓的次数 (观测; 默认关 → 恒 0)。
    std::atomic<std::uint64_t> tox_freezes{0};
    // slice-3b: 比赛结算时被 realize+平仓的持仓笔数 (winner→1 / loser→0)。
    std::atomic<std::uint64_t> positions_settled{0};
    std::atomic<std::uint64_t> hub_reads_empty{0};
    std::atomic<std::uint64_t> quote_publishes{0};
    std::atomic<std::uint64_t> ledger_publishes{0};

    PaperLoopStats() = default;
    PaperLoopStats(const PaperLoopStats&) = delete;
    PaperLoopStats& operator=(const PaperLoopStats&) = delete;
};

// ---------------------------------------------------------------------------
// PaperLoop — 最小 paper 交易循环
// ---------------------------------------------------------------------------
class PaperLoop {
public:
    // 构造 — 注入依赖, 不启动线程
    //   hub:              OrderBookSnapshotHub (只读, live book 快照)
    //   rm:               RiskGateway (写: evaluate; caller 保证此线程是唯一 evaluate 调用方)
    //   position_ledger:  PositionLedger (写: apply_fill; 单 writer = loop_thread_)
    //   ledger_hub:       LedgerSnapshotHub (写: Publish)
    //   quote_hub:        QuoteSnapshotHub (写: Publish)
    //   rm_snap:          RmDebugSnapshot* (可 nullptr; P0-1: 读用, 写由 RM 内部唯一负责)
    //   fv_model:         IFairValueModel (只读; 调用方保证生命周期 >= PaperLoop)
    //   token_map:        condition_id → (token0_id YES, token1_id NO)
    explicit PaperLoop(const polymarket::clob_wss::OrderBookSnapshotHub& hub, risk::RiskGateway& rm,
                       risk::PositionLedger& position_ledger, risk::LedgerSnapshotHub& ledger_hub,
                       sizing::QuoteSnapshotHub& quote_hub, risk::RmDebugSnapshot* rm_snap,
                       const pricing::IFairValueModel& fv_model,
                       std::unordered_map<std::string, std::pair<std::string, std::string>> token_map,
                       PaperLoopConfig cfg = {}) noexcept;

    PaperLoop(const PaperLoop&) = delete;
    PaperLoop& operator=(const PaperLoop&) = delete;
    PaperLoop(PaperLoop&&) = delete;
    PaperLoop& operator=(PaperLoop&&) = delete;

    // 析构 — 保证线程已 join
    ~PaperLoop();

    // SetExecutor — executor 缝注入 (2026-06-12 实盘准备: live build 在 Start 前换 LiveExecutorAdapter)。
    //   仅允许 Start 前调 (单线程装配期); paper build 不调 = VirtualExecutor 默认, 行为零变。
    void SetExecutor(std::unique_ptr<execution::IOrderExecutor> ex) noexcept {
        if (!running_.load(std::memory_order_acquire) && ex) executor_ = std::move(ex);
    }

    // Start — 启动 loop_thread_ (幂等)
    void Start();

    // Stop — 请求停止并等待线程退出 (幂等, noexcept)
    void Stop() noexcept;

    [[nodiscard]] bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    [[nodiscard]] const PaperLoopStats& stats() const noexcept { return stats_; }

    // ---- 账本持久化 (2026-06-11): Start 前调 Restore (单线程); Save 由 TickAll 60s 节流自动调,
    //   测试可直接调。恢复内容: 持仓 (apply_fill 重放) + 累计 realized/fee (总+逐盘) + CLV (聚合+pending)。
    void SaveLedgerSnapshot();
    void RestoreLedgerSnapshot();
    // 逐盘 PnL 实时 MTM 重发 (2026-06-11): 每 tick 用实时 mark 重发 LedgerFeatures (不碰 fee 累计)。
    void RepublishLedgerMark(const std::string& condition_id, const std::string& token_id, double mark_price,
                             const polymarket::clob_wss::OrderBookFeatures& feat) noexcept;

    // bench/test seam (老姜性能评审): 同步跑一次 TickAll, 精确测单 tick 延迟 (不经 RunLoop 的 sleep)。
    //   不起 loop_thread_; 调用方负责先注入 catalog + hub book。仅用于 benchmark/单测, 生产走 Start()。
    void TickAllForBench() { TickAll(); }

    // 引擎分账快照 (2026-06-12 老板「能区分开」; 线程安全拷贝)
    struct EngineSplit {
        double realized{0.0};
        std::int64_t settles{0};
        std::int64_t wins{0};
    };
    [[nodiscard]] std::unordered_map<std::string, EngineSplit> engine_split() const {
        std::lock_guard<std::mutex> lk(engine_mu_);
        std::unordered_map<std::string, EngineSplit> out;
        for (const auto& [e, b] : engine_book_) out[e] = {b.realized, b.settles, b.wins};
        return out;
    }

    // ---- FLB-hold 引擎 (老板 2026-06-11 拍板「与现策略并跑」) -------------------------------------
    //   实证 (299 已结算盘): PM 赛中 favorite 系统性低估 2-3pp, 首穿越 0.80 买入持有到结算净 EV +3.3%/u。
    //   纯订单簿触发 (不需 Goalserve), 只做【非 sharp】盘 (与主引擎物理隔离不抢地盘); 一盘一击 (首穿越,
    //   one-shot); 永不割 (无 book_det/止损路径 — 这些盘无 book 订阅 TickOne 天然跳过), 结算链复用
    //   (SettlementPoller catalog∪held + 孤儿 sweep)。daemon 扫描线程发现触发 → RequestFlbEntry 入队 →
    //   loop_thread_ 在 TickAll 起始排干, 走正常 RM→sign→VirtualMatcher→ledger 全路径 (不绕 RM 红线)。
    struct FlbTrigger {
        std::string condition_id;
        std::string token_id;        // 被买边 token
        bool is_yes{true};
        double ask_px{0.0};          // 触发刻该边可成交买价 (YES=yes_ask; NO=1−yes_bid 合成)
        double ask_sz_usdc{0.0};     // 该价位深度 (撮合模拟用)
        bool dip{false};             // 抄底档 (2026-06-11): true=赛前 favorite 砸坑首触, 分账 engine="flb-dip"
        std::int64_t event_ts_ns{0};  // R-20 4ts: 来自 REST book timestamp
        std::int64_t data_source_ts_ns{0};
        std::int64_t ingestion_ts_ns{0};
    };
    // 扫描线程 (daemon) 调用: 入队 + 一盘一击去重 (重复 condition 直接丢)。线程安全 (flb_mu_)。
    void RequestFlbEntry(const FlbTrigger& t);
    // 该 condition 是否已触发过 (扫描端预过滤省 book 拉取)。线程安全。
    [[nodiscard]] bool FlbSeen(const std::string& condition_id) const;

    // slice-3b: 累计已实现 PnL (whole pUSD; 含结算)。观测/dashboard/测试 (loop_thread_ 写, 读时近似)。
    [[nodiscard]] double cum_realized_pnl_pusd() const noexcept { return cum_realized_pnl_pusd_; }
    // 累计已付 taker fee (whole pUSD, 绝对值单调)。AccountEquity / 端点 / 测试用 (2026-06-01 凯利评审)。
    [[nodiscard]] double cum_fee_pusd() const noexcept { return cum_fee_pusd_; }

    // ---- 成交流水 (2026-06-04 老板「多少价格买的/卖出的都不知道」) ----
    //   每笔 paper 成交落一行: 时间 + 盘口 + 买/卖 + 成交价 + 数量 + 本笔已实现。前端「成交流水」面板 +
    //   /api/v1/fills 用。定长 ring (R-12 bounded), loop_thread_ 写 / 端点读, mutex 保护。纯观测不入决策。
    struct FillRow {
        std::int64_t as_of_ts_ns{0};   // 成交观测刻 (R-20, 上游 ts)
        std::string condition_id;      // 盘口
        std::string event_title;       // 人读队名/比赛 (前端展示; loop 填)
        bool is_yes{true};             // 被交易边 (YES/NO)
        bool is_buy{true};             // 买/卖
        bool is_close{false};          // 是否平仓动作
        double price{0.0};             // 成交价
        double size_usdc{0.0};         // 成交量 (whole pUSD)
        double realized{0.0};          // 本笔已实现 (卖出=（卖价−均入）×量; 买入=0)
        double cum_realized{0.0};      // 成交后累计已实现
        // 模型决策上下文 (2026-06-04 老板「分析交易调模型」): 成交刻模型 fair + 市场 mark,
        //   供前端模型诊断 (声称 edge = fair−price; 模型偏差 = fair−mark; 看是否反指标/系统偏高)。
        double fair{0.0};              // 成交刻模型对【被交易边】的 fair (= p_fair_side, FILL 日志同源)
        double mark{0.0};              // 成交刻市场 mark price
        double fee{0.0};               // 本笔手续费 (老板 2026-06-09「手续费逐笔体现」): size×fee_coef×p×(1−p)
        std::string exit_reason;       // 卖出原因 (2026-06-10 老板「出现卖出就检查是否合理」): rel_stop/vel_exit/
                                       //   frozen_hard/game_decided/kelly_reduce/winprob_cut/m2a_switch (买入空)
        std::string engine;            // 引擎标签 (2026-06-11 FLB 并跑对比): ""=sharp 主引擎 / "flb"=FLB-hold (加性)
    };
    // 最近 N 笔成交 (最新在前)。market 非空 → 只取该 condition 的成交 (盯盘按盘看, 不受全局churn丢失)。
    [[nodiscard]] std::vector<FillRow> RecentFills(std::size_t max_n = 200,
                                                   const std::string& market = "") const {
        std::lock_guard<std::mutex> lk(fills_mu_);
        std::vector<FillRow> out;
        out.reserve(std::min(max_n, fills_ring_.size()));
        // fills_ring_ 末尾最新 → 倒序取; market 过滤 (深环 5000 → 单盘历史够深)
        for (std::size_t i = 0; i < fills_ring_.size() && out.size() < max_n; ++i) {
            const FillRow& r = fills_ring_[fills_ring_.size() - 1 - i];
            if (market.empty() || r.condition_id == market) out.push_back(r);
        }
        return out;
    }

    // AccountEquity — 单一账户权益口径 (2026-06-01 凯利评审, docs/MEETINGS/2026-06-01-kelly-equity-review.md)。
    //   收敛原双轨 (RecordEquity@TickAll 与 FeedRiskGateway daily_pnl 各算一套 = 审计噩梦)。
    //   双口径分离 (六席共识): 风控/DD/凯利分母用 best_bid 保守清算价; 展示用 microprice。
    //   无效 bid / stale book (data_source_ts 超 score_staleness_limit_ns) 仓位按 0 浮盈 (不臆造正值, 老韩铁律#2)。
    //   R-11: 只读 paper ledger 实例 (position_ledger_ + hub_), 不碰真账本。loop_thread_ 写账本, 读时近似 (观测/sizing 容忍)。
    struct AccountEquitySnapshot {
        double bankroll_init{0.0};   // 起始虚拟本金 (cfg_.bankroll_usdc)
        double cum_realized{0.0};    // 累计已实现 PnL
        double cum_fee{0.0};         // 累计已付 fee
        double realized_equity{0.0}; // bankroll_init + cum_realized − cum_fee (已落袋净值)
        double unrealized_bid{0.0};  // Σ(best_bid − avg_entry)×qty (保守, 无效/stale 计 0) — 喂凯利/DD
        double unrealized_mark{0.0}; // Σ(microprice − avg_entry)×qty (展示用)
        double position_mtm{0.0};    // Σ qty×microprice (持仓市值, 展示)
        double equity_bid{0.0};      // realized_equity + unrealized_bid (凯利分母 / DD 基, 保守)
        double equity_mark{0.0};     // realized_equity + unrealized_mark (展示净值)
        double cash_available{0.0};  // MVP 近似: bankroll_init − Σ(avg_entry×qty, 多头) + cum_realized − cum_fee
        double sharpe{0.0};          // 年化 (仅 published 副本填; account_equity() 内为 0)
        double max_drawdown{0.0};    // ∈[0,1] (仅 published 副本填)
        int open_positions{0};
        double deploy_pct{0.0};      // P4 部署率 = locked_cost/bankroll (2026-06-11 晚会; >0.85 WARN)
        std::int64_t as_of_ts_ns{0}; // 最新仓位 book data_source_ts (R-20, 禁 now())
    };
    [[nodiscard]] AccountEquitySnapshot account_equity() const noexcept;

    // published_account_equity — 线程安全发布副本 (loop_thread_ 每 tick 末发布; 任意线程拷贝读)。
    //   debug_api 经 daemon 回调读此 (不直接调 account_equity, 避免 HTTP 线程并发读账本)。含 sharpe/maxDD。
    [[nodiscard]] AccountEquitySnapshot published_account_equity() const {
        std::lock_guard<std::mutex> lk(acct_pub_mu_);
        return published_equity_;
    }

    // [mark-staleness fix 2026-06-05] per-持仓 live MTM — /api/v1/positions 盯盘端点用。
    //   根因: 旧 /positions 读 LedgerSnapshotHub.mark_price, 仅该市场有新成交才更新 (PublishLedgerSnapshot
    //   fill-gated) → 空仓期冻结陈旧 (实测落后 account 20min); 且该快照无 side 字段, 端点硬编码 "YES"。
    //   本方法读真 PositionLedger (per-token, 带 Outcome) + 当前 live 簿 (hub_, 与 account_equity() 同源),
    //   一次修对陈旧 + YES/NO。纯观测 (debug_api 经 daemon 回调读), 不喂决策/风控。
    struct PositionMtm {
        std::string condition_id;
        bool is_yes{true};
        double net_qty{0.0};
        double avg_entry{0.0};
        double mark{0.0};
        double pnl_unrealized{0.0};
        std::int64_t as_of_ts_ns{0};
        // 状态标 (2026-06-11 老板「状态能标一下吗」): live=活簿实时 / settling_won=终局赢定等结算 /
        //   settling_lost=终局输定 / settling=终局未明。终局盘无活簿时 mark 用 CLV 末次观测 mid 估值。
        std::string status{"live"};
    };
    [[nodiscard]] std::vector<PositionMtm> positions_mtm() const noexcept;

    // M3 成果尺子: CLV 聚合报告 (G1 验收: clv_close_mean>1.5% + positive_rate>55%)。
    [[nodiscard]] eval::CLVTracker::Report clv_report() const noexcept { return clv_tracker_.report(); }
    // Phase 0 项5: 组合度量 (Sharpe/maxDD/VaR; periods_per_year 由调用方按 tick 间隔传)。
    [[nodiscard]] eval::PortfolioMetrics::Report portfolio_report(double periods_per_year = 0.0) const noexcept {
        return portfolio_metrics_.report(periods_per_year);
    }
    // 权益时序拷贝 (2026-06-01 凯利评审: debug_api pnl_timeseries 落地用; loop 写读时拷)。
    [[nodiscard]] std::vector<std::pair<std::int64_t, double>> equity_snapshot() const {
        return portfolio_metrics_.equity_snapshot();
    }
    // tick 间隔 (ms) — 调用方算 periods_per_year (年化 Sharpe) 用。
    [[nodiscard]] std::int64_t tick_interval_ms() const noexcept { return cfg_.tick_interval_ms; }

    // 有持仓 (open) 的 condition 集 (2026-06-10 老板「我们结算的没有遗漏吧」防孤儿结算): 供 daemon 重发现保留。
    //   比赛结束掉出 discovery (只留 live+≤1h) 的市场若仍有持仓, 必须留在 catalog + SettlementPoller 直到结算,
    //   否则 token_map_ 重建丢弃它 → TickOne(结算路) + SettlementPoller(resolution 路) 两路皆断 → 孤儿仓永不结算
    //   (= CLV=0 根因)。线程安全: get_per_condition_exposure 是 shared_lock 读, 可跨线程 (daemon 重发现线程) 调用。
    [[nodiscard]] std::vector<std::string> HeldConditions() const {
        std::vector<std::string> out;
        for (const auto& [cid, sz] : position_ledger_.get_per_condition_exposure()) {
            if (sz != 0) out.push_back(cid);  // 净敞口非 0 = 仍持仓 = 需保留至结算
        }
        return out;
    }

    // A1: 注入真实 Goalserve 比分源 (可空; nullptr → 恒 stub 路径, 行为同 A1 前).
    //   单 writer: 仅主线程在 Start() 前调用一次 (score_store_ 之后只读).
    void SetScoreStore(const data::ScoreSnapshotStore* s) noexcept { score_store_ = s; }

    // 事件驱动触发 (2026-06-04 老板「别轮询, 直接触发更快」): 数据源 (WSS book / 149hz poll / 赔率) 到达即调。
    //   仅短锁 + notify (R-12 安全, 调用线程<1us 不阻塞); loop_thread_ 等 cv 醒来即跑一轮决策 (单写, 无 ledger 竞争)。
    void RequestTick() noexcept {
        {
            std::lock_guard<std::mutex> lk(tick_mu_);
            tick_pending_ = true;
        }
        tick_cv_.notify_one();
    }

    // R-3: fee/cat/parent 三表已并入 PaperCatalog (SetPaperCatalog 统一注入)。原 3 个独立 setter 删除。

    // slice-3c: 注入 per-condition 结算状态 (app 层轮询 gamma `closed` / clob `tokens[].winner` →
    //   此处注入)。单 writer: Start() 前注入 / 周期热刷 (loop_thread_ 只读)。喂 resolution_status 特征
    //   + 给 3b 提供权威 winner (status=Resolved+winner → 按 winner 结算, 优于 Goalserve 比分推断)。
    // [R-1 并发修复 2026-06-01 老周评审] RCU 热刷: 刷新线程 (RefreshResolution 30s) 运行期写,
    //   loop_thread_ 读 — 原 plain map move-assign 并发 find = UB。改 mutex+shared_ptr swap (同 event_map)。
    using ResolutionMap = std::unordered_map<std::string, ResolutionEntry>;
    void SetResolutionByCondition(ResolutionMap m) noexcept {
        std::lock_guard<std::mutex> lk(resolution_mu_);
        resolution_by_condition_ = std::make_shared<const ResolutionMap>(std::move(m));
    }

    // live_stats 采集 hop: 注入 join_key(league|home|away) → LiveStatsFields (app 层轮询
    //   commentaries Feed → 此处注入)。[R-1] 同 resolution: RefreshLiveStats 30s 运行期写, loop 读 → RCU。
    //   game_row 填充时按 es 队名 join → FillLiveStats → g_*_diff 特征。查不到 → soccer_* 保持 -1。
    void SetLiveStatsByTeams(data::livescore::LiveStatsMap m) noexcept {
        std::lock_guard<std::mutex> lk(live_stats_mu_);
        live_stats_by_teams_ = std::make_shared<const data::livescore::LiveStatsMap>(std::move(m));
    }

    // bm_slots: 注入 inplay_match_id → 跨庄家赔率 (app 层 RefreshOdds 拉 getodds + inplay-mapping
    //   join → 此处注入)。[R-1] 同 live_stats RCU: 刷新线程运行期写, loop 读。game_row 填充时按
    //   matched inplay_match_id 查 → FillBmSlotsYesCanonical 定向填 bm_slots → g_bm_* 特征。
    void SetOddsByMatchId(data::OddsMap m) noexcept {
        std::lock_guard<std::mutex> lk(odds_mu_);
        odds_by_match_ = std::make_shared<const data::OddsMap>(std::move(m));
    }

    // [P1 backtest-equivalence 2026-06-01] DecisionInputSnapshot — TickAll 入口冻结的【5 个非 book 决策输入】
    //   聚合引用 (book 第 6 输入走 hub_, 已可经 ReplayDriver 注入 → 不在此)。live: TickAll 读各 store 填;
    //   replay: SetReplayInputs() 注入历史帧 → 闭合红线#3 (回测=实盘当前只覆盖 1/6 输入) 的单一注入点。
    //   各 store 仍各自 RCU 高频 swap; 本 struct 只在 tick 入口聚合一次, 整 tick 持有同版本 (消 read-skew)。
    //   归口: docs/RESEARCH/laolei-backtest-equivalence-spec-v1.md (小蒋 P2 ReplayImpl 喂帧)。
    struct DecisionInputSnapshot {
        std::shared_ptr<const ConditionEventMap> event_map;       // condition→event 映射桥
        std::shared_ptr<const data::ScoreMap> score;              // #2 比分/时钟 (+#3 inplay sharp 赔率附此)
        std::shared_ptr<const PaperCatalog> catalog;              // #6 fee/cat/parent/line 静态元
        std::shared_ptr<const ResolutionMap> resolution;          // #5 REST 结算
        std::shared_ptr<const data::livescore::LiveStatsMap> live_stats;  // #4 g_*_diff 微观
        std::shared_ptr<const data::OddsMap> odds;  // bm_slots: inplay_match_id→跨庄家赔率 (g_bm_*)
    };
    // (SetReplayInputs 回放注入缝 2026-06-12 老板裁决删: 回测=in-sample 假象, 验证=脚本+paper。)

    // (大模型注入方法已砍 2026-06-05「砍掉大模型训练功能」: SetMlModel/SetMlModelShared/SetSeqArbModel/
    //  SetSeqArbModelShared/SetFeatureVectorHub。fair 不再有 ONNX 推理 blend; 量化因子直填 qf。)

    // R-3: parent_by_condition_ 已并入 PaperCatalog (SetPaperCatalog)。原 SetParentRefs 删除。

    // A1: 注入/热刷 condition_id→event 映射 (app 层 EventMatcher 解析后周期推送).
    //   线程安全: shared_ptr + mutex 短锁 swap (同 ScoreSnapshotStore 模式; libc++ 无
    //   atomic<shared_ptr>). loop_thread_ 读时短锁拷 ptr, app 层写时短锁换 ptr.
    void SetEventMapping(std::shared_ptr<const ConditionEventMap> m) noexcept {
        std::lock_guard<std::mutex> lk(event_map_mu_);
        event_map_ = std::move(m);
    }

    // 周期重发现 (老板 2026-06-01) + R-3 统一 catalog: per-condition 静态元数据单一 RCU 快照热刷.
    //   loop_thread_ TickAll 入口取快照迭代; daemon 重发现线程构建新 catalog 一次 swap (原子, 消半新半旧).
    using TokenMap = std::unordered_map<std::string, std::pair<std::string, std::string>>;  // ctor 兼容
    void SetPaperCatalog(std::shared_ptr<const PaperCatalog> c) noexcept {
        std::lock_guard<std::mutex> lk(catalog_mu_);
        catalog_ = std::move(c);
    }
    [[nodiscard]] std::shared_ptr<const PaperCatalog> LoadPaperCatalog() const noexcept {
        std::lock_guard<std::mutex> lk(catalog_mu_);
        return catalog_;
    }

private:
    // ---- 依赖引用 ----
    const polymarket::clob_wss::OrderBookSnapshotHub& hub_;
    risk::RiskGateway& rm_;
    risk::PositionLedger& position_ledger_;
    risk::LedgerSnapshotHub& ledger_hub_;
    sizing::QuoteSnapshotHub& quote_hub_;
    // P0-1: rm_snap_ 字段保留供外部通过 attach_rm_debug_snapshot() 读取 ring snapshot.
    // paper_loop 不再调用 push_reject (RM 内部已唯一负责), 但字段生命周期管理仍属 paper_loop.
    // 当前只写不读 (P0-1 后 push_reject 移到 RM); ctor 内 (void)rm_snap_ 抑制 clang
    // -Wunused-private-field (gcc 不认指针成员上的 [[maybe_unused]], 故不用属性, 改 (void) 引用)。
    risk::RmDebugSnapshot* rm_snap_;

    // ---- 自有对象 (FairValueEstimator 是 facade, 持 model 引用) ----
    pricing::FairValueEstimator fv_estimator_;

    // ---- Paper signer 三件套 (自有; 单 writer 用) ----
    signer::paper::VirtualNonceProvider nonce_provider_;
    signer::paper::VirtualGasEstimator gas_estimator_;
    signer::paper::VirtualConfirmWatcher confirm_watcher_;
    signer::paper::PaperSigner psigner_;

    // ---- VirtualMatcher (Mode A++) ----
    execution::VirtualMatcher matcher_;

    // ---- 订单执行器 (G-1 executor 注入; 默认 VirtualExecutor 包 matcher_, 行为逐位不变) ----
    //   声明在 matcher_ 之后 → 析构先于 matcher_ (executor_ 持 matcher_ 引用)。
    std::unique_ptr<execution::IOrderExecutor> executor_;

    // ---- R-3: per-condition 静态元数据统一 catalog (RCU 热刷; loop 读 tick 快照, daemon 写 swap) ----
    //   原 token/fee/cat/parent 4 张并行 map 合并; 周期重发现一次原子 swap (消半新半旧定价分派错)。
    static constexpr double kDefaultFeeCoef = 0.03;  // 体育保守 (= RM kSportsTakerFeeRate); 查不到默认
    mutable std::mutex catalog_mu_;
    std::shared_ptr<const PaperCatalog> catalog_;
    PaperLoopConfig cfg_;

    // [P1] TickAll 入口冻结的 5 输入聚合 (原 tick_catalog_/tick_resolution_/tick_live_stats_/
    //   tick_event_map_/tick_score_snap_ 五个散成员合一; 见 DecisionInputSnapshot)。
    DecisionInputSnapshot tick_inputs_;
    // [2026-06-01 凯利评审] tick 入口冻结一次账户权益 → 整轮所有子盘口 sizing 用同版本 bankroll
    //   (消 read-skew + 防同 tick 内多笔成交驱动 bankroll 抖动; 老韩/小梁「tick 级冻结快照」)。
    AccountEquitySnapshot tick_equity_{};
    // 线程安全发布副本 (loop_thread_ 写 / debug_api 经回调拷贝读)。含 sharpe/maxDD (loop_thread 算)。
    mutable std::mutex acct_pub_mu_;
    AccountEquitySnapshot published_equity_{};

    // 三个 accessor 改读 tick_catalog_ (loop_thread_, TickAll 入口已冻结)。查不到 → 默认。
    [[nodiscard]] const ParentRef* ParentRefFor(const std::string& condition_id) const noexcept {
        if (tick_inputs_.catalog == nullptr) return nullptr;
        auto it = tick_inputs_.catalog->find(condition_id);
        return (it != tick_inputs_.catalog->end()) ? &it->second.parent : nullptr;
    }
    [[nodiscard]] double FeeCoefFor(const std::string& condition_id) const noexcept {
        if (tick_inputs_.catalog == nullptr) return kDefaultFeeCoef;
        auto it = tick_inputs_.catalog->find(condition_id);
        return (it != tick_inputs_.catalog->end()) ? it->second.fee_coef : kDefaultFeeCoef;
    }
    [[nodiscard]] MarketCat MarketCatFor(const std::string& condition_id) const noexcept {
        if (tick_inputs_.catalog == nullptr) return MarketCat{};
        auto it = tick_inputs_.catalog->find(condition_id);
        return (it != tick_inputs_.catalog->end()) ? it->second.cat : MarketCat{};
    }
    // catalog 元数据新鲜度 (gamma 发现/重建该条目时刻; 老板「每个源标时间」)。查不到 → 0 (age NaN)。
    [[nodiscard]] std::int64_t CatalogDiscoveredAtFor(const std::string& condition_id) const noexcept {
        if (tick_inputs_.catalog == nullptr) return 0;
        auto it = tick_inputs_.catalog->find(condition_id);
        return (it != tick_inputs_.catalog->end()) ? it->second.discovered_at_ns : 0;
    }
    // slice-3c: per-condition 结算状态 (REST 注入; 查不到 → nullptr)。[R-1] RCU: mutex+shared_ptr,
    //   loop_thread_ 经 TickAll 入口冻结 tick_resolution_ 快照读 (整 tick 同版本, 不并发刷新线程 swap)。
    mutable std::mutex resolution_mu_;
    std::shared_ptr<const ResolutionMap> resolution_by_condition_;
    [[nodiscard]] std::shared_ptr<const ResolutionMap> LoadResolution() const noexcept {
        std::lock_guard<std::mutex> lk(resolution_mu_);
        return resolution_by_condition_;
    }
    [[nodiscard]] const ResolutionEntry* ResolutionFor(const std::string& condition_id) const noexcept {
        if (tick_inputs_.resolution == nullptr) return nullptr;
        auto it = tick_inputs_.resolution->find(condition_id);
        return (it != tick_inputs_.resolution->end()) ? &it->second : nullptr;
    }
    // live_stats 采集 hop: join_key → LiveStatsFields (REST 注入; 查不到 → nullptr)。[R-1] 同 resolution RCU。
    mutable std::mutex live_stats_mu_;
    std::shared_ptr<const data::livescore::LiveStatsMap> live_stats_by_teams_;
    [[nodiscard]] std::shared_ptr<const data::livescore::LiveStatsMap> LoadLiveStats() const noexcept {
        std::lock_guard<std::mutex> lk(live_stats_mu_);
        return live_stats_by_teams_;
    }
    [[nodiscard]] const data::livescore::LiveStatsFields* LiveStatsFor(
        const std::string& join_key) const noexcept {
        if (tick_inputs_.live_stats == nullptr) return nullptr;
        auto it = tick_inputs_.live_stats->find(join_key);
        return (it != tick_inputs_.live_stats->end()) ? &it->second : nullptr;
    }
    // bm_slots: inplay_match_id → 跨庄家赔率 (RefreshOdds 注入; 查不到 → nullptr)。[R-1] 同 live_stats RCU。
    mutable std::mutex odds_mu_;
    std::shared_ptr<const data::OddsMap> odds_by_match_;
    [[nodiscard]] std::shared_ptr<const data::OddsMap> LoadOdds() const noexcept {
        std::lock_guard<std::mutex> lk(odds_mu_);
        return odds_by_match_;
    }
    [[nodiscard]] const data::goalserve::MatchResultOdds* OddsFor(
        const std::string& inplay_match_id) const noexcept {
        if (tick_inputs_.odds == nullptr) return nullptr;
        auto it = tick_inputs_.odds->find(inplay_match_id);
        return (it != tick_inputs_.odds->end()) ? &it->second : nullptr;
    }
    // (大模型成员已砍 2026-06-05: ml_holder_/seq_arb_holder_/fv_hub_。fair 不依赖 ML 推理。)

    // ---- A1: 真实比分源 + 映射 ----
    // score_store_: 单 writer (Start 前注入), 之后 loop_thread_ 只读 Get(). 可空 → stub 路径.
    const data::ScoreSnapshotStore* score_store_{nullptr};
    // event_map_: condition→event 映射 (shared_ptr + mutex 热刷; loop_thread_ 读, app 层写).
    mutable std::mutex event_map_mu_;
    std::shared_ptr<const ConditionEventMap> event_map_;

    // A4 (老周/老板「相对最近刷新」): tick-local 冻结快照 — TickAll 入口取一次, 整 tick 全子盘口共享同版本。
    //   根除 read-skew (同 event 的 moneyline/spread 看不同比分版本)。loop_thread_ 单线程, 无需锁;
    //   shared_ptr 持有保证 tick 内不被采集线程 swap 掉 (引用计数)。

    // LoadEventMap — 短锁拷当前映射 ptr (loop_thread_ 用; nullptr 若未注入).
    [[nodiscard]] std::shared_ptr<const ConditionEventMap> LoadEventMap() const noexcept {
        std::lock_guard<std::mutex> lk(event_map_mu_);
        return event_map_;
    }

public:
    // 当前映射条目数 (测试/观测: 验证 app 层刷新线程已匹配并注入映射). 短锁.
    [[nodiscard]] std::size_t event_map_size() const noexcept {
        std::lock_guard<std::mutex> lk(event_map_mu_);
        return event_map_ ? event_map_->size() : 0;
    }

    // P0-1 test seam: 测试直接触发喂数 (验 exposure 红线接通 + 单位 ×1e6 门禁, 老周强制测试)。
    void FeedRiskGatewayForTest() noexcept { FeedRiskGateway(); }

    // Phase B test seam: 单测 SelectSide 选边逻辑 (de-vig 锚定: p_fair>=devig 买 YES, < 买 NO)。
    [[nodiscard]] DecisionSide SelectSideForTest(double p_fair_yes, double p_market_devig) const noexcept {
        return SelectSide(p_fair_yes, p_market_devig);
    }

private:
    // ---- 线程控制 ----
    std::jthread loop_thread_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};

    // ---- 事件驱动触发 (2026-06-04 老板「别轮询直接触发」) ----
    //   数据源到达 → RequestTick() 置 tick_pending_ + notify; RunLoop 等 cv (含 fallback 心跳超时) 醒来跑 TickAll。
    //   决策仍 loop_thread_ 单写 (TickOne 不并发, 无 ledger/RM/pnl 竞争); 数据线程仅 notify 不碰决策态 (R-12)。
    std::mutex tick_mu_;
    std::condition_variable tick_cv_;
    bool tick_pending_{false};

    // ---- 统计 ----
    mutable PaperLoopStats stats_;

    // ---- intent_id 单调递增 (loop_thread_ 单写) ----
    std::uint64_t intent_seq_{0};

    // ---- 强制穿越状态 (小梁 Q-梁-2): condition_id → 上 tick YES-canonical p_fair ----
    //   loop_thread_ 单 writer (TickOne 读+写), 无需锁。本 tick |p_fair − last| > 阈 → force_cross。
    std::unordered_map<std::string, double> last_p_fair_;

    // ---- 卖出原因 (2026-06-10 老板「出现卖出就检查是否合理」): token_id → 本 tick 决出的卖出原因 ----
    //   主逻辑在 ExecuteControllerSide 前写; ApplyFill 对卖出成交回读填 FillRow.exit_reason。loop_thread_ 单 writer。
    std::unordered_map<std::string, std::string> last_sell_reason_;
    // ---- 孤儿结算诊断节流 (2026-06-10 老板「查消失的盘结算有没有进账户」): 上次打孤儿诊断的 NowNs (30s 节流) ----
    std::int64_t last_orphan_diag_ns_{0};
    std::int64_t last_ledger_snapshot_ns_{0};  // 账本快照 60s 节流 (loop_thread_, 2026-06-11 持久化)
    // ---- CLV 失效熔断 (2026-06-12 治理「能利用的利用起来」: CLVTracker 反哺入场) ----
    //   CLV(close口径)正率是入场质量金标准 (实测健康期 82.8%, n=122)。正率跌破 70% (样本≥30) =
    //   模型失效信号 (赔率源断/匹配错/延迟恶化) → 熔断新开仓 (减仓/平仓/结算不受限), 恢复自动解除。
    //   loop_thread_ 单线程读写; 30s 节流刷新。CLV 聚合随快照持久化 → 重启后熔断态自愈。
    bool clv_breaker_{false};
    std::int64_t last_clv_breaker_check_ns_{0};
    // ---- GateEvaluator 记分牌 (2026-06-12 治理: stats G1-G7 统计门接 daily-close) ----
    //   逐笔已实现 PnL (卖出+结算两路 append, loop_thread_ 单写; 与 trade_returns_ 同 5000 上限折半)。
    std::vector<double> gate_trade_pnl_;
    std::int64_t first_trade_ts_ns_{0};
    std::int64_t last_trade_ts_ns_{0};
    // FLB 漏斗计数器 (2026-06-11 老板「进场怎么那么少, 是不是机会被错过」): 每道门拦截计数,
    //   loop_thread_ 写, 5min 节流 dump [flb-funnel] 后清零。看清 30 个带内盘没进的真实卡点。
    // P1 (2026-06-11 晚会): per-tick 计数膨胀 51 万级不可读 → 改 per-市场去重 (5min 窗 distinct cond)。
    struct FlbFunnel {
        std::unordered_set<std::string> not_moneyline, sharp_mapped, not_inplay, seen, miss_backoff;
        std::unordered_set<std::string> no_book, one_sided, wide_spread, leadch, baseball;
        std::unordered_set<std::string> not_in_band, thin_depth, mom_jump, fired;
    };
    FlbFunnel flb_funnel_;
    std::int64_t last_funnel_dump_ns_{0};
    std::int64_t last_daily_close_day_{0};  // P5 日级滚账 (UTC 日序号)
    std::int64_t last_deploy_warn_ns_{0};   // P4 部署率告警 5min 节流
    // 引擎归因 (2026-06-12 老板「能区分开就行」): token → engine ("sharp"/"flb"/"flb-dip"), 入场时记,
    //   结算/平仓按真实引擎分账 (废 flb_seen_ 猜测)。loop_thread_ 写; 快照 E 行持久化。
    std::unordered_map<std::string, std::string> engine_by_token_;
    struct EngineBook {
        double realized{0.0};
        std::int64_t settles{0};
        std::int64_t wins{0};
    };
    std::unordered_map<std::string, EngineBook> engine_book_;  // engine → 分账 (engine_mu_ 保护)
    mutable std::mutex engine_mu_;  // loop 写 × HTTP 读 (deque 教训: 跨线程容器必加锁)
    // ---- 三振出局 (老板 2026-06-11 拍板, 治跷跷板循环割肉: 3 个循环盘吃掉 78% realized 亏损) ----
    //   condition_id → 止损 episode 计数 (force_stop 连续段计 1 次); 满 2 次本场不再开新仓。
    //   episode set: force_stop 持续多 tick 只计一次, 清除后再触发算新 episode。loop_thread_ 单 writer。
    std::unordered_map<std::string, int> market_stop_count_;
    std::unordered_set<std::string> market_stop_episode_;
    // ---- FLB-hold 引擎状态 (老板 2026-06-11): 触发队列 (daemon 扫描线程写 / loop_thread_ 排干) +
    //   一盘一击去重集 (入队刻即记, 重复 condition 丢弃)。flb_mu_ 保护两者 (扫描端 FlbSeen 预过滤同锁)。
    mutable std::mutex flb_mu_;
    std::vector<FlbTrigger> flb_pending_;
    std::unordered_set<std::string> flb_seen_;
    // FLB v2 路径状态 (多特征研究 2026-06-11, n=593: 跳升追入 −1.5% vs 缓升 +3.8%; 拉锯3+易主 −3.5%):
    //   per-condition yes_mid 采样环 (30s 格, 16 槽 ≈ 8min) → 5min 动量; 领先易主计数 (mid 穿 0.5)。
    //   loop_thread_ only (MaybeFlbTrigger 更新), 无锁。
    struct FlbPathState {
        std::array<std::pair<std::int64_t, double>, 16> ring{};  // (ts_ns, yes_mid)
        std::size_t ring_n{0};
        std::size_t ring_head{0};
        std::int64_t last_sample_ns{0};
        int lead_changes{0};
        int prev_lead{0};  // +1 yes 领先 / −1 no 领先 / 0 未知
        // 撮合 miss 退避 (2026-06-11: 单盘 tick 频率空转 721 次重试刷屏): miss 后 60s 不重触发;
        //   累计 20 次 miss = 该簿结构性吃不进 → 永久放弃 (保持 seen)。
        int miss_count{0};
        std::int64_t last_miss_ns{0};
        // 抄底锚 (2026-06-11 老板拍板「FLB 加抄底档」): 首见 yes_mid + 时刻 — 赛前/早期首见 ≥0.65 的
        //   favorite 盘中砸坑 (0.30-0.40/+14.5%, 0.50-0.60/+8.4%, 跳 0.40-0.50 死区) 首触即买。
        double first_mid{std::numeric_limits<double>::quiet_NaN()};
        std::int64_t first_mid_ns{0};
    };
    std::unordered_map<std::string, FlbPathState> flb_path_;
    void ProcessFlbTrigger(const FlbTrigger& t);  // loop_thread_ only (TickAll 起始排干调用)
    // 触发型检测 (loop_thread_, TickAll 每市场调; book 落 hub 即唤醒 → 亚秒级, 老板「要触发型」)。
    void MaybeFlbTrigger(const std::string& cond_id, const PaperMarketEntry& entry);

    // ---- 逐盘累计已实现/费 (老板 2026-06-09「前端观测做到位, 交易订单对得上 PnL」): condition_id → 累计 ----
    //   修对账 bug: PublishLedgerSnapshot 原硬编码 pnl_realized=0 + pnl_fee 只本笔 → 逐盘 net_pnl 平仓后丢
    //   realized (顶栏早改 account 口径修了, 逐盘漏)。这里持久累计 (sell + settle 都加), 喂 ledger_hub 逐盘快照。
    //   loop_thread_ 单 writer。account 级 cum_realized_pnl_pusd_ 不变 (权威总账)。
    std::unordered_map<std::string, double> cum_realized_by_market_;
    std::unordered_map<std::string, double> cum_fee_by_market_;

    // ---- 逐笔收益序列 (金融小梁 P1-C: 修 Sharpe −239 垃圾口径): 每次平仓/结算 push r=已实现/名义本金 ----
    //   原 Sharpe 建在 per-tick 权益 MtM 抖动上 ×√7944 → 无金融意义。改建在逐笔已实现收益上 (mean/std)。
    //   loop_thread_ 单 writer。年化口径待金融 ADR (paper 交易频率不稳, per-trade Sharpe 已非垃圾可解读)。
    std::vector<double> trade_returns_;

    // ---- 时序特征环形缓冲 (老板 2026-05-31): condition_id → YES-canonical 微价时序 ----
    //   PIT-safe / BR-1 共用; loop_thread_ 单 writer (TickOne push + PublishQuoteSnapshot 读)。
    //   每 condition 一个定长 ring; 派生微价变化率 + realized vol 进 QuoteFeatures (训练捕获 + 观测)。
    std::unordered_map<std::string, ml::FeatureHistory> ts_history_;
    // NO 边时序环 (老板 2026-05-31「双边信息都要有」): NO book 独立微结构 (OFI/amihud/depth 非
    //   YES 镜像, 各有 vig/流)。与 ts_history_ 对称, 派生 no_* 特征 (双边完整, 不只一边)。
    std::unordered_map<std::string, ml::FeatureHistory> ts_history_no_;

    // ---- sharp fair 时序环 (老板 2026-06-05「方向真值=赔率源 sharp; line movement 一阶导」) ----
    //   condition_id → (sharp, mid) 时序; 派生 sharp velocity + 市场价相对 sharp 收敛/发散率 →
    //   QuoteFeatures (观测先行)。PIT-safe / loop_thread_ 单 writer (TickOne push + 读)。
    std::unordered_map<std::string, ml::SharpFairTrack> sharp_history_;

    // ---- 已向 RM 注册 condition→event 的集合 (相关性集中度 cap R6.2c, 注册一次防每 tick 锁churn) ----
    //   loop_thread_ 单 writer (TickAll insert)。首次见某 condition 即 rm_.set_condition_event。
    std::unordered_set<std::string> rm_event_registered_;

    // ---- 批1 体育动态: 比分时序 (进球新鲜度/动量; game_row.score 派生) ----
    std::unordered_map<std::string, ml::GameScoreHistory> game_history_;

    // ---- A5 (老韩 spec §4): 累计已付 taker fee (whole pUSD, 单调加) ----
    //   DD 喂数: daily_pnl = 时点净 MtM − cum_fee。PublishLedgerSnapshot 算 pnl_fee 后累加,
    //   FeedRiskGateway 读。loop_thread_ 单 writer (两者同线程顺序调), 无需 atomic。
    double cum_fee_pusd_{0.0};

    // ---- slice-3b 结算 (老板 2026-05-31): 累计已实现 PnL + 已结算盘口 ----
    //   FeedRiskGateway 早留口子 (M2 注释): daily_pnl = 开仓 MtM + realized − cum_fee。
    //   比赛 Ended → 按终态比分把持仓 realize 到结算值 (winner 1 / loser 0) + 平仓; 之后不再交易。
    //   loop_thread_ 单 writer。settled_conditions_: 幂等 + 已定盘口跳过决策。
    double cum_realized_pnl_pusd_{0.0};
    std::unordered_map<std::string, char> settled_conditions_;

    // 成交流水 ring (2026-06-04 老板「看懂买卖价」): 定长, loop_thread_ 写 / 端点读 mutex 保护。
    static constexpr std::size_t kFillsRingCap = 5000;  // 深环: 模型驱动100+笔/30s, 5000≈数十分钟/单盘历史够深
    mutable std::mutex fills_mu_;
    std::deque<FillRow> fills_ring_;  // 末尾最新; 超 cap 弹头

    // ---- M3 成果尺子 (老雷 results plan v1): CLV 测量 ----
    //   每笔买入成交记 entry; 每 tick 更新 mid; 结算时算 CLV (close mid / 0-1 settle)。
    //   离线评估 only (小蒋前视红线: 绝不回喂决策)。loop_thread_ 单 writer。
    eval::CLVTracker clv_tracker_;
    // 实时 CLV 滚动环 (Stage2 sizing 用; PIT-safe = 成交刻决策 fair − 成交价; 跨盘口全局)。
    //   loop_thread_ 单 writer (买入成交记录 + sizing 读 Mean)。
    eval::RollingClv rolling_clv_;
    eval::PortfolioMetrics portfolio_metrics_;  // Phase 0 项5: 权益曲线 → Sharpe/maxDD/VaR
    // DD→target 乘子状态 (Stage2 老板「回撤大只停加仓 + hysteresis」): 当前回撤分档乘子, 黏滞恢复。
    //   loop_thread_ 单 writer (RecordEquity 后 UpdateDrawdownMultiplier 写; ControlInput 读)。
    double dd_mult_{1.0};

    // ---- 内部实现 ----
    void RunLoop(std::stop_token st);
    void TickAll();
    // 二元市场双边决策 (老周架构 laozhou-binary-dual-side-arch-v1 + 老郭 review APPROVE-with-conditions):
    //   TickOne 改 per-condition, 入参带整盘口 (YES book + NO book), 决策时带双边信息 (老板原则 C3)。
    void TickOne(const BinaryMarketSnapshot& mkt);
    // SelectSide (Phase B, 小梁 spec): de-vig 锚定下选被低估边。raw_edge_yes=p_fair_yes-p_market_devig;
    //   edge_NO=-edge_YES (精确对称) → raw_edge_yes>=0 买 YES (YES 低估), <0 买 NO (NO 低估)。
    //   是否真下单 (edge 够不够) 由下游 sizing/CI gate 定 (edge_ci<=0 → suggested=0 → 不产 intent)。
    [[nodiscard]] DecisionSide SelectSide(double p_fair_yes, double p_market_devig) const noexcept;

    // 目标仓位控制器单边执行 (Step 4-8; 被选边 target=Kelly / 非选边平旧边 target=0 共用)。
    //   M2-a 选边翻转平旧边: TickOne 对盘口两边各调一次驱动到目标。reservation/Decide/intent/RM/
    //   sign/match/apply_fill(卖负 delta)/ledger 全收进来。loop_thread_ 串行 (R-12 不触碰)。
    void ExecuteControllerSide(const std::string& condition_id, const std::string& token_id,
                               strategy::Outcome outcome,
                               const polymarket::clob_wss::OrderBookFeatures& side_book,
                               double book_depth_l1, double p_fair_side, double target_mag,
                               double fee_coef, bool force_cross, int n_eff, double margin_floor,
                               bool noise_free, bool force_stop, bool near_end,
                               double time_to_res_frac) noexcept;

    // slice-3b 结算: 比赛 Ended → 按终态比分把 YES/NO 持仓 realize 到结算值 (winner 1 / loser 0) +
    //   平仓 (apply_fill 负 delta), realized PnL 累加进 cum_realized_pnl_pusd_。loop_thread_ 单 writer。
    //   winner 从 game_row 终态比分 + orientation 派生 (score_home_total = YES 边比分); 平局 → 0.5 push。
    void SettleCondition(const std::string& condition_id, const std::string& yes_token_id,
                         const std::string& no_token_id, double settle_yes, double settle_no,
                         const data::feature_store::FeatureStoreGameRow& game_row) noexcept;
    // 单 token 结算 (无仓 → no-op)。realize = (settle − avg_entry) × qty。
    void SettleToken(const std::string& condition_id, const std::string& token_id,
                     strategy::Outcome outcome, double settle_price,
                     const data::feature_store::FeatureStoreGameRow& game_row) noexcept;

    // CI 下界: edge_ci_lower = (p_fair - p_ask) - z * sqrt(p*(1-p)/n)
    [[nodiscard]] static double ComputeEdgeCiLower(double p_fair, double p_ask, int n_eff, double z) noexcept;

    [[nodiscard]] static std::int64_t NowNs() noexcept;

    void PublishLedgerSnapshot(const std::string& condition_id, const execution::VirtualFill& fill,
                               double mark_price,
                               const polymarket::clob_wss::OrderBookFeatures& feat) noexcept;

    // P0-1 (老韩 RM 契约 + 老周架构): 把 paper 持仓敞口喂进 RM, 激活 exposure 红线 (生产此前零喂数
    //   → per_condition/per_outcome cap 永不咬)。loop_thread_ 内串行调用 (R-12: 不在 WSS io_thread)。
    //   ⚠ 单位: 仓位账本 size_usdc 是 whole pUSD, RM exposure 比 micro → 必 ×1e6 (漏乘 =
    //   红线静默架空)。
    void FeedRiskGateway() noexcept;

    // P0-3: has_real_fair=false → 清零 edge/kelly/notional/signal/predict_ok (宁可空不可假)
    void PublishQuoteSnapshot(const std::string& condition_id, const pricing::FairValueResult& fv_result,
                              const sizing::SizingOutput& sizing_out, double mark_price, double edge_ci_lower,
                              const polymarket::clob_wss::OrderBookFeatures& feat, bool has_real_fair,
                              double cross_spread, double no_microprice, double no_imbalance, bool devig_ok,
                              std::int64_t joint_as_of_ts_ns, const std::string& event_id,
                              const std::string& neg_risk_market_id, double target_signed_notional,
                              double reservation_buy_px, double reservation_sell_px,
                              double required_margin, double game_decided_sign, bool near_end,
                              double time_to_resolution_frac,
                              double g_time_x_lead, double g_fld_signal, double g_remaining_sec,
                              std::int32_t g_periods_won_home, std::int32_t g_periods_won_away,
                              const SportsFeatures& sports,
                              const data::feature_store::FeatureStoreGameRow& ml_game_row,
                              const data::feature_store::FeatureStoreBookRow& ml_book_row,
                              double decision_fair, std::int8_t fair_src_code,
                              const FairCandidates& fair_cands,
                              std::int64_t no_book_ds_ts, std::int64_t no_book_ing_ts,
                              const polymarket::clob_wss::OrderBookFeatures* no_book_full = nullptr) noexcept;

    // 填 QuoteFeatures 的观测特征列 (MlFeature 18-74 + 4ts/ids/fair/fee/micro/pos/resolution)。
    //   TickOne (决策前 blend predict) 与 PublishQuoteSnapshot (发布) 共用 → 训练=推理同源, 无漂移。
    //   不填决策输出列 (target/reservation/kelly/suggested/signal) 与 provenance。
    void PopulateFeatureColumns(sizing::QuoteFeatures& qf, const std::string& condition_id,
                                const pricing::FairValueResult& fv_result, double mark_price,
                                const polymarket::clob_wss::OrderBookFeatures& feat, double cross_spread,
                                double no_microprice, double no_imbalance, bool devig_ok,
                                std::int64_t joint_as_of_ts_ns, const std::string& event_id,
                                const std::string& neg_risk_market_id, double time_to_resolution_frac,
                                double g_time_x_lead, double g_fld_signal, double g_remaining_sec,
                                std::int32_t g_periods_won_home, std::int32_t g_periods_won_away,
                                const SportsFeatures& sports, std::int64_t no_book_ds_ts,
                                std::int64_t no_book_ing_ts,
                                const polymarket::clob_wss::OrderBookFeatures* no_book_full = nullptr) noexcept;
};

}  // namespace stcpp::paper
