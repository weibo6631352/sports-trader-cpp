/**
 * TradingPage.tsx — 盯盘页 (v6)
 * owner: 小苏  last_review: 2026-05-29
 *
 * v6 增强:
 *  - 筛选栏: 全部 / 进行中 / 有持仓 + 搜索框
 *  - 订单簿: 3→5 档 + 深度条 DepthBar
 *  - 量化: 置信度 ConfBar + CI 区间主显示行
 *  - 赛事头: per-event staleness + LIVE 角标
 *  - 保留 v5 认可的 Event→Condition→DualBook 三层结构不动
 *  - 保留 DEMO/advisory 标记 (ADR-041)
 *
 * 组件树:
 *   TradingPage
 *     TradingToolbar  (筛选栏)
 *     PnlSparkline    (常驻, 不折叠)
 *     EventGrid_v6
 *       EventGroupCard_v6
 *         EventHeader_v6
 *         ConditionColumn_v6
 *           CondQuote_v6  (含 ConfBar + CI 行)
 *           DualBook_v6
 *             MiniHalfBook_v6  (5 档 + DepthBar)
 *           CondPos
 */

import { createSignal, For, Show } from 'solid-js';
import { state } from '../store';
import {
  fmtTs, fmtBps, fmtUsdc, fmtClock, stalenessMs, isEndpointFailing,
} from '../api';
import {
  STATUS_ZH, SPORT_ZH, REJECT_REASON_ZH, SIDE_ZH, inferMarketLabel,
} from '../i18n';
import type {
  EventGroup, ConditionData, BinaryMarketBookView, HalfBook,
  Quote, Position, RiskReject,
} from '../types';
import { PnlSparkline } from './PnlSparkline';
import { StatusDot, wssStateToDot } from './ui/StatusDot';
import { ConfBar } from './ui/ConfBar';
import { DepthBar } from './ui/DepthBar';

// ============================================================
// 小工具
// ============================================================

function RejectDot(props: { rejectRows: RiskReject[] }) {
  const has = () => props.rejectRows.length > 0;
  const tooltip = () =>
    props.rejectRows
      .map((r) => {
        const rz   = REJECT_REASON_ZH[r.reason_code] ?? r.reason_code;
        const side = SIDE_ZH[r.side] ?? r.side;
        return `${rz} · ${side} ${r.size} @${r.price}`;
      })
      .join('\n');
  return (
    <Show when={has()}>
      <span class="reject-dot" title={tooltip()}>
        ×{props.rejectRows.length}
      </span>
    </Show>
  );
}

// ============================================================
// EventHeader_v6 (含 per-event staleness + LIVE 角标)
// ============================================================

function EventHeader_v6(props: { group: EventGroup }) {
  const score      = () => props.group.score;
  const conditions = () => props.group.conditions;
  const firstMkt   = () => conditions().find((c) => c.market)?.market ?? null;
  const isDemoData = () => conditions().some((c) => c.isDemoData);
  const sport      = () => score()?.sport ?? null;
  const sportZh    = () => (sport() ? (SPORT_ZH[sport()!] ?? sport()) : '');
  const eventUrl   = () => firstMkt()?.polymarket_url ?? null;
  const eventIdStr = () => score()?.event_id ?? firstMkt()?.event_id ?? '—';

  const statusZh  = () => STATUS_ZH[score()?.status ?? ''] ?? score()?.status ?? '?';
  const statusCls = () => {
    const st = score()?.status;
    if (st === 'inplay')   return 'score-live';
    if (st === 'halftime') return 'score-ht';
    if (st === 'final')    return 'score-ft';
    return 'score-pre';
  };

  const isLive = () => score()?.status === 'inplay';

  // per-event staleness: 取各条件 book staleness 最大值
  const maxStaleMs = () => {
    const vals = conditions()
      .map((c) => c.book ? stalenessMs(c.book.as_of_ts_ns ?? c.book.token0?.book_as_of_ts) : null)
      .filter((v): v is number => v != null);
    if (vals.length === 0) return null;
    return Math.max(...vals);
  };

  const staleCls = () => {
    const ms = maxStaleMs();
    if (ms == null)  return '';
    if (ms < 2000)   return '';
    if (ms < 10000)  return 'evt-staleness-warn';
    return 'evt-staleness-err';
  };

  const staleText = () => {
    const ms = maxStaleMs();
    if (ms == null) return null;
    if (ms < 1000)  return `延迟 ${Math.round(ms)}ms`;
    return `延迟 ${(ms / 1000).toFixed(1)}s`;
  };

  return (
    <div class="event-header">
      <div class="event-header-main">
        <Show when={sportZh()}>
          <span class="evt-sport-tag">{sportZh()}</span>
        </Show>

        {/* LIVE 角标 */}
        <Show when={isLive()}>
          <span class="live-badge">LIVE</span>
        </Show>

        <Show
          when={score()}
          fallback={<span class="evt-teams-placeholder ph">—</span>}
        >
          {(sc) => (
            <>
              <span class="evt-team">{sc().home}</span>
              <span class="evt-score">{sc().home_score ?? '—'}</span>
              <span class="evt-dash">—</span>
              <span class="evt-score">{sc().away_score ?? '—'}</span>
              <span class="evt-team">{sc().away}</span>
              <span class="evt-sep">|</span>
              <span class="evt-period">{sc().period ?? '—'}</span>
              <Show when={sc().clock_sec != null}>
                <span class="evt-clock">{fmtClock(sc().clock_sec!)}</span>
              </Show>
              <span class={`evt-status ${statusCls()}`}>{statusZh()}</span>
            </>
          )}
        </Show>

        <Show when={eventUrl()}>
          <a
            class="evt-link"
            href={eventUrl()!}
            target="_blank"
            rel="noopener noreferrer"
            title={eventIdStr()}
          >
            Polymarket
          </a>
        </Show>

        <Show when={isDemoData()}>
          <span class="demo-chip" title="演示数据·非实盘">DEMO</span>
        </Show>

        {/* per-event staleness */}
        <Show when={staleText()}>
          <span class={`evt-staleness ${staleCls()}`}>{staleText()}</span>
        </Show>
      </div>
    </div>
  );
}

// ============================================================
// CondQuote_v6 (含 ConfBar + CI 区间主显示行)
// XD-1/3/4/5 红线保持
// ============================================================

function CondQuote_v6(props: { quote: Quote | null; isDemoData: boolean }) {
  const q = () => props.quote;

  return (
    <Show
      when={q()}
      fallback={
        <div class="cond-quote-empty">
          <span class="ph">量化未接入</span>
          <Show when={props.isDemoData}>
            <span class="demo-chip" title="演示数据·非实盘">demo</span>
          </Show>
        </div>
      }
    >
      {(quote) => {
        const fairValue   = () => Number(quote().fair_value);
        const marketMid   = () => Number(quote().market_mid);
        const edgeBps     = () => Number(quote().edge_bps);
        const kelly       = () => Number(quote().kelly_fraction);
        const notional    = () => Number(quote().suggested_notional);
        const signalStr   = () => Number(quote().signal_strength);
        const modelConf   = () => Number(quote().model_confidence ?? quote().model_conf);
        const modelId     = () => quote().model_id ?? '—';
        const modelKind   = () => quote().model_kind ?? '—';
        const calibrated  = () => quote().model_calibrated !== false;
        const predictOk   = () => quote().predict_ok !== false;
        const advisory    = () => quote().advisory === true;
        const ciLower     = () => quote().fair_ci_lower;
        const ciUpper     = () => quote().fair_ci_upper;
        const hasCi       = () => Number.isFinite(ciLower()) && Number.isFinite(ciUpper());
        const edgePositive = () => fairValue() >= marketMid();
        const edgeCls     = () => (edgePositive() ? 'edge-pos' : 'edge-neg');
        const edgeBarPct  = () => `${Math.min(Math.abs(edgeBps()) / 100, 1) * 100}%`;
        const kellyPositive = () => Number.isFinite(kelly()) && kelly() > 0;
        const kellyCls    = () => (kellyPositive() ? 'kelly-pos' : 'kelly-zero');
        const uncalibCls  = () => (calibrated() ? '' : ' q-uncalibrated');

        return (
          <>
            {/* XD-3: advisory 角标 */}
            <Show when={advisory()}>
              <div class="advisory-banner" title="模型当前仅供参考，系统不会自动下单">
                <span class="advisory-icon">!</span>
                仅供参考·不下单
              </div>
            </Show>

            {/* Row 1: 公允价 / 市场中间价 */}
            <div class={`cond-quote-row${uncalibCls()}`}>
              <span class="q-lbl">公允</span>
              <span class="q-fair mono-main">
                {Number.isFinite(fairValue()) ? fairValue().toFixed(4) : '—'}
              </span>
              <Show when={!calibrated()}>
                <span class="uncalib-chip" title="模型尚未完成校准，数值仅供参考">未校准</span>
              </Show>
              <span class="q-lbl">市场</span>
              <span class="q-mid mono-sub">
                {Number.isFinite(marketMid()) ? marketMid().toFixed(4) : '—'}
              </span>
              <Show when={props.isDemoData}>
                <span class="demo-chip" title="演示数据·非实盘">demo</span>
              </Show>
            </div>

            {/* Row 2: CI 区间 主显示 (v6 升格为独立行) */}
            <Show when={hasCi()}>
              <div class="cond-ci-row">
                <span class="q-ci-label">CI</span>
                <span class="q-ci-main">
                  [{ciLower()!.toFixed(3)}–{ciUpper()!.toFixed(3)}]
                </span>
              </div>
            </Show>

            {/* Row 3: AI provenance 行 */}
            <div class={`cond-prov-row${uncalibCls()}`}>
              <span class="q-lbl">模型</span>
              <span
                class="q-model-id mono-sub"
                title={`${modelId()} · ${modelKind()} · ${quote().spec_version ?? '—'}`}
              >
                {modelId()}
              </span>
              <span class="q-prov-sep">|</span>
              <span class="q-lbl">置信</span>
              {/* v6: ConfBar 替换数字 */}
              <ConfBar value={modelConf()} />
            </div>

            {/* XD-5: predict_ok=false → 不画 edge/kelly/notional */}
            <Show
              when={predictOk()}
              fallback={
                <div class="predict-fail-row">
                  <span class="predict-fail-chip" title="模型预测异常，sizing 数据不可用">
                    预测异常
                  </span>
                  <span class="q-lbl">edge/kelly/额度 暂不可用</span>
                </div>
              }
            >
              {/* Row 4: 优势 edge bar + 信号 */}
              <div class="cond-edge-row">
                <span class="q-lbl">优势</span>
                <div class="edge-track-sm">
                  <div class={`edge-fill-sm ${edgeCls()}`} style={{ width: edgeBarPct() }} />
                </div>
                <span class={`q-edge ${edgeCls()}`}>{fmtBps(edgeBps())}</span>
                <span class="q-lbl">信号</span>
                <span class="q-sig mono-sub">
                  {Number.isFinite(signalStr()) ? signalStr().toFixed(2) : '—'}
                </span>
              </div>

              {/* Row 5: Kelly + 建议额度 */}
              <div class="cond-kelly-row">
                <span class="q-lbl">Kelly</span>
                <span class={`q-kelly ${kellyCls()} mono-main`}>
                  {Number.isFinite(kelly()) ? `${(kelly() * 100).toFixed(1)}%` : '—'}
                </span>
                <span class="q-lbl">额</span>
                <span class="q-notional mono-sub">
                  ${Number.isFinite(notional())
                    ? notional().toLocaleString('en-US', { minimumFractionDigits: 0, maximumFractionDigits: 0 })
                    : '—'}
                </span>
              </div>
            </Show>
          </>
        );
      }}
    </Show>
  );
}

// ============================================================
// MiniHalfBook_v6 (5 档 + DepthBar)
// ============================================================

function MiniHalfBook_v6(props: { half: HalfBook }) {
  const h = () => props.half;

  const bid      = () => Number(h().best_bid);
  const ask      = () => Number(h().best_ask);
  const spread   = () => Number(h().spread);
  const imbalance = () => Number(h().imbalance);
  const imbPct   = () =>
    Number.isFinite(imbalance())
      ? `${((imbalance() + 1) / 2 * 100).toFixed(1)}%`
      : '50%';
  const imbNum   = () =>
    Number.isFinite(imbalance()) ? imbalance().toFixed(2) : '—';

  // v6: 5 档 (up from 3)
  const bids = () => (h().bids ?? []).slice(0, 5);
  const asks = () => (h().asks ?? []).slice(0, 5);
  const maxLen   = () => Math.max(bids().length, asks().length);
  const depthIdx = () => Array.from({ length: maxLen() }, (_, i) => i);

  // 最大量 (用于深度条归一化)
  const maxBidSize = () => Math.max(...bids().map((b) => Number(b.size)), 1);
  const maxAskSize = () => Math.max(...asks().map((a) => Number(a.size)), 1);
  const maxSize    = () => Math.max(maxBidSize(), maxAskSize());

  const wssState = () => h().wss_state ?? 'unknown';

  return (
    <div class="mini-half">
      {/* 标题行 */}
      <div class="mini-half-header">
        <span class="mini-outcome">{h().outcome ?? '—'}</span>
        <StatusDot state={wssStateToDot(wssState())} size="sm" title={`WSS: ${wssState()}`} />
        <Show when={(h().gap_count ?? 0) > 0}>
          <span class="gap-dot" title={`gap ${h().gap_count}`} />
        </Show>
      </div>

      {/* Best bid / ask */}
      <div class="mini-ba-row">
        <span class="mini-bid mono-main">
          {Number.isFinite(bid()) ? bid().toFixed(4) : '—'}
        </span>
        <span class="mini-ba-sep">|</span>
        <span class="mini-ask mono-main">
          {Number.isFinite(ask()) ? ask().toFixed(4) : '—'}
        </span>
      </div>

      {/* 价差 + imbalance 条 + 数值 */}
      <div class="mini-spread-row">
        <span class="q-lbl">价差</span>
        <span class="mono-sub">
          {Number.isFinite(spread()) ? `${(spread() * 100).toFixed(2)}%` : '—'}
        </span>
        <div
          class="mini-imb-track"
          title={`失衡 ${imbNum()}`}
        >
          <div class="mini-imb-fill" style={{ width: imbPct() }} />
        </div>
        <span class="mini-imb-val">{imbNum()}</span>
      </div>

      {/* v6: 5 档深度 + 深度条 */}
      <div class="mini-depth">
        <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr', gap: '1px' }}>
          <span class="mini-col-lbl" style={{ 'text-align': 'right' }}>量/买</span>
          <span class="mini-col-lbl" style={{ 'text-align': 'left' }}>卖/量</span>
        </div>

        <For each={depthIdx()}>
          {(i) => {
            const b = () => bids()[i];
            const a = () => asks()[i];
            return (
              <div style={{ display: 'flex', gap: '4px', 'margin-bottom': '2px', 'align-items': 'center' }}>
                {/* 买单: 深度条 + 价格 */}
                <div style={{ flex: '1', display: 'flex', 'flex-direction': 'column', gap: '1px' }}>
                  <div style={{ display: 'flex', gap: '3px', 'align-items': 'center', 'justify-content': 'flex-end' }}>
                    <span class="mini-bid-size" style={{ 'font-size': '9px' }}>
                      {b() ? Number(b().size).toLocaleString() : ''}
                    </span>
                    <span class="mini-bid-px">
                      {b() ? Number(b().price).toFixed(4) : ''}
                    </span>
                  </div>
                  <Show when={b()}>
                    <DepthBar size={Number(b().size)} maxSize={maxSize()} side="bid" />
                  </Show>
                </div>

                <span style={{ color: 'var(--border)', 'font-size': '10px' }}>|</span>

                {/* 卖单: 价格 + 深度条 */}
                <div style={{ flex: '1', display: 'flex', 'flex-direction': 'column', gap: '1px' }}>
                  <div style={{ display: 'flex', gap: '3px', 'align-items': 'center' }}>
                    <span class="mini-ask-px">
                      {a() ? Number(a().price).toFixed(4) : ''}
                    </span>
                    <span class="mini-ask-size" style={{ 'font-size': '9px' }}>
                      {a() ? Number(a().size).toLocaleString() : ''}
                    </span>
                  </div>
                  <Show when={a()}>
                    <DepthBar size={Number(a().size)} maxSize={maxSize()} side="ask" />
                  </Show>
                </div>
              </div>
            );
          }}
        </For>
      </div>

      {/* seq */}
      <div class="mini-seq-row">
        <span class="q-lbl">seq</span>
        <span class="mono-sub">{h().sequence_no ?? '—'}</span>
      </div>
    </div>
  );
}

// ============================================================
// DualBook_v6
// ============================================================

function DualBook_v6(props: { book: BinaryMarketBookView | null; conditionId: string }) {
  const book = () => props.book;

  const vigInfo = () => {
    const cs = Number(book()?.cross_spread);
    if (!Number.isFinite(cs)) return null;
    const cls = cs < 0.02 ? 'vig-low' : cs < 0.04 ? 'vig-mid' : 'vig-high';
    return { text: `${(cs * 100).toFixed(2)}%`, cls };
  };

  return (
    <Show
      when={book()}
      fallback={
        <Show
          when={isEndpointFailing(`/api/v1/book/${props.conditionId}`)}
          fallback={
            <div class="cond-book-empty">
              <span class="ph">订单簿未接入</span>
            </div>
          }
        >
          <div class="cond-book-fail">
            <span class="fail-chip">订单簿拉取失败</span>
          </div>
        </Show>
      }
    >
      {(bk) => (
        <>
          <div class="cond-book-header">
            <span class="cond-book-label">双边订单簿</span>
            <Show when={vigInfo()}>
              {(vi) => (
                <span class={`vig-badge-sm ${vi().cls}`} title="vig (ask0+ask1-1)">
                  {vi().text}
                </span>
              )}
            </Show>
          </div>
          <div class="cond-dual-grid">
            <MiniHalfBook_v6 half={bk().token0} />
            <MiniHalfBook_v6 half={bk().token1} />
          </div>
        </>
      )}
    </Show>
  );
}

// ============================================================
// CondPos (与 v5 相同, 持仓区保持)
// ============================================================

function CondPos(props: { posRows: Position[]; perMarketPnl: number | null }) {
  const pnlStr = () => {
    const v = props.perMarketPnl;
    if (v == null) return null;
    return { text: fmtUsdc(v), cls: v >= 0 ? 'pnl-pos mono-main' : 'pnl-neg mono-main' };
  };

  return (
    <Show
      when={props.posRows.length > 0}
      fallback={
        <div class="cond-pos-header">
          <span class="q-lbl">持仓</span>
          <span class="ph">—</span>
          <span class="q-lbl">PnL</span>
          <Show when={pnlStr()} fallback={<span class="ph">—</span>}>
            {(ps) => <span class={ps().cls}>{ps().text}</span>}
          </Show>
        </div>
      }
    >
      <>
        <div class="cond-pos-header">
          <span class="q-lbl">持仓/PnL</span>
          <Show when={pnlStr()} fallback={<span class="ph">—</span>}>
            {(ps) => <span class={ps().cls}>{ps().text}</span>}
          </Show>
        </div>
        <For each={props.posRows}>
          {(p) => {
            const netQty   = () => Number(p.net_qty);
            const mark     = () => Number(p.mark_price);
            const pnlTotal = () => Number(p.pnl_realized) + Number(p.pnl_unrealized);
            const pnlCls   = () => (pnlTotal() >= 0 ? 'pnl-pos' : 'pnl-neg');
            const qtySign  = () => (netQty() >= 0 ? '+' : '');
            return (
              <div class="cond-pos-row">
                <span class="cond-pos-outcome">{p.outcome ?? '—'}</span>
                <span class="cond-pos-qty mono-sub">
                  {qtySign()}{netQty().toLocaleString()}u
                </span>
                <span class="cond-pos-mark mono-sub">
                  {Number.isFinite(mark()) ? mark().toFixed(4) : '—'}
                </span>
                <span class={`cond-pos-pnl ${pnlCls()} mono-main`}>
                  {fmtUsdc(pnlTotal())}
                </span>
              </div>
            );
          }}
        </For>
      </>
    </Show>
  );
}

// ============================================================
// ConditionColumn_v6
// ============================================================

function ConditionColumn_v6(props: { cond: ConditionData }) {
  const c        = () => props.cond;
  const condId   = () => c().market?.condition_id ?? c().conditionId;
  const mktLabel = () => inferMarketLabel(condId(), c().market);
  const inactive = () => c().market != null && !c().market!.accepting_orders;

  return (
    <div
      class={`cond-col${inactive() ? ' cond-col-inactive' : ''}`}
      data-condition={condId()}
    >
      <div class="cond-col-pos-anchor">
        <RejectDot rejectRows={c().rejectRows} />
      </div>

      <div class="cond-header">
        <span class="cond-type-label">{mktLabel()}</span>
        <Show when={c().market}>
          {(mkt) => (
            <span
              class={`acc-dot ${mkt().accepting_orders ? 'acc-dot-ok' : 'acc-dot-off'}`}
              title={mkt().accepting_orders ? '接单中' : '不接单'}
            />
          )}
        </Show>
        <Show when={c().market?.neg_risk}>
          <span class="neg-risk-tag" title="neg_risk">NR</span>
        </Show>
      </div>

      <div class="cond-quote-section">
        <CondQuote_v6 quote={c().quote} isDemoData={c().isDemoData} />
      </div>

      <div class="cond-book-section">
        <DualBook_v6 book={c().book} conditionId={condId()} />
      </div>

      <div class="cond-pos-section">
        <CondPos posRows={c().posRows} perMarketPnl={c().perMarketPnl} />
      </div>
    </div>
  );
}

// ============================================================
// EventGroupCard_v6
// ============================================================

function EventGroupCard_v6(props: { group: EventGroup }) {
  const allInactive = () =>
    props.group.conditions.every((c) => c.market != null && !c.market!.accepting_orders);

  return (
    <div
      class={`event-group${allInactive() ? ' event-group-inactive' : ''}`}
      data-event={props.group.eventId ?? ''}
    >
      <EventHeader_v6 group={props.group} />
      <div class="event-columns">
        <For each={props.group.conditions}>
          {(cond) => <ConditionColumn_v6 cond={cond} />}
        </For>
      </div>
    </div>
  );
}

// ============================================================
// TradingToolbar (筛选栏)
// ============================================================

type FilterMode = 'all' | 'live' | 'position';

interface TradingToolbarProps {
  filter: FilterMode;
  search: string;
  onFilter: (f: FilterMode) => void;
  onSearch: (s: string) => void;
  visibleCount: number;
  totalCount: number;
}

function TradingToolbar(props: TradingToolbarProps) {
  const filters: { key: FilterMode; label: string }[] = [
    { key: 'all',      label: '全部' },
    { key: 'live',     label: '进行中' },
    { key: 'position', label: '有持仓' },
  ];

  return (
    <div class="trading-toolbar">
      <div class="filter-btn-group">
        <For each={filters}>
          {(f) => (
            <button
              class={`filter-btn${props.filter === f.key ? ' filter-active' : ''}`}
              onClick={() => props.onFilter(f.key)}
              type="button"
            >
              {f.label}
            </button>
          )}
        </For>
      </div>

      <input
        class="search-input"
        type="text"
        placeholder="搜索赛事..."
        value={props.search}
        onInput={(e) => props.onSearch(e.currentTarget.value)}
      />

      <span class="toolbar-count">
        显示 {props.visibleCount} / {props.totalCount}
      </span>
    </div>
  );
}

// ============================================================
// TradingPage (顶层导出)
// ============================================================

export function TradingPage() {
  const [filter, setFilter] = createSignal<FilterMode>('all');
  const [search, setSearch] = createSignal('');

  const allGroups = () => state.eventGroups;

  const filteredGroups = () => {
    let groups = allGroups();

    // 筛选
    const f = filter();
    if (f === 'live') {
      groups = groups.filter((g) => g.score?.status === 'inplay' || g.score?.status === 'halftime');
    } else if (f === 'position') {
      groups = groups.filter((g) => g.conditions.some((c) => c.posRows.length > 0));
    }

    // 搜索 (本地 filter, 不发请求)
    const q = search().trim().toLowerCase();
    if (q) {
      groups = groups.filter((g) => {
        const sc = g.score;
        if (sc) {
          if (sc.home?.toLowerCase().includes(q)) return true;
          if (sc.away?.toLowerCase().includes(q)) return true;
          if (sc.event_id?.toLowerCase().includes(q)) return true;
        }
        if (g.eventId?.toLowerCase().includes(q)) return true;
        return g.conditions.some((c) =>
          c.market?.slug?.toLowerCase().includes(q) ||
          c.conditionId?.toLowerCase().includes(q),
        );
      });
    }

    return groups;
  };

  return (
    <div>
      <TradingToolbar
        filter={filter()}
        search={search()}
        onFilter={setFilter}
        onSearch={setSearch}
        visibleCount={filteredGroups().length}
        totalCount={allGroups().length}
      />

      <div style={{ padding: '6px 10px 4px', 'border-bottom': '1px solid var(--border)', background: 'var(--bg2)' }}>
        <PnlSparkline />
      </div>

      <div id="market-grid" style={{ padding: '10px' }}>
        <Show
          when={filteredGroups().length > 0}
          fallback={
            <div class="no-data grid-placeholder">
              {state.positions == null
                ? '加载市场数据...'
                : filter() !== 'all'
                  ? '该筛选条件下无赛事'
                  : '等待持仓建立 / 后端未接入 · 点 ⚙ 检查'}
            </div>
          }
        >
          <For each={filteredGroups()}>
            {(group) => <EventGroupCard_v6 group={group} />}
          </For>
        </Show>
      </div>
    </div>
  );
}
