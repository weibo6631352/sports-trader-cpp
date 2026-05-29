/**
 * panels.js — v3 单屏盯盘终端 渲染逻辑
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 约定:
 *  - 每个 render* 函数接收 data (可为 null), 返回 HTML string
 *  - null/空数据统一显示 "—" 占位, 不崩溃
 *  - 金额字段全部 Number() 解析
 *  - 4 时间戳字段 epoch_ns number
 *  - DEMO 标记: demo 数字旁显示 [demo] 角标 (老钱红线 P0)
 */

import { fmtTs, fmtUsdc, fmtBps, fmtPct, fmtClock, stalenessMs, modeBadge } from './api.js';

// ---- 通用工具 ----

function noData(label) {
  return `<div class="no-data">${label ? label : '—'}</div>`;
}

function ph(v, fmt) {
  // placeholder: 值为 null/undefined 时返回 em-dash
  if (v == null) return '<span class="ph">—</span>';
  return fmt ? fmt(v) : String(v);
}

function demoBadge(isDemoData) {
  if (!isDemoData) return '';
  return '<span class="demo-chip" title="演示数据·非实盘">demo</span>';
}

function advisoryBadge() {
  return '<span class="advisory-chip" title="paper期模型旁路·不下单">advisory</span>';
}

// staleness chip: ms → green/yellow/red
function staleChip(ms) {
  if (ms == null) return '';
  const cls = ms < 2000 ? 'stale-ok' : ms < 10000 ? 'stale-warn' : 'stale-err';
  return `<span class="stale-chip ${cls}" title="${Math.round(ms)}ms ago">${ms < 1000 ? `${Math.round(ms)}ms` : `${(ms / 1000).toFixed(1)}s`}</span>`;
}

// ---- 顶部常驻条渲染 (返回多个片段, app.js 逐个 innerHTML) ----

export function renderTopBar(healthz, status, attribution, metrics, gate) {
  const s = status || {};
  const h = healthz || {};
  const g = gate || {};

  // DEMO 横幅检测
  const isDemo = s.data_source === 'demo';

  // mode badge
  const mode = s.mode || 'paper';
  const mb = modeBadge(mode);
  const modeBadgeHtml = `<span class="badge ${mb.cls}">${mb.text}</span>`;

  // state
  const stateClass = s.state === 'RUNNING' ? 'state-running' : s.state === 'HALTED' ? 'state-halted' : 'state-drain';
  const stateHtml = `<span class="state-label ${stateClass}">${s.state || '—'}</span>`;

  // uptime
  const uptimeSec = Number(h.uptime_sec || s.uptime_sec || 0);
  const uptimeHtml = `up ${uptimeSec.toLocaleString()}s`;

  // wss dots
  const wss = s.wss_connected || {};
  const wssHtml = Object.entries(wss)
    .map(([k, v]) => `<span class="wss-dot ${v ? 'wss-ok' : 'wss-off'}" title="${k}"></span>`)
    .join('');

  // net PnL from attribution.waterfall.net or metrics parse
  let netPnl = null;
  if (attribution && attribution.waterfall) {
    netPnl = Number(attribution.waterfall.net);
  }
  const pnlClass = netPnl != null && netPnl >= 0 ? 'pnl-pos' : 'pnl-neg';
  const pnlHtml = netPnl != null ? `<span class="${pnlClass} mono-strong">${fmtUsdc(netPnl)}</span>` : '<span class="ph">—</span>';

  // paper-gate
  let gateHtml = '<span class="ph">—</span>';
  if (g.has_data) {
    const prelim = g.prelim_pass;
    const confirm = g.confirm_pass;
    gateHtml = `<span class="gate-chip ${prelim ? 'gate-ok' : 'gate-fail'}">Prelim${prelim ? '✓' : '✗'}</span> <span class="gate-chip ${confirm ? 'gate-ok' : 'gate-fail'}">Confirm${confirm ? '✓' : '✗'}</span>`;
  }

  // p99 from metrics text parse (stcpp_loop_latency_p99_us)
  let p99Html = '<span class="ph">—</span>';
  if (metrics) {
    const m = metrics.match(/stcpp_loop_latency_p99_us[^\n]*\s+([\d.]+)/);
    if (m) p99Html = `<span class="mono-dim">${Number(m[1]).toFixed(0)} us</span>`;
  }

  // staleness from metrics text (stcpp_data_staleness_ms_max)
  let stalenessHtml = '<span class="ph">—</span>';
  if (metrics) {
    const m = metrics.match(/stcpp_data_staleness_ms_max[^\n]*\s+([\d.]+)/);
    if (m) {
      const ms = Number(m[1]);
      const cls = ms < 100 ? 'stale-ok' : ms < 1000 ? 'stale-warn' : 'stale-err';
      stalenessHtml = `<span class="${cls} mono-dim">${ms.toFixed(0)} ms</span>`;
    }
  }

  // rm rejects last 60s
  const rmRejects = s.rm_rejects_last_60s;
  const rejectsHtml = rmRejects != null
    ? `<span class="${rmRejects > 0 ? 'pnl-neg mono-dim' : 'mono-dim'}">${rmRejects}</span>`
    : '<span class="ph">—</span>';

  return {
    isDemo,
    modeBadge: modeBadgeHtml,
    state: stateHtml,
    uptime: uptimeHtml,
    wss: wssHtml,
    pnl: pnlHtml,
    gate: gateHtml,
    p99: p99Html,
    staleness: stalenessHtml,
    rejects: rejectsHtml,
  };
}

// ---- PnL 净值曲线 sparkline (窄条) ----

export function renderPnlSparkline(data) {
  if (!data) return noData('PnL 曲线');
  const buckets = data.buckets || [];
  if (buckets.length < 2) return `<div class="no-data">数据不足</div>`;

  const vals = buckets.map((b) => Number(b.cum_net_pnl));
  const min = Math.min(...vals);
  const max = Math.max(...vals);
  const range = max - min || 1;

  const W = 900;
  const H = 48;
  const padX = 4;
  const padY = 4;

  const points = vals.map((v, i) => {
    const x = padX + ((W - padX * 2) * i) / (vals.length - 1);
    const y = padY + (H - padY * 2) * (1 - (v - min) / range);
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  });

  const firstX = padX;
  const lastX = (padX + (W - padX * 2)).toFixed(1);
  const zeroY = padY + (H - padY * 2) * (1 - (0 - min) / range);
  const clampedZeroY = Math.min(H - padY, Math.max(padY, zeroY));

  const areaPath = `M ${firstX},${clampedZeroY.toFixed(1)} L ${points.join(' L ')} L ${lastX},${clampedZeroY.toFixed(1)} Z`;
  const linePath = `M ${points.join(' L ')}`;

  const latestPnl = vals[vals.length - 1];
  const color = latestPnl >= 0 ? '#22c55e' : '#ef4444';

  return `
    <div class="spark-header">
      <span class="spark-label">NET PnL CURVE</span>
      <span class="${latestPnl >= 0 ? 'pnl-pos' : 'pnl-neg'} mono-strong">${fmtUsdc(latestPnl)}</span>
      <span class="spark-ts">as_of ${fmtTs(data.as_of_ts)}</span>
    </div>
    <svg viewBox="0 0 ${W} ${H}" width="100%" height="${H}" class="spark-svg">
      <defs>
        <linearGradient id="sparkGrad" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stop-color="${color}" stop-opacity="0.3"/>
          <stop offset="100%" stop-color="${color}" stop-opacity="0.02"/>
        </linearGradient>
      </defs>
      <line x1="${padX}" y1="${clampedZeroY.toFixed(1)}" x2="${W - padX}" y2="${clampedZeroY.toFixed(1)}"
            stroke="#374151" stroke-width="0.5" stroke-dasharray="3,3"/>
      <path d="${areaPath}" fill="url(#sparkGrad)"/>
      <path d="${linePath}" fill="none" stroke="${color}" stroke-width="1.5"/>
    </svg>`;
}

// ---- Market 卡片 (核心, 6块直显) ----

/**
 * renderMarketCard — 单个 market 卡片渲染
 * @param {string}  marketId
 * @param {Array}   posRows     — positions 中属于此 marketId 的行 (可多 outcome)
 * @param {object}  market      — /api/v1/market/{id} 响应
 * @param {object}  book        — /api/v1/book/{id}   响应
 * @param {object}  score       — /api/v1/score/{event_id} 响应 (可 null)
 * @param {object}  quote       — /api/v1/quote/{condition_id} 响应 (可 null)
 * @param {Array}   rejectRows  — rejects 中属于此 marketId 的行
 * @param {number}  perMarketPnl — attribution.per_market 中此 market 的 net_pnl
 * @param {boolean} isDemoData  — data_source === 'demo'
 */
export function renderMarketCard({
  marketId,
  posRows,
  market,
  book,
  score,
  quote,
  rejectRows,
  perMarketPnl,
  isDemoData,
}) {
  // ① 比分/赛况条
  const scoreBlock = renderScoreBlock(score, marketId);

  // ② 盘口 + 量化参数
  const quoteBlock = renderQuoteBlock(quote, market, isDemoData);

  // ③ Top-of-book
  const bookBlock = renderBookBlock(book);

  // ④ 持仓 + 该市场净PnL
  const posBlock = renderPosBlock(posRows, perMarketPnl);

  // ⑤ 拒单角标
  const rejectBadge = renderRejectBadge(rejectRows);

  // ⑥ 数据源/stale 标记
  const staleBlock = renderStaleBlock(market, book, score, isDemoData);

  // market 状态指示
  const mActive = market ? (market.accepting_orders ? '' : ' card-inactive') : '';

  return `
    <div class="mkt-card${mActive}" data-market="${marketId}">
      <div class="mkt-header">
        <span class="mkt-id">${marketId}</span>
        ${rejectBadge}
        ${staleBlock}
      </div>

      ${scoreBlock}

      <div class="mkt-body">
        ${quoteBlock}
        ${bookBlock}
        ${posBlock}
      </div>
    </div>`;
}

// ---- ① 比分/赛况条 ----

function renderScoreBlock(score, marketId) {
  if (!score) {
    return `<div class="score-bar score-bar-empty"><span class="ph">比分未接入</span></div>`;
  }

  const statusMap = {
    inplay:   { label: '●', cls: 'score-live' },
    halftime: { label: 'HT', cls: 'score-ht' },
    final:    { label: 'FT', cls: 'score-ft' },
    pregame:  { label: 'PRE', cls: 'score-pre' },
  };
  const st = statusMap[score.status] || { label: score.status || '?', cls: 'score-pre' };

  const homeStr = score.home || '—';
  const awayStr = score.away || '—';
  const homeScore = score.home_score != null ? score.home_score : '—';
  const awayScore = score.away_score != null ? score.away_score : '—';
  const period = score.period || '—';
  const clock = score.clock_sec != null ? fmtClock(score.clock_sec) : '—';

  return `
    <div class="score-bar">
      <span class="score-team">${homeStr}</span>
      <span class="score-num">${homeScore}</span>
      <span class="score-dash">–</span>
      <span class="score-num">${awayScore}</span>
      <span class="score-team">${awayStr}</span>
      <span class="score-period">${period}</span>
      <span class="score-clock">${clock}</span>
      <span class="score-status ${st.cls}">${st.label}</span>
      <span class="score-sport">${score.sport || ''}</span>
    </div>`;
}

// ---- ② 盘口 + 量化参数 ----

function renderQuoteBlock(quote, market, isDemoData) {
  const demo = demoBadge(isDemoData);
  const advisory = advisoryBadge();

  const outcome = market ? (market.outcome || market.market_id || '—') : '—';

  if (!quote) {
    return `
      <div class="quote-block">
        <div class="block-label">盘口 / 量化 ${demo}</div>
        <div class="quote-row">
          <span class="outcome-label">${outcome}</span>
          <span class="ph">量化未接入</span>
          ${advisory}
        </div>
      </div>`;
  }

  const fairValue = Number(quote.fair_value);
  const marketMid = Number(quote.market_mid);
  const edgeBps = Number(quote.edge_bps);
  const kelly = Number(quote.kelly_fraction);
  const notional = Number(quote.suggested_notional);

  // edge 方向: fair > mid → 正边 (绿), fair < mid → 负边 (红)
  const edgePositive = fairValue >= marketMid;
  const edgeBarPct = Math.min(Math.abs(edgeBps) / 100, 1) * 100;
  const edgeCls = edgePositive ? 'edge-pos' : 'edge-neg';

  return `
    <div class="quote-block">
      <div class="block-label">盘口 / 量化 ${demo} ${advisory}</div>
      <div class="quote-row">
        <span class="outcome-label">${outcome}</span>
        <span class="fair-label">fair</span>
        <span class="fair-val mono-strong">${Number.isFinite(fairValue) ? fairValue.toFixed(3) : '—'}</span>
        <span class="mid-label">mid</span>
        <span class="mid-val mono-dim">${Number.isFinite(marketMid) ? marketMid.toFixed(3) : '—'}</span>
      </div>
      <div class="edge-bar-row">
        <span class="edge-label">edge</span>
        <div class="edge-track">
          <div class="edge-fill ${edgeCls}" style="width:${edgeBarPct.toFixed(1)}%"></div>
        </div>
        <span class="edge-val ${edgeCls}">${fmtBps(edgeBps)}</span>
      </div>
      <div class="kelly-row">
        <span class="kelly-label">Kelly</span>
        <span class="kelly-val mono-strong">${Number.isFinite(kelly) ? (kelly * 100).toFixed(2) : '—'}%</span>
        <span class="notional-label">建议额</span>
        <span class="notional-val mono-dim">$${Number.isFinite(notional) ? notional.toLocaleString() : '—'}</span>
      </div>
    </div>`;
}

// ---- ③ Top-of-book ----

function renderBookBlock(book) {
  if (!book) {
    return `
      <div class="book-block">
        <div class="block-label">订单簿</div>
        <div class="ph">未接入</div>
      </div>`;
  }

  const bid = Number(book.best_bid);
  const ask = Number(book.best_ask);
  const spread = Number(book.spread);
  const microprice = Number(book.microprice);
  const imbalance = Number(book.imbalance);

  const wssOk = book.wss_state === 'CONNECTED';
  const wssCls = wssOk ? 'wss-ok-text' : 'wss-off-text';

  // imbalance bar (-1..1 → 0..100%)
  const imbPct = ((imbalance + 1) / 2 * 100).toFixed(1);

  return `
    <div class="book-block">
      <div class="block-label">订单簿 <span class="${wssCls}" title="wss_state">${book.wss_state || '?'}</span></div>
      <div class="book-top-row">
        <span class="bid-price">${Number.isFinite(bid) ? bid.toFixed(4) : '—'}</span>
        <span class="book-sep">bid</span>
        <span class="book-spread">sprd ${Number.isFinite(spread) ? spread.toFixed(4) : '—'}</span>
        <span class="book-sep">ask</span>
        <span class="ask-price">${Number.isFinite(ask) ? ask.toFixed(4) : '—'}</span>
      </div>
      <div class="book-meta-row">
        <span class="book-meta-label">μprice</span>
        <span class="book-meta-val mono-dim">${Number.isFinite(microprice) ? microprice.toFixed(4) : '—'}</span>
        <span class="book-meta-label">imb</span>
        <div class="imb-track" title="imbalance ${imbalance.toFixed(3)}">
          <div class="imb-fill" style="width:${imbPct}%"></div>
        </div>
        <span class="book-meta-val mono-dim">${Number.isFinite(imbalance) ? imbalance.toFixed(3) : '—'}</span>
      </div>
    </div>`;
}

// ---- ④ 持仓 + 净PnL ----

function renderPosBlock(posRows, perMarketPnl) {
  if (!posRows || posRows.length === 0) {
    const pmPnlStr = perMarketPnl != null
      ? `<span class="${perMarketPnl >= 0 ? 'pnl-pos' : 'pnl-neg'} mono-strong">${fmtUsdc(perMarketPnl)}</span>`
      : '<span class="ph">—</span>';
    return `
      <div class="pos-block">
        <div class="block-label">持仓 / 净PnL ${pmPnlStr}</div>
        <div class="ph">无持仓</div>
      </div>`;
  }

  const pmPnl = perMarketPnl != null ? perMarketPnl : null;
  const pmPnlStr = pmPnl != null
    ? `<span class="${pmPnl >= 0 ? 'pnl-pos' : 'pnl-neg'} mono-strong">${fmtUsdc(pmPnl)}</span>`
    : '<span class="ph">—</span>';

  const rows = posRows.map((p) => {
    const netQty = Number(p.net_qty);
    const avg = Number(p.avg_entry_price);
    const mark = Number(p.mark_price);
    const pnlR = Number(p.pnl_realized);
    const pnlU = Number(p.pnl_unrealized);
    const pnlTotal = pnlR + pnlU;
    const pnlCls = pnlTotal >= 0 ? 'pnl-pos' : 'pnl-neg';
    const qtySign = netQty >= 0 ? '+' : '';

    return `
      <div class="pos-row">
        <span class="pos-outcome outcome-${(p.outcome || '').toLowerCase()}">${p.outcome || '—'}</span>
        <span class="pos-qty mono-dim">${qtySign}${netQty.toLocaleString()}u</span>
        <span class="pos-avg mono-dim">@${Number.isFinite(avg) ? avg.toFixed(4) : '—'}</span>
        <span class="pos-mark mono-dim">mk ${Number.isFinite(mark) ? mark.toFixed(4) : '—'}</span>
        <span class="pos-pnl ${pnlCls} mono-strong">${fmtUsdc(pnlTotal)}</span>
      </div>`;
  }).join('');

  return `
    <div class="pos-block">
      <div class="block-label">持仓 / 净PnL ${pmPnlStr}</div>
      ${rows}
    </div>`;
}

// ---- ⑤ 拒单角标 ----

function renderRejectBadge(rejectRows) {
  if (!rejectRows || rejectRows.length === 0) return '';
  const latest = rejectRows[0];
  return `<span class="reject-badge" title="${rejectRows.map((r) => r.reason_code).join(', ')}">拒单×${rejectRows.length} <span class="reject-reason">${latest.reason_code}</span></span>`;
}

// ---- ⑥ 数据源/stale 标记 ----

function renderStaleBlock(market, book, score, isDemoData) {
  const parts = [];

  if (isDemoData) {
    parts.push('<span class="demo-chip-sm">DEMO</span>');
  }

  // book staleness
  if (book) {
    const ms = stalenessMs(book.book_as_of_ts);
    parts.push(staleChip(ms));
  }

  // score staleness
  if (score) {
    const ms = stalenessMs(score.score_as_of_ts);
    parts.push(staleChip(ms));
  }

  if (parts.length === 0) return '';
  return `<span class="stale-group">${parts.join('')}</span>`;
}

// ---- Market Grid (组装所有卡片) ----

export function renderMarketGrid(marketData) {
  // marketData: [{marketId, posRows, market, book, score, quote, rejectRows, perMarketPnl}]
  if (!marketData || marketData.length === 0) {
    return `<div class="no-data grid-placeholder">无市场数据</div>`;
  }
  return marketData.map((d) => renderMarketCard(d)).join('');
}

// ---- PnL 归因瀑布图 (折叠区) ----

export function renderPnlAttribution(data) {
  if (!data) return noData('PnL 归因');
  const wfObj = data.waterfall || {};
  if (Object.keys(wfObj).length === 0) return `<div class="no-data">暂无瀑布数据</div>`;

  const WF_ORDER = ['gross', 'fee', 'gas', 'slippage', 'spread', 'net'];
  const gross = Number(wfObj.gross || 0);

  const bars = WF_ORDER.map((key) => {
    const v = Number(wfObj[key] ?? 0);
    const pct = gross !== 0 ? Math.abs(v / gross) * 100 : 0;
    const isNet = key === 'net';
    const positive = v >= 0;
    return `
      <div class="wf-row">
        <div class="wf-label">${key}</div>
        <div class="wf-bar-wrap">
          <div class="wf-bar ${isNet ? 'wf-net' : positive ? 'wf-pos' : 'wf-neg'}"
               style="width:${Math.min(pct, 100).toFixed(1)}%"></div>
        </div>
        <div class="wf-val ${positive ? 'pnl-pos' : 'pnl-neg'}">${fmtUsdc(v)}</div>
      </div>`;
  });

  const perMarket = (data.per_market || [])
    .map((m) => `
      <div class="pm-row">
        <span class="mono pm-id">${m.market_id}</span>
        <span class="${Number(m.net_pnl) >= 0 ? 'pnl-pos' : 'pnl-neg'}">${fmtUsdc(m.net_pnl)}</span>
      </div>`)
    .join('');

  return `
    <div class="panel-header">
      <span class="panel-ts">as_of ${fmtTs(data.as_of_ts)}</span>
    </div>
    <div class="attr-layout">
      <div class="wf-section">
        <div class="section-title">瀑布图</div>
        ${bars.join('')}
      </div>
      <div class="pm-section">
        <div class="section-title">分市场</div>
        ${perMarket || '<div class="no-data">—</div>'}
      </div>
    </div>`;
}

// ---- Prometheus metrics 原始文本 (折叠区) ----

export function renderMetrics(text) {
  if (!text) return noData('Prometheus /metrics');
  const escaped = text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  return `<pre class="metrics-pre">${escaped}</pre>`;
}
