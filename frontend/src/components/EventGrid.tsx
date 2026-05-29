/**
 * EventGrid.tsx — 赛事分组网格 (v5 核心布局)
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * 组件树:
 *   EventGrid
 *     EventGroup
 *       EventHeader  (比分/赛况, R1: 只渲染一次)
 *       ConditionColumn[]
 *         CondQuote
 *         DualBook
 *           MiniHalfBook x2
 *         CondPos
 */

import { For, Show } from 'solid-js';
import { state } from '../store';
import {
  fmtTs, fmtBps, fmtUsdc, fmtClock, stalenessMs, isEndpointFailing,
} from '../api';
import {
  STATUS_ZH, SPORT_ZH, REJECT_REASON_ZH, SIDE_ZH,
  inferMarketLabel,
} from '../i18n';
import type {
  EventGroup, ConditionData, BinaryMarketBookView, HalfBook,
  Quote, Position, RiskReject,
} from '../types';

// ============================================================
// 小工具
// ============================================================

function WssDot(props: { state: string }) {
  const ok = () => props.state === 'CONNECTED';
  return (
    <span
      class={`wss-dot-sm ${ok() ? 'wss-dot-ok' : 'wss-dot-off'}`}
      title={`WSS: ${props.state}`}
    />
  );
}

function StaleDot(props: { ms: number | null }) {
  const cls = () => {
    const ms = props.ms;
    if (ms == null) return '';
    if (ms < 2000) return 'stale-dot-ok';
    if (ms < 10000) return 'stale-dot-warn';
    return 'stale-dot-err';
  };
  const label = () => {
    const ms = props.ms;
    if (ms == null) return '';
    return ms < 1000 ? `${Math.round(ms)}ms` : `${(ms / 1000).toFixed(1)}s`;
  };
  return (
    <Show when={props.ms != null}>
      <span class={`stale-dot ${cls()}`} title={`数据延迟 ${label()}`} />
    </Show>
  );
}

// ============================================================
// RejectDot (R5)
// ============================================================

function RejectDot(props: { rejectRows: RiskReject[] }) {
  const has = () => props.rejectRows.length > 0;
  const tooltip = () =>
    props.rejectRows
      .map((r) => {
        const rz = REJECT_REASON_ZH[r.reason_code] ?? r.reason_code;
        const sideZh = SIDE_ZH[r.side] ?? r.side;
        return `${rz} · ${sideZh} ${r.size} @${r.price}`;
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
// EventHeader (R1: 比分只在赛事头)
// ============================================================

function EventHeader(props: { group: EventGroup }) {
  const score = () => props.group.score;
  const conditions = () => props.group.conditions;

  const firstMkt = () => conditions().find((c) => c.market)?.market ?? null;
  const isDemoData = () => conditions().some((c) => c.isDemoData);

  const sport = () => score()?.sport ?? null;
  const sportZh = () => (sport() ? (SPORT_ZH[sport()!] ?? sport()) : '');

  const eventUrl = () => firstMkt()?.polymarket_url ?? null;
  const eventIdStr = () =>
    score()?.event_id ?? firstMkt()?.event_id ?? '—';

  const statusZh = () => STATUS_ZH[score()?.status ?? ''] ?? score()?.status ?? '?';
  const statusCls = () => {
    const st = score()?.status;
    if (st === 'inplay') return 'score-live';
    if (st === 'halftime') return 'score-ht';
    if (st === 'final') return 'score-ft';
    return 'score-pre';
  };

  const bookStaleMsList = () =>
    conditions().map((c) => {
      if (!c.book) return null;
      return stalenessMs(c.book.as_of_ts_ns ?? c.book.token0?.book_as_of_ts);
    });

  const scoreStalems = () =>
    score() ? stalenessMs(score()!.score_as_of_ts) : null;

  return (
    <div class="event-header">
      <div class="event-header-main">
        <Show when={sportZh()}>
          <span class="evt-sport-tag">{sportZh()}</span>
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

        <span class="evt-stale-group">
          <For each={bookStaleMsList()}>
            {(ms) => <StaleDot ms={ms} />}
          </For>
          <StaleDot ms={scoreStalems()} />
        </span>
      </div>
    </div>
  );
}

// ============================================================
// CondQuote — AI provenance + sizing (小邓 XD 红线)
//
// XD-1: fair_value 旁必带 model_confidence + model_id (三位一体)
// XD-3: advisory=true → "仅供参考/不下单" 角标
// XD-4: model_calibrated=false → 灰色降级 + "未校准" 标
// XD-5: predict_ok=false → 不渲染 edge/kelly/notional 区
// ============================================================

function CondQuote(props: { quote: Quote | null; isDemoData: boolean }) {
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
        // --- sizing ---
        const fairValue    = () => Number(quote().fair_value);
        const marketMid    = () => Number(quote().market_mid);
        const edgeBps      = () => Number(quote().edge_bps);
        const kelly        = () => Number(quote().kelly_fraction);
        const notional     = () => Number(quote().suggested_notional);
        const signalStr    = () => Number(quote().signal_strength);

        // --- AI provenance ---
        const modelConf    = () => Number(quote().model_confidence ?? quote().model_conf);
        const modelId      = () => quote().model_id ?? '—';
        const modelKind    = () => quote().model_kind ?? '—';
        const calibrated   = () => quote().model_calibrated !== false;
        const predictOk    = () => quote().predict_ok !== false;
        const advisory     = () => quote().advisory === true;
        const ciLower      = () => quote().fair_ci_lower;
        const ciUpper      = () => quote().fair_ci_upper;
        const hasCi        = () =>
          Number.isFinite(ciLower()) && Number.isFinite(ciUpper());

        // --- 样式 ---
        const edgePositive = () => fairValue() >= marketMid();
        const edgeCls      = () => (edgePositive() ? 'edge-pos' : 'edge-neg');
        const edgeBarPct   = () => `${Math.min(Math.abs(edgeBps()) / 100, 1) * 100}%`;
        const kellyPositive = () => Number.isFinite(kelly()) && kelly() > 0;
        const kellyCls      = () => (kellyPositive() ? 'kelly-pos' : 'kelly-zero');

        // XD-4: 未校准 → 降级 class
        const uncalibCls   = () => (calibrated() ? '' : ' q-uncalibrated');

        // model_confidence 颜色 (≥0.7 green, ≥0.5 yellow, <0.5 red/dim)
        const confCls = () => {
          const c = modelConf();
          if (!Number.isFinite(c)) return 'conf-dim';
          if (c >= 0.7) return 'conf-high';
          if (c >= 0.5) return 'conf-mid';
          return 'conf-low';
        };

        return (
          <>
            {/* XD-3: advisory 角标 */}
            <Show when={advisory()}>
              <div class="advisory-banner" title="模型当前仅供参考，系统不会自动下单">
                <span class="advisory-icon">!</span>
                仅供参考·不下单
              </div>
            </Show>

            {/* Row 1: 公允价 || 市场中间价 — XD-1 三位一体 */}
            <div class={`cond-quote-row${uncalibCls()}`}>
              <span class="q-lbl">公允</span>
              <span class="q-fair mono-main">
                {Number.isFinite(fairValue()) ? fairValue().toFixed(4) : '—'}
              </span>
              {/* XD-4: 未校准标注 (Show 反转: when=!calibrated) */}
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

            {/* Row 2: AI provenance (XD-1 三位一体) */}
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
              <span class={`q-conf mono-sub ${confCls()}`}>
                {Number.isFinite(modelConf()) ? `${(modelConf() * 100).toFixed(0)}%` : '—'}
              </span>
              {/* CI 区间 (可选小字) */}
              <Show when={hasCi()}>
                <span
                  class="q-ci mono-sub"
                  title={`95% CI: [${ciLower()!.toFixed(4)}, ${ciUpper()!.toFixed(4)}]`}
                >
                  [{ciLower()!.toFixed(3)}–{ciUpper()!.toFixed(3)}]
                </span>
              </Show>
            </div>

            {/* XD-5: predict_ok=false → 显示预测异常，不画 edge/kelly/notional */}
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
              {/* Row 3: 优势 edge bar */}
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

              {/* Row 4: Kelly + 建议额度 */}
              <div class="cond-kelly-row">
                <span class="q-lbl">Kelly</span>
                <span class={`q-kelly ${kellyCls()} mono-main`}>
                  {Number.isFinite(kelly()) ? `${(kelly() * 100).toFixed(1)}%` : '—'}
                </span>
                <span class="q-lbl">额</span>
                <span class="q-notional mono-sub">
                  ${Number.isFinite(notional()) ? notional().toLocaleString('en-US', { minimumFractionDigits: 0, maximumFractionDigits: 0 }) : '—'}
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
// MiniHalfBook
// ============================================================

function MiniHalfBook(props: { half: HalfBook }) {
  const h = () => props.half;

  const bid = () => Number(h().best_bid);
  const ask = () => Number(h().best_ask);
  const spread = () => Number(h().spread);
  const imbalance = () => Number(h().imbalance);
  const imbPct = () =>
    Number.isFinite(imbalance())
      ? `${((imbalance() + 1) / 2 * 100).toFixed(1)}%`
      : '50%';

  const bids = () => (h().bids ?? []).slice(0, 3);
  const asks = () => (h().asks ?? []).slice(0, 3);
  const maxLen = () => Math.max(bids().length, asks().length);
  const depthIndices = () => Array.from({ length: maxLen() }, (_, i) => i);

  return (
    <div class="mini-half">
      <div class="mini-half-header">
        <span class="mini-outcome">{h().outcome ?? '—'}</span>
        <WssDot state={h().wss_state ?? 'unknown'} />
        <Show when={(h().gap_count ?? 0) > 0}>
          <span class="gap-dot" title={`gap ${h().gap_count}`} />
        </Show>
      </div>

      <div class="mini-ba-row">
        <span class="mini-bid mono-main">
          {Number.isFinite(bid()) ? bid().toFixed(4) : '—'}
        </span>
        <span class="mini-ba-sep">|</span>
        <span class="mini-ask mono-main">
          {Number.isFinite(ask()) ? ask().toFixed(4) : '—'}
        </span>
      </div>

      <div class="mini-spread-row">
        <span class="q-lbl">价差</span>
        <span class="mono-sub">
          {Number.isFinite(spread()) ? `${(spread() * 100).toFixed(2)}%` : '—'}
        </span>
        <div
          class="mini-imb-track"
          title={`失衡 ${Number.isFinite(imbalance()) ? imbalance().toFixed(3) : '—'}`}
        >
          <div class="mini-imb-fill" style={{ width: imbPct() }} />
        </div>
      </div>

      <div class="mini-depth">
        <div class="mini-depth-header">
          <span class="mini-col-lbl">量↑</span>
          <span class="mini-col-lbl">买</span>
          <span class="mini-col-lbl">卖</span>
          <span class="mini-col-lbl">量↑</span>
        </div>
        <For each={depthIndices()}>
          {(i) => {
            const b = () => bids()[i];
            const a = () => asks()[i];
            return (
              <div class="mini-depth-row">
                <span class="mini-bid-size">{b() ? b().size.toLocaleString() : ''}</span>
                <span class="mini-bid-px">{b() ? Number(b().price).toFixed(4) : ''}</span>
                <span class="mini-ask-px">{a() ? Number(a().price).toFixed(4) : ''}</span>
                <span class="mini-ask-size">{a() ? a().size.toLocaleString() : ''}</span>
              </div>
            );
          }}
        </For>
      </div>

      <div class="mini-seq-row">
        <span class="q-lbl">seq</span>
        <span class="mono-sub">{h().sequence_no ?? '—'}</span>
      </div>
    </div>
  );
}

// ============================================================
// DualBook
// ============================================================

function DualBook(props: { book: BinaryMarketBookView | null; conditionId: string }) {
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
                <span
                  class={`vig-badge-sm ${vi().cls}`}
                  title="vig (ask0+ask1-1)"
                >
                  {vi().text}
                </span>
              )}
            </Show>
          </div>
          <div class="cond-dual-grid">
            <MiniHalfBook half={bk().token0} />
            <MiniHalfBook half={bk().token1} />
          </div>
        </>
      )}
    </Show>
  );
}

// ============================================================
// CondPos
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
            const netQty = () => Number(p.net_qty);
            const mark = () => Number(p.mark_price);
            const pnlTotal = () => Number(p.pnl_realized) + Number(p.pnl_unrealized);
            const pnlCls = () => (pnlTotal() >= 0 ? 'pnl-pos' : 'pnl-neg');
            const qtySign = () => (netQty() >= 0 ? '+' : '');
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
// ConditionColumn
// ============================================================

function ConditionColumn(props: { cond: ConditionData }) {
  const c = () => props.cond;
  const condId = () => c().market?.condition_id ?? c().conditionId;
  const marketLabel = () => inferMarketLabel(condId(), c().market);
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
        <span class="cond-type-label">{marketLabel()}</span>
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
        <CondQuote quote={c().quote} isDemoData={c().isDemoData} />
      </div>

      <div class="cond-book-section">
        <DualBook book={c().book} conditionId={condId()} />
      </div>

      <div class="cond-pos-section">
        <CondPos posRows={c().posRows} perMarketPnl={c().perMarketPnl} />
      </div>
    </div>
  );
}

// ============================================================
// EventGroup
// ============================================================

function EventGroupCard(props: { group: EventGroup }) {
  const allInactive = () =>
    props.group.conditions.every((c) => c.market != null && !c.market!.accepting_orders);

  return (
    <div
      class={`event-group${allInactive() ? ' event-group-inactive' : ''}`}
      data-event={props.group.eventId ?? ''}
    >
      <EventHeader group={props.group} />
      <div class="event-columns">
        <For each={props.group.conditions}>
          {(cond) => <ConditionColumn cond={cond} />}
        </For>
      </div>
    </div>
  );
}

// ============================================================
// EventGrid (top-level export)
// ============================================================

export function EventGrid() {
  const groups = () => state.eventGroups;

  return (
    <div id="market-grid">
      <Show
        when={groups().length > 0}
        fallback={
          <div class="no-data grid-placeholder">
            {state.positions == null
              ? '加载市场数据...'
              : '等待持仓建立 / 后端未接入 · 点 ⚙ 检查'}
          </div>
        }
      >
        <For each={groups()}>
          {(group) => <EventGroupCard group={group} />}
        </For>
      </Show>
    </div>
  );
}
