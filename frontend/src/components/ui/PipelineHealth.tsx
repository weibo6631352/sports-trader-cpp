/**
 * PipelineHealth — 4 时间戳瀑布状态行 (ADR R-20)
 * event_ts → data_source_ts → ingestion_ts → as_of_ts
 * 每段显示延迟 ms + 颜色状态
 * owner: 小苏  last_review: 2026-05-29
 */

import { For, Show } from 'solid-js';
import { StatusDot } from './StatusDot';

interface PipelineHealthProps {
  /** epoch_ns timestamps: [event_ts, data_source_ts, ingestion_ts, as_of_ts] */
  timestamps: (number | null | undefined)[];
  labels?: string[];
}

const DEFAULT_LABELS = ['event', 'src', 'ingest', 'as_of'];

function segDeltaMs(a: number | null | undefined, b: number | null | undefined): number | null {
  if (a == null || b == null) return null;
  const delta = (Number(b) - Number(a)) / 1e6;
  return Number.isFinite(delta) ? delta : null;
}

function deltaCls(ms: number | null): 'green' | 'yellow' | 'red' | 'gray' {
  if (ms == null) return 'gray';
  if (ms < 0)    return 'red';   // 倒挂告警
  if (ms < 100)  return 'green';
  if (ms < 1000) return 'yellow';
  return 'red';
}

function fmtDeltaMs(ms: number | null): string {
  if (ms == null) return '—';
  if (ms < 0) return `${ms.toFixed(0)}ms(倒挂!)`;
  if (ms < 1000) return `${Math.round(ms)}ms`;
  return `${(ms / 1000).toFixed(1)}s`;
}

export function PipelineHealth(props: PipelineHealthProps) {
  const labels = () => props.labels ?? DEFAULT_LABELS;
  const ts     = () => props.timestamps;

  // 各段 delta: [0→1, 1→2, 2→3]
  const deltas = () => {
    const t = ts();
    return [
      segDeltaMs(t[0], t[1]),
      segDeltaMs(t[1], t[2]),
      segDeltaMs(t[2], t[3]),
    ];
  };

  return (
    <div class="pipeline-health">
      <For each={labels()}>
        {(lbl, i) => (
          <>
            <div class="pipeline-segment">
              <StatusDot state={deltaCls(i() === 0 ? null : deltas()[i() - 1])} size="sm" />
              <span class="pipeline-label">{lbl}</span>
            </div>
            <Show when={i() < labels().length - 1}>
              <span class="pipeline-arrow">→</span>
              <span
                class={`pipeline-val`}
                style={{ color: `var(--${deltaCls(deltas()[i()]) === 'green' ? 'green' : deltaCls(deltas()[i()]) === 'yellow' ? 'yellow' : deltaCls(deltas()[i()]) === 'red' ? 'red' : 'text-dim'})` }}
                title={`${lbl}→${labels()[i() + 1]}: ${fmtDeltaMs(deltas()[i()])}`}
              >
                {fmtDeltaMs(deltas()[i()])}
              </span>
              <span class="pipeline-arrow">→</span>
            </Show>
          </>
        )}
      </For>
    </div>
  );
}
