/**
 * Badge — 统一标签组件
 * 替换 v5 的 badge / gate-chip / demo-chip / mode-badge 等 10+ 种手写 class
 * owner: 小苏  last_review: 2026-05-29
 */

import { JSX } from 'solid-js';

export type BadgeVariant =
  | 'paper' | 'live' | 'backtest'
  | 'demo'  | 'stub'
  | 'ok'    | 'warn'  | 'err'
  | 'info';

interface BadgeProps {
  variant: BadgeVariant;
  children: JSX.Element;
  title?: string;
  class?: string;
}

export function Badge(props: BadgeProps) {
  const cls = () =>
    `badge badge-${props.variant}${props.class ? ` ${props.class}` : ''}`;

  return (
    <span class={cls()} title={props.title}>
      {props.children}
    </span>
  );
}

/** mode 字符串 → Badge variant */
export function modeVariant(mode: string): BadgeVariant {
  if (mode === 'live')     return 'live';
  if (mode === 'backtest') return 'backtest';
  return 'paper';
}

/** mode 字符串 → 显示文字 */
export function modeText(mode: string): string {
  if (mode === 'live')     return 'LIVE';
  if (mode === 'backtest') return 'BACKTEST';
  return 'PAPER';
}
