// trading/shared.ts — 盯盘页共享: 展开状态 store + 切换/全展全折 + 盘口类型/延迟着色 helper。
//   (从 TradingPage.tsx 拆出, 2026-06-10 架构师 A3)。展开 store 是模块级单例 (createStore) → 单一来源共享。
import { createStore, produce } from 'solid-js/store';
import { addDetailInterest } from '../../store';
import { MARKET_TYPE_ZH } from '../../i18n';

// 读写 sessionStorage (避免页面刷新丢失展开状态)
export function ssGet(key: string): boolean | null {
  try { const v = sessionStorage.getItem(key); return v === null ? null : v === '1'; } catch { return null; }
}
export function ssSet(key: string, val: boolean): void {
  try { sessionStorage.setItem(key, val ? '1' : '0'); } catch { /* ignore */ }
}

// Market 行展开状态
const [expandedMarkets, setExpandedMarkets] = createStore<Record<string, boolean>>({});
// Event 分组展开状态
const [expandedEvents, setExpandedEvents] = createStore<Record<string, boolean>>({});

export function isMarketExpanded(condId: string): boolean {
  if (condId in expandedMarkets) return expandedMarkets[condId];
  const ss = ssGet(`stcpp_mkt_exp_${condId}`);
  return ss ?? false;
}

export function toggleMarket(condId: string): void {
  const next = !isMarketExpanded(condId);
  setExpandedMarkets(condId, next);
  ssSet(`stcpp_mkt_exp_${condId}`, next);
  // 展开即按需拉一次 detail (不等下一轮 5s 轮询), 折叠不主动拉
  if (next) addDetailInterest(condId);
}

export function isEventExpanded(eventId: string, isLive: boolean): boolean {
  if (eventId in expandedEvents) return expandedEvents[eventId];
  const ss = ssGet(`stcpp_evt_exp_${eventId}`);
  if (ss !== null) return ss;
  return isLive; // 进行中赛事默认展开分组
}

export function toggleEvent(eventId: string, isLive: boolean): void {
  const next = !isEventExpanded(eventId, isLive);
  setExpandedEvents(eventId, next);
  ssSet(`stcpp_evt_exp_${eventId}`, next);
}

export function expandAllMarkets(condIds: string[]): void {
  // 批量: 一次 produce 更新 store (替代逐个 setExpandedMarkets, 防 N 次更新/重渲染)。
  setExpandedMarkets(produce((m) => { for (const c of condIds) m[c] = true; }));
  for (const c of condIds) ssSet(`stcpp_mkt_exp_${c}`, true);
}

export function collapseAllMarkets(condIds: string[]): void {
  // 批量折叠: 一次 produce 把传入盘全置 false (store 优先于 ss, 确保折干净);
  //   ss 持久键也同步清 (防 reload 后 ss 残留 true 又自动展开)。
  setExpandedMarkets(produce((m) => { for (const c of condIds) m[c] = false; }));
  for (const c of condIds) ssSet(`stcpp_mkt_exp_${c}`, false);
}

// ============================================================
// 盘口类型 Chip 颜色映射 (胜负蓝/让分橙/大小紫)
// ============================================================

export function marketTypeColor(condId: string): 'primary' | 'warning' | 'secondary' | 'default' {
  if (condId.includes('-ml') || condId.includes('-moneyline')) return 'primary';
  if (condId.includes('-spread')) return 'warning';
  if (condId.includes('-total')) return 'secondary';
  return 'default';
}

export function marketTypeShort(condId: string): string {
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

export function stalenessColor(ms: number | null): 'success' | 'warning' | 'error' | 'default' {
  if (ms == null) return 'default';
  if (ms < 100) return 'success';
  if (ms < 1000) return 'warning';
  return 'error';
}

export function stalenessText(ms: number | null): string {
  if (ms == null) return '—';
  if (ms < 1000) return `${Math.round(ms)}ms`;
  return `${(ms / 1000).toFixed(1)}s`;
}

export function stalenessClass(ms: number | null): string {
  if (ms == null) return 'stale-none';
  if (ms < 100) return 'stale-ok';
  if (ms < 1000) return 'stale-warn';
  return 'stale-err';
}
