/**
 * AnalyticsPage.tsx — PnL 分析页 (v7 Material Design)
 * owner: 小苏  last_review: 2026-05-29
 *
 * v7 变更:
 *  - SUID Card/CardHeader/CardContent: 各区块
 *  - SUID ToggleButtonGroup: 时间窗选择 1h/6h/24h
 *  - SUID Table: 分市场 PnL 表
 *  - SUID LinearProgress: 瀑布图各项目
 *  - SUID Grid: 自适应布局
 *  - 数据补充: 时间窗 1h/6h/24h + 归因瀑布 + per-market 表 + Gate 完整仪表
 */

import { createSignal, For, Show, createMemo } from 'solid-js';
import Card from '@suid/material/Card';
import CardHeader from '@suid/material/CardHeader';
import CardContent from '@suid/material/CardContent';
import Chip from '@suid/material/Chip';
import Typography from '@suid/material/Typography';
import Divider from '@suid/material/Divider';
import ToggleButton from '@suid/material/ToggleButton';
import ToggleButtonGroup from '@suid/material/ToggleButtonGroup';
import LinearProgress from '@suid/material/LinearProgress';
import Grid from '@suid/material/Grid';
import Table from '@suid/material/Table';
import TableHead from '@suid/material/TableHead';
import TableBody from '@suid/material/TableBody';
import TableRow from '@suid/material/TableRow';
import TableCell from '@suid/material/TableCell';
import TableContainer from '@suid/material/TableContainer';
import Paper from '@suid/material/Paper';
import Box from '@suid/material/Box';
import Alert from '@suid/material/Alert';
import { state, refreshSparkline } from '../store';
import { fmtTs, fmtUsdc, fmtPct, fetchPnlTimeseries } from '../api';
import { StatCard } from './ui/StatCard';
import type { PnlTimeseries } from '../types';

const WF_LABELS: Record<string, string> = {
  gross: '毛收益', fee: '手续费', gas: 'Gas',
  slippage: '滑点', spread: '价差收益', net: '净收益',
};
const WF_ORDER = ['gross', 'fee', 'gas', 'slippage', 'spread', 'net'] as const;

type TimeWindow = '1h' | '6h' | '24h';

// ============================================================
// PnL 时序曲线区块 (含时间窗切换)
// ============================================================

function PnlTimeseriesSection() {
  const [window, setWindow] = createSignal<TimeWindow>('1h');
  const [customTs, setCustomTs] = createSignal<PnlTimeseries | null>(null);
  const [loading, setLoading] = createSignal(false);

  async function switchWindow(w: TimeWindow) {
    setWindow(w);
    if (w === '1h') {
      setCustomTs(null);
      return;
    }
    setLoading(true);
    const bucket = w === '6h' ? '15m' : '30m';
    const data = await fetchPnlTimeseries(w, bucket);
    setCustomTs(data);
    setLoading(false);
  }

  const data = () => window() === '1h' ? state.timeseries : customTs();

  const buckets = () => data()?.buckets ?? [];
  const vals = () => buckets().map((b) => Number(b.cum_net_pnl)).filter(Number.isFinite);
  const fees = () => buckets().map((b) => Number(b.fee)).filter(Number.isFinite);
  const trades = () => buckets().reduce((s, b) => s + (Number(b.n_trades) || 0), 0);
  const latestPnl = () => { const v = vals(); return v.length > 0 ? v[v.length - 1] : 0; };
  const totalFee  = () => fees().reduce((s, v) => s + v, 0);

  // SVG sparkline
  const W = 800; const H = 80;
  const sparkPath = () => {
    const v = vals();
    if (v.length < 2) return null;
    const min = Math.min(...v);
    const max = Math.max(...v);
    const range = (max - min) || 1;
    const pts = v.map((val, i) => {
      const x = 4 + (W - 8) * i / (v.length - 1);
      const y = 4 + (H - 8) * (1 - (val - min) / range);
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    });
    return `M ${pts.join(' L ')}`;
  };
  const zeroY = () => {
    const v = vals();
    if (v.length === 0) return H / 2;
    const min = Math.min(...v);
    const max = Math.max(...v);
    const range = (max - min) || 1;
    const raw = 4 + (H - 8) * (1 - (0 - min) / range);
    return Math.min(H - 4, Math.max(4, raw));
  };
  const areaPath = () => {
    const p = sparkPath();
    const v = vals();
    if (!p || v.length < 2) return null;
    const lastX = (4 + (W - 8)).toFixed(1);
    return `M 4,${zeroY().toFixed(1)} L ${p.slice(2)} L ${lastX},${zeroY().toFixed(1)} Z`;
  };
  const lineColor = () => latestPnl() >= 0 ? '#4caf50' : '#f44336';

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, flexWrap: 'wrap' }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              净值时序曲线
            </Typography>
            <ToggleButtonGroup
              value={window()}
              exclusive
              size="small"
              onChange={(_e: unknown, v: unknown) => { if (v) switchWindow(v as TimeWindow); }}
              sx={{ height: '26px' }}
            >
              <ToggleButton value="1h" sx={{ fontSize: '11px', px: 1 }}>1h</ToggleButton>
              <ToggleButton value="6h" sx={{ fontSize: '11px', px: 1 }}>6h</ToggleButton>
              <ToggleButton value="24h" sx={{ fontSize: '11px', px: 1 }}>24h</ToggleButton>
            </ToggleButtonGroup>
            <Show when={data()}>
              <Typography variant="caption" class="panel-ts">
                更新 {fmtTs(data()!.as_of_ts)}
              </Typography>
            </Show>
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        {/* 汇总统计 */}
        <Grid container spacing={1.5} sx={{ mb: 2 }}>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="净 PnL"
              value={vals().length > 0 ? fmtUsdc(latestPnl()) : '—'}
              color={latestPnl() >= 0 ? 'green' : 'red'}
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="总手续费" value={totalFee() !== 0 ? fmtUsdc(totalFee()) : '—'} color="yellow" />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="总成交笔" value={trades() > 0 ? String(trades()) : '—'} />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="时间窗" value={window()} color="default" />
          </Grid>
        </Grid>

        {/* SVG 大图 */}
        <Show when={loading()}>
          <LinearProgress sx={{ mb: 1 }} />
        </Show>
        <Show
          when={sparkPath()}
          fallback={
            <Typography variant="body2" sx={{ p: 3, textAlign: 'center', color: 'text.disabled', fontStyle: 'italic' }}>
              PnL 时序数据加载中... (需要 ≥2 个 bucket)
            </Typography>
          }
        >
          <Box sx={{ background: 'var(--md-surface2)', borderRadius: 1, overflow: 'hidden' }}>
            <svg viewBox={`0 0 ${W} ${H}`} width="100%" height={H} style={{ display: 'block' }}>
              <defs>
                <linearGradient id="areaGrad" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stop-color={lineColor()} stop-opacity="0.25" />
                  <stop offset="100%" stop-color={lineColor()} stop-opacity="0.02" />
                </linearGradient>
              </defs>
              <line x1="4" y1={zeroY().toFixed(1)} x2={W-4} y2={zeroY().toFixed(1)}
                stroke="#444" stroke-width="0.5" stroke-dasharray="4,4" />
              <Show when={areaPath()}>
                <path d={areaPath()!} fill="url(#areaGrad)" />
              </Show>
              <path d={sparkPath()!} fill="none" stroke={lineColor()} stroke-width="2" />
            </svg>
          </Box>
        </Show>
      </CardContent>
    </Card>
  );
}

// ============================================================
// PnL 归因瀑布
// ============================================================

function WaterfallSection() {
  const data = () => state.attribution;

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              PnL 归因瀑布
            </Typography>
            <span class="poll-hint">15s</span>
            <Show when={data()}>
              <Typography variant="caption" class="panel-ts">更新 {fmtTs(data()!.as_of_ts)}</Typography>
            </Show>
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Show when={data()} fallback={
          <Typography variant="caption" sx={{ color: 'text.disabled' }}>PnL 归因数据未加载</Typography>
        }>
          {(attr) => {
            const wf    = () => attr().waterfall ?? {};
            const gross = () => Math.abs(Number(wf().gross ?? 0)) || 1;

            return (
              <Grid container spacing={2}>
                {/* 瀑布图 (LinearProgress 版) */}
                <Grid item xs={12} md={7}>
                  <Typography variant="caption" sx={{ color: 'text.secondary', mb: 1, display: 'block', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>
                    瀑布图
                  </Typography>
                  <For each={WF_ORDER}>
                    {(key) => {
                      const v = () => Number((wf() as unknown as Record<string, number>)[key] ?? 0);
                      const pct = () => Math.min((Math.abs(v()) / gross()) * 100, 100);
                      const isNet = key === 'net';
                      const barColor = (): 'success' | 'error' | 'warning' | 'primary' =>
                        isNet ? 'warning' : v() >= 0 ? 'success' : 'error';
                      return (
                        <div class="wf-row">
                          <Typography class="wf-label">{WF_LABELS[key] ?? key}</Typography>
                          <Box sx={{ flex: 1 }}>
                            <LinearProgress
                              variant="determinate"
                              value={pct()}
                              color={barColor()}
                              sx={{ height: 10, borderRadius: 2 }}
                            />
                          </Box>
                          <Typography
                            class="wf-val"
                            sx={{ color: v() >= 0 ? '#4caf50' : '#f44336', fontFamily: 'monospace' }}
                          >
                            {fmtUsdc(v())}
                          </Typography>
                        </div>
                      );
                    }}
                  </For>
                </Grid>

                {/* 分市场 PnL 表 */}
                <Grid item xs={12} md={5}>
                  <Typography variant="caption" sx={{ color: 'text.secondary', mb: 1, display: 'block', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>
                    分市场 PnL
                  </Typography>
                  <Show when={(attr().per_market ?? []).length > 0} fallback={
                    <Typography variant="caption" sx={{ color: 'text.disabled' }}>—</Typography>
                  }>
                    <TableContainer component={Paper} variant="outlined" sx={{ maxHeight: 280 }}>
                      <Table size="small" stickyHeader>
                        <TableHead>
                          <TableRow>
                            <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75 }}>market_id</TableCell>
                            <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75, textAlign: 'right' }}>净 PnL</TableCell>
                          </TableRow>
                        </TableHead>
                        <TableBody>
                          <For each={attr().per_market}>
                            {(m) => (
                              <TableRow hover>
                                <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px', color: 'text.secondary', maxWidth: 200, overflow: 'hidden', textOverflow: 'ellipsis', py: 0.5 }}>
                                  {m.market_id}
                                </TableCell>
                                <TableCell sx={{ fontFamily: 'monospace', fontSize: '11px', textAlign: 'right', py: 0.5, color: Number(m.net_pnl) >= 0 ? '#4caf50' : '#f44336', fontWeight: 700 }}>
                                  {fmtUsdc(m.net_pnl)}
                                </TableCell>
                              </TableRow>
                            )}
                          </For>
                        </TableBody>
                      </Table>
                    </TableContainer>
                  </Show>
                </Grid>
              </Grid>
            );
          }}
        </Show>
      </CardContent>
    </Card>
  );
}

// ============================================================
// Gate Paper 门禁仪表
// ============================================================

function GateSection() {
  const g = () => state.gate;

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              Gate Paper 门禁仪表
            </Typography>
            <span class="poll-hint">30s</span>
            <Show when={g()?.has_data}>
              <Chip
                label={g()!.confirm_pass ? 'CONFIRMED PASS' : '未通过'}
                color={g()!.confirm_pass ? 'success' : 'error'}
                size="small"
                variant={g()!.confirm_pass ? 'filled' : 'outlined'}
                sx={{ fontWeight: 700 }}
              />
            </Show>
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Show when={g()?.has_data} fallback={
          <Alert severity="info" sx={{ fontSize: '12px' }}>无门禁数据 (交易不足 / 窗口未到期)</Alert>
        }>
          <Grid container spacing={1.5}>
            <Grid item xs={6} sm={3}>
              <StatCard
                label="Sharpe (30d)"
                value={g()!.sharpe.toFixed(2)}
                sub={<span>±{g()!.sharpe_se.toFixed(2)} p={g()!.p_value.toFixed(3)}</span>}
                color={g()!.sharpe >= 1 ? 'green' : g()!.sharpe >= 0 ? 'yellow' : 'red'}
              />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard
                label="命中率"
                value={`${(g()!.hit_rate * 100).toFixed(1)}%`}
                color={g()!.hit_rate >= 0.55 ? 'green' : g()!.hit_rate >= 0.5 ? 'yellow' : 'red'}
              />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard
                label="最大回撤"
                value={`${(g()!.max_drawdown * 100).toFixed(1)}%`}
                color={g()!.max_drawdown < 0.1 ? 'green' : g()!.max_drawdown < 0.15 ? 'yellow' : 'red'}
              />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard
                label="正收益日"
                value={`${(g()!.positive_day_ratio * 100).toFixed(0)}%`}
                color={g()!.positive_day_ratio >= 0.6 ? 'green' : 'yellow'}
              />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard
                label="交易笔数"
                value={String(g()!.n_trades)}
                sub={<span>窗口 {g()!.window_days}日</span>}
              />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard
                label="Gate 初审"
                value={g()!.prelim_pass ? 'PASS' : 'FAIL'}
                color={g()!.prelim_pass ? 'green' : 'red'}
              />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard
                label="Gate 确认审"
                value={g()!.confirm_pass ? 'PASS' : 'FAIL'}
                color={g()!.confirm_pass ? 'green' : 'red'}
              />
            </Grid>
          </Grid>
        </Show>
      </CardContent>
    </Card>
  );
}

// ============================================================
// 资金概览 (账户级现金 + 估值; 2026-06-01 凯利评审, 老板「虚拟盘要有现金估值显示」)
//   数据源 GET /api/v1/account (store.account, 5s 轮询)。双口径: equity(microprice 展示) +
//   kelly_bankroll(best_bid 保守, 实际喂凯利)。Kelly bankroll 说明条让操盘员确认「纸面化」已修。
// ============================================================

function AccountSummarySection() {
  const a = () => state.account?.account;
  const hasData = () => state.account?.has_data === true && a() != null;

  // 净值颜色: > 初始 绿; < 初始×0.85 (近 15% MDD 红线) 红; 中间黄。
  const equityColor = () => {
    const d = a();
    if (!d) return 'default' as const;
    if (d.equity >= d.bankroll_initial) return 'green' as const;
    if (d.equity < d.bankroll_initial * 0.85) return 'red' as const;
    return 'yellow' as const;
  };
  const pnlColor = (v: number): 'green' | 'red' | 'default' =>
    v > 0 ? 'green' : v < 0 ? 'red' : 'default';
  const ddColor = (v: number): 'green' | 'yellow' | 'red' =>
    v < 0.1 ? 'green' : v < 0.15 ? 'yellow' : 'red';

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              资金概览 (现金 / 估值)
            </Typography>
            <span class="poll-hint">5s</span>
            <Chip label={state.account?.mode ?? 'paper'} size="small" variant="outlined" sx={{ fontWeight: 700 }} />
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Show when={hasData()} fallback={
          <Alert severity="info" sx={{ fontSize: '12px' }}>账户数据未就绪 (paper 引擎未启动 / 尚无快照)</Alert>
        }>
          {/* 第一行: 现金 / 净值 / 未实现 / 已实现 */}
          <Grid container spacing={1.5}>
            <Grid item xs={6} sm={3}>
              <StatCard label="现金余额" value={fmtUsdc(a()!.cash_available)}
                sub={<span>可动用估算 (不含锁仓)</span>} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="账户净值" value={fmtUsdc(a()!.equity)} color={equityColor()}
                sub={<span>= 现金 + 持仓市值 {fmtUsdc(a()!.position_mtm)}</span>} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="未实现 PnL" value={fmtUsdc(a()!.cum_unrealized_pnl)} color={pnlColor(a()!.cum_unrealized_pnl)}
                sub={<span>microprice 估值</span>} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="已实现 PnL" value={fmtUsdc(a()!.cum_realized_pnl)} color={pnlColor(a()!.cum_realized_pnl)}
                sub={<span>累计手续费 {fmtUsdc(a()!.cum_fee_paid)}</span>} />
            </Grid>
            {/* 第二行: 净PnL / 收益率 / 最大回撤 / 持仓数 */}
            <Grid item xs={6} sm={3}>
              <StatCard label="净 PnL" value={fmtUsdc(a()!.net_pnl)} color={pnlColor(a()!.net_pnl)} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="收益率" value={fmtPct(a()!.return_pct)} color={pnlColor(a()!.return_pct)} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="最大回撤" value={fmtPct(a()!.max_drawdown)} color={ddColor(a()!.max_drawdown)}
                sub={<span>北极星红线 ≤15%</span>} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="持仓数" value={String(a()!.open_positions)}
                sub={<span>Sharpe {a()!.sharpe.toFixed(2)}</span>} />
            </Grid>
            {/* CLV 入场 edge 真伪金标准 (2026-06-10): >0=入场打败收盘线=edge真; <0=结构性逆选; ~50笔统计显著 */}
            <Grid item xs={12} sm={6}>
              <StatCard
                label="入场 CLV · edge 真伪 (金标准)"
                value={(a()!.clv_n ?? 0) >= 5
                  ? `${(a()!.clv_close_mean ?? 0) >= 0 ? '+' : ''}${((a()!.clv_close_mean ?? 0) * 100).toFixed(1)}pt`
                  : '—'}
                color={(a()!.clv_n ?? 0) < 20 ? 'yellow' : (a()!.clv_close_mean ?? 0) > 0 ? 'green' : 'red'}
                sub={<span>{(a()!.clv_n ?? 0) >= 5
                  ? `vs收盘线 · n=${a()!.clv_n} · 优于收盘${((a()!.clv_positive_rate ?? 0) * 100).toFixed(0)}% · 正=edge真/负=逆选`
                  : `待结算累积 (n=${a()!.clv_n ?? 0}/20 才统计显著)`}</span>} />
            </Grid>
          </Grid>
          {/* Kelly bankroll 说明条: 让操盘员确认凯利实际喂的是动态净值 (「纸面化」已修) */}
          <Box sx={{ mt: 1.5, p: 1.2, borderRadius: 1, bgcolor: 'rgba(76,175,80,0.08)', border: '1px solid rgba(76,175,80,0.3)' }}>
            <Typography variant="caption" sx={{ color: 'text.secondary', fontWeight: 600 }}>
              凯利 bankroll 基础:&nbsp;
              <span style={{ color: '#4caf50', 'font-family': 'monospace', 'font-weight': 700 }}>
                {a()!.kelly_bankroll_basis} = {fmtUsdc(a()!.kelly_bankroll)}
              </span>
              &nbsp;→ 单笔上限 (bankroll×10%) ≈ {fmtUsdc(a()!.kelly_bankroll * 0.1)}
            </Typography>
          </Box>
        </Show>
      </CardContent>
    </Card>
  );
}

// ============================================================
// 成交流水 (2026-06-04 老板「多少价格买的/卖出的都不知道」)
//   每笔: 时间 | 盘口 | 买/卖 | 边 | 成交价 | 数量 | 本笔已实现 | 累计已实现
// ============================================================
function FillsLogSection() {
  const fills = () => state.fills?.fills ?? [];
  // condition_id → 人读队名 (用 /grid 写进 conditionCache 的 title; 缺则短 hash)
  const nameFor = (cid: string): string => {
    const t = state.conditionCache[cid]?.summary?.title;
    return t && t.length > 0 ? t : `${cid.slice(0, 10)}…`;
  };
  const realColor = (v: number): 'green' | 'red' | 'default' =>
    v > 0.0001 ? 'green' : v < -0.0001 ? 'red' : 'default';
  const chipSx = (c: 'green' | 'red' | 'default') =>
    c === 'green' ? { color: '#4caf50', fontWeight: 700 }
      : c === 'red' ? { color: '#f44336', fontWeight: 700 }
        : { color: '#aaa' };

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              成交流水 (每笔买/卖价 + 已实现盈亏)
            </Typography>
            <span class="poll-hint">5s</span>
            <Chip label={`${fills().length} 笔`} size="small" variant="outlined" sx={{ fontWeight: 700 }} />
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 0 }}>
        <Show when={fills().length > 0} fallback={
          <Alert severity="info" sx={{ fontSize: '12px', m: 2 }}>
            尚无成交 (paper 引擎未产生成交; 有成交后这里逐笔显示买价/卖价/已实现)
          </Alert>
        }>
          <TableContainer sx={{ maxHeight: 420 }}>
            <Table size="small" stickyHeader>
              <TableHead>
                <TableRow>
                  <TableCell sx={{ fontWeight: 700 }}>时间</TableCell>
                  <TableCell sx={{ fontWeight: 700 }}>盘口</TableCell>
                  <TableCell sx={{ fontWeight: 700 }}>动作</TableCell>
                  <TableCell sx={{ fontWeight: 700 }} align="right">成交价</TableCell>
                  <TableCell sx={{ fontWeight: 700 }} align="right">模型fair</TableCell>
                  <TableCell sx={{ fontWeight: 700 }} align="right">声称edge</TableCell>
                  <TableCell sx={{ fontWeight: 700 }} align="right">数量(u)</TableCell>
                  <TableCell sx={{ fontWeight: 700 }} align="right">本笔已实现</TableCell>
                  <TableCell sx={{ fontWeight: 700 }} align="right">累计已实现</TableCell>
                </TableRow>
              </TableHead>
              <TableBody>
                <For each={fills()}>
                  {(f) => (
                    <TableRow hover>
                      <TableCell sx={{ fontFamily: 'monospace', fontSize: '11px', color: '#999' }}>
                        {fmtTs(f.as_of_ts)}
                      </TableCell>
                      <TableCell sx={{ fontSize: '12px', maxWidth: 220, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                        {nameFor(f.market_id)}
                      </TableCell>
                      <TableCell>
                        <span style={{ color: f.side === 'buy' ? '#42a5f5' : '#ffa726', 'font-weight': 700 }}>
                          {f.side === 'buy' ? '买入' : (f.is_close ? '卖出(平)' : '卖出')}
                        </span>
                        <span style={{ color: '#888', 'margin-left': '6px', 'font-size': '11px' }}>{f.outcome}</span>
                      </TableCell>
                      <TableCell align="right" sx={{ fontFamily: 'monospace', fontWeight: 700 }}>
                        {f.price.toFixed(4)}
                      </TableCell>
                      <TableCell align="right" sx={{ fontFamily: 'monospace', color: '#bbb' }}>
                        {f.fair > 0 ? f.fair.toFixed(4) : '—'}
                      </TableCell>
                      <TableCell align="right" sx={{ fontFamily: 'monospace', color: f.fair > 0 && Math.abs((f.side === 'buy' ? f.fair - f.price : f.price - f.fair)) > 0.05 ? '#f44336' : '#888', fontWeight: 700 }}>
                        {f.fair > 0 ? `${(f.side === 'buy' ? f.fair - f.price : f.price - f.fair) >= 0 ? '+' : ''}${(((f.side === 'buy' ? f.fair - f.price : f.price - f.fair)) * 100).toFixed(1)}` : '—'}
                      </TableCell>
                      <TableCell align="right" sx={{ fontFamily: 'monospace' }}>
                        {f.size_usdc.toFixed(1)}
                      </TableCell>
                      <TableCell align="right" sx={{ fontFamily: 'monospace', ...chipSx(realColor(f.realized)) }}>
                        {f.side === 'sell' ? `${f.realized >= 0 ? '+' : ''}${f.realized.toFixed(2)}` : '—'}
                      </TableCell>
                      <TableCell align="right" sx={{ fontFamily: 'monospace', ...chipSx(realColor(f.cum_realized)) }}>
                        {`${f.cum_realized >= 0 ? '+' : ''}${f.cum_realized.toFixed(2)}`}
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
// 模型诊断 (2026-06-04 老板「分析我们的交易, 看看模型怎么调」+「前端没有要了解的数据=前端功能不够」)
//   从成交流水算模型可观测: fair 系统偏差 / 声称 edge / 方向偏置 / 声称利润 vs 实际实现 →
//   一眼看出模型哪里错 + 怎么调。后端已把成交刻 fair+mark 落进 FillRow (前端零猜)。
// ============================================================
function ModelDiagnosticSection() {
  const fills = () => state.fills?.fills ?? [];
  const buys = () => fills().filter((f) => f.side === 'buy' && f.fair > 0);
  const sells = () => fills().filter((f) => f.side === 'sell');
  const withMark = () => fills().filter((f) => f.mark > 0 && f.fair > 0);

  // 模型偏差 = fair − mark (>0 = 模型系统性高估价值, 到处看到"便宜")
  const biasMean = createMemo(() => {
    const a = withMark().map((f) => f.fair - f.mark);
    return a.length ? a.reduce((s, x) => s + x, 0) / a.length : 0;
  });
  // 声称 edge (买入视角 = fair − price; 模型认为便宜多少)
  const claimed = () => buys().map((f) => f.fair - f.price);
  const claimedMean = createMemo(() => {
    const a = claimed();
    return a.length ? a.reduce((s, x) => s + x, 0) / a.length : 0;
  });
  const posPct = createMemo(() => {
    const a = claimed();
    return a.length ? (100 * a.filter((e) => e > 0).length) / a.length : 0;
  });
  const buyYes = () => buys().filter((f) => f.outcome === 'YES').length;
  const buyNo = () => buys().filter((f) => f.outcome === 'NO').length;
  // 声称总利润 (按量加权) vs 实际实现 — 声称巨大正、实现负 = edge 是幻觉
  const claimedProfit = createMemo(() => buys().reduce((s, f) => s + (f.fair - f.price) * f.size_usdc, 0));
  const realizedTot = createMemo(() => sells().reduce((s, f) => s + f.realized, 0));
  const hasData = () => buys().length >= 3;

  const verdict = createMemo(() => {
    if (!hasData()) return [];
    const m: string[] = [];
    if (biasMean() > 0.03)
      m.push(`模型 fair 系统性高于市场 +${(biasMean() * 100).toFixed(1)} 点 → 到处看到假"便宜"`);
    if (posPct() > 85)
      m.push(`${posPct().toFixed(0)}% 买入都声称正 edge → 模型几乎从不认为高估 (严重单向偏置)`);
    if (claimedProfit() > 1 && realizedTot() < 0)
      m.push(`声称总利润 +$${claimedProfit().toFixed(0)} 但实际实现 -$${Math.abs(realizedTot()).toFixed(2)} → 声称 edge 是幻觉, 不是真 alpha`);
    return m;
  });

  const Metric = (p: { label: string; value: string; sub?: string; color?: string }) => (
    <Box sx={{ flex: '1 1 0', minWidth: 130, p: 1.2, border: '1px solid #373737', borderRadius: 1 }}>
      <Typography sx={{ fontSize: '11px', color: '#999' }}>{p.label}</Typography>
      <Typography sx={{ fontSize: '20px', fontWeight: 700, fontFamily: 'monospace', color: p.color ?? '#e0e0e0' }}>
        {p.value}
      </Typography>
      <Show when={p.sub}><Typography sx={{ fontSize: '10px', color: '#777' }}>{p.sub}</Typography></Show>
    </Box>
  );

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              模型诊断 (调模型看这里)
            </Typography>
            <span class="poll-hint">5s</span>
            <Chip label={`${buys().length} 买入样本`} size="small" variant="outlined" sx={{ fontWeight: 700 }} />
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Show when={hasData()} fallback={
          <Alert severity="info" sx={{ fontSize: '12px' }}>
            样本不足 (需 ≥3 笔带模型上下文的买入)。重启后等成交累积; 模型偏差/声称 edge/方向偏置在此实时呈现。
          </Alert>
        }>
          <Box sx={{ display: 'flex', gap: 1, flexWrap: 'wrap', mb: 1.5 }}>
            <Metric label="模型偏差 (fair−mark)" value={`${biasMean() >= 0 ? '+' : ''}${(biasMean() * 100).toFixed(1)}点`}
              sub="模型 fair 相对市场系统偏离" color={biasMean() > 0.03 ? '#f44336' : biasMean() < -0.03 ? '#ff9800' : '#4caf50'} />
            <Metric label="声称 edge 均值" value={`${claimedMean() >= 0 ? '+' : ''}${(claimedMean() * 100).toFixed(1)}点`}
              sub="买入时模型认为便宜多少" color={claimedMean() > 0.05 ? '#f44336' : '#e0e0e0'} />
            <Metric label="正 edge 买入占比" value={`${posPct().toFixed(0)}%`}
              sub=">85% = 单向偏置" color={posPct() > 85 ? '#f44336' : '#e0e0e0'} />
            <Metric label="买入方向" value={`Y${buyYes()} / N${buyNo()}`} sub="YES / NO 笔数" />
            <Metric label="声称利润 vs 实现"
              value={`$${claimedProfit().toFixed(0)} / ${realizedTot() >= 0 ? '+' : ''}$${realizedTot().toFixed(0)}`}
              sub="声称(按量) vs 实际已实现"
              color={claimedProfit() > 1 && realizedTot() < 0 ? '#f44336' : '#e0e0e0'} />
          </Box>
          <Show when={verdict().length > 0}>
            <Alert severity="warning" sx={{ fontSize: '12px', mb: 1 }}>
              <Typography sx={{ fontSize: '12px', fontWeight: 700, mb: 0.5 }}>诊断</Typography>
              <For each={verdict()}>{(v) => <div>• {v}</div>}</For>
            </Alert>
          </Show>
          <Alert severity="info" sx={{ fontSize: '12px' }}>
            <Typography sx={{ fontSize: '12px', fontWeight: 700, mb: 0.5 }}>调模型方向</Typography>
            <div>① <b>去偏</b>: 训练残差中心化 (减均值) / serving 减去已测偏差 — 直接干掉系统性高估</div>
            <div>② <b>收紧校准</b>: Platt/isotonic 把过度自信的 fair 压回市场附近</div>
            <div>③ <b>cap 异常 edge</b>: 声称 &gt;X 点的当过度自信丢弃 (高效市场没那么大真 edge)</div>
          </Alert>
        </Show>
      </CardContent>
    </Card>
  );
}

// ============================================================
// AnalyticsPage (顶层导出)
// ============================================================

export function AnalyticsPage() {
  return (
    <div class="ops-page">
      <AccountSummarySection />
      <ModelDiagnosticSection />
      <FillsLogSection />
      <PnlTimeseriesSection />
      <WaterfallSection />
      <GateSection />
    </div>
  );
}
