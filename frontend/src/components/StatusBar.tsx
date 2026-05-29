/**
 * StatusBar.tsx — 跨页常驻状态条 (v6)
 * 替换 v5 GlobalBar, 精简为顶部一行关键健康信息
 * owner: 小苏  last_review: 2026-05-29
 *
 * 内容: mode badge / 系统状态 / 净PnL / 运行时间 / WSS 状态点 /
 *        Gate 初/确认审 / API 异常 chip / 设置按钮
 */

import { createSignal, For, Show } from 'solid-js';
import { state } from '../store';
import {
  fmtUsdc, fmtUptime, isEndpointFailing, failingEndpointsSummary,
} from '../api';
import { Badge, modeVariant, modeText } from './ui/Badge';
import { StatusDot, boolToDot } from './ui/StatusDot';

export function StatusBar() {
  const [settingsOpen, setSettingsOpen] = createSignal(false);
  const [apiBaseInput, setApiBaseInput] = createSignal(
    localStorage.getItem('stcpp_api_base') ?? 'http://127.0.0.1:8080',
  );

  const s = () => state.status ?? ({} as NonNullable<typeof state.status>);
  const h = () => state.healthz ?? ({} as NonNullable<typeof state.healthz>);
  const g = () => state.gate   ?? ({} as NonNullable<typeof state.gate>);

  const isDemo = () => s().data_source !== 'live';
  const mode   = () => s().mode ?? 'paper';

  const stateClass = () => {
    const st = s().state;
    if (st === 'RUNNING') return 'state-running';
    if (st === 'HALTED')  return 'state-halted';
    return 'state-drain';
  };

  const stateText = () => {
    if (!state.status)
      return isEndpointFailing('/status') ? '后端未连接' : '连接中...';
    return s().state ?? '—';
  };

  const uptimeSec = () => Number(h().uptime_sec ?? s().uptime_sec ?? 0);

  const wssEntries = () =>
    Object.entries(s().wss_connected ?? {}) as [string, boolean][];

  const netPnl = () => {
    const attr = state.attribution;
    if (attr?.waterfall) return Number(attr.waterfall.net);
    return null;
  };

  const rmRejects = () => s().rm_rejects_last_60s;

  const hasApiErr = () =>
    isEndpointFailing('/status') || isEndpointFailing('/api/v1/positions');

  const apiErrTooltip = () => {
    const summary = failingEndpointsSummary(3);
    return summary ? `失败端点:\n${summary}` : 'API 异常';
  };

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

      {/* 常驻状态条 */}
      <div class="status-bar">
        {/* 模式 badge */}
        <Badge variant={modeVariant(mode())}>{modeText(mode())}</Badge>

        {/* 系统状态 */}
        <span class={`state-label ${stateClass()}`}
          title={!state.status && isEndpointFailing('/status')
            ? '后端未连接 · 请检查 8080 或点 ⚙ 改 API Base'
            : undefined}
        >
          {stateText()}
        </span>

        <span class="top-sep">|</span>

        {/* 净PnL */}
        <span class="top-label">净PnL</span>
        <span class={`top-pnl ${netPnl() != null ? (netPnl()! >= 0 ? 'pnl-pos' : 'pnl-neg') : ''}`}>
          {netPnl() != null ? fmtUsdc(netPnl()) : '—'}
        </span>

        <span class="top-sep">|</span>

        {/* 运行时间 */}
        <span class="top-dim">运行 {fmtUptime(uptimeSec())}</span>

        <span class="top-sep">|</span>

        {/* WSS 状态 */}
        <span class="top-label">WSS</span>
        <For each={wssEntries()}>
          {([k, v]) => (
            <StatusDot
              state={boolToDot(v)}
              size="sm"
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

        {/* 拒单/60s */}
        <span class="top-label">拒单/60s</span>
        <span class={`mono-dim ${rmRejects() != null && rmRejects()! > 0 ? 'pnl-neg' : ''}`}>
          {rmRejects() != null ? String(rmRejects()) : '—'}
        </span>

        {/* API 异常 */}
        <Show when={hasApiErr()}>
          <span class="api-err-chip" title={apiErrTooltip()}>API 异常</span>
        </Show>

        {/* 设置按钮 */}
        <div class="top-right">
          <button
            class="icon-btn"
            title="API 配置"
            onClick={() => setSettingsOpen((v) => !v)}
          >
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
