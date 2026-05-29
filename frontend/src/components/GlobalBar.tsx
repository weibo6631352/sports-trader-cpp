/**
 * GlobalBar.tsx — 顶部常驻条 + 设置面板
 * owner: 小苏
 * last_review: 2026-05-29
 */

import { createSignal, For, Show } from 'solid-js';
import { state } from '../store';
import {
  fmtUsdc, fmtUptime, modeBadge, isEndpointFailing,
} from '../api';

export function GlobalBar() {
  const [settingsOpen, setSettingsOpen] = createSignal(false);
  const [apiBaseInput, setApiBaseInput] = createSignal(
    localStorage.getItem('stcpp_api_base') ?? 'http://127.0.0.1:8080',
  );

  const s = () => state.status ?? ({} as NonNullable<typeof state.status>);
  const h = () => state.healthz ?? ({} as NonNullable<typeof state.healthz>);
  const g = () => state.gate ?? ({} as NonNullable<typeof state.gate>);

  const isDemo = () => s().data_source !== 'live';

  const modeInfo = () => modeBadge(s().mode ?? 'paper');

  const stateClass = () => {
    const st = s().state;
    if (st === 'RUNNING') return 'state-running';
    if (st === 'HALTED') return 'state-halted';
    return 'state-drain';
  };

  const stateText = () => {
    if (!state.status) {
      return isEndpointFailing('/status') ? 'API OFFLINE' : '连接中...';
    }
    return s().state ?? '—';
  };

  const uptimeSec = () => Number(h().uptime_sec ?? s().uptime_sec ?? 0);

  const wssEntries = () => Object.entries(s().wss_connected ?? {}) as [string, boolean][];

  const netPnl = () => {
    const attr = state.attribution;
    if (attr?.waterfall) return Number(attr.waterfall.net);
    return null;
  };

  const p99Text = () => {
    const m = state.metrics;
    if (!m) return null;
    const match = m.match(/stcpp_loop_latency_p99_us[^\n]*\s+([\d.]+)/);
    return match ? `${Number(match[1]).toFixed(0)} us` : null;
  };

  const stalenessInfo = (): { text: string; cls: string } | null => {
    const m = state.metrics;
    if (!m) return null;
    const match = m.match(/stcpp_data_staleness_ms_max[^\n]*\s+([\d.]+)/);
    if (!match) return null;
    const ms = Number(match[1]);
    const cls = ms < 100 ? 'stale-dim' : ms < 1000 ? 'stale-warn-text' : 'stale-err-text';
    return { text: `${ms.toFixed(0)} ms`, cls };
  };

  const rmRejects = () => s().rm_rejects_last_60s;

  const hasApiErr = () =>
    isEndpointFailing('/status') || isEndpointFailing('/api/v1/positions');

  function saveApiBase() {
    const v = apiBaseInput().trim();
    if (v) {
      localStorage.setItem('stcpp_api_base', v);
      location.reload();
    }
  }

  return (
    <>
      {/* DEMO 横幅 */}
      <Show when={isDemo()}>
        <div id="demo-banner">
          演示数据 · 非实盘 — 所有量化参数仅供参考, 不触发下单
        </div>
      </Show>

      {/* 顶部常驻条 */}
      <div class="top-bar">
        <span class={`badge ${modeInfo().cls}`}>{modeInfo().text}</span>
        <span class={`state-label ${stateClass()}`}>{stateText()}</span>
        <span class="top-sep">|</span>
        <span class="top-label">净PnL</span>
        <span class={`top-pnl ${netPnl() != null ? (netPnl()! >= 0 ? 'pnl-pos' : 'pnl-neg') : ''}`}>
          {netPnl() != null ? fmtUsdc(netPnl()) : '—'}
        </span>
        <span class="top-sep">|</span>
        <span class="top-dim">运行 {fmtUptime(uptimeSec())}</span>
        <span class="top-sep">|</span>
        <span class="top-label">WSS</span>
        <For each={wssEntries()}>
          {([k, v]) => (
            <span
              class={`wss-dot-sm ${v ? 'wss-dot-ok' : 'wss-dot-off'}`}
              title={k}
            />
          )}
        </For>
        <span class="top-sep">|</span>
        {/* Gate */}
        <Show when={g().has_data} fallback={<span class="ph">—</span>}>
          <span class={`gate-chip ${g().prelim_pass ? 'gate-ok' : 'gate-fail'}`}>
            初审{g().prelim_pass ? '✓' : '✗'}
          </span>
          {' '}
          <span class={`gate-chip ${g().confirm_pass ? 'gate-ok' : 'gate-fail'}`}>
            确认{g().confirm_pass ? '✓' : '✗'}
          </span>
        </Show>
        <span class="top-sep">|</span>
        <span class="top-label">p99</span>
        <span class="top-dim">{p99Text() ?? '—'}</span>
        <span class="top-sep">|</span>
        <span class="top-label">数据延迟</span>
        <Show when={stalenessInfo()} fallback={<span class="ph">—</span>}>
          {(info) => <span class={`${info().cls} mono-dim`}>{info().text}</span>}
        </Show>
        <span class="top-sep">|</span>
        <span class="top-label">拒单/60s</span>
        <span class={`mono-dim ${rmRejects() != null && rmRejects()! > 0 ? 'pnl-neg' : ''}`}>
          {rmRejects() != null ? String(rmRejects()) : '—'}
        </span>
        <Show when={hasApiErr()}>
          <span class="api-err-chip">API 异常</span>
        </Show>
        <div class="top-right">
          <button class="icon-btn" title="API 配置" onClick={() => setSettingsOpen((v) => !v)}>
            &#9881;
          </button>
        </div>
      </div>

      {/* 设置面板 */}
      <Show when={settingsOpen()}>
        <div id="settings-panel">
          <div class="settings-row">
            <label>API Base</label>
            <input
              type="text"
              placeholder="http://127.0.0.1:8080"
              value={apiBaseInput()}
              onInput={(e) => setApiBaseInput(e.currentTarget.value)}
            />
            <button class="btn-primary" onClick={saveApiBase}>保存并刷新</button>
          </div>
          <div class="settings-hint">默认 127.0.0.1:8080. 加 ?stub=1 使用 mock 数据.</div>
        </div>
      </Show>
    </>
  );
}
