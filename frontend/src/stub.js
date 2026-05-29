/**
 * stub.js — v4 单屏盯盘终端 本地 stub 数据
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 结构与后端真实 wire JSON 对齐 (curl 验证 2026-05-29):
 *   /status          → 含 data_source: "demo"|"live"
 *   /api/v1/market   → 含 condition_id, tokens[], slug, polymarket_url,
 *                      event_id, tick_size/fee_rate/neg_risk/neg_risk_market_id,
 *                      active/closed/resolved/accepting_orders
 *   /api/v1/book     → BinaryMarketBookView: condition_id, cross_spread,
 *                      token0{...}/token1{...} 各含完整单边 book
 *   /api/v1/score    → found, event_id, sport, status, period, clock_sec,
 *                      home/away/home_score/away_score, source,
 *                      event_ts/data_source_ts/ingestion_ts/score_as_of_ts
 *   /api/v1/quote    → found, market_id, fair_value, market_mid, edge_bps,
 *                      kelly_fraction, suggested_notional, signal_strength, model_conf,
 *                      quote_as_of_ts
 *   金额字段均为 JSON number (不是 string)
 *   ts 字段为 epoch_ns int64 (JS Number, 精度损失约 1024ns, 已知可接受)
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
  data_source: 'demo',          // v3 新增字段 (老钱红线)
  wss_connected: {
    sports_api: true,
    clob: true,
    user_channel: false,
  },
  signals_active_count: 3,
  positions_count: 3,
  rm_rejects_last_60s: 2,
  uptime_sec: 3721,
  as_of_ts: NOW_NS,
};

export const STUB_POSITIONS = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  positions: [
    {
      market_id: 'nba-lal-bos-ml',
      outcome: 'LAL',
      net_qty: 1500,
      avg_entry_price: 0.62,
      mark_price: 0.65,
      pnl_realized: 45,
      pnl_unrealized: 45,
      as_of_ts: NOW_NS - 5.8e10,
    },
    {
      market_id: 'nba-lal-bos-ml',
      outcome: 'BOS',
      net_qty: -800,
      avg_entry_price: 0.38,
      mark_price: 0.35,
      pnl_realized: -12.5,
      pnl_unrealized: 24,
      as_of_ts: NOW_NS - 2.8e10,
    },
    {
      market_id: 'epl-ars-che-total',
      outcome: 'OVER_2.5',
      net_qty: 2200,
      avg_entry_price: 0.71,
      mark_price: 0.74,
      pnl_realized: 110,
      pnl_unrealized: 66,
      as_of_ts: NOW_NS - 9e9,
    },
  ],
};

// PnL timeseries: 60 buckets of 1-min data
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
    { market_id: 'nba-lal-bos-ml', net_pnl: 90 },
    { market_id: 'epl-ars-che-total', net_pnl: 176 },
    { market_id: 'nfl-kc-buf-spread', net_pnl: 77.5 },
    { market_id: 'mlb-nyy-bos-ml', net_pnl: -36.5 },
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

// v4: market 现含 condition_id / tokens[] / slug / polymarket_url / neg_risk_market_id
export const STUB_MARKET = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  found: true,
  condition_id: 'nba-lal-bos-ml',
  market_id: 'nba-lal-bos-ml',
  tick_size: 0.01,
  fee_rate: 0.02,
  neg_risk: false,
  neg_risk_market_id: '',
  accepting_orders: true,
  active: true,
  closed: false,
  resolved: false,
  source: 'polymarket',
  event_id: 'nba-lal-bos-2026-05-29',
  slug: 'nba-lal-bos-2026-05-29',
  polymarket_url: 'https://polymarket.com/event/nba-lal-bos-2026-05-29',
  tokens: [
    { token_id: 'tok-lal-001', outcome: 'LAL', price: 0.65, winner: false },
    { token_id: 'tok-bos-001', outcome: 'BOS', price: 0.35, winner: false },
  ],
};

// v4: BinaryMarketBookView — 双边 book
function makeHalfBook(tokenId, outcome, bid, ask, imbalance, seqOffset) {
  return {
    found: true,
    token_id: tokenId,
    condition_id: 'nba-lal-bos-ml',
    outcome,
    market_id: 'nba-lal-bos-ml',
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
      { price: bid,              size: 3200 },
      { price: bid - 0.006,      size: 1800 },
      { price: bid - 0.014,      size: 900  },
      { price: bid - 0.024,      size: 400  },
    ],
    asks: [
      { price: ask,              size: 2700 },
      { price: ask + 0.006,      size: 1500 },
      { price: ask + 0.014,      size: 600  },
      { price: ask + 0.024,      size: 300  },
    ],
  };
}

export const STUB_BOOK = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  found: true,
  condition_id: 'nba-lal-bos-ml',
  cross_spread: 0.012,
  event_ts: NOW_NS - 1e8,
  data_source_ts: NOW_NS - 9e7,
  ingestion_ts: NOW_NS - 8e7,
  as_of_ts_ns: NOW_NS,
  token0: makeHalfBook('tok-lal-001', 'LAL', 0.644, 0.656,  0.23, 0),
  token1: makeHalfBook('tok-bos-001', 'BOS', 0.344, 0.356, -0.23, 1),
};

// v3 新增: 比分/赛况 stub
export const STUB_SCORE = {
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
};

// v3 新增: 量化参数 stub
export const STUB_QUOTE = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  found: true,
  market_id: 'nba-lal-bos-ml',
  fair_value: 0.662,
  market_mid: 0.648,
  edge_bps: 21.6,
  kelly_fraction: 0.042,
  suggested_notional: 850,
  signal_strength: 0.71,
  model_conf: 0.62,
  quote_as_of_ts: NOW_NS - 3e8,
};

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
