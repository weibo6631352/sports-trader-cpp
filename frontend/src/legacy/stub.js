/**
 * stub.js — v5 赛事分组卡布局 本地 stub 数据
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * v5 变更:
 *  - LAL-BOS 三盘口 (ml/total/spread) 共享 event_id 'nba-lal-bos-2026-05-29'
 *  - ARS-CHE / KC-BUF / NYY-BOS 各单盘口
 *  - STUB_MARKET_MAP: marketId → market (支持多盘口各自元信息)
 *  - STUB_BOOK_MAP / STUB_QUOTE_MAP: 各盘口独立 book/quote
 *  - STUB_SCORE_MAP: event_id → score (按事件去重)
 *
 * 结构与后端真实 wire JSON 对齐 (curl 验证 2026-05-29):
 *   /api/v1/market/{id} → 含 event_id (v5 核心: 按 event_id 分组用)
 */

const NOW_NS = Date.now() * 1e6;

export const STUB_HEALTHZ = {
  ok: true,
  threads: {
    ingest_reactor: 'alive',
    signal_engine: 'alive',
    risk_manager: 'alive',
    paper_signer: 'alive',
    api_server: 'alive',
  },
  uptime_sec: 3721,
  as_of_ts: NOW_NS,
};

export const STUB_STATUS = {
  state: 'RUNNING',
  mode: 'paper',
  data_source: 'demo',
  wss_connected: {
    sports_api: true,
    clob: true,
    user_channel: false,
  },
  signals_active_count: 5,
  positions_count: 5,
  rm_rejects_last_60s: 2,
  uptime_sec: 3721,
  as_of_ts: NOW_NS,
};

// v5: 5 条持仓记录 — 对齐后端 DemoStateProvider.positions()
// LAL-BOS 三盘口 (ml/total/spread) 全部有持仓, 与后端 demo 数据保持一致
export const STUB_POSITIONS = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  positions: [
    // LAL-BOS 胜负盘 — token0 LAL 多头
    {
      market_id: 'nba-lal-bos-ml',
      outcome: 'LAL',
      net_qty: 1500,
      avg_entry_price: 0.62,
      mark_price: 0.65,
      pnl_realized: 45,
      pnl_unrealized: 45,
      as_of_ts: NOW_NS - 6e10,
    },
    // LAL-BOS 胜负盘 — token1 BOS 空头
    {
      market_id: 'nba-lal-bos-ml',
      outcome: 'BOS',
      net_qty: -800,
      avg_entry_price: 0.38,
      mark_price: 0.35,
      pnl_realized: -12.5,
      pnl_unrealized: 24,
      as_of_ts: NOW_NS - 3e10,
    },
    // LAL-BOS 大小盘 — OVER_220.5 多头 (对齐后端 demo)
    {
      market_id: 'nba-lal-bos-total',
      outcome: 'OVER_220.5',
      net_qty: 900,
      avg_entry_price: 0.50,
      mark_price: 0.52,
      pnl_realized: 18,
      pnl_unrealized: 18,
      as_of_ts: NOW_NS - 2e10,
    },
    // LAL-BOS 让分盘 — LAL_-5.5 多头 (对齐后端 demo)
    {
      market_id: 'nba-lal-bos-spread',
      outcome: 'LAL_-5.5',
      net_qty: 600,
      avg_entry_price: 0.47,
      mark_price: 0.49,
      pnl_realized: 12,
      pnl_unrealized: 12,
      as_of_ts: NOW_NS - 1.5e10,
    },
    // ARS-CHE 大小盘 — OVER_2.5 多头
    {
      market_id: 'epl-ars-che-total',
      outcome: 'OVER_2.5',
      net_qty: 2200,
      avg_entry_price: 0.71,
      mark_price: 0.74,
      pnl_realized: 110,
      pnl_unrealized: 66,
      as_of_ts: NOW_NS - 1e10,
    },
    // KC-BUF 让分盘 / NYY-BOS 胜负盘: 只有 attribution 记录, 无持仓行
  ],
};

export const STUB_PNL_TIMESERIES = (() => {
  const buckets = [];
  const n = 60;
  let cumPnl = 0;
  for (let i = 0; i < n; i++) {
    const delta = (Math.random() - 0.4) * 12;
    cumPnl += delta;
    buckets.push({
      bucket_start_ts: NOW_NS - (n - i) * 60 * 1e9,
      cum_net_pnl: parseFloat(cumPnl.toFixed(4)),
      realized: parseFloat((delta * 0.6).toFixed(4)),
      unrealized: parseFloat((delta * 0.4).toFixed(4)),
      fee: parseFloat((Math.random() * 1.5).toFixed(4)),
      gas: parseFloat((Math.random() * 0.2).toFixed(4)),
      n_trades: Math.floor(Math.random() * 6),
    });
  }
  return {
    mode: 'paper',
    window_sec: 3600,
    bucket_sec: 60,
    as_of_ts: NOW_NS,
    buckets,
  };
})();

export const STUB_PNL_ATTRIBUTION = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  waterfall: {
    gross: 312.5,
    fee: -18.3,
    gas: -2.1,
    slippage: -9.7,
    spread: 24.6,
    net: 307,
  },
  per_market: [
    { market_id: 'nba-lal-bos-ml',     net_pnl: 90 },
    { market_id: 'nba-lal-bos-total',   net_pnl: 12.5 },
    { market_id: 'nba-lal-bos-spread',  net_pnl: -8 },
    { market_id: 'epl-ars-che-total',   net_pnl: 176 },
    { market_id: 'nfl-kc-buf-spread',   net_pnl: 77.5 },
    { market_id: 'mlb-nyy-bos-ml',      net_pnl: -36.5 },
  ],
};

export const STUB_RISK_REJECTS = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  rejects: [
    {
      intent_ref: 'intent-7f3a',
      market_id: 'nba-lal-bos-ml',
      reason_code: 'MAX_POSITION_EXCEEDED',
      side: 'BUY',
      size: 500,
      price: 0.41,
      rejected_ts: NOW_NS - 1.8e9,
    },
    {
      intent_ref: 'intent-9c21',
      market_id: 'mlb-nyy-bos-ml',
      reason_code: 'MARKET_NOT_ACCEPTING_ORDERS',
      side: 'SELL',
      size: 100,
      price: 0.88,
      rejected_ts: NOW_NS - 1.18e10,
    },
    {
      intent_ref: 'intent-b04e',
      market_id: 'epl-ars-che-total',
      reason_code: 'KELLY_FRACTION_CAP',
      side: 'BUY',
      size: 1200,
      price: 0.73,
      rejected_ts: NOW_NS - 4.1e10,
    },
  ],
};

export const STUB_GATE_PAPER = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  window_days: 30,
  has_data: true,
  n_trades: 142,
  positive_day_ratio: 0.6333,
  sharpe: 1.12,
  sharpe_se: 0.18,
  p_value: 0.043,
  hit_rate: 0.5634,
  max_drawdown: 0.082,
  prelim_pass: true,
  confirm_pass: false,
};

// ============================================================
// v5: 多盘口 market stub map
// 各盘口 market 共享 event_id → 赛事分组卡
// ============================================================

function makeMarket(marketId, conditionId, eventId, slug, url, tokens, accepting) {
  return {
    mode: 'paper',
    as_of_ts: NOW_NS,
    found: true,
    condition_id: conditionId,
    market_id: marketId,
    tick_size: 0.01,
    fee_rate: 0.02,
    neg_risk: false,
    neg_risk_market_id: '',
    accepting_orders: accepting !== false,
    active: true,
    closed: false,
    resolved: false,
    source: 'polymarket',
    event_id: eventId,
    slug,
    polymarket_url: url,
    tokens,
  };
}

// LAL-BOS 三盘口 — 共享 event_id 'nba-lal-bos-2026-05-29'
const NBA_EVENT_ID  = 'nba-lal-bos-2026-05-29';
const NBA_EVENT_URL = 'https://polymarket.com/event/nba-lal-bos-2026-05-29';

const MARKET_NBA_ML = makeMarket(
  'nba-lal-bos-ml', 'nba-lal-bos-ml', NBA_EVENT_ID,
  'nba-lal-bos-2026-05-29-ml', NBA_EVENT_URL,
  [
    { token_id: 'tok-lal-001', outcome: 'LAL', price: 0.65, winner: false },
    { token_id: 'tok-bos-001', outcome: 'BOS', price: 0.35, winner: false },
  ],
);

const MARKET_NBA_TOTAL = makeMarket(
  'nba-lal-bos-total', 'nba-lal-bos-total', NBA_EVENT_ID,
  'nba-lal-bos-2026-05-29-total', NBA_EVENT_URL,
  [
    { token_id: 'tok-over-001', outcome: '大 215.5', price: 0.50, winner: false },
    { token_id: 'tok-under-001', outcome: '小 215.5', price: 0.50, winner: false },
  ],
);

const MARKET_NBA_SPREAD = makeMarket(
  'nba-lal-bos-spread', 'nba-lal-bos-spread', NBA_EVENT_ID,
  'nba-lal-bos-2026-05-29-spread', NBA_EVENT_URL,
  [
    { token_id: 'tok-cover-001', outcome: 'LAL -3.5', price: 0.49, winner: false },
    { token_id: 'tok-dog-001', outcome: 'BOS +3.5', price: 0.51, winner: false },
  ],
);

// ARS-CHE 单盘口
const ARS_EVENT_ID  = 'epl-ars-che-2026-05-29';
const ARS_EVENT_URL = 'https://polymarket.com/event/epl-ars-che-2026-05-29';
const MARKET_ARS_TOTAL = makeMarket(
  'epl-ars-che-total', 'epl-ars-che-total', ARS_EVENT_ID,
  'epl-ars-che-2026-05-29-total', ARS_EVENT_URL,
  [
    { token_id: 'tok-arsover-001', outcome: '大 2.5', price: 0.74, winner: false },
    { token_id: 'tok-arsunder-001', outcome: '小 2.5', price: 0.26, winner: false },
  ],
);

// KC-BUF 单盘口
const KC_EVENT_ID  = 'nfl-kc-buf-2026-05-29';
const KC_EVENT_URL = 'https://polymarket.com/event/nfl-kc-buf-2026-05-29';
const MARKET_KC_SPREAD = makeMarket(
  'nfl-kc-buf-spread', 'nfl-kc-buf-spread', KC_EVENT_ID,
  'nfl-kc-buf-2026-05-29-spread', KC_EVENT_URL,
  [
    { token_id: 'tok-kccover-001', outcome: 'KC -6.5', price: 0.62, winner: false },
    { token_id: 'tok-bufdog-001', outcome: 'BUF +6.5', price: 0.38, winner: false },
  ],
);

// NYY-BOS 单盘口
const NYY_EVENT_ID  = 'mlb-nyy-bos-2026-05-29';
const NYY_EVENT_URL = 'https://polymarket.com/event/mlb-nyy-bos-2026-05-29';
const MARKET_NYY_ML = makeMarket(
  'mlb-nyy-bos-ml', 'mlb-nyy-bos-ml', NYY_EVENT_ID,
  'mlb-nyy-bos-2026-05-29-ml', NYY_EVENT_URL,
  [
    { token_id: 'tok-nyy-001', outcome: 'NYY', price: 0.55, winner: false },
    { token_id: 'tok-bos2-001', outcome: 'BOS', price: 0.45, winner: false },
  ],
  false, // not accepting_orders
);

// STUB_MARKET_MAP: marketId → market (app.js safeGet fallback 用)
export const STUB_MARKET_MAP = {
  'nba-lal-bos-ml':     MARKET_NBA_ML,
  'nba-lal-bos-total':  MARKET_NBA_TOTAL,
  'nba-lal-bos-spread': MARKET_NBA_SPREAD,
  'epl-ars-che-total':  MARKET_ARS_TOTAL,
  'nfl-kc-buf-spread':  MARKET_KC_SPREAD,
  'mlb-nyy-bos-ml':     MARKET_NYY_ML,
};

// 向后兼容 (STUB_MARKET 单值供 fallback)
export const STUB_MARKET = MARKET_NBA_ML;

// ============================================================
// v5: 多盘口 book stub map
// ============================================================

function makeHalfBook(tokenId, condId, outcome, bid, ask, imbalance, seqOffset) {
  return {
    found: true,
    token_id: tokenId,
    condition_id: condId,
    outcome,
    market_id: condId,
    best_bid: bid,
    best_ask: ask,
    microprice: parseFloat(((bid + ask) / 2).toFixed(4)),
    spread: parseFloat((ask - bid).toFixed(4)),
    imbalance,
    sequence_no: 88421 + seqOffset,
    gap_count: 0,
    wss_state: 'CONNECTED',
    source: 'polymarket',
    event_ts: NOW_NS - 1e8,
    data_source_ts: NOW_NS - 9e7,
    ingestion_ts: NOW_NS - 8e7,
    book_as_of_ts: NOW_NS,
    bids: [
      { price: bid,         size: 3200 },
      { price: bid - 0.006, size: 1800 },
      { price: bid - 0.014, size: 900  },
    ],
    asks: [
      { price: ask,         size: 2700 },
      { price: ask + 0.006, size: 1500 },
      { price: ask + 0.014, size: 600  },
    ],
  };
}

function makeBook(condId, crossSpread, t0, t1) {
  return {
    mode: 'paper',
    as_of_ts: NOW_NS,
    found: true,
    condition_id: condId,
    cross_spread: crossSpread,
    event_ts: NOW_NS - 1e8,
    data_source_ts: NOW_NS - 9e7,
    ingestion_ts: NOW_NS - 8e7,
    as_of_ts_ns: NOW_NS,
    token0: t0,
    token1: t1,
  };
}

export const STUB_BOOK_MAP = {
  'nba-lal-bos-ml': makeBook('nba-lal-bos-ml', 0.012,
    makeHalfBook('tok-lal-001',   'nba-lal-bos-ml', 'LAL',     0.644, 0.656,  0.23, 0),
    makeHalfBook('tok-bos-001',   'nba-lal-bos-ml', 'BOS',     0.344, 0.356, -0.23, 1)),
  'nba-lal-bos-total': makeBook('nba-lal-bos-total', 0.008,
    makeHalfBook('tok-over-001',  'nba-lal-bos-total', '大 215.5', 0.496, 0.504,  0.05, 2),
    makeHalfBook('tok-under-001', 'nba-lal-bos-total', '小 215.5', 0.496, 0.504, -0.05, 3)),
  'nba-lal-bos-spread': makeBook('nba-lal-bos-spread', 0.016,
    makeHalfBook('tok-cover-001', 'nba-lal-bos-spread', 'LAL -3.5', 0.486, 0.502,  0.10, 4),
    makeHalfBook('tok-dog-001',   'nba-lal-bos-spread', 'BOS +3.5', 0.498, 0.514, -0.10, 5)),
  'epl-ars-che-total': makeBook('epl-ars-che-total', 0.020,
    makeHalfBook('tok-arsover-001',  'epl-ars-che-total', '大 2.5', 0.732, 0.752,  0.35, 6),
    makeHalfBook('tok-arsunder-001', 'epl-ars-che-total', '小 2.5', 0.248, 0.268, -0.35, 7)),
  'nfl-kc-buf-spread': makeBook('nfl-kc-buf-spread', 0.018,
    makeHalfBook('tok-kccover-001', 'nfl-kc-buf-spread', 'KC -6.5',  0.610, 0.628,  0.18, 8),
    makeHalfBook('tok-bufdog-001',  'nfl-kc-buf-spread', 'BUF +6.5', 0.372, 0.390, -0.18, 9)),
  'mlb-nyy-bos-ml': makeBook('mlb-nyy-bos-ml', 0.022,
    makeHalfBook('tok-nyy-001',  'mlb-nyy-bos-ml', 'NYY', 0.538, 0.560,  0.12, 10),
    makeHalfBook('tok-bos2-001', 'mlb-nyy-bos-ml', 'BOS', 0.440, 0.462, -0.12, 11)),
};

// 向后兼容
export const STUB_BOOK = STUB_BOOK_MAP['nba-lal-bos-ml'];

// ============================================================
// v5: score stub map (按 event_id)
// ============================================================

export const STUB_SCORE_MAP = {
  'nba-lal-bos-2026-05-29': {
    mode: 'paper',
    as_of_ts: NOW_NS,
    found: true,
    event_id: 'nba-lal-bos-2026-05-29',
    sport: 'basketball',
    status: 'inplay',
    period: 'Q3',
    clock_sec: 522,
    home: 'LAL',
    away: 'BOS',
    home_score: 87,
    away_score: 91,
    source: 'goalserve',
    event_ts: NOW_NS - 2.5e9,
    data_source_ts: NOW_NS - 2.3e9,
    ingestion_ts: NOW_NS - 2e9,
    score_as_of_ts: NOW_NS - 5e8,
  },
  'epl-ars-che-2026-05-29': {
    mode: 'paper',
    as_of_ts: NOW_NS,
    found: true,
    event_id: 'epl-ars-che-2026-05-29',
    sport: 'soccer',
    status: 'inplay',
    period: 'H1',
    clock_sec: 2280,
    home: 'ARS',
    away: 'CHE',
    home_score: 2,
    away_score: 1,
    source: 'goalserve',
    event_ts: NOW_NS - 1e9,
    data_source_ts: NOW_NS - 9e8,
    ingestion_ts: NOW_NS - 8e8,
    score_as_of_ts: NOW_NS - 3e8,
  },
  'nfl-kc-buf-2026-05-29': {
    mode: 'paper',
    as_of_ts: NOW_NS,
    found: true,
    event_id: 'nfl-kc-buf-2026-05-29',
    sport: 'football',
    status: 'pregame',
    period: '—',
    clock_sec: null,
    home: 'KC',
    away: 'BUF',
    home_score: null,
    away_score: null,
    source: 'goalserve',
    event_ts: NOW_NS - 5e9,
    data_source_ts: NOW_NS - 4.8e9,
    ingestion_ts: NOW_NS - 4.5e9,
    score_as_of_ts: NOW_NS - 4e9,
  },
  'mlb-nyy-bos-2026-05-29': {
    mode: 'paper',
    as_of_ts: NOW_NS,
    found: true,
    event_id: 'mlb-nyy-bos-2026-05-29',
    sport: 'baseball',
    status: 'halftime',
    period: '7th',
    clock_sec: null,
    home: 'NYY',
    away: 'BOS',
    home_score: 4,
    away_score: 3,
    source: 'goalserve',
    event_ts: NOW_NS - 3e9,
    data_source_ts: NOW_NS - 2.8e9,
    ingestion_ts: NOW_NS - 2.5e9,
    score_as_of_ts: NOW_NS - 1e9,
  },
};

// 向后兼容
export const STUB_SCORE = STUB_SCORE_MAP['nba-lal-bos-2026-05-29'];

// ============================================================
// v5: quote stub map (按 condition_id)
// ============================================================

function makeQuote(condId, fairValue, marketMid, edgeBps, kelly, notional, sig, conf) {
  return {
    mode: 'paper',
    as_of_ts: NOW_NS,
    found: true,
    market_id: condId,
    fair_value: fairValue,
    market_mid: marketMid,
    edge_bps: edgeBps,
    kelly_fraction: kelly,
    suggested_notional: notional,
    signal_strength: sig,
    model_conf: conf,
    quote_as_of_ts: NOW_NS - 3e8,
  };
}

export const STUB_QUOTE_MAP = {
  'nba-lal-bos-ml':     makeQuote('nba-lal-bos-ml',    0.662, 0.648, 21.6,  0.042, 850,  0.71, 0.62),
  'nba-lal-bos-total':  makeQuote('nba-lal-bos-total',  0.503, 0.500,  4.1,  0.008, 160,  0.41, 0.55),
  'nba-lal-bos-spread': makeQuote('nba-lal-bos-spread', 0.491, 0.494, -3.8,  0.000,   0,  0.28, 0.38),
  'epl-ars-che-total':  makeQuote('epl-ars-che-total',  0.748, 0.742, 15.2,  0.031, 620,  0.65, 0.71),
  'nfl-kc-buf-spread':  makeQuote('nfl-kc-buf-spread',  0.617, 0.619, -4.9,  0.000,   0,  0.22, 0.44),
  'mlb-nyy-bos-ml':     makeQuote('mlb-nyy-bos-ml',     0.541, 0.549, -7.8,  0.000,   0,  0.18, 0.31),
};

// 向后兼容
export const STUB_QUOTE = STUB_QUOTE_MAP['nba-lal-bos-ml'];

export const STUB_METRICS_TEXT = `
# HELP stcpp_uptime_seconds Process uptime in seconds
# TYPE stcpp_uptime_seconds gauge
stcpp_uptime_seconds{mode="paper"} 3721
# HELP stcpp_wss_connected WSS channel connected (1=up 0=down)
# TYPE stcpp_wss_connected gauge
stcpp_wss_connected{mode="paper",channel="sports_api"} 1
stcpp_wss_connected{mode="paper",channel="clob"} 1
stcpp_wss_connected{mode="paper",channel="user"} 0
# HELP stcpp_loop_latency_p99_us Hot loop p99 latency microseconds
# TYPE stcpp_loop_latency_p99_us gauge
stcpp_loop_latency_p99_us{mode="paper"} 74
# HELP stcpp_rm_decision_total Total RM decisions
# TYPE stcpp_rm_decision_total counter
stcpp_rm_decision_total{mode="paper"} 142
# HELP stcpp_rm_reject_total Total RM rejects
# TYPE stcpp_rm_reject_total counter
stcpp_rm_reject_total{mode="paper"} 3
# HELP stcpp_fill_total Total fills
# TYPE stcpp_fill_total counter
stcpp_fill_total{mode="paper"} 139
# HELP stcpp_net_edge_bps Net edge basis points
# TYPE stcpp_net_edge_bps gauge
stcpp_net_edge_bps{mode="paper"} 12.4
# HELP stcpp_cum_net_pnl Cumulative net PnL
# TYPE stcpp_cum_net_pnl gauge
stcpp_cum_net_pnl{mode="paper"} 307
# HELP stcpp_data_staleness_ms_max Max feed staleness milliseconds
# TYPE stcpp_data_staleness_ms_max gauge
stcpp_data_staleness_ms_max{mode="paper"} 38
`.trim();
