/**
 * StalenessHeatCell — staleness 热力格子
 * 用于 Ops 观测页各市场 staleness 热力表
 * owner: 小苏  last_review: 2026-05-29
 *
 * 颜色: <100ms 绿 / <1s 黄 / ≥1s 红 / null 灰
 */

import { Show } from 'solid-js';
import { StatusDot, stalenessToDot } from './StatusDot';

interface StalenessHeatCellProps {
  /** staleness in ms */
  ms: number | null;
  label?: string;
}

function fmtMs(ms: number | null): string {
  if (ms == null) return '—';
  if (ms < 1000) return `${Math.round(ms)}ms`;
  return `${(ms / 1000).toFixed(1)}s`;
}

function tierCls(ms: number | null): string {
  if (ms == null) return 'staleness-cell-none';
  if (ms < 100)  return 'staleness-cell-ok';
  if (ms < 1000) return 'staleness-cell-warn';
  return 'staleness-cell-err';
}

export function StalenessHeatCell(props: StalenessHeatCellProps) {
  const dotState = () => stalenessToDot(props.ms);
  const text = () => fmtMs(props.ms);
  const cls  = () => `staleness-cell ${tierCls(props.ms)}`;
  const title = () =>
    props.label
      ? `${props.label}: ${text()}`
      : `staleness: ${text()}`;

  return (
    <div class={cls()} title={title()}>
      <StatusDot state={dotState()} size="sm" />
      <Show when={props.label}>
        <span style={{ 'font-size': '9px', 'color': 'var(--text-dim)', 'max-width': '60px', 'overflow': 'hidden', 'text-overflow': 'ellipsis' }}>
          {props.label}
        </span>
      </Show>
      {text()}
    </div>
  );
}
