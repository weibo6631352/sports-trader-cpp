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

import { createSignal, For, Show, createMemo, createEffect, onCleanup } from 'solid-js';
import { createStore, produce } from 'solid-js/store';
import Chip from '@suid/material/Chip';
import LinearProgress from '@suid/material/LinearProgress';
import Typography from '@suid/material/Typography';
import ToggleButton from '@suid/material/ToggleButton';
import ToggleButtonGroup from '@suid/material/ToggleButtonGroup';
import TextField from '@suid/material/TextField';
import Box from '@suid/material/Box';
import Alert from '@suid/material/Alert';
import Badge from '@suid/material/Badge';
import { state, setDetailInterest, addDetailInterest, uiNow, posSeenAt, getSharpTrend, sseAgeMs, sseIsAlive } from '../store';
import {
  fmtTs, fmtBps, fmtUsdc, fmtClock, stalenessMs, isEndpointFailing,
} from '../api';
import {
  STATUS_ZH, GAMESTATE_ZH, SPORT_ZH, REJECT_REASON_ZH, SIDE_ZH, MARKET_TYPE_ZH, inferMarketLabel,
  inferMarketTypeZh, inferSportFromSlug, sportIcon,
} from '../i18n';
import type {
  EventGroup, ConditionData, BinaryMarketBookView, HalfBook,
  Quote, Position, RiskReject, Score, Fill,
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
  // 批量: 一次 produce 更新 store (替代逐个 setExpandedMarkets, 防 N 次更新/重渲染)。
  setExpandedMarkets(produce((m) => { for (const c of condIds) m[c] = true; }));
  for (const c of condIds) ssSet(`stcpp_mkt_exp_${c}`, true);
}

function collapseAllMarkets(condIds: string[]): void {
  // 批量折叠: 一次 produce 把传入盘全置 false (store 优先于 ss, 确保折干净);
  //   ss 持久键也同步清 (防 reload 后 ss 残留 true 又自动展开)。
  setExpandedMarkets(produce((m) => { for (const c of condIds) m[c] = false; }));
  for (const c of condIds) ssSet(`stcpp_mkt_exp_${c}`, false);
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

  // sharp 偏离 (老板 2026-06-05「赔率源=真值, 量化不可靠」): 折叠态直接显 sharp + 市场价距 sharp 多远。
  //   优先展开拉来的 quote.sharp_fair (更鲜), 降级 /grid 摘要。Δ=sharp−mid (YES 视角):
  //   Δ>0 = 市场价低于 sharp = YES 偏便宜(绿); Δ<0 = YES 偏贵(红)。这是方向真值, 比 ML 的 Edge 可信。
  const sharpVal = () => {
    const s = fin(quote()?.sharp_fair ?? summary()?.sharp);
    return (s != null && s > 0 && s < 1) ? s : null;
  };
  const midVal = () => {
    const m = fin(quote()?.market_mid ?? summary()?.mid);
    if (m != null) return m;
    const b = bestBid(), a = bestAsk();
    return (b != null && a != null) ? (b + a) / 2 : null;
  };
  const sharpDev = () => {
    const s = sharpVal(), m = midVal();
    return (s != null && m != null) ? (s - m) : null;
  };

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

  // 市场名称: 优先 grid 的真实 title (gamma groupItemTitle, 如 "Game 1 Winner"); 缺则回退推断。
  //   (2026-06-02 老板「别显示 hash」: 折叠行也有真实名, 不再露 condition_id)
  const mktLabel = () => c().summary?.title || inferMarketLabel(condId(), c().market);

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

      {/* sharp 偏离 (赔率源真值锚, 老板 2026-06-05) — 折叠态就能扫出"市场价距 sharp 多远" */}
      <span class="v8-row-sharp"
            title="赔率源 sharp (bet365 de-vig YES 胜率) + 市场价距 sharp 偏离 Δ=sharp−mid (点)。Δ>0=市场价低于 sharp=YES 偏便宜(绿); Δ<0=偏贵(红)。这是方向真值, 比右侧 ML差 可信。">
        <Show when={sharpVal() != null} fallback={<span class="v8-dim">—</span>}>
          <span class="v8-row-sharp-val">{sharpVal()!.toFixed(3)}</span>
          <Show when={sharpDev() != null}>
            <span class={sharpDev()! >= 0 ? 'v8-edge-pos' : 'v8-edge-neg'}>
              {sharpDev()! >= 0 ? '+' : ''}{(sharpDev()! * 100).toFixed(1)}
            </span>
          </Show>
        </Show>
      </span>

      {/* ML差 (模型 edge — 老板定调 ML 不可靠, 降级为灰色仅参考; 方向看左侧 sharp 偏离) */}
      <span class="v8-row-edge v8-dim"
            title="ML 模型 edge (bps) — 模型不可靠, 仅参考。方向真值看左侧 sharp 偏离。">
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
  // ★ 响应式判定 (非 early-return): book 在展开后才异步到达, early-return 会卡在"未接入"不更新。
  const hasBook = () => { const b = book(); return !!b && (b as { found?: boolean }).found !== false && !!b.token0; };
  const bk = () => book()!;
  // grid 顶档摘要 (含两边 outcome 名); book 未带 outcome 时用它标注哪边是哪队/选手。
  const summ = () => state.conditionCache[props.conditionId]?.summary ?? null;
  // 订单簿版本年龄 (2026-06-05「飘」根治, 小郑独立诊断 + git+本地REST双证): = now − data_source_ts, 而
  //   data_source_ts 是 Polymarket /book 的 timestamp (订单簿【版本号时刻】, 只在簿真变化才前进; 149hz poller
  //   簿没变就拿到同一个 ts)。所以这个数 = 【距上次簿变化的年龄】, 静市场天然 sawtooth 爬升, 非"卡"。
  //   "数据管道是否实时"看顶部「SSE 实时」灯 (亚秒稳定), 别和这个会飘的版本年龄混淆。阈值放宽 (静市场几秒正常)。
  // ★ bug 修复 (老板 2026-06-05「订单簿变动了但版本年龄还 6s」): 版本年龄必须读【你正在看的订单簿】自己的
  //   data_source_ts (book.token0/token1 取最新那边), 不是读 quote 的 —— 二者是两条独立数据流, on-change 下
  //   订单簿变了(book 帧推)但报价没变(quote 帧不推) → 读 quote.data_source_ts 会卡在旧值, 而订单簿其实是新的。
  const freshTok  = () => { const b = book(); if (!b) return null; const d0 = Number(b.token0?.data_source_ts ?? 0), d1 = Number(b.token1?.data_source_ts ?? 0); return d0 >= d1 ? b.token0 : b.token1; };
  const bookTs    = () => { const t = freshTok(); const ds = t ? Number(t.data_source_ts) : 0; return ds > 0 ? ds : Number(state.conditionCache[props.conditionId]?.quote?.data_source_ts ?? 0); };
  const bookAgeS  = () => bookTs() > 0 ? Math.max(0, (uiNow() - bookTs() / 1e6) / 1000) : NaN;
  const bookFresh = () => !Number.isFinite(bookAgeS()) ? '#888'
    : bookAgeS() < 5 ? '#4caf50' : bookAgeS() < 12 ? '#ff9800' : '#f44336';  // 版本年龄: <5s绿 <12s黄 (静市场几秒正常)
  // 链路时间 (老板「能看出是我们系统卡还是本来就慢」): 后端入口 = ingestion_ts − data_source_ts
  //   (Polymarket 发布该版 → 伦敦后端收到; 就近部署正常 ~毫秒; 跟源头静默无关、不随时间涨)。高 = 我们后端/取数在拖。
  //   跨洋(后端→你浏览器)那段看顶部「SSE 实时」灯。
  const linkMs    = () => { const t = freshTok(); if (!t) return NaN; const ds = Number(t.data_source_ts), ing = Number(t.ingestion_ts); return (ds > 0 && ing >= ds) ? (ing - ds) / 1e6 : NaN; };
  const linkColor = () => !Number.isFinite(linkMs()) ? '#888' : linkMs() < 100 ? '#4caf50' : linkMs() < 500 ? '#ff9800' : '#f44336';
  const vigInfo = () => {
    const cs = Number(bk().cross_spread);
    if (!Number.isFinite(cs)) return null;
    const color: 'success' | 'warning' | 'error' = cs < 0.02 ? 'success' : cs < 0.04 ? 'warning' : 'error';
    return { text: `${(cs * 100).toFixed(2)}%`, color };
  };

  function HalfPane(p: { half: HalfBook; label: string }) {
    if (!p.half) return null;  // 防御: half 缺失(found:false/畸形 book)不渲染, 不读 .outcome 崩页
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
    <Show
      when={hasBook()}
      fallback={
        <div class="v8-expand-panel">
          <div class="v8-panel-title">双边订单簿</div>
          <Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>
            {isEndpointFailing(`/api/v1/book_pair/${props.conditionId}`) ? '订单簿拉取失败' : '订单簿未接入'}
          </Typography>
        </div>
      }
    >
      <div class="v8-expand-panel">
        <div class="v8-panel-title">
          双边订单簿
          {/* 数据年龄 + 链路 + 时刻 (老板 2026-06-05「让我看懂是我们系统卡还是源头本来就慢」):
              数据年龄=源头这版多旧(读订单簿自己的ts, 已修6s bug); 链路=后端取数延迟(ms=快); 跨洋看顶部SSE灯。 */}
          <Show when={Number.isFinite(bookAgeS())}>
            <span class="mono-sub" style={{ 'margin-left': '8px', 'font-weight': '700' }}
                  title="三段定位「卡在哪」: ①数据年龄=now−订单簿版本时刻(你看到这版数据多旧; 源头静默会涨属正常)。②链路=后端从源头取到这版的延迟(ingestion−data_source; 毫秒=我们后端没卡)。③跨洋(后端→你浏览器)看顶部「SSE 实时」灯。判断: 只有数据年龄高=源头本来就慢(不怪我们); 链路或SSE高=我们系统在卡。">
              <Show keyed when={bookTs()}><span class="v8-live-dot">●</span></Show>
              {' '}<span style={{ color: bookFresh() }}>数据年龄 {bookAgeS().toFixed(1)}s</span>
              <Show when={Number.isFinite(linkMs())}>
                <span style={{ 'margin-left': '6px', color: linkColor() }} title="链路 = 后端从 Polymarket 取到这版的延迟 (ingestion−data_source)。就近部署正常 ~毫秒; 高=我们后端/取数在拖, 跟源头静默无关。">链路 {linkMs() < 1 ? '<1' : linkMs().toFixed(0)}ms</span>
              </Show>
              <span class="mono-sub v8-dim" style={{ 'margin-left': '6px', 'font-weight': '400' }} title="该版订单簿的实际版本时刻 (data_source_ts)">· 时刻 {fmtTs(bookTs()).slice(-12)}</span>
            </span>
          </Show>
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
          <HalfPane half={bk().token0} label={summ()?.outcome0 || 'YES'} />
          <HalfPane half={bk().token1} label={summ()?.outcome1 || 'NO'} />
        </div>
      </div>
    </Show>
  );
}

// 单盘口最近成交 (老板「还是持仓那个地方显示」): 直接后端按盘拉 /api/v1/fills?market=cond,
//   深环5000保证有数据(不受全局churn丢失), 即便已平仓也留着买卖价。展开时每4s刷。
function MarketFills(props: { conditionId: string }) {
  // 2026-06-04 老板「都走同一个 wss」「刷新频率对齐订单簿/量化 AI」: 不再自己 4s 轮询,
  //   直接读 store.fillsByMarket (由 fetchDetailFor 与 book/quote 同 2s 节拍写入) → 完全同步刷新。
  const rows = () => state.fillsByMarket[props.conditionId] ?? [];
  const fills = () => rows().slice(0, 7);  // 7 行对齐订单簿/量化 AI 两栏高度 (老板「显示7行就行了」)
  // 2026-06-05 老板「买入卖出太草率, 看不懂卖了几单」: 加 买/卖 笔数 + 单位汇总, 每笔明确显示数量。
  const buysAll = () => rows().filter((f) => f.side === 'buy');
  const sellsAll = () => rows().filter((f) => f.side === 'sell');
  const sumU = (a: Fill[]) => a.reduce((s, f) => s + f.size_usdc, 0);
  const totalReal = () => sellsAll().reduce((s, f) => s + f.realized, 0);
  const buysFair = () => rows().filter((f) => f.side === 'buy' && f.fair > 0);
  const avgClaim = () => { const b = buysFair(); return b.length ? b.reduce((s, f) => s + (f.fair - f.price), 0) / b.length : NaN; };
  const claimEdge = (f: Fill) => f.side === 'buy' ? f.fair - f.price : f.price - f.fair;
  return (
    <>
      <div class="v8-reject-title">成交</div>
      {/* 买/卖 笔数 + 单位汇总 — 一眼看清买了几单卖了几单 (老板) */}
      <div class="v8-pos-row" style={{ gap: '10px', 'font-size': '11px' }}>
        <span style={{ color: '#42a5f5', 'font-weight': 700 }}>买 {buysAll().length}笔 · {sumU(buysAll()).toFixed(1)}u</span>
        <span style={{ color: '#ffa726', 'font-weight': 700 }}>卖 {sellsAll().length}笔 · {sumU(sellsAll()).toFixed(1)}u</span>
        <span class={`${totalReal() >= 0 ? 'pnl-pos' : 'pnl-neg'}`} style={{ 'margin-left': 'auto', 'font-weight': 700 }}>
          已实现 {totalReal() >= 0 ? '+' : ''}{totalReal().toFixed(2)}
        </span>
      </div>
      <Show when={Number.isFinite(avgClaim())}>
        <div class="mono-sub" style={{ color: avgClaim() > 0.05 ? '#f44336' : '#888', 'font-size': '10px', 'margin-bottom': '2px' }}
          title="本盘买入时模型平均声称便宜多少 (>5点=模型对此盘系统性高估)">
          模型偏差 {avgClaim() >= 0 ? '+' : ''}{(avgClaim() * 100).toFixed(1)}点 · 下方逐笔
        </div>
      </Show>
      <Show
        when={fills().length > 0}
        fallback={<Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>暂无成交</Typography>}
      >
        <For each={fills()}>
          {(f) => (
            <div class="v8-pos-row" title={fmtTs(f.as_of_ts)} style={{ gap: '6px' }}>
              <span class="mono-sub" style={{ color: '#888', 'font-size': '10px', width: '54px' }}>{fmtTs(f.as_of_ts).slice(-8)}</span>
              <span class="mono-sub" style={{ color: f.side === 'buy' ? '#42a5f5' : '#ffa726', 'font-weight': 700, width: '46px' }}>
                {f.side === 'buy' ? '买' : (f.is_close ? '卖平' : '卖')}{f.outcome}
              </span>
              <span class="mono-sub" style={{ 'font-weight': 700, width: '52px', 'text-align': 'right' }} title="本笔数量(单位)">{f.size_usdc.toFixed(1)}u</span>
              <span class="mono-sub" style={{ width: '54px', 'text-align': 'right' }} title="成交价">@{f.price.toFixed(3)}</span>
              <span class="mono-sub" style={{ color: f.fair > 0 && Math.abs(claimEdge(f)) > 0.05 ? '#f44336' : '#777', 'font-size': '10px', width: '34px', 'text-align': 'right' }}
                title="模型声称 edge (点)">{f.fair > 0 ? `${claimEdge(f) >= 0 ? '+' : ''}${(claimEdge(f) * 100).toFixed(0)}pt` : ''}</span>
              <span class={`mono-sub ${f.side === 'sell' ? (f.realized >= 0 ? 'pnl-pos' : 'pnl-neg') : ''}`} style={{ 'margin-left': 'auto' }} title="本笔已实现(卖出才有)">
                {f.side === 'sell' ? `${f.realized >= 0 ? '+' : ''}${f.realized.toFixed(2)}` : '—'}
              </span>
            </div>
          )}
        </For>
      </Show>
    </>
  );
}

// ============================================================
// 展开区子块 B: 量化 / AI (XD-1/3/4/5 红线保持)
// ============================================================

// 收敛/发散迷你图 (老板 2026-06-05「市场价相对 sharp 收敛/发散 = 持仓对错核心信号」):
//   叠 PM mid(蓝) 与 sharp fair(橙) 时序; |gap| 缩小=收敛(绿底), 扩大=发散(红底)。随 1s uiNow 重绘。
function ConvergenceSparkline(props: { conditionId: string }) {
  const W = 132, H = 30;
  const ring = () => { uiNow(); return getSharpTrend(props.conditionId); };  // uiNow 触发每秒重读 module 环
  const bounds = () => {
    const r = ring();
    if (r.length < 2) return null;
    let lo = Infinity, hi = -Infinity;
    for (const p of r) { lo = Math.min(lo, p.sharp, p.mid); hi = Math.max(hi, p.sharp, p.mid); }
    if (!(hi > lo)) { lo -= 0.01; hi += 0.01; }
    const pad = (hi - lo) * 0.08;
    return { lo: lo - pad, hi: hi + pad };
  };
  const poly = (sel: (p: { sharp: number; mid: number }) => number) => {
    const r = ring(), b = bounds();
    if (!b) return '';
    return r.map((p, i) => {
      const x = r.length <= 1 ? 0 : (i / (r.length - 1)) * W;
      const y = H - ((sel(p) - b.lo) / (b.hi - b.lo)) * H;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    }).join(' ');
  };
  // 服务端权威 (SharpFairTrack; 持久可信, 不随刷新归零): sharp_samples≥2 时用后端 conv_rate(prob/sec) 判向。
  const q = () => state.conditionCache[props.conditionId]?.quote ?? null;
  const srvSamples = () => Number(q()?.sharp_samples ?? 0);
  const srvConv = () => Number(q()?.sharp_conv_rate ?? Number.NaN);
  const srvVel = () => Number(q()?.sharp_velocity ?? Number.NaN);
  const conv = (): 'converge' | 'diverge' | 'flat' | 'none' => {
    if (srvSamples() >= 2 && Number.isFinite(srvConv())) {  // 优先服务端
      if (srvConv() < -1e-4) return 'converge';
      if (srvConv() > 1e-4) return 'diverge';
      return 'flat';
    }
    const r = ring();  // 回退: 前端自攒环 (服务端样本不足时)
    if (r.length < 3) return 'none';
    const now = r[r.length - 1], past = r[Math.max(0, r.length - 6)];
    const gNow = Math.abs(now.sharp - now.mid), gPast = Math.abs(past.sharp - past.mid);
    if (gNow < gPast * 0.7) return 'converge';
    if (gNow > gPast * 1.3) return 'diverge';
    return 'flat';
  };
  const meta = () => ({
    converge: { label: '收敛 →', color: '#4caf50', bg: 'rgba(76,175,80,0.10)' },
    diverge: { label: '发散 ←', color: '#f44336', bg: 'rgba(244,67,54,0.10)' },
    flat: { label: '震荡 ≈', color: '#888', bg: 'transparent' },
    none: { label: '攒样本…', color: '#888', bg: 'transparent' },
  }[conv()]);
  return (
    <div class="v8-q-row" style={{ 'align-items': 'center', gap: '6px' }}>
      <span class="q-lbl" title="市场价(蓝) 相对 sharp(橙) 的收敛/发散轨迹 = 持仓对错的实时信号。收敛=市场向我们 sharp 靠拢=持仓变对; 发散=变错。前端自攒, 刷新重置。">趋势</span>
      <Show when={ring().length >= 2}
            fallback={<span class="mono-sub v8-dim">{srvSamples() >= 2 ? '' : '攒样本中…'}</span>}>
        <svg width={W} height={H} style={{ background: meta().bg, 'border-radius': '3px' }}>
          <polyline points={poly((p) => p.mid)} fill="none" stroke="#42a5f5" stroke-width="1.2" />
          <polyline points={poly((p) => p.sharp)} fill="none" stroke="#ffa726" stroke-width="1.2" />
        </svg>
        <span class="q-lbl" title="蓝=PM 市场 mid · 橙=sharp fair" style={{ 'font-size': '9px' }}>
          <span style={{ color: '#42a5f5' }}>━mid</span> <span style={{ color: '#ffa726' }}>━sharp</span>
        </span>
      </Show>
      <Show when={conv() !== 'none'}>
        <span class="mono-sub" style={{ color: meta().color, 'font-weight': 700 }}
              title={srvSamples() >= 2 ? '服务端 SharpFairTrack 收敛率 (持久可信)' : '前端自攒 (服务端样本不足回退)'}>
          {meta().label}{srvSamples() >= 2 ? '' : '·前'}
        </span>
      </Show>
      <Show when={srvSamples() >= 2 && Number.isFinite(srvVel())}>
        <span class="mono-sub" title="服务端 sharp 速度 (line movement; prob/sec → 点/秒)"
              style={{ color: Math.abs(srvVel()) < 1e-5 ? '#888' : srvVel() > 0 ? '#4caf50' : '#f44336', 'font-weight': 700 }}>
          速度 {srvVel() >= 0 ? '+' : ''}{(srvVel() * 100).toFixed(2)}pt/s
        </span>
      </Show>
    </div>
  );
}

function ExpandQuotePanel(props: { quote: Quote | null; conditionId: string }) {
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
  const modelKind  = () => String(q().model_kind ?? '').toLowerCase();  // onnx=真实模型 / stub=退化占位
  const isOnnx     = () => modelKind() === 'onnx';
  const calibrated = () => q().model_calibrated !== false;
  const predictOk  = () => q().predict_ok !== false;
  const advisory   = () => q().advisory === true;
  const ciLower    = () => q().fair_ci_lower;
  const ciUpper    = () => q().fair_ci_upper;
  const hasCi      = () => Number.isFinite(ciLower()) && Number.isFinite(ciUpper()) && ciUpper() > ciLower();
  const modelReady = () => calibrated() && Number.isFinite(modelConf()) && modelConf() > 0;  // 模型真出活
  const confPct    = () => Number.isFinite(modelConf()) ? modelConf() * 100 : 0;
  const confColor  = (): 'success' | 'warning' | 'error' | 'inherit' =>
    modelConf() >= 0.7 ? 'success' : modelConf() >= 0.4 ? 'warning' : 'error';
  const edgePos    = () => fairValue() >= marketMid();
  const edgePct    = () => Math.min(Math.abs(edgeBps()) / 100, 1) * 100;
  const kellyPos   = () => Number.isFinite(kelly()) && kelly() > 0;
  // 可观测: fair 来源 / 数据管道 (老板 2026-06-02: 把后台可观测搬到前端大模型下面)
  const sharpFair  = () => Number(q().sharp_fair ?? -1);
  const hasSharp   = () => sharpFair() > 0 && sharpFair() < 1;          // inplay To Win de-vig sharp 锚
  const sharpDev   = () => hasSharp() ? (sharpFair() - marketMid()) : NaN;  // sharp vs 市场 = 价差信号
  const jointTs    = () => Number(q().joint_as_of_ts ?? 0);
  const mapped     = () => jointTs() > 0;                                // 比分/订单簿映射已连通 (匹配上)
  const devigOk    = () => q().devig_ok === true;
  const jointAgeS  = () => mapped() ? Math.max(0, (Date.now() * 1e6 - jointTs()) / 1e9) : NaN;
  // (订单簿新鲜度心跳已挪到「双边订单簿」面板 — 老板 2026-06-05「WSS新鲜度放订单簿位置+改名订单簿新鲜度」)
  // GS sharp 赔率延迟 (2026-06-05 老板「赔率延迟放合适位置」): now − sharp 赔率版本时刻 (Goalserve inplay
  //   updated_ts; 驱动 sharp fair 的那一版多旧)。这是 3s 新鲜度门管的延迟; 摆 sharp 行旁。
  const sharpTs    = () => Number(q().sharp_data_source_ts ?? 0);
  const sharpAgeS  = () => sharpTs() > 0 ? Math.max(0, (uiNow() - sharpTs() / 1e6) / 1000) : NaN;
  // 赔率版本年龄着色 (2026-06-05「飘」根治, 小郑诊断): Goalserve 每 ~2-3s 才出一版赔率 + 落后 bet365 ~2.3s
  //   (团队记忆 gs-bet365-latency), 故 ≤3.5s 是固有物理延迟属正常, >6s 才真陈旧。放宽阈值, 不再让人误以为系统卡。
  const sharpAgeColor = () => !Number.isFinite(sharpAgeS()) ? '#888'
    : sharpAgeS() < 3.5 ? '#4caf50' : sharpAgeS() < 6 ? '#ff9800' : '#f44336';

  return (
    <div class="v8-expand-panel">
      <div class="v8-panel-title">
        量化 / AI <span class="mono-sub" style={{ 'font-weight': '400' }}>· 均为 YES 边胜率</span>
        {/* XD-3: ADVISORY 角标强制显示 (paper 期) */}
        <Show when={advisory()}>
          <span class="v8-advisory-badge">ADVISORY</span>
        </Show>
      </div>

      {/* sharp 锚 (inplay de-vig) — 模型未训练时这才是【可用 fair】, 摆最前高亮 */}
      <div class="v8-q-row">
        <span class="q-lbl" title="inplay bet365 'To Win' de-vig 的 YES 胜率 — 模型未训练时用它当 fair">sharp</span>
        <Show when={hasSharp()} fallback={<span class="mono-sub v8-dim">{mapped() ? '无赔率' : '未映射'}</span>}>
          <span class="mono-strong" style={{ 'font-size': '15px', 'color': !modelReady() ? '#4caf50' : undefined }}>
            {sharpFair().toFixed(4)}
          </span>
          <span class="q-lbl">vs市场</span>
          <span class={`mono-sub${sharpDev() >= 0 ? ' edge-pos' : ' edge-neg'}`} style={{ 'font-weight': '700' }}>
            {fmtBps(sharpDev() * 10000)}
          </span>
          <Show when={!modelReady()}><span class="mono-sub" style={{ 'color': '#4caf50' }}>← 当前 fair</span></Show>
          {/* GS sharp 赔率版本年龄 (2026-06-05「飘」根治): = Goalserve 赔率版本年龄, 非系统卡。每~2-3s出一版属正常 */}
          <Show when={Number.isFinite(sharpAgeS())}>
            <span class="mono-sub" style={{ 'margin-left': 'auto', 'font-weight': '700', color: sharpAgeColor() }}
                  title="赔率版本年龄 = now − Goalserve inplay 赔率版本时刻。Goalserve 每 ~2-3s 才出一版赔率 + 它本身落后 bet365 ~2.3s, 故 ≤3.5s 是固有物理延迟属正常 (sawtooth 0→3s 是 GS 出版节奏, 压不下去, 非系统卡)。>6s 才真陈旧。">
              赔率龄 {sharpAgeS().toFixed(1)}s
              <span class="mono-sub v8-dim" style={{ 'margin-left': '4px', 'font-weight': '400' }} title="该版 Goalserve 赔率的实际版本时刻 (sharp_data_source_ts)">· 时刻 {fmtTs(sharpTs()).slice(-12)}</span>
            </span>
          </Show>
        </Show>
      </div>

      {/* 收敛/发散趋势迷你图 (老板 2026-06-05「盘口趋势 = 持仓对错核心信号」) */}
      <ConvergenceSparkline conditionId={props.conditionId} />

      <div class="v8-q-row">
        <span class="q-lbl">市场</span>
        <span class="mono-sub">{Number.isFinite(marketMid()) ? marketMid().toFixed(4) : '—'}</span>
      </div>

      {/* 模型公允 — 未训练时 dim + 明确标注, 不当真值 */}
      <div class="v8-q-row">
        <span class="q-lbl">模型</span>
        <span class={`mono-strong${!modelReady() ? ' v8-dim' : ''}`}>
          {Number.isFinite(fairValue()) ? fairValue().toFixed(4) : '—'}
        </span>
        <Show when={!modelReady()} fallback={hasCi() ? <span class="mono-sub">[{ciLower()!.toFixed(3)}–{ciUpper()!.toFixed(3)}]</span> : null}>
          <span class="uncalib-chip">未训练·占位</span>
        </Show>
      </div>

      {/* 置信 / edge / kelly — 仅当模型真出活 (calibrated + conf>0) 才显; 未训练折叠成一句, 不堆 0 */}
      <Show
        when={modelReady() && predictOk()}
        fallback={
          <div class="v8-q-row v8-dim" style={{ 'margin-top': '3px', 'font-size': '11px' }}>
            {!predictOk() ? '⚠ 预测异常 · 无信号' : '模型未训练 · paper 期不产生 edge/Kelly 信号 (看上方 sharp 价差)'}
          </div>
        }
      >
        <div class="v8-q-row">
          <span class="q-lbl">置信</span>
          <Box sx={{ flex: 1, minWidth: '40px' }}>
            <LinearProgress variant="determinate" value={confPct()} color={confColor()} sx={{ height: 5, borderRadius: 2 }} />
          </Box>
          <Typography sx={{ fontFamily: 'monospace', fontSize: '10px', ml: 0.5,
            color: confColor() === 'success' ? '#4caf50' : confColor() === 'warning' ? '#ff9800' : '#f44336' }}>
            {`${(modelConf() * 100).toFixed(0)}%`}
          </Typography>
        </div>

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

      {/* model provenance — 清晰标出【真实大模型 vs 退化 stub】(老板 2026-06-03: 前端要看得出用哪个) */}
      <div class="v8-q-row v8-model-row">
        <span class="q-lbl">model</span>
        <span class="mono-sub" title={`${q().model_id} · ${q().model_kind} · ${q().spec_version}`}>
          {modelId()}
        </span>
        <span class="mono-sub" style={{
          'font-weight': '700',
          'color': (isOnnx() && modelReady()) ? '#4caf50' : isOnnx() ? '#ff9800' : '#888',
        }}>
          {isOnnx() && modelReady()
            ? `🟢真实模型·已校准·置信${confPct().toFixed(0)}%`
            : isOnnx()
            ? '🟡ONNX·未校准(不驱动)'
            : '⚪退化stub(不驱动)'}
        </span>
      </div>

      {/* 数据管道状态 (sharp 锚已挪到面板最上方) */}
      <div class="v8-obs-block">
        <div class="v8-q-row">
          <span class="q-lbl" title="比分/订单簿映射连通 (匹配到 Goalserve) + de-vig 状态 + 联合新鲜度">管道</span>
          <span class={`mono-sub ${mapped() ? 'edge-pos' : 'edge-neg'}`}>{mapped() ? '✓映射' : '✗未映射'}</span>
          <span class="mono-sub">{devigOk() ? 'de-vig✓' : 'de-vig✗'}</span>
          <Show when={mapped() && Number.isFinite(jointAgeS())}>
            <span class="mono-sub v8-dim">{jointAgeS().toFixed(0)}s</span>
          </Show>
        </div>
      </div>
    </div>
  );
}

// ============================================================
// 展开区子块 C: 持仓 + 拒单
// ============================================================

function ExpandPosPanel(props: { posRows: Position[]; rejectRows: RiskReject[]; perMarketPnl: number | null; conditionId: string }) {
  // 持仓管理 (2026-06-05 老板「持仓管理怎么体现: 凯利系数 / 希望持多少yes多少no / 实际持有 + 估值」):
  //   凯利系数 + 控制器目标仓位(希望持) vs 实际持仓 + 估值, 按 YES/NO 两边列清楚。数据: quote(凯利/目标/
  //   选边) + posRows(实际/mark)。希望持: 被选边(fair≥市场=YES, 否则NO)= 凯利目标 suggested_notional, 另一边 0。
  const quote = () => state.conditionCache[props.conditionId]?.quote ?? null;
  const num = (v: unknown): number => { const n = Number(v); return Number.isFinite(n) ? n : NaN; };
  const kelly = () => { const q = quote(); return q ? num(q.kelly_fraction) : NaN; };
  const favoredYes = () => { const q = quote(); return q ? num(q.fair_value) >= num(q.market_mid) : true; };
  const target = () => { const q = quote(); return q ? num(q.suggested_notional) : 0; };
  const posFor = (oc: string) => props.posRows.find((p) => p.outcome === oc) ?? null;
  const held = (oc: string) => { const p = posFor(oc); return p ? num(p.net_qty) : 0; };
  const val = (oc: string) => { const p = posFor(oc); return p ? num(p.net_qty) * num(p.mark_price) : 0; };
  const want = (oc: string) => { const t = target(); return (oc === 'YES') === favoredYes() ? (Number.isFinite(t) ? t : 0) : 0; };
  const u = (v: number) => (Number.isFinite(v) && v !== 0 ? `${v >= 0 ? '' : ''}${v.toFixed(1)}u` : '—');
  const hasPos = () => props.posRows.length > 0 || (Number.isFinite(target()) && target() > 0);

  // C 持仓可解释 (老板 2026-06-05「盯盘人要一眼判断这仓管得对不对」):
  //   C2 距 sharp 距离 = 入场价 vs 当前 sharp(本边); sharp_fair 是 YES 胜率, NO 边=1−sharp_yes。
  //     dist = sharp_side − 入场价: >0 = sharp 在我方上方 = 持仓正确(绿)。这是「这仓对不对」核心判据。
  //   C1 持仓时长 = 本会话墙钟近似 (后端 entry_ts 待补; uiNow 驱动每秒重算; 刷新会重置)。
  const sharpYes = () => { const q = quote(); const s = q ? num(q.sharp_fair) : NaN; return (s > 0 && s < 1) ? s : NaN; };
  const sharpFor = (oc: string) => { const sy = sharpYes(); return Number.isFinite(sy) ? (oc === 'YES' ? sy : 1 - sy) : NaN; };
  const entryFor = (oc: string) => { const p = posFor(oc); return p ? num(p.avg_entry_price) : NaN; };
  const distFor = (oc: string) => { const s = sharpFor(oc), e = entryFor(oc); return (Number.isFinite(s) && Number.isFinite(e)) ? s - e : NaN; };
  const ageSecFor = (oc: string) => { const p = posFor(oc); if (!p) return NaN; const seen = posSeenAt(p.market_id, oc); return seen ? Math.max(0, (uiNow() - seen) / 1000) : NaN; };
  const ageText = (s: number) => !Number.isFinite(s) ? '—' : s < 60 ? `${Math.round(s)}s` : s < 3600 ? `${Math.floor(s / 60)}min` : `${(s / 3600).toFixed(1)}h`;
  const heldSides = () => ['YES', 'NO'].filter((oc) => { const p = posFor(oc); return p != null && Math.abs(num(p.net_qty)) > 0; });
  return (
    <div class="v8-expand-panel">
      <div class="v8-panel-title">
        持仓管理
        <Show when={Number.isFinite(kelly())}>
          <span class="mono-sub" style={{ 'margin-left': '8px', 'font-weight': 400 }}>
            · 凯利系数 <b style={{ color: kelly() > 0 ? '#4caf50' : '#888' }}>{(kelly() * 100).toFixed(0)}%</b>
          </span>
        </Show>
      </div>
      <Show when={hasPos()} fallback={
        <Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>无持仓 / 无目标</Typography>
      }>
        <div class="v8-pos-row" style={{ gap: '8px', color: '#888', 'font-size': '10px' }}>
          <span style={{ width: '32px' }}>边</span>
          <span style={{ width: '64px', 'text-align': 'right' }} title="控制器凯利目标仓位">希望持</span>
          <span style={{ width: '64px', 'text-align': 'right' }} title="账本当前实际持仓">实际持</span>
          <span style={{ 'margin-left': 'auto' }} title="实际持仓 × 标记价">估值</span>
        </div>
        <For each={['YES', 'NO']}>
          {(oc) => (
            <div class="v8-pos-row" style={{ gap: '8px' }}>
              <Chip label={oc} size="small" variant="outlined" sx={{ fontSize: '9px', height: '16px', fontWeight: 700, width: '32px' }} />
              <span class="mono-sub" style={{ width: '64px', 'text-align': 'right', color: want(oc) > 0 ? '#42a5f5' : '#666' }}>{u(want(oc))}</span>
              <span class="mono-sub" style={{ width: '64px', 'text-align': 'right', 'font-weight': 700 }}>{u(held(oc))}</span>
              <span class="mono-sub" style={{ 'margin-left': 'auto', color: '#bbb' }}>{val(oc) !== 0 ? fmtUsdc(val(oc)) : '—'}</span>
            </div>
          )}
        </For>
        {/* C 持仓可解释: 每个实际持仓边一行 — 入场价 vs 当前 sharp 距离 (距锚>0=持仓正确绿) + 本会话持仓时长 */}
        <For each={heldSides()}>
          {(oc) => (
            <div class="v8-pos-explain"
                 title="入场价 vs 当前 sharp(本边) 的距离 = 这仓对不对的核心判据; 距锚>0=sharp 在我方上方=持仓正确(绿)。持仓时长为本会话墙钟近似(后端 entry_ts 待补, 刷新页面会重置)。">
              <Chip label={oc} size="small" variant="outlined" sx={{ fontSize: '8px', height: '14px', fontWeight: 700, width: '30px' }} />
              <span class="mono-sub">入场 {Number.isFinite(entryFor(oc)) ? entryFor(oc).toFixed(3) : '—'}</span>
              <Show when={Number.isFinite(sharpFor(oc))} fallback={<span class="mono-sub v8-dim">· 无 sharp 锚</span>}>
                <span class="mono-sub">· sharp {sharpFor(oc).toFixed(3)}</span>
                <span class={`mono-sub ${distFor(oc) >= 0 ? 'v8-edge-pos' : 'v8-edge-neg'}`} style={{ 'font-weight': 700 }}>
                  · 距锚 {distFor(oc) >= 0 ? '+' : ''}{(distFor(oc) * 100).toFixed(1)}点
                </span>
              </Show>
              <span class="mono-sub v8-dim" style={{ 'margin-left': 'auto' }} title="本会话持仓时长 (近似)">持 ~{ageText(ageSecFor(oc))}</span>
            </div>
          )}
        </For>
        <Show when={props.perMarketPnl != null}>
          <div class="v8-pos-total">
            <span class="q-lbl">本盘 已实现+浮盈</span>
            <span class={`mono-strong ${(props.perMarketPnl ?? 0) >= 0 ? 'pnl-pos' : 'pnl-neg'}`}>{fmtUsdc(props.perMarketPnl ?? 0)}</span>
          </div>
        </Show>
      </Show>

      {/* 成交 — 与订单簿/量化 AI 同 2s 节拍刷新 (走同一个 fetchDetailFor), 7 行对齐 */}
      <MarketFills conditionId={props.conditionId} />

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
      <ExpandQuotePanel quote={c().quote} conditionId={c().conditionId} />
      <ExpandPosPanel posRows={c().posRows} rejectRows={c().rejectRows} perMarketPnl={c().perMarketPnl} conditionId={c().conditionId} />
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

  // 比赛状态/开赛时间 (老板 2026-06-02: 盯盘要看几点开赛 + 是否进行中)。
  //   同 event 各盘共享开赛时间/state, 取首个有值的 summary (来自 grid game_state/kickoff_ts)。
  const gameSummary = () => {
    for (const c of grp().conditions) {
      const s = c.summary;
      if (s && (s.kickoffTs != null || s.gameState != null)) return s;
    }
    return null;
  };
  const kickoffLabel = () => {
    const ts = gameSummary()?.kickoffTs;
    return ts ? fmtTs(ts) : null;
  };
  // 未匹配(无 Goalserve 比分)时用后端 game_state 兜底显示状态; 已匹配的上方 score chip 已显示。
  const fallbackStateZh = () => {
    const gs = gameSummary()?.gameState;
    return gs && gs !== 'unknown' ? (GAMESTATE_ZH[gs] ?? gs) : null;
  };
  // 官方 Polymarket 跳转 (老板 2026-06-02: 想直接对比官方盘口)。用 event slug 拼官方 event 页。
  const polymarketUrl = () => {
    const slug = grp().eventSlug;
    return slug ? `https://polymarket.com/event/${slug}` : null;
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

        {/* 赛事图 (老板 2026-06-02: 图片有 URL; gamma icon/image)。加载失败自动隐藏。 */}
        <Show when={grp().iconUrl}>
          <img class="v8-evt-icon" src={grp().iconUrl!} alt="" loading="lazy"
            onError={(e) => { (e.currentTarget as HTMLImageElement).style.display = 'none'; }} />
        </Show>

        {/* 运动图标 + 标签 (老板 2026-06-02 要比赛图标) */}
        <Show when={sportZh()}>
          <Chip label={`${sportIcon(grp().sport ?? score()?.sport)} ${sportZh()}`} size="small" variant="outlined"
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
          {/* 网球实时比分 (老板 2026-06-03「显示实时比分而非只赛点」): 逐盘比分 + 当前局分 + 发球方 */}
          <Show when={score()?.set_summary}>
            <span title="逐盘比分 (各盘已打局数 home-away)"
              style={{ color: '#fbbf24', 'font-size': '11px', 'margin-left': '4px', 'font-weight': 600 }}>
              {score()!.set_summary}
            </span>
          </Show>
          <Show when={score()?.pts_home || score()?.pts_away}>
            <span title="当前局得分 (🎾=发球方)" style={{ color: '#4ade80', 'font-size': '11px' }}>
              {score()?.serving === 0 ? '🎾' : ''}{score()!.pts_home || '0'}–{score()!.pts_away || '0'}{score()?.serving === 1 ? '🎾' : ''}
            </span>
          </Show>
          <span class="v8-evt-team">{awayTeam() ?? '—'}</span>
        </Show>

        {/* 状态 Chip (已匹配: Goalserve 比分 status 权威) */}
        <Show when={score()}>
          <Chip
            label={statusZh()}
            color={isLive() ? 'success' : 'default'}
            size="small"
            variant="outlined"
            sx={{ fontSize: '9px', height: '18px' }}
          />
        </Show>

        {/* 未匹配: 后端 game_state 兜底状态 (赛前/进行中/已结束/已结算) */}
        <Show when={!score() && fallbackStateZh()}>
          <Chip
            label={fallbackStateZh()!}
            color={gameSummary()?.gameState === 'inplay' ? 'success' : 'default'}
            size="small"
            variant="outlined"
            sx={{ fontSize: '9px', height: '18px' }}
          />
        </Show>

        {/* 在打但无 Goalserve 实时比分: 明示"数据源未覆盖", 不留空白让人以为漏了 (老板 2026-06-03)。
            场景: 小众女子板球/ITF 等 Goalserve 列赛程但不直播逐球 (status=Not covered Live) → 无比分。 */}
        <Show when={!score() && gameSummary()?.gameState === 'inplay'}>
          <span
            style={{ color: '#c8924a', 'font-size': '10px', 'margin-left': '2px' }}
            title="该比赛 Goalserve 不提供实时比分 (小众赛事/女子赛/未直播覆盖); 非系统遗漏"
          >
            ⚠ 无实时比分·数据源未覆盖
          </span>
        </Show>

        {/* 开赛时间 (始终显示; 老板「看几点开赛」) */}
        <Show when={kickoffLabel()}>
          <span class="v8-evt-kickoff" title="开赛时间">🕒 {kickoffLabel()}</span>
        </Show>

        {/* 官方 Polymarket 跳转 (老板: 对比官方盘口; stopPropagation 防触发折叠) */}
        <Show when={polymarketUrl()}>
          <a class="v8-evt-link" href={polymarketUrl()!} target="_blank" rel="noopener noreferrer"
            onClick={(e) => e.stopPropagation()} title="在 Polymarket 官方查看 (对比盘口)">官方 ↗</a>
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
          <span class="v8-col-sharp" title="赔率源 sharp + 市场价距 sharp 偏离 (方向真值)">sharp偏离</span>
          <span class="v8-col-edge" title="ML 模型 edge — 不可靠, 仅参考">ML差</span>
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

type FilterMode = 'all' | 'live' | 'sourced' | 'position';

function TradingToolbar(props: {
  filter: FilterMode;
  search: string;
  onFilter: (f: FilterMode) => void;
  onSearch: (s: string) => void;
  visibleCount: number;
  totalCount: number;
  totalMarkets: number;
  hideNoBook: boolean;
  onToggleNoBook: () => void;
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
        <ToggleButton value="sourced" sx={{ fontSize: '12px', px: 1.5 }}>有直播源</ToggleButton>
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

      {/* 隐藏无簿盘 (默认开): 纯显示过滤 — 后端仍订阅+45s 重打底全部盘, 一旦挂上单 book_found
          翻 true 经 SSE grid 通道推达, 该行自动重新出现, 不漏跟踪。有持仓的盘永不隐藏。 */}
      <button
        class="v8-quickbtn"
        onClick={props.onToggleNoBook}
        title={props.hideNoBook ? '当前隐藏无订单簿的盘口 (点击显示全部; 后端仍在跟踪, 有簿自动现身)' : '当前显示全部盘口 (点击隐藏无订单簿的)'}
        style={{ opacity: props.hideNoBook ? '1' : '0.55' }}
      >
        {props.hideNoBook ? '隐藏无簿 ✓' : '隐藏无簿'}
      </button>

      {/* 全展开/全折叠快捷按钮 */}
      <div class="v8-toolbar-btns">
        <button class="v8-quickbtn" onClick={props.onExpandAll} title="全部展开">全展</button>
        <button class="v8-quickbtn" onClick={props.onCollapseAll} title="全部折叠">全折</button>
      </div>
    </div>
  );
}

// ============================================================
// 模型诊断条 (2026-06-04 老板「在盯盘页面本身做好, 不要分散」): 全局成交算模型偏差/声称edge/方向/
//   实现 → 一条紧凑横条挂盯盘顶部, 调模型一眼看, 不用进 PNL/市场详情页找。
// ============================================================
function ModelBiasStrip() {
  const fills = () => state.fills?.fills ?? [];
  const buys = () => fills().filter((f) => f.side === 'buy' && f.fair > 0);
  const withMark = () => fills().filter((f) => f.mark > 0 && f.fair > 0);
  const mean = (a: number[]) => (a.length ? a.reduce((s, x) => s + x, 0) / a.length : NaN);
  const bias = () => mean(withMark().map((f) => f.fair - f.mark));
  const claim = () => mean(buys().map((f) => f.fair - f.price));
  const posPct = () => { const a = buys(); return a.length ? (100 * a.filter((f) => f.fair > f.price).length) / a.length : NaN; };
  const buyYes = () => buys().filter((f) => f.outcome === 'YES').length;
  const buyNo = () => buys().filter((f) => f.outcome === 'NO').length;
  const realized = () => fills().filter((f) => f.side === 'sell').reduce((s, f) => s + f.realized, 0);
  const itemSx = { display: 'flex', 'align-items': 'baseline', gap: '4px', 'font-size': '11px' } as const;
  const bad = '#f44336'; const dim = '#888';
  return (
    <Show when={buys().length >= 3}>
      <div style={{ display: 'flex', 'align-items': 'center', gap: '16px', 'flex-wrap': 'wrap',
        padding: '6px 12px', margin: '0 0 6px', background: '#1a1a1a', border: '1px solid #373737',
        'border-radius': '6px' }}>
        <span style={{ 'font-weight': 700, 'font-size': '11px', 'letter-spacing': '0.05em', color: '#ccc' }}>
          模型诊断 · 调模型看这里
        </span>
        <span style={itemSx}><span style={{ color: dim }}>偏差(fair−mark)</span>
          <b style={{ color: bias() > 0.03 ? bad : '#4caf50', 'font-family': 'monospace' }}>
            {Number.isFinite(bias()) ? `${bias() >= 0 ? '+' : ''}${(bias() * 100).toFixed(1)}点` : '—'}</b></span>
        <span style={itemSx}><span style={{ color: dim }}>声称edge</span>
          <b style={{ color: claim() > 0.05 ? bad : '#ccc', 'font-family': 'monospace' }}>
            {Number.isFinite(claim()) ? `${claim() >= 0 ? '+' : ''}${(claim() * 100).toFixed(1)}点` : '—'}</b></span>
        <span style={itemSx}><span style={{ color: dim }}>正edge占比</span>
          <b style={{ color: posPct() > 85 ? bad : '#ccc', 'font-family': 'monospace' }}>
            {Number.isFinite(posPct()) ? `${posPct().toFixed(0)}%` : '—'}</b></span>
        <span style={itemSx}><span style={{ color: dim }}>方向</span>
          <b style={{ 'font-family': 'monospace', color: '#ccc' }}>Y{buyYes()}/N{buyNo()}</b></span>
        <span style={itemSx}><span style={{ color: dim }}>已实现</span>
          <b class={realized() >= 0 ? 'pnl-pos' : 'pnl-neg'} style={{ 'font-family': 'monospace' }}>
            {realized() >= 0 ? '+' : ''}{realized().toFixed(2)}</b></span>
        <Show when={bias() > 0.05}>
          <span style={{ color: bad, 'font-size': '10px' }}>← 模型系统性高估, 先去偏/中心化残差</span>
        </Show>
      </div>
    </Show>
  );
}

// ============================================================
// TradingPage (顶层导出)
// ============================================================

// ============================================================
// 全局健康栏 (老板 2026-06-05「盯盘人 5 秒内要答: 系统活着吗? 有没有仓需关注?」)
//   页顶常驻一屏概览: 后端/WSS 灯 + 信号/持仓/浮盈 + 异常置顶聚合。异常自己跳出来找人, 不用逐盘翻。
//   全部基于现有 state 全局可算 (零新增请求); 持仓级异常用 summary.sharp + getSharpTrend (全市场可得)。
// ============================================================
function GlobalHealthBar() {
  const [open, setOpen] = createSignal(false);
  const backendOk = () => state.healthz?.ok === true;
  const wssOk = () => state.status?.wss_connected?.clob === true;
  const sseMs = () => { uiNow(); return sseAgeMs(); };  // SSE 管道存活年龄 (配 uiNow 每秒重算)
  const signals = () => state.status?.signals_active_count ?? 0;
  const positions = () => state.positions?.positions?.filter((p) => Math.abs(Number(p.net_qty)) > 0) ?? [];
  const floatPnl = () => positions().reduce((a, p) => a + Number(p.pnl_realized) + Number(p.pnl_unrealized), 0);
  const mktName = (cid: string) => state.conditionCache[cid]?.summary?.title || cid.slice(0, 8);

  // 异常扫描 (全局可算): 后端/WSS 断 + 持仓 sharp 反向 + 持仓发散 + 资金不足拒单。
  const anomalies = () => {
    uiNow();  // 趋势/年龄随 1s 时钟刷新
    const out: Array<{ sev: 'err' | 'warn'; text: string }> = [];
    if (state.healthz && !backendOk()) out.push({ sev: 'err', text: '后端离线 · 数据停更' });
    if (state.status && !wssOk()) out.push({ sev: 'err', text: '订单簿 WSS 断连 · 价可能过期' });
    for (const p of positions()) {
      const cid = p.market_id;
      const sy = state.conditionCache[cid]?.summary?.sharp;  // YES sharp
      const entry = Number(p.avg_entry_price);
      if (sy != null && sy > 0 && sy < 1 && Number.isFinite(entry)) {
        const sSide = p.outcome === 'YES' ? sy : 1 - sy;        // 本边 sharp
        const dist = sSide - entry;                              // <0 = sharp 已跌破入场 = 持仓亏向
        if (dist < -0.02) out.push({ sev: 'warn', text: `${mktName(cid)} ${p.outcome} · sharp 已反向 ${(dist * 100).toFixed(1)}点` });
      }
      // 发散告警: 优先服务端 conv_rate (SharpFairTrack, 持久权威), 回退前端自攒环。
      const sq = state.conditionCache[cid]?.quote;
      const srvN = Number(sq?.sharp_samples ?? 0);
      const srvC = Number(sq?.sharp_conv_rate ?? Number.NaN);
      if (srvN >= 2 && Number.isFinite(srvC)) {
        if (srvC > 1e-4) out.push({ sev: 'warn', text: `${mktName(cid)} · 持仓发散中 (市场远离 sharp)` });
      } else {
        const ring = getSharpTrend(cid);
        if (ring.length >= 3) {
          const now = ring[ring.length - 1], past = ring[Math.max(0, ring.length - 6)];
          const gNow = Math.abs(now.sharp - now.mid), gPast = Math.abs(past.sharp - past.mid);
          if (gNow > gPast * 1.3 && gNow > 0.01) out.push({ sev: 'warn', text: `${mktName(cid)} · 持仓发散中 (市场远离 sharp)` });
        }
      }
    }
    const insf = (state.rejects?.rejects ?? []).filter((r) => r.reason_code === 'INSUFFICIENT_FUNDS').length;
    if (insf > 0) out.push({ sev: 'warn', text: `资金不足拒单 ×${insf}` });
    return out;
  };
  const errCount = () => anomalies().filter((a) => a.sev === 'err').length;

  return (
    <>
      <div class="v8-health-bar">
        <span class={`v8-health-dot ${backendOk() ? 'hd-ok' : 'hd-err'}`} title="后端心跳 /healthz">● 后端</span>
        <span class={`v8-health-dot ${wssOk() ? 'hd-ok' : 'hd-err'}`} title="订单簿 WSS (clob) 连接">● WSS</span>
        {/* SSE 管道实时灯 (老板 2026-06-05「飘」根治: 真·管道是否实时, 亚秒稳定; 区别于静市场会飘的"订单簿版本年龄") */}
        <span class={`v8-health-dot ${sseIsAlive() && (sseMs() < 5000 || !Number.isFinite(sseMs())) ? 'hd-ok' : sseMs() < 12000 ? 'hd-warn' : 'hd-err'}`}
              title="SSE 数据管道实时性 = now − 最近一帧到达。健康连接每 1-2s 有帧→亚秒稳定。这是「管道是否实时」的真指标 (149hz 订单簿/赔率推送是否在流); 不要和下面会飘的「订单簿版本年龄」(=距上次簿变化) 混淆。">
          ● SSE {sseIsAlive() && Number.isFinite(sseMs()) ? `${(sseMs() / 1000).toFixed(1)}s` : (sseIsAlive() ? '实时' : '回退')}
        </span>
        <span class="v8-health-sep">·</span>
        <span class="mono-sub" title="活跃信号数">信号 {signals()}</span>
        <span class="v8-health-sep">·</span>
        <span class="mono-sub" title="有持仓盘口数 + 合计浮盈亏">
          持仓 {positions().length} · 浮盈 <b class={floatPnl() >= 0 ? 'pnl-pos' : 'pnl-neg'}>{floatPnl() >= 0 ? '+' : ''}${floatPnl().toFixed(1)}</b>
        </span>
        <span class="v8-health-spacer" />
        <Show when={anomalies().length > 0} fallback={<span class="v8-health-ok">✓ 无异常</span>}>
          <span class={`v8-health-alert ${errCount() > 0 ? 'hd-err' : 'hd-warn'}`}
                onClick={() => setOpen((v) => !v)} role="button" tabIndex={0}
                title="点击展开/收起异常列表">
            ⚠ {anomalies().length} 需关注 {open() ? '▲' : '▼'}
          </span>
        </Show>
      </div>
      <Show when={open() && anomalies().length > 0}>
        <div class="v8-health-list">
          <For each={anomalies().slice(0, 12)}>
            {(a) => <div class={`v8-health-item ${a.sev === 'err' ? 'hd-err' : 'hd-warn'}`}>{a.sev === 'err' ? '🔴' : '🟠'} {a.text}</div>}
          </For>
        </div>
      </Show>
    </>
  );
}

export function TradingPage() {
  // 默认只显示「正在比赛」(gamma live=true); 可切「全部/持仓」(老板 2026-06-01)
  const [filter, setFilter] = createSignal<FilterMode>('live');
  const [search, setSearch] = createSignal('');
  // 隐藏无订单簿的盘 (默认开): 纯显示过滤。后端始终订阅 + 每 45s REST 重打底全部已发现盘,
  //   某盘一旦挂上单 → book_found 翻 true → SSE grid 通道推达 → summary.bid/ask 非空 → 该行
  //   自动重新出现 (≤1s)。所以"隐藏" ≠ "停止跟踪", 不会漏掉后来才有簿的盘。有持仓盘永不隐藏。
  const [hideNoBook, setHideNoBook] = createSignal(true);

  // P1-6: WSS 连接状态 Alert 计算.
  //   只有 clob 是真用的 WSS (订单簿). user_channel 仅 live 真单订阅 (paper 不用) — false 预期, 不告警。
  //   (sports_api 通道已删 2026-06-02 — Goalserve 走 HTTP REST inplay feed, 非 WSS)
  const wssStatus = () => state.status?.wss_connected ?? null;
  const wssAllDown = () => {
    const w = wssStatus();
    if (!w) return false; // 后端未连接时 StatusBar 已有"后端离线"提示，不重复
    return !w.clob; // 仅 clob 断 = 真订单簿断连 (报警有意义)
  };
  const wssPartialDown = () => {
    const w = wssStatus();
    if (!w) return false;
    const vals = [w.clob]; // 仅 clob 计入 (user_channel 未用, 不误报)
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
      // 只显示【真正在打】= Goalserve 比分 status=inplay/halftime (有真比分)。2026-06-03 老板:
      //   "很多看不到比分" —— 因 PM 的 g.live 过度包含 (把未开赛的 ITF/cricket 也标 live, Goalserve
      //   实为 Not Started, 无比分)。不再用 g.live, 改要求确认有 in-play 比分 → 进行中只剩真在打+有比分。
      //   game_state=inplay 兜底 (matched 但 score 状态未及更新时)。看全部用「全部」过滤。
      groups = groups.filter((g) => {
        if (g.score?.status === 'inplay' || g.score?.status === 'halftime') return true;
        // 兜底: 后端 game_state 明确 inplay (有比分链路但 score chip 未及刷新)
        return g.conditions.some((c) => c.summary?.gameState === 'inplay');
      });
    } else if (f === 'sourced') {
      // 有直播源: 只看 Goalserve 提供实时比分的盘 (g.score 在打) — 真有比分可观测/可做 in-play (老板 2026-06-03)。
      //   与「进行中」区别: 进行中含 game_state=inplay 但无 Goalserve 比分的 (显"未覆盖"); 有直播源排除那些。
      groups = groups.filter((g) => g.score?.status === 'inplay' || g.score?.status === 'halftime');
    } else if (f === 'position') {
      // 只看持仓: 过滤到【有持仓的盘口】本身 (不只是有持仓的 event); 组内无持仓的盘也隐藏 (老板 2026-06-03)。
      groups = groups
        .map((g) => ({ ...g, conditions: g.conditions.filter((c) => c.posRows.length > 0) }))
        .filter((g) => g.conditions.length > 0);
    }
    // 隐藏无簿盘 (condition 级, 纯显示): 保留「有 bid 或 ask (含单边簿)」或「有持仓」的盘;
    //   两者皆无 = 当前无订单簿 → 隐藏。后端仍跟踪, 有簿经 SSE 自动现身 (见 hideNoBook 注释)。
    if (hideNoBook()) {
      groups = groups
        .map((g) => ({
          ...g,
          conditions: g.conditions.filter(
            (c) => c.summary?.bid != null || c.summary?.ask != null || c.posRows.length > 0,
          ),
        }))
        .filter((g) => g.conditions.length > 0);
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

  // 全展可展开集 (2026-06-02 老板「全展没反应」修): 只展「有订单簿」的盘 + 封顶 32。
  //   原因: 全展 282 盘 → 157 订单簿 + 89 无簿占位 撑爆 DOM 卡死; 且 SSE 全档 focus 上限 32,
  //   超出的盘推不到实时全档。无簿盘没书可展(展了也"未接入")。故只展有簿的前 32 个。
  const EXPAND_CAP = 32;
  const expandableCondIds = () =>
    filteredGroups()
      .flatMap((g) => g.conditions)
      .filter((c) => c.summary && (c.summary.bid != null || c.summary.ask != null))
      .map((c) => c.conditionId)
      .slice(0, EXPAND_CAP);

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

      {/* 全局健康栏 (老板 2026-06-05): 一屏概览 + 异常置顶 */}
      <GlobalHealthBar />

      <TradingToolbar
        filter={filter()}
        search={search()}
        onFilter={setFilter}
        onSearch={setSearch}
        visibleCount={filteredGroups().length}
        totalCount={allGroups().length}
        totalMarkets={totalMarkets()}
        hideNoBook={hideNoBook()}
        onToggleNoBook={() => setHideNoBook((v) => !v)}
        onExpandAll={() => expandAllMarkets(expandableCondIds())}
        onCollapseAll={() => collapseAllMarkets(allCondIds())}
      />

      {/* 模型诊断条 (老板「在盯盘页面本身做好, 不要分散」) — 调模型一眼看, 不用进别的页 */}
      <ModelBiasStrip />

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
