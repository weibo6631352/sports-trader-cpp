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
import { state, setDetailInterest, addDetailInterest, uiNow, posSeenAt, getSharpTrend } from '../store';
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
import { MarketExpandArea } from './trading/panels';  // 展开区 6 组件已拆出 (架构师 A3 文件拆分)

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
  // 行级订阅 (架构师 A1 根治): book/quote/summary 直接响应式读 conditionCache, 不走 c() 树快照 →
  //   hot 帧写 conditionCache 只重渲染这一行 (不再每帧重建整树 126ms)。c() 树只管结构 + posRows/pnl(低频)。
  const book = () => state.conditionCache[condId()]?.book ?? null;
  const quote = () => state.conditionCache[condId()]?.quote ?? null;
  const summary = () => state.conditionCache[condId()]?.summary ?? null;  // /grid 顶档摘要
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
    // 修 0.0k bug (dogfood P1-1): net_qty 单位是 shares(~39u), /1000 截成 0.0k 一眼误判无仓。上千才用 k。
    return `${p.outcome} ${qty >= 1000 ? (qty / 1000).toFixed(1) + 'k' : qty.toFixed(0) + 'u'}`;
  };

  // 本盘 net PnL (含已实现) —— 修「positions.pnl_realized 硬编码 0 → 折叠行丢已实现 (金融实测亏损低估73%)」:
  //   改用逐盘权威 perMarketPnl (= ledger_hub 累计realized + 浮盈 − 费, 上轮已修对); 无则降级仅浮盈。
  const pnlTotal = () => {
    const pm = c().perMarketPnl;
    if (pm != null) return pm;
    if (posRows().length === 0) return null;
    return posRows().reduce((acc, p) => acc + Number(p.pnl_unrealized), 0);
  };
  const pnlFmt = () => {
    const v = pnlTotal();
    if (v == null) return null;
    if (Math.abs(v) < 0.01) return '$0.0';                       // 浮点残差当 0 (架构师 B-5)
    return `${v >= 0 ? '+' : '−'}$${Math.abs(v).toFixed(1)}`;    // 统一 −$X 红 (去会计括号, UX Bug3)
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
  const signals = () => state.status?.signals_active_count ?? 0;
  const positions = () => state.positions?.positions?.filter((p) => Math.abs(Number(p.net_qty)) > 0) ?? [];
  // 全局 net PnL —— 修「positions.pnl_realized 硬编码 0 → 健康栏亏损低估 73% (金融 P1-B)」: 用 account 权威口径
  //   net_pnl (= equity − bankroll, 含已实现+浮盈−费), 不再逐盘加总(平仓盘已不在 positions 列表 → 结构性丢已实现)。
  const floatPnl = () => {
    const a = (state.account as any)?.account;
    if (a && Number.isFinite(Number(a.net_pnl))) return Number(a.net_pnl);
    return positions().reduce((s, p) => s + Number(p.pnl_unrealized), 0);
  };
  const mktName = (cid: string) => state.conditionCache[cid]?.summary?.title || cid.slice(0, 8);

  // 异常扫描 (全局可算): 后端/WSS 断 + 持仓 sharp 反向 + 持仓发散 + 资金不足拒单。
  const anomalies = () => {
    uiNow();  // 趋势/年龄随 1s 时钟刷新
    const out: Array<{ sev: 'err' | 'warn'; text: string }> = [];
    if (state.healthz && !backendOk()) out.push({ sev: 'err', text: '后端离线 · 数据停更' });
    if (state.status && !wssOk()) out.push({ sev: 'err', text: '订单簿 WSS 断连 · 价可能过期' });
    const divergeSeen = new Set<string>();  // 发散是 per-market, 同盘 YES+NO 两条 position 别重复告警 (架构师 B-4)
    for (const p of positions()) {
      const cid = p.market_id;
      // 优先 quote.sharp_fair (展开按需拉, 更鲜) 回退 summary.sharp (grid 2s 批量) — 防漏报错向仓 (dogfood#5/操盘手Bug4)
      const sq = state.conditionCache[cid]?.quote;
      const qSh = Number(sq?.sharp_fair ?? -1);
      const sy = (qSh > 0 && qSh < 1) ? qSh : state.conditionCache[cid]?.summary?.sharp;  // YES sharp
      const entry = Number(p.avg_entry_price);
      if (sy != null && sy > 0 && sy < 1 && Number.isFinite(entry)) {
        const sSide = p.outcome === 'YES' ? sy : 1 - sy;        // 本边 sharp
        const dist = sSide - entry;                              // <0 = sharp 已跌破入场 = 持仓亏向
        if (dist < -0.02) out.push({ sev: 'warn', text: `${mktName(cid)} ${p.outcome} · sharp 已反向 ${(dist * 100).toFixed(1)}点` });
      }
      // 发散告警 (per-market, 去重): 优先服务端 conv_rate (SharpFairTrack, 持久权威), 回退前端自攒环。
      if (divergeSeen.has(cid)) continue;
      const srvN = Number(sq?.sharp_samples ?? 0);
      const srvC = Number(sq?.sharp_conv_rate ?? Number.NaN);
      if (srvN >= 2 && Number.isFinite(srvC)) {
        if (srvC > 1e-4) { out.push({ sev: 'warn', text: `${mktName(cid)} · 持仓发散中 (市场远离 sharp)` }); divergeSeen.add(cid); }
      } else {
        const ring = getSharpTrend(cid);
        if (ring.length >= 3) {
          const now = ring[ring.length - 1], past = ring[Math.max(0, ring.length - 6)];
          const gNow = Math.abs(now.sharp - now.mid), gPast = Math.abs(past.sharp - past.mid);
          if (gNow > gPast * 1.3 && gNow > 0.01) { out.push({ sev: 'warn', text: `${mktName(cid)} · 持仓发散中 (市场远离 sharp)` }); divergeSeen.add(cid); }
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
        <span class="v8-health-sep">·</span>
        <span class="mono-sub" title="活跃信号数">信号 {signals()}</span>
        <span class="v8-health-sep">·</span>
        <span class="mono-sub" title="持仓盘口数 + 账户净盈亏 (account.net_pnl 权威口径: 已实现+浮盈−费, = equity−bankroll)">
          持仓 {positions().length} · 净 <b class={floatPnl() >= 0 ? 'pnl-pos' : 'pnl-neg'}>{floatPnl() >= 0 ? '+' : '−'}${Math.abs(floatPnl()).toFixed(1)}</b>
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
