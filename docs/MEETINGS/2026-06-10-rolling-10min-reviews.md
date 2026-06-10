# 滚动复盘日志 — 10 分钟节奏（2026-06-10 晚，老板指令「10分钟就可以复盘」）

owner: 老雷 (GM) | last_review: 2026-06-10
> 23:44 三修重启（exit-cap 豁免 / event cap 120u / 转写匹配）后的连续观测。铁律：不再重启，攒 clv_n。
> 每轮检查: clv_n / 覆盖 / fills+离场原因 / EXCEED+orphan / 净PnL。

## 迭代 #5 — 23:48（运行 ~25min）

- **净 +5.54**（unreal +6.05，浮盈）| real 0 | 费 0.51 | 持仓 6
- 入场 8 笔 / 离场 0 —— 「拿住赢面仓零 churn」按设计运行，浮盈在长
- 覆盖 sharp=50 稳 ✓ | EXCEED 拒单 0 ✓（exit-cap 修复后保持干净）| orphan-diag 静默 ✓ | clv_n=0（等结算）
- 频率 ≈19 笔/h（持续爬升：上轮 12/h）
- **无需动作**：系统按设计跑，等首批结算进 CLV

## 迭代 #6 — 23:59（运行 ~35min）

- 净 +0.57 | **real −6.26**（首批离场）| unreal +7.74 | 费 0.92 | 持仓 9
- **首次离场实测：5 笔 fill 全部 `book_deteriorate`**（同一仓分块平掉）：NO 入 ~0.66 → fair 崩到 0.371（≤0.46 赢面门 ✓）+ 簿恶化 ✓ → 卖 0.350（fair−2.1%，在 0.05 slip cap 内 ✓ 无砸卖灾难）
- **出场三件套首次实战通过**：触发条件、卖价纪律、无拒单循环（EXCEED 0）全符设计
- 13 入 5 出（出全是单一崩盘仓）—— 不是 churn，是止损纪律
- sharp=48 稳 | orphan 静默 | 结算池 24 resolved（在涨，我们的仓还没轮到结算 → clv_n=0）
- 判读：浮盈 +7.74 vs 止损 −6.26 = 典型「favorite 回归 + 截断左尾」形态，方向是否 +EV 等 CLV

## 迭代 #7 — 00:11（运行 ~45min）

- 净 **+2.22**（unreal +9.39 继续爬）| real −6.26 不变 | 10min 内无新 fills（13入/5出）
- **[orphan-diag] 首次触发**：孤儿持仓=2（比赛打完掉出 catalog）、resolved=0、**pending=2** —— 预期中间态（PM resolution 在赛后几分钟~几小时才出）。SettlementPoller 已在轮询其 cid（HeldConditions 并集修复）。**盯：pending 若 >1h 不降 = 结算漏，立刻查**
- EXCEED 0 / 进程 1 / clv_n=0（两个孤儿 resolve 后应首开张）

## 迭代 #8 — 00:22（运行 ~55min）

- 净 +1.20 | real −8.80（新增 3 笔 book_deteriorate 止损）| unreal **+11.34** | 持仓 12
- 频率 ≈21 笔/h（19入/8出），出场仍 100% book_deteriorate（纪律一致）
- **orphan pending 2→1**：一个孤儿离开 pending（推测：比赛回到 catalog 或仓被平）；剩 1 个继续盯
- clv_n=0：PM resolution（UMA）对 ITF 网球出得慢，属平台节奏非我们的漏
- 形态持续：「截左尾(realized−) + 浮盈右尾(unreal+)」——结算落地前 realized 必然偏负，**勿据此判死策略**，等 settlement 把赢家变现

## 迭代 #9 — 00:33（运行 ~65min）

- 净 **+7.97** ↑↑ | unreal **+19.09**（17 仓）| real −8.80 无新止损 | 费 2.32
- 10min 新入 12 笔（频率 ≈28/h 加速，晚场赛事增多）；出场 0 新增
- orphan pending=1 稳 | clv_n=0 | EXCEED 0
- 无动作。净值曲线 +1.2→+8.0，浮盈引擎在转
