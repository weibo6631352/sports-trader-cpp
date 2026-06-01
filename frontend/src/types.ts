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
  sports_api: boolean;
  clob: boolean;
  user_channel: boolean;
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
  condition_ids: string[];
}

export interface EventsResponse {
  events: EventSummary[];
  as_of_ts: number;
  data_source?: string;  // 2026-05-31 接口核查: 后端 endpoint_events.cpp:38 输出, 前端原未声明。
}

// ---------- 渲染用聚合类型 ----------

export interface ConditionData {
  conditionId: string;
  posRows: Position[];
  market: Market | null;
  book: BinaryMarketBookView | null;
  quote: Quote | null;
  rejectRows: RiskReject[];
  perMarketPnl: number | null;
}

export interface EventGroup {
  eventId: string | null;
  eventSlug: string | null;
  eventTitle: string | null;
  sport: string | null;
  score: Score | null;
  conditions: ConditionData[];
}
