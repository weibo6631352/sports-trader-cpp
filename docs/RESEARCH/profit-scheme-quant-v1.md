# 盈利方案 — 价格走势预测 + 双边仓位管理套利的量化建模规范 v1

> **owner:** 小袁 (quant-microstructure, 量化研究部 #C)
> **last_review:** 2026-06-03
> **召唤背景:** 老板 2026-06-03 战略转向 —— 弃结算 outcome 标签, 改为"预测未来 Δt 价格走势 → 单市场 YES/NO 双边仓位管理 → 套利"。已证实 alpha: in-play PM 散户市场滞后 bet365 sharp 共识约 12s。
> **本报告范围:** 预测目标定义 / 监督信号构造 / 价格预测模型设计 / 套利数学 / 现有资源盘点 / 防过拟合纪律。**金融风险口径(λ/cap/bankroll)归老韩, 协议机制(min size/fee/neg-risk)归 polymarket-protocol-expert; 本报告标注待确认处。**

---

## TL;DR — 五条结论

1. **预测量应是【in-play sharp 收敛缺口的实现幅度】, 不是裸 Δmid。** 裸 Δmid 在 5–60s 内中位为 0, p90 仅 0.5c(shorthorizon-locking §B), 不可预测; 但 mid 向 sharp 共识收敛的【方向 + 幅度】有结构(reprice 中位 13.2s, 我们感知延迟 3.1s, 留 ~10s 窗口)。**主标签 = 符号化的收敛实现量 `y_h = sign(gap)·(mid(t+Δ) − mid(t))`, 其中 gap = inplay_sharp_fair − mid(t)。** 配 4 个辅助标签(双边 fill 概率 / 短期 σ / 到 sharp 距离 / 到达时间)。

2. **horizon 不是越短越好。** 我们端到端延迟中位 3.1s, PM reprice p25=8.5s/中位 13.2s。**唯一对套利有用的 horizon 落在 [我们延迟, PM reprice 完成] = [~3s, ~15s] 的交集 ⇒ 主 horizon Δ = {5s, 10s, 12s, 15s}**, 短于 5s 我们抢不到(数据 5s 采样盲区 + 延迟), 长于 15s 收敛已基本完成、edge 被吃光。已有 `kWallHorizonsSec={2,3,5,10,12,15,30,60}` 全采, 但训练/上线只用中段 4 个。

3. **强基线立即可上: 线性/GBDT 短期收敛回归, X = {sharp_gap, microprice−mid, OFI, trade_signed_vol, realized_vol, time_to_reprice 代理}。** 序列模型(小 Transformer / GRU)是第二步, 在基线 OOS IC 站稳后再上, 不要一上来就深度模型(296 市场样本量撑不住)。

4. **套利的正期望条件是: `保守收敛幅度(CI 下界) − 往返成本 > 0`。** 这套数学 `arb_signal.hpp` 已经写对了(`net_edge = cons_move − BE`, `BE = fee_roundtrip + slip + spread`)。本报告补全 microprice/fill-rate/slippage 如何精确进 BE, 以及"双边仓位管理"相对"单边 taker"的两个增益(用 NO 边对冲 + cross-book 锁定)。

5. **296 市场 / 20.5h 是极小样本, 过拟合是头号死因。** 强制: ① 按 condition group 切 CV(PurgedKFold 已实现); ② walk-forward 时间序滚动(WalkForwardSplitter 已实现); ③ 标签 PIT 用 `mid_return_label.hpp` 的"无未来覆盖→NaN 禁外推"(已实现); ④ 特征数 ≤ 样本量约束下砍到 8–15 个; ⑤ OOS rank-IC(walk_forward_ic 已实现)做唯一上线门, 不看 IS。

---

## 1. 预测目标的精确定义 (关键)

### 1.1 为什么不能直接预测 Δmid

老板的措辞是"预测未来一段时间的价格"。但实证(shorthorizon-locking §B1)说得很清楚:

| H | 中位 \|Δmid\| | p90 \|Δmid\| | >BE 比例 |
|---|---|---|---|
| 5s | 0.000c | 0.500c | 0.4% |
| 15s | 0.000c | 0.000c | 0.8% |
| 60s | 0.000c | 0.500c | 2.9% |

裸 Δmid 的条件分布在绝大多数 tick 上是退化的(中位恒 0): 价格大部分时间不动。直接回归 Δp 会被海量 0 标签淹没, 模型退化成"预测 0"。**这不是有用的预测量。**

但同一份数据(§C)告诉我们: 够本移动里 **70%+ 发生在比分事件 10s 内**, 且 **PM reprice 中位滞后 sharp 共识 13.2s**。这说明 **价格移动不是随机游走 —— 它是朝着 sharp 共识收敛的、有方向、有触发器的过程**。可预测的是【收敛】, 不是【裸价格】。

### 1.2 主预测量 — 符号化 sharp 收敛实现量

**定义(per token, per horizon h):**

```
gap(t)   = inplay_sharp_fair(t) − mid(t)          # sharp 共识相对当前市场的缺口 (signed)
y_h(t)   = sign(gap(t)) · ( mid(t+Δ_h) − mid(t) )  # 朝缺口方向的 mid 实现移动 (signed)
```

- `inplay_sharp_fair` = `g_bm_inplay_fair`(MlFeature #18, bet365 inplay de-vig YES-canonical fair, 已在特征向量里)。
- `y_h > 0` ⇒ 市场朝 sharp 收敛(我们顺势进场能赚);`y_h < 0` ⇒ 市场背离 sharp(假信号 / sharp 自己错 / 数据陈旧)。
- 这是 **回归标签**(连续, 单位 = prob, 即美元价)。它把"预测价格"问题转化成"预测向已知锚点收敛多少", 锚点(sharp)我们当下就知道, 模型只需学"这次会收敛多少 + 会不会反向"。

**为什么这个标签好:**
1. 它的条件分布**不退化** —— 当 |gap| 大且新鲜(刚有事件)时 y_h 显著为正, 模型有信号可学。
2. 它**直接对应套利动作**: 进场买 mid 偏离 sharp 的那一边, 等收敛, 平仓。
3. 它**自监督 / 时序回归, 不用结算标签** —— label 完全来自未来的 mid 自身, 符合老板"弃结算标签"。

### 1.3 辅助预测量 (多任务头, 共享 encoder)

主标签给方向+幅度, 但套利决策还需要三件事, 各配一个标签(同一模型多输出头, 或并列小模型):

| 标签 | 定义 | 用途 | 已有载体 |
|---|---|---|---|
| **σ_h (短期波动)** | 窗口 [t, t+Δ_h] 内 \|Δmid\| 的 RMS(或用 ci_high−ci_low 反推) | Kelly 分母(`arb_sizing` 的 `sigma_dmid`)+ 风险门 | `RealizedVol()` 已实现, 标签侧前向算 |
| **p_fill_yes / p_fill_no (双边 fill 概率)** | 在 best_ask(买入)/best_bid(平仓)挂单, 未来 Δ_h 内成交的概率 | 决策"挂单还是吃单 + 能不能出得来" | `bid_absence_frac` / `exit_depth_mean` / `fill_rate_model` 是输入; 标签需新建(见 §1.5) |
| **τ_reprice (收敛到达时间)** | mid 首次移动 ≥ k·tick 朝 gap 方向所需秒数 | 选 horizon + 判 RJ-ARB-5 stale | reprice delay 分析已有(§C2 中位 13.2s), 标签需新建 |

**优先级: 主标签 y_h + σ_h 先上(套利数学只强依赖这俩); p_fill / τ 第二批。**

### 1.4 horizon Δt 的选取依据 (结合 12s 滞后窗口)

这是微观结构的核心判断, 直接定生死:

```
我们感知 + 下单端到端延迟 (eventdelay-arb-feasibility §1): 中位 3.1s, 最差 ~4.5s
PM reprice 完成时刻 (shorthorizon §C2):                  p25 8.5s, 中位 13.2s, p75 20.5s
```

**可用窗口 = [我们进场时刻, PM 收敛完成时刻]。** 进场太早(< 3s)我们抢不到 + 5s 数据盲区看不见; 进场点固定在 ~3–5s 后, 那么 horizon 的有效范围是"从进场到收敛完成":

| Δ_h | 是否可用 | 理由 |
|---|---|---|
| 2s, 3s | **不用于上线** | < 端到端延迟, 抢不到; 且 5s 采样盲区下标签本身不可测 |
| **5s, 10s, 12s, 15s** | **主用** | 落在 [3s 延迟, 13–20s 收敛完成] 交集; 12s ≈ 中位滞后窗 |
| 30s, 60s | 仅训练采、不上线 | 收敛已完成, edge 被吃光; 持仓越久越吃 PM reprice 后的不利价(无事件持仓负期望) |

**结论: 训练侧把 8 个 wall horizon 全采(已实现), 但模型上线只在 {5,10,12,15}s 这 4 个 horizon 出可操作信号。** `arb_signal.hpp` 遍历全 horizon 选最优 —— 上线时应限制 `h ∈ {2,3,4,5}` 索引(对应 5/10/12/15s), 屏蔽 30/60s。这是一行 gate, 派给实现方加。

事件钟 horizon(`kEventHorizons={1,2,5,10,20,50}`)是有价值的对照组 —— 老板"触发事件驱动不规律, 该按事件不按秒"的直觉对路。**建议: 主用墙钟训练/上线(延迟门是墙钟的), 事件钟做 OOS IC 对照, 看哪个预测力强**(walk_forward_ic 直接能跑两套对比)。

### 1.5 监督信号怎么从 tick 序列构造 (不用结算标签)

**核心机制已实现 in `mid_return_label.hpp`**, 只需把 label 从"裸 Δmid"换成"符号化收敛量", 并加辅助标签:

```
对每个 condition 的 tick tape (按 as_of_ts 升序, 去重同 ts):
  对 tape 中样本 i (时刻 t = tape[i].ts):
    1. 主标签 y_h:
       gap = sharp_fair(t) − mid(t)              # sharp_fair = f18 = g_bm_inplay_fair
       对每个 Δ_h:
         target = t + Δ_h
         if target > tape.back().ts: y_h = NaN    # 无未来覆盖 → 禁外推 (PIT 命门, 已实现)
         else: mid_fut = prevailing(target)        # 最后一个 ts≤target 的真实观测, 不插值 (已实现)
               y_h = sign(gap) · (mid_fut − mid(t))
    2. σ_h: 窗口 [t, target] 内相邻 |Δmid| 的 RMS (前向, 同样 PIT 截断)
    3. p_fill_*: 窗口内 best_bid/best_ask 是否触达挂单价 (前向; 需 tape 带双边 L1)
    4. τ_reprice: 首个 |mid_fut − mid(t)| ≥ k·tick 朝 sign(gap) 的 Δt (前向)
  label_valid = (最短上线 horizon 5s 有未来覆盖)
```

**关键 PIT 纪律(已在 `ComputeMidReturnLabel` 落实, 必须保留):**
- 标签是 offline, **绝不作特征 / 不回喂实时决策**(`mid_return_label` 头注释红线)。
- `gap` 用的 `sharp_fair(t)` 和 `mid(t)` 都是 t 时刻**已观测**值 —— 它们是特征不是标签, 进 X 合法。**标签里只有"未来 mid"是 t 之后的, 严禁进 X。**
- horizon 超出 tape 尾部 → NaN, 禁外推。这是"回测金光实盘亏穿"的命门。
- prevailing(target) 用 t 之后最后一个 ts≤target 的真实观测, **不插值**(插值=偷看未来)。

**改动量小**: `mid_return_label.hpp` 的 `ComputeMidReturnLabel` 已经做了 95% 的事(PIT 截断 + prevailing + 双时钟)。要加的是: ① 读 sharp_fair 列(f18)算 gap 并符号化; ② 加 σ_h / p_fill / τ 三个辅助标签函数。这是标签管道扩展, 不是新建, 派给实现方(老雷/小邓)。

---

## 2. 价格预测模型设计

### 2.1 强基线 (立即可上, 不用深度学习)

**模型: 每个上线 horizon 一个 GBDT 回归(LightGBM), 预测 y_h。** 用现成 Python 训练栈(§12.4 允许离线), 导 ONNX, C++ 推理(`seq_arb_model.cpp` 的 ONNX 封装已就位)。

**为什么 GBDT 先行:**
- 296 市场 / 841 tick = ~25 万样本, 但**有效样本(label_valid + 有 sharp + 双边报价)远少**, 估计落在 3–8 万。这个量级 GBDT >> 深度模型, 后者会过拟合。
- GBDT 自带特征重要性 → 直接验证"哪些微结构信号真有预测力", 是序列模型的特征筛选前置。
- categorical 原生支持(`cat_sport`/`cat_league`/`cat_market_type` 已是 LightGBM categorical_feature)。

**基线特征集 (砍到精, 8–15 个, 防过拟合):**

| 类 | 特征 | enum / 列 | 物理意义 |
|---|---|---|---|
| sharp 缺口 | `x_inplay_fair_minus_mid` | #102 | **头号特征**: gap 本身, 收敛幅度的最强先验 |
| sharp 缺口 | `x_inplay_market_absdev` | #103 | \|gap\|, 分歧强度 |
| 订单流 | `b_ofi_10s` | #107 | 10s OFI, 短期方向压力(收敛前兆) |
| 订单流 | `b_trade_signed_vol_5m` | #96 | 成交净流, taker 方向 |
| microprice | `b_dislocation` (= micro−mid) | #64 | 即时买卖压力方向 |
| 动量 | `b_mp_roc_5s` | #106 | 5s 微价动量(已在收敛?) |
| 动量 | `b_mp_accel` | #109 | 动量加速度 |
| 波动 | `b_realized_vol_10s` | #108 | 短期 σ, 噪声水平(σ 标签的特征侧) |
| 流动性 | `mkt_liquidity_usdc` / `exit_depth_mean` | #95 / #27 | 进得去出得来 |
| 事件新鲜度 | `g_goal_freshness` | #73 | exp(−Δt/120s), 触发器有多新 |
| 数据质量 | `b_book_age_sec` / `x_joint_staleness_sec` | #75 / #81 | 数据陈旧 → gap 不可信 |
| 价位上下文 | `b_mid`, `x_pin_risk` | #8 / #62 | 极端价位甜区(BE 低) |
| 类别 | `cat_sport`, `cat_market_type` | #83 / #84 | 分盘口适配 |

**Loss: Huber / quantile。**
- 主预测用 **Huber loss**(对 y_h 的厚尾 robust; MSE 会被罕见大移动主导)。
- **额外训练 quantile head(τ=0.1 和 τ=0.9)直接产 CI 下界/上界** —— 这是 `arb_signal` 需要的 `ci_low`/`ci_high`(它现在靠 stub)。quantile GBDT 是最干净的 CI 来源, 不用 conformal。

**输出契约(对齐 `ArbHorizonPred`):** 每 horizon 输出 `{dmid=点估计(中位 head), ci_low=q10, ci_high=q90, confidence}`。confidence 用 OOS 该 horizon 的 rank-IC 映射到 [0,1](sidecar `.meta.json`, 对齐 model-calibration 现有机制)。

### 2.2 序列模型 (第二步, 基线站稳后)

基线把"哪些特征有用 + OOS IC 有多少"摸清后, 再上序列模型吃 tick 轨迹的时间结构(OFI 累积、动量拐点、reprice 半程信号), 这些截面特征丢了顺序信息。

**架构: 轻量 GRU 或 1D-causal-conv + 小 Transformer encoder。**
- 输入: 每 token 最近 L 个 tick 的特征序列(L=16–32, ring 容量 128 够)。`FeatureHistory` 的 ring 正好是序列源, 不用新存储。
- **causal masking 强制** —— 只能看 ≤ t 的 tick, 这是 PIT 在序列模型里的体现, attention mask 必须下三角。这是序列模型最容易引入未来函数的点, 务必单测验证。
- 输出头同基线: per-horizon {median, q10, q90}, 多任务共享 encoder(主 y_h + σ_h + p_fill)。
- 训练: M5 Apple Silicon + torch MPS(已装), 导 ONNX, 服务器 onnxruntime C++ 推理(待装, CLAUDE.md §13 标注)。

**规模纪律**: encoder 维度 ≤ 64, 层数 ≤ 2, 总参数 < 50k。296 市场撑不起大模型。**先证明序列模型 OOS IC > GBDT 基线再加复杂度**, 否则停在 GBDT。

### 2.3 训练 / 验证 (防泄漏 + 防未来函数)

| 维度 | 方法 | 现成实现 |
|---|---|---|
| **group 防泄漏** | 按 `condition_id` 分组, 同一 condition 所有 tick 只进同一 fold(同场比赛 tick 高度自相关, 跨 fold = 泄漏) | `PurgedKFold`(walk_forward.hpp, 按 game_id 分组 + embargo) |
| **时间序防未来** | walk-forward 滚动 IS→OOS, OOS 严禁参与调参(编译期 `DataAccessGuard`) | `WalkForwardSplitter` + `DataAccessGuard<Phase>` |
| **embargo** | fold 边界两侧 embargo(20.5h 跨度下 embargo 取 1–2 场比赛时长 ~3.5h, 不是默认 7 天 —— 数据太短) | `PurgedKFold(embargo_ns)` 可配, **默认 7 天对我们数据太大, 需调** |
| **上线门** | OOS per-horizon rank-IC(预测 vs 实现 y_h)+ IC-IR(跨折稳定) | `evaluate_walk_forward_ic`(把 label 从结算换成 y_h) |

**`walk_forward_ic` 当前 `label` 字段写的是"结算 outcome"** —— 新论点下把 `LabeledSample.label` 喂 `y_h`(未来收敛量)即可, 函数本身不用改。这是把现有 IC 评估器直接复用到新标签的关键。

---

## 3. 套利数学

### 3.1 单边 taker 套利的正期望条件 (arb_signal 已实现, 此处给数学闭环)

记进场买入价 `p_in`(多头吃 ask, 空头吃 bid), 预测保守收敛幅度 `m̂_cons`(CI 下界方向), 往返成本 `BE`。

```
多头 (gap>0, 预测 mid 上行):
  E[净利/share] = m̂_cons − BE
  m̂_cons = ci_low_h           # CI 下界, 保守 (点估计禁直接 sizing — 微利高频必死)
  BE = fee_roundtrip + slip_in + slip_out + spread_cross
  进场条件: ci_low_h − BE > 0  ⟺  net_edge > 0   ← arb_signal.hpp:77 已写对
```

**BE 各项的精确口径(微观结构补全, 这是我的领域):**

```
fee_roundtrip = 2 · fee_coef · p · (1−p)        # fee_coef=0.03 体育 (gamma feeSchedule), polymarket-protocol-expert 确认费率适用
spread_cross  = best_ask − best_bid             # 进场吃 ask 出场吃 bid 的全点差 (taker 双吃)
slip_in       = SlippageModel(size, ask, depth_ask) 的 (fill_px − ask)   # 进场冲击
slip_out      = SlippageModel(size, bid, depth_bid) 的 (bid − fill_px)   # 平仓冲击
```

slippage 用现成 `slippage_model.hpp`(Linear 模式, KAPPA_DEPTH_GAMEDAY=1.0 已校准)。**关键: BE 必须含双向 slippage**, 现 `arb_signal` 的 `slip_est` 是单一标量, 应拆成进/出两段(平仓侧深度通常更薄 = `exit_depth_mean`, 出场滑点 > 进场)。这是 BE 被低估的隐患, 派实现方拆。

**进场用 microprice 而非 mid 做基准:** mid 是 (bid+ask)/2, 对称假设; microprice(size 加权)反映即时压力方向, 是更准的"现在真实成交中枢"。**建议: gap 用 `sharp_fair − microprice` 而非 `sharp_fair − mid`** —— 当簿口失衡时 microprice 已部分反映收敛, 用 mid 会高估剩余 edge。这是一个特征侧改进(`b_dislocation` 已是 micro−mid, 模型能间接学到, 但显式更稳)。

### 3.2 出场 / 收敛条件

```
目标平仓价 target_exit = p_in + m̂_cons (多头) / p_in − m̂_cons (空头)   ← arb_signal.hpp:123 已写
出场触发 (任一):
  (a) 收敛达标: mid 触达 target_exit → 限价单平仓 (赚足预测幅度)
  (b) horizon 到期: t > entry + Δ_h → 强平 (OpenLegLedger.SweepExpired, 已实现)
       理由: 超过 horizon 后预测失效, 裸方向暴露违背套利初衷 (arb_risk 红线)
  (c) sharp 反转: gap 翻号 (sharp 自己更新, 我们方向错了) → 立即平
  (d) 数据断流: book/sharp feed 失效 → AllOpen 全平 (已实现)
```

**这里"双边仓位管理"相对单边 taker 的两个增益(老板论点的核心, 现 arb_signal 还是单边):**

**增益 1 — NO 边对冲降低裸方向风险。** PM 单市场 YES/NO 是互补 token(p_yes + p_no ≈ 1)。多头 YES 收敛预测时, 可同时观测 NO 边: 若 NO 边 best_bid 也在朝相反方向动(确认收敛), 信号更强; 若 NO 边背离, 是数据噪声警告。**双边 microprice 一致性(`b_no_microprice`/`b_cross_spread` 已是特征)进决策**: cross_spread = yes_ask + no_ask − 1 = vig, vig 越小双边定价越健康、收敛越可信。

**增益 2 — cross-book 无风险锁定(罕见但纯利)。** 当 `yes_bid + no_bid > 1` 时, 同时卖 YES + 卖 NO 锁定 `(yes_bid + no_bid − 1)` 无风险利润(结算时必有一边赔 $1, 但收了 > $1)。反向: `yes_ask + no_ask < 1` 时买双边锁定。**`x_arb_free_edge`(#105)= max(1−(yes_ask+no_ask), (yes_bid+no_bid)−1, 0) 已是特征**, 这是确定性套利(不需预测), 一旦 > 成本就无脑做。**这是双边管理的"白送"分支, 优先级最高、风险最低, 应独立于预测模型直接 gate 执行。**需 polymarket-protocol-expert 确认: ① YES/NO 是否真同一 condition 的互补 token; ② neg-risk 市场的双边定价约束。

### 3.3 净敞口管理

```
per-condition 净敞口 net_qty = pos_yes_qty − pos_no_qty   (pos_net_qty #52 已是特征)
管理目标:
  - 方向性套利期间: |net_qty| ≤ horizon_cap (RJ-ARB-4 已 enforce)
  - 收敛达标后回平到 net≈0 (锁利退出, 不留隔夜方向暴露)
  - cross-book 锁定: 故意建 yes+no 双多/双空 (net 可大, 但已结算对冲, 无方向风险)
```

库存特征(`pos_*` #48-53)已全进模型 → 模型能学"已有 YES 仓时再加 YES 的边际"(避免过度集中)。

### 3.4 fill-rate / slippage / microprice 如何进决策 (汇总, 我的领域归口)

| 量 | 进决策的位置 | 现成 |
|---|---|---|
| **microprice** | gap 基准(§3.1)+ 即时压力方向特征 | `compute_l1_probe`, capped ±2tick |
| **fill-rate** | ① p_fill 标签预测能否成交; ② `exit_depth_mean` 进 RJ-ARB-2(出不来则拒) | `fill_rate_model`, `FeatureHistory.ExitDepthMean/BidAbsenceFrac` |
| **slippage** | BE 的 slip_in + slip_out 两段(§3.1)+ 风控修正 sizing | `slippage_model` Linear |
| **sizing** | 连续 Kelly `f* = λ·edge/σ²`, σ 用 quantile 头反推 | `arb_sizing.hpp` 已实现 |

**slippage 模型给风控修正 sizing(我的边界): σ_dmid 应含 slippage 不确定性, 不只是价格波动。** 当前 `arb_sizing.sigma_dmid` 只来自模型 CI; 建议在深度薄时(`exit_depth_mean` 低)放大 σ_dmid(滑点方差大)→ Kelly 自动缩仓。这是 slippage→sizing 的修正项, 我出公式, 风控(老韩)定系数。

---

## 4. 现有数据 / 量化资源盘点

### 4.1 直接可用 (绿灯, 不用新建)

| 资源 | 文件 | 用途 | 状态 |
|---|---|---|---|
| **tick 采集** | `include/stcpp/ml/feature_recorder.hpp` | 5s 轮询落 quotes.jsonl, 全 116 列 QuoteFeatures | 已跑, 296 市场/841 tick |
| **订单簿微结构** | `include/stcpp/microstructure/orderbook.hpp` | microprice(capped)/imbalance/depth_within_ticks | 我写的, 单测齐 |
| **时序派生环** | `include/stcpp/ml/feature_history.hpp` | OFI/Amihud/RealizedVol/ROC/ExitDepth, PIT-safe ring(128) | 已实现, 序列模型输入源 |
| **slippage** | `include/stcpp/numerical/slippage_model.hpp` | Linear VWAP + fill_rate + fail-closed | 已实现, BE 进/出两段都靠它 |
| **标签管道** | `include/stcpp/ml/mid_return_label.hpp` | 双时钟多窗 mid 收益标签, PIT 截断/prevailing/禁外推 | 已实现, 改 label 公式即用 |
| **套利信号核** | `include/stcpp/risk/arb_signal.hpp` | net_edge = cons_move − BE, 遍历 horizon 选最优 | 已实现, 数学对 |
| **套利 sizing** | `include/stcpp/risk/arb_sizing.hpp` | 连续 Kelly f*=λ·edge/σ² + 深度封顶 | 已实现 |
| **套利风控门** | `include/stcpp/risk/arb_risk.hpp` | RJ-ARB-1..5 + OpenLegLedger 强平 | 已实现 |
| **edge CI 门** | `include/stcpp/strategy/edge_ci.hpp` | 源感知 edge 下界(sharp 纯 net-EV / 统计源二项 CI) | 已实现 |
| **walk-forward** | `include/stcpp/backtest/walk_forward.hpp` | Splitter + DataAccessGuard + PurgedKFold | 已实现 |
| **OOS IC 评估** | `include/stcpp/backtest/walk_forward_ic.hpp` | per-fold rank-IC + IC-IR | 已实现, label 换 y_h 即用 |
| **seq 模型接口** | `include/stcpp/ml/seq_arb_model.hpp` | per-horizon {dmid,ci_low,ci_high,conf} + ONNX 工厂 + Stub | 接口已定, 待真模型 |

**这套 Phase 1-5 套利引擎骨架几乎就是老板新论点的完整脚手架 —— 它本来就是为"预测 Δmid → 套利"建的, 不是为结算预测。新论点只是把预测量从裸 Δmid 精化成符号化收敛量, 骨架不动。**

### 4.2 要新建 / 要改 (黄灯)

| 缺口 | 工作 | 派给 | 优先级 |
|---|---|---|---|
| **标签公式精化** | `mid_return_label` 加 sharp_gap 符号化(读 f18)+ σ_h/p_fill/τ 三辅助标签 | 老雷/小邓 | P0 |
| **真 ONNX seq 模型** | LightGBM quantile 基线训练 → ONNX → `seq_arb_model.cpp` 接真模型(现 stub) | 小邓(训练)+ 实现方 | P0 |
| **horizon 上线 gate** | `arb_signal` 限 h∈{5,10,12,15}s, 屏蔽 2/3/30/60s | 实现方 | P0(一行) |
| **BE 双向 slippage** | `arb_signal.slip_est` 拆 slip_in/slip_out(出场用 exit_depth) | 小肖(slippage)+ 实现方 | P1 |
| **cross-book 锁定分支** | `x_arb_free_edge>成本` 时无脑双边锁定, 独立于预测模型 | 实现方 + polymarket-protocol-expert 确认机制 | P1 |
| **序列数据管道** | per-token tick 序列窗 → 训练 tensor(causal); torch MPS 训练脚本 | 小邓 | P2(基线站稳后) |
| **新标签回测闭环** | `event_replayer` 当前事件不带 sharp 源 → 让 replay 事件携带 fair 源(edge_ci 头注释已标这个 follow-up) | 小蒋 | P1 |
| **embargo 调参** | PurgedKFold 默认 7 天 embargo 对 20.5h 数据过大, 改 ~3.5h(1 场时长) | 小蒋 | P1 |

### 4.3 数据规模现实 (老板要数字说话)

```
296 市场 × ~841 tick ≈ 25 万行原始
× label_valid (最短 horizon 有未来覆盖, ~95%)
× 有双边报价 (shorthorizon §A: 54.5%)
× 有 sharp_fair (生产白名单内, 开发态多为 NaN — 这是最大约束!)
⇒ 真正可训练样本估计 3–8 万, 且高度集中在少数活跃 condition
```

**最大数据风险: sharp_fair(g_bm_inplay_fair)覆盖率。** 整个论点建立在 sharp 锚上, 但 inplay 赔率只在 Goalserve 白名单 + 赛季内有(MEMORY: inplay-Goalserve-has-odds)。**没有 sharp_fair 的 tick 上, 主标签 y_h 无法符号化 → 这些样本退化回不可预测的裸 Δmid。** 上线前必须实测 sharp_fair 在生产环境的真实覆盖率, 这是可行性的硬前提。这一条派给数据组(小余/小冯)和我联合核 —— 标"需实测确认"。

---

## 5. 防过拟合 / 防未来函数纪律

### 5.1 样本量约束下的硬纪律

296 市场 / 20.5h 是**极小样本**, 且 condition 内 tick 高度自相关(有效独立样本远少于行数 —— 一场比赛的 841 个 tick 不是 841 个独立观测, 可能等效 5–20 个)。过拟合是头号死因。

| 纪律 | 规则 | 强制点 |
|---|---|---|
| **特征数上限** | GBDT ≤ 15 特征, 序列模型参数 < 50k。有效独立样本 ~数百场, 特征多必过拟合 | 训练侧 review |
| **group CV** | 切分单位是 condition, 永不是 tick(同场跨 fold = 泄漏) | `PurgedKFold` 已强制 |
| **OOS-only 上线门** | 只看 OOS rank-IC + IC-IR, IS 指标一律不作上线依据 | `DataAccessGuard<OutOfSample>` 编译期挡调参 |
| **IC 门槛** | per-horizon OOS rank-IC > 0.03 且 IC-IR > 0.5(跨折稳定)才上该 horizon | walk_forward_ic 输出 |
| **无 sharp 不预测** | sharp_fair=NaN 的 tick 不产可操作信号(退化回噪声, 不赌) | arb_signal gate |
| **stub 永不驱动** | 真 ONNX 上线前套利分支不发任何单(已实现 StubSeqArbModel ok=false) | 已实现 |

### 5.2 PIT (point-in-time) 正确性 — 命门清单

**这是"回测金光、实盘亏穿"的唯一根源, 逐条核:**

1. **标签禁外推**: horizon 超 tape 尾部 → NaN(`mid_return_label` 已实现)。
2. **prevailing 不插值**: 未来 mid 取最后一个 ts≤target 的真实观测(已实现)。
3. **标签不进 X**: 未来 mid 只在 y, 绝不当特征。gap 用的 sharp/mid 是 t 时刻已观测值, 进 X 合法。
4. **序列模型 causal mask**: attention/conv 只看 ≤t 的 tick, 下三角 mask, **单测验证**(序列模型最易漏的未来函数)。
5. **4ts 单调链**: event ≤ data_source ≤ ingestion ≤ as_of, ring 用 data_source_ts 锚(`ts_order_ok` / `FeatureHistory.Push` 单调门已实现)。
6. **回测=实盘同 binary**: BR-1, replay 与 paper 共用 feature pipeline + slippage(已是架构红线)。
7. **特征用 as_of 锚, 禁本地 now()**: R-20 已贯穿。

### 5.3 walk-forward 强制流程

```
全量样本 (按 as_of_ts 排序)
  → WalkForwardSplitter 滚动切 IS/embargo/OOS (embargo 改 ~3.5h)
  → 每 fold: IS 上 PurgedKFold (按 condition group) inner-CV 调超参 → freeze_params
  → freeze 后 OOS 评估 (DataAccessGuard<OutOfSample> 编译期禁再调参)
  → per-horizon rank-IC + IC-IR 聚合
  → 唯一上线门: OOS IC > 0.03 且 IC-IR > 0.5
```

20.5h 数据下 walk-forward fold 数有限(可能只 2–3 折), **IC-IR 跨折稳定性比单折 IC 绝对值更重要** —— 一折偶然高没用, 要多折都正。**强烈建议: 上线前再积累至少 1–2 周数据(feature_recorder 持续在跑), 把 fold 数提到 ≥ 5。** 当前 20.5h 只够做可行性验证, 不够做上线决策。这是数据驱动的诚实判断。

### 5.4 与停摆历史的关系 (诚实交代)

MEMORY 记录 SeqArb 曾停摆, shorthorizon-locking 结论是"锁利意图死, 只有事件延迟套利活"。本方案与之**不矛盾, 是它的正确版本**:

- **死的是**: "连续预测 + 随时进场 + 持有等噪声移动"(裸 Δmid 预测, >BE 仅 0.4–4.4%)。
- **活的是**: "事件触发 + sharp 锚收敛预测 + 短窗进出"(本方案), 这正是 shorthorizon §"可行路径"和我 eventdelay-feasibility §5 指向的版本。

**本方案的预测量(符号化 sharp 收敛)= 把"事件延迟套利"从纯规则(score_to_direction)升级成模型量化收敛幅度 + CI**, 让 sizing 有 σ 可用、让进出有阈值依据。规则版是 v1, 模型版是 v2, 不是推翻。

---

## 6. 给设计会的三个待决问题 (跨域, 不越界)

1. **[需 polymarket-protocol-expert]** YES/NO 是否同一 condition 的互补 token? cross-book 锁定(`x_arb_free_edge`)在 neg-risk 市场的双边定价约束? fee 是否对双边各收?(影响 §3.2 增益 2 能否落地)
2. **[需数据组 小余/小冯 + 我联合实测]** sharp_fair(g_bm_inplay_fair)在**生产环境**的真实覆盖率? 这是整个论点的硬前提 —— 没 sharp 锚, 主标签退化(§4.3)。
3. **[需老韩 RM]** 连续 Kelly λ 起点(arb_sizing 现 0.10)? σ_dmid 含 slippage 不确定性的修正系数(§3.4)? horizon_cap / max_open_legs 真钱前的 paper 验证阈值?(我出 σ 修正公式, 系数风控定)

---

## 附: 一句话交付

预测量定为**符号化 sharp 收敛实现量 y_h = sign(gap)·Δmid**(gap = inplay_sharp_fair − microprice), horizon 锁 **{5,10,12,15}s**(落在我们 3s 延迟与 PM 13s 收敛的交集), 强基线 **LightGBM quantile 回归**(直接产 CI 下界给 arb_signal), 套利正期望条件 **ci_low − BE > 0**(BE 含双向 slippage), 防过拟合靠 **condition-group PurgedKFold + walk-forward OOS rank-IC** 唯一上线门。骨架(Phase 1-5 + 标签管道 + walk-forward + IC)**已实现 90%**, 缺口是标签公式精化 + 真 ONNX 基线 + sharp 覆盖率实测。最大风险不是建模, 是 **sharp_fair 生产覆盖率**(待实测)和 **20.5h 样本量不够上线**(需再攒 1-2 周)。
