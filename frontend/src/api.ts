/**
 * api.ts — ADR-038 debug_api 类型化 client (SolidJS + TS 版)
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 设计要点:
 *  - 全部 endpoint 默认连 127.0.0.1:8080 (ADR-037 本地优先)
 *  - 4 时间戳字段 epoch_ns
 *  - 404 / found:false → null
 *  - fetchErrorMap: 全局错误追踪 (P0-03)
 */

import type {
  Healthz, Status, Positions, PnlTimeseries, PnlAttribution,
  RiskRejects, GatePaper, Market, BinaryMarketBookView, Score, Quote,
} from './types';

// ---------- API base ----------

function loadBaseUrl(): string {
  try {
    return localStorage.getItem('stcpp_api_base') || 'http://127.0.0.1:8080';
  } catch {
    return 'http://127.0.0.1:8080';
  }
}

let _baseUrl = loadBaseUrl();

export function getBaseUrl(): string {
  return _baseUrl;
}

export function setBaseUrl(url: string): void {
  localStorage.setItem('stcpp_api_base', url);
  location.reload();
}

// ---------- 全局错误追踪 (P0-03) ----------

interface EndpointStatus {
  error: string | null;
  failCount: number;
  lastOk: boolean;
}

export const fetchErrorMap = new Map<string, EndpointStatus>();

function recordError(path: string, err: unknown | null): void {
  const prev = fetchErrorMap.get(path) ?? { error: null, failCount: 0, lastOk: true };
  fetchErrorMap.set(path, {
    error: err != null ? String(err) : null,
    failCount: err != null ? prev.failCount + 1 : 0,
    lastOk: err == null,
  });
}

export function isEndpointFailing(path: string): boolean {
  const e = fetchErrorMap.get(path);
  return e != null && e.failCount >= 3;
}

export function anyEndpointFailing(): boolean {
  for (const [, v] of fetchErrorMap) {
    if (v.failCount >= 1) return true;
  }
  return false;
}

// ---------- 通用 fetch wrapper ----------

async function apiFetch<T>(path: string): Promise<T | null> {
  const url = `${_baseUrl}${path}`;
  let resp: Response;
  try {
    resp = await fetch(url, { signal: AbortSignal.timeout(5000) });
  } catch (e) {
    recordError(path, e);
    return null;
  }
  if (resp.status === 404) {
    recordError(path, null);
    return null;
  }
  if (!resp.ok) {
    recordError(path, new Error(`HTTP_${resp.status}`));
    return null;
  }
  const data = await resp.json() as Record<string, unknown>;
  if (data && data['found'] === false) {
    recordError(path, null);
    return null;
  }
  if (data && data['has_data'] === false) {
    recordError(path, null);
    return null;
  }
  recordError(path, null);
  return data as T;
}

// ---------- endpoints ----------

export const fetchHealthz = (): Promise<Healthz | null> => apiFetch('/healthz');
export const fetchStatus = (): Promise<Status | null> => apiFetch('/status');

export const fetchPositions = (): Promise<Positions | null> => apiFetch('/api/v1/positions');
export const fetchPnlTimeseries = (window = '1h', bucket = '5m'): Promise<PnlTimeseries | null> =>
  apiFetch(`/api/v1/pnl/timeseries?window=${window}&bucket=${bucket}`);
export const fetchPnlAttribution = (): Promise<PnlAttribution | null> => apiFetch('/api/v1/pnl/attribution');

export const fetchRiskRejects = (): Promise<RiskRejects | null> => apiFetch('/api/v1/risk/rejects');
export const fetchGatePaper = (): Promise<GatePaper | null> => apiFetch('/api/v1/gate/paper');

export const fetchMarket = (marketId: string): Promise<Market | null> =>
  apiFetch(`/api/v1/market/${marketId}`);

/** fetchBook — 返回 BinaryMarketBookView (ADR-040: /api/v1/book_pair/{conditionId}) */
export const fetchBook = (conditionId: string): Promise<BinaryMarketBookView | null> =>
  apiFetch(`/api/v1/book_pair/${conditionId}`);

export const fetchScore = (eventId: string): Promise<Score | null> =>
  apiFetch(`/api/v1/score/${eventId}`);

export const fetchQuote = (conditionId: string): Promise<Quote | null> =>
  apiFetch(`/api/v1/quote/${conditionId}`);

export async function fetchMetrics(): Promise<string | null> {
  try {
    const resp = await fetch(`${_baseUrl}/metrics`, { signal: AbortSignal.timeout(5000) });
    return resp.ok ? resp.text() : null;
  } catch {
    return null;
  }
}

// ---------- 工具函数 ----------

/** epoch_ns → HH:MM:SS.mmm 本地时间 */
export function fmtTs(epochNs: number | null | undefined): string {
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

/** USDC 金额 */
export function fmtUsdc(val: number | null | undefined): string {
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
export function fmtBps(val: number | null | undefined): string {
  const n = Number(val);
  if (!Number.isFinite(n)) return '—';
  const sign = n >= 0 ? '+' : '';
  return `${sign}${n.toFixed(1)} bps`;
}

/** 百分比 */
export function fmtPct(val: number | null | undefined): string {
  const n = Number(val);
  if (!Number.isFinite(n)) return '—';
  return `${(n * 100).toFixed(2)}%`;
}

/** clock_sec → M:SS */
export function fmtClock(sec: number | null | undefined): string {
  const s = Number(sec);
  if (!Number.isFinite(s) || s < 0) return '—';
  const m = Math.floor(s / 60);
  const r = Math.floor(s % 60);
  return `${m}:${String(r).padStart(2, '0')}`;
}

/** epoch_ns as_of → ms ago */
export function stalenessMs(asOfNs: number | null | undefined): number | null {
  if (asOfNs == null) return null;
  const nowMs = Date.now();
  const thenMs = Number(asOfNs) / 1e6;
  return nowMs - thenMs;
}

/** mode badge */
export function modeBadge(mode: string): { text: string; cls: string } {
  if (mode === 'live') return { text: 'LIVE', cls: 'badge-live' };
  return { text: 'PAPER', cls: 'badge-paper' };
}

/** uptime 秒 → 可读字符串 */
export function fmtUptime(sec: number): string {
  const s = Number(sec);
  if (!Number.isFinite(s) || s < 0) return '—';
  if (s < 60) return `${Math.floor(s)}s`;
  if (s < 3600) return `${Math.floor(s / 60)}分${Math.floor(s % 60)}s`;
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return `${h}h ${m}m`;
}
