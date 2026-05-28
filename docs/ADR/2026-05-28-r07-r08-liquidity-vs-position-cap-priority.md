# ADR-004: RiskGateway 21 reject enum 短路顺序 — liquidity vs position_caps

- **Owner / 仲裁人:** 老郭 (chief-architecture-reviewer)
- **Decision date:** 2026-05-28
- **Status:** Decided (B 选项, 触发老韩 W5 patch)
- **会签:** 老韩 (RM owner) / 小肖 (slippage / Kelly) / 小程 (signal) — 待回执
- **Escalation:** 老雷 (若任一会签人不同意)
- **last_review:** 2026-05-28
- **触发文档:**
  - 实现: `src/stcpp/risk/risk_gateway.cpp` §evaluate (W4 Wave 19, 老韩)
  - spec: `docs/RESEARCH/laohan-riskmanager-design-v0.3.md` §3.10 (优先级未硬定)
  - 上游 ADR: `docs/ADR/2026-05-28-gm-signoff-adr-003-closeout.md` §3 (cheap→expensive 原则)
  - latency budget: `docs/RESEARCH/laojiang-latency-budget-v1.md` (内环 500us / RM 200us)

---

## 1. 议题

`RiskGateway::evaluate()` 在 21 enum 短路链中，老韩当前实现把 **liquidity (R-15/16/17 = LOW_FILL_RATE / EXCESSIVE_SLIPPAGE / EXCEED_BOOK_DEPTH)** 排在 **position_caps (R-6/7/8/9/10 = EXCEED_PER_ORDER_CAP / EXCEED_MARKET_EXPOSURE / INSUFFICIENT_BANKROLL / DAILY_LOSS_HALT / CONSEC_LOSS_HALT)** **之前**。

老韩理由："物理 book 不够无法 fill，比 cap 软约束更早闸门"。spec §3.10 未硬定顺序，自己也建议立 ADR-004。

---

## 2. 候选

| 选项 | 描述 | 老韩立场 |
|---|---|---|
| A | liquidity 先 (当前实现) | 默认 |
| B | position_caps 先 | 待会签 |
| C | 平行 evaluate, 返 `vector<RejectCode>` 全 violation | 待会签 |

---

## 3. 仲裁: **选 B (position_caps 先)**

### 3.1 关键论点

**(1) 红线 vs 客观状态 — 哪个更"硬"**

老韩把 book depth 当 "物理硬闸门"，把 cap 当 "软约束"。**这个判定本末倒置：**

- `EXCEED_PER_ORDER_CAP` / `EXCEED_MARKET_EXPOSURE` / `INSUFFICIENT_BANKROLL` / `DAILY_LOSS_HALT` / `CONSEC_LOSS_HALT` 是**公司红线**, 是 GM + 老韩 + 老黄 + 老唐 联签的合规边界. 这五条**违反任意一条 = 公司不许下单, 无论市场如何**.
- `EXCEED_BOOK_DEPTH` / `LOW_FILL_RATE` / `EXCESSIVE_SLIPPAGE` 是**市场状态**, 表达"该订单当前不经济", 但不违反任何公司约束. 改个 size / 等会儿撤 quote 就能过.

**红线优先 = §8 第一铁律 "纪律高于收益" 的直接体现**. 在状态机层 (HALTED/DRAIN/SAFE_MODE) 已先于 liquidity, 在 cap 层却让 liquidity 插队, 内部不一致.

**(2) 多重 reject 时报哪个 — 可观察性**

实盘最常见的 "多重违规": **size 设大 → 同时超 per_order_cap + 超 book_depth**。

- 选 A: 报 `EXCEED_BOOK_DEPTH`. 策略侧看到 "市场不够深, 调小或等深度". 但**真实根因是 sizer 越过 cap**, 策略以为是市场问题去等深度, 一直等不到合法下单 → 误导.
- 选 B: 报 `EXCEED_PER_ORDER_CAP`. 策略侧看到 "我的 sizer 越线了", 直接定位 Kelly / 信号侧 size 计算 bug. 红线先暴露 = **根因优先**.

**小宋测试框架 v0.1 §1 已把 `EXCEED_PER_ORDER_CAP_basic` 列为 "第一个该跑的测试 case, 验 RM 红线最硬一条规则不通过任何'软放行'"** — test owner 的语义判断本身就是 "per_order_cap 最硬".

**(3) audit 复盘价值**

老韩 / 老唐 月度 G8 误拒率审计 (`docs/RESEARCH/laohan-riskmanager-design-v0.3.md` §15) 抽 30 笔判 "本应放行":
- 选 A: 一笔 cap 越界的订单 audit 显示 `EXCEED_BOOK_DEPTH`, 复盘人**看不出 sizer 出 bug**, 误判为 "市场流动性问题, 应放行" → G8 误拒率统计被污染.
- 选 B: cap 越界直接显形, 流动性问题在 cap 内的订单才暴露 → 两类问题**正交可分**.

**(4) 量化研究侧 — 小程 / 小蒋 调试**

策略迭代关心两类信号:
- 信号侧 bug (sizer / Kelly / EV 计算错) → 表现为 cap 越界
- 微观结构问题 (book depth 不够 / 滑点爆) → 表现为 liquidity 拒

**选 B 让两类问题在 reject stream 上线性可分**. 选 A 会让 cap 越界被 liquidity 掩盖, 策略侧只能看到 "市场不行", 看不到 "我的代码不行".

**(5) 决策 latency — 性能不是矛盾点**

| check | 估算 | 来源 |
|---|---|---|
| `check_position_caps_` | ~30ns | 4 个 atomic load + cmp + 1 个 mutex-protected unordered_map find (per_market exposure) |
| `check_liquidity_` (SlippageModel::compute) | 50-200ns p99 | `docs/RESEARCH/xiaoxiao-slippage-model-lib-v1.md` §p99 benchmark |

**position_caps 比 liquidity 便宜约 3-7x**. 按 ADR-003 closeout §3 已确立的 **"cheap→expensive short-circuit"** 原则, position_caps 本就应该排前. 选 B **同时满足**:
- 语义优先级 (红线 > 客观状态)
- 性能优先级 (便宜 > 昂贵)

老韩 v0.3 §14.4 已说 "classify+threshold < 200ns 不挤 G3 evaluate 200us 预算" — 200us 预算下两个 check 的顺序对 latency 影响 < 0.1%, **性能不是矛盾点**, 语义和审计才是.

### 3.2 否决 C (平行 evaluate / vector violation)

- C 的优势: audit 完整 violation 集合, 不丢信息.
- C 的劣势: 1) `RiskDecision.reject` 单值变 vector 要全链路改 (audit schema / WAL / Prometheus exporter / dashboard / replay), 老唐 audit_schema_v1.1 已落表, 改动半个 sprint; 2) 不 short-circuit 意味着每笔订单都跑完全 9 个 check, p99 evaluate latency 从 ~80ns (常常第一档命中) 漂到 ~500ns, 挤压老姜 latency budget; 3) 实操上 "多重 violation" 大部分是同根因 (cap + depth 都因 size 设大), 报第一个红线就够定位.
- **C 不取**. audit 完整性靠 replay 兜底 (小宋 replay framework 可重跑同 intent 验所有 check 结果, 不需要 evaluate 本身返 vector).

### 3.3 RiskDecision 签名固化

**不增 `std::vector<RejectCode> all_violations` 字段**. 沿用现有单 `RejectCode reject` 字段, 保持 ADR-003 closeout §3 "21 enum 总数硬约束" 的语义最小化原则.

如未来 G8 误拒率审计发现 "看不清多重违规" 是真痛点 (≥ 3 个月持续问题), 再走 ADR-005 加 `secondary_reject` (单 secondary, 不 vector), 不破坏当前签名.

---

## 4. 最终短路顺序 (本 ADR 法律效力)

```
state → invalid_intent (含 PIT + 8 sub_reason)
      → duplicate
      → stale_data
      → market (type / active)
      → position_caps          ← [本 ADR 调整: 6/7/8/9/10 前移]
      → liquidity              ← [本 ADR 调整: 15/16/17 后移]
      → signal (CI / negated_by_slip)
      → strategy_decayed
      → AUDIT_WAL_BACKPRESSURE (emit 失败兜底)
```

**注:** `signal.EDGE_NEGATED_BY_SLIPPAGE` (R-12) 依赖 `d.slippage_bps` 由 liquidity 阶段填值. **liquidity 在 signal 之前的顺序不变**, 只是 position_caps 插入到 liquidity 之前. signal check 读到的 `slippage_bps` 仍来自 liquidity stage, 数据流不破.

---

## 5. 老韩 W5 patch 要求

**触发 patch (P1, W5 Wave 内必出):**

1. `src/stcpp/risk/risk_gateway.cpp` evaluate 主循环: `check_liquidity_` 与 `check_position_caps_` 调用顺序互换.
2. `include/stcpp/risk/risk_gateway.hpp` header 注释: 更新 §evaluate 21 reject 优先级声明, 注明 "依 ADR-004 调整".
3. `docs/RESEARCH/laohan-riskmanager-design-v0.3.md` §3.10 / §1.2: 加 "短路顺序见 ADR-004" 引用, 不在 spec 里复刻顺序 (single source of truth = 本 ADR).
4. unit test (小宋): 加 `evaluate_priority_position_cap_before_liquidity_test.cc` — 构造同时违反 R-6 + R-17 的 intent, 验返回 `EXCEED_PER_ORDER_CAP` 而非 `EXCEED_BOOK_DEPTH`.
5. audit replay (小宋): 历史 W4 期内若已有 `EXCEED_BOOK_DEPTH` audit, replay 验是否实际同时违反 cap; 若是, 标 "ADR-004 前样本", 不计入 G8 误拒率分母.

**不触发的:**
- `RiskDecision` 签名: 不动.
- `RejectCode` enum 数值: 不动 (21 数量 + 数值都保持).
- audit schema (老唐 v1.1): 不动.

---

## 6. 与 §11 性能预算关系

- 选 B 把更便宜的 check 前移, **p50 evaluate latency 预期下降 30-100ns** (常见 cap 命中场景).
- p99 不变 (worst case 仍跑完整链).
- 老姜 latency_budget_v1 §1 RM 200us 预算未受影响, 反而宽松.

---

## 7. 完成汇报 (输出给老雷)

- **ADR-004 选 B (position_caps 先).**
- **理由三层**: (1) 红线优先 — 公司合规边界先于市场状态; (2) 审计正交 — sizer bug 与微观结构问题在 reject stream 上线性可分; (3) cheap→expensive 性能原则 (ADR-003 closeout §3 已确立) 也支持 B.
- **触发老韩 patch**: W5 Wave 内必出. 5 项变更 (代码 1 + 注释 1 + spec 1 + test 1 + replay 标记 1), 估 0.5 day.
- **不触发的**: RiskDecision 签名 / RejectCode enum / audit schema 均不动.
- **需会签确认**:
  - **老韩** (RM owner, 实现者) — 接 patch, 同意顺序调整
  - **小肖** (slippage / Kelly) — 确认 SlippageModel::compute 仍在 signal stage 前调用 (R-12 EDGE_NEGATED_BY_SLIPPAGE 数据流不破)
  - **小程** (signal owner) — 确认 cap 优先在策略调试侧更优
  - **老钱** (CPO) — 知会, 无产品侧 blocker 即可
- **任一会签人不同意 → escalate 老雷**, 不擅自启 patch.

---

## 8. 失败回滚条件

若 W5 patch 上线后, G8 误拒率连续 2 个月 > 5% **且根因可归到 ADR-004 顺序调整** (例: cap 误前移导致 sizer 在 liquidity 不足时未收到正确信号), 走 ADR-005 复议, 不允许悄改回 A.
