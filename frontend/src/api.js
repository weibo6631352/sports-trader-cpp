/**
 * api.js — ADR-038 debug_api client
 * owner: 小苏 (E 产品业务保障部)
 * last_review: 2026-05-29
 *
 * 设计要点:
 *  - 全部 endpoint 默认连 127.0.0.1:8080 (ADR-037 本地优先)
 *  - 金额字段 JSON number 统一用 Number() 解析, 不做 toString 假设
 *  - 404 / has_data:false 返回 null, 调用方显示 "未接入"
 *  - 4 时间戳字段 (event_ts/data_source_ts/ingestion_ts/as_of_ts) 单位 epoch_ns int64
 *    — JS Number 精度够到 2^53, epoch_ns 约 1.7×10^18 超限; 用 BigInt 保护
 */

const BASE_URL = (() => {
  const stored = localStorage.getItem('stcpp_api_base');
  return stored || 'http://127.0.0.1:8080';
})();

export function getBaseUrl() {
  return BASE_URL;
}

export function setBaseUrl(url) {
  localStorage.setItem('stcpp_api_base', url);
  location.reload();
}

/**
 * 通用 fetch wrapper.
 * - 返回 parsed JSON (数字字段保留为 Number, ts 字段提升为 BigInt 由 parseTsFields 处理)
 * - 404 → null
 * - has_data === false → null (视为"未接入")
 * - 其他非 2xx → throw Error
 */
async function apiFetch(path) {
  const url = `${BASE_URL}${path}`;
  let resp;
  try {
    resp = await fetch(url, { signal: AbortSignal.timeout(5000) });
  } catch (e) {
    throw new Error(`NETWORK_ERROR: ${e.message}`);
  }
  if (resp.status === 404) return null;
  if (!resp.ok) throw new Error(`HTTP_${resp.status}`);
  const data = await resp.json();
  if (data && data.has_data === false) return null;
  return data;
}

// ---------- PnL / 持仓 ----------

export const fetchPositions = () => apiFetch('/api/v1/positions');

export const fetchPnlTimeseries = (window = '1h', bucket = '5m') =>
  apiFetch(`/api/v1/pnl/timeseries?window=${window}&bucket=${bucket}`);

export const fetchPnlAttribution = () => apiFetch('/api/v1/pnl/attribution');

// ---------- 订单 / 风控 ----------

export const fetchOpenOrders = () => apiFetch('/api/v1/orders/open');

export const fetchRiskRejects = () => apiFetch('/api/v1/risk/rejects');

export const fetchSignalsEdge = () => apiFetch('/api/v1/signals/edge_vs_fill');

// ---------- Polymarket 协议 ----------

export const fetchMarket = (conditionId) => apiFetch(`/api/v1/market/${conditionId}`);

export const fetchBook = (conditionId) => apiFetch(`/api/v1/book/${conditionId}`);

export const fetchOrder = (clientOrderId) => apiFetch(`/api/v1/order/${clientOrderId}`);

// ---------- 调试 / 可观测性 ----------

export const fetchGatePaper = () => apiFetch('/api/v1/gate/paper');

export const fetchMetrics = () =>
  fetch(`${BASE_URL}/metrics`, { signal: AbortSignal.timeout(5000) })
    .then((r) => (r.ok ? r.text() : null))
    .catch(() => null);

export const fetchHealthz = () => apiFetch('/healthz');

export const fetchStatus = () => apiFetch('/status');

// ---------- 工具函数 ----------

/**
 * epoch_ns BigInt → human-readable local time string
 * 处理 int64 超 JS Number 精度: JSON 里是 number 字段时截断误差最多 1024ns, 可接受
 */
export function fmtTs(epochNs) {
  if (epochNs == null) return '—';
  // JSON 里传来的是 Number (已经有精度损失), 转 ms 显示
  const ms = Number(epochNs) / 1e6;
  if (!Number.isFinite(ms) || ms <= 0) return '—';
  return new Date(ms).toLocaleTimeString('zh-CN', {
    hour12: false,
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    fractionalSecondDigits: 3,
  });
}

/** 金额格式: 确保是 Number, 保留 6 位小数 */
export function fmtUsdc(val) {
  const n = Number(val);
  if (!Number.isFinite(n)) return '—';
  return n.toLocaleString('en-US', {
    style: 'currency',
    currency: 'USD',
    minimumFractionDigits: 2,
    maximumFractionDigits: 6,
  });
}

/** bps 格式 */
export function fmtBps(val) {
  const n = Number(val);
  if (!Number.isFinite(n)) return '—';
  return `${n.toFixed(1)} bps`;
}

/** 百分比 */
export function fmtPct(val) {
  const n = Number(val);
  if (!Number.isFinite(n)) return '—';
  return `${(n * 100).toFixed(2)}%`;
}

/** mode badge: paper|live */
export function modeBadge(mode) {
  if (mode === 'live') return { text: 'LIVE', cls: 'badge-live' };
  return { text: 'PAPER', cls: 'badge-paper' };
}
