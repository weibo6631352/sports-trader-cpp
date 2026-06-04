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

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include "stcpp/data/score_snapshot_store.hpp"  // A4: ScoreMap (tick-local 共享比分快照, 消 read-skew)
#include "stcpp/data/live_stats_store.hpp"      // live_stats 采集 hop: LiveStatsMap/LiveStatsFields join
#include "stcpp/data/odds_snapshot_store.hpp"   // bm_slots: OddsMap (inplay_match_id→跨庄家赔率)
#include "stcpp/data/bm_slots_fill.hpp"         // bm_slots: FillBmSlotsYesCanonical (定向 de-vig 填充)
#include "stcpp/eval/clv_tracker.hpp"            // CLV 测量 harness (成果尺子, 离线评估)
#include "stcpp/eval/portfolio_metrics.hpp"      // Phase 0 项5: Sharpe/maxDD/VaR (北极星 KPI)
#include "stcpp/ml/feature_history.hpp"          // 时序特征环形缓冲 (PIT-safe, BR-1 共用)
#include "stcpp/ml/game_score_history.hpp"       // 比分时序 (进球新鲜度/动量)
#include "stcpp/ml/fair_value_model.hpp"         // ml::FairValueModel/ModelPrediction (步④ 推理接线)
#include "stcpp/ml/seq_arb_model.hpp"            // ml::SeqArbModel (短时套利 advisory 旁路)
#include "stcpp/ml/hot_swap_model.hpp"           // ml::HotSwapHolder (模型热加载)
#include "stcpp/ml/feature_vector_hub.hpp"       // Phase 2 项6: 完整 75 列向量发布 (训练捕获)
#include "stcpp/ml/model_feature_spec.hpp"       // extract_joined (game_row+book_row → FeatureVector)
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

    // 动态持仓退出 (2026-06-04 金融团队会议「动态持仓实现盈利, 非结算」, docs/MEETINGS/2026-06-04-dynamic-exit-realization.md):
    //   收敛兑现: 持仓边市场收敛到 fair (剩余 edge ≤ cap) 且 best_bid 越获利线 (avg_entry+margin) → 卖平锁利。
    //   解「只买不卖」死结 (Kelly target 随 live fair 涨不降 + reservation_sell 在 fair 上方永不触发)。
    //   take_profit_margin ≤0 = 关 (默认, 契约/管线测试不变); 生产 daemon 置正值开。
    double take_profit_margin{0.0};      // best_bid ≥ avg_entry + 此值 → 平仓锁利 (净利垫, 覆盖往返成本)
    double take_profit_edge_cap{0.02};   // 仅当剩余 edge(fair−mark) ≤ 此值才退 (收敛已捕获, 防 churn 立即回买)

    // paper_no_edge_gates (老板 2026-06-03「把门都去了, 虚拟盘专门调模型, 模型自主, 识别各种情况」):
    //   虚拟盘调模型模式 — 去掉所有 edge 边门, 让模型/sharp/score-prior 的任意正净 edge 都成交:
    //     ① edge_ci_lower 全源走 raw_edge (不扣二项抽样噪声)
    //     ② net_ev_ok 强制 true (不要 2×fee+slippage 门)
    //     ③ sizing 跳过 Step1/2/3 edge 门 (sz_in.no_edge_gate)
    //     ④ target 放行模型驱动 fair (ml_blend) + sharp, 不再硬要 has_real_fair (让模型在其训练域 pre-game 也能交易)
    //   仍保: devig_ok (市场锚有效) + sizing Step5 (net 正, 不在保证亏的盘交易) + RM cap 链 (仓位上限非 edge 门)。
    //   默认 false (实盘/契约测试不变); paper daemon 显式置 true。R-11: 纯 paper VirtualFill, 不碰真钱。
    bool paper_no_edge_gates{false};

    // ML 驱动决策 blend 权重 (老板 2026-05-31 放开 paper 期 ML-R2)。p_fair = (1−w)·baseline + w·ml_p_yes。
    //   0 = 纯 baseline (默认; 现有契约测试不变)。仅当真 ONNX 模型 (kind==Onnx) 加载才生效, stub 永不驱动。
    //   PaperLoop 天然 paper (不花真钱); live 路径不复用此 blend。daemon 生产可设 1.0 (有模型时 ML 全驱动)。
    double ml_fair_blend_weight{0.0};
    // ML 驱动总开关 (2026-06-03, 小邓研究「绝不一上来 weight=1.0」+ §7 垃圾事故配套)。
    //   false (默认) = ML 仅 advisory (照算照显示 ml_advisory_p_yes, 但【不进 fair / 不驱动交易】)。
    //   true = 放行 ML blend 进 fair (须先过 7 道验证关卡 + 策略评审, 见 ml-engineer-fairvalue-alpha-design)。
    //   独立于 weight: weight 是 blend 力度, drive_enabled 是"是否让模型碰真决策"的安全闸 (默认关)。
    bool ml_drive_enabled{false};

    // 短时套利 advisory 配置 (seq_arb_model 旁路信号; 不驱动真单)。
    double arb_est_rtt_ns{50'000'000.0};  // 预估端到端 RTT (含成交确认); 就近部署 ~50ms 默认 (G1 实测后调)
    double arb_max_notional_usdc{2'000.0};
    int arb_max_open_legs{20};
    double arb_lambda{0.10};  // 套利 Kelly 分数 (老韩 RM 联签起 0.10)

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

    // Start — 启动 loop_thread_ (幂等)
    void Start();

    // Stop — 请求停止并等待线程退出 (幂等, noexcept)
    void Stop() noexcept;

    [[nodiscard]] bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    [[nodiscard]] const PaperLoopStats& stats() const noexcept { return stats_; }

    // bench/test seam (老姜性能评审): 同步跑一次 TickAll, 精确测单 tick 延迟 (不经 RunLoop 的 sleep)。
    //   不起 loop_thread_; 调用方负责先注入 catalog + hub book。仅用于 benchmark/单测, 生产走 Start()。
    void TickAllForBench() { TickAll(); }

    // slice-3b: 累计已实现 PnL (whole pUSD; 含结算)。观测/dashboard/测试 (loop_thread_ 写, 读时近似)。
    [[nodiscard]] double cum_realized_pnl_pusd() const noexcept { return cum_realized_pnl_pusd_; }
    // 累计已付 taker fee (whole pUSD, 绝对值单调)。AccountEquity / 端点 / 测试用 (2026-06-01 凯利评审)。
    [[nodiscard]] double cum_fee_pusd() const noexcept { return cum_fee_pusd_; }

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
        std::int64_t as_of_ts_ns{0}; // 最新仓位 book data_source_ts (R-20, 禁 now())
    };
    [[nodiscard]] AccountEquitySnapshot account_equity() const noexcept;

    // published_account_equity — 线程安全发布副本 (loop_thread_ 每 tick 末发布; 任意线程拷贝读)。
    //   debug_api 经 daemon 回调读此 (不直接调 account_equity, 避免 HTTP 线程并发读账本)。含 sharpe/maxDD。
    [[nodiscard]] AccountEquitySnapshot published_account_equity() const {
        std::lock_guard<std::mutex> lk(acct_pub_mu_);
        return published_equity_;
    }

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

    // A1: 注入真实 Goalserve 比分源 (可空; nullptr → 恒 stub 路径, 行为同 A1 前).
    //   单 writer: 仅主线程在 Start() 前调用一次 (score_store_ 之后只读).
    void SetScoreStore(const data::ScoreSnapshotStore* s) noexcept { score_store_ = s; }

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
    // 回放注入: 非 nullptr → TickAll 用注入帧替代 store 读 (小蒋 P2 回测 harness 用)。owner = 调用方;
    //   指针生命周期须覆盖 loop 运行期。live 路径恒 nullptr → 行为逐位不变。
    void SetReplayInputs(const DecisionInputSnapshot* s) noexcept { replay_inputs_ = s; }

    // 步④: 注入 ML 推理模型 (ml::FairValueModel; daemon 装配 Stub/ONNX)。非占有 (调用方管生命周期, 启动期用)。
    //   ML-R2 (老板 2026-05-31 放开): 真 ONNX 模型 blend 进决策 p_fair (stub 永不驱动); 无模型 → baseline。
    void SetMlModel(const ml::FairValueModel* m) noexcept { ml_holder_.StoreNonOwning(m); }

    // 热加载换 fair_value 模型 (老板「边训边跑边更新模型可重新加载」, 2026-06-01)。占有式: 训练产新 ONNX →
    //   daemon watcher 加载校验后 Store 原子换上, 不停盘; 推理线程 Load 拿 copy 期内旧模型不被删。任意线程可调。
    void SetMlModelShared(std::shared_ptr<const ml::FairValueModel> m) noexcept {
        ml_holder_.Store(std::move(m));
    }

    // 注入短时套利序列模型 (ml::SeqArbModel; daemon 装配 Stub/ONNX)。advisory 旁路: 只填 qf.arb_* 观测,
    //   绝不驱动真单 (stub 恒 ok=false 不发; 真模型也止于 advisory 直到 LiveOrderGate 开闸)。
    //   非占有注入 (调用方管生命周期; 启动期用)。
    void SetSeqArbModel(const ml::SeqArbModel* m) noexcept { seq_arb_holder_.StoreNonOwning(m); }

    // 热加载换模型 ("边跑边训": 旁边离线训练进程产新 ONNX → 不停盘原子换上)。占有式 (holder 持引用,
    //   旧模型最后引用释放时回收)。任意线程可调; 推理线程 Load 拿 copy 期内旧模型不被删。
    void SetSeqArbModelShared(std::shared_ptr<const ml::SeqArbModel> m) noexcept {
        seq_arb_holder_.Store(std::move(m));
    }

    // Phase 2 项6: 注入完整 75 列向量 hub (daemon 持有 + recorder 线程消费)。nullptr = 不捕获。
    //   PublishQuoteSnapshot 算完 extract_full 后 Publish 进来 (训练 X 含 0-17 原始列)。单 writer loop_thread_。
    void SetFeatureVectorHub(ml::FeatureVectorHub* h) noexcept { fv_hub_ = h; }

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
    const DecisionInputSnapshot* replay_inputs_{nullptr};  // 非空 → TickAll 用注入帧 (小蒋 P2); live 恒 null
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
    // 步④: ML 推理模型 (非自有; daemon 注入 + 持有)。loop_thread_ 只读。nullptr = baseline only。
    ml::HotSwapHolder<ml::FairValueModel> ml_holder_;    // 步④ fair_value 模型 (热加载: daemon watcher 原子换新 ONNX)
    ml::HotSwapHolder<ml::SeqArbModel> seq_arb_holder_;  // 短时套利模型 (advisory 旁路; 支持热加载换模型)
    // Phase 2 项6: 完整向量 hub (非自有; daemon 注入)。loop_thread_ 单 writer Publish。nullptr = 不捕获。
    ml::FeatureVectorHub* fv_hub_{nullptr};

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

    // ---- 统计 ----
    mutable PaperLoopStats stats_;

    // ---- intent_id 单调递增 (loop_thread_ 单写) ----
    std::uint64_t intent_seq_{0};

    // ---- 强制穿越状态 (小梁 Q-梁-2): condition_id → 上 tick YES-canonical p_fair ----
    //   loop_thread_ 单 writer (TickOne 读+写), 无需锁。本 tick |p_fair − last| > 阈 → force_cross。
    std::unordered_map<std::string, double> last_p_fair_;

    // ---- 时序特征环形缓冲 (老板 2026-05-31): condition_id → YES-canonical 微价时序 ----
    //   PIT-safe / BR-1 共用; loop_thread_ 单 writer (TickOne push + PublishQuoteSnapshot 读)。
    //   每 condition 一个定长 ring; 派生微价变化率 + realized vol 进 QuoteFeatures (训练捕获 + 观测)。
    std::unordered_map<std::string, ml::FeatureHistory> ts_history_;
    // NO 边时序环 (老板 2026-05-31「双边信息都要有」): NO book 独立微结构 (OFI/amihud/depth 非
    //   YES 镜像, 各有 vig/流)。与 ts_history_ 对称, 派生 no_* 特征 (双边完整, 不只一边)。
    std::unordered_map<std::string, ml::FeatureHistory> ts_history_no_;

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

    // ---- M3 成果尺子 (老雷 results plan v1): CLV 测量 ----
    //   每笔买入成交记 entry; 每 tick 更新 mid; 结算时算 CLV (close mid / 0-1 settle)。
    //   离线评估 only (小蒋前视红线: 绝不回喂决策)。loop_thread_ 单 writer。
    eval::CLVTracker clv_tracker_;
    eval::PortfolioMetrics portfolio_metrics_;  // Phase 0 项5: 权益曲线 → Sharpe/maxDD/VaR

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
                               bool noise_free) noexcept;

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
                              double required_margin, double time_to_resolution_frac,
                              double g_time_x_lead, double g_fld_signal, double g_remaining_sec,
                              std::int32_t g_periods_won_home, std::int32_t g_periods_won_away,
                              const SportsFeatures& sports,
                              const data::feature_store::FeatureStoreGameRow& ml_game_row,
                              const data::feature_store::FeatureStoreBookRow& ml_book_row,
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
