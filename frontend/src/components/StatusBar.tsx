/**
 * StatusBar.tsx — 跨页常驻 Material AppBar 状态条 (v7)
 * owner: 小苏  last_review: 2026-05-29
 *
 * v7 变更: AppBar + Toolbar + Chip + IconButton (SUID Material)
 * 内容: mode chip / 系统状态 / 净PnL / 运行时间 / WSS dot /
 *        Gate chip / 拒单/60s / API 异常 chip / 设置按钮
 */

import { createSignal, For, Show } from 'solid-js';
import AppBar from '@suid/material/AppBar';
import Toolbar from '@suid/material/Toolbar';
import Chip from '@suid/material/Chip';
import IconButton from '@suid/material/IconButton';
import Alert from '@suid/material/Alert';
import Typography from '@suid/material/Typography';
import Divider from '@suid/material/Divider';
import TextField from '@suid/material/TextField';
import Button from '@suid/material/Button';
import Box from '@suid/material/Box';
import { state } from '../store';
import {
  fmtUsdc, fmtUptime, isEndpointFailing, failingEndpointsSummary, setBaseUrl, getBaseUrl,
} from '../api';
import { StatusDot, boolToDot } from './ui/StatusDot';

export function StatusBar() {
  const [settingsOpen, setSettingsOpen] = createSignal(false);
  const [apiBaseInput, setApiBaseInput] = createSignal(getBaseUrl());

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

  const apiErrTooltip = () => failingEndpointsSummary(3) || 'API 异常';

  const modeColor = () =>
    mode() === 'live' ? 'error' : mode() === 'paper' ? 'default' : 'info';

  function saveApiBase() {
    const v = apiBaseInput().trim();
    if (v) setBaseUrl(v);
  }

  return (
    <>
      {/* DEMO 横幅 — Material Alert (P0-02 红线: 非实盘必须显示) */}
      <Show when={isDemo()}>
        <Alert
          severity="warning"
          class="demo-alert-banner"
          sx={{ borderRadius: 0, py: 0.5, px: 2, fontSize: '12px', fontWeight: 700 }}
        >
          演示数据 · 非实盘 — 所有量化参数仅供参考, 不触发下单
        </Alert>
      </Show>

      {/* Material AppBar */}
      <AppBar position="sticky" color="default" elevation={1}
        sx={{ zIndex: 100, bgcolor: 'background.paper', borderBottom: '1px solid #373737' }}>
        <Toolbar variant="dense" sx={{ gap: 1, flexWrap: 'wrap', minHeight: '44px', py: 0.5 }}>
          {/* Logo */}
          <Typography variant="subtitle2" sx={{ fontWeight: 700, mr: 1, color: 'primary.main', letterSpacing: '0.04em', whiteSpace: 'nowrap' }}>
            STCPP
          </Typography>

          {/* Mode chip */}
          <Chip
            label={mode().toUpperCase()}
            color={modeColor()}
            size="small"
            variant={mode() === 'live' ? 'filled' : 'outlined'}
            sx={{ fontWeight: 700, fontSize: '10px', height: '20px' }}
          />

          {/* 系统状态 */}
          <Typography
            variant="caption"
            class={stateClass()}
            title={!state.status && isEndpointFailing('/status') ? '后端未连接 · 请检查 8080 或点 ⚙ 改 API Base' : undefined}
            sx={{ fontWeight: 700, fontSize: '11px', whiteSpace: 'nowrap' }}
          >
            {stateText()}
          </Typography>

          <span class="appbar-sep">|</span>

          {/* 净PnL */}
          <Typography variant="caption" sx={{ color: 'text.secondary', fontSize: '11px' }}>净PnL</Typography>
          <Show when={netPnl() != null}>
            <Typography
              variant="caption"
              class={`mono-main ${netPnl()! >= 0 ? 'pnl-pos' : 'pnl-neg'}`}
              sx={{ fontSize: '13px' }}
            >
              {fmtUsdc(netPnl()!)}
            </Typography>
          </Show>
          <Show when={netPnl() == null}>
            <Typography variant="caption" sx={{ color: 'text.disabled', fontFamily: 'monospace' }}>—</Typography>
          </Show>

          <span class="appbar-sep">|</span>

          {/* 运行时间 */}
          <Typography variant="caption" class="appbar-dim">运行 {fmtUptime(uptimeSec())}</Typography>

          <span class="appbar-sep">|</span>

          {/* WSS 状态 */}
          <Typography variant="caption" sx={{ color: 'text.secondary', fontSize: '11px' }}>WSS</Typography>
          <For each={wssEntries()}>
            {([k, v]) => (
              <StatusDot state={boolToDot(v)} size="sm" title={k} />
            )}
          </For>

          <span class="appbar-sep">|</span>

          {/* Gate */}
          <Show when={g().has_data}>
            <Chip
              label={g().prelim_pass ? '初审✓' : '初审✗'}
              color={g().prelim_pass ? 'success' : 'error'}
              size="small"
              variant="outlined"
              sx={{ fontSize: '10px', height: '20px' }}
            />
            <Chip
              label={g().confirm_pass ? '确认✓' : '确认✗'}
              color={g().confirm_pass ? 'success' : 'error'}
              size="small"
              variant="outlined"
              sx={{ fontSize: '10px', height: '20px' }}
            />
          </Show>
          <Show when={!g().has_data}>
            <Typography variant="caption" sx={{ color: 'text.disabled' }}>—</Typography>
          </Show>

          <span class="appbar-sep">|</span>

          {/* 拒单/60s */}
          <Typography variant="caption" sx={{ color: 'text.secondary', fontSize: '11px' }}>拒单/60s</Typography>
          <Typography
            variant="caption"
            sx={{
              fontFamily: 'monospace',
              fontSize: '12px',
              color: (rmRejects() ?? 0) > 0 ? 'error.main' : 'text.secondary',
            }}
          >
            {rmRejects() != null ? String(rmRejects()) : '—'}
          </Typography>

          {/* API 异常 Chip */}
          <Show when={hasApiErr()}>
            <Chip
              label="API 异常"
              color="error"
              size="small"
              title={apiErrTooltip()}
              sx={{ fontSize: '10px', height: '20px', animation: 'blink 1.6s step-end infinite' }}
            />
          </Show>

          {/* 右侧: 设置按钮 */}
          <Box sx={{ ml: 'auto' }}>
            <IconButton
              size="small"
              title="API 配置"
              onClick={() => setSettingsOpen((v) => !v)}
              color={settingsOpen() ? 'primary' : 'default'}
            >
              <span style={{ 'font-size': '16px' }}>&#9881;</span>
            </IconButton>
          </Box>
        </Toolbar>
      </AppBar>

      {/* 设置面板 */}
      <Show when={settingsOpen()}>
        <div class="settings-panel-wrap">
          <Typography variant="caption" sx={{ color: 'text.secondary', mr: 1 }}>API Base</Typography>
          <TextField
            size="small"
            variant="outlined"
            placeholder="http://127.0.0.1:8080"
            value={apiBaseInput()}
            onInput={(e) => setApiBaseInput((e.currentTarget as HTMLInputElement).value)}
            sx={{ width: 280, '& input': { fontFamily: 'monospace', fontSize: '12px', py: '4px' } }}
          />
          <Button variant="contained" size="small" onClick={saveApiBase}>
            保存并刷新
          </Button>
          <Typography variant="caption" sx={{ color: 'text.disabled', ml: 1 }}>
            默认 127.0.0.1:8080 · 加 ?stub=1 使用 mock 数据
          </Typography>
        </div>
      </Show>
    </>
  );
}
