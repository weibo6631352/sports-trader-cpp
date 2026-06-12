# ADR 2026-06-12 — RM 边界:风控只管真实风险,执行质量与 alpha 归引擎

- **owner:** 老雷 (GM)
- **last_review:** 2026-06-12
- **状态:** 已决 (老板 2026-06-12 拍板:「全部同意,我已经点头了,不用其他人点头」)
- **触发:** 实盘 disarmed 观测发现 RM 以 `LOW_FILL_RATE` 硬拒了赔率引擎合法决策(订单尺寸 > L1 深度 → fill_rate < floor → 整单拒),把「成交质量」当「风险」挡掉,越界进了引擎的执行逻辑。

## 1. 背景

实盘拒单面板出现两类拒因:

1. `INVALID_TOKEN_ID_FORMAT` — token_id 78 位被误拒(off-by-one,`>77u` 应 `>78u`,2^256-1 是 78 位十进制)。**已 Phase 0 修复**。
2. `LOW_FILL_RATE` — 订单尺寸超过订单簿 L1 两档内可成交深度,`SlippageModel` 算出 `expected_fill_rate < FILL_RATE_FLOOR(0.50)` → `RiskGateway::check_liquidity_` 直接 `reject = LOW_FILL_RATE` 整单否掉。

第 2 类是本 ADR 的核心。老板原则(2026-06-12 verbatim):

> **「风控不能挡我们引擎的逻辑啊,要调也是调引擎不是调风控。」**
> **「风控 / churn 护栏,RM fill-rate 门,合理的情况下需要让步。」**

`LOW_FILL_RATE` 不是风险 —— 它是**成交质量信号**。一笔单只能成交 40% 不代表「危险」,只代表「这次吃不满」。正确反应是**引擎按可成交深度切小订单、分批吃**(见配套设计 `depth-aware-accumulation-design.md`),而不是 RM 把整笔合法决策毙掉。RM 越界替引擎做了「要不要下、下多少」的执行决策。

## 2. 决策:RM 的边界

**RiskManager 只强制「真实风险」—— 即一旦突破会造成不可逆资金损失 / 账本损坏 / 系统性灾难的边界。执行质量(fill_rate / slippage)与 alpha(edge / EV)不是风险,归各盈利引擎自己管。**

### 2.1 RM 留下(真实风险,保持硬拒)

| 类别 | 拒因 | 为什么是真实风险 |
|---|---|---|
| 仓位上限 | `EXCEED_*_CAP` (per-market / per-condition / gross) | 突破 = 单点暴露超授权,真实无界亏损面 |
| 资金 | `INSUFFICIENT_BANKROLL` / `KELLY_OVERSIZE` | 突破 = 下注超过钱包 / 凯利上限,真实爆仓面 |
| 状态机 | `MARKET_NOT_TRADEABLE` / `DUPLICATE` / `NONCE_*` | 突破 = 对已结算/重复/乱序单动钱,真实账本损坏 |
| 格式/血缘 | `INVALID_TOKEN_ID_FORMAT` / `BOOK_TS_ZERO` / 4ts | 突破 = 发非法/无血缘单,真实链上失败或合规事故 |
| 簿深度天花板 | `EXCEED_BOOK_DEPTH` | 保留为**纵深 sanity bound**(单子 >100× 全簿明显是 bug);引擎按深度切单后正常不触发 |

### 2.2 RM 让出(执行质量 / alpha,降级为 advisory)

| 拒因 | 旧行为 | 新行为 | 归属 |
|---|---|---|---|
| `LOW_FILL_RATE` | 整单硬拒 | **降级 advisory**:仍算 `expected_fill_rate` 写入决策供观测/审计,**不 set reject** | 引擎(按可成交深度切单) |
| `EXCESSIVE_SLIPPAGE` | 整单硬拒 | **降级 advisory**:仍算 `slippage_bps` 写入决策,**不 set reject** | 引擎(限价单本身已封顶滑点) |

**advisory 语义**(固化,沿用 §8.1 D3):RM 仍计算并填充 `d.expected_fill_rate` / `d.slippage_bps`,这些值继续流到前端拒单面板 / audit log 作为**观测指标**;但**不再设 `d.reject`**,`check_liquidity_` 对这两类返回 `false`(放行)。观测不变,门拆掉。

### 2.3 本 ADR 不动(留待老板后续定夺)

- `EDGE_CI_NEGATIVE` / `EDGE_NEGATED_BY_SLIPPAGE`(`check_signal_` 内):按「edge 归引擎」原则它们也该让出,但**仅当 `signal_edge_ci` 被注册时才触发**(live sharp/flb 路径当前不注册 → 实际不挡)。Phase 2 不动,避免越界过度修改;若未来 live 路径注册 signal CI 再议。

## 3. 后果

1. **引擎拿回执行主权**:赔率引擎/FLB 引擎自己决定「吃多少、分几次吃」,按订单簿可成交深度切单(`depth-aware-accumulation`)。RM 不再用成交质量否掉合法决策。
2. **调参调引擎不调 RM**:成交不理想 → 调引擎的切单/累积逻辑;RM 的 cap/bankroll/状态门**永不为追成交而放松**(纪律 > 收益,§3 铁律 2)。
3. **观测零损失**:fill_rate / slippage 继续上报,只是从「门」变「仪表」。前端拒单面板不再被执行质量噪声刷屏,留真实风险拒因。
4. **红线不破**:§8「任何下单链路绕过 RM」**仍成立** —— 所有单照走 RM,RM 照查真实风险门;本 ADR 只收窄 RM 的**职责范围**(不查执行质量),不是绕过。caps/bankroll/状态/格式/血缘门一条不删。

## 4. 落地(Phase 2)

- `risk_gateway.cpp::check_liquidity_`:`FillRateBelowFloor` 与 `slip_abs > excessive_slippage_bps` 两分支不再 set reject,改为只填 advisory 字段后 `return false`。
- `sizing_calculator.cpp`:`fill_rate < floor` 不再把尺寸归零;尺寸由引擎按 `min(target − current, 可成交深度)` 决定(见 `depth-aware-accumulation-design.md`)。
- RM 单测:`LOW_FILL_RATE` / `EXCESSIVE_SLIPPAGE` 相关用例从「断言拒」改为「断言放行 + advisory 字段已填」。

## 5. cite

- 老板 verbatim 2026-06-12:「风控不能挡我们引擎的逻辑啊,要调也是调引擎不是调风控」/「全部同意,我已经点头了,不用其他人点头」
- §8.1 D3 advisory 语义(advisory = 不自动路由,≠ 不产生动作)
- 配套设计:`docs/RESEARCH/depth-aware-accumulation-design.md`
- §3 铁律 2(纪律 > 收益):RM 真实风险门不为成交让步,本 ADR 让出的只有执行质量
