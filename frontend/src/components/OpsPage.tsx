/**
 * OpsPage.tsx — 开发者/运维观测页 (v6 新增, P0)
 * owner: 小苏  last_review: 2026-05-29
 *
 * 按小郑规范 (xiaozheng-dev-observability-page-spec-v1.md) 实现:
 *   §1.1 订阅状态区块 (S-01~S-07)
 *   §1.2 系统健康区块 (H-01~H-07)
 *   §1.3 数据质量区块 (Q-01~Q-03)
 *   §1.4 业务吞吐区块 (B-01~B-09)
 *   §1.5 错误/拒单区块 (E-01~E-02)
 *   + Prometheus 裸文本折叠区 (兜底)
 *
 * 数据来源:
 *   /metrics    (30s轮询) → Prometheus 文本解析
 *   /status     (5s轮询)  → state/mode/wss_connected
 *   /healthz    (5s轮询)  → ok/threads/uptime
 *   /api/v1/risk/rejects (10s) → 拒单列表
 *   /api/v1/gate/paper   (30s) → Gate 门禁
 *   /api/v1/pnl/timeseries (15s) → PnL 曲线
 *
 * 订阅数 (GAP-01/02): 后端尚未实现 subscribed_tokens_total /
 *   subscribed_markets_total。临时兜底: 读 store.conditionCache
 *   key 数量推算, 同时显示 "(前端估算 / 后端待接)" 标注。
 *   小冯+小卢 P0 填好后删兜底改直接读 /metrics。
 *
 * 安全: 不显示私钥/签名相关任何字段 (ADR-038 §5)
 */

import { createSignal, For, Show } from 'solid-js';
import { state } from '../store';
import { fmtTs, fmtUsdc, fmtUptime, stalenessMs } from '../api';
import { StatCard } from './ui/StatCard';
import { StatusDot, boolToDot } from './ui/StatusDot';
import { Badge, modeVariant, modeText } from './ui/Badge';
import { StalenessHeatCell } from './ui/StalenessHeatCell';
import { PipelineHealth } from './ui/PipelineHealth';
import type { RiskReject } from '../types';
import { REJECT_REASON_ZH, SIDE_ZH } from '../i18n';

// ============================================================
// Metrics 文本解析工具
// ============================================================

function parseMetricVal(text: string | null, metricName: string): number | null {
  if (!text) return null;
  // 匹配 metricName{...} VALUE 或 metricName VALUE
  const re = new RegExp(`${metricName}(?:\\{[^}]*\\})?\\s+([\\d.]+)`, 'g');
  let match: RegExpExecArray | null;
  let last: number | null = null;
  while ((match = re.exec(text)) !== null) {
    last = Number(match[1]);
  }
  return last;
}

function parseMetricAll(text: string | null, metricName: string): { labels: string; val: number }[] {
  if (!text) return [];
  const re = new RegExp(`${metricName}((?:\\{[^}]*\\})?)\\s+([\\d.]+)`, 'g');
  const results: { labels: string; val: number }[] = [];
  let match: RegExpExecArray | null;
  while ((match = re.exec(text)) !== null) {
    results.push({ labels: match[1], val: Number(match[2]) });
  }
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
  const loopP99Color = (): 'default' | 'green' | 'yellow' | 'red' => {
    const v = loopP99();
    if (v == null) return 'default';
    if (v < 200)  return 'green';
    if (v < 500)  return 'yellow';
    return 'red';
  };

  const stateColor = (): 'default' | 'green' | 'yellow' | 'red' => {
    const st = sysState();
    if (st === 'RUNNING') return 'green';
    if (st === 'HALTED')  return 'red';
    return 'yellow';
  };

  const threads = () => Object.entries(h()?.threads ?? {});
  const allAlive = () => threads().every(([, v]) => v === 'alive');

  return (
    <div class="ops-section">
      <div class="ops-section-title">
        系统健康
        <span class="poll-hint">5s</span>
        <StatusDot state={h()?.ok ? 'green' : h() == null ? 'gray' : 'red'} size="md"
          title={h()?.ok ? '系统正常' : '系统异常'} />
      </div>

      <div class="stat-grid-2x4" style={{ 'margin-bottom': '12px' }}>
        <StatCard
          label="系统状态"
          value={sysState()}
          color={stateColor()}
          pollHint="5s"
        />
        <StatCard
          label="运行模式"
          value={<Badge variant={modeVariant(mode())}>{modeText(mode())}</Badge>}
          pollHint="5s"
        />
        <StatCard
          label="运行时间"
          value={fmtUptime(uptime())}
          pollHint="5s"
        />
        <StatCard
          label="数据源"
          value={dataSource()}
          color={dataSource() === 'live' ? 'green' : 'yellow'}
          pollHint="5s"
        />
        <StatCard
          label="活跃信号"
          value={String(s()?.signals_active_count ?? '—')}
          pollHint="5s"
        />
        <StatCard
          label="持仓数量"
          value={String(s()?.positions_count ?? '—')}
          pollHint="5s"
        />
        <StatCard
          label="拒单/60s"
          value={String(s()?.rm_rejects_last_60s ?? '—')}
          color={(s()?.rm_rejects_last_60s ?? 0) > 0 ? 'red' : 'green'}
          pollHint="5s"
        />
        <StatCard
          label="loop p99"
          value={loopP99() != null ? `${loopP99()!.toFixed(0)} μs` : '—'}
          color={loopP99Color()}
          pollHint="30s"
          title="热路径事件循环 P99 延迟"
        />
      </div>

      {/* 线程心跳 */}
      <div class="ops-section-title" style={{ 'font-size': '10px', 'margin-bottom': '6px' }}>
        线程心跳
        <span class="poll-hint">5s</span>
        <Show when={threads().length > 0}>
          <StatusDot state={allAlive() ? 'green' : 'red'} size="sm"
            title={allAlive() ? '全部线程正常' : '有线程异常'} />
        </Show>
        <span class="uncalib-chip" style={{ 'margin-left': '6px' }}>stub (W10+ 接 watchdog)</span>
      </div>
      <div class="thread-grid">
        <Show when={threads().length > 0} fallback={<div class="no-data">线程数据未加载</div>}>
          <For each={threads()}>
            {([name, st]) => (
              <div class="thread-row">
                <StatusDot state={st === 'alive' ? 'green' : 'red'} size="sm" />
                <span class="thread-name">{name}</span>
                <span class={`thread-state ${st === 'alive' ? 'thread-alive' : 'thread-dead'}`}>
                  {st}
                </span>
              </div>
            )}
          </For>
        </Show>
      </div>
    </div>
  );
}

// ============================================================
// §1.1 订阅状态区块 + WSS 连接
// ============================================================

function SubscriptionSection() {
  const s = () => state.status;
  const m = () => state.metrics;

  // GAP-01/02 兜底: 从 store.conditionCache 推算
  // (小冯+小卢 P0 补好后删此兜底, 直接读 /metrics stcpp_subscribed_markets_total)
  const condCount   = () => Object.keys(state.conditionCache).length;
  const tokenEst    = () => condCount() * 2;

  // 尝试从 /metrics 直接读 (后端实现后生效)
  const subTokens   = () => parseMetricVal(m(), 'stcpp_subscribed_tokens_total');
  const subMarkets  = () => parseMetricVal(m(), 'stcpp_subscribed_markets_total');
  const reconnectAll = () => parseMetricAll(m(), 'stcpp_wss_reconnect_total');

  const wssChannels: { key: string; label: string }[] = [
    { key: 'sports_api',  label: 'sports_api' },
    { key: 'clob',        label: 'clob' },
    { key: 'user_channel', label: 'user_channel' },
  ];

  const reconnectCount = (channel: string): number | null => {
    // 尝试解析 stcpp_wss_reconnect_total{...,channel="xxx"}
    const allR = reconnectAll();
    const found = allR.find((r) => r.labels.includes(channel));
    if (found) return found.val;
    // 兜底: 若无细分则返回聚合值
    return parseMetricVal(m(), 'stcpp_wss_reconnect_total');
  };

  return (
    <div class="ops-section">
      <div class="ops-section-title">
        市场订阅状态
        <span class="poll-hint">5s</span>
      </div>

      <div class="ops-row" style={{ 'margin-bottom': '12px' }}>
        {/* 订阅数 stat */}
        <div class="stat-grid-2x2" style={{ flex: '1' }}>
          <StatCard
            label="已订阅市场 (condition)"
            value={subMarkets() != null ? String(subMarkets()!) : `${condCount()} *`}
            sub={subMarkets() == null ? <span class="uncalib-chip">前端估算 · 后端待接 GAP-02</span> : undefined}
            pollHint="5s"
            title="已订阅 condition_id 数量 (每个 condition = 2 token)"
          />
          <StatCard
            label="已订阅 token"
            value={subTokens() != null ? String(subTokens()!) : `${tokenEst()} *`}
            sub={subTokens() == null ? <span class="uncalib-chip">前端估算 · 后端待接 GAP-01</span> : undefined}
            pollHint="5s"
            title="已订阅 CLOB token 数量"
          />
        </div>

        {/* WSS 连接状态 */}
        <div class="ops-col">
          <div class="ops-section-title" style={{ 'font-size': '10px', 'margin-bottom': '8px', 'border-left-color': 'var(--green)' }}>
            WSS 连接状态
          </div>
          <div class="wss-status-grid">
            <For each={wssChannels}>
              {(ch) => {
                const ok = () => (s()?.wss_connected as Record<string, boolean> | undefined)?.[ch.key];
                const rc = () => reconnectCount(ch.key);
                return (
                  <div class="wss-status-row">
                    <StatusDot state={boolToDot(ok())} size="md" />
                    <span class="wss-channel-name">{ch.label}</span>
                    <span class={`wss-channel-state ${ok() ? 'wss-state-ok' : 'wss-state-off'}`}>
                      {ok() == null ? '—' : ok() ? 'CONNECTED' : 'DISCONNECTED'}
                    </span>
                    <Show when={rc() != null}>
                      <span class={`wss-reconnect-cnt ${(rc() ?? 0) > 5 ? 'wss-reconnect-warn' : ''}`}>
                        重连 {rc()}次
                      </span>
                    </Show>
                  </div>
                );
              }}
            </For>
          </div>
        </div>
      </div>

      {/* Condition 级别 WSS 状态 (从 conditionCache 读 book.token0.wss_state) */}
      <div class="ops-section-title" style={{ 'font-size': '10px', 'margin-bottom': '6px' }}>
        各 Condition WSS 状态
        <span class="poll-hint">5s</span>
      </div>
      <div class="staleness-heat-grid">
        <For each={Object.entries(state.conditionCache)}>
          {([condId, data]) => {
            const wssOk = () =>
              data.book?.token0?.wss_state === 'CONNECTED' &&
              data.book?.token1?.wss_state === 'CONNECTED';
            const shortId = () => condId.slice(0, 18) + (condId.length > 18 ? '…' : '');
            return (
              <div
                class={`staleness-cell ${wssOk() ? 'staleness-cell-ok' : 'staleness-cell-err'}`}
                title={`${condId} WSS: ${data.book?.token0?.wss_state ?? '—'} / ${data.book?.token1?.wss_state ?? '—'}`}
              >
                <StatusDot state={wssOk() ? 'green' : 'red'} size="sm" />
                {shortId()}
              </div>
            );
          }}
        </For>
        <Show when={Object.keys(state.conditionCache).length === 0}>
          <div class="no-data">无已订阅市场</div>
        </Show>
      </div>
    </div>
  );
}

// ============================================================
// §1.3 数据质量区块
// ============================================================

function DataQualitySection() {
  const m = () => state.metrics;

  const stalenessMax = () => parseMetricVal(m(), 'stcpp_data_staleness_ms_max');
  const gapTotal     = () => parseMetricVal(m(), 'stcpp_feed_gap_total');
  const driftBps     = () => parseMetricVal(m(), 'stcpp_price_drift_bps');

  const stalenessColor = (): 'green' | 'yellow' | 'red' | 'default' => {
    const v = stalenessMax();
    if (v == null) return 'default';
    if (v < 500)  return 'green';
    if (v < 2000) return 'yellow';
    return 'red';
  };

  const gapColor = (): 'green' | 'yellow' | 'red' | 'default' => {
    const v = gapTotal();
    if (v == null) return 'default';
    return v === 0 ? 'green' : 'yellow';
  };

  // staleness bar (相对 2000ms 最大)
  const stalenessPct = () => {
    const v = stalenessMax();
    if (v == null) return 0;
    return Math.min(v / 2000, 1) * 100;
  };

  const stalenessBarCls = () => {
    const c = stalenessColor();
    if (c === 'green')  return 'latency-bar-fill-ok';
    if (c === 'yellow') return 'latency-bar-fill-warn';
    return 'latency-bar-fill-err';
  };

  // 4 时间戳状态 (从各 condition book 采样第一个)
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
    <div class="ops-section">
      <div class="ops-section-title">
        数据质量 (ADR R-20 四时间戳)
        <span class="poll-hint">30s</span>
      </div>

      <div class="stat-grid-3x3" style={{ 'margin-bottom': '12px' }}>
        <StatCard
          label="最大 staleness"
          value={stalenessMax() != null ? `${stalenessMax()!.toFixed(0)} ms` : '—'}
          color={stalenessColor()}
          title="stcpp_data_staleness_ms_max: <100ms 绿, <1s 黄, ≥1s 红"
          pollHint="30s"
        />
        <StatCard
          label="Gap 累计"
          value={gapTotal() != null ? String(gapTotal()!) : '—'}
          color={gapColor()}
          title="stcpp_feed_gap_total: >0 即黄"
          pollHint="30s"
        />
        <StatCard
          label="价格漂移 (bps)"
          value={driftBps() != null ? `${driftBps()!.toFixed(1)}bps` : '—'}
          color={(driftBps() ?? 0) > 50 ? 'yellow' : 'default'}
          title="stcpp_price_drift_bps: Goalserve vs PM >50bps 橙"
          pollHint="30s"
        />
      </div>

      {/* staleness bar */}
      <div class="ops-section-title" style={{ 'font-size': '10px', 'margin-bottom': '6px' }}>staleness 可视化</div>
      <div class="latency-bar-row">
        <span class="latency-bar-label">staleness (max)</span>
        <div class="latency-bar-track">
          <div class={stalenessBarCls()} style={{ width: `${stalenessPct()}%` }} />
        </div>
        <span class="latency-bar-val" style={{ color: `var(--${stalenessColor() === 'green' ? 'green' : stalenessColor() === 'yellow' ? 'yellow' : stalenessColor() === 'red' ? 'red' : 'text-dim'})` }}>
          {stalenessMax() != null ? `${stalenessMax()!.toFixed(0)}ms` : '—'}
        </span>
      </div>

      {/* 4 时间戳瀑布 (R-20) */}
      <div class="ops-section-title" style={{ 'font-size': '10px', 'margin-top': '10px', 'margin-bottom': '6px' }}>
        4 时间戳流水线 (采样首个市场)
      </div>
      <Show when={sampleBook()} fallback={<div class="no-data">无市场数据</div>}>
        <PipelineHealth timestamps={ts4()} />
        <div style={{ 'font-size': '10px', color: 'var(--text-dim)', 'margin-top': '4px' }}>
          更新 {fmtTs(sampleBook()?.as_of_ts_ns ?? null)}
        </div>
      </Show>

      {/* 各市场 staleness 热力表 */}
      <div class="ops-section-title" style={{ 'font-size': '10px', 'margin-top': '10px', 'margin-bottom': '6px' }}>
        各市场 staleness 热力表
      </div>
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
          <div class="no-data">无市场数据</div>
        </Show>
      </div>
    </div>
  );
}

// ============================================================
// §1.4 业务吞吐区块
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

  const netPnlColor = (): 'green' | 'red' | 'default' => {
    const v = netPnl();
    if (v == null) return 'default';
    return v >= 0 ? 'green' : 'red';
  };

  // PnL 迷你曲线 SVG (从 timeseries)
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
    if (vals.length === 0) return '#4b5563';
    return vals[vals.length - 1] >= 0 ? '#22c55e' : '#ef4444';
  };

  return (
    <div class="ops-section">
      <div class="ops-section-title">
        业务吞吐
        <span class="poll-hint">30s</span>
      </div>

      <div class="stat-grid-2x4" style={{ 'margin-bottom': '12px' }}>
        <StatCard
          label="RM 决策总数"
          value={rmDecision() != null ? String(rmDecision()!) : '—'}
          pollHint="30s"
        />
        <StatCard
          label="RM 拒单数 / 拒单率"
          value={rmReject() != null ? String(rmReject()!) : '—'}
          sub={rejectRate() != null ? <span>拒单率 {rejectRate()}</span> : undefined}
          color={(rmReject() ?? 0) > 0 ? 'yellow' : 'green'}
          pollHint="30s"
        />
        <StatCard
          label="成交 fills"
          value={fillTotal() != null ? String(fillTotal()!) : '—'}
          color={fillTotal() != null && fillTotal()! > 0 ? 'green' : 'default'}
          pollHint="30s"
        />
        <StatCard
          label="净 PnL"
          value={netPnl() != null ? fmtUsdc(netPnl()!) : '—'}
          color={netPnlColor()}
          pollHint="30s"
        />
        <StatCard
          label="净 Edge"
          value={netEdge() != null ? `${netEdge()!.toFixed(1)} bps` : '—'}
          pollHint="30s"
        />
        <Show when={g()?.has_data}>
          <StatCard
            label="Sharpe (30d)"
            value={g()!.sharpe != null ? g()!.sharpe.toFixed(2) : '—'}
            sub={<span>±{g()!.sharpe_se.toFixed(2)} p={g()!.p_value.toFixed(3)}</span>}
            color={g()!.sharpe >= 1 ? 'green' : g()!.sharpe >= 0 ? 'yellow' : 'red'}
            pollHint="30s"
          />
          <StatCard
            label="Gate 初审"
            value={g()!.prelim_pass ? 'PASS' : 'FAIL'}
            color={g()!.prelim_pass ? 'green' : 'red'}
            pollHint="30s"
          />
          <StatCard
            label="Gate 确认审"
            value={g()!.confirm_pass ? 'PASS' : 'FAIL'}
            color={g()!.confirm_pass ? 'green' : 'red'}
            pollHint="30s"
          />
        </Show>
      </div>

      {/* PnL 迷你曲线 */}
      <Show when={sparkPath()}>
        <div style={{ 'margin-bottom': '8px' }}>
          <div class="ops-section-title" style={{ 'font-size': '10px', 'margin-bottom': '4px' }}>
            PnL 时序 (近1h)
            <span class="poll-hint">15s</span>
          </div>
          <svg viewBox="0 0 200 40" width="100%" height="40" style={{ display: 'block', background: 'var(--bg3)', 'border-radius': '4px' }}>
            <path d={sparkPath()!} fill="none" stroke={sparkColor()} stroke-width="1.5" />
          </svg>
        </div>
      </Show>
    </div>
  );
}

// ============================================================
// §1.5 错误/拒单区块
// ============================================================

function RejectSection() {
  const rejects = () => state.rejects?.rejects ?? [];

  // reason_code 分布 (前端本地聚合)
  const reasonDist = () => {
    const dist: Record<string, number> = {};
    for (const r of rejects()) {
      dist[r.reason_code] = (dist[r.reason_code] ?? 0) + 1;
    }
    return Object.entries(dist)
      .sort((a, b) => b[1] - a[1])
      .slice(0, 8);
  };

  const maxCount = () => Math.max(...reasonDist().map(([, c]) => c), 1);

  return (
    <div class="ops-section">
      <div class="ops-section-title">
        错误 / 拒单
        <span class="poll-hint">10s</span>
      </div>

      <div class="ops-row">
        {/* 最近拒单列表 (E-01) */}
        <div class="ops-col" style={{ flex: '2' }}>
          <div class="ops-section-title" style={{ 'font-size': '10px', 'margin-bottom': '6px' }}>
            最近拒单 (最多50条)
          </div>
          <div class="table-wrap">
            <Show
              when={rejects().length > 0}
              fallback={<div class="no-data">无拒单记录</div>}
            >
              <table class="reject-log-table">
                <thead>
                  <tr>
                    <th>时间</th>
                    <th>market_id</th>
                    <th>reason_code</th>
                    <th>方向</th>
                    <th>数量</th>
                    <th>价格</th>
                  </tr>
                </thead>
                <tbody>
                  <For each={rejects().slice(0, 50)}>
                    {(r: RiskReject) => (
                      <tr>
                        <td class="ts">{fmtTs(r.rejected_ts)}</td>
                        <td class="mono" style={{ 'max-width': '120px', overflow: 'hidden', 'text-overflow': 'ellipsis' }}>
                          {r.market_id}
                        </td>
                        <td>
                          <span class="fail-chip" style={{ 'font-size': '9px' }}>
                            {REJECT_REASON_ZH[r.reason_code] ?? r.reason_code}
                          </span>
                        </td>
                        <td class="mono">{SIDE_ZH[r.side] ?? r.side}</td>
                        <td class="num mono">{Number(r.size).toLocaleString()}</td>
                        <td class="num mono">{Number(r.price).toFixed(4)}</td>
                      </tr>
                    )}
                  </For>
                </tbody>
              </table>
            </Show>
          </div>
        </div>

        {/* reason_code 分布柱状 (E-02) */}
        <div class="ops-col" style={{ flex: '1' }}>
          <div class="ops-section-title" style={{ 'font-size': '10px', 'margin-bottom': '6px' }}>
            reason_code 分布
          </div>
          <Show when={reasonDist().length > 0} fallback={<div class="no-data">无拒单</div>}>
            <For each={reasonDist()}>
              {([code, cnt]) => (
                <div class="reason-bar-row">
                  <span class="reason-bar-label" title={code}>
                    {REJECT_REASON_ZH[code] ?? code}
                  </span>
                  <div class="reason-bar-track">
                    <div class="reason-bar-fill" style={{ width: `${(cnt / maxCount()) * 100}%` }} />
                  </div>
                  <span class="reason-bar-count">{cnt}</span>
                </div>
              )}
            </For>
          </Show>
        </div>
      </div>
    </div>
  );
}

// ============================================================
// Prometheus 裸文本折叠 (兜底)
// ============================================================

function MetricsRawSection() {
  const [open, setOpen] = createSignal(false);
  const text = () => state.metrics;
  const escaped = () =>
    text()
      ?.replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;') ?? '';

  return (
    <div class="ops-section">
      <div style={{ display: 'flex', 'align-items': 'center', gap: '8px', 'margin-bottom': open() ? '10px' : '0' }}>
        <div class="ops-section-title" style={{ flex: '1', 'margin-bottom': '0' }}>
          Prometheus /metrics (裸文本兜底)
          <span class="poll-hint">30s</span>
        </div>
        <button class="metrics-toggle-btn" onClick={() => setOpen((v) => !v)}>
          {open() ? '折叠' : '展开'}
        </button>
      </div>
      <Show when={open()}>
        <Show when={text()} fallback={<div class="no-data">metrics 未加载</div>}>
          <pre class="metrics-pre" innerHTML={escaped()} />
        </Show>
      </Show>
    </div>
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
      <MetricsRawSection />
    </div>
  );
}
