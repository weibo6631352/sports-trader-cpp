/**
 * api.ts — ADR-038 debug_api 类型化 client (SolidJS + TS 版)
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 设计要点:
 *  - API base 优先级: localStorage > 同源 origin > 回退 127.0.0.1:8080
 *    · 同源托管 (C++ 从 8080 serve dist): window.location.origin → http://127.0.0.1:8080
 *    · vite dev (3000): origin=http://127.0.0.1:3000 → 不是 8080 → 回退 http://127.0.0.1:8080
 *    · localStorage 有值: 用它 (设置面板覆盖)
 *  - 4 时间戳字段 epoch_ns
 *  - 404 / found:false → null
 *  - fetchErrorMap: 全局错误追踪 (P0-03)
 */

import type {
  Healthz, Status, Positions, PnlTimeseries, PnlAttribution,
  RiskRejects, GatePaper, Market, BinaryMarketBookView, Score, Quote,
} from './types';

// ---------- API base ----------

/** DEV_PORTS: vite dev server 端口列表; 这些端口不代表后端, 须回退到后端地址 */
const DEV_PORTS = new Set(['3000', '3001', '3002', '4173']);
/** 后端默认地址, 用于 vite dev / file:// 回退 */
const FALLBACK_API = 'http://127.0.0.1:8080';

function resolveOriginBase(): string {
  const proto = window.location.protocol;
  // file:// — 本地双击打开 HTML, 没有 server, 直接回退
  if (proto === 'file:') return FALLBACK_API;
  const port = window.location.port;
  // vite dev 端口 → 回退到后端地址
  if (DEV_PORTS.has(port)) return FALLBACK_API;
  // 其他: 从当前 host:port 托管 (C++ 8080 / 任意生产端口) → 同源
  return window.location.origin;
}

function loadBaseUrl(): string {
  try {
    const stored = localStorage.getItem('stcpp_api_base');
    if (stored) return stored;
  } catch {
    // localStorage 不可用 (e.g. 隐私模式限制)
  }
  return resolveOriginBase();
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

/** 前缀匹配版: 匹配 path 开头的任意 key (用于带 query string 的 endpoint) */
export function isEndpointFailingPrefix(prefix: string): boolean {
  for (const [key, v] of fetchErrorMap) {
    if (key.startsWith(prefix) && v.failCount >= 3) return true;
  }
  return false;
}

export function anyEndpointFailing(): boolean {
  for (const [, v] of fetchErrorMap) {
    if (v.failCount >= 1) return true;
  }
  return false;
}

/** 返回所有连续失败 (failCount >= threshold) 的 endpoint 路径 + failCount, 供 tooltip 显示 */
export function failingEndpointsSummary(threshold = 3): string {
  const lines: string[] = [];
  for (const [path, v] of fetchErrorMap) {
    if (v.failCount >= threshold) {
      lines.push(`${path} (连续失败 ${v.failCount} 次)`);
    }
  }
  return lines.length > 0 ? lines.join('\n') : '';
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
