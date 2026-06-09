/**
 * MarketDetailPage.tsx — 市场详情页 (v7, 第4页完整实现)
 * owner: 小苏  last_review: 2026-05-29
 *
 * 功能: 单盘口深钻
 *   - 搜索/选择 conditionId (从 conditionCache)
 *   - 全字段 Market 信息 (Table)
 *   - 双边全档订单簿 (MiniHalfBook 扩展版)
 *   - Quote 详情 (含 CI / model provenance)
 *   - Score 实时比分
 *   - 该市场拒单明细
 *
 * SUID 组件: Card/CardHeader/CardContent/Table/Chip/TextField/Autocomplete
 */

import { createSignal, For, Show, createEffect } from 'solid-js';
import Card from '@suid/material/Card';
import CardHeader from '@suid/material/CardHeader';
import CardContent from '@suid/material/CardContent';
import Chip from '@suid/material/Chip';
import Typography from '@suid/material/Typography';
import Divider from '@suid/material/Divider';
import TextField from '@suid/material/TextField';
import Grid from '@suid/material/Grid';
import Table from '@suid/material/Table';
import TableHead from '@suid/material/TableHead';
import TableBody from '@suid/material/TableBody';
import TableRow from '@suid/material/TableRow';
import TableCell from '@suid/material/TableCell';
import TableContainer from '@suid/material/TableContainer';
import Paper from '@suid/material/Paper';
import LinearProgress from '@suid/material/LinearProgress';
import Box from '@suid/material/Box';
import Alert from '@suid/material/Alert';
import { state, setDetailInterest, addDetailInterest } from '../store';
import { fmtTs, fmtBps, fmtUsdc, fmtClock, stalenessMs } from '../api';
import { REJECT_REASON_ZH, SIDE_ZH, STATUS_ZH, SPORT_ZH } from '../i18n';
import { StatusDot, wssStateToDot } from './ui/StatusDot';
import { StatCard } from './ui/StatCard';
import { PipelineHealth } from './ui/PipelineHealth';
import type { HalfBook, OrderLevel } from '../types';

// ============================================================
// 全档订单簿面板
// ============================================================

function FullHalfBook(props: { half: HalfBook; title: string }) {
  const h = () => props.half;
  const bids = () => h().bids ?? [];
  const asks = () => h().asks ?? [];
  const maxSize = () => Math.max(...bids().map((b) => Number(b.size)), ...asks().map((a) => Number(a.size)), 1);

  return (
    <Card variant="outlined" sx={{ height: '100%' }}>
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700 }}>{props.title}</Typography>
            <Chip label={h().outcome ?? '—'} size="small" variant="outlined"
              sx={{ fontSize: '10px', height: '18px', fontWeight: 700 }} />
            <StatusDot state={wssStateToDot(h().wss_state)} size="sm" title={`WSS: ${h().wss_state}`} />
            <Show when={(h().gap_count ?? 0) > 0}>
              <Chip label={`gap:${h().gap_count}`} color="error" size="small" sx={{ fontSize: '9px', height: '16px' }} />
            </Show>
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        {/* Best bid/ask + microprice */}
        <Grid container spacing={1.5} sx={{ mb: 2 }}>
          <Grid item xs={6} sm={3}>
            <StatCard label="Best Bid" value={h().best_bid?.toFixed(4) ?? '—'} color="green" />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="Best Ask" value={h().best_ask?.toFixed(4) ?? '—'} color="red" />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="Microprice" value={h().microprice?.toFixed(4) ?? '—'} />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="Spread"
              value={h().spread != null ? `${(Number(h().spread) * 100).toFixed(3)}%` : '—'}
              color={(Number(h().spread) > 0.02) ? 'yellow' : 'green'}
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard
              label="Imbalance"
              value={h().imbalance?.toFixed(3) ?? '—'}
              color={Math.abs(Number(h().imbalance)) > 0.3 ? 'yellow' : 'default'}
            />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="Sequence" value={String(h().sequence_no ?? '—')} />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="Gap Count" value={String(h().gap_count ?? 0)}
              color={(h().gap_count ?? 0) > 0 ? 'red' : 'green'} />
          </Grid>
          <Grid item xs={6} sm={3}>
            <StatCard label="WSS State" value={h().wss_state ?? '—'}
              color={h().wss_state === 'CONNECTED' ? 'green' : 'red'} />
          </Grid>
        </Grid>

        {/* 4 时间戳 */}
        <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.75, display: 'block', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>
          4 时间戳 (ADR R-20)
        </Typography>
        <PipelineHealth timestamps={[h().event_ts, h().data_source_ts, h().ingestion_ts, h().book_as_of_ts]} />
        <Typography variant="caption" sx={{ color: 'text.disabled', display: 'block', mt: 0.5, fontFamily: 'monospace' }}>
          更新 {fmtTs(h().book_as_of_ts)}
        </Typography>

        <Divider sx={{ my: 1.5 }} />

        {/* 全档订单簿表 */}
        <Grid container spacing={1.5}>
          <Grid item xs={6}>
            <Typography variant="caption" sx={{ color: '#4caf50', mb: 0.5, display: 'block', fontWeight: 700, textTransform: 'uppercase' }}>
              买单 Bids ({bids().length} 档)
            </Typography>
            <TableContainer component={Paper} variant="outlined" sx={{ maxHeight: 240 }}>
              <Table size="small">
                <TableHead>
                  <TableRow>
                    <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.5, color: 'text.secondary' }}>档位</TableCell>
                    <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.5, textAlign: 'right', color: '#4caf50' }}>价格</TableCell>
                    <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.5, textAlign: 'right', color: 'text.secondary' }}>量</TableCell>
                    <TableCell sx={{ fontSize: '10px', py: 0.5 }}>深度</TableCell>
                  </TableRow>
                </TableHead>
                <TableBody>
                  <For each={bids()}>
                    {(lvl: OrderLevel, i) => (
                      <TableRow hover>
                        <TableCell sx={{ fontSize: '10px', py: 0.5, color: 'text.disabled' }}>{i() + 1}</TableCell>
                        <TableCell sx={{ fontFamily: 'monospace', fontSize: '11px', textAlign: 'right', py: 0.5, color: '#4caf50', fontWeight: 700 }}>
                          {Number(lvl.price).toFixed(4)}
                        </TableCell>
                        <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px', textAlign: 'right', py: 0.5, color: 'text.secondary' }}>
                          {Number(lvl.size).toLocaleString()}
                        </TableCell>
                        <TableCell sx={{ py: 0.5, minWidth: 60 }}>
                          <LinearProgress
                            variant="determinate"
                            value={(Number(lvl.size) / maxSize()) * 100}
                            color="success"
                            sx={{ height: 5, borderRadius: 2 }}
                          />
                        </TableCell>
                      </TableRow>
                    )}
                  </For>
                </TableBody>
              </Table>
            </TableContainer>
          </Grid>
          <Grid item xs={6}>
            <Typography variant="caption" sx={{ color: '#f44336', mb: 0.5, display: 'block', fontWeight: 700, textTransform: 'uppercase' }}>
              卖单 Asks ({asks().length} 档)
            </Typography>
            <TableContainer component={Paper} variant="outlined" sx={{ maxHeight: 240 }}>
              <Table size="small">
                <TableHead>
                  <TableRow>
                    <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.5, color: 'text.secondary' }}>档位</TableCell>
                    <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.5, textAlign: 'right', color: '#f44336' }}>价格</TableCell>
                    <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.5, textAlign: 'right', color: 'text.secondary' }}>量</TableCell>
                    <TableCell sx={{ fontSize: '10px', py: 0.5 }}>深度</TableCell>
                  </TableRow>
                </TableHead>
                <TableBody>
                  <For each={asks()}>
                    {(lvl: OrderLevel, i) => (
                      <TableRow hover>
                        <TableCell sx={{ fontSize: '10px', py: 0.5, color: 'text.disabled' }}>{i() + 1}</TableCell>
                        <TableCell sx={{ fontFamily: 'monospace', fontSize: '11px', textAlign: 'right', py: 0.5, color: '#f44336', fontWeight: 700 }}>
                          {Number(lvl.price).toFixed(4)}
                        </TableCell>
                        <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px', textAlign: 'right', py: 0.5, color: 'text.secondary' }}>
                          {Number(lvl.size).toLocaleString()}
                        </TableCell>
                        <TableCell sx={{ py: 0.5, minWidth: 60 }}>
                          <LinearProgress
                            variant="determinate"
                            value={(Number(lvl.size) / maxSize()) * 100}
                            color="error"
                            sx={{ height: 5, borderRadius: 2 }}
                          />
                        </TableCell>
                      </TableRow>
                    )}
                  </For>
                </TableBody>
              </Table>
            </TableContainer>
          </Grid>
        </Grid>
      </CardContent>
    </Card>
  );
}

// ============================================================
// Market 全字段信息表
// ============================================================

function MarketInfoCard(props: { condId: string }) {
  const cache = () => state.conditionCache[props.condId];
  const mkt = () => cache()?.market;
  const score = () => cache()?.score;
  const quote = () => cache()?.quote;
  const book = () => cache()?.book;

  const fields = () => {
    const m = mkt();
    if (!m) return [];
    return [
      { key: 'condition_id', val: m.condition_id },
      { key: 'market_id', val: m.market_id },
      { key: 'event_id', val: m.event_id },
      { key: 'slug', val: m.slug },
      { key: 'tick_size', val: String(m.tick_size) },
      { key: 'fee_rate', val: `${(Number(m.fee_rate) * 100).toFixed(4)}%` },
      { key: 'neg_risk', val: String(m.neg_risk) },
      { key: 'neg_risk_market_id', val: m.neg_risk_market_id || '—' },
      { key: 'accepting_orders', val: String(m.accepting_orders) },
      { key: 'active', val: String(m.active) },
      { key: 'closed', val: String(m.closed) },
      { key: 'resolved', val: String(m.resolved) },
      { key: 'source', val: m.source },
      { key: 'mode', val: m.mode },
      { key: 'as_of_ts', val: fmtTs(m.as_of_ts) },
      { key: 'polymarket_url', val: m.polymarket_url },
    ];
  };

  return (
    <Card variant="outlined">
      <CardHeader
        title={<Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>Market 全字段</Typography>}
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Show when={mkt()} fallback={
          <Typography variant="caption" sx={{ color: 'text.disabled' }}>无 Market 数据 (conditionCache 未命中)</Typography>
        }>
          {/* Tokens */}
          <Typography variant="caption" sx={{ color: 'text.secondary', mb: 0.75, display: 'block', fontWeight: 600, textTransform: 'uppercase' }}>
            Tokens
          </Typography>
          <Box sx={{ display: 'flex', gap: 1, mb: 2, flexWrap: 'wrap' }}>
            <For each={mkt()!.tokens}>
              {(tok) => (
                <Chip
                  label={`${tok.outcome}: ${tok.price.toFixed(4)}`}
                  size="small"
                  color={tok.winner ? 'success' : 'default'}
                  variant={tok.winner ? 'filled' : 'outlined'}
                  title={tok.token_id}
                  sx={{ fontFamily: 'monospace', fontWeight: 700 }}
                />
              )}
            </For>
          </Box>

          {/* 字段表 */}
          <TableContainer>
            <Table size="small">
              <TableBody>
                <For each={fields()}>
                  {(f) => (
                    <TableRow hover>
                      <TableCell sx={{ fontSize: '11px', color: 'text.secondary', fontWeight: 500, width: 160, py: 0.5, borderColor: '#373737' }}>
                        {f.key}
                      </TableCell>
                      <TableCell sx={{ fontFamily: 'monospace', fontSize: '11px', py: 0.5, wordBreak: 'break-all', borderColor: '#373737' }}>
                        {f.key === 'polymarket_url' && f.val && f.val !== '—'
                          ? <Typography component="a" href={f.val} target="_blank" rel="noopener noreferrer"
                              sx={{ color: 'primary.main', fontSize: '11px', fontFamily: 'monospace', textDecoration: 'none', '&:hover': { textDecoration: 'underline' } }}>
                              {f.val}
                            </Typography>
                          : f.val}
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
// Score + Quote 联合卡片
// ============================================================

function ScoreQuoteCard(props: { condId: string }) {
  const cache = () => state.conditionCache[props.condId];
  const score = () => cache()?.score;
  const quote = () => cache()?.quote;
  const q     = () => quote();

  return (
    <Grid container spacing={1.5}>
      {/* Score */}
      <Grid item xs={12} md={5}>
        <Card variant="outlined" sx={{ height: '100%' }}>
          <CardHeader
            title={<Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>实时比分</Typography>}
            sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
          />
          <CardContent sx={{ p: 2 }}>
            <Show when={score()} fallback={
              <Typography variant="caption" sx={{ color: 'text.disabled' }}>无比分数据</Typography>
            }>
              {(sc) => (
                <>
                  <Grid container spacing={1.5} sx={{ mb: 1.5 }}>
                    <Grid item xs={6} sm={4}>
                      <StatCard label="体育" value={sc().sport ? (SPORT_ZH[sc().sport] ?? sc().sport) : '—'} />
                    </Grid>
                    <Grid item xs={6} sm={4}>
                      <StatCard label="状态" value={STATUS_ZH[sc().status ?? ''] ?? sc().status ?? '—'}
                        color={sc().status === 'inplay' ? 'green' : sc().status === 'final' ? 'default' : 'yellow'} />
                    </Grid>
                    <Grid item xs={6} sm={4}>
                      <StatCard label="节/时" value={`${sc().period ?? '—'} ${sc().clock_sec != null ? fmtClock(sc().clock_sec) : ''}`} />
                    </Grid>
                    <Grid item xs={6} sm={6}>
                      <StatCard label={sc().home ?? 'Home'} value={String(sc().home_score ?? '—')} color="default" />
                    </Grid>
                    <Grid item xs={6} sm={6}>
                      <StatCard label={sc().away ?? 'Away'} value={String(sc().away_score ?? '—')} color="default" />
                    </Grid>
                  </Grid>
                  <Typography variant="caption" sx={{ color: 'text.disabled', fontFamily: 'monospace', display: 'block' }}>
                    更新 {fmtTs(sc().score_as_of_ts)}
                  </Typography>
                </>
              )}
            </Show>
          </CardContent>
        </Card>
      </Grid>

      {/* Quote */}
      <Grid item xs={12} md={7}>
        <Card variant="outlined" sx={{ height: '100%' }}>
          <CardHeader
            title={<Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>Quote 量化详情</Typography>}
            sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
          />
          <CardContent sx={{ p: 2 }}>
            <Show when={q()} fallback={
              <Typography variant="caption" sx={{ color: 'text.disabled' }}>无 Quote 数据</Typography>
            }>
              {(qt) => (
                <>
                  <Grid container spacing={1.5} sx={{ mb: 1.5 }}>
                    <Grid item xs={6} sm={4}>
                      <StatCard label="公允价" value={qt().fair_value?.toFixed(4) ?? '—'} />
                    </Grid>
                    <Grid item xs={6} sm={4}>
                      <StatCard label="市场中间价" value={qt().market_mid?.toFixed(4) ?? '—'} />
                    </Grid>
                    <Grid item xs={6} sm={4}>
                      {/* sharp bet365 in-play 共识 (盈利修复后 = fair 锚源); -1 = 无 odds/未映射 */}
                      <StatCard label="Sharp 共识"
                        value={Number(qt().sharp_fair) >= 0 ? Number(qt().sharp_fair).toFixed(4) : '— 未映射'}
                        color={Number(qt().sharp_fair) >= 0 ? 'green' : 'default'}
                      />
                    </Grid>
                    <Grid item xs={6} sm={4}>
                      <StatCard label="Edge"
                        value={fmtBps(qt().edge_bps)}
                        color={Number(qt().edge_bps) > 0 ? 'green' : 'red'}
                      />
                    </Grid>
                    <Grid item xs={6} sm={4}>
                      <StatCard label="Kelly"
                        value={Number.isFinite(qt().kelly_fraction) ? `${(qt().kelly_fraction*100).toFixed(1)}%` : '—'}
                        color={qt().kelly_fraction > 0 ? 'green' : 'default'}
                      />
                    </Grid>
                    <Grid item xs={6} sm={4}>
                      <StatCard label="建议额度" value={fmtUsdc(qt().suggested_notional)} />
                    </Grid>
                    <Grid item xs={6} sm={4}>
                      <StatCard label="信号强度" value={Number.isFinite(qt().signal_strength) ? qt().signal_strength.toFixed(3) : '—'} />
                    </Grid>
                  </Grid>

                  {/* 来源/时序字段表 (大模型 provenance model_id/kind/confidence/CI/advisory 已砍 2026-06-05) */}
                  <TableContainer>
                    <Table size="small">
                      <TableBody>
                        {[
                          { k: 'predict_ok', v: String(qt().predict_ok) },
                          { k: 'quote_as_of_ts', v: fmtTs(qt().quote_as_of_ts) },
                          { k: 'model_as_of_ts', v: fmtTs(qt().model_as_of_ts) },
                        ].map((row) => (
                          <TableRow hover>
                            <TableCell sx={{ fontSize: '10px', color: 'text.secondary', fontWeight: 500, width: 140, py: 0.5, borderColor: '#373737' }}>
                              {row.k}
                            </TableCell>
                            <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px', py: 0.5, borderColor: '#373737' }}>
                              {row.v ?? '—'}
                            </TableCell>
                          </TableRow>
                        ))}
                      </TableBody>
                    </Table>
                  </TableContainer>
                </>
              )}
            </Show>
          </CardContent>
        </Card>
      </Grid>
    </Grid>
  );
}

// ============================================================
// 持仓管理决策 (老板 2026-06-09「调试持仓逻辑, 盯盘下面补观测, 查明真正原因」)
//   暴露控制器目标 / 保留价是否可成交 / 决出状态 (must_win_lock 根因) / 本盘 churn & 已实现盈亏。
//   一眼看出: ① 为何不开新仓 (target=0?) ② 为何不锁利 (未决出?) ③ 手续费是否吃掉毛利 (churn 多少笔)。
// ============================================================

function PositionMgmtCard(props: { condId: string }) {
  const cache = () => state.conditionCache[props.condId];
  const quote = () => cache()?.quote;
  const book  = () => cache()?.book;
  const fills = () => state.fillsByMarket[props.condId] ?? [];
  const positions = () => (state.positions?.positions ?? []).filter(
    (p) => p.market_id === props.condId || p.market_id === cache()?.market?.market_id);

  // 本盘成交统计 (churn & fee bleed 核心): 买/卖笔数 + 已实现合计。
  const nBuy  = () => fills().filter((f) => f.side === 'buy').length;
  const nSell = () => fills().filter((f) => f.side === 'sell').length;
  const realizedSum = () => fills().reduce((s, f) => s + (f.realized || 0), 0);
  const unrealizedSum = () => positions().reduce((s, p) => s + (p.pnl_unrealized || 0), 0);
  // 毛已实现往返成本代理: 卖出笔数 = 平仓次数, 每次往返付双边费 → 高 churn = fee 放血。
  const roundTrips = () => Math.min(nBuy(), nSell());

  // 决出状态 → must_win_lock 触发根因。
  const decided = () => Number(quote()?.game_decided_sign ?? 0);
  const decidedText = () => decided() > 0 ? '已决出 · YES 方必赢 (应锁利)'
    : decided() < 0 ? '已决出 · NO 方必赢 (YES 必输)'
    : '未决出 (must_win_lock 不触发)';
  const decidedColor = () => decided() > 0 ? 'green' : decided() < 0 ? 'red' : 'default';

  // 控制器目标 → 为何不开新仓。
  const tgt = () => Number(quote()?.target_signed_notional ?? 0);
  const tgtText = () => Math.abs(tgt()) < 1e-9 ? '0 · 不开新仓 (只减/平)'
    : tgt() > 0 ? `+${tgt().toFixed(1)} 加多 YES` : `${tgt().toFixed(1)} 加多 NO`;

  // 保留价 vs 市价 (是否可成交)。token0=outcomes[0] (本系统多为 YES 被选边)。
  const t0 = () => book()?.token0;
  const resBuy  = () => Number(quote()?.reservation_buy_px ?? 0);
  const resSell = () => Number(quote()?.reservation_sell_px ?? 0);
  const bestAsk = () => Number(t0()?.best_ask ?? 0);
  const bestBid = () => Number(t0()?.best_bid ?? 0);
  // 可买: best_ask ≤ 买保留价 (且两者 >0)。可卖: best_bid ≥ 卖保留价。
  const buyable  = () => resBuy() > 0 && bestAsk() > 0 && bestAsk() <= resBuy();
  const sellable = () => resSell() > 0 && bestBid() > 0 && bestBid() >= resSell();

  // 综合「为何此刻无成交」推断 (decision-diag 的前端版)。
  const reason = () => {
    const q = quote();
    if (!q) return '无 quote 数据';
    if (q.devig_ok === false) return 'de-vig 失败 (无市场锚 → fail-closed)';
    if (Math.abs(tgt()) < 1e-9 && decided() === 0) return 'target=0 且未决出 → 无开仓信号 (sharp edge 不足/被门挡)';
    if (tgt() > 0 && !buyable()) return '有 target 但 best_ask > 买保留价 → 限价不追 (等价回落)';
    if (decided() > 0 && !buyable()) return '已决出该锁利但 ask 已收敛 → 无套利空间';
    return '满足成交条件 (应有 intent)';
  };

  const mult = (v: number | undefined) => (v == null ? '—' : v.toFixed(2));

  return (
    <Card variant="outlined">
      <CardHeader
        title={<Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
          持仓管理决策 · 控制器 / 保留价 / 决出 / churn
        </Typography>}
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Show when={quote()} fallback={
          <Typography variant="caption" sx={{ color: 'text.disabled' }}>无 Quote 数据 (该盘未发布决策快照)</Typography>
        }>
          {/* 第1行: 控制器目标 + 决出状态 + 当前持仓 */}
          <Grid container spacing={1.5} sx={{ mb: 1 }}>
            <Grid item xs={6} sm={3}>
              <StatCard label="控制器目标" value={tgtText()} color={Math.abs(tgt()) < 1e-9 ? 'default' : 'green'} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="决出状态" value={decidedText()} color={decidedColor() as 'green' | 'red' | 'default'} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="当前净持仓"
                value={`${Number(quote()?.pos_net_qty ?? 0).toFixed(2)} sh`}
                sub={Number(quote()?.pos_avg_entry ?? 0) > 0 ? `均入 ${Number(quote()?.pos_avg_entry).toFixed(4)}` : '无仓'} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="未实现 PnL"
                value={`${unrealizedSum() >= 0 ? '+' : ''}${unrealizedSum().toFixed(3)}u`}
                color={unrealizedSum() >= 0 ? 'green' : 'red'} />
            </Grid>
          </Grid>

          {/* 第2行: 保留价 vs 市价 (是否可成交) */}
          <Grid container spacing={1.5} sx={{ mb: 1 }}>
            <Grid item xs={6} sm={3}>
              <StatCard label="买保留价 vs ask"
                value={`${resBuy().toFixed(4)} / ${bestAsk().toFixed(4)}`}
                sub={buyable() ? '可买 (ask≤保留)' : 'ask>保留 不追'}
                color={buyable() ? 'green' : 'default'} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="卖保留价 vs bid"
                value={`${resSell().toFixed(4)} / ${bestBid().toFixed(4)}`}
                sub={sellable() ? '可卖 (bid≥保留)' : 'bid<保留 持有'}
                color={sellable() ? 'green' : 'default'} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="安全边际 margin" value={Number(quote()?.required_margin ?? 0).toFixed(4)} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="末段 near_end" value={quote()?.near_end ? '是 (>85%)' : '否'}
                color={quote()?.near_end ? 'yellow' : 'default'} />
            </Grid>
          </Grid>

          {/* 第3行: 乘子链 (target 量级缩放) */}
          <Grid container spacing={1.5} sx={{ mb: 1 }}>
            <Grid item xs={6} sm={3}>
              <StatCard label="生命周期×" value={mult(quote()?.lifecycle_mult)} sub="噪声缩 ≤1" />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="CLV×" value={mult(quote()?.clv_mult)} sub=">1=放大" />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="回撤× (dd)" value={mult(quote()?.dd_mult)} sub="账户级去险"
                color={Number(quote()?.dd_mult ?? 1) < 1 ? 'yellow' : 'default'} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="相关性× (corr)" value={mult(quote()?.corr_mult)} sub="同赛事 taper" />
            </Grid>
          </Grid>

          {/* 第4行: 本盘 churn & 已实现 (fee bleed 诊断) */}
          <Grid container spacing={1.5} sx={{ mb: 1 }}>
            <Grid item xs={6} sm={3}>
              <StatCard label="本盘成交笔" value={`${fills().length}`}
                sub={`买${nBuy()} / 卖${nSell()} · 往返${roundTrips()}`}
                color={roundTrips() >= 2 ? 'yellow' : 'default'} />
            </Grid>
            <Grid item xs={6} sm={3}>
              <StatCard label="本盘已实现"
                value={`${realizedSum() >= 0 ? '+' : ''}${realizedSum().toFixed(3)}u`}
                color={realizedSum() >= 0 ? 'green' : 'red'} />
            </Grid>
            <Grid item xs={12} sm={6}>
              <StatCard label="为何此刻无成交 (推断)" value={reason()} />
            </Grid>
          </Grid>

          <Typography variant="caption" sx={{ color: 'text.disabled', fontFamily: 'monospace', display: 'block', mt: 0.5 }}>
            target=控制器目标净仓 (0=只减不开); 决出状态驱动 must_win_lock; churn 往返≥2 = 手续费放血风险
          </Typography>
        </Show>
      </CardContent>
    </Card>
  );
}

// ============================================================
// 该市场拒单明细
// ============================================================

function MarketRejectsCard(props: { condId: string }) {
  const allRejects = () => state.rejects?.rejects ?? [];
  const rejects = () => allRejects().filter(
    (r) => r.market_id === props.condId || r.market_id === state.conditionCache[props.condId]?.market?.market_id,
  );

  return (
    <Card variant="outlined">
      <CardHeader
        title={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              该市场拒单明细
            </Typography>
            <Show when={rejects().length > 0}>
              <Chip label={String(rejects().length)} color="error" size="small" sx={{ fontSize: '10px', height: '18px' }} />
            </Show>
          </Box>
        }
        sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
      />
      <CardContent sx={{ p: 2 }}>
        <Show when={rejects().length > 0} fallback={
          <Typography variant="caption" sx={{ color: 'text.disabled' }}>该市场无拒单记录</Typography>
        }>
          <TableContainer component={Paper} variant="outlined" sx={{ maxHeight: 280 }}>
            <Table size="small" stickyHeader>
              <TableHead>
                <TableRow>
                  <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75 }}>时间</TableCell>
                  <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75 }}>reason_code</TableCell>
                  <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75 }}>方向</TableCell>
                  <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75, textAlign: 'right' }}>数量</TableCell>
                  <TableCell sx={{ fontSize: '10px', fontWeight: 700, py: 0.75, textAlign: 'right' }}>价格</TableCell>
                </TableRow>
              </TableHead>
              <TableBody>
                <For each={rejects()}>
                  {(r) => (
                    <TableRow hover>
                      <TableCell sx={{ fontFamily: 'monospace', fontSize: '10px', color: 'text.secondary', py: 0.5 }}>
                        {fmtTs(r.rejected_ts)}
                      </TableCell>
                      <TableCell sx={{ py: 0.5 }}>
                        <Chip label={REJECT_REASON_ZH[r.reason_code] ?? r.reason_code} color="error"
                          size="small" variant="outlined" sx={{ fontSize: '9px', height: '16px' }} />
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
      </CardContent>
    </Card>
  );
}

// ============================================================
// MarketDetailPage (顶层导出)
// ============================================================

export function MarketDetailPage() {
  const [selectedId, setSelectedId] = createSignal<string>('');
  const [searchInput, setSearchInput] = createSignal('');

  // condId 列表来自事件发现 (/api/v1/events), 不再依赖 conditionCache 预取
  // (detail 已改按需, conditionCache 只含已选中/展开的盘口)
  const condIds = () => state.eventGroups.flatMap((g) => g.conditions.map((c) => c.conditionId));
  const filteredIds = () => {
    const q = searchInput().toLowerCase();
    return q ? condIds().filter((id) => id.toLowerCase().includes(q)) : condIds();
  };

  const cache = () => selectedId() ? state.conditionCache[selectedId()] : null;
  const book  = () => cache()?.book;

  // 选中某盘口 → 注册为关注 (立即拉 detail + 后续 5s 轮询持续刷新该盘口)
  createEffect(() => {
    const id = selectedId();
    if (id) {
      setDetailInterest([id]);
      addDetailInterest(id);
    } else {
      setDetailInterest([]);
    }
  });

  return (
    <div class="ops-page">
      {/* 选择市场 */}
      <Card variant="outlined">
        <CardHeader
          title={<Typography variant="subtitle2" sx={{ fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.06em' }}>市场详情 — 单盘口深钻</Typography>}
          sx={{ py: 1, px: 2, borderBottom: '1px solid #373737' }}
        />
        <CardContent sx={{ p: 2 }}>
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 2, flexWrap: 'wrap' }}>
            <TextField
              size="small"
              variant="outlined"
              placeholder="搜索 condition_id..."
              value={searchInput()}
              onInput={(e) => setSearchInput((e.currentTarget as HTMLInputElement).value)}
              sx={{ width: 340, '& input': { fontFamily: 'monospace', fontSize: '12px' } }}
            />
            <Show when={condIds().length === 0}>
              <Alert severity="info" sx={{ fontSize: '11px', py: 0.25 }}>
                当前无已订阅市场 · 等待后端推送持仓/拒单数据
              </Alert>
            </Show>
          </Box>
          {/* 条件 ID 列表 */}
          <Show when={filteredIds().length > 0}>
            <Box sx={{ mt: 1.5, display: 'flex', flexWrap: 'wrap', gap: 0.75 }}>
              <For each={filteredIds()}>
                {(id) => (
                  <Chip
                    label={id.length > 22 ? id.slice(0, 22) + '…' : id}
                    size="small"
                    variant={selectedId() === id ? 'filled' : 'outlined'}
                    color={selectedId() === id ? 'primary' : 'default'}
                    onClick={() => setSelectedId(id)}
                    title={id}
                    sx={{ fontFamily: 'monospace', fontSize: '11px', cursor: 'pointer' }}
                  />
                )}
              </For>
            </Box>
          </Show>
        </CardContent>
      </Card>

      {/* 详情内容区 */}
      <Show when={selectedId()}>
        {/* Market 全字段 */}
        <MarketInfoCard condId={selectedId()} />

        {/* Score + Quote */}
        <ScoreQuoteCard condId={selectedId()} />

        {/* 持仓管理决策 (老板 2026-06-09「盯盘下面补观测」) — 紧跟盘口/quote 下面 */}
        <PositionMgmtCard condId={selectedId()} />

        {/* 双边全档订单簿 */}
        <Show when={book()} fallback={
          <Alert severity="info" sx={{ fontSize: '12px' }}>
            订单簿暂无数据 (等待 /api/v1/book_pair 响应)
          </Alert>
        }>
          {(bk) => (
            <Grid container spacing={1.5}>
              <Grid item xs={12} md={6}>
                <FullHalfBook half={bk().token0} title="Token0 买单" />
              </Grid>
              <Grid item xs={12} md={6}>
                <FullHalfBook half={bk().token1} title="Token1 买单" />
              </Grid>
            </Grid>
          )}
        </Show>

        {/* 该市场拒单 */}
        <MarketRejectsCard condId={selectedId()} />
      </Show>

      <Show when={!selectedId()}>
        <Alert severity="info" sx={{ fontSize: '12px' }}>
          请在上方选择一个 condition_id 开始深钻 · 数据来自 conditionCache (实时同步)
        </Alert>
      </Show>
    </div>
  );
}
