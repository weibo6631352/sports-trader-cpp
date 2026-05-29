/**
 * stub.js — 本地 stub 数据, 后端 API 未接入时使用
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 结构与 ADR-038 §4 endpoint schema 对齐.
 * 金额字段均为 JSON number (不是 string), 对齐老高提示.
 * ts 字段为 epoch_ns int64 (在 JS 表示为 number, 有精度损失约 1024ns, 已知).
 */

const NOW_NS = Date.now() * 1e6; // approximate epoch_ns

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
  wss_connected: {
    sports_api: true,
    clob: true,
    user_channel: false,
  },
  signals_active_count: 3,
  positions_count: 5,
  rm_rejects_last_60s: 2,
  uptime_sec: 3721,
  as_of_ts: NOW_NS,
};

export const STUB_POSITIONS = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  positions: [
    {
      market_id: 'mkt-abc-001',
      outcome: 'YES',
      net_qty: 1500,
      avg_entry_price: 0.62,
      mark_price: 0.65,
      pnl_realized: 45.0,
      pnl_unrealized: 45.0,
      as_of_ts: NOW_NS - 5.8e10,
    },
    {
      market_id: 'mkt-def-002',
      outcome: 'NO',
      net_qty: -800,
      avg_entry_price: 0.38,
      mark_price: 0.35,
      pnl_realized: -12.5,
      pnl_unrealized: -24.0,
      as_of_ts: NOW_NS - 2.8e10,
    },
    {
      market_id: 'mkt-ghi-003',
      outcome: 'YES',
      net_qty: 2200,
      avg_entry_price: 0.71,
      mark_price: 0.74,
      pnl_realized: 110.0,
      pnl_unrealized: 66.0,
      as_of_ts: NOW_NS - 9e9,
    },
  ],
};

// PnL timeseries: 24 buckets of 5-min data
export const STUB_PNL_TIMESERIES = (() => {
  const buckets = [];
  const n = 24;
  let cumPnl = 0;
  for (let i = 0; i < n; i++) {
    const delta = (Math.random() - 0.4) * 30;
    cumPnl += delta;
    buckets.push({
      bucket_start_ts: NOW_NS - (n - i) * 5 * 60 * 1e9,
      cum_net_pnl: parseFloat(cumPnl.toFixed(4)),
      realized: parseFloat((delta * 0.6).toFixed(4)),
      unrealized: parseFloat((delta * 0.4).toFixed(4)),
      fee: parseFloat((Math.random() * 1.5).toFixed(4)),
      gas: parseFloat((Math.random() * 0.2).toFixed(4)),
      n_trades: Math.floor(Math.random() * 8),
    });
  }
  return {
    mode: 'paper',
    window: '2h',
    bucket: '5m',
    as_of_ts: NOW_NS,
    buckets,
  };
})();

export const STUB_PNL_ATTRIBUTION = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  // waterfall 是 object, 顺序固定: gross→fee→gas→slippage→spread→net (ADR-038 §4.1)
  waterfall: {
    gross: 312.5,
    fee: -18.3,
    gas: -2.1,
    slippage: -9.7,
    spread: 24.6,
    net: 307.0,
  },
  per_market: [
    { market_id: 'mkt-abc-001', net_pnl: 90.0 },
    { market_id: 'mkt-def-002', net_pnl: -36.5 },
    { market_id: 'mkt-ghi-003', net_pnl: 176.0 },
    { market_id: 'mkt-jkl-004', net_pnl: 77.5 },
  ],
};

export const STUB_RISK_REJECTS = {
  mode: 'paper',
  as_of_ts: NOW_NS,
  rejects: [
    {
      intent_ref: 'intent-7f3a',
      market_id: 'mkt-def-002',
      reason_code: 'MAX_POSITION_EXCEEDED',
      side: 'BUY',
      size: 500,
      price: 0.41,
      rejected_ts: NOW_NS - 1.8e9,
    },
    {
      intent_ref: 'intent-9c21',
      market_id: 'mkt-xyz-099',
      reason_code: 'MARKET_NOT_ACCEPTING_ORDERS',
      side: 'SELL',
      size: 100,
      price: 0.88,
      rejected_ts: NOW_NS - 1.18e10,
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

export const STUB_BOOK = {
  mode: 'paper',
  condition_id: 'mkt-abc-001',
  sequence_no: 88421,
  gap_count: 0,
  wss_state: 'CONNECTED',
  microprice: 0.648,
  spread: 0.012,
  imbalance: 0.23,
  as_of_ts: NOW_NS,
  bids: [
    { price: 0.644, size: 3200 },
    { price: 0.638, size: 1800 },
    { price: 0.630, size: 900 },
    { price: 0.620, size: 400 },
  ],
  asks: [
    { price: 0.656, size: 2700 },
    { price: 0.662, size: 1500 },
    { price: 0.670, size: 600 },
    { price: 0.680, size: 300 },
  ],
  event_ts: NOW_NS - 1e8,
  data_source_ts: NOW_NS - 9e7,
  ingestion_ts: NOW_NS - 8e7,
  book_as_of_ts: NOW_NS,
};

export const STUB_MARKET = {
  mode: 'paper',
  condition_id: 'mkt-abc-001',
  question: 'Will Team A win the NBA Finals Game 5?',
  outcomes: ['YES', 'NO'],
  token_ids: ['tok-yes-001', 'tok-no-001'],
  tick_size: 0.01,
  fee_rate: 0.02,
  neg_risk: false,
  accepting_orders: true,
  status: 'active',
  as_of_ts: NOW_NS,
};

export const STUB_METRICS_TEXT = `
# HELP stcpp_uptime_seconds Process uptime
# TYPE stcpp_uptime_seconds gauge
stcpp_uptime_seconds 3721

# HELP stcpp_wss_reconnect_total WebSocket reconnect count
# TYPE stcpp_wss_reconnect_total counter
stcpp_wss_reconnect_total{feed="sports_api"} 0
stcpp_wss_reconnect_total{feed="clob"} 1

# HELP stcpp_rm_decision_total Risk manager decisions
# TYPE stcpp_rm_decision_total counter
stcpp_rm_decision_total{verdict="ALLOW"} 140
stcpp_rm_decision_total{verdict="REJECT"} 2

# HELP stcpp_pnl_net_usdc Current net PnL in USDC
# TYPE stcpp_pnl_net_usdc gauge
stcpp_pnl_net_usdc 307.0

# HELP stcpp_loop_p99_ns Hot-path loop p99 latency nanoseconds
# TYPE stcpp_loop_p99_ns gauge
stcpp_loop_p99_ns 74
`.trim();
