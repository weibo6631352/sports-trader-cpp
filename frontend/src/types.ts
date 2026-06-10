/**
 * types.ts — 所有后端 wire 类型定义
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 对齐 ADR-038 debug_api schema + ADR-040 book_pair 端点
 * 4 时间戳契约: event_ts / data_source_ts / ingestion_ts / as_of_ts (epoch_ns)
 */

// ---------- 通用 ----------

export interface Token {
  token_id: string;
  outcome: string;
  price: number;
  winner: boolean;
}

// ---------- /healthz ----------

export interface Healthz {
  ok: boolean;
  threads: Record<string, string>;
  uptime_sec: number;
  as_of_ts: number;
}

// ---------- /status ----------

export interface WssConnected {
  clob: boolean;
  user_channel: boolean;  // 预留: 真钱下单用户通道 (paper 不用)
}

export interface Status {
  state: string;
  mode: string;
  data_source: string;
  wss_connected: WssConnected;
  signals_active_count: number;
  positions_count: number;
  rm_rejects_last_60s: number;
  uptime_sec: number;
  as_of_ts: number;
}

// ---------- /api/v1/positions ----------

export interface Position {
  market_id: string;
  outcome: string;
  net_qty: number;
  avg_entry_price: number;
  mark_price: number;
  pnl_realized: number;
  pnl_unrealized: number;
  as_of_ts: number;
}

export interface Positions {
  mode: string;
  as_of_ts: number;
  positions: Position[];
}

// ---------- /api/v1/fills (成交流水: 多少价买/卖 + 本笔已实现) ----------

export interface Fill {
  market_id: string;     // condition_id (前端用市场缓存映射成人读队名)
  side: 'buy' | 'sell';
  outcome: 'YES' | 'NO';
  is_close: boolean;
  price: number;
  size_usdc: number;
  realized: number;      // 本笔已实现 (卖=（卖价−均入）×量; 买=0)
  cum_realized: number;  // 成交后累计已实现
  fair: number;          // 成交刻模型 fair (被交易边) — 模型诊断: 声称 edge=fair−price
  mark: number;          // 成交刻市场 mark — 模型诊断: 模型偏差=fair−mark
  fee: number;           // 本笔手续费 (老板「逐笔体现」): size×fee_coef×p×(1−p)
  as_of_ts: number;
}

export interface Fills {
  mode: string;
  fills: Fill[];         // 最新在前
}

// ---------- /api/v1/pnl/timeseries ----------

export interface PnlBucket {
  bucket_start_ts: number;
  cum_net_pnl: number;
  realized: number;
  unrealized: number;
  fee: number;
  gas: number;
  n_trades: number;
}

export interface PnlTimeseries {
  mode: string;
  window_sec: number;
  bucket_sec: number;
  as_of_ts: number;
  buckets: PnlBucket[];
}

// ---------- /api/v1/pnl/attribution ----------

export interface PnlWaterfall {
  gross: number;
  fee: number;
  gas: number;
  slippage: number;
  spread: number;
  net: number;
}

export interface PerMarketPnl {
  market_id: string;
  net_pnl: number;
}

export interface PnlAttribution {
  mode: string;
  as_of_ts: number;
  waterfall: PnlWaterfall;
  per_market: PerMarketPnl[];
}

// ---------- /api/v1/account (账户级现金/估值; 2026-06-01 凯利评审) ----------

export interface AccountDetail {
  bankroll_initial: number;
  cash_available: number;
  position_mtm: number;
  equity: number;                // 展示净值 (microprice 口径)
  equity_conservative: number;   // best_bid 口径 (= kelly_bankroll)
  cum_realized_pnl: number;
  cum_unrealized_pnl: number;
  cum_fee_paid: number;
  net_pnl: number;
  return_pct: number;            // 小数 (fmtPct ×100)
  max_drawdown: number;          // ∈[0,1]
  sharpe: number;
  kelly_bankroll: number;        // 实际喂凯利的 bankroll
  kelly_bankroll_basis: string;  // 口径说明 (动态 vs 静态)
  open_positions: number;
  // CLV 验真入场 edge (2026-06-10 金标准): >0=入场打败收盘线=edge真; <0=结构性逆选。~50 笔统计显著。
  clv_close_mean?: number;       // 平均 CLV vs 收盘线 (prob; 低方差领先指标)
  clv_settle_mean?: number;      // 平均 CLV vs 0/1 结算 (≈realized)
  clv_positive_rate?: number;    // 入场优于收盘线命中率 ∈[0,1]
  clv_n?: number;                // 已结算计入 CLV 的成交数 (<~20 噪声大)
}

export interface Account {
  mode: string;
  has_data: boolean;
  as_of_ts: number;
  account: AccountDetail;
}

// ---------- /api/v1/risk/rejects ----------

export interface RiskReject {
  intent_ref: string;
  market_id: string;
  reason_code: string;
  side: string;
  size: number;
  price: number;
  rejected_ts: number;
  sub_reason?: string;  // INVALID_INTENT 细分码 (后端 2026-06-10 暴露; 仅 INVALID_INTENT 时有值)
}

export interface RiskRejects {
  mode: string;
  as_of_ts: number;
  rejects: RiskReject[];
}

// ---------- /api/v1/gate/paper ----------

export interface GatePaper {
  mode: string;
  as_of_ts: number;
  window_days: number;
  has_data: boolean;
  n_trades: number;
  positive_day_ratio: number;
  sharpe: number;
  sharpe_se: number;
  p_value: number;
  hit_rate: number;
  max_drawdown: number;
  prelim_pass: boolean;
  confirm_pass: boolean;
}

// ---------- /api/v1/market/{id} ----------

export interface Market {
  mode: string;
  as_of_ts: number;
  found: boolean;
  condition_id: string;
  market_id: string;
  tick_size: number;
  fee_rate: number;
  neg_risk: boolean;
  neg_risk_market_id: string;
  accepting_orders: boolean;
  active: boolean;
  closed: boolean;
  resolved: boolean;
  source: string;
  event_id: string;
  slug: string;
  polymarket_url: string;
  // 2026-05-31 接口核查: 后端 endpoint_market.cpp:75-77 已输出, 前端原未声明 (信息丢失)。
  sports_market_type?: string;
  group_item_title?: string;
  tokens: Token[];
}

// ---------- /api/v1/book_pair/{conditionId} (ADR-040) ----------

export interface OrderLevel {
  price: number;
  size: number;
}

export interface HalfBook {
  found: boolean;
  token_id: string;
  condition_id: string;
  outcome: string;
  market_id: string;
  best_bid: number;
  best_ask: number;
  microprice: number;
  spread: number;
  imbalance: number;
  sequence_no: number;
  gap_count: number;
  wss_state: string;
  source: string;
  event_ts: number;
  data_source_ts: number;
  ingestion_ts: number;
  book_as_of_ts: number;
  bids: OrderLevel[];
  asks: OrderLevel[];
}

export interface BinaryMarketBookView {
  mode: string;
  as_of_ts: number;
  found: boolean;
  condition_id: string;
  cross_spread: number;
  event_ts: number;
  data_source_ts: number;
  ingestion_ts: number;
  as_of_ts_ns: number;
  token0: HalfBook;
  token1: HalfBook;
}

// ---------- /api/v1/score/{eventId} ----------

export interface Score {
  mode: string;
  as_of_ts: number;
  found: boolean;
  event_id: string;
  sport: string;
  status: string;
  period: string;
  clock_sec: number | null;
  home: string;
  away: string;
  home_score: number | null;
  away_score: number | null;
  games_home?: number | null;       // 网球: 全场已打局数 (totals 用); 非网球=0
  games_away?: number | null;
  set_summary?: string;             // 网球逐盘比分 "6-4 3-2" (实时比分; 非网球空)
  pts_home?: string;                // 当前局分 "0"/"15"/"30"/"40"/"AD"
  pts_away?: string;
  serving?: number;                 // 发球方 0=home/1=away/-1
  sharp_home_fair?: number | null;  // in-play bet365 de-vig home 胜率 (-1=无 odds); 套利信号锚
  sharp_away_fair?: number | null;
  source: string;
  event_ts: number;
  data_source_ts: number;
  ingestion_ts: number;
  score_as_of_ts?: number;
}

// ---------- /api/v1/quote/{conditionId} ----------

// 映射状态 (老雷 2026-06-01 可观测): /api/v1/mapping/status
export interface MappingMarketRow {
  condition_id: string;
  team0: string;
  team1: string;
  is_draw: boolean;
  matched: boolean;
  inplay_match_id: string;
  match_confidence: number;
}
export interface MappingLiveGame {
  event_id: string;
  home: string;
  away: string;
  sport: string;
  status: string;
  home_score: number;
  away_score: number;
}
export interface MappingStatus {
  mode: string;
  as_of_ts: number;
  total_markets: number;
  matched: number;
  live_games: number;
  markets: MappingMarketRow[];
  games: MappingLiveGame[];
}

// 特征健康 (老雷 2026-06-01 可观测): /api/v1/features/health
export interface FeatureHealthRow {
  i: number;
  name: string;
  populated: number;
  nonzero: number;
  min: number;
  max: number;
  mean: number;
  status: 'healthy' | 'dead' | 'const';
}
export interface FeatureHealth {
  mode: string;
  as_of_ts: number;
  n_records: number;
  dead: number;
  const: number;
  healthy: number;
  total: number;
  rows: FeatureHealthRow[];
}

export interface Quote {
  mode: string;
  as_of_ts: number;
  found: boolean;
  market_id: string;

  // --- 真实 sizing 字段 ---
  fair_value: number;
  /** 决策 fair 实际选用的来源 (后端 fair_src 直读, 非前端反推): sharp/derivative/score_prior/market_devig/ml */
  fair_src?: string;
  /** 候选 fair 全集 (列出所有源 + 在 fair_src 标记正在用的; -1 = 该源对此盘不适用) */
  fair_cands?: { devig: number; sharp: number; score_prior: number; derivative: number };
  market_mid: number;
  edge_bps: number;
  kelly_fraction: number;
  suggested_notional: number;
  signal_strength: number;
  quote_as_of_ts: number;

  // --- 调试可观测: fair 来源分解 (老雷 2026-06-01) ---
  /** sharp bet365 in-play de-vig 共识胜率 (盈利修复后 = fair 锚源); -1 = 无 odds/未映射 Goalserve */
  sharp_fair?: number;
  /** de-vig 是否成功 (区分 de-vig 失败 vs edge 不足) */
  devig_ok?: boolean;
  /** 时间感知领先 = score_diff×(1−time_frac); 映射上+in-play 才非 0 */
  g_time_x_lead?: number;
  /** 联合新鲜度 epoch_ns = min(score,book).as_of; 映射连通时 >0 */
  joint_as_of_ts?: number;
  /** 真实赔率新鲜度 epoch_ns = 订单簿 WSS 版本时刻 (data_source_ts); now−它 = 市场赔率多旧 */
  data_source_ts?: number;
  /** GS sharp 赔率新鲜度 epoch_ns = inplay 赔率版本时刻; now−它 = sharp(bet365 de-vig) 赔率多旧 (3s 门管的就是它) */
  sharp_data_source_ts?: number;
  // --- sharp fair 时序 (老板 2026-06-05「方向真值=赔率源 sharp; line movement 一阶导」; 服务端 SharpFairTrack 派生) ---
  /** sharp 速度 prob/sec (+升 −降; line movement 方向)。有效性看 sharp_samples≥2 (NaN 经后端 json→0) */
  sharp_velocity?: number;
  /** 收敛率 prob/sec (<0 市场向 sharp 收敛=持仓变对 / >0 发散=变错)。服务端权威值, 优于前端自攒 */
  sharp_conv_rate?: number;
  /** sharp 抖动度 RMS (高=噪声多于真移动) */
  sharp_vol?: number;
  /** sharp 时序环窗口内样本数 (有效性闸: <2 则 velocity/conv_rate 无意义) */
  sharp_samples?: number;
  // --- 持仓管理 Stage 2 sizing 乘子 (观测; 实际乘到 |target| 的值, 方向仍归 sharp) ---
  /** sharp 生命周期乘子 ∈[floor,1] (Vol 稳定性×发散谨慎; 缩噪声; per-market) */
  lifecycle_mult?: number;
  /** CLV sizing 乘子 ∈[floor,max] (滚动 CLV 调; >1=放大; 全局系统级) */
  clv_mult?: number;
  /** 全局滚动 CLV 均值 (prob; 正=入场优于 fair; NaN 经 json→0, 看 rolling_clv_n) */
  rolling_clv_mean?: number;
  /** 滚动 CLV 样本数 (<min 则 clv_mult=1 fail-open) */
  rolling_clv_n?: number;
  /** DD→target 乘子 ∈[0,1] (账户级回撤去险; 只压加仓; 全局; §4.1) */
  dd_mult?: number;
  /** 相关性折扣乘子 ∈[floor,1] (同赛事 ρ 加权占用; per-event; §4.1) */
  corr_mult?: number;

  // --- 持仓管理决策可视 (老板 2026-06-09「调试持仓逻辑, 盯盘下面补观测」) ---
  /** 控制器目标净仓位 (signed pUSD; +多YES −多NO=空YES)。0=不开新仓(只减/平) */
  target_signed_notional?: number;
  /** 买入保留价上界 (best_ask≤它才买; 0=无) */
  reservation_buy_px?: number;
  /** 卖出保留价下界 (best_bid≥它才卖; 0=无) */
  reservation_sell_px?: number;
  /** reservation 安全边际 (CI 半宽与 floor 取大) */
  required_margin?: number;
  /** 当前净持仓 (whole pUSD; 正=净多 YES) */
  pos_net_qty?: number;
  /** 持仓加权均入价 (YES 边) */
  pos_avg_entry?: number;
  /** +1=YES方已决出(必赢)/−1=NO方已决出(YES必输)/0=未决出 → must_win_lock 触发根因 */
  game_decided_sign?: number;
  /** 末段 phase_frac>0.85 */
  near_end?: boolean;

  // --- 决策 fair 来源标记 (大模型 provenance model_id/kind/confidence/calibrated/fair_ci/advisory
  //     已砍 2026-06-05「砍掉大模型训练功能」) ---
  /** baseline fair 有效 (false → 不画 edge/kelly/notional) */
  predict_ok: boolean;
  /** feature PIT 锚 epoch_ns */
  model_as_of_ts: number;
}

// ---------- /api/v1/events ----------

export interface EventSummary {
  event_id: string;
  slug: string;
  title: string;
  sport: string;
  neg_risk_market_id: string;
  live?: boolean;  // gamma event.live=true (正在比赛)
  icon_url?: string;  // gamma 赛事图
  condition_ids: string[];
}

export interface EventsResponse {
  events: EventSummary[];
  as_of_ts: number;
  data_source?: string;  // 2026-05-31 接口核查: 后端 endpoint_events.cpp:38 输出, 前端原未声明。
}

// ---------- /api/v1/grid (全市场轻量摘要批量端点, 老雷 2026-06-02) ----------

/** 单 condition 顶档摘要 (折叠态摘要行用; 全档深度仍走 /book_pair 按需) */
export interface GridMarket {
  condition_id: string;
  book_found: boolean;
  best_bid?: number;
  best_ask?: number;
  cross_spread?: number;
  event_ts?: number;
  ingestion_ts?: number;
  quote_found: boolean;
  fair?: number;
  market_mid?: number;
  edge_bps?: number;
  sharp_fair?: number;
  kickoff_ts?: number;   // 开赛时刻 (epoch ns; 0/缺=未知)
  end_ts?: number;       // 结束/结算窗口 (epoch ns)
  game_state?: string;   // 后端派生: pregame/inplay/ended/resolved/unknown
  title?: string;        // 可读盘口名 (gamma groupItemTitle, 如 "Game 1 Winner")
  outcome0?: string;     // token0(YES) 边名 (球员/队/Over/Yes)
  outcome1?: string;     // token1(NO) 边名
}

export interface GridResponse {
  mode: string;
  as_of_ts: number;
  count: number;
  markets: GridMarket[];
}

// ---------- 渲染用聚合类型 ----------

/** condition 顶档摘要 (来自 /grid; 与 book/quote 全档分离, 避免 2s 刷新覆盖展开的全档) */
export interface ConditionSummary {
  bid: number | null;
  ask: number | null;
  edgeBps: number | null;
  fair: number | null;
  sharp: number | null;       // 赔率源 sharp (bet365 de-vig YES 胜率; 方向真值锚, 老板 2026-06-05)
  mid: number | null;         // 市场 microprice mid (算 sharp 偏离 Δ=sharp−mid 用)
  eventTs: number | null;
  kickoffTs: number | null;   // 开赛时刻 (epoch ns)
  gameState: string | null;   // pregame/inplay/ended/resolved/unknown
  title: string | null;       // 可读盘口名 (groupItemTitle)
  outcome0: string | null;    // token0 边名
  outcome1: string | null;    // token1 边名
}

export interface ConditionData {
  conditionId: string;
  posRows: Position[];
  market: Market | null;
  book: BinaryMarketBookView | null;
  quote: Quote | null;
  summary: ConditionSummary | null;  // /grid 顶档摘要 (折叠态摘要行)
  rejectRows: RiskReject[];
  perMarketPnl: number | null;
}

export interface EventGroup {
  eventId: string | null;
  eventSlug: string | null;
  eventTitle: string | null;
  iconUrl: string | null;  // gamma 赛事图
  sport: string | null;
  live: boolean;  // gamma event.live=true (正在比赛; 前端默认过滤用)
  score: Score | null;
  conditions: ConditionData[];
}
