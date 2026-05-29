/**
 * TradingPage.tsx — 盯盘页 (v7 Material Design)
 * owner: 小苏  last_review: 2026-05-29
 *
 * v7 变更:
 *  - SUID Card/CardContent/CardHeader: 事件卡片 / 盘口卡片
 *  - SUID Chip: 状态标签 (sport/status/mode/NR/demo/advisory)
 *  - SUID LinearProgress: 置信度条 / 深度条 / edge 条
 *  - SUID ToggleButtonGroup: 筛选栏
 *  - 数据更全: tick/fee/neg_risk/accepting/source/微价/imbalance/gap/seq
 *  - 保留 Event→Condition→DualBook 三层结构
 *  - 保留 DEMO/advisory/XD 红线
 */

import { createSignal, For, Show } from 'solid-js';
import Card from '@suid/material/Card';
import CardContent from '@suid/material/CardContent';
import CardHeader from '@suid/material/CardHeader';
import Chip from '@suid/material/Chip';
import LinearProgress from '@suid/material/LinearProgress';
import Typography from '@suid/material/Typography';
import Divider from '@suid/material/Divider';
import ToggleButton from '@suid/material/ToggleButton';
import ToggleButtonGroup from '@suid/material/ToggleButtonGroup';
import TextField from '@suid/material/TextField';
import Box from '@suid/material/Box';
import Alert from '@suid/material/Alert';
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
import { DepthBar } from './ui/DepthBar';

// ============================================================
// 小工具
// ============================================================

function RejectChip(props: { rejectRows: RiskReject[] }) {
  const has = () => props.rejectRows.length > 0;
  const tooltip = () =>
    props.rejectRows
      .map((r) => `${REJECT_REASON_ZH[r.reason_code] ?? r.reason_code} · ${SIDE_ZH[r.side] ?? r.side} ${r.size}@${r.price}`)
      .join('\n');
  return (
    <Show when={has()}>
      <Chip
        label={`x${props.rejectRows.length}`}
        color="error"
        size="small"
        title={tooltip()}
        sx={{ fontSize: '10px', height: '18px', fontWeight: 700 }}
      />
    </Show>
  );
}

// ============================================================
// EventHeader (Material CardHeader 风格)
// ============================================================

function EventHeader(props: { group: EventGroup }) {
  const score      = () => props.group.score;
  const conditions = () => props.group.conditions;
  const firstMkt   = () => conditions().find((c) => c.market)?.market ?? null;
  const isDemoData = () => conditions().some((c) => c.isDemoData);
  const sport      = () => score()?.sport ?? null;
  const sportZh    = () => sport() ? (SPORT_ZH[sport()!] ?? sport()) : '';
  const eventUrl   = () => firstMkt()?.polymarket_url ?? null;

  const statusZh  = () => STATUS_ZH[score()?.status ?? ''] ?? score()?.status ?? '?';
  const isLive    = () => score()?.status === 'inplay';
  const statusColor = (): 'success' | 'warning' | 'default' | 'error' => {
    const st = score()?.status;
    if (st === 'inplay')   return 'success';
    if (st === 'halftime') return 'warning';
    if (st === 'final')    return 'default';
    return 'default';
  };

  // per-event staleness
  const maxStaleMs = () => {
    const vals = conditions()
      .map((c) => c.book ? stalenessMs(c.book.as_of_ts_ns ?? c.book.token0?.book_as_of_ts) : null)
      .filter((v): v is number => v != null);
    if (vals.length === 0) return null;
    return Math.max(...vals);
  };
  const staleText = () => {
    const ms = maxStaleMs();
    if (ms == null) return null;
    return ms < 1000 ? `延迟 ${Math.round(ms)}ms` : `延迟 ${(ms / 1000).toFixed(1)}s`;
  };
  const staleColor = (): 'error' | 'warning' | '' => {
    const ms = maxStaleMs();
    if (ms == null) return '';
    if (ms >= 10000) return 'error';
    if (ms >= 2000)  return 'warning';
    return '';
  };

  const subheader = () => {
    const sc = score();
    if (!sc) return null;
    const parts: string[] = [];
    if (sc.period) parts.push(sc.period);
    if (sc.clock_sec != null) parts.push(fmtClock(sc.clock_sec));
    return parts.join(' · ');
  };

  return (
    <div class="event-header-wrap">
      {/* Sport Chip */}
      <Show when={sportZh()}>
        <Chip label={sportZh()} size="small" variant="outlined"
          sx={{ fontSize: '10px', height: '20px', color: 'text.secondary' }} />
      </Show>

      {/* LIVE 角标 */}
      <Show when={isLive()}>
        <span class="live-badge">LIVE</span>
      </Show>

      {/* 队伍 / 比分 */}
      <Show
        when={score()}
        fallback={<Typography variant="body2" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>—</Typography>}
      >
        {(sc) => (
          <>
            <Typography variant="subtitle2" class="evt-team">{sc().home}</Typography>
            <Typography variant="h6" class="evt-score">{sc().home_score ?? '—'}</Typography>
            <span class="evt-dash">—</span>
            <Typography variant="h6" class="evt-score">{sc().away_score ?? '—'}</Typography>
            <Typography variant="subtitle2" class="evt-team">{sc().away}</Typography>
            <Chip
              label={statusZh()}
              color={statusColor()}
              size="small"
              variant="outlined"
              sx={{ fontSize: '10px', height: '20px' }}
            />
            <Show when={subheader()}>
              <Typography variant="caption" class="evt-period">{subheader()}</Typography>
            </Show>
          </>
        )}
      </Show>

      {/* DEMO chip */}
      <Show when={isDemoData()}>
        <Chip label="DEMO" color="warning" size="small" variant="outlined"
          title="演示数据·非实盘" sx={{ fontSize: '10px', height: '20px' }} />
      </Show>

      {/* Polymarket 链接 */}
      <Show when={eventUrl()}>
        <Typography
          component="a"
          href={eventUrl()!}
          target="_blank"
          rel="noopener noreferrer"
          variant="caption"
          sx={{ color: 'primary.main', textDecoration: 'none', ml: 0.5, '&:hover': { textDecoration: 'underline' } }}
        >
          Polymarket
        </Typography>
      </Show>

      {/* staleness */}
      <Show when={staleText()}>
        <Typography
          variant="caption"
          sx={{
            fontFamily: 'monospace',
            ml: 'auto',
            color: staleColor() === 'error' ? 'error.main' : staleColor() === 'warning' ? 'warning.main' : 'text.disabled',
          }}
        >
          {staleText()}
        </Typography>
      </Show>
    </div>
  );
}

// ============================================================
// CondQuote (Material Alert + LinearProgress)
// XD-1/3/4/5 红线保持
// ============================================================

function CondQuote(props: { quote: Quote | null; isDemoData: boolean }) {
  const q = () => props.quote;

  return (
    <Show
      when={q()}
      fallback={
        <Box sx={{ p: 1, display: 'flex', alignItems: 'center', gap: 0.5 }}>
          <Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>量化未接入</Typography>
          <Show when={props.isDemoData}>
            <Chip label="demo" color="warning" size="small" sx={{ fontSize: '9px', height: '16px' }} />
          </Show>
        </Box>
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
        const edgePct     = () => Math.min(Math.abs(edgeBps()) / 100, 1) * 100;
        const kellyPositive = () => Number.isFinite(kelly()) && kelly() > 0;
        const confPct     = () => Number.isFinite(modelConf()) ? modelConf() * 100 : 0;
        const confColor = (): 'success' | 'warning' | 'error' | 'inherit' =>
          modelConf() >= 0.7 ? 'success' : modelConf() >= 0.4 ? 'warning' : 'error';

        return (
          <>
            {/* XD-3: advisory Alert */}
            <Show when={advisory()}>
              <Alert severity="warning" sx={{ py: 0.25, px: 1, fontSize: '11px', mb: 0.5 }}>
                仅供参考·不下单
              </Alert>
            </Show>

            {/* Row 1: 公允价 / 市场中间价 */}
            <div class="cond-quote-row">
              <span class="q-lbl">公允</span>
              <Typography
                sx={{ fontFamily: 'monospace', fontSize: '15px', fontWeight: 700,
                  color: !calibrated() ? 'text.disabled' : 'text.primary' }}
              >
                {Number.isFinite(fairValue()) ? fairValue().toFixed(4) : '—'}
              </Typography>
              <Show when={!calibrated()}>
                <span class="uncalib-chip" title="模型尚未完成校准">未校准</span>
              </Show>
              <span class="q-lbl">市场</span>
              <Typography sx={{ fontFamily: 'monospace', fontSize: '11px', color: 'text.secondary' }}>
                {Number.isFinite(marketMid()) ? marketMid().toFixed(4) : '—'}
              </Typography>
              <Show when={props.isDemoData}>
                <Chip label="demo" color="warning" size="small" sx={{ fontSize: '9px', height: '16px', ml: 'auto' }} />
              </Show>
            </div>

            {/* Row 2: CI 区间 */}
            <Show when={hasCi()}>
              <div class="cond-ci-row">
                <span class="q-ci-label">CI</span>
                <Typography sx={{ fontFamily: 'monospace', fontSize: '11px', color: 'text.secondary' }}>
                  [{ciLower()!.toFixed(3)}–{ciUpper()!.toFixed(3)}]
                </Typography>
              </div>
            </Show>

            {/* Row 3: AI provenance + 置信度 LinearProgress */}
            <div class="cond-prov-row">
              <span class="q-lbl">模型</span>
              <Typography
                title={`${modelId()} · ${modelKind()} · ${quote().spec_version ?? '—'}`}
                sx={{ fontFamily: 'monospace', fontSize: '10px', color: 'text.secondary', overflow: 'hidden', textOverflow: 'ellipsis', maxWidth: '90px', whiteSpace: 'nowrap' }}
              >
                {modelId()}
              </Typography>
              <span class="q-prov-sep">|</span>
              <span class="q-lbl">置信</span>
              {/* v7: LinearProgress 替换 ConfBar */}
              <Box sx={{ flex: 1, minWidth: '30px' }}>
                <LinearProgress
                  variant="determinate"
                  value={confPct()}
                  color={confColor()}
                  sx={{ height: 6, borderRadius: 3 }}
                />
              </Box>
              <Typography sx={{ fontFamily: 'monospace', fontSize: '10px', color: confColor() === 'success' ? '#4caf50' : confColor() === 'warning' ? '#ff9800' : '#f44336', ml: 0.5, whiteSpace: 'nowrap' }}>
                {Number.isFinite(modelConf()) ? `${(modelConf() * 100).toFixed(0)}%` : '—'}
              </Typography>
            </div>

            {/* XD-5: predict_ok=false → 不画 edge/kelly/notional */}
            <Show
              when={predictOk()}
              fallback={
                <Alert severity="error" sx={{ py: 0.25, px: 1, fontSize: '10px', mt: 0.5 }}>
                  预测异常 · edge/kelly/额度暂不可用
                </Alert>
              }
            >
              {/* Row 4: edge LinearProgress + 信号 */}
              <div class="cond-edge-row">
                <span class="q-lbl">优势</span>
                <Box sx={{ flex: 1, minWidth: '20px' }}>
                  <LinearProgress
                    variant="determinate"
                    value={edgePct()}
                    color={edgePositive() ? 'success' : 'error'}
                    sx={{ height: 5, borderRadius: 2 }}
                  />
                </Box>
                <Typography class={`q-edge ${edgePositive() ? 'edge-pos' : 'edge-neg'}`}>
                  {fmtBps(edgeBps())}
                </Typography>
                <span class="q-lbl">信号</span>
                <Typography class="q-sig">
                  {Number.isFinite(signalStr()) ? signalStr().toFixed(2) : '—'}
                </Typography>
              </div>

              {/* Row 5: Kelly + 建议额度 */}
              <div class="cond-kelly-row">
                <span class="q-lbl">Kelly</span>
                <Typography class={`q-kelly ${kellyPositive() ? 'kelly-pos' : 'kelly-zero'}`}>
                  {Number.isFinite(kelly()) ? `${(kelly() * 100).toFixed(1)}%` : '—'}
                </Typography>
                <span class="q-lbl">额</span>
                <Typography class="q-notional">
                  ${Number.isFinite(notional())
                    ? notional().toLocaleString('en-US', { minimumFractionDigits: 0, maximumFractionDigits: 0 })
                    : '—'}
                </Typography>
              </div>
            </Show>
          </>
        );
      }}
    </Show>
  );
}

// ============================================================
// MiniHalfBook (5 档 + DepthBar)
// ============================================================

function MiniHalfBook(props: { half: HalfBook }) {
  const h = () => props.half;
  const bid      = () => Number(h().best_bid);
  const ask      = () => Number(h().best_ask);
  const micro    = () => Number(h().microprice);
  const spread   = () => Number(h().spread);
  const imbalance = () => Number(h().imbalance);
  const imbPct   = () => Number.isFinite(imbalance()) ? `${((imbalance() + 1) / 2 * 100).toFixed(1)}%` : '50%';
  const imbNum   = () => Number.isFinite(imbalance()) ? imbalance().toFixed(2) : '—';
  const bids     = () => (h().bids ?? []).slice(0, 5);
  const asks     = () => (h().asks ?? []).slice(0, 5);
  const maxLen   = () => Math.max(bids().length, asks().length);
  const depthIdx = () => Array.from({ length: maxLen() }, (_, i) => i);
  const maxSize  = () => Math.max(...bids().map((b) => Number(b.size)), ...asks().map((a) => Number(a.size)), 1);
  const wssState = () => h().wss_state ?? 'unknown';
  const source   = () => h().source ?? '—';

  return (
    <div class="mini-half">
      {/* 标题行 */}
      <div class="mini-half-header">
        <Chip
          label={h().outcome ?? '—'}
          size="small"
          variant="outlined"
          sx={{ fontSize: '10px', height: '18px', maxWidth: '80px', fontWeight: 700 }}
        />
        <StatusDot state={wssStateToDot(wssState())} size="sm" title={`WSS: ${wssState()}`} />
        <Show when={(h().gap_count ?? 0) > 0}>
          <span class="gap-dot" title={`gap ${h().gap_count}`} />
        </Show>
        <Typography variant="caption" sx={{ fontSize: '9px', color: 'text.disabled', ml: 'auto' }} title="来源">
          {source()}
        </Typography>
      </div>

      {/* Best bid / ask + microprice */}
      <div class="mini-ba-row">
        <Typography class="mono-main mini-bid" sx={{ fontSize: '13px !important' }}>
          {Number.isFinite(bid()) ? bid().toFixed(4) : '—'}
        </Typography>
        <span class="mini-ba-sep">|</span>
        <Typography class="mono-main mini-ask" sx={{ fontSize: '13px !important' }}>
          {Number.isFinite(ask()) ? ask().toFixed(4) : '—'}
        </Typography>
      </div>

      {/* microprice + 价差 + imbalance */}
      <div class="mini-spread-row">
        <span class="q-lbl">微价</span>
        <Typography sx={{ fontFamily: 'monospace', fontSize: '10px', color: 'text.secondary' }}>
          {Number.isFinite(micro()) ? micro().toFixed(4) : '—'}
        </Typography>
        <span class="q-lbl">差</span>
        <Typography sx={{ fontFamily: 'monospace', fontSize: '10px', color: 'text.secondary' }}>
          {Number.isFinite(spread()) ? `${(spread() * 100).toFixed(2)}%` : '—'}
        </Typography>
      </div>
      <div class="mini-spread-row">
        <span class="q-lbl">失衡</span>
        <div class="mini-imb-track" title={`imbalance ${imbNum()}`}>
          <div class="mini-imb-fill" style={{ width: imbPct() }} />
        </div>
        <Typography class="mini-imb-val">{imbNum()}</Typography>
      </div>

      {/* 5 档深度 */}
      <div class="mini-depth">
        <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr', gap: '1px', 'margin-bottom': '2px' }}>
          <Typography class="mini-col-lbl" sx={{ textAlign: 'right', fontSize: '9px !important' }}>量/买</Typography>
          <Typography class="mini-col-lbl" sx={{ textAlign: 'left', fontSize: '9px !important' }}>卖/量</Typography>
        </div>
        <For each={depthIdx()}>
          {(i) => {
            const b = () => bids()[i];
            const a = () => asks()[i];
            return (
              <div style={{ display: 'flex', gap: '4px', 'margin-bottom': '2px', 'align-items': 'center' }}>
                <div style={{ flex: '1', display: 'flex', 'flex-direction': 'column', gap: '1px' }}>
                  <div style={{ display: 'flex', gap: '3px', 'align-items': 'center', 'justify-content': 'flex-end' }}>
                    <Typography class="mini-bid-size" sx={{ fontFamily: 'monospace', fontSize: '9px' }}>
                      {b() ? Number(b().size).toLocaleString() : ''}
                    </Typography>
                    <Typography class="mini-bid-px" sx={{ fontFamily: 'monospace', fontSize: '10px' }}>
                      {b() ? Number(b().price).toFixed(4) : ''}
                    </Typography>
                  </div>
                  <Show when={b()}>
                    <DepthBar size={Number(b().size)} maxSize={maxSize()} side="bid" />
                  </Show>
                </div>
                <span style={{ color: 'var(--md-border)', 'font-size': '10px' }}>|</span>
                <div style={{ flex: '1', display: 'flex', 'flex-direction': 'column', gap: '1px' }}>
                  <div style={{ display: 'flex', gap: '3px', 'align-items': 'center' }}>
                    <Typography class="mini-ask-px" sx={{ fontFamily: 'monospace', fontSize: '10px' }}>
                      {a() ? Number(a().price).toFixed(4) : ''}
                    </Typography>
                    <Typography class="mini-ask-size" sx={{ fontFamily: 'monospace', fontSize: '9px' }}>
                      {a() ? Number(a().size).toLocaleString() : ''}
                    </Typography>
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

      {/* seq / gap / wss_state 详情 */}
      <div class="mini-seq-row">
        <span class="q-lbl">seq</span>
        <Typography class="mono-sub">{h().sequence_no ?? '—'}</Typography>
        <Show when={(h().gap_count ?? 0) > 0}>
          <Chip label={`gap:${h().gap_count}`} color="error" size="small" sx={{ fontSize: '9px', height: '16px', ml: 0.5 }} />
        </Show>
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
    const color: 'success' | 'warning' | 'error' = cs < 0.02 ? 'success' : cs < 0.04 ? 'warning' : 'error';
    return { text: `${(cs * 100).toFixed(2)}%`, color };
  };

  return (
    <Show
      when={book()}
      fallback={
        <Box sx={{ p: 1, color: 'text.disabled', fontSize: '12px', fontStyle: 'italic' }}>
          <Show
            when={isEndpointFailing(`/api/v1/book/${props.conditionId}`)}
            fallback={<span>订单簿未接入</span>}
          >
            <Chip label="订单簿拉取失败" color="error" size="small" />
          </Show>
        </Box>
      }
    >
      {(bk) => (
        <>
          <div class="cond-book-header">
            <Typography variant="caption" class="cond-book-label">双边订单簿</Typography>
            <Show when={vigInfo()}>
              {(vi) => (
                <Chip
                  label={`vig ${vi().text}`}
                  color={vi().color}
                  size="small"
                  variant="outlined"
                  title="vig (ask0+ask1-1)"
                  sx={{ fontSize: '10px', height: '18px' }}
                />
              )}
            </Show>
            {/* mode chip */}
            <Chip
              label={bk().mode ?? ''}
              size="small"
              variant="outlined"
              sx={{ fontSize: '9px', height: '16px', color: 'text.secondary', ml: 0.5 }}
            />
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
// CondPos (持仓区)
// ============================================================

function CondPos(props: { posRows: Position[]; perMarketPnl: number | null }) {
  const pnlStr = () => {
    const v = props.perMarketPnl;
    if (v == null) return null;
    return { text: fmtUsdc(v), pos: v >= 0 };
  };

  return (
    <Show
      when={props.posRows.length > 0}
      fallback={
        <div class="cond-pos-header">
          <Typography variant="caption" sx={{ color: 'text.disabled' }}>持仓 —</Typography>
          <Show when={pnlStr()}>
            {(ps) => (
              <Typography variant="caption" class={`mono-main ${ps().pos ? 'pnl-pos' : 'pnl-neg'}`} sx={{ ml: 1 }}>
                {ps().text}
              </Typography>
            )}
          </Show>
        </div>
      }
    >
      <>
        <div class="cond-pos-header">
          <Typography variant="caption" sx={{ color: 'text.secondary' }}>持仓/PnL</Typography>
          <Show when={pnlStr()}>
            {(ps) => (
              <Typography variant="caption" class={`mono-main ${ps().pos ? 'pnl-pos' : 'pnl-neg'}`} sx={{ ml: 'auto' }}>
                {ps().text}
              </Typography>
            )}
          </Show>
        </div>
        <For each={props.posRows}>
          {(p) => {
            const netQty   = () => Number(p.net_qty);
            const mark     = () => Number(p.mark_price);
            const pnlTotal = () => Number(p.pnl_realized) + Number(p.pnl_unrealized);
            const pos      = () => pnlTotal() >= 0;
            const qtySign  = () => netQty() >= 0 ? '+' : '';
            return (
              <div class="cond-pos-row">
                <Chip label={p.outcome ?? '—'} size="small" variant="outlined"
                  sx={{ fontSize: '9px', height: '16px', fontWeight: 700 }} />
                <Typography class="cond-pos-qty">
                  {qtySign()}{netQty().toLocaleString()}u
                </Typography>
                <Typography class="cond-pos-mark">
                  {Number.isFinite(mark()) ? mark().toFixed(4) : '—'}
                </Typography>
                <Typography class={`mono-main ${pos() ? 'pnl-pos' : 'pnl-neg'}`} sx={{ ml: 'auto', fontSize: '12px !important' }}>
                  {fmtUsdc(pnlTotal())}
                </Typography>
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
  const c        = () => props.cond;
  const condId   = () => c().market?.condition_id ?? c().conditionId;
  const mktLabel = () => inferMarketLabel(condId(), c().market);
  const mkt      = () => c().market;
  const inactive = () => mkt() != null && !mkt()!.accepting_orders;

  // 补充: tick / fee / source
  const tickSz = () => mkt()?.tick_size;
  const feeRate = () => mkt()?.fee_rate;
  const accepting = () => mkt()?.accepting_orders;
  const negRisk = () => mkt()?.neg_risk;

  return (
    <div class={`cond-col${inactive() ? ' cond-col-inactive' : ''}`} data-condition={condId()}>
      {/* 拒单 Chip 右上角 */}
      <div class="cond-col-pos-anchor">
        <RejectChip rejectRows={c().rejectRows} />
      </div>

      {/* 盘口标题行 */}
      <div class="cond-header">
        <Typography class="cond-type-label" title={condId()}>{mktLabel()}</Typography>
        <Show when={mkt()}>
          {(m) => (
            <>
              <span
                class={`acc-dot ${m().accepting_orders ? 'acc-dot-ok' : 'acc-dot-off'}`}
                title={m().accepting_orders ? '接单中' : '不接单'}
              />
              <Show when={m().neg_risk}>
                <Chip label="NR" size="small" variant="outlined"
                  title="neg_risk" sx={{ fontSize: '9px', height: '16px', color: 'text.disabled' }} />
              </Show>
              {/* tick + fee */}
              <Show when={m().tick_size != null}>
                <Typography variant="caption" sx={{ fontFamily: 'monospace', fontSize: '9px', color: 'text.disabled' }} title="tick_size">
                  tick:{m().tick_size}
                </Typography>
              </Show>
              <Show when={m().fee_rate != null}>
                <Typography variant="caption" sx={{ fontFamily: 'monospace', fontSize: '9px', color: 'text.disabled' }} title="fee_rate">
                  fee:{(Number(m().fee_rate)*100).toFixed(2)}%
                </Typography>
              </Show>
            </>
          )}
        </Show>
      </div>

      {/* 量化决策区 */}
      <div class="cond-quote-section">
        <CondQuote quote={c().quote} isDemoData={c().isDemoData} />
      </div>

      {/* 双边订单簿 */}
      <div class="cond-book-section">
        <DualBook book={c().book} conditionId={condId()} />
      </div>

      {/* 持仓区 */}
      <div class="cond-pos-section">
        <CondPos posRows={c().posRows} perMarketPnl={c().perMarketPnl} />
      </div>
    </div>
  );
}

// ============================================================
// EventGroupCard (Material Card)
// ============================================================

function EventGroupCard(props: { group: EventGroup }) {
  const allInactive = () =>
    props.group.conditions.every((c) => c.market != null && !c.market!.accepting_orders);

  return (
    <Card
      variant="outlined"
      class={allInactive() ? 'event-group-inactive' : ''}
      data-event={props.group.eventId ?? ''}
      sx={{ overflow: 'hidden' }}
    >
      <EventHeader group={props.group} />
      <div class="event-columns">
        <For each={props.group.conditions}>
          {(cond) => <ConditionColumn cond={cond} />}
        </For>
      </div>
    </Card>
  );
}

// ============================================================
// TradingToolbar (Material ToggleButtonGroup 筛选)
// ============================================================

type FilterMode = 'all' | 'live' | 'position';

function TradingToolbar(props: {
  filter: FilterMode;
  search: string;
  onFilter: (f: FilterMode) => void;
  onSearch: (s: string) => void;
  visibleCount: number;
  totalCount: number;
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
        显示 {props.visibleCount} / {props.totalCount}
      </Typography>
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
    const f = filter();
    if (f === 'live') {
      groups = groups.filter((g) => g.score?.status === 'inplay' || g.score?.status === 'halftime');
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
          if (sc.event_id?.toLowerCase().includes(q)) return true;
        }
        if (g.eventId?.toLowerCase().includes(q)) return true;
        return g.conditions.some((c) =>
          c.market?.slug?.toLowerCase().includes(q) || c.conditionId?.toLowerCase().includes(q),
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

      {/* PnL 净值曲线 */}
      <div class="spark-section">
        <PnlSparkline />
      </div>

      {/* 市场网格 */}
      <div class="market-grid">
        <Show
          when={filteredGroups().length > 0}
          fallback={
            <Typography
              variant="body2"
              sx={{ p: 5, textAlign: 'center', color: 'text.disabled', fontStyle: 'italic' }}
            >
              {state.positions == null
                ? '加载市场数据...'
                : filter() !== 'all'
                  ? '该筛选条件下无赛事'
                  : '等待持仓建立 / 后端未接入 · 点 ⚙ 检查'}
            </Typography>
          }
        >
          <For each={filteredGroups()}>
            {(group) => <EventGroupCard group={group} />}
          </For>
        </Show>
      </div>
    </div>
  );
}
