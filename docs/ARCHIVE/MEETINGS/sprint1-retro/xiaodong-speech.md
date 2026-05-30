# Sprint-1 Retro · 小董发言 (Batch 2)

- Speaker: 小董 (stats-inference)
- Date: 2026-05-28
- 约束: 听取义务 + 双向收口 (回应 Batch 1 8 份)
- Batch 1 必读: 老钱 / 小梁 / 老韩 / 老胡 / 老郭 / 老黄 / 老周 / 小余
- 行长上限: ≤ 150 行
- 关联 (我自己): `docs/RESEARCH/xiaodong-stats-validation-framework-v1.md`

---

## 0. 30 秒立场 (给老雷)

1. **7 hard gate 老钱 + 小梁全收, 我满意, 不松一个字**.
2. **老韩 misalignment #5 (RM 误拒率)** 我**接受**, 加入 v1.1 作为 G8, 但**不进 M4.5 first-pass 必过项, 是观察项**.
3. **OQ-D11 (允许 1 yellow) 老钱拒 + 小梁拒, 我 Agreed**. 7 hard 不松.
4. **OQ-D12 W1 $200 / W2 $500 / W3 $2K 我统计角度认同**, 但有一条 sample-size precondition.
5. **OQ-D13 BLACK → kill switch 老钱+小梁支持**, 我**作者立场原本就 yes**, 给 Bayesian threshold 精确化.

---

## 1. 老钱 + 小梁全接 7 hard gate — 我满意吗?

**满意. 但加 1 条 follow-up.**

老钱 §3 七 gate 全接, 小梁 §2.6 全 strong-认 (G2 / G7 标 "强认"). 这是 v1 §5.2 我作者期待的 ceiling.

**两条延伸我必须强调** (写入 v1.1):

(1) **G2 bootstrap CI 下界 > 0.3 是不可替代的**. 单 Sharpe 14 天 50 笔点估 1.0, 在 t 分布下 σ_SR ≈ √((1+0.5×1²)/50) ≈ 0.17, 95% CI ≈ [0.67, 1.33] 看似稳, 但 N=50 + SR 估计本身有偏 (LdP 2014 Eq 6), 真 SR 可能 0.5, **CI 下界 > 0.3 是把这层偏差吃掉的最小代价**. 老钱 §3 末尾的"统计功效"担忧合理, 我接受 G6 不够时 sample window 延伸 (§5.3), **不接受降阈值**.

(2) **老钱 §3 末段的 14 天 50 笔功效警告我同步背书** — 小梁 §4.3 也算了: 单笔净 1¢ × 50 笔 × $2K = $1,000 paper PnL. 在 $20-50K bankroll 上是 2-5% return / 14d, 这个体量 t-test p < 0.10 数学上勉强能过, 但 G7 paired shadow p < 0.10 才是真正的过滤器. 老钱 §3 G7 评语 "防 lucky bias" 是对的, 我**保留 G7 为最 marginal gate 的判定**: 如果 M4.5 fail, 一半概率是 G7 fail, 不是 G1/G2 fail. 这条预判我写进 v1.1 §5.5.

---

## 2. 老韩 misalignment #5: RM 误拒率 KPI — 接受加 G8?

**接受加 G8, 但定位"观察项不是 hard gate".**

老韩 §3 #5 + §6.2 派单给我: M4.5 缺 "RM 误拒率" KPI, 怕 RM 过严反推松绑. 我同意问题真实, 但**不能把 G8 加进 7 hard 一票否决组**, 理由:

1. **样本量不够**. 14 天 50 笔 reject, 假设 reject 率 30% = 15 笔, 人工 replay 15 笔判 false-reject, 阈值 ≤ 2% (老韩 §3 #5 原话) = ≤ 0.3 笔, 这是离散变量, 0/1 二值化, **没有统计意义**. M4.5 阶段不该用.
2. **人工判定主观**. 老韩派的 "小宋抽样 replay + 老韩+小梁双签判定" 引入人因, 与 G1-G7 全机器判定原则违背.

**v1.1 我加 G8 (观察项), 定义如下:**

```
G8 (观察, 非 hard):
  M4.5 窗口内 RM REJECT 总数 R
  抽样 min(R, 30) 笔走 stcpp-audit replay
  老韩 + 小梁双签判定 "本应放行" = N
  误拒率 = N / sampled, 月度报表
  阈值 ≤ 5% 月度 (不是老韩说的 2% — 2% 在 30 样本下分辨率不够)
  连续 2 个月 > 5% → 触发 RM v0.3 阈值复议 (走 ADR, 不动 G1-G7)
```

**给老韩 (§6.2 派单回执):** G8 我接, 写入 v1.1 §5.6, **但 M4.5 first-pass 不卡 G8, 只卡 G1-G7**. G8 是 long-run monitoring, 不是 14 天 gate. Sprint-2 末我交付 v1.1, 含 replay 抽样 SOP, 与你 + 小宋联签.

---

## 3. OQ-D11 (允许 1 yellow) — Agreed 拒

**Agreed. 7 hard 不松一格.**

老钱 §3 全收硬, 小梁 §2.6 显式 "不允许. 1 个 yellow 通过会破坏 multiplicative 严格性 — 一旦开口子, 下次就开两个". 这是我 v1 §5.2 原意, 现在 GM 决议链上 owner 全签, 关闭.

**统计角度 1 yellow 为什么不能开:**

7 gate 独立性假设下, single-gate FPR α = 0.10, family-wise type-I error = 1 - 0.9^7 = 52%. 已经偏高. 如果允许 1 yellow (即 "至少 6 过"), Family-wise α 变成 1 - Σ(C(7,k) × 0.1^k × 0.9^(7-k), k≤1) ≈ 0.85, **过半概率假阳通过**. M4.5 = 实盘闸门, 这个 FPR 不能要.

**唯一例外** (我 v1 §5.3 已写, 重申): G6 trade_count < 50 时**窗口延伸不算 yellow**, 是 sample augmentation, OK. 老钱 §3 末段同意这条.

---

## 4. OQ-D12 W1 $200 / W2 $500 / W3 $2K — 统计认同?

**条件认同. 加 1 条 precondition.**

GM 决议这条 sequential cap 节奏, 老钱 §3.4 hard-cap 背书 W1 $200, 小梁 §2.4 PER_ORDER_CAP_SOFT $2K 是 W3+ 落点. 节奏方向我同意, 但**统计学意义上的"上量"应该看 Sharpe 不看周数**.

**我 v1 §3.4 Pocock sequential 给的口径:**

每周作为一个 look, α 单 look spent 0.005, 累积 0.015 (3 周). 上量条件 = 当周 PnL 累计 t-stat > Pocock boundary (≈ 2.5 σ).

**与 GM cap 节奏对齐建议:**

| Week | GM cap | 我的 stat 条件 (AND) |
|---|---|---|
| W1 | $200 hard | 起步, 无 stat 条件 (sample 不够) |
| W2 | $500 | W1 累计 ≥ 20 笔 AND t-stat > 1.5 AND DD < 4% |
| W3 | $2,000 | W1+W2 累计 ≥ 40 笔 AND t-stat > 2.0 AND DD < 6% |
| W4+ | small_cap 调升 | W1-W3 累计 ≥ 60 笔 AND Sharpe (3w) > 1.0 |

**precondition**: W2 上调到 $500 不只是"W1 过了一周", 还要满足上表 stat 条件. 若 W1 sample 不够 (< 20 笔), W2 保持 $200, 不进度. 这是 Pocock 防过早 escalation 的原意.

**给老钱 (§3 W1 $200 hard cap 落 RM 软参数):** 我接你的 hard, 加我 stat precondition, **不冲突**. RM 软参数表 $200 不变, 上调到 $500 走 ADR + 我 stat 条件双签.

---

## 5. OQ-D13 BLACK → kill switch — 我建议?

**我作者立场原本就 yes**, 老钱 §3 + 小梁 §2.6 + 老韩 §6.2 三方支持, 关闭. 给 Bayesian threshold 精确化:

**v1 §5.4 写法 (我重申):**

```
Bayesian decay monitor (μ_i = strategy i 真 daily PnL mean):
  prior: N(0, σ_prior²), σ_prior = 0.3 × bankroll × historical_vol
  posterior 用滚动 14 天数据 + Bayesian update
  
状态:
  GREEN:  P(μ_i > 0 | data) > 0.7
  YELLOW: P(μ_i > 0 | data) ∈ [0.5, 0.7]
  RED:    P(μ_i > 0 | data) ∈ [0.3, 0.5]
  BLACK:  P(μ_i < 0 | data) > 0.3 持续 2 周

BLACK 触发动作:
  1. RM 立刻 kill switch (老韩接口, evaluate() 一律 REJECT(STRATEGY_DECAYED))
  2. 不能 hot reload 恢复, 必须 ADR + 老钱+老雷+小梁三方签
  3. audit emit AET_STRATEGY_DECAYED (老唐 schema 加 enum)
```

**给老韩 (RM kill switch interface):** Sprint-2 我交付 `bayes_decay_monitor.py` MVP, 每日 cron, 推 metric `stcpp_strategy_decay_state`. RM v0.3 读这个 metric 做 hard interlock. 我 + 你 Sprint-2 W3 联签 interface spec.

---

## 6. 给 GM 老雷 (双向收口)

我已闭项 (Batch 2 内):
1. M4.5 7 hard gate **关闭**, 老钱 + 小梁全收
2. OQ-D11 **关闭** (拒, 不允许 yellow)
3. OQ-D13 **关闭** (Bayesian BLACK → RM kill switch)
4. G8 (RM 误拒率) **加入 v1.1 观察项**, 不进 hard gate

待 Sprint-2 我 owner:
1. v1.1 文档 (加 G8 + §5.5 G7 marginal 预判 + §5.6 G8 SOP) — 截止 6/19
2. `bayes_decay_monitor.py` MVP — 截止 Sprint-2 末
3. `tools/m45_gate_evaluator.py` 公式定稿 + 单测 (老钱 §5 P0-3) — 截止 Sprint-2 末
4. OQ-D7 (DSR 口径) 小梁 §2.6 已回 LdP 2014 标准, 我 v1.1 落

OQ-D 残留: D1 (老胡 6/19) / D2 (小蒋 6/26) / D5 (老韩+小蒋 M2) / D9 (小程+小蒋).

---

**7 hard 关闭. G8 观察项不进 hard. yellow 永拒. W1/W2/W3 cap 认+stat precondition. BLACK→kill switch 是我原意. Sprint-2 交 v1.1 + bayes monitor + gate evaluator 三件.**

— 小董, 2026-05-28
