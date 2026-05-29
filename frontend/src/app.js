/**
 * app.js — v3 单屏盯盘终端 主入口
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 布局: 顶部常驻条 + PnL sparkline + market 卡片网格 + 底部折叠区
 * 删除: 5 tab 导航
 *
 * 轮询分层 (决议 §4):
 *   status/positions:          5s
 *   book/score/quote per-mkt:  5s (仅可见卡)
 *   rejects:                   5s
 *   timeseries/attribution:    15s
 *   metrics:                   30s (折叠时暂停)
 *   market info:               60s
 *
 * 数据组装 (决议 §3):
 *   positions → market 集合 → 并发拉 market/book/score/quote
 *   rejects/attribution.per_market 按 market_id 分组挂上
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

// 缓存: 各 market 的上次数据 (用于增量更新)
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

// ---------- stub fallback ----------

async function safeGet(apiFn, stubData) {
  if (USE_STUB) return stubData;
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

  // DEMO 横幅 (老钱红线 P0)
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
}

// ---------- PnL sparkline ----------

async function refreshSparkline() {
  const data = await safeGet(() => fetchPnlTimeseries('1h', '1m'), STUB_PNL_TIMESERIES);
  cache.timeseries = data;
  const el = $('pnl-sparkline');
  if (el) el.innerHTML = renderPnlSparkline(data);
}

// ---------- 数据组装: market 卡片网格 ----------

/**
 * 从 positions 推断 market 集合, 并发拉取每个 market 的
 * market-info / book / score / quote, 然后组装卡片
 */
async function refreshMarketGrid() {
  // 1. 拉 positions (基础)
  const posData = await safeGet(fetchPositions, STUB_POSITIONS);
  cache.positions = posData;

  const positions = posData ? (posData.positions || []) : [];

  // 2. 按 market_id 分组 pos rows
  const posMap = {};
  for (const p of positions) {
    if (!posMap[p.market_id]) posMap[p.market_id] = [];
    posMap[p.market_id].push(p);
  }

  // 3. attribution.per_market 分组 (已缓存)
  const pmPnlMap = {};
  if (cache.attribution && cache.attribution.per_market) {
    for (const pm of cache.attribution.per_market) {
      pmPnlMap[pm.market_id] = Number(pm.net_pnl);
    }
  }

  // 4. rejects 按 market_id 分组 (已缓存)
  const rejectMap = {};
  if (cache.rejects && cache.rejects.rejects) {
    for (const r of cache.rejects.rejects) {
      if (!rejectMap[r.market_id]) rejectMap[r.market_id] = [];
      rejectMap[r.market_id].push(r);
    }
  }

  // 5. market 集合 = positions 中出现的 + rejects 中出现的 + attribution 中出现的
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

  const isDemoData = cache.status && cache.status.data_source === 'demo';

  // 6. 并发拉取每个 market 的 market/book/score/quote
  await Promise.all(
    [...allMarketIds].map(async (mktId) => {
      // market info (60s 缓存: 如果已有则复用, market info 本轮不重拉 — 由 refreshMarketInfoSlow 处理)
      let market = cache.marketData[mktId] ? cache.marketData[mktId].market : null;
      if (!market) {
        market = await safeGet(() => fetchMarket(mktId), STUB_MARKET);
      }

      // book (5s)
      const book = await safeGet(() => fetchBook(mktId), STUB_BOOK);

      // score: 需要 event_id (来自 market.event_id)
      let score = null;
      if (market && market.event_id) {
        score = await safeGet(() => fetchScore(market.event_id), STUB_SCORE);
      }

      // quote (5s)
      const quote = await safeGet(() => fetchQuote(mktId), STUB_QUOTE);

      cache.marketData[mktId] = { market, book, score, quote };
    })
  );

  // 7. 组装 marketData 数组 (按 market_id 排序)
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
  // rejects 更新后顶部条的拒单计数依赖 status (rm_rejects_last_60s) 不依赖此, 无需 applyTopBar
}

// ---------- attribution + gate (影响顶部条) ----------

async function refreshAttribution() {
  const data = await safeGet(fetchPnlAttribution, STUB_PNL_ATTRIBUTION);
  cache.attribution = data;
  applyTopBar(); // 净PnL 来自 attribution

  const el = $('pnl-attr-panel');
  if (el) el.innerHTML = renderPnlAttribution(data);
}

async function refreshGate() {
  const data = await safeGet(fetchGatePaper, STUB_GATE_PAPER);
  cache.gate = data;
  applyTopBar(); // PAPER-GATE 来自 gate
}

// ---------- metrics (折叠区, 折叠时暂停) ----------

async function refreshMetrics() {
  const details = $('secondary-details');
  if (details && !details.open) return; // 折叠时暂停

  const text = USE_STUB ? STUB_METRICS_TEXT : await fetchMetrics();
  cache.metrics = text;
  applyTopBar(); // p99/staleness 来自 metrics text

  const el = $('metrics-panel');
  if (el) el.innerHTML = renderMetrics(text);
}

// ---------- 初始化 & 轮询 ----------

function every(fn, ms) {
  fn();
  return setInterval(fn, ms);
}

function initPolling() {
  // 顶部条 (status + healthz): 5s
  every(refreshTopBar, 5000);

  // sparkline: 15s
  every(refreshSparkline, 15000);

  // market 卡片网格 (positions + book + score + quote per-mkt): 5s
  every(refreshMarketGrid, 5000);

  // rejects: 5s
  every(refreshRejects, 5000);

  // attribution + gate: 15s
  every(refreshAttribution, 15000);
  every(refreshGate, 15000);

  // market info (slow): 60s
  every(refreshMarketInfoSlow, 60000);

  // metrics: 30s (折叠时暂停, 内部检测)
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
