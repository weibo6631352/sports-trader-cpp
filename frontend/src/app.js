/**
 * app.js — v5 赛事分组卡布局 主入口
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * v5 变更 (方向 A: 赛事分组卡, 老板选定 + 小尤设计):
 *  - 数据组装: positions/attribution/rejects → condition 集合
 *    → 每个 condition 拉 market(得 event_id) / book / score / quote
 *    → 按 event_id 分组: 同 event 的多盘口归一个赛事区块
 *    → score 按 event_id 去重拉取(不重复)
 *  - renderEventGroup 替换 renderMarketCard (每赛事一区块, 多盘口横向并列)
 *  - 比分条移到赛事头, 每 event 只渲染一次 (去乱 R1)
 *
 * 轮询分层:
 *   status/positions:          5s
 *   book/score/quote per-mkt:  5s
 *   rejects:                   5s
 *   timeseries/attribution:    15s
 *   metrics:                   30s (折叠时暂停)
 *   market info:               60s
 */

import {
  fetchHealthz,
  fetchStatus,
  fetchPositions,
  fetchPnlTimeseries,
  fetchPnlAttribution,
  fetchRiskRejects,
  fetchGatePaper,
  fetchMarket,
  fetchBook,
  fetchScore,
  fetchQuote,
  fetchMetrics,
  getBaseUrl,
  setBaseUrl,
} from './api.js';

import {
  STUB_HEALTHZ,
  STUB_STATUS,
  STUB_POSITIONS,
  STUB_PNL_TIMESERIES,
  STUB_PNL_ATTRIBUTION,
  STUB_RISK_REJECTS,
  STUB_GATE_PAPER,
  STUB_MARKET_MAP,
  STUB_BOOK_MAP,
  STUB_SCORE_MAP,
  STUB_QUOTE_MAP,
  STUB_METRICS_TEXT,
} from './stub.js';

import {
  renderTopBar,
  renderPnlSparkline,
  renderEventGrid,
  renderPnlAttribution,
  renderMetrics,
} from './panels.js';

// ---------- 全局状态 ----------

const USE_STUB = new URLSearchParams(location.search).get('stub') === '1';

const cache = {
  healthz: null,
  status: null,
  positions: null,
  attribution: null,
  rejects: null,
  gate: null,
  metrics: null,
  timeseries: null,
  // per-condition: conditionId → { market, book, score, quote }
  conditionData: {},
};

// ---------- DOM 引用 ----------
const $ = (id) => document.getElementById(id);

// ---------- stub fallback ----------

/**
 * safeGet:
 *  - USE_STUB: 直接返回 stubData
 *  - 正常: apiFn() 成功 → 返回数据; throw → 返回 null
 */
async function safeGet(apiFn, stubData) {
  if (USE_STUB) return stubData;
  try {
    return await apiFn();
  } catch {
    return null;
  }
}

/**
 * v5 stub 路由: stub 模式下按 key 取 map, fallback stub 单值
 * apiFn 仍用真实 fetch 路径; stubMap 为 { key: value } 字典
 */
async function safeGetMapped(apiFn, stubMap, key) {
  if (USE_STUB) {
    return (stubMap && stubMap[key]) || null;
  }
  try {
    return await apiFn();
  } catch {
    return null;
  }
}

// ---------- 顶部常驻条 ----------

async function refreshTopBar() {
  const [healthz, status] = await Promise.all([
    safeGet(fetchHealthz, STUB_HEALTHZ),
    safeGet(fetchStatus, STUB_STATUS),
  ]);
  cache.healthz = healthz;
  cache.status = status;
  applyTopBar();
}

function applyTopBar() {
  const result = renderTopBar(
    cache.healthz,
    cache.status,
    cache.attribution,
    cache.metrics,
    cache.gate,
  );

  // P0-02: fail-safe — isDemo = data_source !== 'live'
  const demoBanner = $('demo-banner');
  if (demoBanner) {
    if (result.isDemo) {
      demoBanner.classList.remove('hidden');
    } else {
      demoBanner.classList.add('hidden');
    }
  }

  const topModeBadge = $('top-mode-badge');
  if (topModeBadge) topModeBadge.innerHTML = result.modeBadge;
  const topState = $('top-state');
  if (topState) topState.innerHTML = result.state;
  const topUptime = $('top-uptime');
  if (topUptime) topUptime.textContent = result.uptime;
  const topWss = $('top-wss');
  if (topWss) topWss.innerHTML = result.wss;
  const topPnl = $('top-pnl');
  if (topPnl) topPnl.innerHTML = result.pnl;
  const topGate = $('top-gate');
  if (topGate) topGate.innerHTML = result.gate;
  const topP99 = $('top-p99');
  if (topP99) topP99.innerHTML = result.p99;
  const topStaleness = $('top-staleness');
  if (topStaleness) topStaleness.innerHTML = result.staleness;
  const topRejects = $('top-rejects');
  if (topRejects) topRejects.innerHTML = result.rejects;
  const topApiErr = $('top-api-err');
  if (topApiErr) topApiErr.innerHTML = result.apiErr || '';
}

// ---------- PnL sparkline ----------

async function refreshSparkline() {
  const data = await safeGet(() => fetchPnlTimeseries('1h', '1m'), STUB_PNL_TIMESERIES);
  cache.timeseries = data;
  const el = $('pnl-sparkline');
  if (el) el.innerHTML = renderPnlSparkline(data);
}

// ---------- 数据组装: 赛事分组卡 (v5 核心) ----------

async function refreshMarketGrid() {
  // 1. positions
  const posData = await safeGet(fetchPositions, STUB_POSITIONS);
  cache.positions = posData;
  const positions = posData ? (posData.positions || []) : [];

  // 2. 按 market_id 分组 positions
  const posMap = {};
  for (const p of positions) {
    if (!posMap[p.market_id]) posMap[p.market_id] = [];
    posMap[p.market_id].push(p);
  }

  // 3. attribution.per_market → per condition PnL
  const pmPnlMap = {};
  if (cache.attribution && cache.attribution.per_market) {
    for (const pm of cache.attribution.per_market) {
      pmPnlMap[pm.market_id] = Number(pm.net_pnl);
    }
  }

  // 4. rejects 按 market_id 分组
  const rejectMap = {};
  if (cache.rejects && cache.rejects.rejects) {
    for (const r of cache.rejects.rejects) {
      if (!rejectMap[r.market_id]) rejectMap[r.market_id] = [];
      rejectMap[r.market_id].push(r);
    }
  }

  // 5. 市场集合 (来自 positions + rejects + attribution)
  const allConditionIds = new Set([
    ...Object.keys(posMap),
    ...Object.keys(rejectMap),
    ...Object.keys(pmPnlMap),
  ]);

  if (allConditionIds.size === 0) {
    const grid = $('market-grid');
    if (grid) grid.innerHTML = `<div class="no-data grid-placeholder">无市场数据 (positions 为空)</div>`;
    return;
  }

  // P0-02: fail-safe — data_source !== 'live' 即为 demo
  const isDemoData = !cache.status || cache.status.data_source !== 'live';

  // 6. 并发拉取每个 condition 的 market / book / quote
  await Promise.all(
    [...allConditionIds].map(async (condId) => {
      // market (60s 缓存, 首次或无缓存时拉)
      let market = cache.conditionData[condId] ? cache.conditionData[condId].market : null;
      if (!market) {
        market = await safeGetMapped(() => fetchMarket(condId), STUB_MARKET_MAP, condId);
      }

      // book: 按 condition_id (含 fallback)
      const bookCondId = (market && market.condition_id) || condId;
      const book = await safeGetMapped(() => fetchBook(bookCondId), STUB_BOOK_MAP, bookCondId);

      // quote
      const quote = await safeGetMapped(() => fetchQuote(bookCondId), STUB_QUOTE_MAP, bookCondId);

      // score: 暂存 event_id, 统一在下面按 event_id 去重拉
      if (!cache.conditionData[condId]) cache.conditionData[condId] = {};
      cache.conditionData[condId].market = market;
      cache.conditionData[condId].book   = book;
      cache.conditionData[condId].quote  = quote;
    })
  );

  // 7. 按 event_id 收集需要拉取的 score (去重: 同一 event 只拉一次)
  const eventScoreCache = {}; // event_id → score
  const eventIdsNeeded = new Set();
  for (const condId of allConditionIds) {
    const mkt = cache.conditionData[condId] && cache.conditionData[condId].market;
    if (mkt && mkt.event_id) eventIdsNeeded.add(mkt.event_id);
  }
  await Promise.all(
    [...eventIdsNeeded].map(async (evId) => {
      const score = await safeGetMapped(() => fetchScore(evId), STUB_SCORE_MAP, evId);
      eventScoreCache[evId] = score;
    })
  );
  // 写回 score 到各 condition
  for (const condId of allConditionIds) {
    const mkt = cache.conditionData[condId] && cache.conditionData[condId].market;
    if (mkt && mkt.event_id) {
      cache.conditionData[condId].score = eventScoreCache[mkt.event_id] || null;
    } else {
      cache.conditionData[condId].score = null;
    }
  }

  // 8. 按 event_id 分组, 组装 eventGroups 数组
  //    eventId → { eventId, score, sport, conditions: [...] }
  const eventGroupMap = {};
  const NO_EVENT = '__no_event__';

  for (const condId of [...allConditionIds].sort()) {
    const d = cache.conditionData[condId] || {};
    const mkt = d.market;
    const evId = (mkt && mkt.event_id) || NO_EVENT;

    if (!eventGroupMap[evId]) {
      eventGroupMap[evId] = {
        eventId: evId === NO_EVENT ? null : evId,
        score:   d.score || null,
        sport:   mkt ? mkt.sport : null,
        conditions: [],
      };
    }
    // 更新 score (以第一个有效 score 为准)
    if (!eventGroupMap[evId].score && d.score) {
      eventGroupMap[evId].score = d.score;
    }

    eventGroupMap[evId].conditions.push({
      conditionId:  condId,
      posRows:      posMap[condId] || [],
      market:       mkt || null,
      book:         d.book  || null,
      quote:        d.quote || null,
      rejectRows:   rejectMap[condId] || [],
      perMarketPnl: pmPnlMap[condId] != null ? pmPnlMap[condId] : null,
      isDemoData,
    });
  }

  // 9. 排序: 有 positions 的赛事优先 → 按 eventId 字典序
  const eventGroups = Object.values(eventGroupMap).sort((a, b) => {
    const aHasPos = a.conditions.some((c) => c.posRows.length > 0);
    const bHasPos = b.conditions.some((c) => c.posRows.length > 0);
    if (aHasPos !== bHasPos) return aHasPos ? -1 : 1;
    return (a.eventId || '').localeCompare(b.eventId || '');
  });

  // 10. 渲染
  const grid = $('market-grid');
  if (grid) grid.innerHTML = renderEventGrid(eventGroups);
}

// ---------- 慢速刷新: market info (60s) ----------

async function refreshMarketInfoSlow() {
  const condIds = Object.keys(cache.conditionData);
  await Promise.all(
    condIds.map(async (condId) => {
      const market = await safeGetMapped(() => fetchMarket(condId), STUB_MARKET_MAP, condId);
      if (cache.conditionData[condId]) {
        cache.conditionData[condId].market = market;
      }
    })
  );
}

// ---------- 拒单 ----------

async function refreshRejects() {
  const data = await safeGet(fetchRiskRejects, STUB_RISK_REJECTS);
  cache.rejects = data;
}

// ---------- attribution + gate ----------

async function refreshAttribution() {
  const data = await safeGet(fetchPnlAttribution, STUB_PNL_ATTRIBUTION);
  cache.attribution = data;
  applyTopBar();
  const el = $('pnl-attr-panel');
  if (el) el.innerHTML = renderPnlAttribution(data);
}

async function refreshGate() {
  const data = await safeGet(fetchGatePaper, STUB_GATE_PAPER);
  cache.gate = data;
  applyTopBar();
}

// ---------- metrics ----------

async function refreshMetrics() {
  const details = $('secondary-details');
  if (details && !details.open) return;

  const text = USE_STUB ? STUB_METRICS_TEXT : await fetchMetrics();
  cache.metrics = text;
  applyTopBar();
  const el = $('metrics-panel');
  if (el) el.innerHTML = renderMetrics(text);
}

// ---------- 初始化 & 轮询 ----------

function every(fn, ms) {
  fn();
  return setInterval(fn, ms);
}

function initPolling() {
  every(refreshTopBar, 5000);
  every(refreshSparkline, 15000);
  every(refreshMarketGrid, 5000);
  every(refreshRejects, 5000);
  every(refreshAttribution, 15000);
  every(refreshGate, 15000);
  every(refreshMarketInfoSlow, 60000);
  every(refreshMetrics, 30000);
}

// ---------- 设置面板 ----------

function initSettings() {
  const btn = $('settings-btn');
  const panel = $('settings-panel');
  if (!btn || !panel) return;

  btn.addEventListener('click', () => {
    panel.classList.toggle('hidden');
  });

  const urlInput = $('api-base-input');
  const saveBtn = $('api-base-save');
  if (urlInput) urlInput.value = getBaseUrl();
  if (saveBtn) {
    saveBtn.addEventListener('click', () => {
      const v = urlInput?.value?.trim();
      if (v) setBaseUrl(v);
    });
  }
}

// ---------- stub 标识 ----------

function initStubBanner() {
  const banner = $('stub-banner');
  if (!banner) return;
  if (USE_STUB) banner.classList.remove('hidden');
}

// ---------- 启动 ----------

document.addEventListener('DOMContentLoaded', () => {
  initStubBanner();
  initSettings();
  initPolling();
});
