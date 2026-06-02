/**
 * TradingPage.tsx — 盯盘页 v8 (折叠/展开 Accordion + Collapse)
 * owner: 小苏  last_review: 2026-05-29
 *
 * v8 变更:
 *  - 市场发现: /api/v1/events (不再依赖 positions)
 *  - Event 分组: 原生 Accordion (CSS 动画, 无 SUID Accordion — 库未收录)
 *  - Market 行: 折叠摘要行 (~40px) + SUID Collapse 展开区
 *  - 展开区三列: 双边订单簿 / 量化AI / 持仓拒单
 *  - 默认: 进行中赛事分组展开 / Market 行全部折叠
 *  - 去 demo 改 LIVE, 持仓空态改为"无持仓/等待 paper runtime"
 *  - 全展开/全折叠快捷按钮
 *  - sessionStorage 保留展开状态
 */

import { createSignal, For, Show, createMemo, createEffect } from 'solid-js';
import { createStore } from 'solid-js/store';
import Chip from '@suid/material/Chip';
import LinearProgress from '@suid/material/LinearProgress';
import Typography from '@suid/material/Typography';
import ToggleButton from '@suid/material/ToggleButton';
import ToggleButtonGroup from '@suid/material/ToggleButtonGroup';
import TextField from '@suid/material/TextField';
import Box from '@suid/material/Box';
import Alert from '@suid/material/Alert';
import Badge from '@suid/material/Badge';
import { state, setDetailInterest, addDetailInterest } from '../store';
import {
  fmtTs, fmtBps, fmtUsdc, fmtClock, stalenessMs, isEndpointFailing,
} from '../api';
import {
  STATUS_ZH, SPORT_ZH, REJECT_REASON_ZH, SIDE_ZH, MARKET_TYPE_ZH, inferMarketLabel, inferMarketTypeZh,
  inferSportFromSlug,
} from '../i18n';
import type {
  EventGroup, ConditionData, BinaryMarketBookView, HalfBook,
  Quote, Position, RiskReject,
} from '../types';
import { PnlSparkline } from './PnlSparkline';
import { StatusDot, wssStateToDot } from './ui/StatusDot';
import { DepthBar } from './ui/DepthBar';

// ============================================================
// 展开状态 store (per conditionId + per eventId)
// ============================================================

// 读写 sessionStorage (避免页面刷新丢失展开状态)
function ssGet(key: string): boolean | null {
  try { const v = sessionStorage.getItem(key); return v === null ? null : v === '1'; } catch { return null; }
}
function ssSet(key: string, val: boolean): void {
  try { sessionStorage.setItem(key, val ? '1' : '0'); } catch { /* ignore */ }
}

// Market 行展开状态
const [expandedMarkets, setExpandedMarkets] = createStore<Record<string, boolean>>({});
// Event 分组展开状态
const [expandedEvents, setExpandedEvents] = createStore<Record<string, boolean>>({});

function isMarketExpanded(condId: string): boolean {
  if (condId in expandedMarkets) return expandedMarkets[condId];
  const ss = ssGet(`stcpp_mkt_exp_${condId}`);
  return ss ?? false;
}

function toggleMarket(condId: string): void {
  const next = !isMarketExpanded(condId);
  setExpandedMarkets(condId, next);
  ssSet(`stcpp_mkt_exp_${condId}`, next);
  // 展开即按需拉一次 detail (不等下一轮 5s 轮询), 折叠不主动拉
  if (next) addDetailInterest(condId);
}

function isEventExpanded(eventId: string, isLive: boolean): boolean {
  if (eventId in expandedEvents) return expandedEvents[eventId];
  const ss = ssGet(`stcpp_evt_exp_${eventId}`);
  if (ss !== null) return ss;
  return isLive; // 进行中赛事默认展开分组
}

function toggleEvent(eventId: string, isLive: boolean): void {
  const next = !isEventExpanded(eventId, isLive);
  setExpandedEvents(eventId, next);
  ssSet(`stcpp_evt_exp_${eventId}`, next);
}

function expandAllMarkets(condIds: string[]): void {
  for (const c of condIds) {
    setExpandedMarkets(c, true);
    ssSet(`stcpp_mkt_exp_${c}`, true);
  }
}

function collapseAllMarkets(condIds: string[]): void {
  for (const c of condIds) {
    setExpandedMarkets(c, false);
    ssSet(`stcpp_mkt_exp_${c}`, false);
  }
}

// ============================================================
// 盘口类型 Chip 颜色映射 (胜负蓝/让分橙/大小紫)
// ============================================================

function marketTypeColor(condId: string): 'primary' | 'warning' | 'secondary' | 'default' {
  if (condId.includes('-ml') || condId.includes('-moneyline')) return 'primary';
  if (condId.includes('-spread')) return 'warning';
  if (condId.includes('-total')) return 'secondary';
  return 'default';
}

function marketTypeShort(condId: string): string {
  if (condId.includes('-ml') || condId.includes('-moneyline')) return '胜负';
  if (condId.includes('-spread')) return '让分';
  if (condId.includes('-total')) return '大小';
  // fallback: last segment
  const parts = condId.split('-');
  const last = parts[parts.length - 1];
  return MARKET_TYPE_ZH[last] ?? last.toUpperCase().slice(0, 4);
}

// ============================================================
// 延迟三色辅助
// ============================================================

function stalenessColor(ms: number | null): 'success' | 'warning' | 'error' | 'default' {
  if (ms == null) return 'default';
  if (ms < 100) return 'success';
  if (ms < 1000) return 'warning';
  return 'error';
}

function stalenessText(ms: number | null): string {
  if (ms == null) return '—';
  if (ms < 1000) return `${Math.round(ms)}ms`;
  return `${(ms / 1000).toFixed(1)}s`;
}

function stalenessClass(ms: number | null): string {
  if (ms == null) return 'stale-none';
  if (ms < 100) return 'stale-ok';
  if (ms < 1000) return 'stale-warn';
  return 'stale-err';
}

// ============================================================
// Market 摘要行 (折叠态, ~40px)
// ============================================================

function MarketSummaryRow(props: { cond: ConditionData; expanded: boolean; onClick: () => void }) {
  const c = () => props.cond;
  const condId = () => c().conditionId;
  const book = () => c().book;
  const quote = () => c().quote;
  const summary = () => c().summary;  // /grid 顶档摘要 (折叠态全市场 2s 批量供)
  const posRows = () => c().posRows;

  const fin = (v: number | null | undefined): number | null =>
    (v != null && Number.isFinite(Number(v))) ? Number(v) : null;

  // 最优买卖: 优先 /grid 摘要 (全市场都有), 降级展开拉来的全档 book.token0
  const bestBid = () => fin(summary()?.bid ?? book()?.token0?.best_bid);
  const bestAsk = () => fin(summary()?.ask ?? book()?.token0?.best_ask);

  // edge: 优先 /grid 摘要, 降级全档 quote
  const edgeBps = () => fin(summary()?.edgeBps ?? quote()?.edge_bps);

  // 持仓摘要
  const posText = () => {
    if (posRows().length === 0) return null;
    const p = posRows()[0];
    const qty = Math.abs(Number(p.net_qty));
    return `${p.outcome} ${(qty / 1000).toFixed(1)}k`;
  };

  // 浮盈
  const pnlTotal = () => {
    if (posRows().length === 0) return null;
    const v = posRows().reduce((acc, p) => acc + Number(p.pnl_realized) + Number(p.pnl_unrealized), 0);
    return v;
  };
  const pnlFmt = () => {
    const v = pnlTotal();
    if (v == null) return null;
    if (v >= 0) return `+$${v.toFixed(1)}`;
    return `($${Math.abs(v).toFixed(1)})`;
  };

  // 拒单数
  const rejectCount = () => c().rejectRows.length;

  // 延迟 — 使用真实数据时刻 event_ts / ingestion_ts (P1-7: 避免 book_as_of_ts 恒新假阳性)
  const staleMs = () => {
    const b = book();
    // 优先取 event_ts (最接近数据源时刻); 降级 ingestion_ts; 最后才 book_as_of_ts
    const ts = b ? (b.event_ts ?? b.token0?.event_ts ?? b.ingestion_ts ?? b.token0?.ingestion_ts) : summary()?.eventTs;
    return stalenessMs(ts);
  };

  // 市场名称 (含线值)
  const mktLabel = () => inferMarketLabel(condId(), c().market);

  // 展开图标
  const arrow = () => props.expanded ? '▼' : '▶';

  return (
    <div
      class={`v8-market-row${props.expanded ? ' v8-market-row-open' : ''}`}
      onClick={props.onClick}
      role="button"
      tabIndex={0}
      onKeyDown={(e) => { if (e.key === 'Enter' || e.key === ' ') props.onClick(); }}
      title={`点击${props.expanded ? '折叠' : '展开'}详情`}
    >
      {/* 展开箭头 */}
      <span class="v8-row-arrow">{arrow()}</span>

      {/* 盘口类型 Chip */}
      <Chip
        label={marketTypeShort(condId())}
        color={marketTypeColor(condId())}
        size="small"
        sx={{ fontSize: '10px', height: '20px', fontWeight: 700, minWidth: '40px', flexShrink: 0 }}
      />

      {/* 市场名 */}
      <span class="v8-row-name">{mktLabel()}</span>

      {/* 最优买 */}
      <span class={`v8-row-price v8-bid${bestBid() == null ? ' v8-dim' : ''}`}>
        {bestBid() != null ? bestBid()!.toFixed(3) : '—'}
      </span>

      {/* 最优卖 */}
      <span class={`v8-row-price v8-ask${bestAsk() == null ? ' v8-dim' : ''}`}>
        {bestAsk() != null ? bestAsk()!.toFixed(3) : '—'}
      </span>

      {/* Edge */}
      <span class={`v8-row-edge${edgeBps() == null ? ' v8-dim' : edgeBps()! > 0 ? ' v8-edge-pos' : edgeBps()! < 0 ? ' v8-edge-neg' : ' v8-dim'}`}>
        {edgeBps() != null ? fmtBps(edgeBps()!) : '—'}
      </span>

      {/* 持仓 */}
      <span class={`v8-row-pos${posText() ? '' : ' v8-dim'}`}>
        {posText() ?? '无持仓'}
      </span>

      {/* 浮盈 */}
      <span class={`v8-row-pnl${pnlFmt() == null ? ' v8-dim' : pnlTotal()! >= 0 ? ' v8-pnl-pos' : ' v8-pnl-neg'}`}>
        {pnlFmt() ?? '—'}
      </span>

      {/* 拒单 Badge */}
      <span class="v8-row-reject">
        <Show when={rejectCount() > 0}>
          <span class="v8-reject-badge" title={c().rejectRows.map((r) => REJECT_REASON_ZH[r.reason_code] ?? r.reason_code).join(', ')}>
            ×{rejectCount()}
          </span>
        </Show>
        <Show when={rejectCount() === 0}>
          <span class="v8-dim">×0</span>
        </Show>
      </span>

      {/* 延迟 */}
      <span class={`v8-row-stale ${stalenessClass(staleMs())}`}>
        {stalenessText(staleMs())}
      </span>
    </div>
  );
}

// ============================================================
// 展开区子块 A: 双边订单簿
// ============================================================

function ExpandBookPanel(props: { book: BinaryMarketBookView | null; conditionId: string }) {
  const book = () => props.book;
  if (!book()) {
    return (
      <div class="v8-expand-panel">
        <div class="v8-panel-title">双边订单簿</div>
        <Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>
          {isEndpointFailing(`/api/v1/book_pair/${props.conditionId}`) ? '订单簿拉取失败' : '订单簿未接入'}
        </Typography>
      </div>
    );
  }

  const bk = () => book()!;
  const vigInfo = () => {
    const cs = Number(bk().cross_spread);
    if (!Number.isFinite(cs)) return null;
    const color: 'success' | 'warning' | 'error' = cs < 0.02 ? 'success' : cs < 0.04 ? 'warning' : 'error';
    return { text: `${(cs * 100).toFixed(2)}%`, color };
  };

  function HalfPane(p: { half: HalfBook; label: string }) {
    const h = () => p.half;
    const bids = () => (h().bids ?? []).slice(0, 5);
    const asks = () => (h().asks ?? []).slice(0, 5);
    const maxLen = () => Math.max(bids().length, asks().length);
    const depthIdx = () => Array.from({ length: maxLen() }, (_, i) => i);
    const maxSize = () => Math.max(...bids().map((b) => Number(b.size)), ...asks().map((a) => Number(a.size)), 1);
    const imbalance = () => Number(h().imbalance);
    const imbPct = () => Number.isFinite(imbalance()) ? `${((imbalance() + 1) / 2 * 100).toFixed(0)}%` : '50%';

    return (
      <div class="v8-half-pane">
        <div class="v8-half-label">
          <Chip label={h().outcome ?? p.label} size="small" variant="outlined"
            sx={{ fontSize: '9px', height: '16px', fontWeight: 700 }} />
          <StatusDot state={wssStateToDot(h().wss_state ?? 'unknown')} size="sm" title={`WSS: ${h().wss_state}`} />
        </div>
        <div class="v8-half-ba">
          <span class="v8-bid mono-strong">{Number.isFinite(Number(h().best_bid)) ? Number(h().best_bid).toFixed(4) : '—'}</span>
          <span class="v8-ba-sep">|</span>
          <span class="v8-ask mono-strong">{Number.isFinite(Number(h().best_ask)) ? Number(h().best_ask).toFixed(4) : '—'}</span>
        </div>
        <div class="v8-half-imb">
          <span class="q-lbl">失衡</span>
          <div class="mini-imb-track">
            <div class="mini-imb-fill" style={{ width: imbPct() }} />
          </div>
          <span class="mono-sub">{Number.isFinite(imbalance()) ? imbalance().toFixed(2) : '—'}</span>
        </div>
        <div class="v8-depth-header">
          <span class="v8-depth-col-bid">量/买</span>
          <span class="v8-depth-col-ask">卖/量</span>
        </div>
        <For each={depthIdx()}>
          {(i) => {
            const b = () => bids()[i];
            const a = () => asks()[i];
            return (
              <div class="v8-depth-row">
                <div class="v8-depth-bid-side">
                  <Show when={b()}>
                    <>
                      <span class="v8-depth-size mono-sub">{Number(b().size).toLocaleString()}</span>
                      <span class="v8-bid mono-sub">{Number(b().price).toFixed(4)}</span>
                      <DepthBar size={Number(b().size)} maxSize={maxSize()} side="bid" />
                    </>
                  </Show>
                </div>
                <span class="v8-depth-sep">|</span>
                <div class="v8-depth-ask-side">
                  <Show when={a()}>
                    <>
                      <DepthBar size={Number(a().size)} maxSize={maxSize()} side="ask" />
                      <span class="v8-ask mono-sub">{Number(a().price).toFixed(4)}</span>
                      <span class="v8-depth-size mono-sub">{Number(a().size).toLocaleString()}</span>
                    </>
                  </Show>
                </div>
              </div>
            );
          }}
        </For>
        <div class="v8-half-seq">
          <span class="q-lbl">seq</span>
          <span class="mono-sub">{h().sequence_no ?? '—'}</span>
          <Show when={(h().gap_count ?? 0) > 0}>
            <Chip label={`gap:${h().gap_count}`} color="error" size="small" sx={{ fontSize: '9px', height: '14px' }} />
          </Show>
        </div>
      </div>
    );
  }

  return (
    <div class="v8-expand-panel">
      <div class="v8-panel-title">
        双边订单簿
        <Show when={vigInfo()}>
          {(vi) => (
            <Chip
              label={`vig ${vi().text}`}
              color={vi().color}
              size="small"
              variant="outlined"
              sx={{ fontSize: '9px', height: '16px', ml: 0.5 }}
            />
          )}
        </Show>
      </div>
      <div class="v8-dual-pane">
        <HalfPane half={bk().token0} label="YES" />
        <HalfPane half={bk().token1} label="NO" />
      </div>
    </div>
  );
}

// ============================================================
// 展开区子块 B: 量化 / AI (XD-1/3/4/5 红线保持)
// ============================================================

function ExpandQuotePanel(props: { quote: Quote | null }) {
  if (!props.quote) {
    return (
      <div class="v8-expand-panel">
        <div class="v8-panel-title">量化 / AI</div>
        <Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>量化未接入</Typography>
      </div>
    );
  }

  const q = () => props.quote!;
  const fairValue  = () => Number(q().fair_value);
  const marketMid  = () => Number(q().market_mid);
  const edgeBps    = () => Number(q().edge_bps);
  const kelly      = () => Number(q().kelly_fraction);
  const notional   = () => Number(q().suggested_notional);
  const signalStr  = () => Number(q().signal_strength);
  const modelConf  = () => Number(q().model_confidence ?? q().model_conf);
  const modelId    = () => q().model_id ?? '—';
  const calibrated = () => q().model_calibrated !== false;
  const predictOk  = () => q().predict_ok !== false;
  const advisory   = () => q().advisory === true;
  const ciLower    = () => q().fair_ci_lower;
  const ciUpper    = () => q().fair_ci_upper;
  const hasCi      = () => Number.isFinite(ciLower()) && Number.isFinite(ciUpper());
  const confPct    = () => Number.isFinite(modelConf()) ? modelConf() * 100 : 0;
  const confColor  = (): 'success' | 'warning' | 'error' | 'inherit' =>
    modelConf() >= 0.7 ? 'success' : modelConf() >= 0.4 ? 'warning' : 'error';
  const edgePos    = () => fairValue() >= marketMid();
  const edgePct    = () => Math.min(Math.abs(edgeBps()) / 100, 1) * 100;
  const kellyPos   = () => Number.isFinite(kelly()) && kelly() > 0;

  return (
    <div class="v8-expand-panel">
      <div class="v8-panel-title">
        量化 / AI
        {/* XD-3: ADVISORY 角标强制显示 (paper 期) */}
        <Show when={advisory()}>
          <span class="v8-advisory-badge">ADVISORY</span>
        </Show>
      </div>

      {/* Fair / 市场中间价 */}
      <div class="v8-q-row">
        <span class="q-lbl">公允</span>
        <span class={`mono-strong${!calibrated() ? ' v8-dim' : ''}`} style={{ 'font-size': '15px' }}>
          {Number.isFinite(fairValue()) ? fairValue().toFixed(4) : '—'}
        </span>
        <Show when={!calibrated()}>
          <span class="uncalib-chip">未校准</span>
        </Show>
        <Show when={hasCi()}>
          <span class="mono-sub">[{ciLower()!.toFixed(3)}–{ciUpper()!.toFixed(3)}]</span>
        </Show>
      </div>

      <div class="v8-q-row">
        <span class="q-lbl">市场</span>
        <span class="mono-sub">{Number.isFinite(marketMid()) ? marketMid().toFixed(4) : '—'}</span>
      </div>

      {/* 置信度 */}
      <div class="v8-q-row">
        <span class="q-lbl">置信</span>
        <Box sx={{ flex: 1, minWidth: '40px' }}>
          <LinearProgress variant="determinate" value={confPct()} color={confColor()} sx={{ height: 5, borderRadius: 2 }} />
        </Box>
        <Typography sx={{ fontFamily: 'monospace', fontSize: '10px', ml: 0.5,
          color: confColor() === 'success' ? '#4caf50' : confColor() === 'warning' ? '#ff9800' : '#f44336' }}>
          {Number.isFinite(modelConf()) ? `${(modelConf() * 100).toFixed(0)}%` : '—'}
        </Typography>
        <Show when={calibrated()}>
          <span class="mono-sub" style={{ 'color': '#4caf50' }}>已校准</span>
        </Show>
      </div>

      {/* XD-5: predict_ok=false */}
      <Show
        when={predictOk()}
        fallback={
          <Alert severity="error" sx={{ py: 0.25, px: 1, fontSize: '10px', mt: 0.5 }}>
            预测异常 · edge/kelly/额度暂不可用
          </Alert>
        }
      >
        <div class="v8-q-row">
          <span class="q-lbl">优势</span>
          <Box sx={{ flex: 1, minWidth: '24px' }}>
            <LinearProgress variant="determinate" value={edgePct()} color={edgePos() ? 'success' : 'error'} sx={{ height: 4, borderRadius: 2 }} />
          </Box>
          <span class={`mono-sub${edgePos() ? ' edge-pos' : ' edge-neg'}`} style={{ 'font-weight': '700' }}>
            {fmtBps(edgeBps())}
          </span>
        </div>

        <div class="v8-q-row">
          <span class="q-lbl">Kelly</span>
          <span class={`mono-strong${kellyPos() ? ' kelly-pos' : ' kelly-zero'}`}>
            {Number.isFinite(kelly()) ? `${(kelly() * 100).toFixed(1)}%` : '—'}
          </span>
          <span class="q-lbl">额</span>
          <span class="mono-sub">
            ${Number.isFinite(notional()) ? notional().toLocaleString('en-US', { maximumFractionDigits: 0 }) : '—'}
          </span>
          <span class="v8-advisory-inline">[advisory]</span>
        </div>

        <div class="v8-q-row">
          <span class="q-lbl">信号α</span>
          <span class="mono-sub">{Number.isFinite(signalStr()) ? signalStr().toFixed(2) : '—'}</span>
        </div>
      </Show>

      {/* model provenance */}
      <div class="v8-q-row v8-model-row">
        <span class="q-lbl">model</span>
        <span class="mono-sub" title={`${q().model_id} · ${q().model_kind} · ${q().spec_version}`}>
          {modelId()}
        </span>
      </div>
    </div>
  );
}

// ============================================================
// 展开区子块 C: 持仓 + 拒单
// ============================================================

function ExpandPosPanel(props: { posRows: Position[]; rejectRows: RiskReject[]; perMarketPnl: number | null }) {
  const pnlStr = () => {
    const v = props.perMarketPnl;
    if (v == null) return null;
    return { text: fmtUsdc(v), pos: v >= 0 };
  };

  return (
    <div class="v8-expand-panel">
      <div class="v8-panel-title">持仓 + 拒单</div>

      {/* 持仓 */}
      <Show
        when={props.posRows.length > 0}
        fallback={
          <div class="v8-pos-empty">
            <Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>
              暂无 paper 成交（策略未触发 edge）
            </Typography>
          </div>
        }
      >
        <For each={props.posRows}>
          {(p) => {
            const netQty  = () => Number(p.net_qty);
            const mark    = () => Number(p.mark_price);
            const pnlTot  = () => Number(p.pnl_realized) + Number(p.pnl_unrealized);
            const pos     = () => pnlTot() >= 0;
            return (
              <div class="v8-pos-row">
                <Chip label={p.outcome ?? '—'} size="small" variant="outlined"
                  sx={{ fontSize: '9px', height: '16px', fontWeight: 700 }} />
                <span class="mono-sub">{netQty() >= 0 ? '+' : ''}{netQty().toLocaleString()}u</span>
                <span class="mono-sub">@{Number.isFinite(mark()) ? mark().toFixed(4) : '—'}</span>
                <span class={`mono-sub ${pos() ? 'pnl-pos' : 'pnl-neg'}`} style={{ 'margin-left': 'auto' }}>
                  {fmtUsdc(pnlTot())}
                </span>
              </div>
            );
          }}
        </For>
        <Show when={pnlStr()}>
          {(ps) => (
            <div class="v8-pos-total">
              <span class="q-lbl">合计</span>
              <span class={`mono-strong ${ps().pos ? 'pnl-pos' : 'pnl-neg'}`}>{ps().text}</span>
            </div>
          )}
        </Show>
      </Show>

      {/* 拒单明细 */}
      <div class="v8-reject-title">拒单 (最近 {Math.min(props.rejectRows.length, 5)} 条)</div>
      <Show
        when={props.rejectRows.length > 0}
        fallback={<Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>—</Typography>}
      >
        <For each={props.rejectRows.slice(0, 5)}>
          {(r) => (
            <div class="v8-reject-row">
              <span class={`v8-reject-reason${r.reason_code === 'MARKET_NOT_ACCEPTING_ORDERS' ? ' v8-reject-stale' : ''}`}>
                {REJECT_REASON_ZH[r.reason_code] ?? r.reason_code}
              </span>
              <span class="mono-sub">{SIDE_ZH[r.side] ?? r.side}</span>
              <span class="mono-sub">{r.size}@{r.price}</span>
            </div>
          )}
        </For>
      </Show>
    </div>
  );
}

// ============================================================
// Market 展开区 (三列横排)
// ============================================================

function MarketExpandArea(props: { cond: ConditionData }) {
  const c = () => props.cond;
  return (
    <div class="v8-expand-area">
      <ExpandBookPanel book={c().book} conditionId={c().conditionId} />
      <ExpandQuotePanel quote={c().quote} />
      <ExpandPosPanel posRows={c().posRows} rejectRows={c().rejectRows} perMarketPnl={c().perMarketPnl} />
    </div>
  );
}

// ============================================================
// Event 分组 (Accordion 风格, 原生 CSS)
// ============================================================

function EventAccordion(props: { group: EventGroup }) {
  const grp = () => props.group;
  const score = () => grp().score;
  const eventId = () => grp().eventId ?? '__no_event__';

  const isLive = () => score()?.status === 'inplay' || score()?.status === 'halftime';

  const expanded = () => isEventExpanded(eventId(), isLive());

  // 赛事头显示文本
  const sportZh = () => {
    const sp = grp().sport ?? score()?.sport;
    if (sp) return SPORT_ZH[sp] ?? sp;
    // sport 字段为空 (如 outright/futures) → 从 slug / neg_risk_market_id 推断
    return inferSportFromSlug(grp().eventSlug ?? grp().eventTitle);
  };

  // P1-5: 无 score 且 title 不含 ' vs ' 时直接用 title 原文，不强拆队名
  const hasVs = () => (grp().eventTitle ?? '').includes(' vs ');
  const isOutright = () => !score() && !hasVs();

  const homeTeam = () => {
    if (score()?.home) return score()!.home;
    if (isOutright()) return null; // outright: 不显示主队列
    return grp().eventTitle?.split(' vs ')[0] ?? null;
  };
  const awayTeam = () => {
    if (score()?.away) return score()!.away;
    if (isOutright()) return null; // outright: 不显示客队列
    const parts = grp().eventTitle?.split(' vs ');
    return (parts && parts.length >= 2) ? parts[1] : null;
  };
  const homeScore = () => score()?.home_score;
  const awayScore = () => score()?.away_score;
  const statusZh  = () => STATUS_ZH[score()?.status ?? ''] ?? score()?.status ?? '?';
  const subheader = () => {
    const sc = score();
    // outright 无 score: 不把 title 重复放进 subheader (P1-5 — 已在主标题行显示完整 title)
    if (!sc) return '';
    const parts: string[] = [];
    if (sc.period) parts.push(sc.period);
    // #2 修: 后端 clock_sec 用 0 表示"无计时"(不输出 null), 故 >0 才显示, 否则隐藏避免误显 "0:00"。
    if (sc.clock_sec != null && sc.clock_sec > 0) parts.push(fmtClock(sc.clock_sec));
    return parts.join(' · ');
  };

  // 当前 event 下的最大延迟 — 使用真实数据时刻 event_ts / ingestion_ts (P1-7)
  const maxStaleMs = () => {
    const vals = grp().conditions
      .map((c) => {
        const b = c.book;
        if (!b) return null;
        const ts = b.event_ts ?? b.token0?.event_ts ?? b.ingestion_ts ?? b.token0?.ingestion_ts;
        return stalenessMs(ts);
      })
      .filter((v): v is number => v != null);
    if (vals.length === 0) return null;
    return Math.max(...vals);
  };

  return (
    <div class="v8-event-accordion" data-event={eventId()}>
      {/* 赛事头 (点击折叠/展开整组) */}
      <div
        class={`v8-event-header${expanded() ? ' v8-event-header-open' : ''}`}
        onClick={() => toggleEvent(eventId(), isLive())}
        role="button"
        tabIndex={0}
        onKeyDown={(e) => { if (e.key === 'Enter' || e.key === ' ') toggleEvent(eventId(), isLive()); }}
      >
        <span class="v8-evt-arrow">{expanded() ? '▼' : '▶'}</span>

        {/* 运动图标/标签 */}
        <Show when={sportZh()}>
          <Chip label={sportZh()} size="small" variant="outlined"
            sx={{ fontSize: '9px', height: '18px', color: 'text.secondary', mr: 0.5 }} />
        </Show>

        {/* 进行中闪烁标识 */}
        <Show when={isLive()}>
          <span class="live-badge">LIVE</span>
        </Show>

        {/* 队伍 + 比分 — outright 无 ' vs ' 时直接用 title 原文 (P1-5) */}
        <Show
          when={!isOutright()}
          fallback={
            <span class="v8-evt-outright-title" title={grp().eventTitle ?? ''}>
              {grp().eventTitle ?? '—'}
            </span>
          }
        >
          <span class="v8-evt-team">{homeTeam() ?? '—'}</span>
          <Show when={homeScore() != null}>
            <span class="v8-evt-score">{homeScore()}</span>
            <span class="v8-evt-dash">—</span>
            <span class="v8-evt-score">{awayScore()}</span>
          </Show>
          <span class="v8-evt-team">{awayTeam() ?? '—'}</span>
        </Show>

        {/* 状态 Chip */}
        <Show when={score()}>
          <Chip
            label={statusZh()}
            color={isLive() ? 'success' : 'default'}
            size="small"
            variant="outlined"
            sx={{ fontSize: '9px', height: '18px' }}
          />
        </Show>

        {/* 子标题: 节次/时钟 */}
        <Show when={subheader()}>
          <Typography variant="caption" sx={{ color: 'text.secondary', fontSize: '10px', fontFamily: 'monospace' }}>
            {subheader()}
          </Typography>
        </Show>

        {/* 延迟 */}
        <Show when={maxStaleMs() != null}>
          <span class={`v8-evt-stale ${stalenessClass(maxStaleMs())}`}>
            {stalenessText(maxStaleMs())}
          </span>
        </Show>

        {/* Market 数 */}
        <span class="v8-evt-mkt-count">{grp().conditions.length}个盘口</span>
      </div>

      {/* 分组内容 (展开/折叠) */}
      <div class={`v8-event-body${expanded() ? ' v8-event-body-open' : ''}`}>
        {/* 摘要行表格头 */}
        <div class="v8-market-list-header">
          <span class="v8-col-arrow" />
          <span class="v8-col-type">类型</span>
          <span class="v8-col-name">盘口</span>
          <span class="v8-col-price">买价</span>
          <span class="v8-col-price">卖价</span>
          <span class="v8-col-edge">Edge</span>
          <span class="v8-col-pos">持仓</span>
          <span class="v8-col-pnl">浮盈</span>
          <span class="v8-col-rej">拒单</span>
          <span class="v8-col-stale">延迟</span>
        </div>

        <For each={grp().conditions}>
          {(cond) => {
            const condId = cond.conditionId;
            const isExpanded = () => isMarketExpanded(condId);
            return (
              <>
                <MarketSummaryRow
                  cond={cond}
                  expanded={isExpanded()}
                  onClick={() => toggleMarket(condId)}
                />
                {/* 展开区 — 用 CSS height 动画 (SUID Collapse 未包含在库中) */}
                <div class={`v8-market-collapse${isExpanded() ? ' v8-market-collapse-open' : ''}`}>
                  <Show when={isExpanded()}>
                    <MarketExpandArea cond={cond} />
                  </Show>
                </div>
              </>
            );
          }}
        </For>
      </div>
    </div>
  );
}

// ============================================================
// TradingToolbar
// ============================================================

type FilterMode = 'all' | 'live' | 'position';

function TradingToolbar(props: {
  filter: FilterMode;
  search: string;
  onFilter: (f: FilterMode) => void;
  onSearch: (s: string) => void;
  visibleCount: number;
  totalCount: number;
  totalMarkets: number;
  onExpandAll: () => void;
  onCollapseAll: () => void;
}) {
  return (
    <div class="trading-toolbar">
      <ToggleButtonGroup
        value={props.filter}
        exclusive
        size="small"
        onChange={(_e: unknown, val: unknown) => { if (val) props.onFilter(val as FilterMode); }}
        sx={{ height: '32px' }}
      >
        <ToggleButton value="all" sx={{ fontSize: '12px', px: 1.5 }}>全部</ToggleButton>
        <ToggleButton value="live" sx={{ fontSize: '12px', px: 1.5 }}>进行中</ToggleButton>
        <ToggleButton value="position" sx={{ fontSize: '12px', px: 1.5 }}>有持仓</ToggleButton>
      </ToggleButtonGroup>

      <TextField
        size="small"
        variant="outlined"
        placeholder="搜索赛事..."
        value={props.search}
        onInput={(e) => props.onSearch((e.currentTarget as HTMLInputElement).value)}
        sx={{
          width: 200,
          '& input': { fontFamily: 'monospace', fontSize: '12px', py: '5px' },
          '& .MuiOutlinedInput-root': { height: '32px' },
        }}
      />

      <Typography variant="caption" class="toolbar-count">
        {props.visibleCount} 赛事 / {props.totalMarkets} 盘口
      </Typography>

      {/* 全展开/全折叠快捷按钮 */}
      <div class="v8-toolbar-btns">
        <button class="v8-quickbtn" onClick={props.onExpandAll} title="全部展开">全展</button>
        <button class="v8-quickbtn" onClick={props.onCollapseAll} title="全部折叠">全折</button>
      </div>
    </div>
  );
}

// ============================================================
// TradingPage (顶层导出)
// ============================================================

export function TradingPage() {
  // 默认只显示「正在比赛」(gamma live=true); 可切「全部/持仓」(老板 2026-06-01)
  const [filter, setFilter] = createSignal<FilterMode>('live');
  const [search, setSearch] = createSignal('');

  // P1-6: WSS 连接状态 Alert 计算.
  //   只有 clob 是真用的 WSS (订单簿). sports_api 走 HTTP REST inplay feed (非 WSS),
  //   user_channel 仅 live 真单订阅 (paper 不用) — 这俩 false 是预期, 不该告警 (老板 2026-06-01)。
  const wssStatus = () => state.status?.wss_connected ?? null;
  const wssAllDown = () => {
    const w = wssStatus();
    if (!w) return false; // 后端未连接时 StatusBar 已有"后端离线"提示，不重复
    return !w.clob; // 仅 clob 断 = 真订单簿断连 (报警有意义)
  };
  const wssPartialDown = () => {
    const w = wssStatus();
    if (!w) return false;
    const vals = [w.clob]; // 仅 clob 计入 (sports_api/user_channel 未用, 不误报)
    const downCount = vals.filter((v) => !v).length;
    return downCount > 0 && downCount < 1; // clob 单通道无"部分断"概念 → 恒 false
  };

  // P1 空态: 是否有成交 (判断显示 PnL 语境)
  const hasFills = () => {
    const attr = state.attribution;
    return (attr?.per_market?.length ?? 0) > 0;
  };

  const allGroups = () => state.eventGroups;

  const filteredGroups = () => {
    let groups = allGroups();
    const f = filter();
    if (f === 'live') {
      // gamma live=true (正在比赛) 优先; 兼容 Goalserve 比分 inplay/halftime
      groups = groups.filter((g) => g.live || g.score?.status === 'inplay' || g.score?.status === 'halftime');
    } else if (f === 'position') {
      groups = groups.filter((g) => g.conditions.some((c) => c.posRows.length > 0));
    }
    const q = search().trim().toLowerCase();
    if (q) {
      groups = groups.filter((g) => {
        const sc = g.score;
        if (sc) {
          if (sc.home?.toLowerCase().includes(q)) return true;
          if (sc.away?.toLowerCase().includes(q)) return true;
        }
        if (g.eventId?.toLowerCase().includes(q)) return true;
        if (g.eventTitle?.toLowerCase().includes(q)) return true;
        if (g.sport?.toLowerCase().includes(q)) return true;
        return g.conditions.some((c) =>
          c.market?.slug?.toLowerCase().includes(q) || c.conditionId?.toLowerCase().includes(q),
        );
      });
    }
    return groups;
  };

  const totalMarkets = () => filteredGroups().reduce((acc, g) => acc + g.conditions.length, 0);

  const allCondIds = () => filteredGroups().flatMap((g) => g.conditions.map((c) => c.conditionId));

  // 关注集 = 当前可见且展开的盘口行。展开行变化时同步给 store, 让 5s 轮询只刷新这些盘口。
  // (读 expandedMarkets store 实现响应式; 切换展开 → 重算 → 轮询只拉展开行的 detail)
  createEffect(() => {
    const expanded = allCondIds().filter((cid) => isMarketExpanded(cid));
    setDetailInterest(expanded);
  });

  return (
    <div>
      {/* P1-6: WSS 全断全局 Alert — 数据可能已过期 */}
      <Show when={wssAllDown()}>
        <Alert
          severity="error"
          sx={{ borderRadius: 0, py: 0.5, px: 2, fontSize: '13px', fontWeight: 600 }}
        >
          WSS 全部断连 · 订单簿数据可能已过期 · 请检查网络或 /status
        </Alert>
      </Show>
      <Show when={wssPartialDown()}>
        <Alert
          severity="warning"
          sx={{ borderRadius: 0, py: 0.5, px: 2, fontSize: '13px' }}
        >
          WSS 部分断连 · 部分市场数据可能已过期
        </Alert>
      </Show>

      <TradingToolbar
        filter={filter()}
        search={search()}
        onFilter={setFilter}
        onSearch={setSearch}
        visibleCount={filteredGroups().length}
        totalCount={allGroups().length}
        totalMarkets={totalMarkets()}
        onExpandAll={() => expandAllMarkets(allCondIds())}
        onCollapseAll={() => collapseAllMarkets(allCondIds())}
      />

      {/* PnL 净值曲线 — 无成交时明示语境 (空态语境) */}
      <div class="spark-section">
        <PnlSparkline noFills={!hasFills()} />
      </div>

      {/* 赛事列表 (v8 Accordion) */}
      <div class="v8-event-list">
        <Show
          when={filteredGroups().length > 0}
          fallback={
            <Typography
              variant="body2"
              sx={{ p: 5, textAlign: 'center', color: 'text.disabled', fontStyle: 'italic' }}
            >
              {state.eventGroups.length === 0 && allGroups().length === 0
                ? '加载赛事数据... (从 /api/v1/events 发现)'
                : filter() !== 'all'
                  ? '该筛选条件下无赛事'
                  : '等待后端连接 · 点 ⚙ 检查 API 地址'}
            </Typography>
          }
        >
          <For each={filteredGroups()}>
            {(group) => <EventAccordion group={group} />}
          </For>
        </Show>
      </div>
    </div>
  );
}
