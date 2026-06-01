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
  fetchFeatureHealth, fetchMappingStatus, fetchAccount,
} from './api';
import {
  STUB_HEALTHZ, STUB_STATUS, STUB_POSITIONS, STUB_PNL_TIMESERIES,
  STUB_PNL_ATTRIBUTION, STUB_RISK_REJECTS, STUB_GATE_PAPER,
  STUB_MARKET_MAP, STUB_BOOK_MAP, STUB_SCORE_MAP, STUB_QUOTE_MAP,
  STUB_METRICS_TEXT, STUB_EVENTS, STUB_ACCOUNT,
} from './stub';
import type {
  Healthz, Status, Positions, PnlTimeseries, PnlAttribution,
  RiskRejects, GatePaper, BinaryMarketBookView, Market, Score, Quote,
  EventGroup, ConditionData, Position, RiskReject, FeatureHealth, MappingStatus, Account,
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

/** 整体替换关注集 (组件 createEffect 调用: 展开集合变化时同步) */
export function setDetailInterest(condIds: string[]): void {
  detailInterest.clear();
  for (const c of condIds) if (c) detailInterest.add(c);
}

/** 追加单个关注盘口并立即拉一次 detail (展开/选中即见数据, 不等下一轮 5s) */
export function addDetailInterest(condId: string): void {
  if (!condId) return;
  detailInterest.add(condId);
  void fetchDetailFor([condId]);
}

// ---------- store shape ----------

interface PerConditionCache {
  market: Market | null;
  book: BinaryMarketBookView | null;
  quote: Quote | null;
  score: Score | null;
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

  // 2. positions 按 market_id 分组 (可能为空, live 模式 paper 未跑)
  const positions: Position[] = posData?.positions ?? [];
  const posMap: Record<string, Position[]> = {};
  for (const p of positions) {
    if (!posMap[p.market_id]) posMap[p.market_id] = [];
    posMap[p.market_id].push(p);
  }

  // 3. attribution per_market PnL map
  const pmPnlMap: Record<string, number> = {};
  const localAttr = attrData ?? state.attribution;
  if (localAttr?.per_market) {
    for (const pm of localAttr.per_market) {
      pmPnlMap[pm.market_id] = Number(pm.net_pnl);
    }
  }

  // 4. rejects 按 market_id 分组
  const rejectMap: Record<string, RiskReject[]> = {};
  const localRejects = rejectsData ?? state.rejects;
  if (localRejects?.rejects) {
    for (const r of localRejects.rejects) {
      if (!rejectMap[r.market_id]) rejectMap[r.market_id] = [];
      rejectMap[r.market_id].push(r);
    }
  }

  // 5. 从 /api/v1/events 获取 condition_ids
  const events = eventsData?.events ?? [];
  if (events.length === 0) {
    setState({ eventGroups: [] });
    return;
  }

  // 6. score 只拉 live 赛事 (gamma live=true), 封顶 SCORE_CAP, 且每 SCORE_EVERY_N 轮才拉一次。
  //    非 live 赛事 score 几乎不变且不显示比分, 不值得拉; live 判定用 events 自带 live 字段。
  //    复用上轮缓存 (lastEventScore) → 跳过拉取的轮次比分照常显示, 只是慢 5s 更新。
  const eventScoreCache: Record<string, Score | null> = { ...lastEventScore };
  const doFetchScores = (_scoreTick++ % SCORE_EVERY_N) === 0;
  if (doFetchScores) {
    const liveEvents = events.filter((e) => e.live === true).slice(0, SCORE_CAP);
    await Promise.all(
      liveEvents.map(async (e) => {
        eventScoreCache[e.event_id] = await safeGetMapped(
          () => fetchScore(e.event_id), STUB_SCORE_MAP, e.event_id);
      }),
    );
  }

  // 7. EventGroup 构造器 (读当前 conditionCache; 不等 per-condition 拉取即可建组)
  const buildGroups = (): EventGroup[] => {
    const groups: EventGroup[] = events.map((evSummary) => ({
      eventId: evSummary.event_id,
      eventSlug: evSummary.slug,
      eventTitle: evSummary.title,
      sport: evSummary.sport,
      live: evSummary.live === true,
      score: eventScoreCache[evSummary.event_id] ?? null,
      conditions: evSummary.condition_ids.map((condId) => {
        const d = state.conditionCache[condId];
        return {
          conditionId: condId,
          posRows: posMap[condId] ?? [],
          market: d?.market ?? null,
          book: d?.book ?? null,
          quote: d?.quote ?? null,
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
  };

  // 8. 立即用缓存建组 → UI 立刻显示全部赛事 (不等 per-condition 拉取)
  //    eventScoreCache 缓存进 store, 供 buildGroups 复用 (避免按需 detail 时丢 score)
  for (const eid in eventScoreCache) lastEventScore[eid] = eventScoreCache[eid];
  setState({ eventGroups: buildGroups() });

  // 9. per-condition detail 只拉「用户正在看」的盘口 (展开行 / 选中行), 封顶 DETAIL_CAP。
  //    旧设计预取 90 盘口×3 = 270 请求/5s 打爆跨洋链路; 现改按需, 折叠的行不拉。
  const toFetch = Array.from(detailInterest).slice(0, DETAIL_CAP);
  if (toFetch.length > 0) {
    await fetchDetailFor(toFetch);
    // 10. detail 到位后重建组 (含报价/book)
    setState({ eventGroups: buildGroups() });
  }
}

// ---------- fetchDetailFor: 拉指定盘口的 market/book/quote (按需) ----------

/** 拉取指定盘口集合的 market/book/quote 写入 conditionCache (受 api.ts 并发闸控制) */
export async function fetchDetailFor(condIds: string[]): Promise<void> {
  await Promise.all(
    condIds.map(async (condId) => {
      const cached = state.conditionCache[condId];
      let market: Market | null = cached?.market ?? null;
      if (!market) market = await safeGetMapped(() => fetchMarket(condId), STUB_MARKET_MAP, condId);
      const bookCondId = market?.condition_id ?? condId;
      const book = await safeGetMapped(() => fetchBook(bookCondId), STUB_BOOK_MAP, bookCondId);
      const quote = await safeGetMapped(() => fetchQuote(bookCondId), STUB_QUOTE_MAP, bookCondId);
      setState(
        produce((s) => {
          if (!s.conditionCache[condId]) {
            s.conditionCache[condId] = { market: null, book: null, quote: null, score: null };
          }
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

export function initPolling(): void {
  every(() => { void refreshTopBar(); }, 5000);
  every(() => { void refreshAccount(); }, 5000);  // 凯利评审: 账户现金/估值 5s 轮询
  every(() => { void refreshSparkline(); }, 15000);
  every(() => { void refreshMarketGrid(); }, 5000);
  every(() => { void refreshAttribution(); }, 15000);
  every(() => { void refreshGate(); }, 15000);
  // v6: metrics 无条件 30s 轮询 (Ops 页常驻消费)
  every(() => { void refreshMetrics(); }, 30000);
  every(() => { void refreshMarketInfoSlow(); }, 60000);
  // 老雷 2026-06-01: 特征健康 + 映射状态 轮询 (Ops 页可观测)
  every(() => { void refreshFeatureHealth(); }, 20000);
  every(() => { void refreshMappingStatus(); }, 10000);
}
