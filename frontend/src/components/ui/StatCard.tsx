/**
 * StatCard — Grafana stat panel 风格大数字卡片
 * 用于 Ops 页系统健康 / 订阅数 / 吞吐等单值 KPI
 * owner: 小苏  last_review: 2026-05-29
 */

import { JSX, Show } from 'solid-js';

export type StatColor = 'default' | 'green' | 'red' | 'yellow';

interface StatCardProps {
  label: string;
  value: JSX.Element;
  unit?: string;
  sub?: JSX.Element;
  color?: StatColor;
  title?: string;
  pollHint?: string;
}

export function StatCard(props: StatCardProps) {
  const colorCls = () => {
    const c = props.color ?? 'default';
    if (c === 'green')  return 'stat-card-green';
    if (c === 'red')    return 'stat-card-red';
    if (c === 'yellow') return 'stat-card-yellow';
    return '';
  };

  return (
    <div class={`stat-card ${colorCls()}`} title={props.title}>
      <div class="stat-card-label">
        {props.label}
        <Show when={props.pollHint}>
          <span class="poll-hint" style={{ 'margin-left': '4px' }}>{props.pollHint}</span>
        </Show>
      </div>
      <div class="stat-card-value">
        {props.value}
        <Show when={props.unit}>
          <span style={{ 'font-size': '12px', 'font-weight': '400', 'color': 'var(--text-dim)', 'margin-left': '4px' }}>
            {props.unit}
          </span>
        </Show>
      </div>
      <Show when={props.sub}>
        <div class="stat-card-sub">{props.sub}</div>
      </Show>
    </div>
  );
}
