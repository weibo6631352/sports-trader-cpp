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

import { createSignal } from 'solid-js';
import { createStore, produce, reconcile } from 'solid-js/store';
import {
  fetchHealthz, fetchStatus, fetchPositions, fetchPnlTimeseries,
  fetchPnlAttribution, fetchRiskRejects, fetchGatePaper,
  fetchMarket, fetchBook, fetchScore, fetchQuote, fetchMetrics, fetchEvents,
  fetchFeatureHealth, fetchMappingStatus, fetchAccount, fetchFills, fetchGrid,
} from './api';
import {
  STUB_HEALTHZ, STUB_STATUS, STUB_POSITIONS, STUB_PNL_TIMESERIES,
  STUB_PNL_ATTRIBUTION, STUB_RISK_REJECTS, STUB_GATE_PAPER,
  STUB_MARKET_MAP, STUB_BOOK_MAP, STUB_SCORE_MAP, STUB_QUOTE_MAP,
  STUB_METRICS_TEXT, STUB_EVENTS, STUB_ACCOUNT,
} from './stub';
import { inflateRaw as pakoInflateRaw } from 'pako';
import { getBaseUrl, postStreamFocus, fetchDetailBatch } from './api';
import type {
  Healthz, Status, Positions, PnlTimeseries, PnlAttribution,
  RiskRejects, GatePaper, BinaryMarketBookView, Market, Score, Quote,
  EventGroup, ConditionData, Position, RiskReject, FeatureHealth, MappingStatus, Account,
  EventSummary, ConditionSummary, GridMarket, Fills, Fill,
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

/** 全局 UI 时钟 (1s, 与数据推送节奏一致; 2026-06-05 老板「量化AI 刷新慢」诊断配套): 任何"数据年龄"对 uiNow() 求差即重算,
 *  使【活但静市场】(quote 每秒重发但值不变) 与【真冻】(快照停更) 在面板上可视区分 ——
 *  age 小且稳=实时·静市场, age 持续增长=数据滞后。initPolling 启一个 setInterval 驱动, 全站复用。 */
export const [uiNow, setUiNow] = createSignal(Date.now());

// ---- SSE focus 订阅状态 (连接池架构 2026-06-05, 老板「多开点链路负载更大更灵活」) ----
// 看板 bulk 走 /api/v1/stream (全局数据, 单条无法按盘分片); 每盘 book/quote 走 hot 连接【池】
//   (/api/v1/stream/hot), 按盯盘数弹性扩展多条独立 TCP, 把盘分摊到各连接 → 各自拥塞窗口、互不队头阻塞。
//   真因: 带宽够, 卡=挤一条 TCP 干等(队头阻塞+阻塞写串行); 多条独立链路并行 = 不再干等。
let _streamId: string | null = null;       // bulk 流 hello 下发 (诊断用; focus 不走它)
// 2026-06-05 老板「没必要 rest 的用 sse + 全部打包一次性」: hot 流改【单连接 + 批量 detail 帧】。
//   服务端把所有 focus 盘 book/quote/fills 合成一帧推 → 一条 TCP 够用, 多开反而撞浏览器 6 连接/域上限
//   (跨洋实测多连接各卡各的 + 抢 REST 槽)。1 bulk + 1 hot = 2 长连接, 留 4 给 REST/多标签。
const HOT_MAX_CONNS = 1;                    // hot 单连接 (批量帧, 不再多开链路 — 6 连接/域硬限下越多越抢)
const HOT_PER_CONN = 64;                    // 单连接带全部 focus (≥ HOT_FOCUS_CAP, 不分片)
const HOT_FOCUS_CAP = 30;                   // 总 focus 盘上限 (≤ 服务端 kMaxFocus=32)

interface HotConn {
  es: EventSource;
  streamId: string | null;
  connected: boolean;
  cids: string[];                 // 本连接负责的盘 (shard)
  focusSeq: number;               // 本连接独立单调序 (服务端 focus_seq 对账)
  helloTimer?: number;
  lastFocusFrameMs: number;       // 最近 book/quote 帧到达 (半卡看门狗, 基于帧到达非 ts)
  lastReconnectMs: number;        // 本连接重连时刻 (宽限防风暴)
}
const _hotPool: HotConn[] = [];   // hot 连接池 (弹性: 无 focus 时 0 条, 盯盘越多越多条, 封顶 HOT_MAX_CONNS)

/** focus 变化 → 重平衡 hot 连接池 (弹性开/关连接 + 盘 round-robin 分摊 + 各连接 POST 自己的 shard)。 */
function maybePostFocus(): void {
  rebalanceHotPool();
}

/** 最近一次各 event 的 score 缓存 (score 节流时复用; addDetailInterest 即时拉取时回填) */
const lastEventScore: Record<string, Score | null> = {};

/** 最近一次 /events 发现的赛事列表 (供 refreshGrid 快刷时复用建组, 无需重拉 events) */
let lastEvents: EventSummary[] = [];

/** 持仓首见时刻 (本会话墙钟; 老板 2026-06-05「盯盘人要知道仓持了多久」)。后端暂无 entry_ts (N1 待补),
 *  前端按「本会话首次见到该 (市场,边) 有非零仓」近似。刷新页面会重置 → UI 标注「~本会话估」。
 *  平仓 (净仓归零) 即清除, 下次再建仓重新计时。 */
const positionFirstSeen: Record<string, number> = {};
export function posSeenAt(marketId: string, outcome: string): number | null {
  return positionFirstSeen[`${marketId}|${outcome}`] ?? null;
}
function recordPositionSeen(p: Positions | null): void {
  if (!p) return;
  const now = Date.now();
  const live = new Set<string>();
  for (const pos of p.positions) {
    if (Math.abs(Number(pos.net_qty)) > 0) {
      const k = `${pos.market_id}|${pos.outcome}`;
      live.add(k);
      if (!(k in positionFirstSeen)) positionFirstSeen[k] = now;
    }
  }
  for (const k of Object.keys(positionFirstSeen)) if (!live.has(k)) delete positionFirstSeen[k];
}

/** sharp/mid 时序环 (老板 2026-06-05「盘口趋势/收敛发散」): 前端自攒 sharp fair 与 PM mid 的时序,
 *  供盯盘画「市场价相对 sharp 收敛/发散」迷你图。module-level 环 (非 store, 避免 600 盘×60 样本 reactivity
 *  churn); 组件随 1s uiNow tick 读取重绘。从 /grid (全市场 2s) + 展开 quote (更鲜) 双源 push, 值不变去重。 */
export interface SharpTrendPt { ts: number; sharp: number; mid: number }
const sharpTrendRing: Record<string, SharpTrendPt[]> = {};
const SHARP_TREND_CAP = 60;
export function pushSharpTrend(cid: string | undefined, sharp: number | null | undefined, mid: number | null | undefined, ts: number): void {
  if (!cid) return;
  const s = Number(sharp), m = Number(mid);
  if (!(s > 0 && s < 1) || !Number.isFinite(m)) return;
  const ring = (sharpTrendRing[cid] ??= []);
  const last = ring[ring.length - 1];
  if (last && Math.abs(last.sharp - s) < 1e-6 && Math.abs(last.mid - m) < 1e-6) return;  // 静市场去重
  ring.push({ ts, sharp: s, mid: m });
  if (ring.length > SHARP_TREND_CAP) ring.shift();
}
export function getSharpTrend(cid: string): SharpTrendPt[] {
  return sharpTrendRing[cid] ?? [];
}

/** 整体替换关注集 (组件 createEffect 调用: 展开集合变化时同步)。
 *  幂等: 集合未变则跳过 —— 防 SSE 每帧 rebuildGroups → effect 重跑 → 反复 postFocus 风暴。 */
export function setDetailInterest(condIds: string[]): void {
  const next = condIds.filter(Boolean);
  if (next.length === detailInterest.size && next.every((c) => detailInterest.has(c))) return;
  detailInterest.clear();
  for (const c of next) detailInterest.add(c);
  // 半卡看门狗基准: 非空→起计(已有则保留), 空→清 0 (无 focus 时不查半卡)。
  _focusActiveSinceMs = next.length > 0 ? (_focusActiveSinceMs || Date.now()) : 0;
  maybePostFocus();  // SSE: 告知服务端推这些盘口的全档 book/quote
}

/** 追加单个关注盘口并立即拉一次 detail (展开/选中即见数据, 不等下一轮 5s) */
export function addDetailInterest(condId: string): void {
  if (!condId) return;
  if (detailInterest.size === 0) _focusActiveSinceMs = Date.now();  // 首个 focus 起计 (半卡看门狗基准)
  detailInterest.add(condId);
  // 首屏兜底 (评审: 必选): 展开瞬间优先级 REST 即时拉一次, ~200ms 出数据; SSE 随后接管增量。
  void fetchDetailFor([condId], /*priority=*/true);
  maybePostFocus();  // SSE: 告知服务端开始推该盘口
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
  fills: Fills | null;  // 成交流水 (老板「看懂买卖价」) — 全局最近 (AnalyticsPage 全量日志)
  fillsByMarket: Record<string, Fill[]>;  // 累积 per-market 成交 (全局环churn快; 盯盘按盘留住历史)
  conditionCache: Record<string, PerConditionCache>;
  eventGroups: EventGroup[];
  liveGames: Score[];  // 盯盘看板: 全部 in-play 比赛比分 (SSE scores 通道直推; 老板「人盯盘」)
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
  fills: null,
  fillsByMarket: {},
  liveGames: [],
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

// ---------- refreshFills (2026-06-04 老板「多少价格买的/卖出的都不知道」) ----------

export async function refreshFills(): Promise<void> {
  if (USE_STUB) return;
  const data = await fetchFills();
  if (!data) return;
  setState({ fills: data });
  // 累积 per-market: 全局环 churn 很快(模型驱动 100+笔/30s), 这里按盘留住成交历史 →
  //   盯盘展开任一盘(含已平仓 flat)都能看到它"多少价买的/卖的"。dedup + 每盘留 40 笔, 最新在前。
  const keyOf = (f: Fill) => `${f.as_of_ts}|${f.side}|${f.outcome}|${f.price}|${f.size_usdc}`;
  const touched = new Set<string>();
  for (const f of data.fills) touched.add(f.market_id);
  // produce patch 只动 touched 盘 (架构师 A4/B-9: 原 setState({fillsByMarket: 全量新对象}) 每5s 炸所有
  //   展开盘 MarketFills 重渲染, 即便那盘成交没变)。produce 只标记被改的 market key → 只重渲染变了的那盘。
  setState(produce((s) => {
    for (const m of touched) {
      const existing = s.fillsByMarket[m] ?? [];
      const seen = new Set(existing.map(keyOf));
      const merged = existing.slice();
      for (const f of data.fills) {
        if (f.market_id === m && !seen.has(keyOf(f))) merged.push(f);
      }
      merged.sort((a, b) => b.as_of_ts - a.as_of_ts);
      s.fillsByMarket[m] = merged.slice(0, 40);
    }
  }));
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

  if (posData) { setState({ positions: posData }); recordPositionSeen(posData); }
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

// ---------- refreshExpandedDetail (SSE 冷启/兜底: 批量取展开盘全档 book/quote/fills) ----------

/** 批量拉「用户正在看」的盘口全档 detail —— 【一次往返】(POST /api/v1/detail) 替原 N×3 个 REST。
 *  2026-06-05 老板「没必要 rest 的用 sse + 全部打包一次性, 网络往返太浪费」: 常态 book/quote 走 SSE
 *  detail 帧推送 (零轮询); 本函数仅【SSE 冷启 (展开瞬间) + hot 流断时兜底】用, 不再 2s 常驻轮询。 */
export async function refreshExpandedDetail(): Promise<void> {
  const toFetch = Array.from(detailInterest).slice(0, DETAIL_CAP);
  if (toFetch.length === 0) return;
  const map = await fetchDetailBatch(toFetch);
  if (!map) return;
  setState(produce((s) => {
    for (const cid of Object.keys(map)) {
      if (!detailInterest.has(cid)) continue;
      const ent = map[cid];
      const bk = ent.book as (BinaryMarketBookView & { found?: boolean }) | undefined;
      const qt = ent.quote as (Quote & { found?: boolean }) | undefined;
      const fl = ent.fills as { fills?: unknown[] } | undefined;
      s.conditionCache[cid] ??= { market: null, book: null, quote: null, score: null, summary: null };
      if (bk && bk.found === true) s.conditionCache[cid].book = bk;
      if (qt && qt.found === true) { s.conditionCache[cid].quote = qt; pushSharpTrend(cid, qt.sharp_fair, qt.market_mid, Date.now()); }
      if (fl && Array.isArray(fl.fills)) s.fillsByMarket[cid] = fl.fills as Fill[];
    }
  }));
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
    iconUrl: evSummary.icon_url ?? null,
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
          // 赔率源 sharp + 市场 mid (老板 2026-06-05「赔率源=真值」): 折叠摘要行直接显 sharp 偏离。
          //   sharp_fair = -1 表示无 odds/未映射 → 存 null (前端显 '—' 而非假 0)。
          sharp: m.quote_found && m.sharp_fair != null && m.sharp_fair > 0 && m.sharp_fair < 1 ? m.sharp_fair : null,
          mid: m.quote_found && m.market_mid != null ? m.market_mid : null,
          eventTs: m.event_ts ?? null,
          kickoffTs: m.kickoff_ts && m.kickoff_ts > 0 ? m.kickoff_ts : null,
          gameState: m.game_state ?? null,
          title: m.title ?? null,
          outcome0: m.outcome0 ?? null,
          outcome1: m.outcome1 ?? null,
        };
      }
    }),
  );
  // sharp/mid 时序环 (收敛/发散迷你图): 全市场每轮 grid push 一点 (值不变自动去重)。
  const nowMs = Date.now();
  for (const m of markets) {
    const sh = m.quote_found && m.sharp_fair != null && m.sharp_fair > 0 && m.sharp_fair < 1 ? m.sharp_fair : null;
    const md = m.quote_found && m.market_mid != null ? m.market_mid : null;
    pushSharpTrend(m.condition_id, sh, md, nowMs);
  }
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
      // book/quote/fills 直接用 condId 拉 (端点 key = condId)。
      //   修 bug: 原先先拉 market 再用 market.condition_id 拉 book → market 没缓存住时每轮重拉
      //   market(58次/6s 风暴)挤满并发闸、把 book 拉取饿死 → 后端有簿前端却"未接入"。
      // 2026-06-04 老板「订单簿和量化 ai 加载的很慢」: 服务器端点 <1ms, 慢=跨洋 RTT × 串行往返。
      //   book→quote→fills 三次串行 await = 3× RTT 才出数据 → 改 Promise.all 并行, 1× RTT 同时到。
      //   成交折进同一拉取 (老板「都走同一个」「刷新对齐订单簿/量化 ai」): 单一 store 源 + 同 2s 节拍。
      const [book, quote, fd] = await Promise.all([
        safeGetMapped(() => fetchBook(condId, priority), STUB_BOOK_MAP, condId),
        safeGetMapped(() => fetchQuote(condId, priority), STUB_QUOTE_MAP, condId),
        fetchFills(condId),
      ]);
      setState(
        produce((s) => {
          s.conditionCache[condId] ??= { market: null, book: null, quote: null, score: null, summary: null };
          s.conditionCache[condId].book = book;
          s.conditionCache[condId].quote = quote;
          if (fd) s.fillsByMarket[condId] = fd.fills;
        }),
      );
      if (quote) pushSharpTrend(condId, quote.sharp_fair, quote.market_mid, Date.now());  // 展开盘更鲜的 sharp/mid 点
      // market 元数据仅在用户交互(priority)且未缓存时拉一次 —— 常驻 refreshExpandedDetail
      //   (priority=false) 不拉 market, 彻底消除 market 重拉风暴; 元数据由 refreshMarketInfoSlow(60s) 兜。
      if (priority && !state.conditionCache[condId]?.market) {
        const market = await safeGetMapped(() => fetchMarket(condId, priority), STUB_MARKET_MAP, condId);
        if (market) {
          setState(produce((s) => {
            if (s.conditionCache[condId]) {
              s.conditionCache[condId].market = market;
              if (market.event_id) s.conditionCache[condId].score = lastEventScore[market.event_id] ?? null;
            }
          }));
        }
      }
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

let _es: EventSource | null = null;          // bulk 流 (/api/v1/stream): 9 通道 + Ops (单条)
let _sseConnected = false;
let _helloTimer: number | undefined;
// hot 行情连接【池】: _hotPool (见上方 focus 订阅状态区). 每条 /api/v1/stream/hot 独立 TCP, 带自己的 shard。
let _fallbackTimers: number[] = [];
let _fallbackActive = false;
// SSE 存活看门狗 (老板 2026-06-05「订单簿新鲜度阻塞严重延迟」根因): EventSource 半死 (TCP 开 /
//   readyState=OPEN 但帧停, 不触发 onerror/CLOSED) → 前端静默停滞且永不重连 (旧逻辑仅 CLOSED 才重连)。
//   实测: 服务端重启时所有已开 tab 的 SSE 连接半死, 订单簿新鲜度一路 climbing, reload 才恢复。
//   看门狗: 任何帧 (含 heartbeat) 刷新 _lastSseFrameMs; 超 SSE_STALE_MS 无帧 → 强制重连 → 自愈。
const SSE_STALE_MS = 12_000;        // 任何帧停超此 = 全断
const FOCUS_STALE_MS = 18_000;      // 展开盘有 focus 却超此收不到任何 book/quote 帧 = 半卡 (服务端 warmup 典型)
let _lastSseFrameMs = 0;            // 最近【bulk 流任何帧】到达时刻 (bulk 全断检测)
let _lastFocusFrameMs = 0;          // 最近【任一 hot 连接 book/quote】帧 (全局粗粒度健康; 细粒度按连接 conn.lastFocusFrameMs)
let _focusActiveSinceMs = 0;        // detailInterest 变非空的时刻 (0 = 无 focus); 半卡看门狗基准
let _lastReconnectMs = 0;           // bulk 最近重连时刻 (宽限防风暴)
let _sseWatchdogStarted = false;

/** SSE 死 → 启动 fast 轮询回退 (幂等) */
function startFallbackPolling(): void {
  if (_fallbackActive) return;
  _fallbackActive = true;
  console.warn('[stcpp] SSE 不可用, 回退轮询模式');
  _fallbackTimers.push(every(() => { void refreshTopBar(); }, 2000));
  _fallbackTimers.push(every(() => { void refreshGrid(); }, 2000));
  _fallbackTimers.push(every(() => { void refreshMarketGrid(); }, 5000));
  _fallbackTimers.push(every(() => { void refreshAccount(); }, 5000));
  // refreshFills 已移到 initPolling 常驻 (fills 无 SSE 通道, 不分 SSE 死活都要拉) — 此处不再重复挂。
  _fallbackTimers.push(every(() => { void refreshAttribution(); }, 15000));
  _fallbackTimers.push(every(() => { void refreshGate(); }, 15000));
  // Ops/慢通道: SSE 活时由 healthz/features/mapping/timeseries 通道推; 仅回退时轮询。
  _fallbackTimers.push(every(() => { void refreshSparkline(); }, 15000));
  _fallbackTimers.push(every(() => { void refreshFeatureHealth(); }, 20000));
  _fallbackTimers.push(every(() => { void refreshMappingStatus(); }, 10000));
  _fallbackTimers.push(every(() => { void refreshHealthz(); }, 10000));
  // SSE 断 → focus 推送也死, 展开盘的全档 book/quote 改由 REST 兜 (2s)。SSE 活时不跑 (服务端推)。
  _fallbackTimers.push(every(() => { void refreshExpandedDetail(); }, 2000));
}

function stopFallbackPolling(): void {
  if (!_fallbackActive) return;
  _fallbackActive = false;
  for (const t of _fallbackTimers) clearInterval(t);
  _fallbackTimers = [];
}

/** 强制重连 bulk 流 (全断自愈): 关旧 ES → 重置 → 新建。 */
function reconnectBulkSSE(reason: string): void {
  console.warn(`[stcpp] SSE(bulk) ${reason} → 强制重连`);
  try { _es?.close(); } catch { /* noop */ }
  _sseConnected = false;
  const t = Date.now();
  _lastSseFrameMs = t;     // 给新连接 hello 窗口, 避免立即再判死
  _lastReconnectMs = t;
  connectSSE();
}

/** 重连池中某条 hot 连接 (半卡/半死自愈): 原地换新 EventSource, 保留它的 shard; 新 hello 重订该 shard。 */
function reconnectHotConn(conn: HotConn, reason: string): void {
  const idx = _hotPool.indexOf(conn);
  if (idx < 0) return;
  console.warn(`[stcpp] hot 连接[${idx}] ${reason} → 重连`);
  try { conn.es.close(); } catch { /* noop */ }
  if (conn.helloTimer) clearTimeout(conn.helloTimer);
  const fresh = openHotConn();
  fresh.cids = conn.cids;            // 保留 shard, 新 hello 会 postHotFocus 重订
  const t = Date.now();
  fresh.lastReconnectMs = t;         // 给新连接 FOCUS_STALE_MS 宽限, 防风暴
  fresh.lastFocusFrameMs = t;
  _hotPool[idx] = fresh;
}

/** 看门狗 tick (5s): 两类卡死自愈 (老板 2026-06-05「根治」)。健康连接每 1-2s 有 book/quote 帧 → 不误触。
 *  ① 全断: 任何帧 (grid/status) 都停超 SSE_STALE_MS。
 *  ② 半卡: 全局帧在来但 focus 的 book/quote 不来 (服务端 warmup / 连接半死典型) — 展开盘有 focus 却超
 *     FOCUS_STALE_MS 收不到任何 book/quote 帧。判据用【帧到达】非 data_source_ts (静市场 book 不变但
 *     服务端每 tick 必推帧, data_source_ts 老属正常, 不该误判 → 否则会无谓重连健康的静市场)。 */
/** SSE 管道存活年龄 (ms): now − 最近任何帧到达时刻 (老板 2026-06-05「飘」根治: 把「管道是否实时」与
 *  「订单簿版本年龄」两个语义拆开)。健康连接每 1-2s 有帧 → 亚秒级稳定 = 真·管道实时指标 (不随静市场飘)。
 *  NaN = 未连过。组件读它 (配 uiNow 每秒重算) 显「SSE ● 实时」灯。 */
export function sseAgeMs(): number { return _lastSseFrameMs > 0 ? Date.now() - _lastSseFrameMs : Number.NaN; }
export function sseIsAlive(): boolean { return _sseConnected && !_fallbackActive; }

function sseWatchdogTick(): void {
  if (USE_STUB || _fallbackActive) return;
  const now = Date.now();
  // ① bulk 全断 (grid/status/heartbeat 都停)
  if (_sseConnected && _lastSseFrameMs > 0 && now - _lastSseFrameMs > SSE_STALE_MS) {
    reconnectBulkSSE(`静默 ${Math.round((now - _lastSseFrameMs) / 1000)}s (bulk 全断)`);
  }
  // ② hot 池半卡: 逐条查 — 连上且有 shard 但 book/quote 不来 (连接半死 / 服务端 warmup 典型) → 重连该条。
  //    判据用【帧到达】非 data_source_ts (静市场 book 不变但服务端每 tick 必推帧, 老 ts 属正常不该误判)。
  if (detailInterest.size > 0) {
    for (const conn of [..._hotPool]) {  // 拷贝迭代: reconnectHotConn 会原地换元素
      if (!conn.connected || conn.cids.length === 0) continue;
      const ref = Math.max(_focusActiveSinceMs, conn.lastReconnectMs);       // 何时开始期待 focus 帧
      const lastFocus = Math.max(conn.lastFocusFrameMs, conn.lastReconnectMs);
      if (now - ref > FOCUS_STALE_MS && now - lastFocus > FOCUS_STALE_MS) {
        reconnectHotConn(conn, `focus book/quote ${Math.round((now - lastFocus) / 1000)}s 无帧 (半卡)`);
      }
    }
  }
}

/** 解析一帧信封, 返回内层 data (失败返回 null) */
function parseEnvelope(raw: string): { mode: string; data: unknown; focusSeq: number | null } | null {
  try {
    const env = JSON.parse(raw) as { mode?: string; data?: unknown; focus_seq?: number; enc?: string };
    let data = env.data ?? null;
    // enc="df" (raw-deflate+base64; 老板「gzip 压缩帧」): atob → bytes → pako.inflateRaw → JSON。
    //   pako 同步, parseEnvelope 保持同步 (零 ripple)。仅 ?gz=1 连接的大帧才压, 小帧/旧连接走原文。
    if (env.enc === 'df' && typeof data === 'string') {
      const bin = atob(data);
      const bytes = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
      data = JSON.parse(pakoInflateRaw(bytes, { to: 'string' }) as string);
    }
    return { mode: env.mode ?? 'snapshot', data, focusSeq: env.focus_seq ?? null };
  } catch { return null; }
}

function connectSSE(): void {
  if (USE_STUB) { startFallbackPolling(); return; }  // stub 模式直接轮询(stub 供数)
  let url: string;
  try { url = `${getBaseUrl()}/api/v1/stream`; } catch { startFallbackPolling(); return; }

  const es = new EventSource(url);
  _es = es;
  _lastSseFrameMs = Date.now();  // 连接建立即视为刚收帧 (hello 窗口内不误判半死)

  // hello 8s 内没来 → 判定 SSE 不通, 回退轮询
  _helloTimer = window.setTimeout(() => {
    if (!_sseConnected) { try { es.close(); } catch { /* noop */ } startFallbackPolling(); }
  }, 8000);

  const on = (ch: string, fn: (data: unknown, mode: string, focusSeq: number | null) => void) => {
    es.addEventListener(ch, (ev: MessageEvent) => {
      _lastSseFrameMs = Date.now();  // 任何帧 = 连接活着 (看门狗存活信号)
      const p = parseEnvelope(ev.data);
      if (p) fn(p.data, p.mode, p.focusSeq);
    });
  };
  // heartbeat: 不进 on() (无 envelope), 单独监听仅刷新存活时间戳 (空闲连接靠它喂看门狗, 防误重连)。
  es.addEventListener('heartbeat', () => { _lastSseFrameMs = Date.now(); });

  on('hello', (d) => {
    _sseConnected = true;
    if (_helloTimer) clearTimeout(_helloTimer);
    stopFallbackPolling();  // SSE 通了 → 停掉回退轮询(若曾启动)
    _streamId = (d as { stream_id?: string })?.stream_id ?? null;  // 诊断用; focus 走 hot 流不走它
  });
  on('status', (d) => { if (d) setState({ status: d as Status }); });
  on('account', (d) => { setState({ account: (d as Account) ?? null }); });
  on('positions', (d) => { if (d) { setState({ positions: d as Positions }); recordPositionSeen(d as Positions); } rebuildGroups(); });
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
    // SSE scores 是【当前全部 in-play 比赛】的权威快照 (snapshot 模式) → 重建缓存, 清掉已结束/掉出
    //   feed 的比赛 (老板 2026-06-03: 比赛已结束却还显示进行中 = 旧缓存从不清理的 bug)。
    for (const k of Object.keys(lastEventScore)) delete lastEventScore[k];
    for (const s of arr) if (s.event_id) lastEventScore[s.event_id] = s;
    setState({ liveGames: arr });  // 盯盘看板: 全部 in-play 比赛 (老板「人盯盘看比分/赛点/进度」)
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
  // (focus book/quote 已拆到独立 hot 连接池 — bulk 流不接收 book/quote, 见 rebalanceHotPool)
  // Ops/慢通道 (healthz/features/mapping/timeseries) — SSE 推, 取代常驻轮询
  on('healthz', (d) => { if (d) setState({ healthz: d as Healthz }); });
  on('features', (d) => { if (d) setState({ featureHealth: d as FeatureHealth }); });
  on('mapping', (d) => { if (d) setState({ mappingStatus: d as MappingStatus }); });
  on('timeseries', (d) => { setState({ timeseries: (d as PnlTimeseries) ?? null }); });
  // heartbeat: 仅保活, 无需处理 (收到即证明连接活着)

  es.onerror = () => {
    // EventSource 会自动重连; 仅当彻底关闭(CLOSED)才回退轮询
    if (es.readyState === EventSource.CLOSED) startFallbackPolling();
  };
}

// ============ hot 行情连接池 (focus book/quote, 按盯盘数弹性分片到多条独立 TCP) ============
//   老板「多开点链路负载更大更灵活」+ 跨洋实测「各连接独立卡 / 各自拥塞窗口」。
//   bulk 卡不波及任一 hot; hot 之间也各卡各的。失败不致命: book/quote 有 refreshExpandedDetail REST 兜底。

/** hot 池整体连接健康 (所有在册连接都 connected)。REST 兜底据此决定是否补拉。 */
function hotPoolAllConnected(): boolean {
  return _hotPool.length > 0 && _hotPool.every((c) => c.connected);
}

/** hot 连接收到的【批量 detail 帧】→ 一帧含所有 focus 盘的 book/quote/fills, 一次写入 conditionCache。
 *  2026-06-05 老板「全部打包一次性, 网络往返太浪费」: 替代原逐盘 book/quote 帧 (N×2 → 1)。 */
function handleHotDetail(conn: HotConn, d: unknown): void {
  const map = d as Record<string, { book?: unknown; quote?: unknown; fills?: unknown }> | null;
  if (!map || typeof map !== 'object') return;
  const t = Date.now();
  conn.lastFocusFrameMs = t; _lastFocusFrameMs = t;  // 半卡看门狗存活信号 (帧到达)
  const sharpPts: Array<{ cid: string; sharp: number | null | undefined; mid: number | null | undefined }> = [];
  setState(produce((s) => {
    for (const cid of Object.keys(map)) {
      if (!detailInterest.has(cid)) continue;
      const ent = map[cid];
      const bk = ent.book as (BinaryMarketBookView & { found?: boolean }) | undefined;
      const qt = ent.quote as (Quote & { found?: boolean }) | undefined;
      const fl = ent.fills as { fills?: unknown[] } | undefined;
      s.conditionCache[cid] ??= { market: null, book: null, quote: null, score: null, summary: null };
      if (bk && bk.found === true) s.conditionCache[cid].book = bk;   // found:false 不覆盖已有簿
      if (qt && qt.found === true) {
        s.conditionCache[cid].quote = qt;
        sharpPts.push({ cid, sharp: qt.sharp_fair, mid: qt.market_mid });
      }
      if (fl && Array.isArray(fl.fills)) s.fillsByMarket[cid] = fl.fills as Fill[];
    }
  }));
  for (const p of sharpPts) pushSharpTrend(p.cid, p.sharp, p.mid, t);
  rebuildGroups();
}

/** 上报某条 hot 连接的 shard 给服务端 (focus POST)。 */
function postHotFocus(conn: HotConn): void {
  if (!conn.streamId) return;
  conn.focusSeq += 1;
  void postStreamFocus(conn.streamId, conn.focusSeq, conn.cids);
}

/** 开一条 hot 连接 (返回 HotConn; cids 由 rebalance 随后赋值, hello 时 postHotFocus 上报)。 */
function openHotConn(): HotConn {
  const url = `${getBaseUrl()}/api/v1/stream/hot`;
  const es = new EventSource(url);
  const conn: HotConn = {
    es, streamId: null, connected: false, cids: [], focusSeq: 0,
    lastFocusFrameMs: Date.now(), lastReconnectMs: Date.now(),
  };
  conn.helloTimer = window.setTimeout(() => {
    if (!conn.connected) console.warn('[stcpp] hot 连接 hello 超时, 该片 book/quote 暂由 REST 兜底');
  }, 8000);
  const onHot = (ch: string, fn: (data: unknown) => void) => {
    es.addEventListener(ch, (ev: MessageEvent) => {
      const p = parseEnvelope(ev.data);
      if (p) fn(p.data);
    });
  };
  onHot('hello', (d) => {
    conn.connected = true;
    if (conn.helloTimer) clearTimeout(conn.helloTimer);
    conn.streamId = (d as { stream_id?: string })?.stream_id ?? null;
    postHotFocus(conn);  // (重)连后上报本连接的 shard
  });
  onHot('detail', (d) => handleHotDetail(conn, d));  // 批量帧: 一帧含所有 focus 盘 book/quote/fills
  es.onerror = () => {
    // EventSource 自动重连; 彻底关闭不回退全局轮询 (book/quote 由 refreshExpandedDetail REST 兜)。
    if (es.readyState === EventSource.CLOSED) conn.connected = false;
  };
  return conn;
}

/** 关一条 hot 连接。 */
function closeHotConn(conn: HotConn): void {
  try { conn.es.close(); } catch { /* noop */ }
  if (conn.helloTimer) clearTimeout(conn.helloTimer);
  conn.connected = false;
}

/** 重平衡连接池: 按 focus 盘数弹性开/关连接, 把盘 round-robin 分摊到各连接, 变化的连接重发 focus。
 *  无 focus → 0 连接; 盯盘越多 → 越多条 (封顶 HOT_MAX_CONNS), 每条只扛 ~HOT_PER_CONN 个盘。 */
function rebalanceHotPool(): void {
  if (USE_STUB) return;
  const all = Array.from(detailInterest).slice(0, HOT_FOCUS_CAP);
  const desired = all.length === 0 ? 0 : Math.min(HOT_MAX_CONNS, Math.ceil(all.length / HOT_PER_CONN));
  while (_hotPool.length < desired) _hotPool.push(openHotConn());
  while (_hotPool.length > desired) { const c = _hotPool.pop(); if (c) closeHotConn(c); }
  if (_hotPool.length === 0) return;
  // round-robin 分摊 (盘均匀散到各连接)
  const shards: string[][] = Array.from({ length: _hotPool.length }, () => []);
  all.forEach((cid, i) => shards[i % _hotPool.length].push(cid));
  _hotPool.forEach((conn, i) => {
    const next = shards[i];
    const changed = conn.cids.length !== next.length || conn.cids.some((c, j) => c !== next[j]);
    conn.cids = next;
    if (changed && conn.connected) postHotFocus(conn);  // shard 变了且已连上 → 重发 (未连上者 hello 时发)
  });
}

// rebuildGroups — 重建 eventGroups 树并写回 store (触发网格重渲染)。
//   合并节流 (2026-06-02 老板「操作有时很卡, 点着不动」): 8 个 SSE 通道都调它, 其中 book/quote
//   通道每帧都来 (全展 38 盘 → 38 book + 38 quote on-change/s) → 原来每帧重建整树 + 重渲染全网格
//   (含 38 个展开订单簿阶梯) → 每秒几十次 → 主线程占满 → 点击事件排不上 = 卡死。
//   修: 120ms 窗口内的所有调用合并成 1 次重建 (≤~8 次/s), 主线程腾出给交互。视觉延迟 ≤120ms 无感。
//   2026-06-02 加重节流 200→450ms: 全盘口期盘口数涨到 600+, 每次 buildEventGroups 重建整树 +
//   <For> 按引用重渲染所有事件头(含图片)≈ 126ms/次; 200ms 节流 → 稳态 ~3 卡顿/秒。450ms → ~2/秒,
//   价格 2 次/秒更新视觉无感, 卡顿明显缓解。(根治需行级细粒度读 store, 列为后续。)
let _rebuildTimer: number | undefined;
function rebuildGroups(): void {
  if (_rebuildTimer != null) return;  // 窗口内已排程 → 吸收本次调用
  _rebuildTimer = window.setTimeout(() => {
    _rebuildTimer = undefined;
    // reconcile 按 eventId diff: 只更新内容变化的事件组 → <For> 只重渲染变化的事件头 (不再
    //   按引用全量重建 22 个事件头+图片)。根治"全盘口期重建慢"的核心。eventId 唯一 (真 event id /
    //   合成盘=condition_id)。
    setState('eventGroups', reconcile(buildEventGroups(), { key: 'eventId', merge: false }));
  }, 450);
}

// ---------- 定时轮询初始化 (入口) ----------

export function initPolling(): void {
  // 连接池架构 (2026-06-05 老板「多开点链路负载更大更灵活」+ 跨洋实测背书):
  //   bulk 流 connectSSE — 9 通道 + Ops (全局看板数据, 单条)。
  //   hot 连接【池】rebalanceHotPool — focus book/quote 按盯盘数弹性分摊到多条独立 TCP (互不队头阻塞)。
  //   任一失败自动回退/重连; bulk 死回退 fast 轮询 (startFallbackPolling)。
  connectSSE();
  rebalanceHotPool();  // 弹性: 无 focus 时 0 连接; 用户展开盘 → maybePostFocus → 按需开池。

  // SSE 存活看门狗 (老板 2026-06-05 根因修复): 每 5s 查半死连接 → 强制重连自愈 (① bulk 全断 ② hot 池逐条半卡)。
  //   启一次 (initPolling 仅入口调一次)。
  if (!_sseWatchdogStarted) {
    _sseWatchdogStarted = true;
    window.setInterval(() => sseWatchdogTick(), 5000);
  }

  // hot 池未全连上时的 book/quote 兜底: 池里有连接半死 (不触发全局 fallback) 时, 用 REST 覆盖展开盘,
  //   直到看门狗 ② 重连。仅 focus 活 && 池未全连 && 非全局回退 (回退已自带 2s 兜) 时跑。
  every(() => {
    if (!hotPoolAllConnected() && !_fallbackActive && detailInterest.size > 0) void refreshExpandedDetail();
  }, 3000);

  // 全局 1s UI 时钟: 驱动各面板的"数据年龄/新鲜度"显示每秒重算 (量化AI 心跳等)。
  every(() => setUiNow(Date.now()), 1000);  // 1s: 与数据推送节奏一致 (老板「数据1s一推, 时钟没必要更快」)

  // 展开行全档 book/quote: 不再常驻 REST 轮询 (2026-06-02 老板「接口都做成推送了为什么还要拉」)。
  //   服务端 endpoint_stream.cpp:351 — 新进 focus 的盘口下一 tick(≤1s) 立即推一次 book 快照,
  //   之后 on-change 推。展开即由 SSE focus 推全档; 单个展开另有 addDetailInterest 的 priority REST
  //   兜首屏即时 (~200ms)。常驻 2s 轮询 (全展 38 盘 → 76 请求/2s 灌 cap-6 跨洋闸) 是纯冗余风暴,
  //   导致"拉取失败"闪烁 → 删除。REST 仅在 SSE 断时回退 (startFallbackPolling 内挂 refreshExpandedDetail)。
  // 其余常驻 REST: metrics (Prometheus 抓取需) + marketInfoSlow (market 元数据慢刷)。
  every(() => { void refreshMetrics(); }, 30000);
  every(() => { void refreshMarketInfoSlow(); }, 60000);
  // 成交流水(全局 500 深): SSE 9 通道【不含 fills】→ 必须常驻轮询, 否则 SSE 活时 AnalyticsPage 成交/
  //   模型诊断永远空 (2026-06-04 bug: refreshFills 只挂在 fallback, SSE 健康时从不拉)。已 gzip, 5s 一拉。
  every(() => { void refreshFills(); }, 5000);
  // (2026-06-05 老板「SSE 推送挺好的, 修复它就行, 为什么还要请求」): 撤回 1s REST 常驻快刷 ——
  //   实测它对展开 8 盘 = 24 req/s 风暴 + 大量 ERR_ABORTED, 还挤占浏览器连接池。book/quote 回归纯 SSE focus 推,
  //   REST 仅 SSE 断线 fallback。SSE focus 推的真实问题单独修 (不靠轮询盖)。
}
