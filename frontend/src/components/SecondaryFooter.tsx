/**
 * SecondaryFooter.tsx — 折叠次要区 (PnL 归因瀑布 + Prometheus metrics)
 * owner: 小苏
 * last_review: 2026-05-29
 */

import { For, Show } from 'solid-js';
import { state, setState, refreshMetrics } from '../store';
import { fmtTs, fmtUsdc } from '../api';

const WF_LABELS: Record<string, string> = {
  gross:    '毛收益',
  fee:      '手续费',
  gas:      'Gas',
  slippage: '滑点',
  spread:   '价差收益',
  net:      '净收益',
};
const WF_ORDER = ['gross', 'fee', 'gas', 'slippage', 'spread', 'net'] as const;

function PnlAttributionPanel() {
  const data = () => state.attribution;

  return (
    <Show when={data()} fallback={<div class="no-data">PnL 归因</div>}>
      {(attr) => {
        const wf = () => attr().waterfall ?? {};
        const gross = () => Number(wf().gross ?? 0);

        return (
          <>
            <div class="panel-header">
              <span class="panel-ts">更新 {fmtTs(attr().as_of_ts)}</span>
            </div>
            <div class="attr-layout">
              <div class="wf-section">
                <div class="section-title">瀑布图</div>
                <For each={WF_ORDER}>
                  {(key) => {
                    const v = () => Number((wf() as unknown as Record<string, number>)[key] ?? 0);
                    const pct = () =>
                      gross() !== 0
                        ? Math.min((Math.abs(v()) / gross()) * 100, 100).toFixed(1)
                        : '0';
                    const isNet = key === 'net';
                    const barCls = () =>
                      isNet ? 'wf-net' : v() >= 0 ? 'wf-pos' : 'wf-neg';
                    return (
                      <div class="wf-row">
                        <div class="wf-label">{WF_LABELS[key] ?? key}</div>
                        <div class="wf-bar-wrap">
                          <div class={`wf-bar ${barCls()}`} style={{ width: `${pct()}%` }} />
                        </div>
                        <div class={`wf-val ${v() >= 0 ? 'pnl-pos' : 'pnl-neg'}`}>
                          {fmtUsdc(v())}
                        </div>
                      </div>
                    );
                  }}
                </For>
              </div>
              <div class="pm-section">
                <div class="section-title">分市场</div>
                <Show when={(attr().per_market ?? []).length > 0} fallback={<div class="no-data">—</div>}>
                  <For each={attr().per_market}>
                    {(m) => (
                      <div class="pm-row">
                        <span class="mono pm-id">{m.market_id}</span>
                        <span class={Number(m.net_pnl) >= 0 ? 'pnl-pos' : 'pnl-neg'}>
                          {fmtUsdc(m.net_pnl)}
                        </span>
                      </div>
                    )}
                  </For>
                </Show>
              </div>
            </div>
          </>
        );
      }}
    </Show>
  );
}

function MetricsPanel() {
  const text = () => state.metrics;
  const escaped = () =>
    text()
      ?.replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;') ?? '';

  return (
    <Show
      when={text()}
      fallback={<div class="no-data">Prometheus /metrics</div>}
    >
      <pre class="metrics-pre" innerHTML={escaped()} />
    </Show>
  );
}

export function SecondaryFooter() {
  function handleToggle(e: Event) {
    const open = (e.currentTarget as HTMLDetailsElement).open;
    setState({ secondaryOpen: open });
    if (open) void refreshMetrics(true);
  }

  return (
    <details
      id="secondary-details"
      class="secondary-section"
      onToggle={handleToggle}
    >
      <summary class="secondary-summary">
        次要信息 (PnL 归因瀑布 / raw metrics)
      </summary>
      <div class="secondary-inner">
        <div class="panel-card">
          <div class="panel-title">
            PnL 归因 (瀑布) <span class="poll-hint">15s</span>
          </div>
          <PnlAttributionPanel />
        </div>
        <div class="panel-card">
          <div class="panel-title">
            Prometheus /metrics <span class="poll-hint">30s</span>
          </div>
          <MetricsPanel />
        </div>
      </div>
    </details>
  );
}
