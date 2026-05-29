/**
 * panels.js — v4 单屏盯盘终端 渲染逻辑
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * v4 变更:
 *  - 双边订单簿卡片 (老板核心: token0/token1 并排, cross_spread/vig 显著标)
 *  - Polymarket 超链接 (market.polymarket_url)
 *  - 中文化 (专有名词保留, 集中映射表)
 *  - 信息扩展 (盘口元信息全量/tokens两边/book健康/signal_strength/model_conf/score全量)
 *  - 小尤 P0-01: 配色纪律 (删语义蓝/橙, PAPER改灰, DEMO改黄系, score-pre改灰, wf-net改黄)
 *  - 小尤 P0-02: demo fail-safe (data_source !== 'live' 为 demo)
 *  - 小尤 P0-03: 错误态可读 (区分"未接入"vs"拉取失败", 顶部失败 indicator)
 *  - 小宫 P2-03: sparkline 时间范围标注
 *
 * 约定:
 *  - 每个 render* 函数接收 data (可为 null), 返回 HTML string
 *  - null/空数据: 区分"正常未接入 (—)"vs"拉取失败 (拉取失败)"
 *  - 金额字段全部 Number() 解析
 *  - 4 时间戳字段 epoch_ns number
 *  - DEMO 标记: demo 数字旁显示 [demo] 角标 (老钱红线 P0)
 */

import {
  fmtTs, fmtUsdc, fmtBps, fmtPct, fmtClock, stalenessMs, modeBadge, fmtUptime,
  isEndpointFailing,
} from './api.js';

// ============================================================
// 中文映射表 (集中管理, 专有名词不译)
// ============================================================

const STATUS_ZH = {
  inplay:   '进行中',
  halftime: '中场',
  final:    '完场',
  pregame:  '赛前',
};

const SPORT_ZH = {
  basketball: '篮球',
  soccer:     '足球',
  football:   '橄榄球',
  baseball:   '棒球',
  tennis:     '网球',
  hockey:     '冰球',
};

const WSS_STATE_ZH = {
  CONNECTED:    '已连接',
  DISCONNECTED: '已断开',
  unknown:      '未知',
};

const REJECT_REASON_ZH = {
  MAX_POSITION_EXCEEDED:        '超过最大持仓',
  MARKET_NOT_ACCEPTING_ORDERS:  '市场暂不接单',
  KELLY_FRACTION_CAP:           'Kelly 仓位上限',
};

const SIDE_ZH = { BUY: '买', SELL: '卖' };

// ============================================================
// 通用工具
// ============================================================

function noData(label) {
  return `<div class="no-data">${label || '—'}</div>`;
}

function ph(v, fmt) {
  if (v == null) return '<span class="ph">—</span>';
  return fmt ? fmt(v) : String(v);
}

/**
 * 区分 "正常未接入" (data==null, 非失败) vs "拉取失败" (endpoint 报错)
 * endpointPath: '/api/v1/book/xxx' 等，用于查 fetchErrorMap
 */
function dataStatus(data, label, endpointPath) {
  if (data != null) return null; // 有数据，不需要占位
  if (endpointPath && isEndpointFailing(endpointPath)) {
    return `<div class="data-fail"><span class="fail-chip">拉取失败</span> ${label}</div>`;
  }
  return `<div class="no-data">${label} <span class="ph">未接入</span></div>`;
}

function demoBadge(isDemoData) {
  if (!isDemoData) return '';
  return '<span class="demo-chip" title="演示数据·非实盘">demo</span>';
}

function advisoryBadge() {
  return '<span class="advisory-chip" title="paper期模型旁路·不下单">仅供参考</span>';
}

// staleness chip: ms → green/yellow/red
function staleChip(ms) {
  if (ms == null) return '';
  const cls = ms < 2000 ? 'stale-ok' : ms < 10000 ? 'stale-warn' : 'stale-err';
  return `<span class="stale-chip ${cls}" title="${Math.round(ms)}ms ago">${ms < 1000 ? `${Math.round(ms)}ms` : `${(ms / 1000).toFixed(1)}s`}</span>`;
}

// ============================================================
// 顶部常驻条渲染
// ============================================================

export function renderTopBar(healthz, status, attribution, metrics, gate) {
  const s = status || {};
  const h = healthz || {};
  const g = gate || {};

  // P0-02: fail-safe — 非 live 均视为 demo (status 失败时默认 demo)
  const isDemo = s.data_source !== 'live';

  // mode badge
  const mode = s.mode || 'paper';
  const mb = modeBadge(mode);
  const modeBadgeHtml = `<span class="badge ${mb.cls}">${mb.text}</span>`;

  // state — P0-03: status 失败时显示 API OFFLINE
  let stateHtml;
  if (!status) {
    // status 拉取失败
    const failPath = '/status';
    if (isEndpointFailing(failPath)) {
      stateHtml = `<span class="state-label state-halted">API OFFLINE</span>`;
    } else {
      stateHtml = `<span class="state-label state-drain">连接中...</span>`;
    }
  } else {
    const stateClass = s.state === 'RUNNING' ? 'state-running'
      : s.state === 'HALTED' ? 'state-halted' : 'state-drain';
    stateHtml = `<span class="state-label ${stateClass}">${s.state || '—'}</span>`;
  }

  // uptime
  const uptimeSec = Number(h.uptime_sec || s.uptime_sec || 0);
  const uptimeHtml = `运行 ${fmtUptime(uptimeSec)}`;

  // wss dots
  const wss = s.wss_connected || {};
  const wssHtml = Object.entries(wss)
    .map(([k, v]) => `<span class="wss-dot ${v ? 'wss-ok' : 'wss-off'}" title="${k}"></span>`)
    .join('');

  // net PnL (最重要, 紧跟 state)
  let netPnl = null;
  if (attribution && attribution.waterfall) {
    netPnl = Number(attribution.waterfall.net);
  }
  const pnlClass = netPnl != null && netPnl >= 0 ? 'pnl-pos' : 'pnl-neg';
  const pnlHtml = netPnl != null
    ? `<span class="${pnlClass} mono-strong">${fmtUsdc(netPnl)}</span>`
    : '<span class="ph">—</span>';

  // paper-gate
  let gateHtml = '<span class="ph">—</span>';
  if (g.has_data) {
    const prelim = g.prelim_pass;
    const confirm = g.confirm_pass;
    gateHtml = `<span class="gate-chip ${prelim ? 'gate-ok' : 'gate-fail'}">初审${prelim ? '✓' : '✗'}</span> <span class="gate-chip ${confirm ? 'gate-ok' : 'gate-fail'}">确认${confirm ? '✓' : '✗'}</span>`;
  }

  // p99 from metrics text parse
  let p99Html = '<span class="ph">—</span>';
  if (metrics) {
    const m = metrics.match(/stcpp_loop_latency_p99_us[^\n]*\s+([\d.]+)/);
    if (m) p99Html = `<span class="mono-dim">${Number(m[1]).toFixed(0)} us</span>`;
  }

  // staleness from metrics text
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

  // P0-03: 全局 API 失败指示
  const apiErrHtml = isEndpointFailing('/status') || isEndpointFailing('/api/v1/positions')
    ? `<span class="api-err-chip">API 异常</span>`
    : '';

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
    apiErr: apiErrHtml,
  };
}

// ============================================================
// PnL 净值曲线 sparkline
// ============================================================

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

  // P2-03 小宫: 时间范围标注
  const windowSec = Number(data.window_sec || 3600);
  const windowLabel = windowSec >= 3600
    ? `近 ${Math.round(windowSec / 3600)}h`
    : `近 ${Math.round(windowSec / 60)}min`;
  const firstTs = buckets[0] ? fmtTs(buckets[0].bucket_start_ts) : '—';
  const lastTs = fmtTs(data.as_of_ts);

  return `
    <div class="spark-header">
      <span class="spark-label">净值曲线</span>
      <span class="spark-window">${windowLabel} · ${firstTs} – ${lastTs}</span>
      <span class="${latestPnl >= 0 ? 'pnl-pos' : 'pnl-neg'} mono-strong">${fmtUsdc(latestPnl)}</span>
      <span class="spark-ts">更新 ${fmtTs(data.as_of_ts)}</span>
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

// ============================================================
// Market 卡片
// ============================================================

/**
 * renderMarketCard — 单个 market 卡片渲染
 * @param {string}  marketId
 * @param {Array}   posRows     — positions 中属于此 marketId 的行
 * @param {object}  market      — /api/v1/market/{id} 响应 (含 tokens[]/polymarket_url)
 * @param {object}  book        — /api/v1/book/{id} BinaryMarketBookView 双边
 * @param {object}  score       — /api/v1/score/{event_id} 响应 (可 null)
 * @param {object}  quote       — /api/v1/quote/{condition_id} 响应 (可 null)
 * @param {Array}   rejectRows  — rejects 中属于此 marketId 的行
 * @param {number}  perMarketPnl — attribution.per_market 中此 market 的 net_pnl
 * @param {boolean} isDemoData  — data_source !== 'live'
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
  const conditionId = (market && market.condition_id) || marketId;

  // ① 比分/赛况条
  const scoreBlock = renderScoreBlock(score, marketId);

  // ② 盘口元信息 (全量)
  const marketInfoBlock = renderMarketInfoBlock(market, isDemoData);

  // ③ 双边订单簿 (老板核心)
  const dualBookBlock = renderDualBookBlock(book, quote, isDemoData, conditionId);

  // ④ 持仓 + 净PnL
  const posBlock = renderPosBlock(posRows, perMarketPnl, book);

  // ⑤ 拒单角标
  const rejectBadge = renderRejectBadge(rejectRows);

  // ⑥ 数据源/stale 标记
  const staleBlock = renderStaleBlock(market, book, score, isDemoData);

  // market 状态指示
  const mActive = market ? (market.accepting_orders ? '' : ' card-inactive') : '';

  // 超链接 (P0, 老板要求)
  const url = market && market.polymarket_url;
  const titleEl = url
    ? `<a class="mkt-id mkt-link" href="${url}" target="_blank" rel="noopener noreferrer">${conditionId}</a>`
    : `<span class="mkt-id">${conditionId}</span>`;

  return `
    <div class="mkt-card${mActive}" data-market="${marketId}">
      <div class="mkt-header">
        ${titleEl}
        ${rejectBadge}
        ${staleBlock}
      </div>

      ${scoreBlock}
      ${marketInfoBlock}
      ${dualBookBlock}
      ${posBlock}
    </div>`;
}

// ============================================================
// ① 比分/赛况条
// ============================================================

function renderScoreBlock(score, marketId) {
  if (!score) {
    const ep = `/api/v1/score/${marketId}`;
    if (isEndpointFailing(ep)) {
      return `<div class="score-bar score-bar-empty"><span class="fail-chip">比分拉取失败</span></div>`;
    }
    return `<div class="score-bar score-bar-empty"><span class="ph">比分未接入</span></div>`;
  }

  const statusZh = STATUS_ZH[score.status] || score.status || '?';
  const statusCls = score.status === 'inplay' ? 'score-live'
    : score.status === 'halftime' ? 'score-ht'
    : score.status === 'final' ? 'score-ft'
    : 'score-pre'; // pregame → 灰

  const sportZh = SPORT_ZH[score.sport] || score.sport || '';
  const homeStr = score.home || '—';
  const awayStr = score.away || '—';
  const homeScore = score.home_score != null ? score.home_score : '—';
  const awayScore = score.away_score != null ? score.away_score : '—';
  const period = score.period || '—';
  const clock = score.clock_sec != null ? fmtClock(score.clock_sec) : '—';

  // 赛况栏: 主队 分 – 分 客队 | 节 时钟 状态 运动
  return `
    <div class="score-bar">
      <span class="score-team">${homeStr}</span>
      <span class="score-num">${homeScore}</span>
      <span class="score-dash">–</span>
      <span class="score-num">${awayScore}</span>
      <span class="score-team">${awayStr}</span>
      <span class="score-sep">|</span>
      <span class="score-period">${period}</span>
      <span class="score-clock">${clock}</span>
      <span class="score-status ${statusCls}">${statusZh}</span>
      <span class="score-sport">${sportZh}</span>
    </div>`;
}

// ============================================================
// ② 盘口元信息 (全量)
// ============================================================

function renderMarketInfoBlock(market, isDemoData) {
  if (!market) {
    return `<div class="market-info-block"><span class="ph">盘口信息未接入</span></div>`;
  }

  const demo = demoBadge(isDemoData);

  // 状态三态
  const stateStr = market.resolved ? '已结算'
    : market.closed ? '已关闭'
    : market.active ? '活跃'
    : '未知';
  const stateCls = market.resolved ? 'state-ft'
    : market.closed ? 'state-ft'
    : market.active ? 'state-active'
    : '';

  const accepting = market.accepting_orders
    ? '<span class="acc-yes">接单中</span>'
    : '<span class="acc-no">不接单</span>';

  const negRisk = market.neg_risk
    ? `<span class="meta-tag neg-risk">neg_risk</span>${market.neg_risk_market_id ? ` <span class="mono-dim" title="neg_risk_market_id" style="font-size:9px">${market.neg_risk_market_id}</span>` : ''}`
    : '';

  // tokens 两边价格
  const tokens = market.tokens || [];
  const tokensHtml = tokens.map((t) => {
    const winnerMark = t.winner ? ' <span class="winner-chip">胜</span>' : '';
    return `<span class="token-chip"><span class="token-outcome">${t.outcome}</span> <span class="token-price">${Number(t.price).toFixed(3)}</span>${winnerMark}</span>`;
  }).join(' ');

  return `
    <div class="market-info-block">
      <div class="market-info-row">
        <span class="meta-label">状态</span>
        <span class="meta-val ${stateCls}">${stateStr}</span>
        ${accepting}
        ${negRisk}
        ${demo}
      </div>
      <div class="market-info-row">
        <span class="meta-label">tick</span>
        <span class="meta-val mono-dim">${market.tick_size ?? '—'}</span>
        <span class="meta-label">手续费</span>
        <span class="meta-val mono-dim">${market.fee_rate != null ? fmtPct(market.fee_rate) : '—'}</span>
        <span class="meta-label">来源</span>
        <span class="meta-val mono-dim">${market.source || '—'}</span>
      </div>
      ${tokensHtml ? `<div class="market-info-row tokens-row">${tokensHtml}</div>` : ''}
    </div>`;
}

// ============================================================
// ③ 双边订单簿 + 量化决策区 (老板核心)
// ============================================================

function renderDualBookBlock(book, quote, isDemoData, conditionId) {
  const demo = demoBadge(isDemoData);
  const advisory = advisoryBadge();

  // cross_spread / vig 显著标
  let vigHtml = '';
  if (book) {
    const cs = Number(book.cross_spread);
    if (Number.isFinite(cs)) {
      const vigCls = cs < 0.02 ? 'vig-low' : cs < 0.04 ? 'vig-mid' : 'vig-high';
      vigHtml = `<span class="vig-badge ${vigCls}" title="ask0+ask1-1 = 等效vig">vig ${(cs * 100).toFixed(2)}%</span>`;
    }
  }

  // 量化参数区 (fair/edge/Kelly + signal/conf)
  const quoteHtml = renderQuoteSection(quote, isDemoData);

  // 双边 book
  let halfBooksHtml;
  if (!book) {
    const ep = `/api/v1/book/${conditionId}`;
    if (isEndpointFailing(ep)) {
      halfBooksHtml = `<div class="data-fail"><span class="fail-chip">订单簿拉取失败</span></div>`;
    } else {
      halfBooksHtml = `<div class="no-data"><span class="ph">订单簿未接入</span></div>`;
    }
  } else {
    const t0 = book.token0 || {};
    const t1 = book.token1 || {};
    halfBooksHtml = `
      <div class="dual-book-grid">
        ${renderHalfBook(t0, 'token0')}
        ${renderHalfBook(t1, 'token1')}
      </div>`;
  }

  return `
    <div class="dual-book-block">
      <div class="block-label">
        双边订单簿 ${vigHtml} ${demo} ${advisory}
      </div>
      ${quoteHtml}
      ${halfBooksHtml}
    </div>`;
}

/**
 * 单边 book 渲染 (token0 或 token1)
 */
function renderHalfBook(half, side) {
  const outcome = half.outcome || side;
  const bid = Number(half.best_bid);
  const ask = Number(half.best_ask);
  const spread = Number(half.spread);
  const microprice = Number(half.microprice);
  const imbalance = Number(half.imbalance);
  const seqNo = half.sequence_no != null ? half.sequence_no : '—';
  const gapCount = half.gap_count != null ? half.gap_count : '—';
  const wssRaw = half.wss_state || 'unknown';
  const wssZh = WSS_STATE_ZH[wssRaw] || wssRaw;
  const wssOk = wssRaw === 'CONNECTED';
  const wssCls = wssOk ? 'wss-ok-text' : 'wss-off-text';

  // imbalance bar (-1..1 → 0..100%)
  const imbPct = Number.isFinite(imbalance) ? ((imbalance + 1) / 2 * 100).toFixed(1) : '50';

  // 深度档 (最多展示前3档)
  const bids = (half.bids || []).slice(0, 3);
  const asks = (half.asks || []).slice(0, 3);

  const depthRows = [];
  const maxLen = Math.max(bids.length, asks.length);
  for (let i = 0; i < maxLen; i++) {
    const b = bids[i];
    const a = asks[i];
    depthRows.push(`
      <div class="depth-row">
        <span class="depth-bid-size">${b ? b.size.toLocaleString() : ''}</span>
        <span class="depth-bid-price">${b ? Number(b.price).toFixed(4) : ''}</span>
        <span class="depth-mid"></span>
        <span class="depth-ask-price">${a ? Number(a.price).toFixed(4) : ''}</span>
        <span class="depth-ask-size">${a ? a.size.toLocaleString() : ''}</span>
      </div>`);
  }

  return `
    <div class="half-book">
      <div class="half-book-header">
        <span class="half-outcome">${outcome}</span>
        <span class="${wssCls} half-wss" title="WSS: ${wssRaw}">${wssZh}</span>
      </div>
      <div class="half-ba-row">
        <span class="bid-price">${Number.isFinite(bid) ? bid.toFixed(4) : '—'}</span>
        <span class="book-sep">买</span>
        <span class="book-spread" title="价差">${Number.isFinite(spread) ? (spread * 100).toFixed(2) + '%' : '—'}</span>
        <span class="book-sep">卖</span>
        <span class="ask-price">${Number.isFinite(ask) ? ask.toFixed(4) : '—'}</span>
      </div>
      <div class="half-meta-row">
        <span class="book-meta-label">微观价</span>
        <span class="book-meta-val mono-dim">${Number.isFinite(microprice) ? microprice.toFixed(4) : '—'}</span>
        <span class="book-meta-label">失衡</span>
        <div class="imb-track" title="失衡 ${Number.isFinite(imbalance) ? imbalance.toFixed(3) : '—'}">
          <div class="imb-fill" style="width:${imbPct}%"></div>
        </div>
        <span class="book-meta-val mono-dim">${Number.isFinite(imbalance) ? imbalance.toFixed(3) : '—'}</span>
      </div>
      <div class="depth-section">
        <div class="depth-header">
          <span class="depth-col-label">量 (买)</span>
          <span class="depth-col-label">价</span>
          <span class="depth-col-mid"></span>
          <span class="depth-col-label">价</span>
          <span class="depth-col-label">量 (卖)</span>
        </div>
        ${depthRows.join('')}
      </div>
      <div class="half-health-row">
        <span class="book-meta-label">seq</span>
        <span class="book-meta-val mono-dim">${seqNo}</span>
        <span class="book-meta-label">gap</span>
        <span class="book-meta-val ${Number(gapCount) > 0 ? 'pnl-neg' : 'mono-dim'}">${gapCount}</span>
      </div>
    </div>`;
}

/**
 * 量化决策区: fair/edge/Kelly/signal/conf (老板:"做决策也需要另一边的数据")
 */
function renderQuoteSection(quote, isDemoData) {
  const demo = demoBadge(isDemoData);
  if (!quote) {
    return `<div class="quote-section"><span class="ph">量化参数未接入</span> ${demo}</div>`;
  }

  const fairValue = Number(quote.fair_value);
  const marketMid = Number(quote.market_mid);
  const edgeBps = Number(quote.edge_bps);
  const kelly = Number(quote.kelly_fraction);
  const notional = Number(quote.suggested_notional);
  const signalStr = Number(quote.signal_strength);
  const modelConf = Number(quote.model_conf);

  const edgePositive = fairValue >= marketMid;
  const edgeBarPct = Math.min(Math.abs(edgeBps) / 100, 1) * 100;
  const edgeCls = edgePositive ? 'edge-pos' : 'edge-neg';

  return `
    <div class="quote-section">
      <div class="quote-row-main">
        <span class="q-label">公允价</span>
        <span class="fair-val mono-strong">${Number.isFinite(fairValue) ? fairValue.toFixed(4) : '—'}</span>
        <span class="q-label">中间价</span>
        <span class="mid-val mono-dim">${Number.isFinite(marketMid) ? marketMid.toFixed(4) : '—'}</span>
        ${demo}
      </div>
      <div class="edge-bar-row">
        <span class="edge-label">优势</span>
        <div class="edge-track">
          <div class="edge-fill ${edgeCls}" style="width:${edgeBarPct.toFixed(1)}%"></div>
        </div>
        <span class="edge-val ${edgeCls}">${fmtBps(edgeBps)}</span>
      </div>
      <div class="kelly-row">
        <span class="q-label">Kelly 建议仓位</span>
        <span class="kelly-val mono-strong">${Number.isFinite(kelly) ? (kelly * 100).toFixed(2) : '—'}%</span>
        <span class="q-label">建议额</span>
        <span class="notional-val mono-dim">$${Number.isFinite(notional) ? notional.toLocaleString() : '—'}</span>
      </div>
      <div class="model-row">
        <span class="q-label">信号强度</span>
        <span class="model-val mono-dim">${Number.isFinite(signalStr) ? signalStr.toFixed(3) : '—'}</span>
        <span class="q-label">模型置信</span>
        <span class="model-val mono-dim">${Number.isFinite(modelConf) ? modelConf.toFixed(3) : '—'}</span>
      </div>
    </div>`;
}

// ============================================================
// ④ 持仓块 (按 outcome 分行, 补 per_market PnL)
// ============================================================

function renderPosBlock(posRows, perMarketPnl, book) {
  const pmPnlStr = perMarketPnl != null
    ? `<span class="${perMarketPnl >= 0 ? 'pnl-pos' : 'pnl-neg'} mono-strong">${fmtUsdc(perMarketPnl)}</span>`
    : '<span class="ph">—</span>';

  if (!posRows || posRows.length === 0) {
    return `
      <div class="pos-block">
        <div class="block-label">持仓 / 盘口净PnL ${pmPnlStr}</div>
        <div class="ph">无持仓</div>
      </div>`;
  }

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
        <span class="pos-outcome">${p.outcome || '—'}</span>
        <span class="pos-qty mono-dim">${qtySign}${netQty.toLocaleString()}u</span>
        <span class="pos-avg-mark mono-dim">${Number.isFinite(avg) ? avg.toFixed(4) : '—'} → ${Number.isFinite(mark) ? mark.toFixed(4) : '—'}</span>
        <span class="pos-pnl ${pnlCls} mono-strong">${fmtUsdc(pnlTotal)}</span>
      </div>`;
  }).join('');

  return `
    <div class="pos-block">
      <div class="block-label">持仓 / 盘口净PnL ${pmPnlStr}</div>
      ${rows}
    </div>`;
}

// ============================================================
// ⑤ 拒单角标
// ============================================================

function renderRejectBadge(rejectRows) {
  if (!rejectRows || rejectRows.length === 0) return '';
  const latest = rejectRows[0];
  const reasonZh = REJECT_REASON_ZH[latest.reason_code] || latest.reason_code;
  const allReasons = rejectRows.map((r) => {
    const rz = REJECT_REASON_ZH[r.reason_code] || r.reason_code;
    const sideZh = SIDE_ZH[r.side] || r.side;
    return `${rz} · ${sideZh} ${r.size} @${r.price}`;
  }).join('\n');
  return `<span class="reject-badge" title="${allReasons}">拒单×${rejectRows.length} <span class="reject-reason">${reasonZh}</span></span>`;
}

// ============================================================
// ⑥ 数据源/stale 标记
// ============================================================

function renderStaleBlock(market, book, score, isDemoData) {
  const parts = [];

  if (isDemoData) {
    parts.push('<span class="demo-chip-sm">DEMO</span>');
  }

  if (book) {
    const ms = stalenessMs(book.as_of_ts_ns || book.book_as_of_ts || (book.token0 && book.token0.book_as_of_ts));
    parts.push(staleChip(ms));
  }

  if (score) {
    const ms = stalenessMs(score.score_as_of_ts);
    parts.push(staleChip(ms));
  }

  if (parts.length === 0) return '';
  return `<span class="stale-group">${parts.join('')}</span>`;
}

// ============================================================
// Market Grid (组装所有卡片)
// ============================================================

export function renderMarketGrid(marketData) {
  if (!marketData || marketData.length === 0) {
    return `<div class="no-data grid-placeholder">无市场数据</div>`;
  }
  return marketData.map((d) => renderMarketCard(d)).join('');
}

// ============================================================
// PnL 归因瀑布图 (折叠区)
// ============================================================

export function renderPnlAttribution(data) {
  if (!data) return noData('PnL 归因');
  const wfObj = data.waterfall || {};
  if (Object.keys(wfObj).length === 0) return `<div class="no-data">暂无瀑布数据</div>`;

  const WF_LABELS = {
    gross:    '毛收益',
    fee:      '手续费',
    gas:      'Gas',
    slippage: '滑点',
    spread:   '价差收益',
    net:      '净收益',
  };
  const WF_ORDER = ['gross', 'fee', 'gas', 'slippage', 'spread', 'net'];
  const gross = Number(wfObj.gross || 0);

  const bars = WF_ORDER.map((key) => {
    const v = Number(wfObj[key] ?? 0);
    const pct = gross !== 0 ? Math.abs(v / gross) * 100 : 0;
    const isNet = key === 'net';
    const positive = v >= 0;
    const label = WF_LABELS[key] || key;
    return `
      <div class="wf-row">
        <div class="wf-label">${label}</div>
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
      <span class="panel-ts">更新 ${fmtTs(data.as_of_ts)}</span>
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

// ============================================================
// Prometheus metrics 原始文本 (折叠区)
// ============================================================

export function renderMetrics(text) {
  if (!text) return noData('Prometheus /metrics');
  const escaped = text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  return `<pre class="metrics-pre">${escaped}</pre>`;
}
