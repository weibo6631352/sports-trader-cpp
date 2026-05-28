# Sprint-1 Retro — 小袁发言 (微观结构 / 订单簿)

- Speaker: 小袁 (orderbook-microstructure)
- Date: 2026-05-28
- 场合: Batch 2, 已读 Batch 1 八份 (老周/老韩/老郭/老钱/小梁/老胡/老黄/小余)
- 约束: 听取义务 + 双向收口

---

## 0. 三句话立场

1. **支持老韩 MarketState 分段 STALE — 但 hot 还要切一刀**. v1 §3.5 实测 hot `T_{1/2}=0.21s` 是 inplay ±10min 平均, NBA Q4 末 2min / NFL 2-min warning / MLB 9th 一分差 三个 micro-state 实测 `T_{1/2} < 0.15s`, 500ms 仍 ≥ 3 半衰期. 加一档 INPLAY_HOT_CRIT (200ms/800ms).
2. **支持小梁 5¢ + 老钱 P0-02 OOS Sharpe ≥ 0.8 含 fee**. 数学站得住. 但补一条结构性事实: 0 套利不是机器人勤奋, 是 PM us-east maker 撤单 23-47ms 反应窗, 我们跨洋 64ms 永远输 race. 这件事在 sizing 上要解.
3. **Mode A 上 MVP 我支持**, 但 paper Mode A 默认 fill_rate=1.0 与小肖 floor 0.50 不对齐, paper Sharpe 系统性偏高 0.1-0.3. 加 Mode A++ (fill_rate Bernoulli sampler), 不必等 Mode B.

---

## 1. Q1: 老韩 STALE 分段 — 数据角度支持, hot 加一档

老韩 §3 misalignment #1 建议 4 档 (INPLAY_HOT/PREGAME_NEAR/PREGAME_FAR/OUTRIGHT). 我支持骨架, 加 1 档 5 行表:

| micro-state | `T_{1/2}` 实测 (n) | WARNING | HALT |
|---|---|---|---|
| **INPLAY_HOT_CRIT** (NBA Q4<2min / NFL 2-min / MLB ≥8 局一分差 / OT) | **0.12-0.15s** (n=23) | **200ms** | **800ms** |
| INPLAY_HOT (其余 inplay) | 0.21-0.45s | 500ms | 2000ms |
| PREGAME_NEAR | 8-30s | 2000ms | 10000ms |
| PREGAME_FAR | 60-120s+ | 10000ms | 30000ms |
| OUTRIGHT | 120s+ | 30000ms | 60000ms |

**为什么 hot 要再切**: Q4 末 2 分钟是 P0-02 (比分失配) 最赚钱窗口. hot 500ms WARNING 在 micro-state 下 = `T_{1/2}=0.15s` × 3.3 半衰期, quote 78% 已死. 5 档不算复杂, 5 个数字而已 RM 查表.

**hot 判定 code-level (老韩 §6.2 派我的, Sprint-2 中给 RM v0.3)**:

```
classify(market, now):
  if NBA && period==4 && clock<120s        → INPLAY_HOT_CRIT
  if NFL && two_minute_warning_active      → INPLAY_HOT_CRIT
  if MLB && inning>=8 && |score_diff|<=1   → INPLAY_HOT_CRIT
  if NHL && period==3 && clock<300s && |score_diff|<=1 → INPLAY_HOT_CRIT
  if overtime_active                       → INPLAY_HOT_CRIT
  if game_in_progress && wss_rate_60s>1/s  → INPLAY_HOT
  ...
```

**已知风险**: micro-state 依赖 Goalserve push (小余 C-03 p95 7s 延迟), 进入会延迟 5-10s. 我接受 — stale-from-Goalserve 退一档 hot 比 stale-from-PM 用错 quote 危害小.

---

## 2. Q2: 0 套利 + 3% taker — 微观结构角度支持 5¢

### 2.1 小梁 §2.2 算账我看了, 数学对

| 项 | 数 |
|---|---|
| Pinnacle no-vig 偏离触发 | 5¢ |
| taker fee (3% × $1 notional) | ~3¢ |
| 滑点 ($2K gameday) | 0¢ |
| 净 edge | **2¢** ($2K) / **1¢** ($10K) |

同意 P0-01 5¢, P0-02 进 OOS Sharpe gate 含 fee.

### 2.2 但 0 套利不只是机器人勤奋 — 是 us-east maker 撤单 23-47ms 反应窗

v1 §4.2 实测 maker 撤单→重报 mean 23-47ms, p95 ~120ms.

含义:
- 我们发现 5¢ 偏离 → 跨洋 64ms → 到 PM = T+64ms
- 同期 us-east maker 已在 T+23ms 撤掉 stale quote
- **我们看到的 quote 大概率不是当前 quote** — 结构性 race we lose

### 2.3 sizing 修正建议 (给老韩 RM v0.3)

```
R-new: if (quote_age_ms > 100 && market_state == INPLAY_HOT[_CRIT]) size *= 0.5
```

这是 microprice 失效兜底, 不与小肖 slippage 重叠. P0-02 比 P0-01 受影响大 — P0-02 inplay hot 触发, P0-01 多在 PREGAME 段 maker 撤单慢一档.

### 2.4 给小梁 / 小董 P0-02 一条补充

P0-02 INPLAY_HOT_CRIT 触发 fill_rate 实测会显著低于 0.50 floor (maker 已撤). **fill_rate < 0.30 = 信号死**, 不只看 Sharpe. 加进小董 G6 注释 + 小梁 v1.1.

---

## 3. Q3: Mode A 上 MVP 合适吗

**合适**. MVP P0-01/P0-02 都是 taker, Mode A (best ask fill, slippage=0) 够. Mode B 小蒋 §10.1 PR-9 M3 上, Mode C v2 maker 再做.

### 3.1 但 Mode A 默认 fill_rate=1.0 与 live 系统性偏差

- live fill_rate 实测预期 0.40-0.65 (v1 §3.5)
- Mode A 假设 1.0
- paper Sharpe 系统性偏高 0.1-0.3
- 即 M4.5 G2 paper Sharpe 1.0 ≈ live 0.7-0.9, 小董 CI 下界 0.3 之上但比预期紧

### 3.2 R-14 (新, 给小蒋): Mode A++ fill_rate sampler

```
R-14: paper Mode A 套 fill_rate ~ Bernoulli(0.50-0.65) sampler
      不必等 Mode B; 小肖 v1 floor 0.50 是 live 假设, paper 不能 1.0
```

**给老钱 OQ-10 回复**: 倾向 Mode A 起步 + Mode B M3 上 + Mode C 永不进 MVP, 我同意. 加 Mode A++ MVP 就够.

---

## 4. Q4: 老周 vCPU0 11 connection — 我要 dedicated 吗

**不要**. 微观结构信号 (imbalance / microprice / quote_rate / VPIN-lite M5+) 全部基于 WSS price_change + book event, 与老周 v0.3 §17.1.1 T0 同源, 走 book_to_strat_ring 8ns SPSC 顺序天然保障.

### 4.1 但 §17.6.1 静默期我要主动降级

INPLAY_HOT_CRIT 实测 WSS 静默 max 8-12s. 30s 阈值不会误触 reconnect, **但 8-12s 内 microprice 已死** (`T_{1/2}=0.15s` × 80 半衰期).

我侧 §5 实现自己加一条 (不抓老周):

```
if (token_silence_ms > 5 * T_half_ms_micro_state) {
  signal_state = DEGRADED;  // microprice 不发, 等 RM STALE warning
}
```

### 4.2 老周 Escalate-1 (vCPU0 11 reactor) 我投一票

**倾向 (C) MVP NBA only**, M3 后扩 MLB. 11 connection burst 若 vCPU0 p99 > 50us, 我的 microprice 跟着延迟信号死. 等老姜 Sprint-2 W4 压测数据再判, 我配合提供典型 micro burst load profile.

---

## 5. Q5: 微观结构原始数据保留量级 (给小余)

| 数据类 | 用途 | 频率 | 6 月 NBA+MLB |
|---|---|---|---|
| WSS book full tape (raw frame) | quote half-life 校准 / replay | 全量 | ~200 GB (小余 §5.1 已估) |
| 1s snapshot (5 levels + cum depth) | imbalance / microprice 回放 | 1Hz | ~30 GB |
| micro-state 标注 (Q4 末 / 2min warning) | INPLAY_HOT_CRIT classifier 训练 | event | <100 MB |
| PM trade tape (taker/maker/side) | sharp money / VPIN-lite (M5+) | 全量 | ~10 GB |

**总 ~ 240 GB / 6 月**, 在小余 §5.1 200-250 GB 预算内 (+40 GB 1s snapshot).

### 5.1 落库约束

1. **1s snapshot 我自己生成不抓小余 ETL**. 小冯 raw frame → 我侧 1s 重建 → parquet partition by event_date/market_id_prefix.
2. **90 天 hot + 6 月 cold zstd-19**. half-life 校准月度即可不需 24/7 在线.
3. **PIT 用 event_ts 不用 ingestion_ts** (小余 R-1 对齐).

### 5.2 OQ-11 us-east colo

老钱 §5 P1 第 6 条拒 colo 进 Sprint-2 我同意 MVP 不上, **但 M5 后必须上**: maker 撤单 23-47ms (§2.2) 是 us-east 内, 跨洋 64ms 永远输. v2 maker 没 colo 不能上. 老钱 §7 第 3 条预留升级路径, OK.

---

## 6. 双向收口

### 6.1 我接的派单

| 来自 | 派单 | 截止 |
|---|---|---|
| 老韩 §6.2 | hot token 判定 code-level 给 RM v0.3 | Sprint-2 中 |
| 老钱 §5 P0-2 | OQ-6 Goalserve push → PM maker Δ 分布实测 | Sprint-2 末 |
| 小蒋 PR-9 | Mode A++ fill_rate sampler 联调小肖 | Sprint-2 中 |
| 小余 C-19 | order arrival/cancel 拆分字段需求 v1.1 | Sprint-2 W1 |

### 6.2 我派出 (等回执)

| 派给 | 派单 | 截止 |
|---|---|---|
| 老韩 | INPLAY_HOT_CRIT (200/800ms) 加进 RM v0.3 §11 | Sprint-2 末 |
| 小梁 | P0-02 fill_rate < 0.30 = 死 + 进小董 G6 注释 | Sprint-2 中 |
| 小蒋 | R-14 Mode A++ fill_rate Bernoulli(0.50-0.65) | Sprint-2 中 |
| 老周 | §17.6.1 per-token 5×T_half 触发 micro DEGRADED | v0.4 |
| GM 老雷 | INPLAY_HOT_CRIT 进 MVP 拍板 (我倾向进, 5 个数字而已) | 本会议 |

---

## 7. 我承认做错的

1. **v1 §3.5 hot/cold 二态太粗** — 没把 INPLAY_HOT_CRIT 单列, 老韩 v0.2 STALE 基于"hot 平均态". 反思: 实测分 micro-state 是基本功, 不能图省事用平均.
2. **v1 §3.7 maker rebate 0.75% 没强调"colo 后才可行"** — 老钱 §6 把它当 scope creep 拒, 我表达不清. v1.1 §8 加 "maker = v2 + colo + quote update < 50ms" 明文.
3. **paper Mode A 默认 fill_rate=1.0 与小肖 floor 0.50 不对齐** — 应该 Sprint-1 内自己提, 拖到 Batch 2. 反思: paper 设计要提前注入已知 live 偏差.

---

## 8. Sprint-2 承诺

| # | 承诺 | 度量 | 截止 |
|---|---|---|---|
| S2-MICRO-1 | v1.1: INPLAY_HOT_CRIT 一档 + Mode A++ + maker=v2/colo 明文 | doc, 老韩/小蒋/老钱 review | W2 |
| S2-MICRO-2 | hot 判定 code-level 给 RM v0.3 (与小段 Goalserve 联调) | code rules | Sprint-2 中 |
| S2-MICRO-3 | OQ-6 Goalserve push → PM maker Δ 实测 (n≥100) | 报告给老钱+小程 | Sprint-2 末 |
| S2-MICRO-4 | paper Mode A++ fill_rate sampler 联调小肖+小蒋 | code + 单测 | Sprint-2 中 |
| S2-MICRO-5 | 微观结构 schema 给小余 (1s snapshot + trade + micro-state) | schema YAML | W1 |

**不进 Sprint-2 (M5+)**: VPIN-lite / multi-level imbalance / queue position 精确建模 / colo 评估.

---

**END.** 等 GM 拍 INPLAY_HOT_CRIT 是否进 MVP 即可推进 Sprint-2.

— 小袁, 2026-05-28
