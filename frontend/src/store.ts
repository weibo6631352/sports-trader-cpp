/**
 * store.ts — 应用状态 (Solid createStore + 轮询逻辑) v8
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * v8 变更:
 *  - refreshMarketGrid 改用 /api/v1/events 发现市场 (不再依赖 positions)
 *  - EventGroup 增加 eventSlug / eventTitle / sport 字段
 *  - positions/pnl 真实暂为空 (paper 没跑) → 正常空值, 不报错
 *
 * 轮询分层:
 *   status/events:            5s
 *   book/score/quote per-mkt: 5s
 *   rejects:                  5s
 *   timeseries/attribution:   15s
 *   metrics:                  30s
 *   market info:              60s
 */

import { createStore, produce } from 'solid-js/store';
import {
  fetchHealthz, fetchStatus, fetchPositions, fetchPnlTimeseries,
  fetchPnlAttribution, fetchRiskRejects, fetchGatePaper,
  fetchMarket, fetchBook, fetchScore, fetchQuote, fetchMetrics, fetchEvents,
  fetchFeatureHealth, fetchMappingStatus, fetchAccount, fetchGrid,
} from './api';
import {
  STUB_HEALTHZ, STUB_STATUS, STUB_POSITIONS, STUB_PNL_TIMESERIES,
  STUB_PNL_ATTRIBUTION, STUB_RISK_REJECTS, STUB_GATE_PAPER,
  STUB_MARKET_MAP, STUB_BOOK_MAP, STUB_SCORE_MAP, STUB_QUOTE_MAP,
  STUB_METRICS_TEXT, STUB_EVENTS, STUB_ACCOUNT,
} from './stub';
import { getBaseUrl } from './api';
import type {
  Healthz, Status, Positions, PnlTimeseries, PnlAttribution,
  RiskRejects, GatePaper, BinaryMarketBookView, Market, Score, Quote,
  EventGroup, ConditionData, Position, RiskReject, FeatureHealth, MappingStatus, Account,
  EventSummary, ConditionSummary, GridMarket,
} from './types';

// ---------- stub 检测 ----------

export const USE_STUB = new URLSearchParams(location.search).get('stub') === '1';

// ---------- 请求预算 (跨洋高延迟防风暴, 2026-06-02 老雷) ----------
//
// 旧设计每 5s 一次性发 ~350 请求 (78 score + 90 盘口×3 detail), 跨洋 200ms RTT
// 下排队 ~11s 撞超时。现改为:
//  · score: 只拉 live 赛事 (gamma live=true), 封顶 SCORE_CAP。
//  · 盘口 detail(market/book/quote): 不再预取全量, 只拉「用户正在看」的盘口
//    (Trading 页展开行 + 详情页选中行), 由 detailInterest 集合驱动, 封顶 DETAIL_CAP。
// 稳态每轮请求量从 ~350 降到 ~50, 配合 api.ts 并发闸, 链路不再堵。
const SCORE_CAP = 50;
const DETAIL_CAP = 30;

/** score 拉取节流: 比分变化慢, 每 SCORE_EVERY_N 轮 grid (= N×5s) 才真正拉一次, 中间复用上轮缓存。
 *  把每轮 ~50 个 score 请求的平均负载减半 (跨洋链路削峰)。 */
const SCORE_EVERY_N = 2;
let _scoreTick = 0;

/** 当前「用户正在看」需要实时 detail 的盘口集合 (Trading 展开行 / 详情页选中行 注册) */
const detailInterest = new Set<string>();

/** 最近一次各 event 的 score 缓存 (score 节流时复用; addDetailInterest 即时拉取时回填) */
const lastEventScore: Record<string, Score | null> = {};

/** 最近一次 /events 发现的赛事列表 (供 refreshGrid 快刷时复用建组, 无需重拉 events) */
let lastEvents: EventSummary[] = [];

/** 整体替换关注集 (组件 createEffect 调用: 展开集合变化时同步) */
export function setDetailInterest(condIds: string[]): void {
  detailInterest.clear();
  for (const c of condIds) if (c) detailInterest.add(c);
}

/** 追加单个关注盘口并立即拉一次 detail (展开/选中即见数据, 不等下一轮 5s) */
export function addDetailInterest(condId: string): void {
  if (!condId) return;
  detailInterest.add(condId);
  void fetchDetailFor([condId], /*priority=*/true);  // 用户交互: 插队即时拉
}

// ---------- store shape ----------

interface PerConditionCache {
  market: Market | null;
  book: BinaryMarketBookView | null;
  quote: Quote | null;
  score: Score | null;
  summary: ConditionSummary | null;  // /grid 顶档摘要 (全市场 2s 批量刷新; 与 book/quote 全档分离)
}

interface AppState {
  healthz: Healthz | null;
  status: Status | null;
  positions: Positions | null;
  attribution: PnlAttribution | null;
  rejects: RiskRejects | null;
  gate: GatePaper | null;
  metrics: string | null;
  timeseries: PnlTimeseries | null;
  featureHealth: FeatureHealth | null;
  mappingStatus: MappingStatus | null;
  account: Account | null;
  conditionCache: Record<string, PerConditionCache>;
  eventGroups: EventGroup[];
  secondaryOpen: boolean;
}

export const [state, setState] = createStore<AppState>({
  healthz: null,
  status: null,
  positions: null,
  attribution: null,
  rejects: null,
  gate: null,
  metrics: null,
  timeseries: null,
  featureHealth: null,
  mappingStatus: null,
  account: null,
  conditionCache: {},
  eventGroups: [],
  secondaryOpen: false,
});

// ---------- stub fallback helpers ----------

async function safeGet<T>(apiFn: () => Promise<T | null>, stubData: T): Promise<T | null> {
  if (USE_STUB) return stubData;
  return apiFn();
}

async function safeGetMapped<T>(
  apiFn: () => Promise<T | null>,
  stubMap: Record<string, T>,
  key: string,
): Promise<T | null> {
  if (USE_STUB) return stubMap[key] ?? null;
  return apiFn();
}

// ---------- refreshTopBar ----------

export async function refreshTopBar(): Promise<void> {
  const [healthz, status] = await Promise.all([
    safeGet(fetchHealthz, STUB_HEALTHZ),
    safeGet(fetchStatus, STUB_STATUS),
  ]);
  setState({ healthz, status });
}

/** 仅拉 healthz (SSE status 通道不含 healthz; SSE 模式下用它单独慢刷) */
export async function refreshHealthz(): Promise<void> {
  const healthz = await safeGet(fetchHealthz, STUB_HEALTHZ);
  if (healthz) setState({ healthz });
}

// ---------- refreshSparkline ----------

export async function refreshSparkline(): Promise<void> {
  const data = await safeGet(() => fetchPnlTimeseries('1h', '1m'), STUB_PNL_TIMESERIES);
  setState({ timeseries: data });
}

// ---------- refreshAttribution ----------

export async function refreshAttribution(): Promise<void> {
  const data = await safeGet(fetchPnlAttribution, STUB_PNL_ATTRIBUTION);
  if (data) setState({ attribution: data });
}

// ---------- refreshGate ----------

export async function refreshGate(): Promise<void> {
  const data = await safeGet(fetchGatePaper, STUB_GATE_PAPER);
  if (data) setState({ gate: data });
}

// ---------- refreshMetrics ----------

/**
 * v6: Ops 页常驻, 无论是否展开均轮询 (30s).
 * 旧参数 secondaryOpen 保留签名兼容, 但已忽略.
 */
export async function refreshMetrics(_secondaryOpen?: boolean): Promise<void> {
  const text = USE_STUB ? STUB_METRICS_TEXT : await fetchMetrics();
  setState({ metrics: text ?? null });
}

// ---------- refreshFeatureHealth (老雷 2026-06-01 可观测) ----------

export async function refreshFeatureHealth(): Promise<void> {
  if (USE_STUB) return;
  const data = await fetchFeatureHealth();
  if (data) setState({ featureHealth: data });
}

// ---------- refreshAccount (老雷 2026-06-01 凯利评审: 账户现金/估值) ----------

export async function refreshAccount(): Promise<void> {
  const data = await safeGet(fetchAccount, STUB_ACCOUNT);
  setState({ account: data });
}

export async function refreshMappingStatus(): Promise<void> {
  if (USE_STUB) return;
  const data = await fetchMappingStatus();
  if (data) setState({ mappingStatus: data });
}

// ---------- refreshMarketGrid (v8: 从 /api/v1/events 发现市场) ----------

export async function refreshMarketGrid(): Promise<void> {
  // 1. 并发拉 events + positions + attribution + rejects
  const [eventsData, posData, attrData, rejectsData] = await Promise.all([
    safeGet(fetchEvents, STUB_EVENTS),
    safeGet(fetchPositions, STUB_POSITIONS),
    safeGet(fetchPnlAttribution, STUB_PNL_ATTRIBUTION),
    safeGet(fetchRiskRejects, STUB_RISK_REJECTS),
  ]);

  if (posData) setState({ positions: posData });
  if (attrData) setState({ attribution: attrData });
  if (rejectsData) setState({ rejects: rejectsData });
  // posMap/pmPnlMap/rejectMap 现由 buildEventGroups 从 state 统一计算 (refreshGrid 也复用)。

  // 2. 从 /api/v1/events 获取 condition_ids
  const events = eventsData?.events ?? [];
  if (events.length === 0) {
    setState({ eventGroups: [] });
    return;
  }
  lastEvents = events;  // 供 refreshGrid 快刷复用

  // 6. score 只拉 live 赛事 (gamma live=true), 封顶 SCORE_CAP, 且每 SCORE_EVERY_N 轮才拉一次。
  //    非 live 赛事 score 几乎不变且不显示比分, 不值得拉; live 判定用 events 自带 live 字段。
  //    复用上轮缓存 (lastEventScore) → 跳过拉取的轮次比分照常显示, 只是慢 5s 更新。
  const doFetchScores = (_scoreTick++ % SCORE_EVERY_N) === 0;
  if (doFetchScores) {
    const liveEvents = events.filter((e) => e.live === true).slice(0, SCORE_CAP);
    await Promise.all(
      liveEvents.map(async (e) => {
        lastEventScore[e.event_id] = await safeGetMapped(
          () => fetchScore(e.event_id), STUB_SCORE_MAP, e.event_id);
      }),
    );
  }

  // 7. 建组 (buildEventGroups 读 lastEvents + state + 各 module 缓存)
  //    展开行全档 detail 不在此拉 — 已独立到 refreshExpandedDetail (2s 快刷, 与顶档同步)。
  setState({ eventGroups: buildEventGroups() });
}

// ---------- refreshExpandedDetail (2s 快刷: 已展开/选中盘口的全档 book/quote) ----------

/** 拉「用户正在看」的盘口全档 detail (展开行 / 详情页选中行), 封顶 DETAIL_CAP。
 *  2s 轮询 → 展开的深度阶梯 + Quote 详情与顶档摘要价同步刷新 (不再 5s 滞后)。
 *  detailInterest 为空(无展开)时立即返回, 零请求开销。 */
export async function refreshExpandedDetail(): Promise<void> {
  const toFetch = Array.from(detailInterest).slice(0, DETAIL_CAP);
  if (toFetch.length === 0) return;
  await fetchDetailFor(toFetch);
  setState({ eventGroups: buildEventGroups() });
}

// ---------- buildEventGroups (模块级; refreshMarketGrid + refreshGrid 共用) ----------

/** 从 lastEvents + state(positions/attribution/rejects/conditionCache) + lastEventScore 建 EventGroup[]。
 *  conditions 的折叠态摘要(bid/ask/edge)取 conditionCache[*].summary (/grid 供);
 *  全档 book/quote 取 conditionCache[*].book/.quote (展开按需供)。 */
function buildEventGroups(): EventGroup[] {
  // positions 按 market_id 分组
  const posMap: Record<string, Position[]> = {};
  for (const p of state.positions?.positions ?? []) {
    (posMap[p.market_id] ??= []).push(p);
  }
  // attribution per_market PnL
  const pmPnlMap: Record<string, number> = {};
  for (const pm of state.attribution?.per_market ?? []) {
    pmPnlMap[pm.market_id] = Number(pm.net_pnl);
  }
  // rejects 按 market_id 分组
  const rejectMap: Record<string, RiskReject[]> = {};
  for (const r of state.rejects?.rejects ?? []) {
    (rejectMap[r.market_id] ??= []).push(r);
  }

  const groups: EventGroup[] = lastEvents.map((evSummary) => ({
    eventId: evSummary.event_id,
    eventSlug: evSummary.slug,
    eventTitle: evSummary.title,
    sport: evSummary.sport,
    live: evSummary.live === true,
    score: lastEventScore[evSummary.event_id] ?? null,
    conditions: evSummary.condition_ids.map((condId): ConditionData => {
      const d = state.conditionCache[condId];
      return {
        conditionId: condId,
        posRows: posMap[condId] ?? [],
        market: d?.market ?? null,
        book: d?.book ?? null,
        quote: d?.quote ?? null,
        summary: d?.summary ?? null,
        rejectRows: rejectMap[condId] ?? [],
        perMarketPnl: pmPnlMap[condId] != null ? pmPnlMap[condId] : null,
      };
    }),
  }));
  // 进行中赛事排前面, 其次有持仓
  groups.sort((a, b) => {
    const aLive = a.live || a.score?.status === 'inplay' || a.score?.status === 'halftime';
    const bLive = b.live || b.score?.status === 'inplay' || b.score?.status === 'halftime';
    if (aLive !== bLive) return aLive ? -1 : 1;
    const aHasPos = a.conditions.some((c) => c.posRows.length > 0);
    const bHasPos = b.conditions.some((c) => c.posRows.length > 0);
    if (aHasPos !== bHasPos) return aHasPos ? -1 : 1;
    return (a.eventId ?? '').localeCompare(b.eventId ?? '');
  });
  return groups;
}

// ---------- refreshGrid (2s 快刷: 全市场顶档摘要批量, 跨洋一次拉齐) ----------

/** 把一批 GridMarket 写入 conditionCache[*].summary (REST /grid 与 SSE grid 通道共用) */
function applyGridMarkets(markets: GridMarket[]): void {
  setState(
    produce((s) => {
      for (const m of markets) {
        const cid = m.condition_id;
        if (!cid) continue;
        s.conditionCache[cid] ??= { market: null, book: null, quote: null, score: null, summary: null };
        s.conditionCache[cid].summary = {
          bid: m.book_found && m.best_bid != null ? m.best_bid : null,
          ask: m.book_found && m.best_ask != null ? m.best_ask : null,
          edgeBps: m.quote_found && m.edge_bps != null ? m.edge_bps : null,
          fair: m.quote_found && m.fair != null ? m.fair : null,
          eventTs: m.event_ts ?? null,
        };
      }
    }),
  );
}

export async function refreshGrid(): Promise<void> {
  if (USE_STUB) return;  // stub 模式无 grid 端点, 由 refreshMarketGrid 的 stub 供数
  const grid = await fetchGrid();
  if (!grid?.markets) return;
  applyGridMarkets(grid.markets);
  setState({ eventGroups: buildEventGroups() });
}

// ---------- fetchDetailFor: 拉指定盘口的 market/book/quote (按需) ----------

/** 拉取指定盘口集合的 market/book/quote 写入 conditionCache (受 api.ts 并发闸控制)。
 *  priority=true (用户展开/选中触发): 请求插队到并发闸队首, 不在后台轮询后面排队 → 即时出数据。 */
export async function fetchDetailFor(condIds: string[], priority = false): Promise<void> {
  await Promise.all(
    condIds.map(async (condId) => {
      const cached = state.conditionCache[condId];
      let market: Market | null = cached?.market ?? null;
      if (!market) market = await safeGetMapped(() => fetchMarket(condId, priority), STUB_MARKET_MAP, condId);
      const bookCondId = market?.condition_id ?? condId;
      const book = await safeGetMapped(() => fetchBook(bookCondId, priority), STUB_BOOK_MAP, bookCondId);
      const quote = await safeGetMapped(() => fetchQuote(bookCondId, priority), STUB_QUOTE_MAP, bookCondId);
      setState(
        produce((s) => {
          s.conditionCache[condId] ??= { market: null, book: null, quote: null, score: null, summary: null };
          s.conditionCache[condId].market = market;
          s.conditionCache[condId].book = book;
          s.conditionCache[condId].quote = quote;
          if (market?.event_id) s.conditionCache[condId].score = lastEventScore[market.event_id] ?? null;
        }),
      );
    }),
  );
}

// ---------- refreshMarketInfoSlow (60s) ----------

export async function refreshMarketInfoSlow(): Promise<void> {
  const condIds = Object.keys(state.conditionCache);
  await Promise.all(
    condIds.map(async (condId) => {
      const market = await safeGetMapped(() => fetchMarket(condId), STUB_MARKET_MAP, condId);
      setState(
        produce((s) => {
          if (s.conditionCache[condId]) s.conditionCache[condId].market = market;
        }),
      );
    }),
  );
}

// ---------- 定时轮询初始化 ----------

function every(fn: () => void, ms: number): number {
  fn();
  return window.setInterval(fn, ms);
}

// ---------- SSE 推增量 (主通路; 失败回退轮询) ----------
//
// 设计: docs/RESEARCH/laolei-sse-push-design-v1.md。一条 EventSource 长连接接 9 通道,
//   服务端 1s 推增量, 看板秒级跳动、前端→服务端主动请求≈0。
//   失败(代理掐 SSE / 连不上)→ 自动回退到 fast 轮询, 永不比纯轮询差。
//   回退轮询与 SSE 读同一后端快照、写同一 store, 不分叉 (老郭评审)。

let _es: EventSource | null = null;
let _sseConnected = false;
let _helloTimer: number | undefined;
let _fallbackTimers: number[] = [];
let _fallbackActive = false;

/** SSE 死 → 启动 fast 轮询回退 (幂等) */
function startFallbackPolling(): void {
  if (_fallbackActive) return;
  _fallbackActive = true;
  console.warn('[stcpp] SSE 不可用, 回退轮询模式');
  _fallbackTimers.push(every(() => { void refreshTopBar(); }, 2000));
  _fallbackTimers.push(every(() => { void refreshGrid(); }, 2000));
  _fallbackTimers.push(every(() => { void refreshMarketGrid(); }, 5000));
  _fallbackTimers.push(every(() => { void refreshAccount(); }, 5000));
  _fallbackTimers.push(every(() => { void refreshAttribution(); }, 15000));
  _fallbackTimers.push(every(() => { void refreshGate(); }, 15000));
}

function stopFallbackPolling(): void {
  if (!_fallbackActive) return;
  _fallbackActive = false;
  for (const t of _fallbackTimers) clearInterval(t);
  _fallbackTimers = [];
}

/** 解析一帧信封, 返回内层 data (失败返回 null) */
function parseEnvelope(raw: string): { mode: string; data: unknown } | null {
  try {
    const env = JSON.parse(raw) as { mode?: string; data?: unknown };
    return { mode: env.mode ?? 'snapshot', data: env.data ?? null };
  } catch { return null; }
}

function connectSSE(): void {
  if (USE_STUB) { startFallbackPolling(); return; }  // stub 模式直接轮询(stub 供数)
  let url: string;
  try { url = `${getBaseUrl()}/api/v1/stream`; } catch { startFallbackPolling(); return; }

  const es = new EventSource(url);
  _es = es;

  // hello 8s 内没来 → 判定 SSE 不通, 回退轮询
  _helloTimer = window.setTimeout(() => {
    if (!_sseConnected) { try { es.close(); } catch { /* noop */ } startFallbackPolling(); }
  }, 8000);

  const on = (ch: string, fn: (data: unknown, mode: string) => void) => {
    es.addEventListener(ch, (ev: MessageEvent) => {
      const p = parseEnvelope(ev.data);
      if (p) fn(p.data, p.mode);
    });
  };

  on('hello', () => {
    _sseConnected = true;
    if (_helloTimer) clearTimeout(_helloTimer);
    stopFallbackPolling();  // SSE 通了 → 停掉回退轮询(若曾启动)
  });
  on('status', (d) => { if (d) setState({ status: d as Status }); });
  on('account', (d) => { setState({ account: (d as Account) ?? null }); });
  on('positions', (d) => { if (d) setState({ positions: d as Positions }); rebuildGroups(); });
  on('pnl', (d) => { if (d) setState({ attribution: d as PnlAttribution }); rebuildGroups(); });
  on('gate', (d) => { if (d) setState({ gate: d as GatePaper }); });
  on('rejects', (d) => { if (d) setState({ rejects: d as RiskRejects }); rebuildGroups(); });
  on('events', (d) => {
    const evs = (d as { events?: EventSummary[] })?.events;
    if (evs) { lastEvents = evs; rebuildGroups(); }
  });
  on('scores', (d) => {
    const arr = (d as { scores?: Score[] })?.scores;
    if (!arr) return;
    for (const s of arr) if (s.event_id) lastEventScore[s.event_id] = s;
    rebuildGroups();
  });
  on('grid', (d, mode) => {
    if (mode === 'delta') {
      const dd = d as { changed?: GridMarket[]; removed?: string[] };
      if (dd.changed?.length) applyGridMarkets(dd.changed);
      if (dd.removed?.length) {
        setState(produce((s) => {
          for (const cid of dd.removed!) if (s.conditionCache[cid]) s.conditionCache[cid].summary = null;
        }));
      }
    } else {
      const dd = d as { markets?: GridMarket[] };
      if (dd.markets) applyGridMarkets(dd.markets);
    }
    rebuildGroups();
  });
  // heartbeat: 仅保活, 无需处理 (收到即证明连接活着)

  es.onerror = () => {
    // EventSource 会自动重连; 仅当彻底关闭(CLOSED)才回退轮询
    if (es.readyState === EventSource.CLOSED) startFallbackPolling();
  };
}

function rebuildGroups(): void {
  setState({ eventGroups: buildEventGroups() });
}

// ---------- 定时轮询初始化 (入口) ----------

export function initPolling(): void {
  // 主通路: SSE 推 9 通道 (status/account/grid/scores/events/positions/pnl/gate/rejects)。
  //   失败自动回退到 fast 轮询 (startFallbackPolling)。
  connectSSE();

  // 始终走 REST 的常驻轮询 (SSE 不承载这些): 展开行全档 detail + Ops 页 + 净值曲线 + healthz。
  every(() => { void refreshExpandedDetail(); }, 2000);   // 展开盘口全档 (优先级插队)
  every(() => { void refreshSparkline(); }, 15000);       // 净值曲线 (timeseries)
  every(() => { void refreshMetrics(); }, 30000);         // Ops 页 Prometheus
  every(() => { void refreshMarketInfoSlow(); }, 60000);  // market 元数据
  every(() => { void refreshFeatureHealth(); }, 20000);   // Ops: 特征健康
  every(() => { void refreshMappingStatus(); }, 10000);   // Ops: 映射状态
  every(() => { void refreshHealthz(); }, 10000);         // healthz (SSE status 通道不含)
}
