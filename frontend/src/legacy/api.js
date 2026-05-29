/**
 * api.js — ADR-038 debug_api client (v4 双边订单簿盯盘终端)
 * owner: 小苏 (E 产品业务保障部)
 * last_review: 2026-05-29
 *
 * 设计要点:
 *  - 全部 endpoint 默认连 127.0.0.1:8080 (ADR-037 本地优先)
 *  - 金额字段 JSON number 统一用 Number() 解析
 *  - 404 / found:false 返回 null, 调用方显示占位
 *  - 4 时间戳字段 epoch_ns (event_ts/data_source_ts/ingestion_ts/as_of_ts) 单位 epoch_ns
 *  - fetchError: 全局错误追踪 (P0-03 小尤要求)
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

// ---------- 全局错误追踪 (P0-03) ----------

/**
 * endpoint → { error: string|null, failCount: number, lastOk: boolean }
 * panels.js 可读取此 map 决定显示 "未接入" 还是 "拉取失败"
 */
export const fetchErrorMap = new Map();

function recordError(path, err) {
  const prev = fetchErrorMap.get(path) || { error: null, failCount: 0, lastOk: true };
  fetchErrorMap.set(path, {
    error: err ? String(err) : null,
    failCount: err ? prev.failCount + 1 : 0,
    lastOk: !err,
  });
}

/**
 * 某 path 是否处于持续失败状态 (连续 failCount >= 3)
 */
export function isEndpointFailing(path) {
  const e = fetchErrorMap.get(path);
  return e && e.failCount >= 3;
}

/**
 * 任何 endpoint 当前有失败 (failCount >= 1)
 */
export function anyEndpointFailing() {
  for (const [, v] of fetchErrorMap) {
    if (v.failCount >= 1) return true;
  }
  return false;
}

/**
 * 通用 fetch wrapper.
 * - 404 → null (视为"正常未接入")
 * - found === false → null
 * - has_data === false → null
 * - 其他非 2xx → throw Error (调用方 safeGet 捕获后标记失败)
 */
async function apiFetch(path) {
  const url = `${BASE_URL}${path}`;
  let resp;
  try {
    resp = await fetch(url, { signal: AbortSignal.timeout(5000) });
  } catch (e) {
    recordError(path, e);
    throw new Error(`NETWORK_ERROR: ${e.message}`);
  }
  if (resp.status === 404) {
    recordError(path, null); // 404 视为正常未接入，不计 failCount
    return null;
  }
  if (!resp.ok) {
    const err = new Error(`HTTP_${resp.status}`);
    recordError(path, err);
    throw err;
  }
  const data = await resp.json();
  if (data && data.found === false) {
    recordError(path, null);
    return null;
  }
  if (data && data.has_data === false) {
    recordError(path, null);
    return null;
  }
  recordError(path, null); // 成功
  return data;
}

// ---------- 系统状态 ----------

export const fetchHealthz = () => apiFetch('/healthz');
export const fetchStatus  = () => apiFetch('/status');

// ---------- PnL / 持仓 ----------

export const fetchPositions      = () => apiFetch('/api/v1/positions');
export const fetchPnlTimeseries  = (window = '1h', bucket = '5m') =>
  apiFetch(`/api/v1/pnl/timeseries?window=${window}&bucket=${bucket}`);
export const fetchPnlAttribution = () => apiFetch('/api/v1/pnl/attribution');

// ---------- 风控 ----------

export const fetchRiskRejects = () => apiFetch('/api/v1/risk/rejects');
export const fetchGatePaper   = () => apiFetch('/api/v1/gate/paper');

// ---------- Market / Book (per market) ----------

export const fetchMarket = (marketId)     => apiFetch(`/api/v1/market/${marketId}`);

/**
 * fetchBook — 返回 BinaryMarketBookView (双边 book_pair)
 * 字段: condition_id, cross_spread, token0{...}, token1{...}
 * 路由: /api/v1/book_pair/{conditionId} (ADR-040 显式端点)
 */
export const fetchBook   = (conditionId)  => apiFetch(`/api/v1/book_pair/${conditionId}`);

// ---------- Score / Quote (per market) ----------

/** /api/v1/score/{event_id} — 比分/赛况 */
export const fetchScore = (eventId) => apiFetch(`/api/v1/score/${eventId}`);

/** /api/v1/quote/{condition_id} — 量化参数 fair/edge/Kelly */
export const fetchQuote = (conditionId) => apiFetch(`/api/v1/quote/${conditionId}`);

// ---------- Metrics (raw text) ----------

export const fetchMetrics = () =>
  fetch(`${BASE_URL}/metrics`, { signal: AbortSignal.timeout(5000) })
    .then((r) => (r.ok ? r.text() : null))
    .catch(() => null);

// ---------- 工具函数 ----------

/** epoch_ns Number → HH:MM:SS.mmm 本地时间 */
export function fmtTs(epochNs) {
  if (epochNs == null) return '—';
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

/** USDC 金额格式 */
export function fmtUsdc(val) {
  const n = Number(val);
  if (!Number.isFinite(n)) return '—';
  const sign = n >= 0 ? '+' : '';
  return sign + n.toLocaleString('en-US', {
    style: 'currency',
    currency: 'USD',
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  });
}

/** bps 格式 */
export function fmtBps(val) {
  const n = Number(val);
  if (!Number.isFinite(n)) return '—';
  const sign = n >= 0 ? '+' : '';
  return `${sign}${n.toFixed(1)} bps`;
}

/** 百分比 */
export function fmtPct(val) {
  const n = Number(val);
  if (!Number.isFinite(n)) return '—';
  return `${(n * 100).toFixed(2)}%`;
}

/** clock_sec → M:SS */
export function fmtClock(sec) {
  const s = Number(sec);
  if (!Number.isFinite(s) || s < 0) return '—';
  const m = Math.floor(s / 60);
  const r = Math.floor(s % 60);
  return `${m}:${String(r).padStart(2, '0')}`;
}

/** staleness: epoch_ns as_of → ms ago */
export function stalenessMs(asOfNs) {
  if (asOfNs == null) return null;
  const nowMs = Date.now();
  const thenMs = Number(asOfNs) / 1e6;
  return nowMs - thenMs;
}

/** mode badge HTML */
export function modeBadge(mode) {
  if (mode === 'live') return { text: 'LIVE', cls: 'badge-live' };
  return { text: 'PAPER', cls: 'badge-paper' };
}

/** uptime 秒 → 可读字符串 */
export function fmtUptime(sec) {
  const s = Number(sec);
  if (!Number.isFinite(s) || s < 0) return '—';
  if (s < 60) return `${Math.floor(s)}s`;
  if (s < 3600) return `${Math.floor(s / 60)}分${Math.floor(s % 60)}s`;
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return `${h}h ${m}m`;
}
