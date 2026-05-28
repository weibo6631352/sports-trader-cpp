# laohan-w8-rm-or-assertion-audit.md — RM-01/02 OR 断言自审 + 派老沈修 spec

- **owner:** 老韩 (risk-engineer, B 单元主管)
- **last_review:** 2026-05-28 (W8)
- **触发:** 小宋 W8 W1 retro 发现 R13/R17 OR 断言
- **派单:** 老沈 W8 W3 修 (RM 测试 owner)
- **关联:** ADR-004 (短路顺序) / ADR-005 §2.2 / ADR-021 (worktree) / ADR-023 (IC 自测裁判)

---

## §1 RM-01 R13_EDGE_NEGATED_BY_SLIPPAGE 自审

### 现有 OR 断言 (test_risk_gateway.cpp 行 278-281)

```
EXPECT_TRUE(d2.reject == RejectCode::EDGE_NEGATED_BY_SLIPPAGE ||
            d2.reject == RejectCode::EXCESSIVE_SLIPPAGE       ||
            d2.reject == RejectCode::LOW_FILL_RATE)
```

### 老韩读代码后的自审结论

**确认: 这是真 bug, 根因是 ADR-004 patch 后测试未 update.**

分析如下:

1. **ADR-004 patch 前 (W4) 的顺序:** liquidity 在 position_caps 之前. R13 测试用例
   (`ρ = 1.25, edge_lower = 50 bps`) 当时 `check_liquidity_` 先跑, SlippageModel
   返回 `slippage_bps = 150`, 同时 `fill_rate` 在一档内 (ρ ≤ 1.25, 未触发 FillRateBelowFloor,
   未触发 EXCEED_BOOK_DEPTH). liquidity 通过, 进 `check_signal_`, 因 edge_bps (50) < slippage_bps (150)
   → 精确触发 `EDGE_NEGATED_BY_SLIPPAGE`. **ADR-004 patch 前断言本可精确**.

2. **ADR-004 patch 后 (W5, 老沈) 顺序不变 liquidity → signal.** 这一段顺序 ADR-004
   §4 note 明确: "liquidity 仍在 signal 之前, 数据流不破". 顺序没有影响 R13 测试逻辑.

3. **真正原因: 小肖 SlippageModel 参数边界在 test 设计时未验算清楚.** 具体:
   - `book_depth_l1_usdc = 800`, `size_usdc = 1000` (make_ok_intent 默认) → `ρ = 1.25`
   - 多档公式: `pf = 0.5 + 0.01*(0.5 + 0.25*1.0) = 0.5075`, `slip_bps = 150`
   - `fill_rate = (1/1.25) * (1 - pi_withdraw) * (1 - s_stale)`. `s_stale` 取决于
     `book_snapshot_ts_ns` 距 `wall_now_ns` 的实际 dt. `make_ok_intent` 设
     `book_snapshot_ts_ns = now - 200ms`, 故 `s_stale ≈ 1 - exp(-200/30000) ≈ 0.0066`,
     `pi_withdraw = 1 - exp(-0.3*1.25) ≈ 0.312`,
     `fill_rate ≈ 0.8 * 0.688 * 0.9934 ≈ 0.547 > FILL_RATE_FLOOR (0.50)`.
   - SlippageModel 返回 `RejectCode::Ok` (fill_rate 未跌穿 floor, ρ ≤ RHO_MAX).
   - 然后 `check_liquidity_` 对 slippage_bps: `150 < cfg_.excessive_slippage_bps (200)`,
     不触发 EXCESSIVE_SLIPPAGE.
   - 流程进 `check_signal_`: edge_bps (50) < slippage_bps (150) → `EDGE_NEGATED_BY_SLIPPAGE`.

4. **结论: 此场景数学上精确触发 `EDGE_NEGATED_BY_SLIPPAGE`, OR 断言是防御性写法掩盖
   了 IC 的不确定感, 不是 "多个合法结果之一". 小宋的判断正确: 这是文档断言, 不是测试.**

5. **ADR-004 是否导致 update 漏掉?** 没有. ADR-004 patch 改的是 position_caps 和
   liquidity 的相对顺序, R13 测试不涉及 position_caps. 漏根因是 IC 在编写测试时对
   SlippageModel 参数边界没有做精确手算验证.

### 派老沈 W8 W3 精确断言要求

- 删除三路 OR, 改为: `EXPECT_EQ(d2.reject, RejectCode::EDGE_NEGATED_BY_SLIPPAGE)`
- 在测试注释里写出 ρ/pf/slip_bps/fill_rate 手算值 (防止下次 SlippageModel 参数改动后
  静默失效)
- 参数精确约束: `book_depth_l1_usdc = 800` (ρ=1.25), `size_usdc = 1000`,
  `edge_ci_lower = 0.005` (50 bps), `book_snapshot_ts_ns = now - 200ms`,
  `excessive_slippage_bps = 200 (cfg 默认)`
- 验证路径: liquidity 通过 (fill_rate ≈ 0.547 > floor, slip_bps 150 < 200) →
  signal 触发 (50 < 150)

---

## §2 RM-02 R17_EXCESSIVE_SLIPPAGE 自审

### 现有 OR 断言 (test_risk_gateway.cpp 行 326-328)

```
EXPECT_TRUE(d.reject == RejectCode::EXCESSIVE_SLIPPAGE ||
            d.reject == RejectCode::LOW_FILL_RATE)
    << "got " << static_cast<int>(d.reject);
```

### 老韩读代码后的自审结论

**确认: 这是真 bug, 根因是 SlippageModel 内部优先级 IC 当时没有精确分析.**

分析如下:

1. **测试参数:** `book_depth_l1_usdc = 400`, `size_usdc = 1000` → `ρ = 2.5`.
   多档公式 `pf = 0.5 + 0.01*(0.5 + 1.5*1.0) = 0.52`, `slip_bps = 400`.
   `cfg_.excessive_slippage_bps = 200`, 故 slip_bps 400 > 200.

2. **SlippageModel 内部顺序 (`slippage_model.hpp` `compute_linear`):**
   - 先判 `ρ > RHO_MAX (3.0)`: ρ=2.5 ≤ 3, 不触发 `ExceedBookDepth`.
   - 计算 fill_rate: `(1/2.5) * (1 - pi_withdraw) * (1 - s_stale)`.
     `pi_withdraw = 1 - exp(-0.3*2.5) ≈ 0.528`,
     `s_stale ≈ 0.0066` (同 §1, book_snapshot_ts = now-200ms).
     `fill_rate ≈ 0.4 * 0.472 * 0.9934 ≈ 0.188 < FILL_RATE_FLOOR (0.50)`.
   - SlippageModel **在 fill_rate < floor 时先返回 `FillRateBelowFloor`**, 不再判
     EXCESSIVE_SLIPPAGE. 这是 `slippage_model.hpp` 行 197-203 的明确顺序.
   - 映射到 RM: `check_liquidity_` 收到 `FillRateBelowFloor` → 返回 `LOW_FILL_RATE`.
     EXCESSIVE_SLIPPAGE 分支 (行 284-288) 根本执行不到.

3. **结论: 此场景精确触发 `LOW_FILL_RATE`, 不会触发 `EXCESSIVE_SLIPPAGE`.
   SlippageModel 内部优先级是 `FillRateBelowFloor > EXCESSIVE_SLIPPAGE`,
   由小肖实现时已在 spec 注释里确认 ("FILL_RATE_FLOOR 闸门, 比 Kelly EDGE_NEGATED 更早拒,
   见 spec §1.4"). IC 编写测试时未读透 SlippageModel 代码, 打了防御性 OR.**

4. **ADR-004 影响:** 无. R17 测试不涉及 position_caps, 顺序 patch 对此 case 无影响.

5. **SlippageModel 优先级是否已定?** 已定. `slippage_model.hpp` 实现代码即 SSOT:
   `ExceedBookDepth` (ρ > RHO_MAX) > `FillRateBelowFloor` > EXCESSIVE_SLIPPAGE
   (在 `check_liquidity_` 的 EXCESSIVE_SLIPPAGE 兜底分支). 这个顺序在小肖 spec
   `xiaoxiao-slippage-model-lib-v1.md §1.4` 有 paper case 背书, 不需要新 ADR.

### 派老沈 W8 W3 精确断言要求

- 删除二路 OR, 改为: `EXPECT_EQ(d.reject, RejectCode::LOW_FILL_RATE)`
- 在注释里写明: ρ=2.5, fill_rate≈0.188 < 0.50 floor → SlippageModel 先返
  `FillRateBelowFloor` → RM 映射 `LOW_FILL_RATE`. `EXCESSIVE_SLIPPAGE` 分支未到达.
- 如果需要独立测试 `EXCESSIVE_SLIPPAGE` 路径 (fill_rate >= floor 但 slip_bps > cfg),
  需要另造一个 case: fill_rate 过 floor 但 slippage 大. 参数参考:
  `ρ = 0.5` (一档内, fill_rate 高), `book_snapshot_ts_ns = now-200ms`,
  调大 `size_usdc` 让 slip_bps > excessive_slippage_bps. 具体参数老沈手算后确定.
  这个 case 是新增, R17 只改精确断言, EXCESSIVE_SLIPPAGE 独立 case 是 P2 补充.

---

## §3 ADR-004 顺序与测试一致性 audit

### position_caps 与 liquidity 顺序

ADR-004 已完整 settle (选 B). `check_position_caps_` 在 `check_liquidity_` 之前,
实现代码 (`risk_gateway.cpp` 行 400-401) 和 header 注释均已更新. regression test
`EvaluatePriority_PositionCapBeforeLiquidity` 已存在且精确 (单一断言
`EXPECT_EQ(d.reject, EXCEED_PER_ORDER_CAP)`). **ADR-004 层面无遗留问题.**

### liquidity 内部子优先级 (depth / fill_rate / slippage_bps)

已定, SSOT 为 `slippage_model.hpp` 实现 + 小肖 `xiaoxiao-slippage-model-lib-v1.md §1.4`:

```
ExceedBookDepth (ρ > RHO_MAX=3)
  > FillRateBelowFloor (fill_rate < 0.50)
    > EXCESSIVE_SLIPPAGE (slip_bps > cfg, 兜底)
```

**这个子优先级在老韩 W4 设计时通过 ADR-004 仲裁会隐含确认 (老郭 §3.1 论点 3:
"fill_rate 不足比 slippage 高更早拒绝"). 小肖 `slippage_model.hpp` 实现与该顺序一致.**

**结论: 不需要立新 ADR-025. SSOT 已存在于 `slippage_model.hpp` + 小肖 spec.** 若小肖
确认 spec §1.4 已足够清晰 (见 §6 不耻下问), 则仅需在 `risk_gateway.hpp` header 注释
中补一行 liquidity 子优先级说明 (派老沈 W8 W3 顺带更新).

---

## §4 派单老沈 W8 W3 (worktree + commit)

### 必须完成 (P0)

1. **RM-01 R13 改精确断言:**
   - 文件: `tests/unit/test_risk_gateway.cpp`
   - 改 OR 为 `EXPECT_EQ(d2.reject, RejectCode::EDGE_NEGATED_BY_SLIPPAGE)`
   - 注释补手算: ρ=1.25, slip_bps=150, fill_rate≈0.547, edge_bps=50 < 150 → 精确触发

2. **RM-02 R17 改精确断言:**
   - 同文件
   - 改 OR 为 `EXPECT_EQ(d.reject, RejectCode::LOW_FILL_RATE)`
   - 注释补手算: ρ=2.5, fill_rate≈0.188 < 0.50 → `FillRateBelowFloor` 先触发

3. **risk_gateway.hpp header 注释补 liquidity 子优先级:**
   - 在 check 7 liquidity 行补: "(子优先级: ExceedBookDepth > FillRateBelowFloor > EXCESSIVE_SLIPPAGE, SSOT = slippage_model.hpp + 小肖 spec §1.4)"

4. **ADR-023 要求: 老沈 worktree + git commit + 老韩 review + merge**

### 可选 P2 (ADR-023: IC 自己判断)

- 新增 `R17b_EXCESSIVE_SLIPPAGE_pure` test: 构造 fill_rate >= floor 但 slip_bps > cfg 的 case
- 如小宋 retro 其他 P0 盲点也由老沈一并补 (见 §5)

### 约束 (老韩硬 enforce)

- 老沈写代码, 老韩不写 cpp (ADR-005 §2.2 主管 cpp=0)
- 老沈 worktree 路径: `.claude/worktrees/agent-<老沈 wave id>/`
- commit message 格式: `test(risk-gateway): fix OR assertions R13/R17 to single precise EXPECT_EQ`
- 老韩 review 要点: 断言是否精确, 手算注释是否与代码参数对应, CI 是否通过

---

## §5 与小宋 W8 W1 retro 配套 (其他 RM 盲点)

小宋 retro 指出 32 中 P0 RM 盲点. 以下三项与本 audit 相关, 老韩给建议但不强制
(ADR-023: IC 可选参考):

| 盲点 | 建议 | 优先级 |
|---|---|---|
| RM 并发 evaluate (多线程 race) | 老沈 W8 W3-W4 加并发 smoke test (2 线程 × 100 次 evaluate, verify 无 crash / race) | P1, 建议 W8 W4 |
| bankroll=0 边界 | `set_bankroll(0)` → verify `INSUFFICIENT_BANKROLL` (size>0 即拒). 已有 R11, 但 bankroll=0 边界未覆盖 | P1, 老沈 W8 W3 顺带加 |
| STALE_DATA 精确阈值 (799/800/801ms) | R05 只测 1500ms, 缺 800ms 边界三点. 老沈加三个边界 case. threshold_of() 有 static_assert, 但 evaluate 路径未覆盖边界 | P1, 老沈 W8 W3 顺带加 |

**上述三点老韩建议, 老沈自决是否 W8 W3 一起做. 若老沈 W8 W3 工作量已满, 可延 W4.**

---

## §6 不耻下问

本 audit 涉及跨域问题, 老韩主动求助以下同事:

- **@老沈** (RM 测试 owner, B 单元 IC): 接 W8 W3 派单, 确认 §4 要求无歧义,
  worktree 路径确认, 遇到 SlippageModel 参数不确定找小肖确认后再改.
- **@小肖** (SlippageModel owner, A 单元): 确认 `slippage_model.hpp §1.4 paper case`
  注释是否已足够清晰表达 FillRateBelowFloor > EXCESSIVE_SLIPPAGE 子优先级.
  若 spec 文档 (`xiaoxiao-slippage-model-lib-v1.md §1.4`) 缺少明确优先级声明,
  请 W8 W3 补一行. 不需要 ADR-025, spec 更新即可.
- **@小宋** (tester, E 单元): 确认 retro 发现细节 — OR 断言是 ADR-004 patch 后未
  update 还是原始就有? 本 audit 结论是原始就有 (ADR-004 不影响 R13/R17), 请小宋核实.
  若有其他 retro item 未收录请告知老韩.
- **@老郭** (架构评审, F 顾问): ADR-025 子优先级是否需要立? 老韩判断不需要 (SSOT
  已在 slippage_model.hpp + spec), 但若老郭认为 liquidity 子优先级属于重大架构
  决策需进评审, 请 48h 内回复老韩.
- **@老雷** (GM): ack 本 spec. W8 W3 老沈改完后老韩 review → merge → GM verify.

---

## §7 拒绝原因表 (OR 断言风险分级)

| test | 现状 | 风险 | 修后 |
|---|---|---|---|
| R13 EDGE_NEGATED_BY_SLIPPAGE | 三路 OR | 若 SlippageModel 参数改动导致真实结果从 EDGE_NEGATED_BY_SLIPPAGE 变为 EXCESSIVE_SLIPPAGE, 测试不报警 — 滑点模型 bug 静默 | 单一 EXPECT_EQ + 手算注释 |
| R17 EXCESSIVE_SLIPPAGE | 二路 OR | 此 test 实际触发 LOW_FILL_RATE 而非 EXCESSIVE_SLIPPAGE, OR 让 EXCESSIVE_SLIPPAGE 路径从未被真正覆盖 — 21 enum 中有一个未真实覆盖的枝 | 改为 LOW_FILL_RATE + 新增独立 EXCESSIVE_SLIPPAGE case |

**B 单元主管结论: 两个 OR 断言均确认为真 bug (文档断言, 非测试). 老沈 W8 W3 必修.**
