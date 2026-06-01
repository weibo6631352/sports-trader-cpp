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

// slice-3c 结算注入 (老板 2026-05-31 摸底定论: Polymarket resolution **不在 market WSS**,
//   是 REST 字段 — gamma `closed` + clob `tokens[i].winner`)。app 层轮询 REST → SetResolutionByCondition
//   注入 (同 fee/event-mapping 注入范式), 非 WSS 解析 (market 频道只推 book/price/trade/tick)。
//   status: 0=Open / 1=Resolving / 2=Resolved (= wss::ResolutionStatus 同枚举值, 喂模型特征)。
//   winner: −1=未知 / 0=NO 赢 / 1=YES 赢 (Resolved 时填; 3b 权威结算 winner, 全 market type 通用)。
struct ResolutionEntry {
    std::uint8_t status{0};
    std::int8_t winner{-1};
};

// 批1 体育动态特征 (TickOne 算好, 一struct 传 PublishQuoteSnapshot, 避免 param 爆炸)。
//   全部 game_row.score / live_stats 派生; 无真比分 → NaN。
struct SportsFeatures {
    double game_phase{std::numeric_limits<double>::quiet_NaN()};
    double garbage_time{0.0};
    double clutch{0.0};
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

    // ML 驱动决策 blend 权重 (老板 2026-05-31 放开 paper 期 ML-R2)。p_fair = (1−w)·baseline + w·ml_p_yes。
    //   0 = 纯 baseline (默认; 现有契约测试不变)。仅当真 ONNX 模型 (kind==Onnx) 加载才生效, stub 永不驱动。
    //   PaperLoop 天然 paper (不花真钱); live 路径不复用此 blend。daemon 生产可设 1.0 (有模型时 ML 全驱动)。
    double ml_fair_blend_weight{0.0};

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

    // slice-3b: 累计已实现 PnL (whole pUSD; 含结算)。观测/dashboard/测试 (loop_thread_ 写, 读时近似)。
    [[nodiscard]] double cum_realized_pnl_pusd() const noexcept { return cum_realized_pnl_pusd_; }

    // M3 成果尺子: CLV 聚合报告 (G1 验收: clv_close_mean>1.5% + positive_rate>55%)。
    [[nodiscard]] eval::CLVTracker::Report clv_report() const noexcept { return clv_tracker_.report(); }
    // Phase 0 项5: 组合度量 (Sharpe/maxDD/VaR; periods_per_year 由调用方按 tick 间隔传)。
    [[nodiscard]] eval::PortfolioMetrics::Report portfolio_report(double periods_per_year = 0.0) const noexcept {
        return portfolio_metrics_.report(periods_per_year);
    }

    // A1: 注入真实 Goalserve 比分源 (可空; nullptr → 恒 stub 路径, 行为同 A1 前).
    //   单 writer: 仅主线程在 Start() 前调用一次 (score_store_ 之后只读).
    void SetScoreStore(const data::ScoreSnapshotStore* s) noexcept { score_store_ = s; }

    // R-fee-2: 注入 per-market 手续费系数 (condition_id → gamma feeSchedule.rate)。
    //   单 writer: Start() 前注入一次, 之后 loop_thread_ 只读。空/查不到 → kDefaultFeeCoef (0.03)。
    //   官方禁硬编码 (docs.polymarket): 体育 0.03 / 加密 0.072 / 老市场 0 各异。
    void SetFeeByCondition(std::unordered_map<std::string, double> m) noexcept {
        fee_by_condition_ = std::move(m);
    }

    // v0.7: 注入 per-condition 类别上下文码 (真实 Polymarket 市场结构 → ML 特征 82-85)。
    //   单 writer: Start() 前注入 (market discovery 后); loop_thread_ 只读。查不到 → 默认 (sports/-1)。
    void SetMarketCatByCondition(std::unordered_map<std::string, MarketCat> m) noexcept {
        market_cat_by_condition_ = std::move(m);
    }

    // slice-3c: 注入 per-condition 结算状态 (app 层轮询 gamma `closed` / clob `tokens[].winner` →
    //   此处注入)。单 writer: Start() 前注入 / 周期热刷 (loop_thread_ 只读)。喂 resolution_status 特征
    //   + 给 3b 提供权威 winner (status=Resolved+winner → 按 winner 结算, 优于 Goalserve 比分推断)。
    void SetResolutionByCondition(std::unordered_map<std::string, ResolutionEntry> m) noexcept {
        resolution_by_condition_ = std::move(m);
    }

    // live_stats 采集 hop: 注入 join_key(league|home|away) → LiveStatsFields (app 层轮询
    //   commentaries Feed → 此处注入)。单 writer: Start() 前注入 / 周期热刷 (loop_thread_ 只读)。
    //   game_row 填充时按 es 队名 join → FillLiveStats → g_*_diff 特征。查不到 → soccer_* 保持 -1。
    void SetLiveStatsByTeams(data::livescore::LiveStatsMap m) noexcept {
        live_stats_by_teams_ = std::move(m);
    }

    // 步④: 注入 ML 推理模型 (ml::FairValueModel; daemon 装配 Stub/ONNX)。单 writer: Start() 前注入,
    //   loop_thread_ 只读。nullptr = 无模型 → 走 baseline provenance。ML-R1/R2: 推理结果 advisory,
    //   只填 QuoteFeatures.ml_advisory_p_yes + provenance, 绝不改 fair_value/决策。owner 是 daemon。
    void SetMlModel(const ml::FairValueModel* m) noexcept { ml_model_ = m; }

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

    // 统一数据树: 注入 condition → 父级引用 (event_id / neg_risk_market_id)。
    //   单 writer: Start() 前注入一次, 之后 loop_thread_ 只读。盘口决策/模型带父级 (兄弟经 event_id 导航)。
    void SetParentRefs(std::unordered_map<std::string, ParentRef> m) noexcept {
        parent_by_condition_ = std::move(m);
    }

    // A1: 注入/热刷 condition_id→event 映射 (app 层 EventMatcher 解析后周期推送).
    //   线程安全: shared_ptr + mutex 短锁 swap (同 ScoreSnapshotStore 模式; libc++ 无
    //   atomic<shared_ptr>). loop_thread_ 读时短锁拷 ptr, app 层写时短锁换 ptr.
    void SetEventMapping(std::shared_ptr<const ConditionEventMap> m) noexcept {
        std::lock_guard<std::mutex> lk(event_map_mu_);
        event_map_ = std::move(m);
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

    // ---- 配置与 token map ----
    std::unordered_map<std::string, std::pair<std::string, std::string>> token_map_;
    PaperLoopConfig cfg_;

    // ---- R-fee-2: per-market 手续费系数 (condition_id → feeSchedule.rate) ----
    //   Start 前注入, 之后只读。查不到 → kDefaultFeeCoef。RM/sizing/PnL 同源用此值。
    static constexpr double kDefaultFeeCoef = 0.03;  // 体育保守 (= RM kSportsTakerFeeRate)
    std::unordered_map<std::string, double> fee_by_condition_;
    // 统一数据树: 父级引用 (Start 前注入, 之后只读)。查不到 → 空 ParentRef。
    std::unordered_map<std::string, ParentRef> parent_by_condition_;
    [[nodiscard]] const ParentRef* ParentRefFor(const std::string& condition_id) const noexcept {
        auto it = parent_by_condition_.find(condition_id);
        return (it != parent_by_condition_.end()) ? &it->second : nullptr;
    }
    [[nodiscard]] double FeeCoefFor(const std::string& condition_id) const noexcept {
        auto it = fee_by_condition_.find(condition_id);
        return (it != fee_by_condition_.end()) ? it->second : kDefaultFeeCoef;
    }
    // v0.7: per-condition 类别上下文 (market discovery 注入; 查不到 → 默认 sports/-1 占位)。
    std::unordered_map<std::string, MarketCat> market_cat_by_condition_;
    [[nodiscard]] MarketCat MarketCatFor(const std::string& condition_id) const noexcept {
        auto it = market_cat_by_condition_.find(condition_id);
        return (it != market_cat_by_condition_.end()) ? it->second : MarketCat{};
    }
    // slice-3c: per-condition 结算状态 (REST 注入; 查不到 → nullptr = 未知/默认 Open)。
    std::unordered_map<std::string, ResolutionEntry> resolution_by_condition_;
    [[nodiscard]] const ResolutionEntry* ResolutionFor(const std::string& condition_id) const noexcept {
        auto it = resolution_by_condition_.find(condition_id);
        return (it != resolution_by_condition_.end()) ? &it->second : nullptr;
    }
    // live_stats 采集 hop: join_key → LiveStatsFields (REST 注入; 查不到 → nullptr = 无 live_stats)。
    data::livescore::LiveStatsMap live_stats_by_teams_;
    [[nodiscard]] const data::livescore::LiveStatsFields* LiveStatsFor(
        const std::string& join_key) const noexcept {
        auto it = live_stats_by_teams_.find(join_key);
        return (it != live_stats_by_teams_.end()) ? &it->second : nullptr;
    }
    // 步④: ML 推理模型 (非自有; daemon 注入 + 持有)。loop_thread_ 只读。nullptr = baseline only。
    const ml::FairValueModel* ml_model_{nullptr};
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
    std::shared_ptr<const ConditionEventMap> tick_event_map_;
    std::shared_ptr<const data::ScoreMap> tick_score_snap_;

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
                               double fee_coef, bool force_cross, int n_eff,
                               double margin_floor) noexcept;

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
