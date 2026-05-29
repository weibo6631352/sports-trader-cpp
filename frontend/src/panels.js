/**
 * panels.js — 各看板面板渲染逻辑
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 约定:
 *  - 每个 render* 函数接收 data (可为 null), 返回 HTML string
 *  - null/空数据统一显示 "未接入" 占位
 *  - 金额字段全部 Number() 解析, 不按 string
 *  - 4 时间戳字段 epoch_ns number, 用 fmtTs() 转人类可读
 */

import { fmtTs, fmtUsdc, fmtBps, fmtPct, modeBadge } from './api.js';

// ---- 通用工具 ----

function noData(label = '') {
  return `<div class="no-data">未接入${label ? ` — ${label}` : ''}</div>`;
}

function badge(mode) {
  const b = modeBadge(mode);
  return `<span class="badge ${b.cls}">${b.text}</span>`;
}

function statCard(label, value, sub = '') {
  return `
    <div class="stat-card">
      <div class="stat-label">${label}</div>
      <div class="stat-value">${value}</div>
      ${sub ? `<div class="stat-sub">${sub}</div>` : ''}
    </div>`;
}

// ---- 系统状态栏 ----

export function renderStatusBar(healthz, status) {
  if (!healthz && !status) return noData('系统状态');

  const s = status || {};
  const h = healthz || {};

  const stateClass =
    s.state === 'RUNNING' ? 'state-running' : s.state === 'HALTED' ? 'state-halted' : 'state-drain';

  const wss = s.wss_connected || {};
  const wssItems = Object.entries(wss)
    .map(([k, v]) => `<span class="wss-dot ${v ? 'wss-ok' : 'wss-off'}" title="${k}"></span>`)
    .join('');

  const threads = h.threads || {};
  const threadItems = Object.entries(threads)
    .map(
      ([k, v]) =>
        `<span class="thread-chip ${v === 'alive' ? 'chip-ok' : 'chip-err'}">${k.replace('_', ' ')}</span>`
    )
    .join('');

  return `
    <div class="status-bar">
      <div class="status-left">
        ${s.mode ? badge(s.mode) : ''}
        <span class="state-label ${stateClass}">${s.state || '—'}</span>
        <span class="uptime">up ${Number(h.uptime_sec || s.uptime_sec || 0).toLocaleString()}s</span>
      </div>
      <div class="status-mid">
        <span class="label-small">WSS</span> ${wssItems}
      </div>
      <div class="status-mid threads-row">
        ${threadItems}
      </div>
      <div class="status-right">
        <span class="label-small">as_of</span>
        <span class="ts-text">${fmtTs(h.as_of_ts || s.as_of_ts)}</span>
      </div>
    </div>`;
}

// ---- 持仓面板 ----

export function renderPositions(data) {
  if (!data) return noData('持仓');
  const positions = data.positions || [];
  if (positions.length === 0) return `<div class="no-data">暂无持仓</div>`;

  const rows = positions
    .map((p) => {
      const pnlTotal = Number(p.pnl_realized) + Number(p.pnl_unrealized);
      const pnlClass = pnlTotal >= 0 ? 'pnl-pos' : 'pnl-neg';
      const markVsAvg = Number(p.mark_price) - Number(p.avg_fill_price);
      return `
      <tr>
        <td class="mono">${p.market_id}</td>
        <td><span class="outcome-badge outcome-${(p.outcome || '').toLowerCase()}">${p.outcome}</span></td>
        <td class="num">${Number(p.size).toLocaleString()}</td>
        <td class="num">${Number(p.avg_fill_price).toFixed(4)}</td>
        <td class="num">${Number(p.mark_price).toFixed(4)}</td>
        <td class="num ${markVsAvg >= 0 ? 'pnl-pos' : 'pnl-neg'}">${markVsAvg >= 0 ? '+' : ''}${markVsAvg.toFixed(4)}</td>
        <td class="num">${fmtUsdc(p.pnl_realized)}</td>
        <td class="num">${fmtUsdc(p.pnl_unrealized)}</td>
        <td class="num ${pnlClass}">${fmtUsdc(pnlTotal)}</td>
        <td class="ts">${fmtTs(p.ingestion_ts)}</td>
      </tr>`;
    })
    .join('');

  return `
    <div class="panel-header">
      ${badge(data.mode)}
      <span class="panel-ts">as_of ${fmtTs(data.as_of_ts)}</span>
    </div>
    <div class="table-wrap">
      <table class="data-table">
        <thead>
          <tr>
            <th>Market ID</th><th>方向</th><th>数量</th>
            <th>均价</th><th>Mark</th><th>差价</th>
            <th>已实现</th><th>未实现</th><th>合计 PnL</th>
            <th>ingestion_ts</th>
          </tr>
        </thead>
        <tbody>${rows}</tbody>
      </table>
    </div>`;
}

// ---- PnL 净值曲线 (SVG sparkline) ----

export function renderPnlCurve(data) {
  if (!data) return noData('PnL 曲线');
  const buckets = data.buckets || [];
  if (buckets.length < 2) return `<div class="no-data">数据不足</div>`;

  const vals = buckets.map((b) => Number(b.cum_net_pnl));
  const min = Math.min(...vals);
  const max = Math.max(...vals);
  const range = max - min || 1;

  const W = 600;
  const H = 120;
  const padX = 8;
  const padY = 8;

  const points = vals.map((v, i) => {
    const x = padX + ((W - padX * 2) * i) / (vals.length - 1);
    const y = padY + (H - padY * 2) * (1 - (v - min) / range);
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  });

  // Fill area under curve
  const firstX = padX;
  const lastX = (padX + ((W - padX * 2) * (vals.length - 1)) / (vals.length - 1)).toFixed(1);
  const zeroY = (padY + (H - padY * 2) * (1 - (0 - min) / range)).toFixed(1);
  const clampedZeroY = Math.min(H - padY, Math.max(padY, Number(zeroY)));

  const areaPath = `M ${firstX},${clampedZeroY} L ${points.join(' L ')} L ${lastX},${clampedZeroY} Z`;
  const linePath = `M ${points.join(' L ')}`;

  const latestPnl = vals[vals.length - 1];
  const pnlClass = latestPnl >= 0 ? 'pnl-pos' : 'pnl-neg';

  // x-axis label every ~6 buckets
  const xLabels = buckets
    .filter((_, i) => i % 6 === 0 || i === buckets.length - 1)
    .map((b, idx, arr) => {
      const origIdx = idx * 6 < buckets.length ? idx * 6 : buckets.length - 1;
      const x = padX + ((W - padX * 2) * origIdx) / (vals.length - 1);
      return `<text x="${x.toFixed(1)}" y="${H}" font-size="9" fill="#6b7280" text-anchor="middle">${fmtTs(b.bucket_start_ts).slice(0, 8)}</text>`;
    })
    .join('');

  const tradeBarHtml = buckets
    .map((b, i) => {
      const x = padX + ((W - padX * 2) * i) / (vals.length - 1);
      const trades = Number(b.n_trades);
      if (!trades) return '';
      return `<rect x="${(x - 1.5).toFixed(1)}" y="${(H - padY - trades * 2).toFixed(1)}" width="3" height="${(trades * 2).toFixed(1)}" fill="#6366f1" opacity="0.4"/>`;
    })
    .join('');

  return `
    <div class="panel-header">
      ${badge(data.mode)}
      <span class="curve-summary ${pnlClass}">净 PnL ${fmtUsdc(latestPnl)}</span>
      <span class="panel-ts">as_of ${fmtTs(data.as_of_ts)}</span>
    </div>
    <svg viewBox="0 0 ${W} ${H + 14}" width="100%" class="pnl-chart">
      <defs>
        <linearGradient id="areaGrad" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stop-color="${latestPnl >= 0 ? '#22c55e' : '#ef4444'}" stop-opacity="0.25"/>
          <stop offset="100%" stop-color="${latestPnl >= 0 ? '#22c55e' : '#ef4444'}" stop-opacity="0.02"/>
        </linearGradient>
      </defs>
      <!-- zero line -->
      <line x1="${padX}" y1="${clampedZeroY}" x2="${W - padX}" y2="${clampedZeroY}"
            stroke="#374151" stroke-width="0.5" stroke-dasharray="3,3"/>
      <!-- trade bars -->
      ${tradeBarHtml}
      <!-- area fill -->
      <path d="${areaPath}" fill="url(#areaGrad)"/>
      <!-- line -->
      <path d="${linePath}" fill="none" stroke="${latestPnl >= 0 ? '#22c55e' : '#ef4444'}" stroke-width="1.5"/>
      <!-- x labels -->
      ${xLabels}
    </svg>
    <div class="curve-legend">
      <span class="legend-dot" style="background:#22c55e"></span>cum net PnL &nbsp;
      <span class="legend-dot" style="background:#6366f1;opacity:0.6"></span>成交数/桶
    </div>`;
}

// ---- PnL 归因瀑布图 ----

export function renderPnlAttribution(data) {
  if (!data) return noData('PnL 归因');
  const wf = data.waterfall || [];
  if (wf.length === 0) return `<div class="no-data">暂无瀑布数据</div>`;

  const gross = Number(wf[0]?.value || 0);
  const net = Number(wf[wf.length - 1]?.value || 0);

  const bars = wf.map((item) => {
    const v = Number(item.value);
    const pct = gross !== 0 ? Math.abs(v / gross) * 100 : 0;
    const isNet = item.label === 'net_pnl';
    const positive = v >= 0;
    return `
      <div class="wf-row">
        <div class="wf-label">${item.label}</div>
        <div class="wf-bar-wrap">
          <div class="wf-bar ${isNet ? 'wf-net' : positive ? 'wf-pos' : 'wf-neg'}"
               style="width:${Math.min(pct, 100).toFixed(1)}%"></div>
        </div>
        <div class="wf-val ${positive ? 'pnl-pos' : 'pnl-neg'}">${fmtUsdc(v)}</div>
      </div>`;
  });

  const perMarket = (data.per_market || [])
    .map(
      (m) =>
        `<div class="pm-row">
          <span class="mono pm-id">${m.market_id}</span>
          <span class="${Number(m.net_pnl) >= 0 ? 'pnl-pos' : 'pnl-neg'}">${fmtUsdc(m.net_pnl)}</span>
        </div>`
    )
    .join('');

  return `
    <div class="panel-header">
      ${badge(data.mode)}
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

// ---- RM 拒单流 ----

export function renderRiskRejects(data) {
  if (!data) return noData('RM 拒单');
  const rejects = data.rejects || [];
  if (rejects.length === 0) return `<div class="no-data">暂无拒单记录</div>`;

  const rows = rejects
    .map(
      (r) => `
      <tr>
        <td class="mono">${r.reject_id}</td>
        <td class="mono">${r.market_id}</td>
        <td><span class="reason-code">${r.reason_code}</span></td>
        <td>${r.side}</td>
        <td class="num">${Number(r.size).toLocaleString()}</td>
        <td class="num">${Number(r.price).toFixed(4)}</td>
        <td class="ts">${fmtTs(r.event_ts)}</td>
        <td class="ts">${fmtTs(r.ingestion_ts)}</td>
      </tr>`
    )
    .join('');

  return `
    <div class="panel-header">
      ${badge(data.mode)}
      <span class="panel-ts">as_of ${fmtTs(data.as_of_ts)}</span>
    </div>
    <div class="table-wrap">
      <table class="data-table">
        <thead>
          <tr>
            <th>Reject ID</th><th>Market ID</th><th>原因码</th>
            <th>方向</th><th>数量</th><th>价格</th>
            <th>event_ts</th><th>ingestion_ts</th>
          </tr>
        </thead>
        <tbody>${rows}</tbody>
      </table>
    </div>`;
}

// ---- GM-PAPER-G 门禁仪表 ----

export function renderGatePaper(data) {
  if (!data) return noData('PAPER-GATE');

  const items = [
    { label: '成交笔数', value: Number(data.n_trades).toLocaleString() },
    { label: '盈利日占比', value: fmtPct(data.positive_days_ratio) },
    { label: 'Sharpe', value: `${Number(data.sharpe).toFixed(3)} ± ${Number(data.sharpe_se).toFixed(3)}` },
    { label: 'p-value', value: Number(data.p_value).toFixed(4) },
    { label: '胜率 (hit_rate)', value: fmtPct(data.hit_rate) },
    { label: '最大回撤 (MDD)', value: fmtPct(data.max_drawdown) },
  ];

  const prelim = data.prelim_pass;
  const confirm = data.confirm_pass;

  const gateHtml = `
    <div class="gate-row">
      <div class="gate-item ${prelim ? 'gate-pass' : 'gate-fail'}">
        <div class="gate-icon">${prelim ? '&#10003;' : '&#10007;'}</div>
        <div>初步验证 (Prelim)</div>
      </div>
      <div class="gate-item ${confirm ? 'gate-pass' : 'gate-fail'}">
        <div class="gate-icon">${confirm ? '&#10003;' : '&#10007;'}</div>
        <div>确认放行 (Confirm)</div>
      </div>
    </div>`;

  const statsHtml = items.map((it) => statCard(it.label, it.value)).join('');

  return `
    <div class="panel-header">
      ${badge(data.mode || 'paper')}
      <span class="label-small">30日滚动窗口</span>
      <span class="panel-ts">as_of ${fmtTs(data.as_of_ts)}</span>
    </div>
    ${gateHtml}
    <div class="stats-grid">${statsHtml}</div>`;
}

// ---- 订单簿 ----

export function renderBook(data) {
  if (!data) return noData('订单簿');

  const bids = data.bids || [];
  const asks = data.asks || [];

  const maxSize = Math.max(
    ...bids.map((b) => Number(b.size)),
    ...asks.map((a) => Number(a.size)),
    1
  );

  function bookRows(levels, side) {
    return levels
      .map((lvl) => {
        const sz = Number(lvl.size);
        const pct = (sz / maxSize) * 100;
        return `
          <tr class="book-row-${side}">
            <td class="num book-price">${Number(lvl.price).toFixed(4)}</td>
            <td class="num">${sz.toLocaleString()}</td>
            <td class="depth-cell">
              <div class="depth-bar depth-${side}" style="width:${pct.toFixed(1)}%"></div>
            </td>
          </tr>`;
      })
      .join('');
  }

  const wssClass =
    data.wss_state === 'CONNECTED' ? 'wss-ok-text' : 'wss-off-text';

  return `
    <div class="panel-header">
      ${badge(data.mode)}
      <span class="mono book-cid">${data.condition_id}</span>
      <span class="${wssClass}">WSS ${data.wss_state}</span>
      <span class="panel-ts">seq#${data.sequence_no} gap:${data.gap_count}</span>
    </div>
    <div class="book-meta">
      <span>microprice <strong>${Number(data.microprice).toFixed(4)}</strong></span>
      <span>spread <strong>${Number(data.spread).toFixed(4)}</strong></span>
      <span>imbalance <strong>${Number(data.imbalance).toFixed(4)}</strong></span>
    </div>
    <div class="book-layout">
      <div class="book-side">
        <div class="book-side-header ask">Asks</div>
        <table class="data-table book-table">
          <thead><tr><th>价格</th><th>数量</th><th>深度</th></tr></thead>
          <tbody>${bookRows([...asks].reverse(), 'ask')}</tbody>
        </table>
      </div>
      <div class="book-side">
        <div class="book-side-header bid">Bids</div>
        <table class="data-table book-table">
          <thead><tr><th>价格</th><th>数量</th><th>深度</th></tr></thead>
          <tbody>${bookRows(bids, 'bid')}</tbody>
        </table>
      </div>
    </div>
    <div class="panel-footer">
      event_ts ${fmtTs(data.event_ts)} &nbsp; ingestion_ts ${fmtTs(data.ingestion_ts)}
    </div>`;
}

// ---- Prometheus metrics 原始文本 ----

export function renderMetrics(text) {
  if (!text) return noData('Prometheus /metrics');
  // 高亮 metric 名和值
  const escaped = text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
  return `<pre class="metrics-pre">${escaped}</pre>`;
}
