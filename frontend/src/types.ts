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
  source: string;
  event_ts: number;
  data_source_ts: number;
  ingestion_ts: number;
  score_as_of_ts: number;
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
  market_mid: number;
  edge_bps: number;
  kelly_fraction: number;
  suggested_notional: number;
  signal_strength: number;
  /** @deprecated 旧别名, 改用 model_confidence */
  model_conf: number;
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

  // --- AI provenance 字段 (小邓 XD 红线) ---
  /** 模型 ID, e.g. "demo-fv-v0" */
  model_id: string;
  /** 模型种类, e.g. "stub" / "lgbm" / "nn" */
  model_kind: string;
  /** 特征规格版本, e.g. "ml-feature-spec-v0.1" */
  spec_version: string;
  /** 模型置信度 [0,1] (XD-1 三位一体) */
  model_confidence: number;
  /** 是否已完成校准 (XD-4 降级门控) */
  model_calibrated: boolean;
  /** 公允价置信区间下界 */
  fair_ci_lower: number;
  /** 公允价置信区间上界 */
  fair_ci_upper: number;
  /** 预测是否正常 (XD-5: false → 不画 edge/kelly/notional) */
  predict_ok: boolean;
  /** 仅供参考模式 (XD-3: true → advisory 角标 + 不下单) */
  advisory: boolean;
  /** 模型快照时间戳 epoch_ns */
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
  model_confidence?: number;
  advisory?: boolean;
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
  sport: string | null;
  live: boolean;  // gamma event.live=true (正在比赛; 前端默认过滤用)
  score: Score | null;
  conditions: ConditionData[];
}
