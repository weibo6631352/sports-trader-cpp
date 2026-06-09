// src/stcpp/debug_api/state_provider.hpp — 观测只读状态契约 (ADR-038 MVP)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 小冯 (#34) 2026-05-29: 新增 EventInfo + MarketInfo.sports_market_type/group_item_title
//                         + BookSnapshot.last_trade_price + StateProvider::events() (G-FREEZE-W 只增)
// 关联:
//   docs/ADR/2026-05-29-observability-debug-api.md §2 / §4 / §5
//   docs/RESEARCH/laozhou-observability-api-arch-v1.md §3 (零耦合状态暴露) / §5
//   docs/RESEARCH/xiaobai-observability-api-security-v1.md §1 (黑名单) / §3 (mode)
//   docs/RESEARCH/laozhou-market-structure-contract-fix-v1.md (ADR-040 市场结构修正决议)
//   docs/RESEARCH/laoli-events-ws-mapping-spec-v1.md (Events→Market→Token 字段映射)
//   R-11 (paper 不污染真账本; response 带 mode), R-12 (观测侧零反向依赖),
//   R-20 (4 时间戳 epoch_ns int64)
//
// 设计 (老周 §3): 观测侧只持 const 句柄, 热路径模块零反向依赖。
//   阶段 A: debug_api 持 `const StateProvider*`, 由 main 注入。
//   各模块 owner (老韩 RM / 小冯 orderbook / 小石 ledger) 后续提供 double-buffer
//   snapshot 实现, 接到此接口。MVP 默认 StubStateProvider, 返回结构合法的空/0 值。
//
// schema 铁律 (ADR-038 §3):
//   - 4 时间戳字段名 + epoch_ns int64 (禁 ISO 字符串)
//   - vendor-agnostic 字段语义 (禁 goalserve_/pm_ 原始字段; vendor 降为 source 标签)
//   - 黑名单字段 (私钥/签名字节/API secret) 这些 POD 里【物理上不存在】, 从源头杜绝泄露
//
// ADR-040 市场结构修正 (老周决议, G-FREEZE-W correctness 例外, 2026-05-29):
//   - 新增 TokenInfo: per-token 元数据 (token_id / outcome / price / winner)
//   - MarketInfo 追加 condition_id (权威主键) / tokens[] / neg_risk_market_id / slug / polymarket_url
//     market_id 保留为 deprecated alias (= condition_id 值; 前端切换后 P2 移除)
//   - BookSnapshot per-token 化: 追加 token_id / condition_id / outcome;
//     seq/gap/imbalance 语义变为 per-token (字段不动, 语义注释更新)
//     market_id 保留为 deprecated alias (= condition_id 值)
//   - 新增 BinaryMarketBookView: 双 token book view + cross_spread (后端算好)
//   - StateProvider: book() 语义改为按 token_id 查单边; 新增 book_pair(condition_id)
//
// 小冯 schema append (G-FREEZE-W 只增不改名, 2026-05-29):
//   - 新增 EventInfo: event 层元数据 (event_id / slug / title / sport /
//     neg_risk_market_id / condition_ids); 对应 gamma /events response 顶层字段
//   - MarketInfo 追加 sports_market_type (moneyline/spread/totals/outright/prop/series)
//                     + group_item_title (gamma groupItemTitle, 球队名/大小盘边)
//   - BookSnapshot 追加 last_trade_price (CLOB price_change 最新成交价; 0=未知)
//   - StateProvider 新增 events() → vector<EventInfo> (供前端 Event 层导航)
//
// 注意: 本头文件【不得】#include 任何热路径模块头 (risk/signer/exec), 保证零反向依赖。
//       只用标准库 POD。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace stcpp::debug_api {

// ---- 运行模式 (R-11: response 顶层必带 mode) ----
enum class ExecMode : std::uint8_t { Paper, Live, Backtest };

inline const char* exec_mode_str(ExecMode m) noexcept {
    switch (m) {
        case ExecMode::Paper:
            return "paper";
        case ExecMode::Live:
            return "live";
        case ExecMode::Backtest:
            return "backtest";
    }
    return "paper";
}

// ---- 4 时间戳契约 (R-20) ----
// epoch_ns int64; 缺失上游 ts 时为 0 (调用方判 0 = 无该维度)。
struct FourTs {
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
};

// ============================================================
// EventInfo — event 层元数据 (小冯 schema append, G-FREEZE-W 只增)
//
// 对应 gamma /events response 顶层 Event 对象字段:
//   event_id          — gamma id (string)
//   slug              — gamma slug (e.g. "nba-lal-bos-2026-05-29")
//   title             — gamma title / description (比赛标题)
//   sport             — 运动类别 (e.g. "NBA"/"NFL"/"Soccer")
//   neg_risk_market_id — negRiskMarketID (可空; 合并下注市场 ID)
//   condition_ids     — 本 event 下所有市场的 condition_id 列表 (双 token 盘口主键)
// ============================================================
struct EventInfo {
    std::string event_id;                    // gamma Event id
    std::string slug;                        // gamma slug (可构造 polymarket_url)
    std::string title;                       // gamma title / description
    std::string sport;                       // 运动类别 (e.g. "NBA")
    std::string neg_risk_market_id;          // negRiskMarketID (可空)
    bool live{false};                        // gamma event.live=true (正在比赛; 前端默认过滤用)
    std::string icon_url;                    // gamma 赛事图 (event/market icon|image; 前端事件头显示)
    std::vector<std::string> condition_ids;  // 本 event 下所有盘口 condition_id
};

// ============================================================
// /api/v1/positions
// ============================================================
struct HoldingView {
    std::string market_id;  // vendor-agnostic 内部 id
    std::string outcome;    // 内部 outcome 标签 (非 vendor token 字符串)
    double net_qty{0.0};    // 净持仓 (signed)
    double avg_entry_price{0.0};
    double mark_price{0.0};
    double pnl_realized{0.0};
    double pnl_unrealized{0.0};
    std::int64_t as_of_ts_ns{0};
};

// ============================================================
// /api/v1/fills — 成交流水 (2026-06-04 老板「多少价格买的/卖出的都不知道」)
// ============================================================
struct FillView {
    std::int64_t as_of_ts_ns{0};
    std::string market_id;   // condition_id (前端用市场缓存映射成人读队名)
    bool is_yes{true};       // 被交易边
    bool is_buy{true};       // 买/卖
    bool is_close{false};    // 平仓动作
    double price{0.0};       // 成交价
    double size_usdc{0.0};   // 成交量 (whole pUSD)
    double realized{0.0};    // 本笔已实现 (卖=（卖价−均入）×量; 买=0)
    double cum_realized{0.0};// 成交后累计已实现
    double fair{0.0};        // 成交刻模型 fair (被交易边) — 前端模型诊断: 声称 edge=fair−price
    double mark{0.0};        // 成交刻市场 mark — 前端模型诊断: 模型偏差=fair−mark
    double fee{0.0};         // 本笔手续费 (老板 2026-06-09「手续费逐笔体现」): size×fee_coef×p×(1−p)
};

// ============================================================
// /api/v1/pnl/timeseries
// ============================================================
struct PnlBucket {
    std::int64_t bucket_start_ts_ns{0};
    double cum_net_pnl{0.0};
    double realized{0.0};
    double unrealized{0.0};
    double fee{0.0};
    double gas{0.0};
    std::int64_t n_trades{0};
};

// ============================================================
// /api/v1/pnl/attribution (gross→fee→gas→slippage→spread→net 瀑布)
// ============================================================
// 分市场净 PnL (attribution 面板右侧"分市场"列表; market_id vendor-agnostic)
struct PnlPerMarket {
    std::string market_id;
    double net_pnl{0.0};
};

struct PnlAttribution {
    double gross{0.0};
    double fee{0.0};
    double gas{0.0};
    double slippage{0.0};
    double spread{0.0};
    double net{0.0};
    std::int64_t as_of_ts_ns{0};
    std::vector<PnlPerMarket> per_market{};  // 可空; 缺省 = 不分市场
};

// ============================================================
// /api/v1/account (账户级现金 + 估值; 2026-06-01 凯利评审, 老板「虚拟盘要有现金估值显示」)
// ============================================================
// 双口径分离 (六席共识): equity_conservative (best_bid, 喂凯利/DD) + equity_mark (microprice, 展示)。
// kelly_bankroll = 实际喂 SizingCalculator 的 bankroll (= equity_conservative), kelly_bankroll_basis
//   是口径说明字符串, 让操盘员一眼确认「风控纸面化」是否已修 (动态 vs 静态)。
struct AccountSnapshot {
    std::string mode;               // "paper" / "live" / "backtest"
    double bankroll_initial{0.0};   // 起始虚拟本金
    double cash_available{0.0};     // MVP 近似可动用资金 (不含锁定保证金)
    double position_mtm{0.0};       // 持仓市值 (microprice 展示口径; = Σ qty×mark)
    double equity_mark{0.0};        // 展示净值 = cash + microprice MtM
    double equity_conservative{0.0};// 保守净值 = cash + best_bid MtM (= kelly_bankroll)
    double cum_realized_pnl{0.0};
    double cum_unrealized_pnl{0.0}; // microprice 口径未实现
    double cum_fee_paid{0.0};
    double net_pnl{0.0};            // equity_mark − bankroll_initial
    double return_pct{0.0};         // net_pnl / bankroll_initial
    double max_drawdown{0.0};       // ∈ [0,1] (PortfolioMetrics, best_bid equity 曲线)
    double sharpe{0.0};             // 年化
    double kelly_bankroll{0.0};     // 实际喂凯利的 bankroll (= equity_conservative)
    std::string kelly_bankroll_basis;  // 口径说明 (e.g. "equity_conservative(best_bid, 动态)")
    int open_positions{0};
    std::int64_t as_of_ts_ns{0};
    bool has_data{false};           // false = paper_loop 未注入 / 无数据 → 前端降级灰显
};

// ============================================================
// /api/v1/risk/rejects (RM 拒单列表 + reason_code)
// ============================================================
// side/size/price 为 allowlist 安全字段 (订单意图摘要, 非签名字节/私钥; 小白 §1)。
struct RiskRejectRow {
    std::string reason_code;  // RM 枚举字符串 (e.g. "MAX_POSITION_EXCEEDED")
    std::string market_id;
    std::string intent_ref;  // 内部引用 (非签名/私钥; allowlist 安全字段)
    std::string side;        // "BUY" / "SELL" (allowlist)
    double size{0.0};        // 被拒订单 size (USDC 名义)
    double price{0.0};       // 被拒订单报价
    std::int64_t rejected_ts_ns{0};
};

// ============================================================
// /api/v1/gate/paper (GM-PAPER-G 30 日门禁仪表; MVP 字段可空)
// ============================================================
struct PaperGate {
    std::int64_t n_trades{0};
    double positive_day_ratio{0.0};
    double sharpe{0.0};
    double sharpe_se{0.0};
    double p_value{0.0};
    double hit_rate{0.0};
    double max_drawdown{0.0};
    bool prelim_pass{false};
    bool confirm_pass{false};
    std::int64_t window_days{30};
    bool has_data{false};  // MVP stub: false 表示门禁尚未积累数据
    std::int64_t as_of_ts_ns{0};
};

// ============================================================
// /metrics (Prometheus 业务/健康/数据质量; 低基数 label)
// ============================================================
struct MetricsSnapshot {
    // 健康
    std::int64_t uptime_sec{0};
    bool wss_clob_connected{false};
    bool wss_user_channel_connected{false};
    std::int64_t wss_reconnect_total{0};
    double loop_p99_us{0.0};
    // 业务
    std::int64_t rm_decision_total{0};
    std::int64_t rm_reject_total{0};
    std::int64_t fill_total{0};
    double net_edge_bps{0.0};
    double cum_net_pnl{0.0};
    // 数据质量
    double max_staleness_ms{0.0};
    std::int64_t feed_gap_total{0};
    double price_drift_bps{0.0};
    // 韧性 watchdog (老郭): loop_thread_ 心跳停摆检测。now − last_tick_ts (ms)。
    //   -1 = loop 未跑过 (无心跳); 持续飙升 = loop 卡死 (正常应 ≈ tick 间隔 500ms 内波动)。
    double loop_tick_staleness_ms{-1.0};
    // 订阅计数 (GAP-01/02/03/04, 小冯 ADR-038 append; G-FREEZE-W 只增不改名)
    // subscribed_tokens_total   — hub_.token_count() (已注册 per-token slot 数)
    // subscribed_markets_total  — subscribed_tokens_total / 2 (老李 spec §2.1: 双 token 规则)
    // subscribed_user_conditions — user_condition_ids_.size() (user channel 订阅数)
    // wss_last_disconnect_ts_ns — 最后断连 epoch_ns (0 = 从未断连); GAP-04
    std::int64_t subscribed_tokens_total{0};
    std::int64_t subscribed_markets_total{0};
    std::int64_t subscribed_user_conditions{0};
    std::int64_t wss_last_disconnect_ts_ns{0};

    // ---- 覆盖率/识别率 metric (ADR-038 小卢 2026-05-30 append; G-FREEZE-W 只增不改名) ----
    //
    // 盘口类型识别率 (从 market catalog 算):
    //   market_type_recognized_total — catalog 中 sports_market_type 非空非 "unknown" 的市场数
    //   market_type_unknown_total    — catalog 中 sports_market_type == "" 或 "unknown" 的市场数
    //   派生率 = recognized / (recognized + unknown)
    //   当前 outright 类 sportsMarketType 字段为空 → 0% 识别 (诚实暴露)
    std::int64_t market_type_recognized_total{0};
    std::int64_t market_type_unknown_total{0};

    // 市场覆盖 (从 catalog + token_map 算):
    //   markets_discovered_total  — gamma /events 发现并入 catalog 的市场总数
    //   markets_subscribed_total  — token_map 中已建立双 token 映射的市场数 (可订阅/已订阅)
    //   tokens_subscribed_total   — hub.token_count() 中有效 book slot 数 (实际收到过数据的 token)
    //   覆盖率 = markets_subscribed / markets_discovered
    //   注意: markets_subscribed_total (新语义: token_map 条目) 与旧
    //         subscribed_markets_total (hub.token_count()/2) 各自独立, 两种口径均保留
    std::int64_t markets_discovered_total{0};

    // 直播员/比分匹配率 (从 ScoreSnapshotStore + market catalog 算):
    //   score_matched_total — catalog 中能在 score_store 找到对应 event_id 比分的 condition 数
    //   匹配率 = score_matched / markets_live  (2026-06-02 老板修: 分母应为【在打市场】非全市场。
    //     直播比分只覆盖 in-play 比赛, 拿它比赛前/期货等全市场无意义且偏低误导)
    std::int64_t score_matched_total{0};
    //   markets_live_total — catalog 中 gamma live=true (正在比赛) 的市场数 = 比分匹配率真分母
    //     (= 直播员该覆盖的市场; 0 = 当前无在打市场)
    std::int64_t markets_live_total{0};
};

// ============================================================
// ADR-040: TokenInfo (新增) — per-token 元数据
// token_id = CLOB asset_id (ERC-1155 链上 positionId; 协议事实, 非 vendor 私有字段)
// ============================================================
struct TokenInfo {
    std::string token_id;  // uint256 string (= CLOB asset_id; 协议事实)
    std::string outcome;   // "Yes"/"No"/"Clippers"/"Over 220.5" 等 (gamma outcomes[i])
    double price{0.0};     // gamma outcomePrices[i] / clob tokens[i].price
    bool winner{false};    // 结算后 true
};

// ============================================================
// /api/v1/market/{condition_id} (active/closed/resolved 三态分开)
// ADR-040 修正: 追加 condition_id (权威) / tokens[] / neg_risk_market_id / slug / polymarket_url
//   market_id 保留为 deprecated alias (= condition_id 值; 前端切换后 P2 移除)
// ============================================================
struct MarketInfo {
    bool found{false};
    std::string condition_id;  // 【权威·新增 ADR-040】bytes32 hex, 盘口主键
    // DEPRECATED: 用 condition_id; 前端切换后 P2 移除。值 = condition_id。
    std::string market_id;
    std::vector<TokenInfo> tokens;  // 【P0 新增 ADR-040】双 token 列表 — 下单链路入口
    double tick_size{0.0};
    double fee_rate{0.0};
    bool neg_risk{false};
    std::string neg_risk_market_id;  // 【P1 新增 ADR-040】negRisk 父合约 ID (可空)
    bool accepting_orders{false};
    bool active{false};
    bool closed{false};
    bool resolved{false};
    std::string source{"polymarket"};  // vendor 降为 source 标签 (非字段名前缀)
    std::int64_t as_of_ts_ns{0};
    // 前端 v3 盯盘: market → event 锚 (ADR-038 增量, G-FREEZE-W 只增不改名)
    std::string event_id;
    // ADR-040 Polymarket 超链接 (老板要求 P0)
    std::string slug;            // gamma slug 字段 (如 "nba-lal-bos-2026-05-29")
    std::string polymarket_url;  // = "https://polymarket.com/event/" + slug
    // 小冯 schema append (G-FREEZE-W 只增, 2026-05-29):
    //   sports_market_type — 盘口类型 (moneyline/spread/totals/outright/prop/series/unknown)
    //     来源: gamma market sportsMarketType 字段 (若无则由 groupItemTitle 推断)
    //   group_item_title   — gamma groupItemTitle (球队名/大小盘边, e.g. "LAL"/"Over 220.5")
    //     供前端 outcomes 列表标注 (比 outcome 更可读)
    std::string sports_market_type;  // moneyline/spread/totals/outright/prop/series/unknown
    std::string group_item_title;    // gamma groupItemTitle (可空)
    // 比赛时间 (2026-06-02 老板「前端要看几点开赛/是否进行中」, G-FREEZE-W 只增):
    //   gamma market.gameStartTime / endDate → Unix 秒 (0=缺)。供 grid 派生 game_state + 前端徽章。
    std::int64_t game_start_ts_sec{0};  // 真实开赛时刻
    std::int64_t end_ts_sec{0};         // 结束/结算窗口
    bool live{false};  // 在打 (= kickoff<=now; gamma 原生 live 不可靠故 discovery 算) → 比分匹配率真分母
};

// ============================================================
// /api/v1/score/{event_id} (Goalserve 比分快照; 前端 v3 盯盘)
// ============================================================
// source 固定 "goalserve" (vendor 降为 source 标签, 非字段名前缀)。
// Demo: 时间戳用 now() 减偏移模拟上游链路 (Demo 语义明确; 生产接入后由 provider 透传)。
struct EventScore {
    bool found{false};
    std::string event_id;
    std::string sport;
    std::string status;         // pregame / inplay / halftime / final
    std::string period;         // e.g. "Q3", "2H", "P1"
    std::int64_t clock_sec{0};  // 场内计时 (秒); 0 = 不适用或未知
    std::string home;
    std::string away;
    int home_score{0};
    int away_score{0};
    // 网球: home_score/away_score = 已赢【盘】数 (totalscore); 下面是全场已打【局】数 (s1+..+s5)。
    //   totals(总局 O/U)用和, spreads(让局)用差。非网球 = 0 (不适用)。G-FREEZE-W 只增。
    int games_home{0};
    int games_away{0};
    FourTs ts{};
    std::string source{"goalserve"};
    // A0 映射桥 (condition_id↔goalserve event): EventMatcher 锚定字段.
    //   league_id: Goalserve 联赛 id (从 GameScoreRecord.match_id.league_id 透传; 过滤匹配范围).
    //   kickoff_ts_sec: 开赛 Unix 秒 (= ts.event_ts_ns/1e9; inplay feed 只返进行中 event,
    //     start_ts<=now 不触发 R-20 clamp, 故 event_ts_ns 即真实 kickoff). 时间窗口锚定用.
    std::string league_id;
    std::int64_t kickoff_ts_sec{0};
    // inplay bet365 单源 de-vig 三边 fair — Goalserve home/away/draw 视角 (双边完整, 不丢信息)。
    //   InplayFeedThread 从 ParseResult.inplay_{home,away,draw}_fairs 填; -1=无 odds。
    //   ⚠ orientation: 这是 home/away 视角, 非 Polymarket YES 视角。paper_loop 按 yes_is_home
    //   翻成 YES-canonical 才进 game_row/特征 (away=YES 盘口若不翻 = 镜像反, 绝不可混淆)。
    double inplay_bet365_home_fair{-1.0};
    double inplay_bet365_away_fair{-1.0};
    double inplay_bet365_draw_fair{-1.0};
    // A-step-2 分局盘 sharp (2026-06-04 老板「第一局/第二局」, 小田设计; G-FREEZE-W 只增): 当前段 de-vig
    //   fair (MVP=tennis 当前盘 Set Winner)。home/away 视角 (同全场, paper_loop 按 yes_is_home 翻 YES)。
    //   seg_index = 当前段序号 (tennis 当前盘 1-5; 0=不适用); -1.0 = 当前段无 bet365 赔率 (不交易)。
    double inplay_seg_home_fair{-1.0};
    double inplay_seg_away_fair{-1.0};
    int inplay_seg_index{0};
    // 实时比分细节 (老板 2026-06-03「显示实时比分而非只赛点」): 网球逐盘比分 + 当前局分 + 发球方。
    //   set_summary: 各盘已打局数 "6-4 3-2"(home-away/盘, 空格分隔); 非网球留空。G-FREEZE-W 只增。
    std::string set_summary;
    std::string pts_home;   // 当前局得分 "0"/"15"/"30"/"40"/"AD" (网球; 非网球留空)
    std::string pts_away;
    int serving{-1};        // 发球方 0=home / 1=away / -1=未知 (网球)
    // v3 事件套利地基 (E1, 2026-06-03 老板「不做市/事件套利」+ 双架构评审):
    //   gs_state_code = Goalserve inplay info.state 5位【瞬时事件状态码】(11003=进球/11008=点球/
    //   11006=红牌/网球 11118=破发点/11119=赢局…)。已抓进 GameScoreRecord.gs_state_code, 此前
    //   ToEventScore 未透传 → 下游零消费。E1 纯透传【事实字段】(wire 层只产事实, 不产信号语义;
    //   状态码→事件类型→套利触发 的语义/逻辑归下游 小田/小梁, 非此层)。空=无。G-FREEZE-W 只增。
    std::string gs_state_code;
    // 终态标志 (2026-06-05 老板 a+b: 完赛必退订 WSS+订单簿 API): IsTerminal(rec.status) —
    //   Ended/Retired/Walkover/Abandoned/Cancelled/Postponed/Removed 全为 true。MapStatus 把
    //   Ended→"final" 但其余终态→"pregame"(歧义), 故单看 status 字符串无法区分"完赛"与"未开赛";
    //   此 bool 由 ToEventScore 在源头按 IsTerminal 填, 供 daemon 判定完赛 → 立即退订 (不经 paper_loop,
    //   不动 has_real_fair: 误判终态仍 status="pregame"→NotStarted→fail-closed 不交易)。G-FREEZE-W 只增。
    bool is_terminal{false};
};

// ============================================================
// /api/v1/quote/{condition_id} (量化参数快照; 前端 v3 盯盘)
// ============================================================
// 字段集对齐小梁量化部决议:
//   fair_value      — de-vig fair prob (去佣金后真实概率)
//   market_mid      — book microprice (best_bid+best_ask)/2 附近
//   edge_bps        — net edge (fair_value - market_mid) in bps
//   kelly_fraction  — Kelly 仓位比例 (已 cap; 来自 SizingCalculator 真实计算)
//   suggested_notional — 建议名义仓位 (USDC; 来自 SizingCalculator 真实计算)
//   signal_strength — α 信号强度 ∈ [0,1]
//   as_of_ts_ns     — 快照时刻 epoch ns (R-20)
//   predict_ok      — baseline fair 有效 (false → 看板灰显 fair)
//   model_as_of_ts_ns — feature PIT 锚 (不是快照读取时刻!)
//   (大模型 provenance model_id/model_kind/spec_version/model_confidence/model_calibrated/
//    fair_ci/advisory/model_conf 已砍 2026-06-05「砍掉大模型训练功能」)
struct QuoteParams {
    bool found{false};
    std::string market_id;
    double fair_value{0.0};          // 决策 fair = ResolveFair 输出 p_fair (sharp 优先; 非估计器中间值)
    std::int8_t fair_src{0};         // 决策 fair 选源: 0市场devig/1派生/2sharp/3score-prior/4ml (FairSrc 序)
    double market_mid{0.0};          // book mid (microprice)
    double edge_bps{0.0};            // net edge in basis points
    double kelly_fraction{0.0};      // Kelly 仓位比例 (SizingCalculator 真实计算)
    double suggested_notional{0.0};  // 建议名义仓位 (USDC; SizingCalculator 真实计算)
    double signal_strength{0.0};     // α 信号强度
    std::int64_t as_of_ts_ns{0};

    // ---- 决策 fair 来源标记 (baseline; 大模型 provenance 已砍 2026-06-05) ----
    bool predict_ok{false};             // baseline fair 有效 (false → 灰显 fair)
    std::int64_t model_as_of_ts_ns{0};  // feature PIT 锚 (非快照读取时刻!)
    // ---- 调试可观测 (老雷 2026-06-01; G-FREEZE-W append-only) — fair 来源分解 ----
    //   sharp_fair = Goalserve bet365 in-play de-vig 真胜率共识 (盈利修复后 fair_value 锚到它)。
    //   前端用 fair_value(决策) / sharp_fair(源) / market_mid(PM 市场) 三栏看 edge 来源是否正当。
    double sharp_fair{-1.0};         // g_bm_inplay_fair; -1 = 无 bet365 odds (未映射/无 odds plan)
    bool devig_ok{false};           // de-vig 成功? (区分「de-vig 失败」vs「edge 不足」)
    double g_time_x_lead{0.0};      // 时间感知领先 = score_diff×(1−time_frac) (映射上+in-play 才非0)
    std::int64_t joint_as_of_ts_ns{0};  // 联合新鲜度 = min(score,book).as_of (映射连通时 >0)
    // 真实赔率新鲜度 (2026-06-05 老板「现在就换成真实赔率新鲜度」): 订单簿 data_source_ts (WSS 版本时刻,
    //   非快照发布的 as_of=now)。now − 它 = 市场赔率有多旧 (WSS 健康时亚秒; WSS 死时会涨, 直观暴露断流)。
    std::int64_t data_source_ts_ns{0};
    // GS sharp 赔率新鲜度 (2026-06-05 老板「赔率延迟放合适位置」): = game_row.data_source_ts_ns
    //   (驱动 sharp fair 的那一版 Goalserve inplay 赔率 updated_ts); now−它 = sharp 赔率多旧 (3s 门管的就是它)。
    std::int64_t sharp_data_source_ts_ns{0};
    // sharp fair 时序 (2026-06-05 老板「方向真值=赔率源 sharp; 盘口趋势/line movement 一阶导」; QuoteFeatures
    //   g_sharp_* 透传; G-FREEZE-W append-only)。盯盘页用它画服务端持久可信的 sharp 轨迹/收敛发散 (不再
    //   前端刷新归零)。有效性看 sharp_samples≥2 (json NaN→0, 故不能凭 velocity=0 判无数据)。
    double sharp_velocity{0.0};       // sharp 速度 prob/sec (+升 −降; line movement 方向)
    double sharp_conv_rate{0.0};      // 收敛率 prob/sec (<0 市场向 sharp 收敛/>0 发散)
    double sharp_vol{0.0};            // sharp 抖动度 RMS (高=噪声多于真移动)
    std::int32_t sharp_samples{0};    // sharp 环窗口内样本数 (有效性闸: <2 则上面无意义)
    // 持仓管理 Stage 2 sizing 乘子 (观测, 老板 2026-06-05; G-FREEZE-W append-only): 实际乘到 |target| 的值。
    double lifecycle_mult{1.0};       // sharp 生命周期乘子 ∈[floor,1] (缩噪声; per-market)
    double clv_mult{1.0};             // CLV sizing 乘子 ∈[floor,max] (>1=放大; 全局)
    double rolling_clv_mean{0.0};     // 全局滚动 CLV 均值 (prob; 正=入场优于 fair; json NaN→0, 看 n)
    std::int32_t rolling_clv_n{0};    // 滚动 CLV 样本数 (有效性: <min 则 clv_mult=1)
    double dd_mult{1.0};              // DD→target 乘子 ∈[0,1] (账户级回撤去险; 全局; §4.1)
    double corr_mult{1.0};            // 相关性折扣乘子 ∈[floor,1] (同赛事 ρ 加权占用; per-event; §4.1)
    // ---- 持仓管理决策可视 (老板 2026-06-09「调试持仓逻辑, 盯盘下面补观测」; G-FREEZE-W append-only) ----
    //   盯盘直接看"控制器把仓位往哪推 / 保留价 vs 市价是否可成交 / 是否已决出该锁利"。
    double target_signed_notional{0.0};  // 目标净仓位 (signed pUSD; +多YES −多NO=空YES; 控制器目标)
    double reservation_buy_px{0.0};      // 买入保留价上界 (best_ask≤它才买; 0=无)
    double reservation_sell_px{0.0};     // 卖出保留价下界 (best_bid≥它才卖; 0=无)
    double required_margin{0.0};         // reservation 安全边际
    double pos_net_qty{0.0};             // 当前净持仓 (whole pUSD; 正=净多 YES)
    double pos_avg_entry{0.0};           // 持仓加权均入价 (YES 边)
    double game_decided_sign{0.0};       // +1=YES方已决出(必赢)/−1=NO方已决出(YES必输)/0=未决出
    bool near_end{false};                // 末段 phase_frac>0.85
};

// ============================================================
// /api/v1/book/{token_id} (ADR-040 per-token 化; microprice/spread/imbalance 后端算好)
// ============================================================
// 单档报价 (深度阶梯一档); price/size 均 double。
struct BookLevel {
    double price{0.0};
    double size{0.0};
};

// ADR-040: BookSnapshot per-token 化
//   token_id / condition_id / outcome 为权威新增字段
//   market_id 保留为 deprecated alias (= condition_id 值; 前端切换后 P2 移除)
//   sequence_no / gap_count / imbalance 语义变为 per-token (本 token 的序列号/gap/单边 imbalance)
struct BookSnapshot {
    bool found{false};
    std::string token_id;      // 【权威·新增 ADR-040】asset_id — orderbook 真实粒度
    std::string condition_id;  // 【新增 ADR-040】归属盘口 (UI 分组用)
    std::string outcome;       // 【新增 ADR-040】"Yes"/"No"/球队名 — 这是哪一边的报价
    // DEPRECATED: 用 condition_id; 前端切换后 P2 移除。值 = condition_id。
    std::string market_id;
    double best_bid{0.0};
    double best_ask{0.0};
    double microprice{0.0};
    double spread{0.0};
    double imbalance{0.0};        // 本 token 单边 imbalance (∈ [-1, 1]; per-token, 不再合并双边)
    std::int64_t sequence_no{0};  // 本 token 的 WSS 序列号 (per asset_id; per-token)
    std::int64_t gap_count{0};    // 本 token 的 gap (per-token)
    std::string wss_state{"unknown"};
    FourTs ts{};
    std::string source{"polymarket"};
    // 深度阶梯 (best 在前; 可空 = 仅 L1 摘要)。前端深度条可视化消费。
    std::vector<BookLevel> bids{};
    std::vector<BookLevel> asks{};
    // 小冯 schema append (G-FREEZE-W 只增, 2026-05-29):
    //   last_trade_price — CLOB price_change 事件携带的最新成交价 ∈ [0,1]; 0 = 未知/尚未收到
    //   来源: CLOB market channel event_type=="price_change" 的 price 字段 (字符串→double)
    double last_trade_price{0.0};
};

// ============================================================
// ADR-040: BinaryMarketBookView (新增) — 看板主入口
//   condition_id → 双 token book + cross_spread (后端算好, 1 RTT 拿齐)
//   cross_spread = token0.best_ask + token1.best_ask - 1.0 (等效 vig)
//   token0/token1: index 对齐 gamma outcomes[0]/outcomes[1]; outcome 字段自带语义
// ============================================================
struct BinaryMarketBookView {
    bool found{false};
    std::string condition_id;
    BookSnapshot token0;       // outcomes[0] book (含 token_id + outcome)
    BookSnapshot token1;       // outcomes[1] book (含 token_id + outcome)
    double cross_spread{0.0};  // = token0.best_ask + token1.best_ask - 1.0 (等效 vig)
    FourTs ts{};               // 取两 token 较旧 as_of (保守 staleness)
};

// ============================================================
// StateProvider — 观测只读契约 (const 方法, 线程安全读)
// ============================================================
// 实现侧约束 (R-12): 所有方法在 debug_api 线程调用, 只读 atomic / double-buffer
// front snapshot, 绝不调用 RM::evaluate / signer / emit, 绝不持热路径锁 > 100us。
// 单特征健康 (老雷 2026-06-01 可观测): 当前 fv_hub 全市场快照逐列聚合。
struct FeatureHealthRow {
    int index{0};            // 0..109
    std::string name;        // MlFeature 名 (g_score_diff / b_mid / ...)
    int populated{0};        // 非 null 样本数
    int nonzero{0};          // 非零样本数
    double min{0.0};
    double max{0.0};
    double mean{0.0};
    std::string status;      // "healthy" / "dead"(全0/null) / "const"(无方差)
};
struct FeatureHealthReport {
    int n_records{0};        // 参与聚合的市场快照数
    int dead{0};
    int constant{0};
    int healthy{0};
    std::vector<FeatureHealthRow> rows;
};

// 映射状态 (老雷 2026-06-01 可观测): condition ↔ Goalserve event 桥接实况。
struct MappingMarketRow {
    std::string condition_id;
    std::string team0;            // YES 队 (或 title 拆出)
    std::string team1;
    bool is_draw{false};
    bool matched{false};
    std::string inplay_match_id;  // 匹配上的 Goalserve event id
    double match_confidence{0.0}; // 双队 overlap 和 (越高越确信)
};
struct MappingLiveGame {         // Goalserve 当前 live 比赛 (score_store 候选)
    std::string event_id;
    std::string home;
    std::string away;
    std::string sport;
    std::string status;          // inplay/halftime/final
    int home_score{0};
    int away_score{0};
};
// 无赔率源 market 记录 (2026-06-04 老板「源头pass无赔率源, 匹配不上的记录, api可查」)。
//   reason: "no_goalserve_match" = 无任何 Goalserve live 候选可匹配 (覆盖缺口或名字不对);
//           "matched_no_sharp"   = 匹配到 Goalserve event 但该 event 无 bet365 inplay 赔率 (-1)。
//   两类均无 sharp 直播赔率源 → 源头 pass 不订阅。best_* = 最接近候选 + 相似分 (诊断覆盖 vs bug)。
struct MappingNoSharpRow {
    std::string condition_id;
    std::string team0;
    std::string team1;
    std::string sport;
    std::string reason;          // no_goalserve_match / matched_no_sharp
    std::string best_home;       // 最接近 Goalserve 候选 home (no_goalserve_match 时填)
    std::string best_away;
    double best_score{0.0};      // 最佳双队相似分 ∈[0,1]; -1 = 未算
    std::string kickoff_state;   // 在打 / 赛前 / 无ts
    std::string matched_event_id;// matched_no_sharp 时的 Goalserve event id
};
struct MappingStatusReport {
    int total_markets{0};        // 有匹配输入的 market 数
    int matched{0};              // 成功映射数
    int live_games{0};           // Goalserve 当前 live 候选数
    std::vector<MappingMarketRow> markets;   // 仅含 matched 或 近似 (诊断)
    std::vector<MappingLiveGame> games;      // Goalserve live 候选
    // G-FREEZE-W 只增 (2026-06-04 老板「无赔率源 market 记录, api可查」):
    int matched_with_sharp{0};   // 匹配且有 bet365 赔率 (真正可订阅交易)
    int matched_no_sharp{0};     // 匹配但无 bet365 赔率 (源头 pass)
    int no_match{0};             // 无 Goalserve 候选可匹配 (源头 pass)
    std::vector<MappingNoSharpRow> no_sharp;  // 无赔率源明细 (源头 pass 清单, api可查)
};

class StateProvider {
public:
    virtual ~StateProvider() = default;

    // 特征健康 (老雷 2026-06-01 可观测; 默认空 → stub/未接 fv_hub 返回空报告)。
    virtual FeatureHealthReport feature_health() const { return {}; }
    // 映射状态 (老雷 2026-06-01 可观测; 默认空 → 未接 daemon push 返回空)。
    virtual MappingStatusReport mapping_status() const { return {}; }
    // 账户级现金/估值 (老雷 2026-06-01 凯利评审; 默认空 has_data=false → stub/未注入 paper_loop 灰显)。
    virtual AccountSnapshot account_snapshot() const { return {}; }
    // 成交流水 (2026-06-04 老板「看懂买卖价」; 默认空 → stub/未注入 paper_loop 返回空)。
    //   market 非空 → 只返该 condition 的成交 (盯盘按盘看, 不受全局环churn丢失)。
    virtual std::vector<FillView> fills(const std::string& market = "") const { (void)market; return {}; }

    // R-11: 全局运行模式 (build-time 锁定, 运行时不可切)
    virtual ExecMode mode() const = 0;

    virtual std::vector<HoldingView> positions() const = 0;
    virtual std::vector<PnlBucket> pnl_timeseries(std::int64_t window_sec, std::int64_t bucket_sec) const = 0;
    virtual PnlAttribution pnl_attribution() const = 0;
    virtual std::vector<RiskRejectRow> risk_rejects() const = 0;
    virtual PaperGate paper_gate() const = 0;
    virtual MetricsSnapshot metrics() const = 0;
    virtual MarketInfo market(const std::string& condition_id) const = 0;

    // ADR-040: book() 语义改为按 token_id 查单边 (策略/调试旁路)
    // 签名不变但语义 = per-token; 入参应为 token_id (不再是 condition_id)
    virtual BookSnapshot book(const std::string& token_id) const = 0;

    // ADR-040 新增: 按 condition_id 返回双 token book view + cross_spread (看板主入口)
    virtual BinaryMarketBookView book_pair(const std::string& condition_id) const = 0;

    // 前端 v3 盯盘新增 (ADR-038 增量, G-FREEZE-W 只增不改名)
    virtual EventScore score(const std::string& event_id) const = 0;
    // scores_all — 全部 live 比分快照 (2026-06-03 老板「人盯盘看比分/赛点/进度」+ WSS 实时推):
    //   per-event score() 按 PM event_id 查, 但 score_store 按 Goalserve inplay_match_id 做 key → 永远
    //   found=false → SSE scores 通道空。此法直接枚举 score_store snapshot, 绕开 key 不匹配 → 全 live 赛
    //   事比分都能推 (盯盘看板用)。G-FREEZE-W 只增。
    virtual std::vector<EventScore> scores_all() const = 0;
    virtual QuoteParams quote_params(const std::string& condition_id) const = 0;
    // 数据源标识 (返回 "demo"/"stub"/"live"; 供 /status DEMO 标记; 老钱红线)
    virtual const char* data_source() const = 0;

    // 小冯 schema append (G-FREEZE-W 只增, 2026-05-29):
    //   events() — 返回当前已发现的活跃体育 event 列表 (供前端 Event 层导航)
    //   live 模式: 由 RealStateProvider 从 gamma /events 发现结果填充
    //   stub/demo 模式: 返回空列表 (前端 fallback)
    virtual std::vector<EventInfo> events() const = 0;
};

// StubStateProvider — MVP 默认实现, 返回结构合法的空/0 值。
// 各模块 owner 提供真实 double-buffer snapshot 后, 由 main 注入替换。
class StubStateProvider final : public StateProvider {
public:
    explicit StubStateProvider(ExecMode m) : mode_(m) {}

    ExecMode mode() const override { return mode_; }

    std::vector<HoldingView> positions() const override { return {}; }
    std::vector<PnlBucket> pnl_timeseries(std::int64_t, std::int64_t) const override { return {}; }
    PnlAttribution pnl_attribution() const override { return {}; }
    std::vector<RiskRejectRow> risk_rejects() const override { return {}; }
    PaperGate paper_gate() const override { return {}; }
    MetricsSnapshot metrics() const override { return {}; }

    MarketInfo market(const std::string& condition_id) const override {
        MarketInfo m;
        m.found = false;  // stub: 无市场目录, 一律 not found
        m.condition_id = condition_id;
        m.market_id = condition_id;  // deprecated alias
        return m;
    }

    // ADR-040: book() 按 token_id 查单边 (stub: found=false)
    BookSnapshot book(const std::string& token_id) const override {
        BookSnapshot b;
        b.found = false;  // stub: 无 orderbook feed 接入
        b.token_id = token_id;
        b.condition_id = token_id;  // stub: 无映射, fallback
        b.market_id = token_id;     // deprecated alias
        return b;
    }

    // ADR-040 新增: book_pair — stub 返回 found=false 双空 book
    BinaryMarketBookView book_pair(const std::string& condition_id) const override {
        BinaryMarketBookView v;
        v.found = false;
        v.condition_id = condition_id;
        v.token0.found = false;
        v.token0.condition_id = condition_id;
        v.token0.market_id = condition_id;  // deprecated alias
        v.token1.found = false;
        v.token1.condition_id = condition_id;
        v.token1.market_id = condition_id;  // deprecated alias
        v.cross_spread = 0.0;
        return v;
    }

    // 前端 v3 盯盘新增 (stub: 返回 found=false 合法空值)
    EventScore score(const std::string& event_id) const override {
        EventScore s;
        s.found = false;
        s.event_id = event_id;
        return s;
    }

    std::vector<EventScore> scores_all() const override { return {}; }

    QuoteParams quote_params(const std::string& condition_id) const override {
        QuoteParams q;
        q.found = false;
        q.market_id = condition_id;
        q.predict_ok = false;
        return q;
    }

    const char* data_source() const override { return "stub"; }

    // events: stub 返回空列表
    std::vector<EventInfo> events() const override { return {}; }

private:
    ExecMode mode_;
};

}  // namespace stcpp::debug_api
