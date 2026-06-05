/**
 * api.ts — ADR-038 debug_api 类型化 client (SolidJS + TS 版)
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 设计要点:
 *  - API base 优先级: localStorage > 默认 http://127.0.0.1:8080
 *    · 前端由 Vite 提供 (dev 3000 / preview 4173), 与 API (8080) 不同源
 *    · 跨域由后端 CORS 处理 (已开)
 *    · localStorage 有值: 用它 (设置面板手动覆盖, 给特殊调试)
 *    · 否则默认 http://127.0.0.1:8080
 *  - 4 时间戳字段 epoch_ns
 *  - 404 / found:false → null
 *  - fetchErrorMap: 全局错误追踪 (P0-03)
 */

import type {
  Healthz, Status, Positions, PnlTimeseries, PnlAttribution,
  RiskRejects, GatePaper, Market, BinaryMarketBookView, Score, Quote, EventsResponse,
  FeatureHealth, MappingStatus, Account, GridResponse, Fills,
} from './types';

// ---------- API base ----------

/** 后端默认地址: 自动连【访问前端的同一主机】的 8080 (云部署: 浏览器从公网 IP 打开 → 连该 IP:8080)。
 *  本地 dev (localhost) 仍连 127.0.0.1:8080。可被 localStorage 覆盖。跨域由后端 CORS 处理。 */
function defaultApiBase(): string {
  try {
    const h = window.location.hostname;
    if (h && h !== 'localhost' && h !== '127.0.0.1') return `http://${h}:8080`;
  } catch {
    // SSR / 无 window
  }
  return 'http://127.0.0.1:8080';
}

function loadBaseUrl(): string {
  try {
    const stored = localStorage.getItem('stcpp_api_base');
    if (stored) return stored;
  } catch {
    // localStorage 不可用 (e.g. 隐私模式限制)
  }
  return defaultApiBase();
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

// ---------- 全局并发闸 (跨洋高延迟链路防请求风暴, 2026-06-02 老雷) ----------
//
// 问题: 旧设计每轮一次性 Promise.all 发 ~350 请求。浏览器对同一 origin HTTP/1.1
//       仅开 6 条连接, 跨洋 RTT ~200ms → 吞吐 30 req/s, 350 请求要排 ~11s 才抽干,
//       而轮询每 5s 又灌一轮 → 队列无界增长 → 后发请求全部撞 12s 超时。
// 修法: 全局信号量, 同时在途请求封顶 MAX_CONCURRENT (= 浏览器单 origin 连接数),
//       多出的请求有序排队, 由空出的槽位接力。配合 store 层削减每轮请求量,
//       链路不再堵 (详见 store.ts refreshMarketGrid 注释)。
const MAX_CONCURRENT = 6;
let _inFlight = 0;
const _waitQueue: Array<() => void> = [];

function acquireSlot(priority = false): Promise<void> {
  if (_inFlight < MAX_CONCURRENT) {
    _inFlight++;
    return Promise.resolve();
  }
  // priority=true (用户交互触发, 如展开盘口): 插队到队首, 抢下一个空出的槽位,
  //   不在后台轮询(grid/score 批量)后面排队 → 展开即时出数据。
  return new Promise<void>((resolve) => {
    if (priority) _waitQueue.unshift(resolve);
    else _waitQueue.push(resolve);
  });
}

function releaseSlot(): void {
  const next = _waitQueue.shift();
  if (next) {
    next();           // 槽位接力给排队者, _inFlight 不变 (仍 ≤ MAX)
  } else {
    _inFlight--;
  }
}

/** 受并发闸控制的 fetch — 所有出站请求必须走这里。priority=true 插队 (用户交互) */
async function limitedFetch(url: string, init?: RequestInit, priority = false): Promise<Response> {
  await acquireSlot(priority);
  try {
    return await fetch(url, init);
  } finally {
    releaseSlot();
  }
}

/** 当前在途请求数 (可观测; Ops 页可显) */
export function inFlightCount(): number {
  return _inFlight;
}

/** 当前排队等待的请求数 */
export function queuedCount(): number {
  return _waitQueue.length;
}

// ---------- 通用 fetch wrapper ----------

async function apiFetch<T>(path: string, priority = false): Promise<T | null> {
  const url = `${_baseUrl}${path}`;
  let resp: Response;
  try {
    resp = await limitedFetch(url, { signal: AbortSignal.timeout(12000) }, priority);
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
export const fetchEvents = (): Promise<EventsResponse | null> => apiFetch('/api/v1/events');
/** /api/v1/grid — 全市场顶档摘要 (一次请求, 跨洋链路防风暴; 折叠态摘要行填价) */
export const fetchGrid = (): Promise<GridResponse | null> => apiFetch('/api/v1/grid');

/** SSE focus 订阅: 告知服务端"我在看哪些盘口"(展开盘口走 SSE 推全档 book/quote)。
 *  query 参数 + 无 body/无自定义头 → simple request, 免 CORS 预检。失败静默 (focus 丢了最多晚一 tick)。 */
export async function postStreamFocus(streamId: string, seq: number, cids: string[]): Promise<void> {
  try {
    const q = `stream_id=${encodeURIComponent(streamId)}&seq=${seq}&cids=${cids.map(encodeURIComponent).join(',')}`;
    await fetch(`${_baseUrl}/api/v1/stream/focus?${q}`, { method: 'POST', signal: AbortSignal.timeout(8000) });
  } catch {
    // 静默: focus POST 丢失最多导致展开盘口晚一 tick 收到推送, 不影响其余
  }
}

/** 批量取 focus 盘 book/quote/fills (一次往返替 N×3 REST)。仅 SSE 冷启/兜底用 (hot 流未连/断时);
 *  常态 book/quote 走 SSE detail 帧推送。返回 {cid:{book,quote,fills}} 或 null (失败/超时)。 */
export async function fetchDetailBatch(
  cids: string[],
): Promise<Record<string, { book?: unknown; quote?: unknown; fills?: unknown }> | null> {
  if (cids.length === 0) return {};
  try {
    const q = `cids=${cids.map(encodeURIComponent).join(',')}`;
    const r = await fetch(`${_baseUrl}/api/v1/detail?${q}`, { method: 'POST', signal: AbortSignal.timeout(8000) });
    if (!r.ok) return null;
    return await r.json();
  } catch {
    return null;
  }
}

export const fetchPositions = (): Promise<Positions | null> => apiFetch('/api/v1/positions');
export const fetchPnlTimeseries = (window = '1h', bucket = '5m'): Promise<PnlTimeseries | null> =>
  apiFetch(`/api/v1/pnl/timeseries?window=${window}&bucket=${bucket}`);
export const fetchPnlAttribution = (): Promise<PnlAttribution | null> => apiFetch('/api/v1/pnl/attribution');

export const fetchRiskRejects = (): Promise<RiskRejects | null> => apiFetch('/api/v1/risk/rejects');
export const fetchGatePaper = (): Promise<GatePaper | null> => apiFetch('/api/v1/gate/paper');
export const fetchFeatureHealth = (): Promise<FeatureHealth | null> => apiFetch('/api/v1/features/health');
export const fetchMappingStatus = (): Promise<MappingStatus | null> => apiFetch('/api/v1/mapping/status');
export const fetchAccount = (): Promise<Account | null> => apiFetch('/api/v1/account');
export const fetchFills = (market?: string): Promise<Fills | null> =>
  apiFetch(market ? `/api/v1/fills?market=${encodeURIComponent(market)}` : '/api/v1/fills');

export const fetchMarket = (marketId: string, priority = false): Promise<Market | null> =>
  apiFetch(`/api/v1/market/${marketId}`, priority);

/** fetchBook — 返回 BinaryMarketBookView (ADR-040: /api/v1/book_pair/{conditionId}) */
export const fetchBook = (conditionId: string, priority = false): Promise<BinaryMarketBookView | null> =>
  apiFetch(`/api/v1/book_pair/${conditionId}`, priority);

export const fetchScore = (eventId: string): Promise<Score | null> =>
  apiFetch(`/api/v1/score/${eventId}`);

export const fetchQuote = (conditionId: string, priority = false): Promise<Quote | null> =>
  apiFetch(`/api/v1/quote/${conditionId}`, priority);

export async function fetchMetrics(): Promise<string | null> {
  try {
    const resp = await limitedFetch(`${_baseUrl}/metrics`, { signal: AbortSignal.timeout(12000) });
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
  if (val == null) return '—';  // #6 修: null(无数据) → 占位, 不当 0 显
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
  if (val == null) return '—';  // #6 修: null(无数据) → 占位, 不当 0 显
  const n = Number(val);
  if (!Number.isFinite(n)) return '—';
  const sign = n >= 0 ? '+' : '';
  return `${sign}${n.toFixed(1)} bps`;
}

/** 百分比 */
export function fmtPct(val: number | null | undefined): string {
  if (val == null) return '—';  // #6 修: null(无数据) → 占位, 不当 0 显
  const n = Number(val);
  if (!Number.isFinite(n)) return '—';
  return `${(n * 100).toFixed(2)}%`;
}

/** clock_sec → M:SS */
export function fmtClock(sec: number | null | undefined): string {
  if (sec == null) return '—';  // #6 修: null → 占位, 不当 0:00
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
