/**
 * StatusDot — 统一状态灯组件
 * 替换 v5 分散的 wss-dot-sm / acc-dot / stale-dot
 * owner: 小苏  last_review: 2026-05-29
 *
 * state: 'green' | 'yellow' | 'red' | 'gray'
 * size:  'sm' (7px) | 'md' (9px) | 'lg' (11px)
 */

export type DotState = 'green' | 'yellow' | 'red' | 'gray';
export type DotSize  = 'sm' | 'md' | 'lg';

interface StatusDotProps {
  state: DotState;
  size?: DotSize;
  title?: string;
}

export function StatusDot(props: StatusDotProps) {
  const size = () => props.size ?? 'sm';
  const cls = () =>
    `status-dot status-dot-${size()} status-dot-${props.state}`;

  return <span class={cls()} title={props.title} />;
}

/** 从 WSS state 字符串派生 DotState */
export function wssStateToDot(wssState: string | undefined): DotState {
  if (!wssState) return 'gray';
  if (wssState === 'CONNECTED') return 'green';
  if (wssState === 'CONNECTING') return 'yellow';
  return 'red';
}

/** 从布尔连接状态派生 DotState */
export function boolToDot(ok: boolean | undefined): DotState {
  if (ok == null) return 'gray';
  return ok ? 'green' : 'red';
}

/** 从 staleness ms 派生 DotState */
export function stalenessToDot(ms: number | null): DotState {
  if (ms == null) return 'gray';
  if (ms < 100) return 'green';
  if (ms < 1000) return 'yellow';
  return 'red';
}
