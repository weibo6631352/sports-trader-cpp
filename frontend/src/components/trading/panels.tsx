// trading/panels.tsx — 盯盘页展开区组件 (从 TradingPage.tsx 拆出, 2026-06-10 架构师 A3 文件拆分)
//   ExpandBookPanel / MarketFills / ConvergenceSparkline / ExpandQuotePanel / ExpandPosPanel / MarketExpandArea。
//   均自包含 (props 进 + 行级订阅 conditionCache)。仅 MarketExpandArea 对外导出 (EventAccordion 用)。
import { For, Show } from 'solid-js';
import Box from '@suid/material/Box';
import Chip from '@suid/material/Chip';
import LinearProgress from '@suid/material/LinearProgress';
import Typography from '@suid/material/Typography';
import { state, getSharpTrend, uiNow, posSeenAt } from '../../store';
import { fmtTs, fmtBps, fmtUsdc, isEndpointFailing } from '../../api';
import { SIDE_ZH, REJECT_REASON_ZH } from '../../i18n';
import type { BinaryMarketBookView, HalfBook, Quote, Position, RiskReject, Fill, ConditionData } from '../../types';
import { DepthBar } from '../ui/DepthBar';
import { StatusDot, wssStateToDot } from '../ui/StatusDot';

function ExpandBookPanel(props: { book: BinaryMarketBookView | null; conditionId: string }) {
  const book = () => props.book;
  // ★ 响应式判定 (非 early-return): book 在展开后才异步到达, early-return 会卡在"未接入"不更新。
  const hasBook = () => { const b = book(); return !!b && (b as { found?: boolean }).found !== false && !!b.token0; };
  const bk = () => book()!;
  // grid 顶档摘要 (含两边 outcome 名); book 未带 outcome 时用它标注哪边是哪队/选手。
  const summ = () => state.conditionCache[props.conditionId]?.summary ?? null;
  // 订单簿版本年龄 (2026-06-05「飘」根治, 小郑独立诊断 + git+本地REST双证): = now − data_source_ts, 而
  //   data_source_ts 是 Polymarket /book 的 timestamp (订单簿【版本号时刻】, 只在簿真变化才前进; 149hz poller
  //   簿没变就拿到同一个 ts)。所以这个数 = 【距上次簿变化的年龄】, 静市场天然 sawtooth 爬升, 非"卡"。
  //   "数据管道是否实时"看顶部「SSE 实时」灯 (亚秒稳定), 别和这个会飘的版本年龄混淆。阈值放宽 (静市场几秒正常)。
  // 订单簿新鲜度 = 快照新鲜度 (老板 2026-06-05 最终拍板「我要实时推送, 时刻就是订单簿快照时刻, 不要变动才推送」):
  //   = now − as_of_ts (book 快照发布时刻)。实时(每 tick)推送下 as_of_ts 每秒刷新 → 恒 ~0.x s, 不管簿动不动;
  //   管道一卡(送达停)→ as_of_ts 停 → 数字变大、立刻看出来。不用版本时刻(那是"距上次簿变化", 老板明确不要)。
  const bookTs    = () => { const b = book(); const a = b ? Number(b.as_of_ts_ns ?? 0) : 0; return a > 0 ? a : Number(state.conditionCache[props.conditionId]?.quote?.quote_as_of_ts ?? 0); };
  const bookAgeS  = () => bookTs() > 0 ? Math.max(0, (uiNow() - bookTs() / 1e6) / 1000) : NaN;
  const bookFresh = () => !Number.isFinite(bookAgeS()) ? '#888'
    : bookAgeS() < 2 ? '#4caf50' : bookAgeS() < 5 ? '#ff9800' : '#f44336';  // 实时推送下恒~1s; <2s绿 <5s黄 ≥5s红=管道卡
  const vigInfo = () => {
    const cs = Number(bk().cross_spread);
    if (!Number.isFinite(cs)) return null;
    const color: 'success' | 'warning' | 'error' = cs < 0.02 ? 'success' : cs < 0.04 ? 'warning' : 'error';
    return { text: `${(cs * 100).toFixed(2)}%`, color };
  };

  function HalfPane(p: { half: HalfBook; label: string }) {
    if (!p.half) return null;  // 防御: half 缺失(found:false/畸形 book)不渲染, 不读 .outcome 崩页
    const h = () => p.half;
    const bids = () => (h().bids ?? []).slice(0, 5);
    const asks = () => (h().asks ?? []).slice(0, 5);
    const maxLen = () => Math.max(bids().length, asks().length);
    const depthIdx = () => Array.from({ length: maxLen() }, (_, i) => i);
    const maxSize = () => Math.max(...bids().map((b) => Number(b.size)), ...asks().map((a) => Number(a.size)), 1);
    const imbalance = () => Number(h().imbalance);
    const imbPct = () => Number.isFinite(imbalance()) ? `${((imbalance() + 1) / 2 * 100).toFixed(0)}%` : '50%';

    return (
      <div class="v8-half-pane">
        <div class="v8-half-label">
          <Chip label={h().outcome ?? p.label} size="small" variant="outlined"
            sx={{ fontSize: '9px', height: '16px', fontWeight: 700 }} />
          <StatusDot state={wssStateToDot(h().wss_state ?? 'unknown')} size="sm" title={`WSS: ${h().wss_state}`} />
        </div>
        <div class="v8-half-ba">
          <span class="v8-bid mono-strong">{Number.isFinite(Number(h().best_bid)) ? Number(h().best_bid).toFixed(4) : '—'}</span>
          <span class="v8-ba-sep">|</span>
          <span class="v8-ask mono-strong">{Number.isFinite(Number(h().best_ask)) ? Number(h().best_ask).toFixed(4) : '—'}</span>
        </div>
        <div class="v8-half-imb">
          <span class="q-lbl">失衡</span>
          <div class="mini-imb-track">
            <div class="mini-imb-fill" style={{ width: imbPct() }} />
          </div>
          <span class="mono-sub">{Number.isFinite(imbalance()) ? imbalance().toFixed(2) : '—'}</span>
        </div>
        <div class="v8-depth-header">
          <span class="v8-depth-col-bid">量/买</span>
          <span class="v8-depth-col-ask">卖/量</span>
        </div>
        <For each={depthIdx()}>
          {(i) => {
            const b = () => bids()[i];
            const a = () => asks()[i];
            return (
              <div class="v8-depth-row">
                <div class="v8-depth-bid-side">
                  <Show when={b()}>
                    <>
                      <span class="v8-depth-size mono-sub">{Number(b().size).toLocaleString()}</span>
                      <span class="v8-bid mono-sub">{Number(b().price).toFixed(4)}</span>
                      <DepthBar size={Number(b().size)} maxSize={maxSize()} side="bid" />
                    </>
                  </Show>
                </div>
                <span class="v8-depth-sep">|</span>
                <div class="v8-depth-ask-side">
                  <Show when={a()}>
                    <>
                      <DepthBar size={Number(a().size)} maxSize={maxSize()} side="ask" />
                      <span class="v8-ask mono-sub">{Number(a().price).toFixed(4)}</span>
                      <span class="v8-depth-size mono-sub">{Number(a().size).toLocaleString()}</span>
                    </>
                  </Show>
                </div>
              </div>
            );
          }}
        </For>
        <div class="v8-half-seq">
          <span class="q-lbl">seq</span>
          <span class="mono-sub">{h().sequence_no ?? '—'}</span>
          <Show when={(h().gap_count ?? 0) > 0}>
            <Chip label={`gap:${h().gap_count}`} color="error" size="small" sx={{ fontSize: '9px', height: '14px' }} />
          </Show>
        </div>
      </div>
    );
  }

  return (
    <Show
      when={hasBook()}
      fallback={
        <div class="v8-expand-panel">
          <div class="v8-panel-title">双边订单簿</div>
          <Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>
            {isEndpointFailing(`/api/v1/book_pair/${props.conditionId}`) ? '订单簿拉取失败' : '订单簿未接入'}
          </Typography>
        </div>
      }
    >
      <div class="v8-expand-panel">
        <div class="v8-panel-title">
          双边订单簿
          {/* 订单簿新鲜度 + 快照时刻 (老板 2026-06-05 最终: 实时推送, 时刻=订单簿快照时刻) */}
          <Show when={Number.isFinite(bookAgeS())}>
            <span class="mono-sub" style={{ 'margin-left': '8px', 'font-weight': '700', color: bookFresh() }}
                  title="订单簿新鲜度 = now − 订单簿快照时刻 (as_of_ts; 后端实时每秒推当前快照)。实时推送下恒 ~0.x s, 不管簿动不动; 管道卡了(送达停)数字才变大 → 一眼看出卡。时刻 = 这份快照的发布时刻。">
              <Show keyed when={bookTs()}><span class="v8-live-dot">●</span></Show>
              {' '}订单簿新鲜度 {bookAgeS().toFixed(1)}s
              <span class="mono-sub v8-dim" style={{ 'margin-left': '5px', 'font-weight': '400' }} title="这份订单簿快照的发布时刻 (as_of_ts)">· 时刻 {fmtTs(bookTs()).slice(-12)}</span>
            </span>
          </Show>
          <Show when={vigInfo()}>
            {(vi) => (
              <Chip
                label={`vig ${vi().text}`}
                color={vi().color}
                size="small"
                variant="outlined"
                sx={{ fontSize: '9px', height: '16px', ml: 0.5 }}
              />
            )}
          </Show>
        </div>
        <div class="v8-dual-pane">
          <HalfPane half={bk().token0} label={summ()?.outcome0 || 'YES'} />
          <HalfPane half={bk().token1} label={summ()?.outcome1 || 'NO'} />
        </div>
      </div>
    </Show>
  );
}

// 单盘口最近成交 (老板「还是持仓那个地方显示」): 直接后端按盘拉 /api/v1/fills?market=cond,
//   深环5000保证有数据(不受全局churn丢失), 即便已平仓也留着买卖价。展开时每4s刷。
function MarketFills(props: { conditionId: string }) {
  // 2026-06-04 老板「都走同一个 wss」「刷新频率对齐订单簿/量化 AI」: 不再自己 4s 轮询,
  //   直接读 store.fillsByMarket (由 fetchDetailFor 与 book/quote 同 2s 节拍写入) → 完全同步刷新。
  const rows = () => state.fillsByMarket[props.conditionId] ?? [];
  const fills = () => rows().slice(0, 7);  // 7 行对齐订单簿/量化 AI 两栏高度 (老板「显示7行就行了」)
  // 2026-06-05 老板「买入卖出太草率, 看不懂卖了几单」: 加 买/卖 笔数 + 单位汇总, 每笔明确显示数量。
  const buysAll = () => rows().filter((f) => f.side === 'buy');
  const sellsAll = () => rows().filter((f) => f.side === 'sell');
  const sumU = (a: Fill[]) => a.reduce((s, f) => s + f.size_usdc, 0);
  const totalReal = () => sellsAll().reduce((s, f) => s + f.realized, 0);
  const totalFee = () => rows().reduce((s, f) => s + (f.fee ?? 0), 0);  // 本盘累计费 (逐笔加总, 对账用)
  const buysFair = () => rows().filter((f) => f.side === 'buy' && f.fair > 0);
  const avgClaim = () => { const b = buysFair(); return b.length ? b.reduce((s, f) => s + (f.fair - f.price), 0) / b.length : NaN; };
  const claimEdge = (f: Fill) => f.side === 'buy' ? f.fair - f.price : f.price - f.fair;
  // 兑现指标 (金融小梁/操盘手: operator 判策略好坏的最小集 — 入场声称之外, 看实现的胜率/EV/费拖累)。
  const closes = () => sellsAll().filter((f) => Math.abs(f.size_usdc) > 0.01);     // 实质平仓 (滤 dust)
  const winsN = () => closes().filter((f) => f.realized > 0).length;
  const lossN = () => closes().filter((f) => f.realized < 0).length;
  const winRate = () => { const n = winsN() + lossN(); return n ? winsN() / n : NaN; };
  const avgWin = () => { const w = closes().filter((f) => f.realized > 0); return w.length ? w.reduce((s, f) => s + f.realized, 0) / w.length : 0; };
  const avgLoss = () => { const l = closes().filter((f) => f.realized < 0); return l.length ? Math.abs(l.reduce((s, f) => s + f.realized, 0) / l.length) : 0; };
  const evTrade = () => { const wr = winRate(); return Number.isFinite(wr) ? wr * avgWin() - (1 - wr) * avgLoss() : NaN; };  // 单笔EV(费前gross)
  const feeDrag = () => { const g = Math.abs(totalReal()); return g > 0.01 ? totalFee() / g : NaN; };                        // 费/|毛已实现|
  return (
    <>
      <div class="v8-reject-title">成交</div>
      {/* 买/卖 笔数 + 单位汇总 — 一眼看清买了几单卖了几单 (老板) */}
      <div class="v8-pos-row" style={{ gap: '10px', 'font-size': '11px' }}>
        <span style={{ color: '#42a5f5', 'font-weight': 700 }}>买 {buysAll().length}笔 · {sumU(buysAll()).toFixed(1)}u</span>
        <span style={{ color: '#ffa726', 'font-weight': 700 }}>卖 {sellsAll().length}笔 · {sumU(sellsAll()).toFixed(1)}u</span>
        <span class="mono-sub" style={{ 'margin-left': 'auto', color: '#c97' }} title="本盘累计手续费 (逐笔加总)">费 {totalFee().toFixed(2)}</span>
        <span class={`${totalReal() >= 0 ? 'pnl-pos' : 'pnl-neg'}`} style={{ 'font-weight': 700 }}>
          已实现 {totalReal() >= 0 ? '+' : ''}{totalReal().toFixed(2)}
        </span>
      </div>
      {/* 兑现指标 (金融/操盘手): 胜率 / 单笔EV(费前) / 费拖累 — operator 判这盘策略行不行的最小集 */}
      <Show when={closes().length >= 1}>
        <div class="mono-sub" style={{ 'font-size': '10px', display: 'flex', gap: '8px', 'margin-bottom': '2px' }}>
          <span title="平仓胜率 = 盈利平仓笔 / 总平仓笔 (实质平仓, 滤 dust)">胜率 {Number.isFinite(winRate()) ? (winRate() * 100).toFixed(0) + '%' : '—'} ({winsN()}/{closes().length})</span>
          <span class={evTrade() >= 0 ? 'pnl-pos' : 'pnl-neg'} title="单笔EV(费前) = 胜率×均盈 − 败率×均亏; <0 = 负期望策略">EV {Number.isFinite(evTrade()) ? (evTrade() >= 0 ? '+' : '') + evTrade().toFixed(3) : '—'}</span>
          <span class={feeDrag() > 0.5 ? 'pnl-neg' : ''} style={{ 'margin-left': 'auto', 'font-weight': feeDrag() > 0.5 ? 700 : 400 }} title="费拖累 = 累计费 / |毛已实现|; >50% = churn 吃光 edge (头号成本)">费拖累 {Number.isFinite(feeDrag()) ? (feeDrag() * 100).toFixed(0) + '%' : '—'}</span>
        </div>
      </Show>
      <Show when={Number.isFinite(avgClaim())}>
        <div class="mono-sub" style={{ color: avgClaim() > 0.05 ? '#f44336' : '#888', 'font-size': '10px', 'margin-bottom': '2px' }}
          title="本盘买入时模型平均声称便宜多少 (>5点=模型对此盘系统性高估)">
          模型偏差 {avgClaim() >= 0 ? '+' : ''}{(avgClaim() * 100).toFixed(1)}点 · 下方逐笔
        </div>
      </Show>
      <Show
        when={fills().length > 0}
        fallback={<Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>暂无成交</Typography>}
      >
        <For each={fills()}>
          {(f) => (
            <div class="v8-pos-row" title={fmtTs(f.as_of_ts)} style={{ gap: '6px' }}>
              <span class="mono-sub" style={{ color: '#888', 'font-size': '10px', width: '54px' }}>{fmtTs(f.as_of_ts).slice(-8)}</span>
              <span class="mono-sub" style={{ color: f.side === 'buy' ? '#42a5f5' : '#ffa726', 'font-weight': 700, width: '46px' }}>
                {f.side === 'buy' ? '买' : (f.is_close ? '卖平' : '卖')}{f.outcome}
              </span>
              <span class="mono-sub" style={{ 'font-weight': 700, width: '52px', 'text-align': 'right' }} title="本笔数量(单位)">{f.size_usdc.toFixed(1)}u</span>
              <span class="mono-sub" style={{ width: '54px', 'text-align': 'right' }} title="成交价">@{f.price.toFixed(3)}</span>
              <span class="mono-sub" style={{ color: '#c97', 'font-size': '10px', width: '46px', 'text-align': 'right' }} title="本笔手续费 (老板「逐笔体现」)">费{(f.fee ?? 0).toFixed(3)}</span>
              <span class="mono-sub" style={{ color: f.fair > 0 && Math.abs(claimEdge(f)) > 0.05 ? '#f44336' : '#777', 'font-size': '10px', width: '34px', 'text-align': 'right' }}
                title="模型声称 edge (点)">{f.fair > 0 ? `${claimEdge(f) >= 0 ? '+' : ''}${(claimEdge(f) * 100).toFixed(0)}pt` : ''}</span>
              <span class={`mono-sub ${f.side === 'sell' ? (f.realized >= 0 ? 'pnl-pos' : 'pnl-neg') : ''}`} style={{ 'margin-left': 'auto' }} title="本笔已实现(卖出才有)">
                {f.side === 'sell' ? `${f.realized >= 0 ? '+' : ''}${f.realized.toFixed(2)}` : '—'}
              </span>
            </div>
          )}
        </For>
      </Show>
    </>
  );
}

// ============================================================
// 展开区子块 B: 量化 / AI (XD-1/3/4/5 红线保持)
// ============================================================

// 收敛/发散迷你图 (老板 2026-06-05「市场价相对 sharp 收敛/发散 = 持仓对错核心信号」):
//   叠 PM mid(蓝) 与 sharp fair(橙) 时序; |gap| 缩小=收敛(绿底), 扩大=发散(红底)。随 1s uiNow 重绘。
function ConvergenceSparkline(props: { conditionId: string }) {
  const W = 132, H = 30;
  const ring = () => { uiNow(); return getSharpTrend(props.conditionId); };  // uiNow 触发每秒重读 module 环
  const bounds = () => {
    const r = ring();
    if (r.length < 2) return null;
    let lo = Infinity, hi = -Infinity;
    for (const p of r) { lo = Math.min(lo, p.sharp, p.mid); hi = Math.max(hi, p.sharp, p.mid); }
    if (!(hi > lo)) { lo -= 0.01; hi += 0.01; }
    const pad = (hi - lo) * 0.08;
    return { lo: lo - pad, hi: hi + pad };
  };
  const poly = (sel: (p: { sharp: number; mid: number }) => number) => {
    const r = ring(), b = bounds();
    if (!b) return '';
    return r.map((p, i) => {
      const x = r.length <= 1 ? 0 : (i / (r.length - 1)) * W;
      const y = H - ((sel(p) - b.lo) / (b.hi - b.lo)) * H;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    }).join(' ');
  };
  // 服务端权威 (SharpFairTrack; 持久可信, 不随刷新归零): sharp_samples≥2 时用后端 conv_rate(prob/sec) 判向。
  const q = () => state.conditionCache[props.conditionId]?.quote ?? null;
  const srvSamples = () => Number(q()?.sharp_samples ?? 0);
  const srvConv = () => Number(q()?.sharp_conv_rate ?? Number.NaN);
  const srvVel = () => Number(q()?.sharp_velocity ?? Number.NaN);
  const conv = (): 'converge' | 'diverge' | 'flat' | 'none' => {
    if (srvSamples() >= 2 && Number.isFinite(srvConv())) {  // 优先服务端
      if (srvConv() < -1e-4) return 'converge';
      if (srvConv() > 1e-4) return 'diverge';
      return 'flat';
    }
    const r = ring();  // 回退: 前端自攒环 (服务端样本不足时)
    if (r.length < 3) return 'none';
    const now = r[r.length - 1], past = r[Math.max(0, r.length - 6)];
    const gNow = Math.abs(now.sharp - now.mid), gPast = Math.abs(past.sharp - past.mid);
    if (gNow < gPast * 0.7) return 'converge';
    if (gNow > gPast * 1.3) return 'diverge';
    return 'flat';
  };
  const meta = () => ({
    converge: { label: '收敛 →', color: '#4caf50', bg: 'rgba(76,175,80,0.10)' },
    diverge: { label: '发散 ←', color: '#f44336', bg: 'rgba(244,67,54,0.10)' },
    flat: { label: '震荡 ≈', color: '#888', bg: 'transparent' },
    none: { label: '攒样本…', color: '#888', bg: 'transparent' },
  }[conv()]);
  return (
    <div class="v8-q-row" style={{ 'align-items': 'center', gap: '6px' }}>
      <span class="q-lbl" title="市场价(蓝) 相对 sharp(橙) 的收敛/发散轨迹 = 持仓对错的实时信号。收敛=市场向我们 sharp 靠拢=持仓变对; 发散=变错。前端自攒, 刷新重置。">趋势</span>
      <Show when={ring().length >= 2}
            fallback={<span class="mono-sub v8-dim">{srvSamples() >= 2 ? '' : '攒样本中…'}</span>}>
        <svg width={W} height={H} style={{ background: meta().bg, 'border-radius': '3px' }}>
          <polyline points={poly((p) => p.mid)} fill="none" stroke="#42a5f5" stroke-width="1.2" />
          <polyline points={poly((p) => p.sharp)} fill="none" stroke="#ffa726" stroke-width="1.2" />
        </svg>
        <span class="q-lbl" title="蓝=PM 市场 mid · 橙=sharp fair" style={{ 'font-size': '9px' }}>
          <span style={{ color: '#42a5f5' }}>━mid</span> <span style={{ color: '#ffa726' }}>━sharp</span>
        </span>
      </Show>
      <Show when={conv() !== 'none'}>
        <span class="mono-sub" style={{ color: meta().color, 'font-weight': 700 }}
              title={srvSamples() >= 2 ? '服务端 SharpFairTrack 收敛率 (持久可信)' : '前端自攒 (服务端样本不足回退)'}>
          {meta().label}{srvSamples() >= 2 ? '' : '·前'}
        </span>
      </Show>
      <Show when={srvSamples() >= 2 && Number.isFinite(srvVel())}>
        <span class="mono-sub" title="服务端 sharp 速度 (line movement; prob/sec → 点/秒)"
              style={{ color: Math.abs(srvVel()) < 1e-5 ? '#888' : srvVel() > 0 ? '#4caf50' : '#f44336', 'font-weight': 700 }}>
          速度 {srvVel() >= 0 ? '+' : ''}{(srvVel() * 100).toFixed(2)}pt/s
        </span>
      </Show>
      {/* 持仓管理 Stage 2: 实际乘到 |target| 的调仓乘子 = 生命周期(缩噪声≤1) × CLV(可放大>1)。仅真调整时显示。 */}
      <Show when={Math.abs(Number(q()?.lifecycle_mult ?? 1) * Number(q()?.clv_mult ?? 1) - 1) > 0.02}>
        {(() => {
          const lc = Number(q()?.lifecycle_mult ?? 1);
          const cv = Number(q()?.clv_mult ?? 1);
          const eff = lc * cv;
          const n = Number(q()?.rolling_clv_n ?? 0);
          const col = eff > 1.02 ? '#4caf50' : eff < 0.98 ? '#ff9800' : '#888';
          return (
            <span class="mono-sub" style={{ color: col, 'font-weight': 700 }}
                  title={`调仓乘子 = 生命周期 ${lc.toFixed(2)} (sharp 抖动/发散→缩噪声, ≤1) × CLV ${cv.toFixed(2)} (${n} 笔滚动 CLV ${cv > 1 ? '好→放大' : cv < 1 ? '差→收缩' : ''}) = 实际乘到 |target| 的量级。方向仍 100% 归赔率源 sharp, 乘子只调规模。`}>
              仓×{eff.toFixed(2)}
            </span>
          );
        })()}
      </Show>
    </div>
  );
}

function ExpandQuotePanel(props: { quote: Quote | null; conditionId: string }) {
  if (!props.quote) {
    return (
      <div class="v8-expand-panel">
        <div class="v8-panel-title">量化 / 盘口</div>
        <Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>量化未接入</Typography>
      </div>
    );
  }

  const q = () => props.quote!;
  const fairValue  = () => Number(q().fair_value);
  const marketMid  = () => Number(q().market_mid);
  const edgeBps    = () => Number(q().edge_bps);
  const kelly      = () => Number(q().kelly_fraction);
  const notional   = () => Number(q().suggested_notional);
  const edgePos    = () => Number.isFinite(marketMid()) && marketMid() > 0 && fairValue() >= marketMid();  // NaN护栏: market_mid=0 不算正edge(架构师 B-7)
  const edgePct    = () => Math.min(Math.abs(edgeBps()) / 100, 1) * 100;
  const kellyPos   = () => Number.isFinite(kelly()) && kelly() > 0;
  // 可观测: fair 来源 / 数据管道 (老板 2026-06-02: 把后台可观测搬到前端大模型下面)
  const sharpFair  = () => Number(q().sharp_fair ?? -1);
  const hasSharp   = () => sharpFair() > 0 && sharpFair() < 1;          // inplay To Win de-vig sharp 锚
  const sharpDev   = () => hasSharp() ? (sharpFair() - marketMid()) : NaN;  // sharp vs 市场 = 价差信号
  const jointTs    = () => Number(q().joint_as_of_ts ?? 0);
  const mapped     = () => jointTs() > 0;                                // 比分/订单簿映射已连通 (匹配上)
  const devigOk    = () => q().devig_ok === true;
  const jointAgeS  = () => mapped() ? Math.max(0, (Date.now() * 1e6 - jointTs()) / 1e9) : NaN;
  // (订单簿新鲜度心跳已挪到「双边订单簿」面板 — 老板 2026-06-05「WSS新鲜度放订单簿位置+改名订单簿新鲜度」)
  // GS sharp 赔率延迟 (2026-06-05 老板「赔率延迟放合适位置」): now − sharp 赔率版本时刻 (Goalserve inplay
  //   updated_ts; 驱动 sharp fair 的那一版多旧)。这是 3s 新鲜度门管的延迟; 摆 sharp 行旁。
  const sharpTs    = () => Number(q().sharp_data_source_ts ?? 0);
  const sharpAgeS  = () => sharpTs() > 0 ? Math.max(0, (uiNow() - sharpTs() / 1e6) / 1000) : NaN;
  // 赔率版本年龄着色 (2026-06-05「飘」根治, 小郑诊断): Goalserve 每 ~2-3s 才出一版赔率 + 落后 bet365 ~2.3s
  //   (团队记忆 gs-bet365-latency), 故 ≤3.5s 是固有物理延迟属正常, >6s 才真陈旧。放宽阈值, 不再让人误以为系统卡。
  const sharpAgeColor = () => !Number.isFinite(sharpAgeS()) ? '#888'
    : sharpAgeS() < 3.5 ? '#4caf50' : sharpAgeS() < 6 ? '#ff9800' : '#f44336';
  // 量化因子 (砍大模型后保留的统计计算 — 老板 2026-06-05「留住量化因子之类的统计计算」):
  //   line movement (sharp 线速度/收敛/波动) + 仓位乘子 (生命周期 × CLV, Stage-2) + 全局滚动 CLV。
  const sharpVel   = () => Number(q().sharp_velocity);                          // prob/s, +升 −降
  const sharpConv  = () => Number(q().sharp_conv_rate);                         // 收敛率
  const sharpVol   = () => Number(q().sharp_vol);                               // 波动 σ
  const sharpN     = () => Number(q().sharp_samples ?? 0);
  const hasVel     = () => sharpN() >= 2;                                       // velocity/conv NaN→0, 凭 samples 判有效
  const lifeMult   = () => Number(q().lifecycle_mult);
  const clvMult    = () => Number(q().clv_mult);
  const hasMult    = () => Number.isFinite(lifeMult()) && Number.isFinite(clvMult());
  const rClvMean   = () => Number(q().rolling_clv_mean);
  const rClvN      = () => Number(q().rolling_clv_n ?? 0);
  // sharp 冻结状态 (操盘手老彭: 要区分"实时/冻结等待/无信号"): fair 没跟 sharp(偏离>3点) = sharp 掉档,
  //   引擎冻结持仓等 sharp 回来 (不是没信号, 是在等)。配后端 sharp-dropout 冻结逻辑。
  const srcFrozen  = () => hasSharp() && Number.isFinite(fairValue()) && Math.abs(fairValue() - sharpFair()) > 0.03;

  return (
    <div class="v8-expand-panel">
      <div class="v8-panel-title">
        量化 / 盘口 <span class="mono-sub" style={{ 'font-weight': '400' }}>· 均为 YES 边胜率</span>
      </div>

      {/* sharp 锚 (inplay de-vig) — 砍大模型后这就是【决策 fair 来源】, 摆最前高亮 */}
      <div class="v8-q-row">
        <span class="q-lbl" title="inplay bet365 'To Win' de-vig 的 YES 胜率 — 决策 fair 来源">sharp</span>
        <Show when={hasSharp()} fallback={<span class="mono-sub v8-dim">{mapped() ? '无赔率' : '未映射'}</span>}>
          <span class="mono-strong" style={{ 'font-size': '15px', 'color': '#4caf50' }}>
            {sharpFair().toFixed(4)}
          </span>
          <span class="q-lbl">vs市场</span>
          <span class={`mono-sub${sharpDev() >= 0 ? ' edge-pos' : ' edge-neg'}`} style={{ 'font-weight': '700' }}>
            {fmtBps(sharpDev() * 10000)}
          </span>
          <Show when={srcFrozen()} fallback={<span class="mono-sub" style={{ 'color': '#4caf50' }}>← 决策 fair</span>}>
            <span class="mono-sub" style={{ 'color': '#ffb74d', 'font-weight': 700 }} title="sharp 掉档 (fair 已不用它, 走了 score-prior), 引擎冻结持仓等 sharp 回来/结算 —— 不是没信号, 是在等。">⏸ 冻结·等sharp</span>
          </Show>
          {/* GS sharp 赔率延迟: now − Goalserve 赔率版本时刻 (Goalserve 每~2-3s 出一版+落后bet365~2.3s, 3s内属正常) */}
          <Show when={Number.isFinite(sharpAgeS())}>
            <span class="mono-sub" style={{ 'margin-left': 'auto', 'font-weight': '700', color: sharpAgeColor() }}
                  title="赔率延迟 = now − Goalserve inplay 赔率版本时刻 (sharp_data_source_ts)。Goalserve 每 ~2-3s 出一版 + 落后 bet365 ~2.3s, 故 ≤3.5s 属固有物理延迟(非系统卡), >6s 才陈旧。时刻=该版赔率的发布时刻。">
              延迟 {sharpAgeS().toFixed(1)}s
              <span class="mono-sub v8-dim" style={{ 'margin-left': '4px', 'font-weight': '400' }} title="该版 Goalserve 赔率的版本时刻">· 时刻 {fmtTs(sharpTs()).slice(-12)}</span>
            </span>
          </Show>
        </Show>
      </div>

      {/* 收敛/发散趋势迷你图 (老板 2026-06-05「盘口趋势 = 持仓对错核心信号」) */}
      <ConvergenceSparkline conditionId={props.conditionId} />

      <div class="v8-q-row">
        <span class="q-lbl">市场</span>
        <span class="mono-sub">{Number.isFinite(marketMid()) ? marketMid().toFixed(4) : '—'}</span>
        <span class="q-lbl" style={{ 'margin-left': 'auto' }} title="决策 fair (砍大模型后 = sharp 锚 / score-prior / 市场 de-vig)">fair</span>
        <span class="mono-strong">{Number.isFinite(fairValue()) ? fairValue().toFixed(4) : '—'}</span>
        <Show when={hasSharp() && Number.isFinite(fairValue()) && Math.abs(fairValue() - sharpFair()) > 0.03}>
          <span class="mono-sub" style={{ color: '#ff5252', 'font-weight': 700, 'font-size': '9px', 'margin-left': '4px' }}
            title="决策 fair 偏离 sharp >3点 = ResolveFair 此刻没用 sharp(走了 score-prior/市场 de-vig) → sharp 被判无效(无 bet365 odds / orientation 翻转 / 陈旧>3s)。这是「低估 YES → 卖太便宜/不持赢家」的根。">⚠源非sharp</span>
        </Show>
      </div>

      {/* edge / Kelly — 砍大模型后由 sharp fair vs 市场驱动 (不再 gated on 模型), paper 期仅建议 */}
      <div class="v8-q-row">
        <span class="q-lbl">优势</span>
        <Box sx={{ flex: 1, minWidth: '24px' }}>
          <LinearProgress variant="determinate" value={edgePct()} color={edgePos() ? 'success' : 'error'} sx={{ height: 4, borderRadius: 2 }} />
        </Box>
        <span class={`mono-sub${edgePos() ? ' edge-pos' : ' edge-neg'}`} style={{ 'font-weight': '700' }}>
          {fmtBps(edgeBps())}
        </span>
      </div>

      <div class="v8-q-row">
        <span class="q-lbl">Kelly</span>
        <span class={`mono-strong${kellyPos() ? ' kelly-pos' : ' kelly-zero'}`}>
          {Number.isFinite(kelly()) ? `${(kelly() * 100).toFixed(1)}%` : '—'}
        </span>
        <span class="q-lbl">额</span>
        <span class="mono-sub">
          ${Number.isFinite(notional()) ? notional().toLocaleString('en-US', { maximumFractionDigits: 0 }) : '—'}
        </span>
        <span class="v8-advisory-inline">[paper]</span>
      </div>

      {/* 量化因子 (砍大模型保留的统计计算): line movement + 仓位乘子 + 全局滚动 CLV */}
      <Show when={hasVel() || hasMult()}>
        <div class="v8-obs-block">
          <Show when={hasVel()}>
            <div class="v8-q-row">
              <span class="q-lbl" title="sharp 线速度 (bps/s, +升 −降) — line movement 方向因子">线速</span>
              <span class={`mono-sub${sharpVel() >= 0 ? ' edge-pos' : ' edge-neg'}`} style={{ 'font-weight': '700' }}>
                {(sharpVel() * 1e4 >= 0 ? '+' : '') + (sharpVel() * 1e4).toFixed(1)}<span class="v8-dim"> bps/s</span>
              </span>
              <span class="q-lbl" style={{ 'margin-left': '8px' }} title="收敛率 (sharp 向市场靠拢, ×1e4)">收敛</span>
              <span class="mono-sub">{(sharpConv() * 1e4).toFixed(1)}</span>
              <span class="q-lbl" style={{ 'margin-left': '8px' }} title="sharp 波动 σ (×1e4)">波动</span>
              <span class="mono-sub">{(sharpVol() * 1e4).toFixed(1)}</span>
            </div>
          </Show>
          <Show when={hasMult()}>
            <div class="v8-q-row">
              <span class="q-lbl" title="仓位乘子: 生命周期 × CLV (Stage-2 仓位管理 — 仅缩放规模, 不定方向)">乘子</span>
              <span class="mono-sub" title="生命周期乘子 (赛段)">生命 <b>{lifeMult().toFixed(2)}×</b></span>
              <span class="mono-sub" style={{ 'margin-left': '8px' }} title="CLV 乘子 (入场质量)">CLV <b>{clvMult().toFixed(2)}×</b></span>
              <Show when={rClvN() >= 5}>
                <span class="mono-sub" style={{ 'margin-left': 'auto' }} title={`全局滚动 CLV 均值 (n=${rClvN()}, 正=入场优于 fair)`}>
                  滚动CLV <b class={rClvMean() >= 0 ? 'edge-pos' : 'edge-neg'}>{(rClvMean() * 1e4).toFixed(0)}bps</b>
                </span>
              </Show>
            </div>
          </Show>
        </div>
      </Show>

      {/* 数据管道状态 (sharp 锚已挪到面板最上方) */}
      <div class="v8-obs-block">
        <div class="v8-q-row">
          <span class="q-lbl" title="比分/订单簿映射连通 (匹配到 Goalserve) + de-vig 状态 + 联合新鲜度">管道</span>
          <span class={`mono-sub ${mapped() ? 'edge-pos' : 'edge-neg'}`}>{mapped() ? '✓映射' : '✗未映射'}</span>
          <span class="mono-sub">{devigOk() ? 'de-vig✓' : 'de-vig✗'}</span>
          <Show when={mapped() && Number.isFinite(jointAgeS())}>
            <span class="mono-sub v8-dim">{jointAgeS().toFixed(0)}s</span>
          </Show>
        </div>
      </div>
    </div>
  );
}

// ============================================================
// 展开区子块 C: 持仓 + 拒单
// ============================================================

function ExpandPosPanel(props: { posRows: Position[]; rejectRows: RiskReject[]; perMarketPnl: number | null; conditionId: string }) {
  // 持仓管理 (2026-06-05 老板「持仓管理怎么体现: 凯利系数 / 希望持多少yes多少no / 实际持有 + 估值」):
  //   凯利系数 + 控制器目标仓位(希望持) vs 实际持仓 + 估值, 按 YES/NO 两边列清楚。数据: quote(凯利/目标/
  //   选边) + posRows(实际/mark)。希望持: 被选边(fair≥市场=YES, 否则NO)= 凯利目标 suggested_notional, 另一边 0。
  const quote = () => state.conditionCache[props.conditionId]?.quote ?? null;
  const num = (v: unknown): number => { const n = Number(v); return Number.isFinite(n) ? n : NaN; };
  const kelly = () => { const q = quote(); return q ? num(q.kelly_fraction) : NaN; };
  const favoredYes = () => { const q = quote(); return q ? num(q.fair_value) >= num(q.market_mid) : true; };
  const target = () => { const q = quote(); return q ? num(q.suggested_notional) : 0; };
  const posFor = (oc: string) => props.posRows.find((p) => p.outcome === oc) ?? null;
  const held = (oc: string) => { const p = posFor(oc); return p ? num(p.net_qty) : 0; };
  const val = (oc: string) => { const p = posFor(oc); return p ? num(p.net_qty) * num(p.mark_price) : 0; };
  const want = (oc: string) => { const t = target(); return (oc === 'YES') === favoredYes() ? (Number.isFinite(t) ? t : 0) : 0; };
  const u = (v: number) => (Number.isFinite(v) && v !== 0 ? `${v >= 0 ? '' : ''}${v.toFixed(1)}u` : '—');
  const hasPos = () => props.posRows.length > 0 || (Number.isFinite(target()) && target() > 0);

  // C 持仓可解释 (老板 2026-06-05「盯盘人要一眼判断这仓管得对不对」):
  //   C2 距 sharp 距离 = 入场价 vs 当前 sharp(本边); sharp_fair 是 YES 胜率, NO 边=1−sharp_yes。
  //     dist = sharp_side − 入场价: >0 = sharp 在我方上方 = 持仓正确(绿)。这是「这仓对不对」核心判据。
  //   C1 持仓时长 = 本会话墙钟近似 (后端 entry_ts 待补; uiNow 驱动每秒重算; 刷新会重置)。
  const sharpYes = () => { const q = quote(); const s = q ? num(q.sharp_fair) : NaN; return (s > 0 && s < 1) ? s : NaN; };
  const sharpFor = (oc: string) => { const sy = sharpYes(); return Number.isFinite(sy) ? (oc === 'YES' ? sy : 1 - sy) : NaN; };
  const entryFor = (oc: string) => { const p = posFor(oc); return p ? num(p.avg_entry_price) : NaN; };
  const distFor = (oc: string) => { const s = sharpFor(oc), e = entryFor(oc); return (Number.isFinite(s) && Number.isFinite(e)) ? s - e : NaN; };
  const ageSecFor = (oc: string) => { const p = posFor(oc); if (!p) return NaN; const seen = posSeenAt(p.market_id, oc); return seen ? Math.max(0, (uiNow() - seen) / 1000) : NaN; };
  const ageText = (s: number) => !Number.isFinite(s) ? '—' : s < 60 ? `${Math.round(s)}s` : s < 3600 ? `${Math.floor(s / 60)}min` : `${(s / 3600).toFixed(1)}h`;
  const heldSides = () => ['YES', 'NO'].filter((oc) => { const p = posFor(oc); return p != null && Math.abs(num(p.net_qty)) > 0; });

  // ---- 决策诊断 (老板 2026-06-09「调试持仓逻辑, 查明真正原因」): 决出状态 / 保留价可成交 / 为何无成交 ----
  //   决出状态 = must_win_lock 触发根因; 保留价 vs 市价 = 限价是否可成交; churn 往返 = 手续费放血。
  const decided = () => { const q = quote(); return q ? num(q.game_decided_sign) : 0; };
  const decidedText = () => decided() > 0 ? 'YES 方必赢 (应锁利)'
    : decided() < 0 ? 'NO 方必赢 (YES 必输)' : '未决出';
  const decidedCls = () => decided() > 0 ? 'v8-edge-pos' : decided() < 0 ? 'v8-edge-neg' : 'v8-dim';
  // 控制器目标净仓 (post-乘子 signed; 0=只减不开新仓)。
  const tgtSigned = () => { const q = quote(); return q ? num(q.target_signed_notional) : NaN; };
  // 保留价 vs 本盘 token0(YES) 市价 → 可成交判定。
  const t0book = () => state.conditionCache[props.conditionId]?.book?.token0 ?? null;
  const resBuy = () => { const q = quote(); return q ? num(q.reservation_buy_px) : NaN; };
  const resSell = () => { const q = quote(); return q ? num(q.reservation_sell_px) : NaN; };
  const bAsk = () => { const b = t0book(); return b ? num(b.best_ask) : NaN; };
  const bBid = () => { const b = t0book(); return b ? num(b.best_bid) : NaN; };
  const buyable = () => Number.isFinite(resBuy()) && resBuy() > 0 && Number.isFinite(bAsk()) && bAsk() > 0 && bAsk() <= resBuy();
  const sellable = () => Number.isFinite(resSell()) && resSell() > 0 && Number.isFinite(bBid()) && bBid() > 0 && bBid() >= resSell();
  // 本盘 churn: 累积 fills 买/卖笔数 → 往返次数 (≥2 = 反复进出付双边费 = 手续费放血风险)。
  const mktFills = () => state.fillsByMarket[props.conditionId] ?? [];
  const nBuyF = () => mktFills().filter((f) => f.side === 'buy').length;
  const nSellF = () => mktFills().filter((f) => f.side === 'sell').length;
  const roundTrips = () => Math.min(nBuyF(), nSellF());
  // sharp 冻结检测 (操盘手老彭): fair 偏离 sharp >3点 = sharp 掉档, 引擎冻结持仓等回来。
  const qSharp = () => { const q = quote(); return q ? num(q.sharp_fair ?? -1) : -1; };
  const qFair  = () => { const q = quote(); return q ? num(q.fair_value) : NaN; };
  const frozen = () => qSharp() > 0 && qSharp() < 1 && Number.isFinite(qFair()) && Math.abs(qFair() - qSharp()) > 0.03;
  // 「为何此刻无成交」推断 (decision-diag 前端版)。区分【有仓不追】vs【无仓不开】(dogfood/操盘手: 别一句话糊弄)。
  const noFillReason = () => {
    const q = quote();
    if (!q) return '无 quote';
    if (q.devig_ok === false) return 'de-vig 失败 (无市场锚 fail-closed)';
    const held = heldSides().length > 0;
    if (Number.isFinite(tgtSigned()) && Math.abs(tgtSigned()) < 1e-9 && decided() === 0) {
      if (held) return frozen()
        ? '持仓冻结中 (sharp 掉档 → 不按降级信号减/平, 等 sharp 回来/结算)'
        : '已持仓, target=0 → sharp edge 不支持加仓 (持有等收敛/结算, 非卡死)';
      return '无持仓 + target=0 → 无开仓信号 (sharp edge 不足/被门挡)';
    }
    if (Number.isFinite(tgtSigned()) && tgtSigned() > 0 && !buyable()) return '有 target 但 ask>买保留价 → 限价不追 (等回落)';
    if (decided() > 0 && !buyable()) return '已决出该锁利但 ask 已收敛 → 无套利空间';
    if (resSell() > 1.0 || resBuy() < 0) return '⚠ 保留价逃出[0,1] (fair 非 sharp 导致) → 当前不可成交';
    return '满足成交条件 (应有 intent)';
  };
  return (
    <div class="v8-expand-panel">
      <div class="v8-panel-title">
        持仓管理
        <Show when={Number.isFinite(kelly())}>
          <span class="mono-sub" style={{ 'margin-left': '8px', 'font-weight': 400 }}>
            · 凯利系数 <b style={{ color: kelly() > 0 ? '#4caf50' : '#888' }}>{(kelly() * 100).toFixed(0)}%</b>
          </span>
        </Show>
      </div>
      <Show when={hasPos()} fallback={
        <Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>无持仓 / 无目标</Typography>
      }>
        <div class="v8-pos-row" style={{ gap: '8px', color: '#888', 'font-size': '10px' }}>
          <span style={{ width: '32px' }}>边</span>
          <span style={{ width: '64px', 'text-align': 'right' }} title="控制器凯利目标仓位">希望持</span>
          <span style={{ width: '64px', 'text-align': 'right' }} title="账本当前实际持仓">实际持</span>
          <span style={{ 'margin-left': 'auto' }} title="实际持仓 × 标记价">估值</span>
        </div>
        <For each={['YES', 'NO']}>
          {(oc) => (
            <div class="v8-pos-row" style={{ gap: '8px' }}>
              <Chip label={oc} size="small" variant="outlined" sx={{ fontSize: '9px', height: '16px', fontWeight: 700, width: '32px' }} />
              <span class="mono-sub" style={{ width: '64px', 'text-align': 'right', color: want(oc) > 0 ? '#42a5f5' : '#666' }}>{u(want(oc))}</span>
              <span class="mono-sub" style={{ width: '64px', 'text-align': 'right', 'font-weight': 700 }}>{u(held(oc))}</span>
              <span class="mono-sub" style={{ 'margin-left': 'auto', color: '#bbb' }}>{val(oc) !== 0 ? fmtUsdc(val(oc)) : '—'}</span>
            </div>
          )}
        </For>
        {/* C 持仓可解释: 每个实际持仓边一行 — 入场价 vs 当前 sharp 距离 (距锚>0=持仓正确绿) + 本会话持仓时长 */}
        <For each={heldSides()}>
          {(oc) => (
            <div class="v8-pos-explain"
                 title="入场价 vs 当前 sharp(本边) 的距离 = 这仓对不对的核心判据; 距锚>0=sharp 在我方上方=持仓正确(绿)。持仓时长为本会话墙钟近似(后端 entry_ts 待补, 刷新页面会重置)。">
              <Chip label={oc} size="small" variant="outlined" sx={{ fontSize: '8px', height: '14px', fontWeight: 700, width: '30px' }} />
              <span class="mono-sub">入场 {Number.isFinite(entryFor(oc)) ? entryFor(oc).toFixed(3) : '—'}</span>
              <Show when={Number.isFinite(sharpFor(oc))} fallback={<span class="mono-sub v8-dim">· 无 sharp 锚</span>}>
                <span class="mono-sub">· sharp {sharpFor(oc).toFixed(3)}</span>
                <span class={`mono-sub ${distFor(oc) >= 0 ? 'v8-edge-pos' : 'v8-edge-neg'}`} style={{ 'font-weight': 700 }}>
                  · 距锚 {distFor(oc) >= 0 ? '+' : ''}{(distFor(oc) * 100).toFixed(1)}点
                </span>
              </Show>
              <span class="mono-sub v8-dim" style={{ 'margin-left': 'auto' }} title="本会话持仓时长 (近似)">持 ~{ageText(ageSecFor(oc))}</span>
            </div>
          )}
        </For>
        <Show when={props.perMarketPnl != null}>
          <div class="v8-pos-total">
            <span class="q-lbl">本盘 已实现+浮盈</span>
            <span class={`mono-strong ${(props.perMarketPnl ?? 0) >= 0 ? 'pnl-pos' : 'pnl-neg'}`}>{fmtUsdc(props.perMarketPnl ?? 0)}</span>
          </div>
        </Show>
      </Show>

      {/* 决策诊断 (老板 2026-06-09「查明真正原因」): 决出状态 / 保留价可成交 / churn / 为何无成交。
          始终显示 (含无持仓时) → 一眼看出"为何这盘不开/不平/不锁利"。 */}
      <Show when={quote()}>
        <div class="v8-decision-diag" style={{ 'margin-top': '6px', 'border-top': '1px dashed #373737', 'padding-top': '6px' }}>
          <div class="v8-pos-explain" title="game_decided_sign: 分运动比分+阶段判定该盘是否已决出 → 驱动 must_win_lock 锁利。未决出=不锁利。">
            <span class="q-lbl" style={{ width: '52px' }}>决出</span>
            <span class={`mono-sub ${decidedCls()}`} style={{ 'font-weight': 700 }}>{decidedText()}</span>
            <Show when={quote()?.near_end}>
              <span class="mono-sub" style={{ color: '#ffb74d', 'margin-left': '6px' }}>· 末段&gt;85%</span>
            </Show>
            <span class="mono-sub v8-dim" style={{ 'margin-left': 'auto' }} title="控制器目标净仓 (post-乘子 signed; 0=只减不开)">
              目标 {Number.isFinite(tgtSigned()) ? `${tgtSigned() >= 0 ? '+' : ''}${tgtSigned().toFixed(1)}u` : '—'}
            </span>
          </div>
          <div class="v8-pos-explain" title="保留价 = fair∓(费+margin) 限价界; 买: ask≤买保留价才成交 / 卖: bid≥卖保留价才成交。限价不追内生防churn。">
            <span class="q-lbl" style={{ width: '52px' }}>可成交</span>
            <span class={`mono-sub ${buyable() ? 'v8-edge-pos' : 'v8-dim'}`}>
              买保留 {Number.isFinite(resBuy()) ? resBuy().toFixed(3) : '—'} vs ask {Number.isFinite(bAsk()) ? bAsk().toFixed(3) : '—'} {buyable() ? '✓可买' : '✗不追'}
            </span>
            <span class={`mono-sub ${sellable() ? 'v8-edge-pos' : 'v8-dim'}`} style={{ 'margin-left': '8px' }}>
              卖保留 {Number.isFinite(resSell()) ? resSell().toFixed(3) : '—'} vs bid {Number.isFinite(bBid()) ? bBid().toFixed(3) : '—'} {sellable() ? '✓可卖' : '✗持有'}
              <Show when={resSell() > 1.0}><span style={{ color: '#ff5252', 'font-weight': 700, 'margin-left': '4px' }} title="卖保留价 >1 = 逃出概率空间 (fair 非 sharp 把它推过 1) → 任何 bid 都不可能 ≥ 它 → 静默不可卖。dogfood P1-3。">⚠&gt;1</span></Show>
              <Show when={!sellable() && Number.isFinite(resSell()) && resSell() <= 1.0 && Number.isFinite(bBid())}><span class="mono-sub v8-dim" style={{ 'margin-left': '4px' }} title="差多少点 bid 才会触发减仓">(需 bid↑{((resSell() - bBid()) * 100).toFixed(0)}点)</span></Show>
            </span>
          </div>
          <div class="v8-pos-explain" title="往返 = min(买笔, 卖笔); ≥2 = 反复进出, 每次往返付双边手续费 → 手续费放血 (账户级 fee 可吃掉毛利)。">
            <span class="q-lbl" style={{ width: '52px' }}>churn</span>
            <span class={`mono-sub ${roundTrips() >= 2 ? 'v8-edge-neg' : 'v8-dim'}`} style={{ 'font-weight': roundTrips() >= 2 ? 700 : 400 }}>
              往返 {roundTrips()} (买{nBuyF()}/卖{nSellF()}){roundTrips() >= 2 ? ' · 手续费放血风险' : ''}
            </span>
          </div>
          <div class="v8-pos-explain" title="前端综合推断当前无成交的主因 (服务端 decision-diag 的盯盘版)。">
            <span class="q-lbl" style={{ width: '52px' }}>诊断</span>
            <span class="mono-sub" style={{ color: '#bbb' }}>{noFillReason()}</span>
          </div>
        </div>
      </Show>

      {/* 成交 — 与订单簿/量化 AI 同 2s 节拍刷新 (走同一个 fetchDetailFor), 7 行对齐 */}
      <MarketFills conditionId={props.conditionId} />

      {/* 拒单明细 */}
      <div class="v8-reject-title">拒单 (最近 {Math.min(props.rejectRows.length, 5)} 条)</div>
      <Show
        when={props.rejectRows.length > 0}
        fallback={<Typography variant="caption" sx={{ color: 'text.disabled', fontStyle: 'italic' }}>—</Typography>}
      >
        <For each={props.rejectRows.slice(0, 5)}>
          {(r) => (
            <div class="v8-reject-row">
              <span class={`v8-reject-reason${r.reason_code === 'MARKET_NOT_ACCEPTING_ORDERS' ? ' v8-reject-stale' : ''}`}>
                {REJECT_REASON_ZH[r.reason_code] ?? r.reason_code}
              </span>
              <span class="mono-sub">{SIDE_ZH[r.side] ?? r.side}</span>
              <span class="mono-sub">{r.size}@{r.price}</span>
            </div>
          )}
        </For>
      </Show>
    </div>
  );
}

// ============================================================
// Market 展开区 (三列横排)
// ============================================================

export function MarketExpandArea(props: { cond: ConditionData }) {
  const c = () => props.cond;
  return (
    <div class="v8-expand-area">
      <ExpandBookPanel book={state.conditionCache[c().conditionId]?.book ?? null} conditionId={c().conditionId} />
      {/* 架构师 B-1: 父层 <Show> 守门 → ExpandQuotePanel 永远以非空 quote 挂载 (修 SolidJS early-return 致 quote
          首次 null 后响应式订阅不建立、永停"量化未接入"的反应性 bug)。quote 由 null→非空时 <Show> 重挂子组件。 */}
      <Show when={state.conditionCache[c().conditionId]?.quote} fallback={<div class="v8-expand-panel"><div class="v8-panel-title">量化 / 盘口</div><span class="mono-sub v8-dim" style={{ 'font-style': 'italic' }}>量化未接入 (等 quote)</span></div>}>
        <ExpandQuotePanel quote={state.conditionCache[c().conditionId]?.quote ?? null} conditionId={c().conditionId} />
      </Show>
      <ExpandPosPanel posRows={c().posRows} rejectRows={c().rejectRows} perMarketPnl={c().perMarketPnl} conditionId={c().conditionId} />
    </div>
  );
}

// ============================================================
// Event 分组 (Accordion 风格, 原生 CSS)
// ============================================================

