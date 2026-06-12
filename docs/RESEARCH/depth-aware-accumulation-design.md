# 深度感知累积下单设计 (depth-aware accumulation)

- **owner:** 老雷 (GM)
- **last_review:** 2026-06-12
- **状态:** Phase 2 (sharp) 落地中 / Phase 3 (FLB) 待回测放行
- **配套 ADR:** `docs/ADR/2026-06-12-rm-boundary-risk-vs-execution.md`

## 0. 一句话

引擎不再「想下多少就砸多少」让 RM 用 fill_rate 毙掉,而是**每次只吃订单簿当下能成交的深度,分多次累积到目标仓位**。FOK 单不再因 size > 簿深度整单失败,churn 不再因小单手续费失控。

## 1. 问题

### 1.1 现状(被 RM `LOW_FILL_RATE` 挡)

赔率引擎算出目标仓位(凯利,如 $50),把**整笔 $50 作一个 FOK 单**提交。RM `check_liquidity_` 用 `SlippageModel` 算 `expected_fill_rate = f(order_size, book_depth_l1)`:若簿上只有 $20 可成交 → `fill_rate ≈ 0.40 < FILL_RATE_FLOOR(0.50)` → `reject = LOW_FILL_RATE`,整单毙。

即便 RM 放行(ADR Phase 2),**FOK(fill-or-kill)语义**下 $50 砸 $20 簿仍是 **0 成交**(要么全成要么全杀)。所以光拆 RM 门不够 —— 引擎必须**主动把单切到可成交深度**。

### 1.2 两个引擎的现状

| 引擎 | 目标仓位 | 当前下单方式 | 累积能力 |
|---|---|---|---|
| **sharp**(赔率) | 凯利动态(residual = target − current) | 整笔 residual 作 FOK | **已天然累积**:每 tick sizing 喂 `current_token_exposure`(per-engine sharp 份额),算 residual。缺的只是「把 bite 切到深度」 |
| **FLB**(订单簿) | 平注 $25 | `flb_seen_.insert` 一盘一击,整 $25 一次 FOK | **无累积**:一次没吃满就 `flb_seen_.erase` 重试整 $25,不是「补到 $25」 |

## 2. 核心机制:可成交深度切单

### 2.1 可成交深度 `fillable_depth_usd(px)`

对一笔**限价 BUY @ px**,可成交深度 = 簿上 ask 价 ≤ px 的累计挂单量(USD 计):

```
fillable_depth_usd(px) ≈ Σ ask_size_usd[i]   for all ask_px[i] ≤ px
```

实现近似(热路径,不重建全簿):用 `exec_feat.best_ask_size()`(L1 USD 深度,已有)作一档近似;后续可扩到 2 档内累计(`fill_rate_model` 已用 `depth_within_2_ticks` 概念)。

### 2.2 每次 bite 尺寸

```
remaining   = target_usd − current_engine_pos_usd        // 距目标还差
bite_raw    = min(remaining, fillable_depth_usd × SAFETY_FRAC)
bite_usd    = bite_raw  if bite_raw ≥ MIN_BITE_USD  else 0  // 太小不下,等深度
```

- `SAFETY_FRAC = 0.80` — 不抢光簿上显示量(并发 taker / 撤单竞争留余量);FOK 在 80% 深度内成交概率高。
- `MIN_BITE_USD = 5.0` — 单笔下限。低于 $5 不下,等下个 tick 深度回补。**防小单手续费 churn**(fee = shares×rate×p×(1−p),碎单累积手续费吃掉 edge)。Polymarket 硬下限 $1,我们 $5 留缓冲。
- FOK 单按 `bite_usd` 提交 → 落在可成交深度内 → 成交概率高,RM 不再 advisory-flag。

### 2.3 累积:多 tick 补到目标

```
每个有效入场 tick:
  current = engine_pos[(token, engine)]            // per-engine 真实已持
  if current ≥ target_usd × (1 − ε):  return       // 已到目标,停手 (cap 由 RM 聚合门兜底)
  bite = depth_aware_bite(target, current, book)   // §2.2
  if bite == 0:  return                            // 深度不足,本 tick 跳过,等下次
  submit FOK(bite) → apply_fill 归到 engine 份额
```

下一次入场信号/触发再出现时,簿深度通常已回补(maker 补单),再吃一口,直到 `current ≈ target`。**累积是跨 tick 的,不是单 tick 内循环**(单 tick 内反复砸会打穿簿 + 加剧逆选)。

## 3. 落地

### 3.1 Sharp(Phase 2,不需回测,ADR 已授权)

sharp **已天然按 residual 累积**(每 tick sizing 用 `current_token_exposure` 算 `target − current`)。只需两处改:

1. **RM 拆门**(ADR Phase 2):`check_liquidity_` 的 `LOW_FILL_RATE` / `EXCESSIVE_SLIPPAGE` 降 advisory,不再毙单。
2. **bite 切深度**:sharp 提交前,把凯利 residual 再钳一层 `min(residual, fillable_depth × 0.80)`,且 `≥ MIN_BITE_USD` 才下。这样 FOK 落在可成交深度内 → 真成交。

> sharp 路径 sizing 当前用 `sz_in.fill_rate = 0.65`(固定 M1),**不触发 sizing 的 FILL_RATE_FLOOR**;`LOW_FILL_RATE` 全来自 RM `check_liquidity_`(真实 size vs depth)。故 Phase 2 sharp 改动集中在 RM 拆门 + 提交前 bite 钳深度,不动 sizing_calculator 的 0.65。

### 3.2 FLB(Phase 3,**回测放行后**才实现)

FLB 当前 `flb_seen_` 一盘一击 + 整 $25 FOK。改为累积:

- 触发器从「一盘一击」改为「**补到 $25 为止**」:`current_flb = engine_pos[(token,"flb")]`;每次触发 `bite = min($25 − current_flb, fillable_depth × 0.80)`,`≥ $5` 才下。
- 退出累积:`current_flb ≥ $25 × (1−ε)` → 标记该 condition 完成(类似现 `flb_seen_` 但语义是「已建满仓」非「已触发一次」);或 mid 跌出 0.77 触发带 → 停止补仓(不追)。
- 持有到结算逻辑不变(FLB 是 hold-to-settlement,平注满仓后只等结算)。

**Phase 3 回测门(GATED):** 实现前先回测验证「累积到 $25 vs 一次性 $25」:
- **edge 不劣化**:分批吃的均价 ≤ 一次性吃的均价(分批在更薄簿上吃,均价可能更差?需实测;FLB 是 favorite 低估,簿通常厚,预期持平或更优)。
- **churn 可控**:分批的总手续费增量 < 因「整单 FOK 失败丢失的入场机会」挽回的 EV。
- 数据:复用 FLB 已采集结算样本(`docs/RESEARCH` FLB 理论 + EV 存档),walk-forward 对比两种 sizing。
- 回测不过 → FLB 保持一盘一击 $25,Phase 3 搁置。

## 4. 参数表

| 参数 | 值 | 说明 | 可调 |
|---|---|---|---|
| `SAFETY_FRAC` | 0.80 | 吃簿显示深度的比例 | 引擎常量(不进配置,老板「系数不进配置层」) |
| `MIN_BITE_USD` | 5.0 | 单笔下限,防 churn | 引擎常量 |
| `target`(sharp) | 凯利动态 | residual = target − current | 凯利已定 |
| `target`(FLB) | 25.0 | `kFlbStakeUsdc` | 已定 |
| `ε`(到位容差) | 0.05 | current ≥ target×(1−ε) 即停 | 引擎常量 |

## 5. 不变量 / 红线

- **RM 聚合 cap 仍是硬天花板**:累积每一 bite 照走 RM,per-market/condition/gross cap 一条不松(ADR §2.1)。累积只在 cap **以内**补仓。
- **per-engine 隔离**:sharp 累积吃 sharp 份额预算,FLB 吃 FLB 份额,互不挤占(Option A `engine_pos_`)。RM 聚合门 = 两引擎合并上限。
- **§8 不绕 RM**:每个 bite 都是独立 RM-gated 单,不批量绕过。
- **FOK 语义**:每 bite 仍是 FOK(全成或全杀),只是 size 切到可成交深度让「全成」成为常态。
