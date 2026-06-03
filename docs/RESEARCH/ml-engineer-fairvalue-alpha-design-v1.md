# Fair-Value ML 模型架构 — 找真 alpha + 根治标签泄漏 (设计 v1)

> **owner:** 小邓 (ml-advisor, #31) | **last_review:** 2026-06-03
> **类型:** 研究/设计文档 (不碰生产代码)。承接 `ml-calibration-and-edge-reality-v1.md` §7/§8 事故链 + §8 治本待办。
> **召唤场景:** v2 候选 (fair-value NN) + rule-based 天花板 + MLOps 评估。
> **协作边界提醒 (我的 persona):** v1 不引 ML;ML 是量化研究候选工具;**ML 必经 walk-forward backtest** 才能上线。本文给的就是这个 backtest 关卡。

---

## TL;DR (老板先读这段)

1. **事故根因一句话:** 当前模型 regress 预测结算结果 `outcome∈{0,1}`,特征里有市场价 `b_mid`。近结算的市场价已≈结果 → 模型平凡学到「价≈果」→ AUC=1.0 退化 → 没有 alpha。**这不是 bug,是建模范式错了。** 加再多 gate / 数据筛选只能挡住垃圾,挡不住「模型本身学不到超越市场的东西」。

2. **目标重设计推荐 = 方案 (a) residual 模式,但分两层:**
   - **L1 (先做,低风险):** `y = 结算结果 − sharp_inplay_fair`(残差锚 **sharp** 而非市场价)。本质 = 让模型学「sharp 之外的修正」。无残差信号 → delta≈0 → fair=sharp,**edge 退化到 0 是安全的**(不会产垃圾)。
   - **L2 (有 sharp 覆盖缺口时):** `y = 结算结果 − score_prior_blend`(残差锚比分先验)。用在没 bet365 覆盖的盘口。
   - **关键:** residual 模式下,**market-derived 特征 (b_mid/microprice/dislocation/x_*_minus_mid) 必须全排**。它们是泄漏源。模型只能看 game-state + sharp + 微结构,从中预测「baseline 还差多少」。

3. **验证关卡 (上线前必过 7 关,任一不过不许驱动真单):** purged walk-forward + by-game split + 残差信号显著性 + **CLV 作 ground truth** + paper PnL 净 BE 后为正 + drift 监控 + 影子运行 2 周。详见 §4。

4. **诚实判断 (§6,带 short-horizon 实证数据):** 在**有 bet365 sharp 覆盖**的盘口,ML residual 能挤出薄 alpha(修正 sharp 的微小偏差 + 抢市场重定价的 12s 窗口),但**期望很薄**。在**没有 sharp 覆盖**的盘口,ML 几乎找不到散户没有的信息——**alpha 主力是事件延迟套利(比市场快 12s 反应比分),不是「更聪明的 fair-value 模型」**。把 ML 定位成「事件延迟套利的触发器/方向器 + sharp 残差修正器」,而非「独立 fair-value 预言机」。这是数据支撑的判断,不是画饼。

---

## 1. 目标重设计:模型应该预测什么?

### 1.1 当前范式为什么必然没 alpha (机理,不是玄学)

当前 `train_fair_value.py` mode=regress:`y = label ∈ {0,1}`,X 含 `b_mid` (f8)、`b_microprice` (f9)、`b_dislocation` (f64)、`x_*_minus_mid` 等一堆 **market-derived** 列。

体育市场结算前,市场价 `b_mid` 随比赛进程收敛到 0 或 1。训练样本里**近结算的行占主导**(一场比赛末段产生大量快照,且那时 mid 已极端)。模型只要学会「`b_mid` 高 → y=1」就能在训练集/holdout 上拿到 AUC≈1.0。

**这就是泄漏:** 模型没学到任何「市场不知道的东西」。它学到的是「市场已经知道的东西」(mid 本身就是市场对 y 的估计)。把这种模型当 fair → `fair ≈ b_mid` → `edge = fair − mid ≈ 0` → 要么不交易,要么(退化时)输出垃圾常数 0.0005 → 假 86% edge → 垃圾成交(§7 事故)。

**核心定律:** *任何把市场价当特征、把结算结果当标签的模型,最好的情况是复刻市场,最坏的情况是退化产垃圾。它的上限就是市场本身,不可能有正 alpha。*

### 1.2 候选方案对比

| 方案 | 预测目标 | alpha 来源 | 泄漏风险 | 可行性 | 评价 |
|---|---|---|---|---|---|
| **(a) residual** | `y = outcome − baseline_fair` | 模型修正 baseline 的系统性偏差 | **低**(若 baseline=sharp 且排 market 列) | **高**(数据已落 `fair_value` 字段) | **推荐** |
| (b) 纯 game-state | `outcome`,X 排所有 market 列,只留比分/时钟/动量 | fair vs 市场偏离=edge | 中(game-state 本身公开,散户也定价) | 中 | 备选/L2 锚 |
| (c) 未来短期价格变动 | `Δmid(t→t+H)` 的方向/幅度 | 抢市场重定价延迟(事件延迟套利) | 中(需严格 PIT,易前视泄漏) | 中(实证窗口窄,见 §6) | **独立的第二模型,不是 fair-value** |
| (d) sharp-residual ensemble | `y = outcome − sharp`,sharp 当 base learner | 同 (a),但显式当 boosting base | 低 | 高 | = (a) 的 L1,推荐落地形态 |

### 1.3 推荐:方案 (a) residual,两层 baseline

**为什么 residual 是对的范式:**

residual 模式把 fair 拆成 `fair = baseline + delta`。baseline 是一个**已知可信的锚**(sharp inplay de-vig,或 score-prior blend),delta 是**模型负责的、baseline 没捕捉到的修正量**。

- **安全性内建:** 如果模型学不到任何残差信号(残差不可预测),它会输出 delta≈0 → `fair = baseline` → edge 来自 baseline 本身(sharp vs 市场),**不会无中生有产垃圾**。这是 regress 模式做不到的(regress 退化会产常数垃圾)。
- **alpha 定义清晰:** delta ≠ 0 且统计显著,才说明「模型发现了 sharp/先验之外的系统性偏差」。这是可验证、可证伪的 alpha 主张。
- **数据已就位:** `feature_vector_recorder.hpp:107` 已经把 `fair_value`=baseline_fair 落进训练 jsonl(注释明写「残差训练 baseline 锚;训练侧 y=label−fair_value」)。`train_fair_value.py` 的 `mode=residual` 分支也已存在(`y.append(label − fair_value)`)。**基础设施在,只是没接通 + baseline 锚错了。**

**两层 baseline:**

- **L1 — sharp 残差 (优先,有 bet365 覆盖时):** `baseline = g_bm_inplay_fair (f18)`。模型学「sharp 之外的修正」。sharp 是市面上最强的 fair 锚,能在它之外挤出 delta 才是真本事。这是 alpha 上限最高、最干净的形态。
- **L2 — 先验残差 (无 sharp 覆盖时):** `baseline = score_prior_blend`。模型学「比分先验之外的修正」。alpha 来源弱(见 §6),但比纯 game-state regress 安全。

> **落地形态:** 训练时按 baseline 来源分两个模型(或一个模型 + baseline-source 类别特征 cat 区分)。推理时 paper_loop 已有 `fair_resolve.hpp` 优先级链:sharp 有效 → 用 L1 模型的 delta 叠加在 sharp 上;sharp 无效落 score-prior → 用 L2 模型的 delta。**delta 叠加点就是 `ResolveFair` 第 4 步**(见 §5)。

---

## 2. 特征:该用 / 该排

### 2.1 总原则

residual 模式下,**baseline 已经吸收了「市场对结果的估计」**。如果再把 market-derived 特征喂进去,模型会重新学到「market ≈ outcome」,从而预测出 `delta = outcome − baseline ≈ market − baseline`——又回到复刻市场,残差信号被市场价污染。**所以 market-derived 列必须排。**

但要区分两类「市场列」:
- **市场价水平 (level)** = 对 outcome 的直接估计 → **泄漏,排**。
- **市场微结构 (flow/microstructure)** = 不直接是 outcome 估计,而是「市场参与者行为」 → **可留**(它能告诉模型「市场正在往哪动/有没有反应过来」,这是抢延迟的信号,不是抄答案)。

### 2.2 逐类裁决

| 特征类 | 列 (index) | 裁决 | 理由 |
|---|---|---|---|
| **市场价水平** | b_mid(8), b_microprice(9), b_best_bid(13), b_best_ask(14), b_no_microprice(44) | **排** | 直接≈outcome,泄漏源 |
| **市场偏离 (含 mid)** | x_devig_minus_mid(16), x_microprice_minus_mid(17), b_dislocation(64), x_log_odds_fair(60), x_log_odds_edge(61), x_inplay_fair_minus_mid(102), x_inplay_market_absdev(103) | **排** | 含 mid 项;`*_minus_mid` 把市场价偷渡进来。**注意 102/103 含市场 mid → 排** |
| **arb/pin (含 mid/fair)** | x_pin_risk(62), x_pin_x_expiry(63), x_yes_no_bid_sum(104), x_arb_free_edge(105) | **排** | 都从市场报价或 fair 算,泄漏 + 部分是决策输出回灌 |
| **game-state (核心信号)** | g_score_diff(0), g_score_total(1), g_period(2), g_elapsed_sec(3), g_time_status(4), g_remaining_sec(67), g_game_phase(70), g_garbage_time(71), g_clutch(72), g_time_x_lead(65), g_periods_won_*(68/69) | **留** | game-state 是 baseline 之外修正的真来源 |
| **动量/事件 (核心信号)** | g_goal_freshness(73), g_net_momentum_5m(74) | **留** | 进球新鲜度=抢延迟的关键(见 §6) |
| **live_stats (xG 代理)** | g_danger_attack_diff(19), g_shot_on_target_diff(20), g_possession_home(21), g_red_card_diff(22), g_corner_diff(23), g_fld_signal(66) | **留** | 比分之外的领先质量,sharp 可能没完全定价 |
| **sharp 锚 (L1 baseline,不当 X)** | g_bm_inplay_fair(18) | **L1 当 baseline,不进 X**;**L2 时可进 X** | L1 模式它是 baseline 不能既当锚又当特征;L2(锚 score-prior)时它是有用的额外信号 |
| **跨庄 de-vig (pregame)** | g_bm_devig_p_yes(5), g_bm_overround_avg(6), g_valid_bm_count(7) | **留 (谨慎)** | 是 fair 估计但**来自外部赔率非 PM 市场**,不是 PM mid 泄漏。可留作信号,但若它=baseline 来源则排 |
| **市场微结构 flow (留)** | b_imbalance(10), b_ofi(30), b_ofi_10s(107), b_mp_roc_*(24/32/33/106), b_realized_vol(25/108), b_amihud(28), trade_flow(96-101), depth(86-93) | **留** | 行为信号,告诉模型「市场动没动/往哪动」=抢延迟,不是抄答案 |
| **spread/深度质量** | b_spread_bps(11), b_top3_depth_usdc(12), b_book_levels_valid(15), mkt_liquidity(95), mkt_volume_24h(94) | **留** | 流动性/成本上下文,影响 delta 可执行性,非泄漏 |
| **数据新鲜度 (核心,抢延迟)** | b_book_age_sec(75), score_age(77), x_joint_staleness(81), g_goal_freshness(73), 慢源 age(110-113) | **留** | 「数据多新」决定能否抢延迟,关键 |
| **持仓 (库存,非 fair 信号)** | pos_*(48-53) | **排** | 库存特征属于 sizing 不属于 fair-value;留着会让模型学到「我持仓 → fair 偏」的伪相关 |
| **类别上下文** | cat_asset_class(82), cat_sport(83), cat_market_type(84), cat_league(85) | **留** | 让单模型适配多盘口,categorical 声明已就位 |
| **fee/devig_ok 质量** | fee_rate_coef(54), devig_ok(55), ts_window_samples(56/57), resolution_status(59), time_to_resolution_frac(58) | **留** | 上下文/质量,非泄漏 |

### 2.3 sharp inplay fair #102/#18 是泄漏还是合法信号?(老板特别问的)

**分两个东西,别混:**

- **`g_bm_inplay_fair` (f18) = sharp 的 fair 水平本身** → **合法信号,不是泄漏**。它来自 **Goalserve bet365 in-play de-vig**,是**外部庄家**的估计,不是 Polymarket 市场价。庄家模型比散户强,所以它领先 PM 市场是 alpha 来源,不是抄 PM 的答案。
  - **但在 L1 residual 模式下,它是 baseline 锚 → 不能同时当 X**(否则 `delta = outcome − sharp` 用 sharp 自己预测 sharp 残差,信息冗余/泄漏 baseline)。
  - 在 L2 模式(锚 score-prior)下,它**应该进 X**——「sharp 说什么」是先验之外最有价值的额外信号。

- **`x_inplay_fair_minus_mid` (f102) = sharp − PM_mid** → **含 PM mid → 泄漏,排**。f103 同理(它的绝对值)。这俩把 PM 市场价偷渡进特征了,虽然语义是「分歧强度」,但 `mid` 在里面 → 模型能反解出 mid → 泄漏。
  - 如果想保留「sharp vs 市场分歧」这个语义,改成 `sharp − baseline`(不含 PM mid)或在决策层用,别进 X。

**一句话:** sharp 的**水平**是合法 alpha 信号(外部庄家);任何**含 PM mid 的派生**是泄漏。f18 合法(看模式),f102/f103 排。

---

## 3. 验证设计 (这是整个文档最重要的部分 — 老板最恨没验证就上)

### 3.1 为什么常规 train/test split 会骗你

体育数据有两个泄漏结构,标准随机 split 抓不到:
1. **同一场比赛的多个快照高度相关。** 一场比赛产生几百行 X,随机 split 会把同一场的行分到 train 和 test → test「泄漏」了同场信息 → 虚高指标。
2. **时序前视。** 用未来比赛训练、预测过去 → 现实不可复现。

**必须 by-game split + 时序 purged walk-forward,两个一起上。**

### 3.2 模型上线前必过的验证关卡 (7 关,fail-closed,任一不过 → 不许驱动真单)

> 这就是我 persona 边界里说的「ML 必经 walk-forward backtest」。下面每一关给**判据 + 数字门槛**,不是定性描述。

#### 关卡 1 — Purged Walk-Forward (防时序泄漏)

- 按时间切 N 个 fold(如周滚动)。fold k 训练用 `[0, t_k)`,测试用 `[t_k, t_{k+1})`。
- **Purge gap:** train 末尾与 test 开头之间挖空一段(≥ 一场比赛最大时长,如 4h),防同场跨界泄漏。
- **判据:** residual 模型在每个 out-of-sample fold 上 delta 的方向性命中率(`sign(delta) == sign(outcome − baseline)`)稳定 > 0.5,且**不随 fold 衰减到 0.5**(衰减=过拟合早期数据)。

#### 关卡 2 — By-Game Split (防同场泄漏)

- 训练/验证按 **condition_id 整组** 分(GroupKFold),一场比赛所有行同进 train 或同进 valid。
- **判据:** by-game CV 的 AUC/方向命中率,与 by-row 随机 split 的差距 **< 0.05**。差距大 = 模型在吃同场相关性(假信号)。

#### 关卡 3 — 残差信号显著性 (核心,证明 alpha 存在而非噪声)

- residual 模式独有。在 OOS 上检验:`delta` 与真实残差 `(outcome − baseline)` 的相关是否**统计显著为正**。
- **判据:**
  - OOS 上 `corr(delta_pred, outcome − baseline) > 0` 且 bootstrap 95% CI 下界 > 0(置换检验 p < 0.05)。
  - **降级模型对照:** 把 X 全置零(或 shuffle label)重训,delta 必须退化到无显著相关。如果 shuffle 后还「显著」→ 是泄漏不是 alpha。**这一步是泄漏的金标准探针。**
- **AUC 上界守卫 (沿用 §8):** residual 模式不直接看 AUC(连续 delta),但若把 `baseline + delta` 当 p_yes 算 AUC,**真实体育应落 0.55–0.72**。≥ 0.85 → 几乎必是泄漏,leak_suspect=true,conf=0。

#### 关卡 4 — CLV (Closing Line Value) 作 ground truth (老板问的「CLV 当 ground truth?」→ 是,而且是最硬的一关)

- **CLV = 模型在 t 时刻给的 fair vs 该 condition 临结算前的「收盘线」(最后稳定 mid)。**
- **为什么 CLV 比结算结果更好当 alpha 判据:** 结算结果是单场二值噪声大(一场冷门翻盘不代表模型错);CLV 衡量「模型有没有领先市场的最终共识」——长期 beat closing line 是 sharp 的黄金标准,样本效率远高于等结算。
- **判据:** 模型 fair 与 closing mid 的 **CLV 为正**(模型在便宜侧下注、收盘线朝模型方向移动的比例 > 50% 且净 CLV > 往返 BE)。这是「模型领先市场」的直接证据。
- **数据可得性:** short-horizon 报告已证明能从 fv.jsonl 重建每 condition 的 mid tape + closing mid。CLV 回测可直接在现有 322k 行快照上跑(离线 Python,跑完归档,符合 §12.4)。

#### 关卡 5 — Paper PnL 净 BE 后为正 (用 paper PnL 当最终裁判)

- 在 walk-forward 的每个 OOS fold 上,**用 BR-1 同一份 C++ 决策函数**(`fair_resolve.hpp` + sizing + RM)跑回测,扣**真实 BE**(往返 fee 0.03 + 真实 spread + slip_est)。
- **判据:** OOS 累计 paper PnL **扣 BE 后为正**,且 Sharpe > 0.5(MVP 门槛),最大回撤可控。**净 BE 后为负 = 没有可执行 alpha,不许上线**(short-horizon 报告已证明 near_half BE 6.45c 吃掉绝大多数移动,这一关会刷掉大部分纸面 alpha)。
- **必须用 C++ 回测引擎**(小蒋 #20),不许 Python 回测(红线:回测=实盘同 binary;CLAUDE.md §12.4)。

#### 关卡 6 — Drift / 校准监控 (上线后持续)

- **PSI / KS 漂移:** 监控 X 各列分布与训练分布的 PSI;> 0.25 告警,> 0.5 自动降级(conf=0,回 baseline)。
- **滚动校准:** 在线滚动 ECE,> 训练时 2 倍 → 降级重训。
- **delta 体检:** 监控在线 |delta| 分布;若突然普遍很大(如 > 0.3),= baseline 失效或泄漏复发 → fail-closed(已有 fair-sanity 门 0.45 兜底,但这里要更早告警)。

#### 关卡 7 — 影子运行 2 周 (advisory shadow)

- 模型 calibrated=true 但 **ml_fair_blend_weight 保持小**(如 0.1–0.2,不满驱动),或纯 advisory(只记录不下单),跑 2 周。
- **判据:** 影子期记录的「假如按模型下单」的 paper PnL > 0 且 CLV 为正,且无 drift 告警。**过了才逐步升 blend weight**(0.2 → 0.35 → ...,每档观察一周)。
- **绝不一上来 weight=1.0 满驱动**(§7 事故的直接放大器就是 weight=1.0)。

### 3.3 验证关卡总表 (上线检查清单)

| 关 | 名称 | 判据 (数字门槛) | 不过的后果 |
|---|---|---|---|
| 1 | Purged Walk-Forward | OOS 方向命中 > 0.5 且不衰减 | 时序过拟合,重设计 |
| 2 | By-Game Split | by-game vs by-row CV 差 < 0.05 | 同场泄漏,排相关特征 |
| 3 | 残差信号显著性 | OOS corr>0 且 p<0.05;shuffle 后退化 | 泄漏/无 alpha,不上线 |
| 3b | AUC 上界 | (base+delta) AUC ∈ 0.55–0.72;≥0.85→leak | conf=0,gated |
| 4 | CLV ground truth | 净 CLV > 往返 BE | 不领先市场,不上线 |
| 5 | Paper PnL 净 BE | OOS PnL>0,Sharpe>0.5 | 无可执行 alpha,不上线 |
| 6 | Drift/校准监控 | PSI<0.25,滚动 ECE<2×训练 | 自动降级回 baseline |
| 7 | 影子运行 2 周 | 影子 PnL>0 且 CLV>0,无 drift | 不升 blend weight |

---

## 4. C++ 配合 (residual 模式的最小改动)

### 4.1 现状阻塞点 (我读代码发现的真问题)

**`fair_value_model.cpp:176-179` 有一个对 residual 致命的 bug:**

```cpp
if (cnt == 1) {
    // 回归: 单值 = p_yes (或残差; 调用方按 model 语义 blend)。clamp 到 [0,1]。
    const double v = std::clamp(static_cast<double>(data[0]), 0.0, 1.0);
    p.probs[0] = v;
```

residual 模型输出 delta ∈ **[−1, +1]**(可负)。这里 `clamp(data[0], 0.0, 1.0)` 会把**所有负残差截成 0** → 模型只能往上修正,不能往下 → 一半信号死掉。**当前 cnt==1 路径只对 regress(p_yes∈[0,1])正确,对 residual 错。**

### 4.2 最小改动方案

**改动 1 — 模型携带 mode 标志 (sidecar 加字段,推荐):**

在 `<onnx>.meta.json` sidecar 加一个字段:
```json
{ ..., "output_mode": "residual" }   // 或 "p_yes" (默认,向后兼容)
```
`OnnxFairValueModel::LoadMeta` 读它,存 `output_mode_`。这是**纯加性变更**(struct 末尾加字段 + sidecar 加 key),按 CLAUDE.md §8.1 carve-out #5 走普通 PR review,不触发 R-4 全审计。

**改动 2 — predict 按 mode 解释 cnt==1 输出:**

```cpp
if (cnt == 1) {
    if (output_mode_ == OutputMode::Residual) {
        // 残差: 输出是 delta ∈ [-1,1], 不 clamp 到 [0,1]; 存进 probs[0] 当 delta 载体。
        // 调用方 (paper_loop) 知道这是 delta, 做 fair = baseline + delta。
        p.probs[0] = std::clamp(static_cast<double>(data[0]), -1.0, 1.0);
        p.normalized = false;  // delta 不是概率
        p.is_residual = true;  // 新增标志, 调用方据此解释
    } else {
        const double v = std::clamp(static_cast<double>(data[0]), 0.0, 1.0);  // 原 p_yes 路径不变
        p.probs[0] = v;
        if (outcome_count_ >= 2) p.probs[1] = 1.0 - v;
        p.normalized = (outcome_count_ == 2);
    }
}
```
`ModelPrediction` 加 `bool is_residual{false};`(append 到末尾,append-safe,注释已写「新增字段一律 append」)。

**改动 3 — paper_loop ML blend 接入点 (`paper_loop.cpp:800-812`) 按 mode 解释:**

```cpp
const auto mp = ml_model->predict(fv);
if (mp.ok && mp.calibrated && mp.confidence > 0.0) {
    if (mp.is_residual) {
        // residual: 模型输出 delta, fair = baseline + delta。baseline 由 ResolveFair 优先级定。
        // 不在这里直接算 fair, 而是把 delta 传给 ResolveFair (它知道 baseline 是 sharp 还是 prior)。
        ml_delta_opt = mp.prob(0);   // delta ∈ [-1,1]
    } else {
        const double ml_p = mp.prob(0);
        if (std::isfinite(ml_p) && ml_p > 0.0 && ml_p < 1.0) ml_p_opt = ml_p;
    }
}
```

**改动 4 — `fair_resolve.hpp` 加 residual 叠加路径 (最关键,一处定语义):**

`FairInputs` 加 `std::optional<double> ml_delta;`(append)。`ResolveFair` 第 4 步改:
```cpp
// 4. ML 叠加 (非 derivative)。两种 mode 二选一:
if (in.ml_delta.has_value() && in.ml_blend_weight > 0.0) {
    // residual: fair = baseline(已由 1-3 步算好的 p) + w * delta, 然后 clamp [0,1]。
    // 无残差信号 → delta≈0 → fair≈baseline (安全退化)。
    const double w = std::clamp(in.ml_blend_weight, 0.0, 1.0);
    p = std::clamp(p + w * (*in.ml_delta), 0.0, 1.0);
    // (delta 已是「在 baseline 之上的修正」, baseline 就是 p, 语义自洽)
} else if (in.ml_p_yes.has_value() && in.ml_blend_weight > 0.0) {
    // 原 p_yes blend 路径 (regress 模式), 保持不变
    const double ml = *in.ml_p_yes;
    if (std::isfinite(ml) && ml > 0.0 && ml < 1.0) {
        const double w = std::clamp(in.ml_blend_weight, 0.0, 1.0);
        p = (1.0 - w) * p + w * ml;
    }
}
```

**为什么 residual 叠加在 `p`(=baseline)上是对的:** ResolveFair 的 1-3 步已经把 baseline 算好放进 `p`(sharp 优先,无效落 score-prior)。residual 模型训练时的 baseline 必须**和这里的 `p` 同源**(都是 sharp/score-prior),才能 `fair = baseline + delta` 自洽。**这要求训练侧 `fair_value` 字段记的 baseline = 推理侧 ResolveFair 的 baseline**——需校验 `feature_vector_recorder` 落的 `baseline_fair` 确实是 ResolveFair 的 p(BR-1 一致性,落地前必查)。

**改动 5 — train_fair_value.py residual 分支修 baseline 锚:**

当前 `y.append(label − num(r.get("fair_value"), 0.5))` 用的是 jsonl 里的 `fair_value`。要确保:
- L1 训练:筛 `fair_value` 来源是 sharp 的行(或单独记 `baseline_src` 字段),`y = label − sharp_fair`。
- residual 模式下 **build_xy 必须把 market-derived 列从 X 置 NaN / drop**(§2.2 排的那批),否则泄漏照旧。建议 `build_xy` 加 `drop_market_features=True` for residual mode,显式按 index 列表清零市场列。
- residual 模式 sidecar 的 `confidence` 用 §3.3 关卡 3 的残差显著性(corr 的置换检验),不是 AUC(连续 delta 没有 AUC)。当前 residual 分支用 RMSE 兜 conf 太粗,应升级为「残差相关显著 → conf>0,否则 conf=0」。

### 4.3 改动清单 (按风险/仪式分级)

| 改动 | 文件 | 仪式 (CLAUDE.md §8.1) |
|---|---|---|
| sidecar 加 output_mode | train_fair_value.py + fair_value_model.cpp LoadMeta | 纯加性,普通 PR |
| ModelPrediction 加 is_residual | fair_value_model.hpp | append-safe,普通 PR |
| predict 按 mode 解释 cnt==1 | fair_value_model.cpp | 逻辑分支,普通 PR + 单测 |
| FairInputs 加 ml_delta + ResolveFair 叠加 | fair_resolve.hpp | 纯函数,**必加单测**,普通 PR |
| paper_loop 按 mode 传 delta/p | paper_loop.cpp | 接线,普通 PR |
| build_xy 排市场列 + residual conf | train_fair_value.py | 离线,普通 PR |

**都不碰真钱开闸**(LiveOrderGate 仍 disarmed),按 §8.1 carve-out #6 **不需会签**,走普通 PR review。但**关卡 5(paper PnL)+ 关卡 7(影子 2 周)是升 blend weight 的前置**,这部分要走策略评审(小梁)。

---

## 5. 诚实评估:Polymarket 体育散户市场,ML 真能找到 alpha 吗?

> 老板原话:「没有 bet365 sharp 覆盖的盘口,ML 模型真能找到 alpha 吗?还是 alpha 主要在事件延迟套利?给数据支撑的判断,别画饼。」
> 我直接给判断,带 `shorthorizon-locking-feasibility-v1.md` 的硬数据。

### 5.1 三个市场状态,三个结论

**状态 A — 有 bet365 sharp 覆盖 (白名单盘口):**
- **ML 能挤出薄 alpha,但主要价值是「抢 sharp→PM 的重定价延迟」而非「比 sharp 更准」。**
- 数据支撑:short-horizon 报告测得 **PM 重定价中位延迟 13.2s**,而 Goalserve(含 bet365 inplay)轮询 ~1s → **理论可抢窗口中位 ~12s**。sharp 先于 PM 市场知道正确 fair,ML 把 sharp 残差 + 微结构 + 新鲜度组合成「现在该往哪修正、市场还没反应过来」的信号。
- **但 ML 在这里的增量 alpha 有限**:sharp 本身已经很准,`g_bm_inplay_fair` 直接当 baseline(无需 ML)就已经领先 PM。ML 的 residual delta 是在 sharp 基础上的二阶修正——**能有正 CLV,但很薄**。诚实说:这个状态下,**sharp-anchor baseline 本身(现有 fair_resolve 第 2 步)已经是主要 alpha,ML 是边际增量。**

**状态 B — 无 sharp 覆盖,有实时比分 (大量 PM 体育盘):**
- **ML 几乎找不到散户没有的信息。** 实时比分是**公开**的,Polymarket 群众也看比分定价。光靠「领先=高胜率」这种 game-state 模型,赢不了市场(§2 ml-calibration 报告已下此结论)。
- 唯一活的 alpha = **事件延迟套利**:进球瞬间,Goalserve 比分流(~1s)比 PM 挂单(中位 13.2s)快 → 在事件后 5–10s 抢进。**这不需要「更聪明的 fair-value 模型」,需要的是「快 + 事件触发器 + 方向判断」**——这正好是候选方案 (c)(预测短期价格变动)的领域,但它是**独立的第二个模型/规则**,不是 fair-value 模型。
- **数据支撑(为什么 alpha 这么稀):** short-horizon 报告 H=60s 够本移动仅 **2.9%**,其中 **70%+ 是事件驱动**;near_half 价位 BE 高达 **6.45c**,吃掉绝大多数移动;只有极端价位(mid<0.15/>0.85)BE 仅 1.7c、>BE 比例 9.3% 才有空间。**结论:不是「持续预测 fair」能赚,而是「事件后窄窗口抢极端价位」能赚。**

**状态 C — 无 sharp、无实时比分 (outright/prop/series/pregame):**
- **ML 没有 alpha 来源。** 现状已正确处理:`market_implied` 兜底,fair=市场 de-vig,edge≈0 不交易。ML 不该碰这些盘口(`paper_loop.cpp:828-836` 已挡)。

### 5.2 最终判断 (一句话)

> **Polymarket 体育散户市场的 alpha,主力是「速度」不是「智力」。** 在有 sharp 覆盖的盘口,sharp-anchor baseline 本身就是 alpha,ML residual 是薄薄的边际增量;在无 sharp 覆盖的盘口,alpha 几乎全在「比市场快 ~12s 反应比分事件」的延迟套利上,而那是个**速度/触发问题,不是 fair-value 建模问题**。
>
> **所以 ML fair-value 模型的正确定位:** (1) 当 sharp 残差修正器(L1,薄 alpha,值得做但别期望高);(2) **真正的 v2 重点应该是「事件延迟套利」的触发 + 方向模型(方案 c),而不是更精细的 fair-value 预言机。** 把工程精力按这个优先级排,别在「无 sharp 盘口的 fair-value 模型」上投入太多——数据已经说了那里没有它的 alpha。

### 5.3 不画饼的预期数字

- L1 sharp-residual:CLV 可能微正,paper Sharpe **乐观 0.3–0.6**,扣 BE 后在多数 near_half 盘口**接近 0**,只在极端价位 + 事件后窗口为正。
- 事件延迟套利(方案 c):理论窗口存在,但**受限于跨洋链路延迟**(CLAUDE.md 部署环境:高延迟 + 带宽紧)——12s 窗口里我们的端到端延迟(伦敦节点已就近,但 PM 下单确认 + 我们的 5s 采集节拍)会吃掉一部分。**这是 v2 真正要量化的:我们的实际端到端延迟 vs 12s 窗口,够不够抢。** 这个数没测出来之前,事件延迟套利的可行性是开放的。

---

## 6. 实施步骤 (路线图,给量化/工程排期)

> 严格遵守我 persona 边界:**ML 必经 walk-forward backtest**。下面每步都把验证关卡前置。

**Phase 0 — 数据 + 一致性核验 (阻塞,必须先做):**
1. 核验 `feature_vector_recorder` 落的 `baseline_fair` = 推理侧 `ResolveFair` 的 baseline(BR-1)。不一致 → residual 数学不成立,先修。
2. 攒够干净样本(已结算 + sharp 覆盖的 condition);当前数据重采后从 0 积累,需等(§4 ml-calibration)。
3. 离线在 322k 历史快照上跑 CLV 回测脚手架(Python,跑完归档),确认 CLV ground truth 能算。

**Phase 1 — L1 sharp-residual 模型 (离线,不上线):**
4. `build_xy` 加 residual + drop_market_features;训练 L1(baseline=sharp,排市场列)。
5. 过验证关卡 1–4(walk-forward + by-game + 残差显著性 + CLV)。**任一不过 → 停,不进 Phase 2。**
6. residual conf 用残差显著性(置换检验)而非 RMSE。

**Phase 2 — C++ 接线 (改动 1–5,§4):**
7. sidecar output_mode + predict 按 mode + ResolveFair 残差叠加 + 单测。**不碰真钱开闸,普通 PR。**
8. 过验证关卡 5(C++ 回测 paper PnL 净 BE)。**净 BE 为负 → 停。**

**Phase 3 — 影子 + 灰度 (关卡 6–7):**
9. calibrated=true 但 blend weight 小 / advisory,影子运行 2 周 + drift 监控。
10. 影子 PnL>0 且 CLV>0 → 走策略评审(小梁)→ 逐档升 blend weight。

**并行探索(独立 backlog,不阻塞上面):**
11. 方案 (c) 事件延迟套利模型 + **实测我们的端到端延迟 vs 12s 窗口**(这是无 sharp 盘口 alpha 的真正所在,§5.2)。这条线可能比 fair-value residual 更值钱,建议量化(小梁/小蒋)优先评估。

---

## 7. 给量化研究部 (小梁) 的协作请求 + 我的边界

- **我(小邓)负责:** ML 模型架构、feature spec、ONNX 接线设计、drift 监控设计、验证关卡设计(本文)。
- **需小梁/小蒋定:** (1) C++ 回测引擎跑 walk-forward 的排期;(2) blend weight 灰度的 Sharpe/回撤门槛(关卡 5/7);(3) 方案 (c) 事件延迟套利是否提为 v2 主线(我的判断:它比 fair-value residual 更可能有真 alpha,但需量化拍板)。
- **需老韩(风控)知会:** 验证关卡是「上线前必过」的硬门,blend weight 升档前过策略评审。不涉真钱开闸,无需会签(§8.1)。
- **我不接的:** ETL(数据采集/落盘管道是小余/小冯的);v1 不引 ML(本文是 v2 候选研究,不改 v1 生产路径)。

---

## 附录 A — 一页纸结论 (贴墙版)

1. **范式错了不是 bug:** regress 预测结算 + 含市场价 = 必然复刻市场或退化,上限就是市场,没 alpha。
2. **改 residual:** `fair = baseline + delta`,baseline=sharp(L1)/score-prior(L2),模型学残差。无信号→delta≈0→安全退化。
3. **特征:排所有含 PM mid 的列(b_mid/microprice/dislocation/x_*_minus_mid/102/103);留 game-state + 微结构 flow + 新鲜度;sharp 水平 f18 是合法信号(L1 当 baseline 不进 X,L2 进 X)。**
4. **C++ 真 bug:** `fair_value_model.cpp:178` 把单值 clamp[0,1],会截断残差负值。residual 必须加 output_mode 标志 + ResolveFair 残差叠加路径。
5. **验证 7 关:** purged walk-forward + by-game + 残差显著性(shuffle 探针) + **CLV ground truth** + paper PnL 净 BE + drift + 影子 2 周。任一不过不上线。绝不一上来 weight=1.0。
6. **诚实判断:** alpha 主力是速度不是智力。有 sharp → sharp baseline 本身是 alpha,ML 是薄增量;无 sharp → alpha 全在事件延迟套利(比市场快 12s),那是速度/触发问题不是 fair-value 建模问题。**v2 真正该投的是事件延迟套利模型 + 实测端到端延迟,不是更精细的 fair-value 预言机。**
