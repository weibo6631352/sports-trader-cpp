/**
 * panels.js — v5 赛事分组卡布局 渲染逻辑
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * v5 变更 (方向 A: 赛事分组卡, 老板选定 + 小尤设计):
 *  - renderEventGrid / renderEventGroup: 按 event_id 分组, 一赛事一区块
 *  - renderEventHeader: 比分/赛况只在赛事头渲染一次 (去乱 R1)
 *  - renderConditionColumn: 每盘口一列 (盘口名 + 量化 + 双边簿 + 持仓)
 *  - 小尤 6 条去乱规则全部落地:
 *    R1: 比分条挪赛事头, 列内不重复渲染
 *    R2: 颜色语义收敛 — green=bid/正PnL/正Kelly; wss-ok/stale-ok → 灰点
 *    R3: 字号三档 — 14px主数值 / 11px次要 / 10px标签
 *    R4: 区块分隔用背景色块 (bg/bg2/bg3) 代替 border-bottom 横线
 *    R5: 拒单/gap → 右上角小红点 + tooltip (不内联主路径)
 *    R6: chip 禁 flex-wrap, overflow 截断
 *
 * 约定:
 *  - 每个 render* 函数接收 data (可为 null), 返回 HTML string
 *  - null/空数据: 区分"正常未接入 (—)"vs"拉取失败 (拉取失败)"
 *  - 金额字段全部 Number() 解析
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

// 盘口类型中文 — 从 market_id 尾缀推断
const MARKET_TYPE_ZH = {
  ml:     '胜负盘',
  total:  '大小盘',
  spread: '让分盘',
  h1:     '上半场',
  h2:     '下半场',
  q1:     '第一节',
  q2:     '第二节',
  q3:     '第三节',
  q4:     '第四节',
  series: '系列赛',
  prop:   '特殊盘',
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

function demoBadge(isDemoData) {
  if (!isDemoData) return '';
  return '<span class="demo-chip" title="演示数据·非实盘">demo</span>';
}

function advisoryBadge() {
  return '<span class="advisory-chip" title="paper期模型旁路·不下单">仅供参考</span>';
}

/**
 * 推断盘口类型中文名 — 从 condition_id 最后一段取
 * e.g. 'nba-lal-bos-ml' → 'ml' → '胜负盘'
 */
function inferMarketTypeZh(conditionId) {
  if (!conditionId) return '—';
  const parts = conditionId.split('-');
  const last = parts[parts.length - 1];
  return MARKET_TYPE_ZH[last] || last.toUpperCase();
}

/**
 * 从 market.tokens 推断盘口标题参数 (用于大小盘/让分盘显示线值)
 * e.g. 大小盘 → "大小盘 215.5" (从 outcome 名提取数字)
 */
function inferMarketLabel(conditionId, market) {
  const typeZh = inferMarketTypeZh(conditionId);
  if (!market || !market.tokens || market.tokens.length === 0) return typeZh;

  // 大小盘: 从 outcome 名取数字 e.g. "大 215.5" → 215.5
  const totalMatch = market.tokens[0] && market.tokens[0].outcome
    ? market.tokens[0].outcome.match(/[\d.]+/)
    : null;
  if (conditionId.includes('-total') && totalMatch) {
    return `${typeZh} ${totalMatch[0]}`;
  }
  // 让分盘: 从 outcome 取让分数
  const spreadMatch = market.tokens[0] && market.tokens[0].outcome
    ? market.tokens[0].outcome.match(/[+-][\d.]+/)
    : null;
  if (conditionId.includes('-spread') && spreadMatch) {
    return `${typeZh} ${spreadMatch[0]}`;
  }
  return typeZh;
}

// R2: wss-ok → 灰色小圆点 (降级, 不再绿色竞争注意力)
function wssStatusDot(wssState) {
  const ok = wssState === 'CONNECTED';
  return `<span class="wss-dot-sm ${ok ? 'wss-dot-ok' : 'wss-dot-off'}" title="WSS: ${wssState || 'unknown'}"></span>`;
}

// staleness 小点 (R2: stale-ok 降为灰点)
function staleIndicator(ms) {
  if (ms == null) return '';
  const cls = ms < 2000 ? 'stale-dot-ok' : ms < 10000 ? 'stale-dot-warn' : 'stale-dot-err';
  const label = ms < 1000 ? `${Math.round(ms)}ms` : `${(ms / 1000).toFixed(1)}s`;
  return `<span class="stale-dot ${cls}" title="数据延迟 ${label}"></span>`;
}

// ============================================================
// 顶部常驻条渲染
// ============================================================

export function renderTopBar(healthz, status, attribution, metrics, gate) {
  const s = status || {};
  const h = healthz || {};
  const g = gate || {};

  // P0-02: fail-safe
  const isDemo = s.data_source !== 'live';

  const mode = s.mode || 'paper';
  const mb = modeBadge(mode);
  const modeBadgeHtml = `<span class="badge ${mb.cls}">${mb.text}</span>`;

  // state
  let stateHtml;
  if (!status) {
    stateHtml = isEndpointFailing('/status')
      ? `<span class="state-label state-halted">API OFFLINE</span>`
      : `<span class="state-label state-drain">连接中...</span>`;
  } else {
    const stateClass = s.state === 'RUNNING' ? 'state-running'
      : s.state === 'HALTED' ? 'state-halted' : 'state-drain';
    stateHtml = `<span class="state-label ${stateClass}">${s.state || '—'}</span>`;
  }

  const uptimeSec = Number(h.uptime_sec || s.uptime_sec || 0);
  const uptimeHtml = `运行 ${fmtUptime(uptimeSec)}`;

  // R2: wss 连接状态 → 灰色小点 (不再亮绿竞争注意力)
  const wss = s.wss_connected || {};
  const wssHtml = Object.entries(wss)
    .map(([k, v]) => `<span class="wss-dot-sm ${v ? 'wss-dot-ok' : 'wss-dot-off'}" title="${k}"></span>`)
    .join('');

  let netPnl = null;
  if (attribution && attribution.waterfall) {
    netPnl = Number(attribution.waterfall.net);
  }
  const pnlClass = netPnl != null && netPnl >= 0 ? 'pnl-pos' : 'pnl-neg';
  const pnlHtml = netPnl != null
    ? `<span class="${pnlClass} mono-strong">${fmtUsdc(netPnl)}</span>`
    : '<span class="ph">—</span>';

  let gateHtml = '<span class="ph">—</span>';
  if (g.has_data) {
    const prelim = g.prelim_pass;
    const confirm = g.confirm_pass;
    gateHtml = `<span class="gate-chip ${prelim ? 'gate-ok' : 'gate-fail'}">初审${prelim ? '✓' : '✗'}</span> <span class="gate-chip ${confirm ? 'gate-ok' : 'gate-fail'}">确认${confirm ? '✓' : '✗'}</span>`;
  }

  let p99Html = '<span class="ph">—</span>';
  if (metrics) {
    const m = metrics.match(/stcpp_loop_latency_p99_us[^\n]*\s+([\d.]+)/);
    if (m) p99Html = `<span class="mono-dim">${Number(m[1]).toFixed(0)} us</span>`;
  }

  let stalenessHtml = '<span class="ph">—</span>';
  if (metrics) {
    const m = metrics.match(/stcpp_data_staleness_ms_max[^\n]*\s+([\d.]+)/);
    if (m) {
      const ms = Number(m[1]);
      const cls = ms < 100 ? 'stale-dim' : ms < 1000 ? 'stale-warn-text' : 'stale-err-text';
      stalenessHtml = `<span class="${cls} mono-dim">${ms.toFixed(0)} ms</span>`;
    }
  }

  const rmRejects = s.rm_rejects_last_60s;
  const rejectsHtml = rmRejects != null
    ? `<span class="${rmRejects > 0 ? 'pnl-neg mono-dim' : 'mono-dim'}">${rmRejects}</span>`
    : '<span class="ph">—</span>';

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

  // P2-03: 时间范围标注
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
// v5: Event Grid (赛事分组网格)
// ============================================================

/**
 * renderEventGrid — 顶层渲染入口
 * eventGroups: [{ eventId, score, conditions: [...] }]
 */
export function renderEventGrid(eventGroups) {
  if (!eventGroups || eventGroups.length === 0) {
    return `<div class="no-data grid-placeholder">无市场数据</div>`;
  }
  return eventGroups.map((eg) => renderEventGroup(eg)).join('');
}

/**
 * renderEventGroup — 单个赛事区块
 * 包含: 赛事头(比分/赛况, 只渲染一次) + 盘口并列区(多列)
 */
function renderEventGroup({ eventId, score, conditions }) {
  // R1: 比分条只在赛事头渲染一次
  const headerHtml = renderEventHeader(score, conditions);

  // 盘口列 (横向并列)
  const colsHtml = conditions.map((c) => renderConditionColumn(c)).join('');

  // 赛事区块激活状态 (所有盘口都不接单则整体降调)
  const allInactive = conditions.every((c) => c.market && !c.market.accepting_orders);
  const inactiveCls = allInactive ? ' event-group-inactive' : '';

  return `
    <div class="event-group${inactiveCls}" data-event="${eventId || ''}">
      ${headerHtml}
      <div class="event-columns">
        ${colsHtml}
      </div>
    </div>`;
}

/**
 * renderEventHeader — 赛事头 (比分/赛况/运动/超链接/DEMO/stale)
 * R1: 此处渲染比分, renderConditionColumn 内不再重复
 */
function renderEventHeader(score, conditions) {
  // 从 conditions 中取第一个有 market 的
  const firstMkt = conditions.find((c) => c.market) ? conditions.find((c) => c.market).market : null;
  const isDemoData = conditions.some((c) => c.isDemoData);

  // 运动类型
  const sport = (score && score.sport) || (firstMkt && firstMkt.sport) || null;
  const sportZh = sport ? (SPORT_ZH[sport] || sport) : '';
  const sportTag = sportZh ? `<span class="evt-sport-tag">${sportZh}</span>` : '';

  // 超链接 (Polymarket event URL, 取第一个)
  const eventUrl = firstMkt && firstMkt.polymarket_url;
  const eventIdStr = (score && score.event_id) || (firstMkt && firstMkt.event_id) || '—';

  // DEMO badge
  const demoHtml = isDemoData
    ? `<span class="demo-chip" title="演示数据·非实盘">DEMO</span>`
    : '';

  // stale dots (book stale from conditions, score stale)
  const bookStaleDots = conditions.map((c) => {
    if (!c.book) return '';
    const ms = stalenessMs(c.book.as_of_ts_ns || (c.book.token0 && c.book.token0.book_as_of_ts));
    return staleIndicator(ms);
  }).join('');

  let scoreStaleDot = '';
  if (score) {
    const ms = stalenessMs(score.score_as_of_ts);
    scoreStaleDot = staleIndicator(ms);
  }

  // 比分/赛况主体
  let scoreBody;
  if (!score) {
    scoreBody = `<span class="evt-teams-placeholder ph">—</span>`;
  } else {
    const statusZh = STATUS_ZH[score.status] || score.status || '?';
    const statusCls = score.status === 'inplay' ? 'score-live'
      : score.status === 'halftime' ? 'score-ht'
      : score.status === 'final' ? 'score-ft'
      : 'score-pre';

    const home = score.home || '—';
    const away = score.away || '—';
    const homeScore = score.home_score != null ? score.home_score : '—';
    const awayScore = score.away_score != null ? score.away_score : '—';
    const period = score.period || '—';
    const clock = score.clock_sec != null ? fmtClock(score.clock_sec) : '';

    scoreBody = `
      <span class="evt-team">${home}</span>
      <span class="evt-score">${homeScore}</span>
      <span class="evt-dash">—</span>
      <span class="evt-score">${awayScore}</span>
      <span class="evt-team">${away}</span>
      <span class="evt-sep">|</span>
      <span class="evt-period">${period}</span>
      ${clock ? `<span class="evt-clock">${clock}</span>` : ''}
      <span class="evt-status ${statusCls}">${statusZh}</span>`;
  }

  const linkEl = eventUrl
    ? `<a class="evt-link" href="${eventUrl}" target="_blank" rel="noopener noreferrer" title="${eventIdStr}">Polymarket</a>`
    : '';

  return `
    <div class="event-header">
      <div class="event-header-main">
        ${sportTag}
        ${scoreBody}
        ${linkEl}
        ${demoHtml}
        <span class="evt-stale-group">${bookStaleDots}${scoreStaleDot}</span>
      </div>
    </div>`;
}

// ============================================================
// v5: 单盘口列 (条件/condition column)
// ============================================================

/**
 * renderConditionColumn — 单个盘口列
 * 包含: 盘口标题 + 量化决策行 + 双边迷你订单簿 + 持仓行
 * R1: 不再渲染比分 (移到赛事头)
 */
function renderConditionColumn({ conditionId, posRows, market, book, quote, rejectRows, perMarketPnl, isDemoData }) {
  const condId = (market && market.condition_id) || conditionId;
  const marketLabel = inferMarketLabel(condId, market);

  // R5: 拒单/gap → 右上角小红点 + tooltip
  const rejectDot = renderRejectDot(rejectRows, book);

  // 盘口激活状态
  const inactive = market && !market.accepting_orders;
  const inactiveCls = inactive ? ' col-inactive' : '';

  // 接单状态 → 灰色小点 (R2: acc-yes 不再绿色)
  const accDot = market
    ? `<span class="acc-dot ${market.accepting_orders ? 'acc-dot-ok' : 'acc-dot-off'}"
             title="${market.accepting_orders ? '接单中' : '不接单'}"></span>`
    : '';

  // neg_risk tag (保留, 但降调)
  const negRiskTag = (market && market.neg_risk)
    ? `<span class="neg-risk-tag" title="neg_risk">NR</span>`
    : '';

  return `
    <div class="cond-col${inactiveCls}" data-condition="${condId}">
      <div class="cond-col-pos-anchor">
        ${rejectDot}
      </div>

      <!-- 盘口标题行 -->
      <div class="cond-header">
        <span class="cond-type-label">${marketLabel}</span>
        ${accDot}
        ${negRiskTag}
      </div>

      <!-- 量化决策行 (R4: bg3 背景色块) -->
      <div class="cond-quote-section">
        ${renderCondQuote(quote, isDemoData)}
      </div>

      <!-- 双边迷你订单簿 (R4: bg 最深色块) -->
      <div class="cond-book-section">
        ${renderCondDualBook(book, condId)}
      </div>

      <!-- 持仓行 (R4: bg2 色块) -->
      <div class="cond-pos-section">
        ${renderCondPos(posRows, perMarketPnl)}
      </div>
    </div>`;
}

// ============================================================
// 量化决策行 (简洁版, 适配盘口列宽度)
// ============================================================

function renderCondQuote(quote, isDemoData) {
  const advisory = advisoryBadge();
  const demo = demoBadge(isDemoData);

  if (!quote) {
    return `<div class="cond-quote-empty"><span class="ph">量化未接入</span> ${demo}</div>`;
  }

  const fairValue  = Number(quote.fair_value);
  const marketMid  = Number(quote.market_mid);
  const edgeBps    = Number(quote.edge_bps);
  const kelly      = Number(quote.kelly_fraction);
  const notional   = Number(quote.suggested_notional);

  const edgePositive = fairValue >= marketMid;
  const edgeCls = edgePositive ? 'edge-pos' : 'edge-neg';
  const edgeBarPct = Math.min(Math.abs(edgeBps) / 100, 1) * 100;

  // Kelly: 正数绿色 (R2: Kelly正 → 绿)
  const kellyPositive = Number.isFinite(kelly) && kelly > 0;
  const kellyCls = kellyPositive ? 'kelly-pos' : 'kelly-zero';

  return `
    <div class="cond-quote-row">
      <span class="q-lbl">公允</span>
      <span class="q-fair mono-main">${Number.isFinite(fairValue) ? fairValue.toFixed(4) : '—'}</span>
      <span class="q-lbl">中间</span>
      <span class="q-mid mono-sub">${Number.isFinite(marketMid) ? marketMid.toFixed(4) : '—'}</span>
      ${demo}
    </div>
    <div class="cond-edge-row">
      <span class="q-lbl">优势</span>
      <div class="edge-track-sm">
        <div class="edge-fill-sm ${edgeCls}" style="width:${edgeBarPct.toFixed(1)}%"></div>
      </div>
      <span class="q-edge ${edgeCls}">${fmtBps(edgeBps)}</span>
    </div>
    <div class="cond-kelly-row">
      <span class="q-lbl">Kelly</span>
      <span class="q-kelly ${kellyCls} mono-main">${Number.isFinite(kelly) ? (kelly * 100).toFixed(1) : '—'}%</span>
      <span class="q-lbl">额</span>
      <span class="q-notional mono-sub">$${Number.isFinite(notional) ? notional.toLocaleString() : '—'}</span>
      ${advisory}
    </div>`;
}

// ============================================================
// 双边迷你订单簿 (token0 左 / token1 右, 列内并排)
// ============================================================

function renderCondDualBook(book, conditionId) {
  if (!book) {
    const ep = `/api/v1/book/${conditionId}`;
    if (isEndpointFailing(ep)) {
      return `<div class="cond-book-fail"><span class="fail-chip">订单簿拉取失败</span></div>`;
    }
    return `<div class="cond-book-empty"><span class="ph">订单簿未接入</span></div>`;
  }

  const t0 = book.token0 || {};
  const t1 = book.token1 || {};

  // vig badge (R2: 颜色语义保留 green/yellow/red for vig level)
  let vigHtml = '';
  const cs = Number(book.cross_spread);
  if (Number.isFinite(cs)) {
    const vigCls = cs < 0.02 ? 'vig-low' : cs < 0.04 ? 'vig-mid' : 'vig-high';
    vigHtml = `<span class="vig-badge-sm ${vigCls}" title="vig (ask0+ask1-1)">${(cs * 100).toFixed(2)}%</span>`;
  }

  return `
    <div class="cond-book-header">
      <span class="cond-book-label">双边订单簿</span>
      ${vigHtml}
    </div>
    <div class="cond-dual-grid">
      ${renderMiniHalfBook(t0)}
      ${renderMiniHalfBook(t1)}
    </div>`;
}

/**
 * 单边迷你 book (在盘口列宽度内)
 * R3: 字号三档 — 14px主数值(bid/ask) / 11px次要 / 10px标签
 * R2: wss-ok → 灰点; bid → 绿; ask → 红
 */
function renderMiniHalfBook(half) {
  const outcome  = half.outcome || '—';
  const bid      = Number(half.best_bid);
  const ask      = Number(half.best_ask);
  const spread   = Number(half.spread);
  const imbalance = Number(half.imbalance);
  const seqNo    = half.sequence_no != null ? half.sequence_no : '—';
  const gapCount = half.gap_count != null ? Number(half.gap_count) : 0;
  const wssState = half.wss_state || 'unknown';

  // 深度档 3 档
  const bids = (half.bids || []).slice(0, 3);
  const asks = (half.asks || []).slice(0, 3);
  const maxLen = Math.max(bids.length, asks.length);

  const depthRows = [];
  for (let i = 0; i < maxLen; i++) {
    const b = bids[i];
    const a = asks[i];
    depthRows.push(`
      <div class="mini-depth-row">
        <span class="mini-bid-size">${b ? b.size.toLocaleString() : ''}</span>
        <span class="mini-bid-px">${b ? Number(b.price).toFixed(4) : ''}</span>
        <span class="mini-ask-px">${a ? Number(a.price).toFixed(4) : ''}</span>
        <span class="mini-ask-size">${a ? a.size.toLocaleString() : ''}</span>
      </div>`);
  }

  // R5: gap > 0 → 右上角小红点 (不内联主路径)
  const gapDot = gapCount > 0
    ? `<span class="gap-dot" title="gap ${gapCount}"></span>`
    : '';

  // imbalance bar
  const imbPct = Number.isFinite(imbalance) ? ((imbalance + 1) / 2 * 100).toFixed(1) : '50';

  return `
    <div class="mini-half">
      <div class="mini-half-header">
        <span class="mini-outcome">${outcome}</span>
        ${wssStatusDot(wssState)}
        ${gapDot}
      </div>
      <div class="mini-ba-row">
        <span class="mini-bid mono-main">${Number.isFinite(bid) ? bid.toFixed(4) : '—'}</span>
        <span class="mini-ba-sep">|</span>
        <span class="mini-ask mono-main">${Number.isFinite(ask) ? ask.toFixed(4) : '—'}</span>
      </div>
      <div class="mini-spread-row">
        <span class="q-lbl">价差</span>
        <span class="mono-sub">${Number.isFinite(spread) ? (spread * 100).toFixed(2) + '%' : '—'}</span>
        <div class="mini-imb-track" title="失衡 ${Number.isFinite(imbalance) ? imbalance.toFixed(3) : '—'}">
          <div class="mini-imb-fill" style="width:${imbPct}%"></div>
        </div>
      </div>
      <div class="mini-depth">
        <div class="mini-depth-header">
          <span class="mini-col-lbl">量↑</span>
          <span class="mini-col-lbl">买</span>
          <span class="mini-col-lbl">卖</span>
          <span class="mini-col-lbl">量↑</span>
        </div>
        ${depthRows.join('')}
      </div>
      <div class="mini-seq-row">
        <span class="q-lbl">seq</span>
        <span class="mono-sub">${seqNo}</span>
      </div>
    </div>`;
}

// ============================================================
// 持仓行 (每盘口列内)
// ============================================================

function renderCondPos(posRows, perMarketPnl) {
  const pmPnlStr = perMarketPnl != null
    ? `<span class="${perMarketPnl >= 0 ? 'pnl-pos' : 'pnl-neg'} mono-main">${fmtUsdc(perMarketPnl)}</span>`
    : '<span class="ph">—</span>';

  if (!posRows || posRows.length === 0) {
    return `
      <div class="cond-pos-header">
        <span class="q-lbl">持仓</span>
        <span class="ph">—</span>
        <span class="q-lbl">PnL</span>
        ${pmPnlStr}
      </div>`;
  }

  const rows = posRows.map((p) => {
    const netQty = Number(p.net_qty);
    const avg    = Number(p.avg_entry_price);
    const mark   = Number(p.mark_price);
    const pnlR   = Number(p.pnl_realized);
    const pnlU   = Number(p.pnl_unrealized);
    const pnlTotal = pnlR + pnlU;
    const pnlCls   = pnlTotal >= 0 ? 'pnl-pos' : 'pnl-neg';
    const qtySign  = netQty >= 0 ? '+' : '';

    return `
      <div class="cond-pos-row">
        <span class="cond-pos-outcome">${p.outcome || '—'}</span>
        <span class="cond-pos-qty mono-sub">${qtySign}${netQty.toLocaleString()}u</span>
        <span class="cond-pos-mark mono-sub">${Number.isFinite(mark) ? mark.toFixed(4) : '—'}</span>
        <span class="cond-pos-pnl ${pnlCls} mono-main">${fmtUsdc(pnlTotal)}</span>
      </div>`;
  }).join('');

  return `
    <div class="cond-pos-header">
      <span class="q-lbl">持仓/PnL</span>
      ${pmPnlStr}
    </div>
    ${rows}`;
}

// ============================================================
// R5: 拒单 → 右上角小红点 + tooltip
// ============================================================

function renderRejectDot(rejectRows, book) {
  const hasRejects = rejectRows && rejectRows.length > 0;
  if (!hasRejects) return '';

  const allReasons = rejectRows.map((r) => {
    const rz = REJECT_REASON_ZH[r.reason_code] || r.reason_code;
    const sideZh = SIDE_ZH[r.side] || r.side;
    return `${rz} · ${sideZh} ${r.size} @${r.price}`;
  }).join('\n');

  return `<span class="reject-dot" title="${allReasons}">×${rejectRows.length}</span>`;
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
