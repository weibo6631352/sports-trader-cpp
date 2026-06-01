/**
 * OpsPage.tsx — 开发者/运维观测页 (v8 Material Design)
 * owner: 小苏  last_review: 2026-05-30
 *
 * v8 变更 (老板要求):
 *  - §1.6 新增 CoverageSection: 覆盖率/匹配率 三率 widget
 *    · 盘口识别率 (stcpp_market_type_recognized_total / _unknown_total)
 *    · 市场覆盖   (stcpp_markets_discovered_total / _subscribed_total / stcpp_tokens_subscribed_total)
 *    · 比分匹配率 (stcpp_score_matched_total / stcpp_markets_total)
 *  - 各率低值/0 时附语境文案 (outright 无 inplay → 0% 正常)
 *
 * v7 变更:
 *  - SUID Card/CardHeader/CardContent: 各区块
 *  - SUID Table/TableHead/TableRow/TableCell: 拒单日志
 *  - SUID Chip: 状态/拒单类型
 *  - SUID LinearProgress: staleness / latency 可视化
 *  - SUID Grid: 自适应卡片布局
 *  - 数据更全: loop p99 / staleness / gap / drift / reconnect / fill / Gate
 *
 * 按小郑规范:
 *   §1.1 订阅状态 (S-01~S-07)
 *   §1.2 系统健康 (H-01~H-07)
 *   §1.3 数据质量 (Q-01~Q-03, ADR R-20 四时间戳)
 *   §1.4 业务吞吐 (B-01~B-09, Gate 门禁)
 *   §1.5 错误/拒单 (E-01 日志 + E-02 分布柱状)
 *   §1.6 覆盖率/匹配率 (老板要求)
 *   + Prometheus 裸文本折叠
 */

import { createSignal, For, Show } from 'solid-js';
import Card from '@suid/material/Card';
import CardHeader from '@suid/material/CardHeader';
import CardContent from '@suid/material/CardContent';
import Table from '@suid/material/Table';
import TableHead from '@suid/material/TableHead';
import TableBody from '@suid/material/TableBody';
import TableRow from '@suid/material/TableRow';
import TableCell from '@suid/material/TableCell';
import TableContainer from '@suid/material/TableContainer';
import Chip from '@suid/material/Chip';
import LinearProgress from '@suid/material/LinearProgress';
import Typography from '@suid/material/Typography';
import Divider from '@suid/material/Divider';
import Button from '@suid/material/Button';
import Grid from '@suid/material/Grid';
import Alert from '@suid/material/Alert';
import Box from '@suid/material/Box';
import Paper from '@suid/material/Paper';
import { state } from '../store';
import { fmtTs, fmtUsdc, fmtUptime, stalenessMs } from '../api';
import { StatCard } from './ui/StatCard';
import { StatusDot, boolToDot } from './ui/StatusDot';
import { StalenessHeatCell } from './ui/StalenessHeatCell';
import { PipelineHealth } from './ui/PipelineHealth';
import type { RiskReject } from '../types';
import { REJECT_REASON_ZH, SIDE_ZH } from '../i18n';

// ============================================================
// Metrics 解析
// ============================================================

function parseMetricVal(text: string | null, metricName: string): number | null {
  if (!text) return null;
  const re = new RegExp(`${metricName}(?:\\{[^}]*\\})?\\s+([\\d.]+)`, 'g');
  let match: RegExpExecArray | null;
  let last: number | null = null;
  while ((match = re.exec(text)) !== null) last = Number(match[1]);
  return last;
}

function parseMetricAll(text: string | null, metricName: string): { labels: string; val: number }[] {
  if (!text) return [];
  const re = new RegExp(`${metricName}((?:\\{[^}]*\\})?)\\s+([\\d.]+)`, 'g');
  const results: { labels: string; val: number }[] = [];
  let match: RegExpExecArray | null;
  while ((match = re.exec(text)) !== null) results.push({ labels: match[1], val: Number(match[2]) });
  return results;
}

// ============================================================
// §1.2 系统健康区块
// ============================================================

function SystemHealthSection() {
  const s  = () => state.status;
  const h  = () => state.healthz;
  const m  = () => state.metrics;
  const uptime   = () => Number(h()?.uptime_sec ?? s()?.uptime_sec ?? 0);
  const mode     = () => s()?.mode ?? 'paper';
  const sysState = () => s()?.state ?? '—';
  const dataSource = () => s()?.data_source ?? '—';
  const loopP99  = () => parseMetricVal(m(), 'stcpp_loop_latency_p99_us');
  const loopP99Pct = () => loopP99() != null ? Math.min(loopP99()! / 1000, 1) * 100 : 0;
  const loopP99Color = (): 'success' | 'warning' | 'error' | 'inherit' => {
    const v = loopP99();
    if (v == null) return 'inherit';
    if (v < 200) return 'success';
    if (v < 500) return 'warning';
    return 'error';
  };
  const stateColor = (): 'success' | 'warning' | 'error' | 'default' => {
    const st = sysState();
    if (st === 'RUNNING') return 'success';
    if (st === 'HALTED')  return 'error';
    return 'warning';
  };
  const threads  = () => Object.entries(h()?.threads ?? {});
  const allAlive = () => threads().every(([, v]) => v === 'alive');

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              系统健康
            </Typography>
            <span class="poll-hint">5s</span>
            <StatusDot state={h()?.ok ? 'green' : h() == null ? 'gray' : 'red'} size="md"
              title={h()?.ok ? '系统正常' : '系统异常'} />
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        {/* 8 个 stat card */}
        <Grid container spacing={1.5} sx={{ mb: 2 }}>
          <Grid item xs={6} sm={3}>
            <StatCard label="系统状态" value={
              <Chip label={sysState()} color={stateColor()} size="small" sx={{ fontFamily: 'monospace', fontWeight: 700 }} />
            } pollHint="5s" />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="运行模式" value={
              <Chip
                label={mode().toUpperCase()}
                color={mode() === 'live' ? 'error' : 'default'}
                size="small"
                variant={mode() === 'live' ? 'filled' : 'outlined'}
                sx={{ fontWeight: 700 }}
              />
            } pollHint="5s" />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="运行时间" value={fmtUptime(uptime())} pollHint="5s" />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="数据源"
              value={dataSource()}
              color={dataSource() === 'live' ? 'green' : 'yellow'}
              pollHint="5s"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="活跃信号" value={String(s()?.signals_active_count ?? '—')} pollHint="5s" />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="持仓数量" value={String(s()?.positions_count ?? '—')} pollHint="5s" />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="拒单/60s"
              value={String(s()?.rm_rejects_last_60s ?? '—')}
              color={(s()?.rm_rejects_last_60s ?? 0) > 0 ? 'red' : 'green'}
              pollHint="5s"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="loop p99 (μs)"
              value={loopP99() != null ? loopP99()!.toFixed(0) : '—'}
              unit="μs"
              color={loopP99Color() === 'success' ? 'green' : loopP99Color() === 'warning' ? 'yellow' : loopP99Color() === 'error' ? 'red' : 'default'}
              pollHint="30s"
              title="热路径事件循环 P99 延迟"
            />
          </Grid>
        </Grid>

        {/* loop p99 LinearProgress */}
        <Show when={loopP99() != null}>
          <Box sx={{ mb: 2 }}>
            <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.5, display: 'block' }}>
              loop p99 延迟 (相对 1000μs)
            </Typography>
            <LinearProgress
              variant="determinate"
              value={loopP99Pct()}
              color={loopP99Color()}
              sx={{ height: 8, borderRadius: 3 }}
            />
          </Box>
        </Show>

        <Divider sx={{ mb: 1.5 }} />

        {/* 线程心跳 */}
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 1 }}>
          <Typography variant="caption" sx={{ color: 'text.secondary', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>
            线程心跳
          </Typography>
          <span class="poll-hint">5s</span>
          <Show when={threads().length > 0}>
            <Chip
              label={allAlive() ? '全部正常' : '有线程异常'}
              color={allAlive() ? 'success' : 'error'}
              size="small"
              sx={{ fontSize: '10px', height: '18px' }}
            />
          </Show>
          <span class="uncalib-chip" style={{ 'margin-left': '6px' }}>stub (W10+ 接 watchdog)</span>
        </Box>
        <div class="thread-grid">
          <Show when={threads().length > 0} fallback={<Typography variant="caption" sx={{ color: 'text.disabled' }}>线程数据未加载</Typography>}>
            <For each={threads()}>
              {([name, st]) => (
                <div class="thread-row">
                  <StatusDot state={st === 'alive' ? 'green' : 'red'} size="sm" />
                  <Typography class="thread-name">{name}</Typography>
                  <Typography class={`thread-state ${st === 'alive' ? 'thread-alive' : 'thread-dead'}`}>{st}</Typography>
                </div>
              )}
            </For>
          </Show>
        </div>
      </CardContent>
    </Card>
  );
}

// ============================================================
// §1.1 订阅状态 + WSS
// ============================================================

function SubscriptionSection() {
  const s = () => state.status;
  const m = () => state.metrics;
  const condCount   = () => Object.keys(state.conditionCache).length;
  const tokenEst    = () => condCount() * 2;
  const subTokens   = () => parseMetricVal(m(), 'stcpp_subscribed_tokens_total');
  const subMarkets  = () => parseMetricVal(m(), 'stcpp_subscribed_markets_total');
  const reconnectAll = () => parseMetricAll(m(), 'stcpp_wss_reconnect_total');

  const wssChannels = [
    { key: 'sports_api',   label: 'sports_api' },
    { key: 'clob',         label: 'clob' },
    { key: 'user_channel', label: 'user_channel' },
  ];

  const reconnectCount = (channel: string): number | null => {
    const allR = reconnectAll();
    const found = allR.find((r) => r.labels.includes(channel));
    if (found) return found.val;
    return parseMetricVal(m(), 'stcpp_wss_reconnect_total');
  };

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              市场订阅状态
            </Typography>
            <span class="poll-hint">5s</span>
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Grid container spacing={1.5} sx={{ mb: 2 }}>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="有报价市场"
              value={subMarkets() != null ? String(subMarkets()!) : `${condCount()} *`}
              sub={subMarkets() == null ? <span class="uncalib-chip">前端估算 · GAP-02</span> : undefined}
              pollHint="5s"
              title="有 live book 数据的市场数 (hub slot/2)。注: ≠ 订阅总数 — 订阅了但无盘口流动的 niche 盘不计入。"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="有报价 Token"
              value={subTokens() != null ? String(subTokens()!) : `${tokenEst()} *`}
              sub={subTokens() == null ? <span class="uncalib-chip">前端估算 · GAP-01</span> : undefined}
              pollHint="5s"
              title="有 live book 数据的 token 数 (hub token_count)。≠ 订阅总数。"
            />
          </Grid>
          <Grid item xs={12} sm={6}>
            {/* WSS 连接状态 */}
            <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.5, display: 'block', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>
              WSS 连接状态
            </Typography>
            <div class="wss-status-grid">
              <For each={wssChannels}>
                {(ch) => {
                  const ok = () => (s()?.wss_connected as Record<string, boolean> | undefined)?.[ch.key];
                  const rc = () => reconnectCount(ch.key);
                  return (
                    <div class="wss-status-row">
                      <StatusDot state={boolToDot(ok())} size="md" />
                      <Typography class="wss-channel-name">{ch.label}</Typography>
                      <Chip
                        label={ok() == null ? '—' : ok() ? 'CONNECTED' : 'DISCONNECTED'}
                        color={ok() ? 'success' : ok() == null ? 'default' : 'error'}
                        size="small"
                        variant="outlined"
                        sx={{ fontSize: '10px', height: '18px' }}
                      />
                      <Show when={rc() != null}>
                        <Typography
                          variant="caption"
                          class={`wss-reconnect-cnt${(rc() ?? 0) > 5 ? ' wss-reconnect-warn' : ''}`}
                        >
                          重连 {rc()}次
                        </Typography>
                      </Show>
                    </div>
                  );
                }}
              </For>
            </div>
          </Grid>
        </Grid>

        <Divider sx={{ mb: 1.5 }} />

        {/* 各 Condition WSS 热力 */}
        <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.75, display: 'block', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>
          各 Condition WSS 状态 <span class="poll-hint">5s</span>
        </Typography>
        <div class="staleness-heat-grid">
          <For each={Object.entries(state.conditionCache)}>
            {([condId, data]) => {
              const wssOk = () =>
                data.book?.token0?.wss_state === 'CONNECTED' &&
                data.book?.token1?.wss_state === 'CONNECTED';
              const shortId = condId.slice(0, 18) + (condId.length > 18 ? '…' : '');
              return (
                <div
                  class={`staleness-cell ${wssOk() ? 'staleness-cell-ok' : 'staleness-cell-err'}`}
                  title={`${condId} WSS: ${data.book?.token0?.wss_state ?? '—'} / ${data.book?.token1?.wss_state ?? '—'}`}
                >
                  <StatusDot state={wssOk() ? 'green' : 'red'} size="sm" />
                  {shortId}
                </div>
              );
            }}
          </For>
          <Show when={Object.keys(state.conditionCache).length === 0}>
            <Typography variant="caption" sx={{ color: 'text.disabled' }}>无已订阅市场</Typography>
          </Show>
        </div>
      </CardContent>
    </Card>
  );
}

// ============================================================
// §1.3 数据质量 (ADR R-20 四时间戳)
// ============================================================

function DataQualitySection() {
  const m = () => state.metrics;
  const stalenessMax = () => parseMetricVal(m(), 'stcpp_data_staleness_ms_max');
  const gapTotal     = () => parseMetricVal(m(), 'stcpp_feed_gap_total');
  const driftBps     = () => parseMetricVal(m(), 'stcpp_price_drift_bps');

  const stalenessColor = (): 'success' | 'warning' | 'error' | 'inherit' => {
    const v = stalenessMax();
    if (v == null) return 'inherit';
    if (v < 500)  return 'success';
    if (v < 2000) return 'warning';
    return 'error';
  };
  const stalenessPct = () => {
    const v = stalenessMax();
    return v != null ? Math.min(v / 2000, 1) * 100 : 0;
  };

  const sampleBook = () => {
    for (const data of Object.values(state.conditionCache)) {
      if (data.book) return data.book;
    }
    return null;
  };
  const ts4 = () => {
    const bk = sampleBook();
    if (!bk) return [null, null, null, null];
    return [bk.event_ts, bk.data_source_ts, bk.ingestion_ts, bk.as_of_ts_ns ?? bk.as_of_ts];
  };

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              数据质量 (ADR R-20 四时间戳)
            </Typography>
            <span class="poll-hint">30s</span>
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Grid container spacing={1.5} sx={{ mb: 2 }}>
          <Grid item xs={12} sm={4}>
            <StatCard
              label="最大 staleness"
              value={stalenessMax() != null ? `${stalenessMax()!.toFixed(0)}` : '—'}
              unit="ms"
              color={stalenessColor() === 'success' ? 'green' : stalenessColor() === 'warning' ? 'yellow' : stalenessColor() === 'error' ? 'red' : 'default'}
              title="stcpp_data_staleness_ms_max"
              pollHint="30s"
            />
          </Grid>
          <Grid item xs={12} sm={4}>
            <StatCard
              label="Gap 累计"
              value={gapTotal() != null ? String(gapTotal()!) : '—'}
              color={(gapTotal() ?? 0) === 0 ? 'green' : 'yellow'}
              title="stcpp_feed_gap_total"
              pollHint="30s"
            />
          </Grid>
          <Grid item xs={12} sm={4}>
            <StatCard
              label="价格漂移"
              value={driftBps() != null ? `${driftBps()!.toFixed(1)}` : '—'}
              unit="bps"
              color={(driftBps() ?? 0) > 50 ? 'yellow' : 'default'}
              title="stcpp_price_drift_bps: Goalserve vs PM"
              pollHint="30s"
            />
          </Grid>
        </Grid>

        {/* staleness LinearProgress */}
        <Box sx={{ mb: 2 }}>
          <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.5, display: 'block' }}>
            staleness 可视化 (相对 2000ms)
          </Typography>
          <LinearProgress
            variant="determinate"
            value={stalenessPct()}
            color={stalenessColor()}
            sx={{ height: 8, borderRadius: 3 }}
          />
        </Box>

        {/* 4 时间戳 pipeline */}
        <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.75, display: 'block', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>
          4 时间戳流水线 (采样首个市场)
        </Typography>
        <Show when={sampleBook()} fallback={<Typography variant="caption" sx={{ color: 'text.disabled' }}>无市场数据</Typography>}>
          <PipelineHealth timestamps={ts4()} />
          <Typography variant="caption" sx={{ color: 'text.disabled', display: 'block', mt: 0.5 }}>
            更新 {fmtTs(sampleBook()?.as_of_ts_ns ?? null)}
          </Typography>
        </Show>

        <Divider sx={{ my: 1.5 }} />

        {/* 各市场 staleness 热力表 */}
        <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.75, display: 'block', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>
          各市场 staleness 热力表
        </Typography>
        <div class="staleness-heat-grid">
          <For each={Object.entries(state.conditionCache)}>
            {([condId, data]) => {
              const ms = () =>
                data.book
                  ? stalenessMs(data.book.as_of_ts_ns ?? data.book.token0?.book_as_of_ts)
                  : null;
              const shortId = condId.slice(0, 14) + (condId.length > 14 ? '…' : '');
              return <StalenessHeatCell ms={ms()} label={shortId} />;
            }}
          </For>
          <Show when={Object.keys(state.conditionCache).length === 0}>
            <Typography variant="caption" sx={{ color: 'text.disabled' }}>无市场数据</Typography>
          </Show>
        </div>
      </CardContent>
    </Card>
  );
}

// ============================================================
// §1.4 业务吞吐
// ============================================================

function BusinessThroughputSection() {
  const m  = () => state.metrics;
  const g  = () => state.gate;
  const ts = () => state.timeseries;

  const rmDecision = () => parseMetricVal(m(), 'stcpp_rm_decision_total');
  const rmReject   = () => parseMetricVal(m(), 'stcpp_rm_reject_total');
  const fillTotal  = () => parseMetricVal(m(), 'stcpp_fill_total');
  const netPnl     = () => parseMetricVal(m(), 'stcpp_cum_net_pnl');
  const netEdge    = () => parseMetricVal(m(), 'stcpp_net_edge_bps');

  const rejectRate = () => {
    const d = rmDecision();
    const r = rmReject();
    if (d == null || r == null || d === 0) return null;
    return `${((r / d) * 100).toFixed(1)}%`;
  };

  // PnL mini spark
  const pnlVals = () =>
    (ts()?.buckets ?? []).map((b) => Number(b.cum_net_pnl)).filter(Number.isFinite);
  const sparkPath = () => {
    const vals = pnlVals();
    if (vals.length < 2) return null;
    const W = 200; const H = 40;
    const min = Math.min(...vals);
    const max = Math.max(...vals);
    const range = (max - min) || 1;
    const pts = vals.map((v, i) => {
      const x = (W * i) / (vals.length - 1);
      const y = 4 + (H - 8) * (1 - (v - min) / range);
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    });
    return `M ${pts.join(' L ')}`;
  };
  const sparkColor = () => {
    const vals = pnlVals();
    if (vals.length === 0) return '#555';
    return vals[vals.length - 1] >= 0 ? '#4caf50' : '#f44336';
  };

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              业务吞吐
            </Typography>
            <span class="poll-hint">30s</span>
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Grid container spacing={1.5} sx={{ mb: 2 }}>
          <Grid item xs={6} sm={3}>
            <StatCard label="RM 决策总数" value={rmDecision() != null ? String(rmDecision()!) : '—'} pollHint="30s" />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="拒单数 / 率"
              value={rmReject() != null ? String(rmReject()!) : '—'}
              sub={rejectRate() != null ? <span>拒单率 {rejectRate()}</span> : undefined}
              color={(rmReject() ?? 0) > 0 ? 'yellow' : 'green'}
              pollHint="30s"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="成交 fills"
              value={fillTotal() != null ? String(fillTotal()!) : '—'}
              color={fillTotal() != null && fillTotal()! > 0 ? 'green' : 'default'}
              pollHint="30s"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="净 PnL"
              value={netPnl() != null ? fmtUsdc(netPnl()!) : '—'}
              color={netPnl() != null ? (netPnl()! >= 0 ? 'green' : 'red') : 'default'}
              pollHint="30s"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="净 Edge"
              value={netEdge() != null ? `${netEdge()!.toFixed(1)}` : '—'}
              unit="bps"
              pollHint="30s"
            />
          </Grid>
          <Show when={g()?.has_data}>
            <Grid item xs={6} sm={3}>
              <StatCard
                label="Sharpe (30d)"
                value={g()!.sharpe.toFixed(2)}
                sub={<span>±{g()!.sharpe_se.toFixed(2)} p={g()!.p_value.toFixed(3)}</span>}
                color={g()!.sharpe >= 1 ? 'green' : g()!.sharpe >= 0 ? 'yellow' : 'red'}
                pollHint="30s"
              />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard
                label="Gate 初审"
                value={g()!.prelim_pass ? 'PASS' : 'FAIL'}
                color={g()!.prelim_pass ? 'green' : 'red'}
                pollHint="30s"
              />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard
                label="Gate 确认审"
                value={g()!.confirm_pass ? 'PASS' : 'FAIL'}
                color={g()!.confirm_pass ? 'green' : 'red'}
                pollHint="30s"
              />
            </Grid>
          </Show>
        </Grid>

        {/* PnL mini spark */}
        <Show when={sparkPath()}>
          <Box sx={{ mb: 1 }}>
            <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.5, display: 'block' }}>
              PnL 时序 (近1h) <span class="poll-hint">15s</span>
            </Typography>
            <svg viewBox="0 0 200 40" width="100%" height="40"
              style={{ display: 'block', background: 'var(--md-surface2)', 'border-radius': '4px' }}>
              <path d={sparkPath()!} fill="none" stroke={sparkColor()} stroke-width="1.5" />
            </svg>
          </Box>
        </Show>
      </CardContent>
    </Card>
  );
}

// ============================================================
// §1.5 错误/拒单
// ============================================================

function RejectSection() {
  const rejects = () => state.rejects?.rejects ?? [];

  const reasonDist = () => {
    const dist: Record<string, number> = {};
    for (const r of rejects()) {
      dist[r.reason_code] = (dist[r.reason_code] ?? 0) + 1;
    }
    return Object.entries(dist).sort((a, b) => b[1] - a[1]).slice(0, 8);
  };
  const maxCount = () => Math.max(...reasonDist().map(([, c]) => c), 1);

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              错误 / 拒单
            </Typography>
            <span class="poll-hint">10s</span>
            <Show when={rejects().length > 0}>
              <Chip label={String(rejects().length)} color="error" size="small" sx={{ fontSize: '10px', height: '18px' }} />
            </Show>
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Grid container spacing={2}>
          {/* 最近拒单表 (E-01) */}
          <Grid item xs={12} md={8}>
            <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.75, display: 'block', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>
              最近拒单 (最多50条)
            </Typography>
            <Show when={rejects().length > 0} fallback={
              <Typography variant="caption" sx={{ color: 'text.disabled' }}>无拒单记录</Typography>
            }>
              <TableContainer component={Paper} variant="outlined" sx={{ maxHeight: 300 }}>
                <Table size="small" stickyHeader>
                  <TableHead>
                    <TableRow>
                      <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75 }}>时间</TableCell>
                      <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75 }}>market_id</TableCell>
                      <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75 }}>reason_code</TableCell>
                      <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75 }}>方向</TableCell>
                      <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75, textAlign: 'right' }}>数量</TableCell>
                      <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75, textAlign: 'right' }}>价格</TableCell>
                    </TableRow>
                  </TableHead>
                  <TableBody>
                    <For each={rejects().slice(0, 50)}>
                      {(r: RiskReject) => (
                        <TableRow hover>
                          <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px', color: 'text.secondary', py: 0.5 }}>
                            {fmtTs(r.rejected_ts)}
                          </TableCell>
                          <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px', maxWidth: 120, overflow: 'hidden', textOverflow: 'ellipsis', py: 0.5 }}>
                            {r.market_id}
                          </TableCell>
                          <TableCell sx={{ py: 0.5 }}>
                            <Chip
                              label={REJECT_REASON_ZH[r.reason_code] ?? r.reason_code}
                              color="error"
                              size="small"
                              variant="outlined"
                              sx={{ fontSize: '9px', height: '16px' }}
                            />
                          </TableCell>
                          <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px', py: 0.5 }}>
                            {SIDE_ZH[r.side] ?? r.side}
                          </TableCell>
                          <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px', textAlign: 'right', py: 0.5 }}>
                            {Number(r.size).toLocaleString()}
                          </TableCell>
                          <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px', textAlign: 'right', py: 0.5 }}>
                            {Number(r.price).toFixed(4)}
                          </TableCell>
                        </TableRow>
                      )}
                    </For>
                  </TableBody>
                </Table>
              </TableContainer>
            </Show>
          </Grid>

          {/* reason_code 分布 (E-02) */}
          <Grid item xs={12} md={4}>
            <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.75, display: 'block', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>
              reason_code 分布
            </Typography>
            <Show when={reasonDist().length > 0} fallback={
              <Typography variant="caption" sx={{ color: 'text.disabled' }}>无拒单</Typography>
            }>
              <For each={reasonDist()}>
                {([code, cnt]) => (
                  <div class="reason-bar-row">
                    <Typography class="reason-bar-label" title={code}>
                      {REJECT_REASON_ZH[code] ?? code}
                    </Typography>
                    <div class="reason-bar-track">
                      <div class="reason-bar-fill" style={{ width: `${(cnt / maxCount()) * 100}%` }} />
                    </div>
                    <Typography class="reason-bar-count">{cnt}</Typography>
                  </div>
                )}
              </For>
            </Show>
          </Grid>
        </Grid>
      </CardContent>
    </Card>
  );
}

// ============================================================
// §1.6 覆盖率 / 匹配率
// ============================================================

/**
 * 三率 Widget
 *
 * 数据源 (小卢 /metrics):
 *   盘口识别率:  stcpp_market_type_recognized_total
 *               stcpp_market_type_unknown_total
 *   市场覆盖:   stcpp_markets_discovered_total
 *               stcpp_markets_subscribed_total
 *               stcpp_tokens_subscribed_total
 *   比分匹配率: stcpp_score_matched_total
 *               stcpp_markets_total
 */

function CoverageSection() {
  const m = () => state.metrics;

  // ---- 盘口识别率 ----
  const recognized = () => parseMetricVal(m(), 'stcpp_market_type_recognized_total');
  const unknown    = () => parseMetricVal(m(), 'stcpp_market_type_unknown_total');
  const recogTotal = () => {
    const r = recognized(); const u = unknown();
    if (r == null && u == null) return null;
    return (r ?? 0) + (u ?? 0);
  };
  const recogPct = () => {
    const t = recogTotal(); const r = recognized();
    if (t == null || r == null || t === 0) return 0;
    return Math.min((r / t) * 100, 100);
  };
  const recogColor = (): 'success' | 'warning' | 'error' | 'inherit' => {
    const p = recogPct();
    if (recogTotal() == null) return 'inherit';
    // 当前处于发现阶段，0% 是正常状态，不标 error
    if (p >= 80) return 'success';
    if (p >= 40) return 'warning';
    return 'inherit'; // 0/低值 → 灰色进度条，附语境文案
  };
  const recogStatColor = (): 'default' | 'green' | 'yellow' | 'red' => {
    const p = recogPct();
    if (recogTotal() == null) return 'default';
    if (p >= 80) return 'green';
    if (p >= 40) return 'yellow';
    return 'default';
  };

  // ---- 市场覆盖 ----
  const discovered  = () => parseMetricVal(m(), 'stcpp_markets_discovered_total');
  // 2026-05-31 修: 对齐后端真名 (endpoint_metrics.cpp:112/109) — 原 stcpp_markets_subscribed_total/
  //   stcpp_tokens_subscribed_total 错位致 CoverageSection 永久空 (前后端接口核查发现)。
  const subscribed  = () => parseMetricVal(m(), 'stcpp_subscribed_markets_total');
  const tokensSubbed = () => parseMetricVal(m(), 'stcpp_subscribed_tokens_total');
  const coverPct = () => {
    const d = discovered(); const s = subscribed();
    if (d == null || s == null || d === 0) return 0;
    return Math.min((s / d) * 100, 100);
  };
  const coverColor = (): 'success' | 'warning' | 'error' | 'inherit' => {
    const p = coverPct();
    if (discovered() == null) return 'inherit';
    if (p >= 80) return 'success';
    if (p >= 40) return 'warning';
    return 'inherit';
  };
  const coverStatColor = (): 'default' | 'green' | 'yellow' | 'red' => {
    const p = coverPct();
    if (discovered() == null) return 'default';
    if (p >= 80) return 'green';
    if (p >= 40) return 'yellow';
    return 'default';
  };

  // ---- 比分匹配率 ----
  const scoreMatched  = () => parseMetricVal(m(), 'stcpp_score_matched_total');
  // 2026-05-31 修: 后端无 stcpp_markets_total; 比分匹配率分母用 discovered (endpoint_metrics.cpp:138)。
  const marketsTotal  = () => parseMetricVal(m(), 'stcpp_markets_discovered_total');
  const scorePct = () => {
    const t = marketsTotal(); const r = scoreMatched();
    if (t == null || r == null || t === 0) return 0;
    return Math.min((r / t) * 100, 100);
  };
  const scoreColor = (): 'success' | 'warning' | 'error' | 'inherit' => {
    // outright 类型无 inplay → 0% 是正常值，不标 error
    const p = scorePct();
    if (marketsTotal() == null) return 'inherit';
    if (p >= 70) return 'success';
    if (p >= 30) return 'warning';
    return 'inherit';
  };
  const scoreStatColor = (): 'default' | 'green' | 'yellow' | 'red' => {
    const p = scorePct();
    if (marketsTotal() == null) return 'default';
    if (p >= 70) return 'green';
    if (p >= 30) return 'yellow';
    return 'default';
  };

  // 无数据时统一提示字
  const noData = () => m() == null;

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              覆盖率 / 匹配率
            </Typography>
            <span class="poll-hint">30s</span>
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>

        {/* ── 无数据占位 ── */}
        <Show when={noData()}>
          <Alert severity="info" sx={{ mb: 2, fontSize: '12px' }}>
            后端 /metrics 尚未加载（小卢分支合并后自动填充），当前显示占位。
          </Alert>
        </Show>

        {/* ──────────────────────────────────────────
            盘口识别率
        ────────────────────────────────────────── */}
        <Typography variant="caption" sx={{
          color: 'text.secondary', mb: 0.75, display: 'block',
          textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600,
        }}>
          盘口识别率
        </Typography>
        <Grid container spacing={1.5} sx={{ mb: 1.5 }}>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="已识别"
              value={recognized() != null ? String(recognized()!) : '—'}
              color={recogStatColor()}
              pollHint="30s"
              title="stcpp_market_type_recognized_total"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="未识别"
              value={unknown() != null ? String(unknown()!) : '—'}
              color={(unknown() ?? 0) > 0 ? 'yellow' : 'default'}
              pollHint="30s"
              title="stcpp_market_type_unknown_total"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="识别率"
              value={recogTotal() != null ? `${recogPct().toFixed(1)}%` : '—'}
              color={recogStatColor()}
              pollHint="30s"
              title="recognized / (recognized + unknown)"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="盘口总数"
              value={recogTotal() != null ? String(recogTotal()!) : '—'}
              pollHint="30s"
            />
          </Grid>
        </Grid>
        <Box sx={{ mb: 0.5 }}>
          <LinearProgress
            variant="determinate"
            value={recogPct()}
            color={recogColor()}
            sx={{ height: 8, borderRadius: 3 }}
          />
        </Box>
        <Show when={recogTotal() != null && recogPct() < 40}>
          <Typography variant="caption" sx={{ color: 'text.disabled', display: 'block', mb: 1.5 }}>
            当前识别率偏低属正常 — 发现阶段盘口类型字段待后端完整覆盖；outright / prop 类型 enum 持续扩充中。
          </Typography>
        </Show>
        <Show when={recogTotal() == null}>
          <Typography variant="caption" sx={{ color: 'text.disabled', display: 'block', mb: 1.5 }}>
            待小卢 metrics 分支合并后自动填充。
          </Typography>
        </Show>
        <Show when={recogTotal() != null && recogPct() >= 40}>
          <Box sx={{ mb: 1.5 }} />
        </Show>

        <Divider sx={{ mb: 1.5 }} />

        {/* ──────────────────────────────────────────
            市场覆盖
        ────────────────────────────────────────── */}
        <Typography variant="caption" sx={{
          color: 'text.secondary', mb: 0.75, display: 'block',
          textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600,
        }}>
          市场覆盖
        </Typography>
        <Grid container spacing={1.5} sx={{ mb: 1.5 }}>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="发现市场"
              value={discovered() != null ? String(discovered()!) : '—'}
              pollHint="30s"
              title="stcpp_markets_discovered_total"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="有报价市场"
              value={subscribed() != null ? String(subscribed()!) : '—'}
              color={coverStatColor()}
              pollHint="30s"
              title="stcpp_subscribed_markets_total — 有 live book 的市场数 (≠ 订阅总数)"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="订阅 Token 数"
              value={tokensSubbed() != null ? String(tokensSubbed()!) : '—'}
              pollHint="30s"
              title="stcpp_tokens_subscribed_total"
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="覆盖率"
              value={discovered() != null ? `${coverPct().toFixed(1)}%` : '—'}
              color={coverStatColor()}
              pollHint="30s"
              title="subscribed / discovered"
            />
          </Grid>
        </Grid>
        <Box sx={{ mb: 0.5 }}>
          <LinearProgress
            variant="determinate"
            value={coverPct()}
            color={coverColor()}
            sx={{ height: 8, borderRadius: 3 }}
          />
        </Box>
        <Show when={discovered() == null}>
          <Typography variant="caption" sx={{ color: 'text.disabled', display: 'block', mb: 1.5 }}>
            待小卢 metrics 分支合并后自动填充。
          </Typography>
        </Show>
        <Show when={discovered() != null}>
          <Box sx={{ mb: 1.5 }} />
        </Show>

        <Divider sx={{ mb: 1.5 }} />

        {/* ──────────────────────────────────────────
            比分匹配率
        ────────────────────────────────────────── */}
        <Typography variant="caption" sx={{
          color: 'text.secondary', mb: 0.75, display: 'block',
          textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600,
        }}>
          比分匹配率 (Goalserve live score)
        </Typography>
        <Grid container spacing={1.5} sx={{ mb: 1.5 }}>
          <Grid item xs={6} sm={4}>
            <StatCard
              label="已匹配市场"
              value={scoreMatched() != null ? String(scoreMatched()!) : '—'}
              color={scoreStatColor()}
              pollHint="30s"
              title="stcpp_score_matched_total"
            />
          </Grid>
          <Grid item xs={6} sm={4}>
            <StatCard
              label="市场总数"
              value={marketsTotal() != null ? String(marketsTotal()!) : '—'}
              pollHint="30s"
              title="stcpp_markets_total"
            />
          </Grid>
          <Grid item xs={12} sm={4}>
            <StatCard
              label="匹配率"
              value={marketsTotal() != null ? `${scorePct().toFixed(1)}%` : '—'}
              color={scoreStatColor()}
              pollHint="30s"
              title="score_matched / markets_total"
            />
          </Grid>
        </Grid>
        <Box sx={{ mb: 0.5 }}>
          <LinearProgress
            variant="determinate"
            value={scorePct()}
            color={scoreColor()}
            sx={{ height: 8, borderRadius: 3 }}
          />
        </Box>
        <Show when={marketsTotal() == null}>
          <Typography variant="caption" sx={{ color: 'text.disabled', display: 'block', mt: 0.5 }}>
            待小卢 metrics 分支合并后自动填充。
          </Typography>
        </Show>
        <Show when={marketsTotal() != null && scorePct() < 30}>
          <Typography variant="caption" sx={{ color: 'text.disabled', display: 'block', mt: 0.5 }}>
            当前无 live 比赛 / outright 类型无 inplay 比分，匹配率为 0% 属正常。Goalserve live score 映射将在赛季进行中自动上升。
          </Typography>
        </Show>
      </CardContent>
    </Card>
  );
}

// ============================================================
// Prometheus 裸文本折叠
// ============================================================

function MetricsRawSection() {
  const [open, setOpen] = createSignal(false);
  const text = () => state.metrics;
  const escaped = () =>
    text()
      ?.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;') ?? '';

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              Prometheus /metrics (裸文本兜底)
            </Typography>
            <span class="poll-hint">30s</span>
          </Box>
        }
        action={
          <Button size="small" variant="outlined" onClick={() => setOpen((v) => !v)} sx={{ mr: 1, mt: 0.5 }}>
            {open() ? '折叠' : '展开'}
          </Button>
        }
        sx={{ py: 1, px: 2, borderBottom: open() ? '1px solid #373737' : 'none' }}
      />
      <Show when={open()}>
        <CardContent sx={{ p: 2 }}>
          <Show when={text()} fallback={<Typography variant="caption" sx={{ color: 'text.disabled' }}>metrics 未加载</Typography>}>
            <pre class="metrics-pre" innerHTML={escaped()} />
          </Show>
        </CardContent>
      </Show>
    </Card>
  );
}

// ============================================================
// MappingSection (老雷 2026-06-01 可观测): condition↔Goalserve 映射实况
//   闭合"映射只能 grep 日志看"的缺口: 匹配上几个 / Goalserve 在追哪些 / 比分。
// ============================================================

function MappingSection() {
  const ms = () => state.mappingStatus;
  return (
    <Card variant="outlined" sx={{ mt: 2 }}>
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle1" sx={{ fontWeight: 700 }}>盘口 ↔ 直播员 映射</Typography>
            <Show when={ms()}>
              <Chip size="small" color={ms()!.matched > 0 ? 'success' : 'default'}
                label={`映射 ${ms()!.matched}/${ms()!.total_markets}`} />
              <Chip size="small" color="info" label={`Goalserve live ${ms()!.live_games}`} />
            </Show>
          </Box>
        }
      />
      <CardContent sx={{ p: 2 }}>
        <Show when={ms()} fallback={<Typography variant="caption" sx={{ color: 'text.disabled' }}>加载中…</Typography>}>
          <Show when={ms()!.matched === 0}>
            <Typography variant="caption" sx={{ color: 'warning.main', display: 'block', mb: 1 }}>
              当前 0 映射 — PM 在卖的比赛与 Goalserve 正在追的不重叠 (大赛空档期; 等下一场欧美晚间赛)。
            </Typography>
          </Show>
          <Show when={ms()!.markets.length > 0}>
            <Typography variant="caption" sx={{ color: 'text.secondary', fontWeight: 700 }}>已映射盘口</Typography>
            <TableContainer sx={{ maxHeight: 200, mb: 1.5 }}>
              <Table size="small" stickyHeader>
                <TableHead><TableRow>
                  <TableCell>队伍</TableCell><TableCell>类型</TableCell>
                  <TableCell align="right">置信</TableCell><TableCell>GS match</TableCell>
                </TableRow></TableHead>
                <TableBody>
                  <For each={ms()!.markets}>
                    {(r) => (
                      <TableRow>
                        <TableCell sx={{ fontSize: '11px' }}>{r.team0} vs {r.team1}</TableCell>
                        <TableCell>{r.is_draw ? '平局' : '胜负'}</TableCell>
                        <TableCell align="right">{r.match_confidence.toFixed(2)}</TableCell>
                        <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px' }}>{r.inplay_match_id}</TableCell>
                      </TableRow>
                    )}
                  </For>
                </TableBody>
              </Table>
            </TableContainer>
          </Show>
          <Typography variant="caption" sx={{ color: 'text.secondary', fontWeight: 700 }}>Goalserve 当前 live 比赛</Typography>
          <TableContainer sx={{ maxHeight: 240 }}>
            <Table size="small" stickyHeader>
              <TableHead><TableRow>
                <TableCell>比赛</TableCell><TableCell>运动</TableCell>
                <TableCell>状态</TableCell><TableCell align="right">比分</TableCell>
              </TableRow></TableHead>
              <TableBody>
                <For each={ms()!.games}>
                  {(g) => (
                    <TableRow>
                      <TableCell sx={{ fontSize: '11px' }}>{g.home} vs {g.away}</TableCell>
                      <TableCell>{g.sport}</TableCell>
                      <TableCell><Chip size="small" label={g.status} /></TableCell>
                      <TableCell align="right">{g.home_score}-{g.away_score}</TableCell>
                    </TableRow>
                  )}
                </For>
              </TableBody>
            </Table>
          </TableContainer>
        </Show>
      </CardContent>
    </Card>
  );
}

// ============================================================
// FeatureHealthSection (老雷 2026-06-01 可观测): 110 ML 特征活死
//   "特征没问题训练才有意义" — 一眼看哪些特征是死值 (全 0/null)。
// ============================================================

function FeatureHealthSection() {
  const fh = () => state.featureHealth;
  const dead = () => (fh()?.rows ?? []).filter((r) => r.status === 'dead');
  const constRows = () => (fh()?.rows ?? []).filter((r) => r.status === 'const');
  return (
    <Card variant="outlined" sx={{ mt: 2 }}>
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle1" sx={{ fontWeight: 700 }}>特征健康 (110 列)</Typography>
            <Show when={fh()}>
              <Chip size="small" color="success" label={`活 ${fh()!.healthy}`} />
              <Chip size="small" color="warning" label={`常量 ${fh()!.const}`} />
              <Chip size="small" color="error" label={`死 ${fh()!.dead}`} />
              <Typography variant="caption" sx={{ color: 'text.disabled' }}>
                样本 {fh()!.n_records} 市场
              </Typography>
            </Show>
          </Box>
        }
      />
      <CardContent sx={{ p: 2 }}>
        <Show when={fh()} fallback={<Typography variant="caption" sx={{ color: 'text.disabled' }}>加载中… (无数据时检查 /api/v1/features/health)</Typography>}>
          <Typography variant="caption" sx={{ color: 'text.secondary', display: 'block', mb: 1 }}>
            死特征 = 全 0/null (无信息量)。game 侧 g_* 死 = 当前无 live 比赛映射 Goalserve; pos_* 死 = 无持仓。
          </Typography>
          <TableContainer sx={{ maxHeight: 320 }}>
            <Table size="small" stickyHeader>
              <TableHead>
                <TableRow>
                  <TableCell>#</TableCell>
                  <TableCell>特征</TableCell>
                  <TableCell>状态</TableCell>
                  <TableCell align="right">填充</TableCell>
                  <TableCell align="right">非零</TableCell>
                  <TableCell align="right">range</TableCell>
                </TableRow>
              </TableHead>
              <TableBody>
                <For each={[...dead(), ...constRows()]}>
                  {(r) => (
                    <TableRow>
                      <TableCell>{r.i}</TableCell>
                      <TableCell sx={{ fontFamily: 'monospace', fontSize: '11px' }}>{r.name}</TableCell>
                      <TableCell>
                        <Chip size="small" color={r.status === 'dead' ? 'error' : 'warning'} label={r.status} />
                      </TableCell>
                      <TableCell align="right">{r.populated}</TableCell>
                      <TableCell align="right">{r.nonzero}</TableCell>
                      <TableCell align="right" sx={{ fontFamily: 'monospace', fontSize: '11px' }}>
                        [{r.min.toFixed(3)}, {r.max.toFixed(3)}]
                      </TableCell>
                    </TableRow>
                  )}
                </For>
              </TableBody>
            </Table>
          </TableContainer>
        </Show>
      </CardContent>
    </Card>
  );
}

// ============================================================
// OpsPage (顶层导出)
// ============================================================

export function OpsPage() {
  return (
    <div class="ops-page">
      <SystemHealthSection />
      <SubscriptionSection />
      <DataQualitySection />
      <BusinessThroughputSection />
      <RejectSection />
      <CoverageSection />
      <MappingSection />
      <FeatureHealthSection />
      <MetricsRawSection />
    </div>
  );
}
