/**
 * app.js — v4 单屏盯盘终端 主入口
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 布局: 顶部常驻条 + PnL sparkline + market 卡片网格 + 底部折叠区
 *
 * v4 变更:
 *  - 双边订单簿: fetchBook 按 condition_id (优先 market.condition_id, fallback market_id)
 *  - P0-02: demo fail-safe (data_source !== 'live')
 *  - P0-03: safeGet 区分失败类型, 写 fetchErrorMap; 顶部条增 apiErr slot
 *  - 顶部条 apiErr span 新增
 *
 * 轮询分层:
 *   status/positions:          5s
 *   book/score/quote per-mkt:  5s (仅可见卡)
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
  STUB_BOOK,
  STUB_MARKET,
  STUB_SCORE,
  STUB_QUOTE,
  STUB_METRICS_TEXT,
} from './stub.js';

import {
  renderTopBar,
  renderPnlSparkline,
  renderMarketGrid,
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
  // per-market: marketId → { market, book, score, quote }
  marketData: {},
};

// ---------- DOM 引用 ----------
const $ = (id) => document.getElementById(id);

// ---------- stub fallback (P0-03: 区分 null 来源) ----------

/**
 * safeGet:
 *  - USE_STUB: 直接返回 stubData
 *  - 正常: apiFn() 成功 → 返回数据 (可为 null = 404/found:false)
 *           apiFn() throw → 返回 null (fetchErrorMap 已在 apiFetch 内记录)
 */
async function safeGet(apiFn, stubData) {
  if (USE_STUB) return stubData;
  try {
    return await apiFn();
  } catch {
    // fetchErrorMap 已由 apiFetch 记录失败
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
  // P0-03: API 异常 indicator
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

// ---------- 数据组装: market 卡片网格 ----------

async function refreshMarketGrid() {
  // 1. positions
  const posData = await safeGet(fetchPositions, STUB_POSITIONS);
  cache.positions = posData;

  const positions = posData ? (posData.positions || []) : [];

  // 2. 按 market_id 分组
  const posMap = {};
  for (const p of positions) {
    if (!posMap[p.market_id]) posMap[p.market_id] = [];
    posMap[p.market_id].push(p);
  }

  // 3. attribution.per_market
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

  // 5. 市场集合
  const allMarketIds = new Set([
    ...Object.keys(posMap),
    ...Object.keys(rejectMap),
    ...Object.keys(pmPnlMap),
  ]);

  if (allMarketIds.size === 0) {
    const grid = $('market-grid');
    if (grid) grid.innerHTML = `<div class="no-data grid-placeholder">无市场数据 (positions 为空)</div>`;
    return;
  }

  // P0-02: fail-safe — data_source !== 'live' 即为 demo
  const isDemoData = !cache.status || cache.status.data_source !== 'live';

  // 6. 并发拉取
  await Promise.all(
    [...allMarketIds].map(async (mktId) => {
      let market = cache.marketData[mktId] ? cache.marketData[mktId].market : null;
      if (!market) {
        market = await safeGet(() => fetchMarket(mktId), STUB_MARKET);
      }

      // book: 优先用 condition_id (无则 fallback market_id)
      const condId = (market && market.condition_id) || mktId;
      const book = await safeGet(() => fetchBook(condId), STUB_BOOK);

      // score: 需要 event_id
      let score = null;
      if (market && market.event_id) {
        score = await safeGet(() => fetchScore(market.event_id), STUB_SCORE);
      }

      // quote
      const quote = await safeGet(() => fetchQuote(condId), STUB_QUOTE);

      cache.marketData[mktId] = { market, book, score, quote };
    })
  );

  // 7. 组装数组
  const marketDataArr = [...allMarketIds].sort().map((mktId) => ({
    marketId: mktId,
    posRows: posMap[mktId] || [],
    market: cache.marketData[mktId] ? cache.marketData[mktId].market : null,
    book: cache.marketData[mktId] ? cache.marketData[mktId].book : null,
    score: cache.marketData[mktId] ? cache.marketData[mktId].score : null,
    quote: cache.marketData[mktId] ? cache.marketData[mktId].quote : null,
    rejectRows: rejectMap[mktId] || [],
    perMarketPnl: pmPnlMap[mktId] != null ? pmPnlMap[mktId] : null,
    isDemoData,
  }));

  // 8. 渲染
  const grid = $('market-grid');
  if (grid) grid.innerHTML = renderMarketGrid(marketDataArr);
}

// ---------- 慢速刷新: market info (60s) ----------

async function refreshMarketInfoSlow() {
  const mktIds = Object.keys(cache.marketData);
  await Promise.all(
    mktIds.map(async (mktId) => {
      const market = await safeGet(() => fetchMarket(mktId), STUB_MARKET);
      if (cache.marketData[mktId]) {
        cache.marketData[mktId].market = market;
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
