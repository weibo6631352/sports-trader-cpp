/**
 * ConfBar — 置信度条形图
 * 0-100% 范围, 颜色绿(≥70%)/黄(≥50%)/红(<50%)
 * owner: 小苏  last_review: 2026-05-29
 */

import { Show } from 'solid-js';

interface ConfBarProps {
  /** 置信度 [0, 1] */
  value: number | null | undefined;
}

export function ConfBar(props: ConfBarProps) {
  const v = () => {
    const n = Number(props.value);
    return Number.isFinite(n) ? n : null;
  };

  const pct = () => {
    const n = v();
    if (n == null) return '0%';
    return `${(Math.min(Math.max(n, 0), 1) * 100).toFixed(1)}%`;
  };

  const tier = () => {
    const n = v();
    if (n == null) return 'dim';
    if (n >= 0.7) return 'high';
    if (n >= 0.5) return 'mid';
    return 'low';
  };

  const text = () => {
    const n = v();
    if (n == null) return '—';
    return `${(n * 100).toFixed(0)}%`;
  };

  return (
    <div class="conf-bar-wrap">
      <div class="conf-bar-track">
        <div class={`conf-bar-fill conf-bar-fill-${tier()}`} style={{ width: pct() }} />
      </div>
      <span class={`conf-bar-text conf-bar-text-${tier()}`}>{text()}</span>
    </div>
  );
}
