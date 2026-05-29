/**
 * PnlSparkline.tsx — PnL 净值曲线 (手写 SVG, 轻量无依赖)
 * owner: 小苏
 * last_review: 2026-05-29
 */

import { Show } from 'solid-js';
import { state } from '../store';
import { fmtTs, fmtUsdc, isEndpointFailingPrefix } from '../api';
import type { PnlTimeseries } from '../types';

function SparklineSvg(props: { data: PnlTimeseries }) {
  const buckets = () => props.data.buckets ?? [];

  const vals = () => buckets().map((b) => Number(b.cum_net_pnl));

  const min = () => Math.min(...vals());
  const max = () => Math.max(...vals());
  const range = () => (max() - min()) || 1;

  const W = 900;
  const H = 48;
  const padX = 4;
  const padY = 4;

  const points = () =>
    vals().map((v, i) => {
      const x = padX + ((W - padX * 2) * i) / (vals().length - 1);
      const y = padY + (H - padY * 2) * (1 - (v - min()) / range());
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    });

  const zeroY = () => {
    const raw = padY + (H - padY * 2) * (1 - (0 - min()) / range());
    return Math.min(H - padY, Math.max(padY, raw));
  };

  const lastX = () => (padX + (W - padX * 2)).toFixed(1);

  const areaPath = () =>
    `M ${padX},${zeroY().toFixed(1)} L ${points().join(' L ')} L ${lastX()},${zeroY().toFixed(1)} Z`;

  const linePath = () => `M ${points().join(' L ')}`;

  const latestPnl = () => {
    const v = vals();
    return v.length > 0 ? v[v.length - 1] : 0;
  };

  const color = () => (latestPnl() >= 0 ? '#22c55e' : '#ef4444');

  const windowSec = () => Number(props.data.window_sec ?? 3600);
  const windowLabel = () =>
    windowSec() >= 3600
      ? `近 ${Math.round(windowSec() / 3600)}h`
      : `近 ${Math.round(windowSec() / 60)}min`;

  const firstTs = () =>
    buckets().length > 0 ? fmtTs(buckets()[0].bucket_start_ts) : '—';

  return (
    <>
      <div class="spark-header">
        <span class="spark-label">净值曲线</span>
        <span class="spark-window">
          {windowLabel()} · {firstTs()} – {fmtTs(props.data.as_of_ts)}
        </span>
        <span class={latestPnl() >= 0 ? 'pnl-pos mono-strong' : 'pnl-neg mono-strong'}>
          {fmtUsdc(latestPnl())}
        </span>
        <span class="spark-ts">更新 {fmtTs(props.data.as_of_ts)}</span>
      </div>
      <svg viewBox={`0 0 ${W} ${H}`} width="100%" height={H} class="spark-svg">
        <defs>
          <linearGradient id="sparkGrad" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color={color()} stop-opacity="0.3" />
            <stop offset="100%" stop-color={color()} stop-opacity="0.02" />
          </linearGradient>
        </defs>
        <line
          x1={padX}
          y1={zeroY().toFixed(1)}
          x2={W - padX}
          y2={zeroY().toFixed(1)}
          stroke="#374151"
          stroke-width="0.5"
          stroke-dasharray="3,3"
        />
        <path d={areaPath()} fill="url(#sparkGrad)" />
        <path d={linePath()} fill="none" stroke={color()} stroke-width="1.5" />
      </svg>
    </>
  );
}

export function PnlSparkline() {
  const data = () => state.timeseries;

  const sparkFallbackText = () => {
    if (isEndpointFailingPrefix('/api/v1/pnl/timeseries')) {
      return 'PnL 曲线拉取失败 — 点 ⚙ 确认 API Base, 或等待自动重试 (每 15s)';
    }
    return 'PnL 曲线加载中...';
  };

  return (
    <div id="pnl-sparkline-wrap">
      <Show
        when={data() && (data()!.buckets?.length ?? 0) >= 2}
        fallback={<div class="no-data spark-placeholder">{sparkFallbackText()}</div>}
      >
        <SparklineSvg data={data()!} />
      </Show>
    </div>
  );
}
