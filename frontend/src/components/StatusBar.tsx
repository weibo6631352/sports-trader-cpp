/**
 * StatusBar.tsx — 跨页常驻 Material AppBar 状态条 (v8)
 * owner: 小苏  last_review: 2026-05-29
 *
 * v8 变更:
 *  - 去掉 DEMO 横幅 (data_source 恒 live, 不再有 demo 模式)
 *  - 替换为 LIVE 实时标识 + 连接/加载/stale 状态
 *  - Stub 横幅仅 ?stub=1 时显示
 */

import { createSignal, For, Show } from 'solid-js';
import AppBar from '@suid/material/AppBar';
import Toolbar from '@suid/material/Toolbar';
import Chip from '@suid/material/Chip';
import IconButton from '@suid/material/IconButton';
import Typography from '@suid/material/Typography';
import TextField from '@suid/material/TextField';
import Button from '@suid/material/Button';
import Box from '@suid/material/Box';
import { state } from '../store';
import {
  fmtUsdc, fmtUptime, isEndpointFailing, failingEndpointsSummary, setBaseUrl, getBaseUrl,
} from '../api';
import { StatusDot, boolToDot } from './ui/StatusDot';
import { USE_STUB } from '../store';

export function StatusBar() {
  const [settingsOpen, setSettingsOpen] = createSignal(false);
  const [apiBaseInput, setApiBaseInput] = createSignal(getBaseUrl());

  const s = () => state.status ?? ({} as NonNullable<typeof state.status>);
  const h = () => state.healthz ?? ({} as NonNullable<typeof state.healthz>);
  const g = () => state.gate   ?? ({} as NonNullable<typeof state.gate>);

  const mode = () => s().mode ?? 'live';

  // v8: 连接状态逻辑 (data_source 恒 live, 根据后端是否响应判断)
  const isConnected    = () => state.status != null;
  const isConnecting   = () => state.status == null && !isEndpointFailing('/status');
  const isBackendDown  = () => state.status == null && isEndpointFailing('/status');
  const eventsCount    = () => state.eventGroups.length;

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

  // 空态语境: 有 PnL 返回但无持仓/成交 (P1 空态)
  const hasFills = () => (state.attribution?.per_market?.length ?? 0) > 0;
  const pnlIsZeroNoFills = () => netPnl() === 0 && !hasFills();

  const rmRejects = () => s().rm_rejects_last_60s;

  const hasApiErr = () =>
    isEndpointFailing('/status') || isEndpointFailing('/api/v1/events');

  const apiErrTooltip = () => failingEndpointsSummary(3) || 'API 异常';

  const modeColor = () =>
    mode() === 'live' ? 'error' : mode() === 'paper' ? 'default' : 'info';

  function saveApiBase() {
    const v = apiBaseInput().trim();
    if (v) setBaseUrl(v);
  }

  return (
    <>
      {/* Stub 横幅 — 仅 ?stub=1 时显示 */}
      <Show when={USE_STUB}>
        <div id="stub-banner">
          STUB 模式 — 本地 mock 数据 (URL 含 ?stub=1). 移除参数后连接真实 API.
        </div>
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

          {/* v8: LIVE 实时标识 (连接中/实时/后端未连接) */}
          <Show when={isConnected()}>
            <span class="live-status-badge live-status-ok">
              实时 LIVE
            </span>
          </Show>
          <Show when={isConnecting()}>
            <span class="live-status-badge live-status-connecting">
              连接中...
            </span>
          </Show>
          <Show when={isBackendDown()}>
            <span class="live-status-badge live-status-stale">
              后端离线
            </span>
          </Show>

          {/* 系统状态 */}
          <Typography
            variant="caption"
            class={stateClass()}
            title={!state.status && isEndpointFailing('/status') ? '后端未连接 · 请检查 7080 或点 ⚙ 改 API Base' : undefined}
            sx={{ fontWeight: 700, fontSize: '11px', whiteSpace: 'nowrap' }}
          >
            {stateText()}
          </Typography>

          <span class="appbar-sep">|</span>

          {/* 订阅市场数 */}
          <Show when={eventsCount() > 0}>
            <Typography variant="caption" sx={{ color: 'text.secondary', fontSize: '11px' }}>
              {eventsCount()} 赛事
            </Typography>
          </Show>

          <span class="appbar-sep">|</span>

          {/* 净PnL — 空态语境: $0.00 无成交时加说明，避免误以为统计失效 (P1 空态) */}
          <Typography variant="caption" sx={{ color: 'text.secondary', fontSize: '11px' }}>净PnL</Typography>
          <Show when={netPnl() != null}>
            <Typography
              variant="caption"
              class={`mono-main ${netPnl()! >= 0 ? 'pnl-pos' : 'pnl-neg'}`}
              sx={{ fontSize: '13px' }}
              title={pnlIsZeroNoFills() ? 'paper 启动中，尚无成交记录' : undefined}
            >
              {fmtUsdc(netPnl()!)}
              <Show when={pnlIsZeroNoFills()}>
                <span style={{ 'font-size': '9px', color: '#888', 'margin-left': '4px', 'font-weight': '400', 'font-family': 'monospace' }}>(无成交)</span>
              </Show>
            </Typography>
          </Show>
          <Show when={netPnl() == null}>
            <Typography variant="caption" sx={{ color: 'text.disabled', fontFamily: 'monospace' }}>
              暂无 paper 数据
            </Typography>
          </Show>

          <span class="appbar-sep">|</span>

          {/* 运行时间 */}
          <Typography variant="caption" class="appbar-dim">运行 {fmtUptime(uptimeSec())}</Typography>

          <span class="appbar-sep">|</span>

          {/* WSS 状态. 仅 clob 是真用的 WSS (订单簿); user_channel 仅 live 真单订阅 (paper 不订),
              false 是预期, 灰显而非红色告警. (sports_api 通道已删 2026-06-02 — Goalserve 走 HTTP REST 非 WSS) */}
          <Typography variant="caption" sx={{ color: 'text.secondary', fontSize: '11px' }}>WSS</Typography>
          <For each={wssEntries()}>
            {([k, v]) => {
              // 未使用的通道 (paper 模式预期 false) → 灰显 + 解释 tooltip, 不红色误报。
              const unusedChannel = k === 'user_channel';
              const dotState = (): 'green' | 'red' | 'gray' =>
                v ? 'green' : unusedChannel ? 'gray' : 'red';
              const tip =
                k === 'clob' ? 'CLOB 订单簿 WSS (真实使用)'
                : k === 'user_channel' ? 'Polymarket user channel (仅 live 真单订阅; paper 模式不用, 灰=正常)'
                : k;
              return <StatusDot state={dotState()} size="sm" title={`${k}: ${tip}`} />;
            }}
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
            <Typography variant="caption" sx={{ color: 'text.disabled' }}>Gate —</Typography>
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
            placeholder="http://127.0.0.1:7080"
            value={apiBaseInput()}
            onInput={(e) => setApiBaseInput((e.currentTarget as HTMLInputElement).value)}
            sx={{ width: 280, '& input': { fontFamily: 'monospace', fontSize: '12px', py: '4px' } }}
          />
          <Button variant="contained" size="small" onClick={saveApiBase}>
            保存并刷新
          </Button>
        </div>
      </Show>
    </>
  );
}
