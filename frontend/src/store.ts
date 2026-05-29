/**
 * store.ts — 应用状态 (Solid createStore + 轮询逻辑)
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 轮询分层:
 *   status/positions:         5s
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
  fetchMarket, fetchBook, fetchScore, fetchQuote, fetchMetrics,
} from './api';
import {
  STUB_HEALTHZ, STUB_STATUS, STUB_POSITIONS, STUB_PNL_TIMESERIES,
  STUB_PNL_ATTRIBUTION, STUB_RISK_REJECTS, STUB_GATE_PAPER,
  STUB_MARKET_MAP, STUB_BOOK_MAP, STUB_SCORE_MAP, STUB_QUOTE_MAP,
  STUB_METRICS_TEXT,
} from './stub';
import type {
  Healthz, Status, Positions, PnlTimeseries, PnlAttribution,
  RiskRejects, GatePaper, BinaryMarketBookView, Market, Score, Quote,
  EventGroup, ConditionData, Position, RiskReject,
} from './types';

// ---------- stub 检测 ----------

export const USE_STUB = new URLSearchParams(location.search).get('stub') === '1';

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

export async function refreshMetrics(secondaryOpen: boolean): Promise<void> {
  if (!secondaryOpen) return;
  const text = USE_STUB ? STUB_METRICS_TEXT : await fetchMetrics();
  setState({ metrics: text ?? null });
}

// ---------- refreshMarketGrid (核心, v5 赛事分组逻辑) ----------

export async function refreshMarketGrid(): Promise<void> {
  const [posData, attrData, rejectsData] = await Promise.all([
    safeGet(fetchPositions, STUB_POSITIONS),
    safeGet(fetchPnlAttribution, STUB_PNL_ATTRIBUTION),
    safeGet(fetchRiskRejects, STUB_RISK_REJECTS),
  ]);

  if (posData) setState({ positions: posData });
  if (attrData) setState({ attribution: attrData });
  if (rejectsData) setState({ rejects: rejectsData });

  const positions: Position[] = posData?.positions ?? [];

  // positions 按 market_id 分组
  const posMap: Record<string, Position[]> = {};
  for (const p of positions) {
    if (!posMap[p.market_id]) posMap[p.market_id] = [];
    posMap[p.market_id].push(p);
  }

  // attribution per_market PnL map
  const pmPnlMap: Record<string, number> = {};
  const localAttr = attrData ?? state.attribution;
  if (localAttr?.per_market) {
    for (const pm of localAttr.per_market) {
      pmPnlMap[pm.market_id] = Number(pm.net_pnl);
    }
  }

  // rejects 按 market_id 分组
  const rejectMap: Record<string, RiskReject[]> = {};
  const localRejects = rejectsData ?? state.rejects;
  if (localRejects?.rejects) {
    for (const r of localRejects.rejects) {
      if (!rejectMap[r.market_id]) rejectMap[r.market_id] = [];
      rejectMap[r.market_id].push(r);
    }
  }

  // 三数据源并集
  const allConditionIds = new Set([
    ...Object.keys(posMap),
    ...Object.keys(rejectMap),
    ...Object.keys(pmPnlMap),
  ]);

  if (allConditionIds.size === 0) {
    setState({ eventGroups: [] });
    return;
  }

  const isDemoData = !state.status || state.status.data_source !== 'live';

  // 并发拉取 market / book / quote
  await Promise.all(
    [...allConditionIds].map(async (condId) => {
      const cached = state.conditionCache[condId];
      let market: Market | null = cached?.market ?? null;
      if (!market) {
        market = await safeGetMapped(() => fetchMarket(condId), STUB_MARKET_MAP, condId);
      }
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
        }),
      );
    }),
  );

  // score 按 event_id 去重拉取
  const eventScoreCache: Record<string, Score | null> = {};
  const eventIdsNeeded = new Set<string>();
  for (const condId of allConditionIds) {
    const mkt = state.conditionCache[condId]?.market;
    if (mkt?.event_id) eventIdsNeeded.add(mkt.event_id);
  }
  await Promise.all(
    [...eventIdsNeeded].map(async (evId) => {
      const score = await safeGetMapped(() => fetchScore(evId), STUB_SCORE_MAP, evId);
      eventScoreCache[evId] = score;
    }),
  );

  // 写回 score
  setState(
    produce((s) => {
      for (const condId of allConditionIds) {
        const mkt = s.conditionCache[condId]?.market;
        if (mkt?.event_id) {
          s.conditionCache[condId].score = eventScoreCache[mkt.event_id] ?? null;
        }
      }
    }),
  );

  // 按 event_id 分组
  const NO_EVENT = '__no_event__';
  const eventGroupMap: Record<string, EventGroup> = {};

  for (const condId of [...allConditionIds].sort()) {
    const d = state.conditionCache[condId];
    const mkt = d?.market ?? null;
    const evId = mkt?.event_id ?? NO_EVENT;

    if (!eventGroupMap[evId]) {
      eventGroupMap[evId] = {
        eventId: evId === NO_EVENT ? null : evId,
        score: d?.score ?? null,
        conditions: [],
      };
    }
    if (!eventGroupMap[evId].score && d?.score) {
      eventGroupMap[evId].score = d.score;
    }

    const cond: ConditionData = {
      conditionId: condId,
      posRows: posMap[condId] ?? [],
      market: mkt,
      book: d?.book ?? null,
      quote: d?.quote ?? null,
      rejectRows: rejectMap[condId] ?? [],
      perMarketPnl: pmPnlMap[condId] != null ? pmPnlMap[condId] : null,
      isDemoData,
    };
    eventGroupMap[evId].conditions.push(cond);
  }

  const eventGroups = Object.values(eventGroupMap).sort((a, b) => {
    const aHasPos = a.conditions.some((c) => c.posRows.length > 0);
    const bHasPos = b.conditions.some((c) => c.posRows.length > 0);
    if (aHasPos !== bHasPos) return aHasPos ? -1 : 1;
    return (a.eventId ?? '').localeCompare(b.eventId ?? '');
  });

  setState({ eventGroups });
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
  every(() => { void refreshSparkline(); }, 15000);
  every(() => { void refreshMarketGrid(); }, 5000);
  every(() => { void refreshAttribution(); }, 15000);
  every(() => { void refreshGate(); }, 15000);
  every(() => { void refreshMetrics(state.secondaryOpen); }, 30000);
  every(() => { void refreshMarketInfoSlow(); }, 60000);
}
