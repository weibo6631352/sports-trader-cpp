/**
 * app.js — 观测/PnL 看板主入口
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 轮询策略 (对齐 ADR-038 §4):
 *   statusBar:    5s
 *   positions:    3s
 *   pnlTimeseries: 10s
 *   pnlAttribution: 10s
 *   riskRejects:  5s
 *   gatePaper:    30s
 *   book:         2s (仅当 bookPanel 展开时)
 *   metrics:      15s
 *
 * stub 模式: URL 带 ?stub=1 或后端不可达时自动降级显示 stub 数据
 */

import {
  fetchHealthz,
  fetchStatus,
  fetchPositions,
  fetchPnlTimeseries,
  fetchPnlAttribution,
  fetchRiskRejects,
  fetchGatePaper,
  fetchBook,
  fetchMarket,
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
  STUB_METRICS_TEXT,
} from './stub.js';

import {
  renderStatusBar,
  renderPositions,
  renderPnlCurve,
  renderPnlAttribution,
  renderRiskRejects,
  renderGatePaper,
  renderBook,
  renderMetrics,
} from './panels.js';

// ---------- 全局状态 ----------

const USE_STUB = new URLSearchParams(location.search).get('stub') === '1';
let activeBookId = null; // 当前展开的订单簿 condition_id

// ---------- DOM 引用 ----------

const $ = (id) => document.getElementById(id);

// ---------- 数据获取 with stub fallback ----------

async function safeGet(apiFn, stubData) {
  if (USE_STUB) return stubData;
  try {
    const data = await apiFn();
    return data; // null = 404 / has_data:false — 调用方显示"未接入"
  } catch {
    return null;
  }
}

// ---------- 刷新函数 ----------

async function refreshStatusBar() {
  const [healthz, status] = await Promise.all([
    safeGet(fetchHealthz, STUB_HEALTHZ),
    safeGet(fetchStatus, STUB_STATUS),
  ]);
  $('status-bar').innerHTML = renderStatusBar(healthz, status);
}

async function refreshPositions() {
  const data = await safeGet(fetchPositions, STUB_POSITIONS);
  $('positions-panel').innerHTML = renderPositions(data);
}

async function refreshPnlCurve() {
  const data = await safeGet(
    () => fetchPnlTimeseries('2h', '5m'),
    STUB_PNL_TIMESERIES
  );
  $('pnl-curve-panel').innerHTML = renderPnlCurve(data);
}

async function refreshPnlAttribution() {
  const data = await safeGet(fetchPnlAttribution, STUB_PNL_ATTRIBUTION);
  $('pnl-attr-panel').innerHTML = renderPnlAttribution(data);
}

async function refreshRiskRejects() {
  const data = await safeGet(fetchRiskRejects, STUB_RISK_REJECTS);
  $('risk-rejects-panel').innerHTML = renderRiskRejects(data);
}

async function refreshGatePaper() {
  const data = await safeGet(fetchGatePaper, STUB_GATE_PAPER);
  $('gate-paper-panel').innerHTML = renderGatePaper(data);
}

async function refreshBook() {
  if (!activeBookId) return;
  const data = await safeGet(() => fetchBook(activeBookId), STUB_BOOK);
  $('book-panel').innerHTML = renderBook(data);
}

async function refreshMetrics() {
  let text;
  if (USE_STUB) {
    text = STUB_METRICS_TEXT;
  } else {
    text = await fetchMetrics();
  }
  $('metrics-panel').innerHTML = renderMetrics(text);
}

// ---------- 初始化 & 轮询 ----------

function setInterval2(fn, ms) {
  fn(); // 立即执行一次
  return setInterval(fn, ms);
}

function initPolling() {
  setInterval2(refreshStatusBar, 5000);
  setInterval2(refreshPositions, 3000);
  setInterval2(refreshPnlCurve, 10000);
  setInterval2(refreshPnlAttribution, 10000);
  setInterval2(refreshRiskRejects, 5000);
  setInterval2(refreshGatePaper, 30000);
  setInterval2(refreshBook, 2000);
  setInterval2(refreshMetrics, 15000);
}

// ---------- 订单簿 market 选择 ----------

function initBookSearch() {
  const input = $('book-search-input');
  const btn = $('book-search-btn');
  if (!input || !btn) return;

  // 默认展示第一个 stub market
  if (USE_STUB) {
    activeBookId = STUB_BOOK.condition_id;
  }

  btn.addEventListener('click', () => {
    const val = input.value.trim();
    if (val) {
      activeBookId = val;
      refreshBook();
    }
  });
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') btn.click();
  });
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
  if (USE_STUB) {
    banner.classList.remove('hidden');
  }
}

// ---------- 标签页导航 ----------

function initTabs() {
  const tabs = document.querySelectorAll('.tab-btn');
  const sections = document.querySelectorAll('.tab-section');

  tabs.forEach((tab) => {
    tab.addEventListener('click', () => {
      tabs.forEach((t) => t.classList.remove('tab-active'));
      sections.forEach((s) => s.classList.add('hidden'));
      tab.classList.add('tab-active');
      const target = tab.dataset.tab;
      const section = document.getElementById(`tab-${target}`);
      if (section) section.classList.remove('hidden');
    });
  });
}

// ---------- 启动 ----------

document.addEventListener('DOMContentLoaded', () => {
  initStubBanner();
  initTabs();
  initBookSearch();
  initSettings();
  initPolling();
});
