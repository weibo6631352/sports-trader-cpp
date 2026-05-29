/**
 * PnlSparkline.tsx — PnL 净值曲线 SVG (v7)
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * v7: 去掉 pnl-sparkline-wrap id, 用 spark-section class (在父层)
 */

import { Show } from 'solid-js';
import { state } from '../store';
import { fmtTs, fmtUsdc, isEndpointFailingPrefix } from '../api';
import type { PnlTimeseries } from '../types';

function SparklineSvg(props: { data: PnlTimeseries }) {
  const buckets = () => props.data.buckets ?? [];
  const vals    = () => buckets().map((b) => Number(b.cum_net_pnl));

  const min   = () => Math.min(...vals());
  const max   = () => Math.max(...vals());
  const range = () => (max() - min()) || 1;

  const W  = 900;
  const H  = 52;
  const pX = 4;
  const pY = 5;

  const points = () =>
    vals().map((v, i) => {
      const x = pX + ((W - pX * 2) * i) / (vals().length - 1);
      const y = pY + (H - pY * 2) * (1 - (v - min()) / range());
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    });

  const zeroY = () => {
    const raw = pY + (H - pY * 2) * (1 - (0 - min()) / range());
    return Math.min(H - pY, Math.max(pY, raw));
  };

  const areaPath = () =>
    `M ${pX},${zeroY().toFixed(1)} L ${points().join(' L ')} L ${(pX + (W - pX * 2)).toFixed(1)},${zeroY().toFixed(1)} Z`;

  const linePath = () => `M ${points().join(' L ')}`;

  const latestPnl = () => {
    const v = vals();
    return v.length > 0 ? v[v.length - 1] : 0;
  };

  const color = () => (latestPnl() >= 0 ? '#4caf50' : '#f44336');

  const windowSec  = () => Number(props.data.window_sec ?? 3600);
  const windowLabel = () =>
    windowSec() >= 3600 ? `近 ${Math.round(windowSec() / 3600)}h` : `近 ${Math.round(windowSec() / 60)}min`;
  const firstTs   = () => buckets().length > 0 ? fmtTs(buckets()[0].bucket_start_ts) : '—';

  return (
    <>
      <div class="spark-header">
        <span class="spark-label">净值曲线</span>
        <span class="spark-window">{windowLabel()} · {firstTs()} – {fmtTs(props.data.as_of_ts)}</span>
        <span class={latestPnl() >= 0 ? 'pnl-pos mono-strong' : 'pnl-neg mono-strong'}>
          {fmtUsdc(latestPnl())}
        </span>
        <span class="spark-ts">更新 {fmtTs(props.data.as_of_ts)}</span>
      </div>
      <svg viewBox={`0 0 ${W} ${H}`} width="100%" height={H} class="spark-svg">
        <defs>
          <linearGradient id="sparkGradV7" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color={color()} stop-opacity="0.28" />
            <stop offset="100%" stop-color={color()} stop-opacity="0.02" />
          </linearGradient>
        </defs>
        <line
          x1={pX} y1={zeroY().toFixed(1)}
          x2={W - pX} y2={zeroY().toFixed(1)}
          stroke="#444" stroke-width="0.5" stroke-dasharray="3,3"
        />
        <path d={areaPath()} fill="url(#sparkGradV7)" />
        <path d={linePath()} fill="none" stroke={color()} stroke-width="1.5" />
      </svg>
    </>
  );
}

export function PnlSparkline() {
  const data = () => state.timeseries;

  const fallbackText = () => {
    if (isEndpointFailingPrefix('/api/v1/pnl/timeseries')) {
      return 'PnL 曲线拉取失败 — 点 ⚙ 确认 API Base, 或等待自动重试 (每 15s)';
    }
    return 'PnL 曲线加载中...';
  };

  return (
    <Show
      when={data() && (data()!.buckets?.length ?? 0) >= 2}
      fallback={<div class="spark-placeholder">{fallbackText()}</div>}
    >
      <SparklineSvg data={data()!} />
    </Show>
  );
}
