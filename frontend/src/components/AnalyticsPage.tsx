/**
 * AnalyticsPage.tsx — PnL 分析页 (v6, P1 最小实现)
 * owner: 小苏  last_review: 2026-05-29
 *
 * 本轮实现: 升格 v5 SecondaryFooter 的 PnL 归因瀑布 + 分市场 PnL + Gate 门禁仪表
 * P1 扩展: 净值时序曲线大图 (已实现 PnlSparkline, 放大版) + 时间窗选择
 * 与 v5 SecondaryFooter 数据来源相同, 不重复轮询 (shared store)
 */

import { For, Show } from 'solid-js';
import { state } from '../store';
import { fmtTs, fmtUsdc } from '../api';
import { PnlSparkline } from './PnlSparkline';
import { StatCard } from './ui/StatCard';
import type { PnlTimeseries } from '../types';

const WF_LABELS: Record<string, string> = {
  gross: '毛收益', fee: '手续费', gas: 'Gas',
  slippage: '滑点', spread: '价差收益', net: '净收益',
};
const WF_ORDER = ['gross', 'fee', 'gas', 'slippage', 'spread', 'net'] as const;

function WaterfallSection() {
  const data = () => state.attribution;

  return (
    <div class="analytics-section">
      <div class="panel-title">
        PnL 归因瀑布
        <span class="poll-hint">15s</span>
        <Show when={data()}>
          <span class="panel-ts">更新 {fmtTs(data()!.as_of_ts)}</span>
        </Show>
      </div>
      <Show when={data()} fallback={<div class="no-data">PnL 归因数据未加载</div>}>
        {(attr) => {
          const wf    = () => attr().waterfall ?? {};
          const gross = () => Number(wf().gross ?? 0);

          return (
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
                <div class="section-title">分市场 PnL</div>
                <Show when={(attr().per_market ?? []).length > 0} fallback={<div class="no-data">—</div>}>
                  <For each={attr().per_market}>
                    {(m) => (
                      <div class="pm-row">
                        <span class="mono pm-id" style={{ 'max-width': '160px', overflow: 'hidden', 'text-overflow': 'ellipsis' }}>
                          {m.market_id}
                        </span>
                        <span class={Number(m.net_pnl) >= 0 ? 'pnl-pos mono-main' : 'pnl-neg mono-main'}>
                          {fmtUsdc(m.net_pnl)}
                        </span>
                      </div>
                    )}
                  </For>
                </Show>
              </div>
            </div>
          );
        }}
      </Show>
    </div>
  );
}

function GateSection() {
  const g = () => state.gate;

  return (
    <div class="analytics-section">
      <div class="panel-title">
        Gate Paper 门禁仪表
        <span class="poll-hint">30s</span>
      </div>
      <Show when={g()?.has_data} fallback={<div class="no-data">无门禁数据 (交易不足)</div>}>
        <div class="stat-grid-2x4">
          <StatCard
            label="Sharpe (30d)"
            value={g()!.sharpe.toFixed(2)}
            sub={<span>±{g()!.sharpe_se.toFixed(2)} p={g()!.p_value.toFixed(3)}</span>}
            color={g()!.sharpe >= 1 ? 'green' : g()!.sharpe >= 0 ? 'yellow' : 'red'}
          />
          <StatCard
            label="命中率"
            value={`${(g()!.hit_rate * 100).toFixed(1)}%`}
            color={g()!.hit_rate >= 0.55 ? 'green' : g()!.hit_rate >= 0.5 ? 'yellow' : 'red'}
          />
          <StatCard
            label="最大回撤"
            value={`${(g()!.max_drawdown * 100).toFixed(1)}%`}
            color={g()!.max_drawdown < 0.1 ? 'green' : g()!.max_drawdown < 0.15 ? 'yellow' : 'red'}
          />
          <StatCard
            label="正收益日"
            value={`${(g()!.positive_day_ratio * 100).toFixed(0)}%`}
          />
          <StatCard
            label="交易笔数"
            value={String(g()!.n_trades)}
            sub={<span>窗口 {g()!.window_days}日</span>}
          />
          <StatCard
            label="Gate 初审"
            value={g()!.prelim_pass ? 'PASS' : 'FAIL'}
            color={g()!.prelim_pass ? 'green' : 'red'}
          />
          <StatCard
            label="Gate 确认审"
            value={g()!.confirm_pass ? 'PASS' : 'FAIL'}
            color={g()!.confirm_pass ? 'green' : 'red'}
          />
        </div>
      </Show>
    </div>
  );
}

export function AnalyticsPage() {
  return (
    <div class="analytics-page">
      {/* 净值曲线 大图 (放大版 PnlSparkline) */}
      <div class="analytics-section">
        <div class="panel-title">
          净值时序曲线 (近1h)
          <span class="poll-hint">15s</span>
        </div>
        <PnlSparkline />
      </div>

      <WaterfallSection />
      <GateSection />
    </div>
  );
}
