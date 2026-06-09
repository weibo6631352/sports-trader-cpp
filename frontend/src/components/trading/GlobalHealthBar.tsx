// trading/GlobalHealthBar.tsx — 盯盘页顶部全局健康栏 (从 TradingPage.tsx 拆出, 2026-06-10 架构师 A3)。
//   后端/WSS 灯 + 信号/持仓/净PnL + 异常置顶聚合。自包含 (零 props, 读全局 state)。
import { createSignal, For, Show } from 'solid-js';
import { state, getSharpTrend, uiNow } from '../../store';

export function GlobalHealthBar() {
  const [open, setOpen] = createSignal(false);
  const backendOk = () => state.healthz?.ok === true;
  const wssOk = () => state.status?.wss_connected?.clob === true;
  const signals = () => state.status?.signals_active_count ?? 0;
  const positions = () => state.positions?.positions?.filter((p) => Math.abs(Number(p.net_qty)) > 0) ?? [];
  // 全局 net PnL —— 修「positions.pnl_realized 硬编码 0 → 健康栏亏损低估 73% (金融 P1-B)」: 用 account 权威口径
  //   net_pnl (= equity − bankroll, 含已实现+浮盈−费), 不再逐盘加总(平仓盘已不在 positions 列表 → 结构性丢已实现)。
  const floatPnl = () => {
    const a = (state.account as any)?.account;
    if (a && Number.isFinite(Number(a.net_pnl))) return Number(a.net_pnl);
    return positions().reduce((s, p) => s + Number(p.pnl_unrealized), 0);
  };
  const mktName = (cid: string) => state.conditionCache[cid]?.summary?.title || cid.slice(0, 8);

  // 异常扫描 (全局可算): 后端/WSS 断 + 持仓 sharp 反向 + 持仓发散 + 资金不足拒单。
  const anomalies = () => {
    uiNow();  // 趋势/年龄随 1s 时钟刷新
    const out: Array<{ sev: 'err' | 'warn'; text: string }> = [];
    if (state.healthz && !backendOk()) out.push({ sev: 'err', text: '后端离线 · 数据停更' });
    if (state.status && !wssOk()) out.push({ sev: 'err', text: '订单簿 WSS 断连 · 价可能过期' });
    const divergeSeen = new Set<string>();  // 发散是 per-market, 同盘 YES+NO 两条 position 别重复告警 (架构师 B-4)
    for (const p of positions()) {
      const cid = p.market_id;
      // 优先 quote.sharp_fair (展开按需拉, 更鲜) 回退 summary.sharp (grid 2s 批量) — 防漏报错向仓 (dogfood#5/操盘手Bug4)
      const sq = state.conditionCache[cid]?.quote;
      const qSh = Number(sq?.sharp_fair ?? -1);
      const sy = (qSh > 0 && qSh < 1) ? qSh : state.conditionCache[cid]?.summary?.sharp;  // YES sharp
      const entry = Number(p.avg_entry_price);
      if (sy != null && sy > 0 && sy < 1 && Number.isFinite(entry)) {
        const sSide = p.outcome === 'YES' ? sy : 1 - sy;        // 本边 sharp
        const dist = sSide - entry;                              // <0 = sharp 已跌破入场 = 持仓亏向
        if (dist < -0.02) out.push({ sev: 'warn', text: `${mktName(cid)} ${p.outcome} · sharp 已反向 ${(dist * 100).toFixed(1)}点` });
      }
      // 发散告警 (per-market, 去重): 优先服务端 conv_rate (SharpFairTrack, 持久权威), 回退前端自攒环。
      if (divergeSeen.has(cid)) continue;
      const srvN = Number(sq?.sharp_samples ?? 0);
      const srvC = Number(sq?.sharp_conv_rate ?? Number.NaN);
      if (srvN >= 2 && Number.isFinite(srvC)) {
        if (srvC > 1e-4) { out.push({ sev: 'warn', text: `${mktName(cid)} · 持仓发散中 (市场远离 sharp)` }); divergeSeen.add(cid); }
      } else {
        const ring = getSharpTrend(cid);
        if (ring.length >= 3) {
          const now = ring[ring.length - 1], past = ring[Math.max(0, ring.length - 6)];
          const gNow = Math.abs(now.sharp - now.mid), gPast = Math.abs(past.sharp - past.mid);
          if (gNow > gPast * 1.3 && gNow > 0.01) { out.push({ sev: 'warn', text: `${mktName(cid)} · 持仓发散中 (市场远离 sharp)` }); divergeSeen.add(cid); }
        }
      }
    }
    const insf = (state.rejects?.rejects ?? []).filter((r) => r.reason_code === 'INSUFFICIENT_FUNDS').length;
    if (insf > 0) out.push({ sev: 'warn', text: `资金不足拒单 ×${insf}` });
    return out;
  };
  const errCount = () => anomalies().filter((a) => a.sev === 'err').length;

  return (
    <>
      <div class="v8-health-bar">
        <span class={`v8-health-dot ${backendOk() ? 'hd-ok' : 'hd-err'}`} title="后端心跳 /healthz">● 后端</span>
        <span class={`v8-health-dot ${wssOk() ? 'hd-ok' : 'hd-err'}`} title="订单簿 WSS (clob) 连接">● WSS</span>
        <span class="v8-health-sep">·</span>
        <span class="mono-sub" title="活跃信号数">信号 {signals()}</span>
        <span class="v8-health-sep">·</span>
        <span class="mono-sub" title="持仓盘口数 + 账户净盈亏 (account.net_pnl 权威口径: 已实现+浮盈−费, = equity−bankroll)">
          持仓 {positions().length} · 净 <b class={floatPnl() >= 0 ? 'pnl-pos' : 'pnl-neg'}>{floatPnl() >= 0 ? '+' : '−'}${Math.abs(floatPnl()).toFixed(1)}</b>
        </span>
        <span class="v8-health-spacer" />
        <Show when={anomalies().length > 0} fallback={<span class="v8-health-ok">✓ 无异常</span>}>
          <span class={`v8-health-alert ${errCount() > 0 ? 'hd-err' : 'hd-warn'}`}
                onClick={() => setOpen((v) => !v)} role="button" tabIndex={0}
                title="点击展开/收起异常列表">
            ⚠ {anomalies().length} 需关注 {open() ? '▲' : '▼'}
          </span>
        </Show>
      </div>
      <Show when={open() && anomalies().length > 0}>
        <div class="v8-health-list">
          <For each={anomalies().slice(0, 12)}>
            {(a) => <div class={`v8-health-item ${a.sev === 'err' ? 'hd-err' : 'hd-warn'}`}>{a.sev === 'err' ? '🔴' : '🟠'} {a.text}</div>}
          </For>
        </div>
      </Show>
    </>
  );
}
