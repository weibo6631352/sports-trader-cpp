# c4 review — sizing 读 RM 同源真实 exposure (P0-2 隐患#1 闭合)

- owner: 老韩 (risk-engineer, RM 主权)
- last_review: 2026-05-31
- 标的 commit: e4dc263
- 性质: 只读 review, 不改主干
- 结论: **APPROVE**

## 背景

老韩 A5 review + P0-2 隐患#1 原文:「c2-c5 若让 paper 回写 exposure, sizing 必须从 RM
真实 exposure 取值, 否则第 2 笔后 per_outcome/condition 双轨偶发拒且 sizing 无感」。
A5/P0-1 已让 paper 喂真实 exposure 给 RM (set_condition/outcome_exposure), 但 sizing
侧此前硬编码 current_*_exposure=0 = 活跃分叉。c4 修这个。

## diff 核对

`src/stcpp/paper/paper_loop.cpp` TickOne (loop_thread_), sizing 前:
- 删 `sz_in.current_token_exposure_usdc = 0.0; sz_in.current_condition_exposure_usdc = 0.0;`
- 改为查 `position_ledger_.get_per_condition_exposure()` / `get_per_outcome_exposure()`,
  find(condition_id)/find(token_id), 命中 `÷1e6` (micro→whole) 填 sz_in, 未命中回退 0.0。

## 5 点逐项

### 1. ÷1e6 单位方向 — 正确

- ledger getter 返回 micro (position_ledger.cpp:127-140; A1 后 size_usdc / condition_exposure_
  全 micro, apply_fill:54-55,103 直存 fill_size_usdc=micro 无 cast)。
- sizing headroom 域 = whole pUSD: sizing_calculator.cpp:191-192 `cap2_limit =
  per_outcome_cap_usdc.to_pusd()` (micro→whole), `headroom2 = cap2_limit −
  current_token_exposure_usdc`; cap3 同 (202-203)。两边相减 → current 必须 whole。
- 故 micro `÷1e6` → whole 方向**对**。若反成 ×1e6, headroom 立刻变巨负 → effective
  恒 clamp 0 → 第 2 笔起 sizing 永远砍到 0。现态正确。

### 2. 同源一致性 — 单源, 满足隐患#1

- c4 (paper_loop.cpp:437-438) 与 FeedRiskGateway 喂 RM (759-762) 调**同一对 getter**,
  同一 key (condition_id / token_id)。
- 整链同源同值, 各自在自己量纲内比 cap:
  - RM 侧 (risk_gateway.cpp:509-518,531): 取 condition_exposure_usdc[cid] (micro,
    FeedRiskGateway 喂的同 micro 值), `from_micro(cur+size_micro) > cap (micro)`。全程 micro。
  - sizing 侧: `cap.to_pusd() − current_whole`。全程 whole。
  - 同一 source (get_per_*_exposure) → micro → 一边直喂 RM 比 micro, 一边 ÷1e6 比 whole。
    无第二真值源, 无双轨。**这正是隐患#1 要的"单源"。**

### 3. lookup 回退 0.0 — 正确

首笔无仓位 → token_positions_/condition_exposure_ 无该 key → find→end → 0.0。0 exposure
= 满 headroom, 语义对。fill-producing T15/A2 (0 前置 exposure) 行为不变, 印证。

### 4. behavioral 测试缺口 — 认同归 M2

M1 advisory harness tick sizing 恒返回 kelly=0 (同 T15 不断言 suggested>0), 单位错/对
都给 0, 无法 behavioral 区分。**认同归 M2。** 但留一条更轻的 M1 seam (见下), 比纯 M2 早。

### 5. 副作用 / R-12 — 无顾虑

- TickOne 在 loop_thread_ (paper_loop.cpp:267,283), **非 WSS io_thread** → 不触 R-12
  (R-12 针对 io_thread event loop 的阻塞/锁 >100us)。
- 性能: 该 tick getter 调用 2→4 次 (c4 + FeedRiskGateway), 每次 shared_lock 拷整 map。
  loop_thread_ 非热路径, M1 仓位规模小, 可接受。记 M2 nit。

## REQUEST-CHANGES? — 无, APPROVE

单位方向对、单源满足隐患#1、回退正确、不触 R-12。无阻塞项。

## 建议 (非阻塞, M2 backlog)

- **M2-c4-1 (behavioral, 必做)**: 非 advisory harness, 注入 2 笔同 condition fill →
  ledger 真 exposure → 断言第 2 笔 sizing headroom = cap.to_pusd() − 真 exposure (而非
  满 cap), 且与 RM 拒/放结论一致 (sizing 砍到的额 ≤ RM 放行额, 无 surprise-reject)。
  这是隐患#1 真正的回归守护, 单位反了必红。
- **M2-c4-2 (单测 seam, 更轻可早做)**: 直接 SizingCalculator 单测喂
  current_*_exposure_usdc = 已知 whole 值, 断言 headroom = cap.to_pusd() − current
  (这条与 advisory harness 无关, M1 即可加, 守 sizing 侧单位/headroom 公式)。再加一个
  paper_loop 传参 seam (验 ÷1e6 转换那段) 即把 c4 那行也覆盖到 —— 不必等 M2。建议
  M2-c4-2 提前到 M1 收尾。
- **M2-c4-3 (perf nit)**: 同 tick getter 调 4 次 + map 整拷。若 M2 仓位数上量, 考虑
  TickOne 内复用 FeedRiskGateway 已取的 map (或一次取本 condition/token 单 key)。M1 不动。
