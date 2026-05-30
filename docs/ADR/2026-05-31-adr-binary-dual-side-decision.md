# ADR: 二元市场双边盘口决策

- **status:** Accepted (Phase A 落地; Phase B/M2 分期)
- **date:** 2026-05-31
- **owner:** 老周 (架构主权) + GM 老雷 (执行)
- **last_review:** 2026-05-31
- **会签:** 老周 (架构) / 小袁 (微观结构) / 小梁 (策略·Sharpe·Kelly 主权) / 老韩 (RM 主权·Phase B checklist) / 老郭 (首席架构评审 — APPROVE-with-conditions)
- **关联:**
  - laozhou-binary-dual-side-arch-v1.md (架构主设计)
  - xiaoyuan-binary-dual-side-decision-v1.md (微观选边)
  - xiaoliang-binary-dual-side-strategy-v1.md (策略仲裁)
  - laohan-phaseB-buy-no-rm-checklist-v1.md (Phase B RM checklist)
  - 2026-05-31-laoguo-binary-dual-side-arch-review.md (架构评审 + C1-C6)

---

## Context

老板设计原则 (逐字, §8.1 纪律#1 粘原文):
> 「一个二元市场应该既持有 yes 的快照，也持有 no 的快照，触发 WSS 订阅时，只刷新单边的，这个没问题。但是进入决策时，我们一定要带入这个盘口的信息，而不仅仅是单单一边的信息。」

现状缺口: `paper_loop` 只对 token0(YES) 决策, token1(NO) 仅取一个 mid 标量做 de-vig, NO 完整 book 从不进决策, outcome/side/token_side 硬编码 YES/Buy。YES 高估即跳过 → 放弃买被低估 NO 的正期望。

**策略本质 (小梁 Sharpe/Kelly 主权裁定):** YES/NO **不是独立 alpha、不是翻倍机会** —— 同一比分信号的换边表达 (fair_YES + fair_NO = 1)。可交易 mispricing 来自两边市场价各自独立错价, 通常单 tick 仅一边错 (小袁实测 0 arb hit)。价值 = YES 高估时改买被低估 NO, **捡回当前丢弃的机会**, 非下双倍单。

---

## Decision

1. **存储层 hub 不动** (老郭确认: per-token SWMR double-buffer 已正确满足「双边存储 + 单边刷新」; condition 级聚合会破单边刷新无锁性, 否决)。
2. **新增决策侧值聚合 `paper::BinaryMarketSnapshot`** (SideView yes/no, 各持完整 `OrderBookFeatures`), 决策线程 (loop_thread_) 栈上由两次 `hub_.Read()` 组装, 零锁零存储 (R-12 不触碰)。
3. **两边各持独立完整 4ts, 不合并 4ts 链** —— freshness 判断交决策层 (R-20 per-data-source 透传精神)。
4. **TickOne 改 per-condition** `TickOne(const BinaryMarketSnapshot&)` + `SelectSide` 选边 + 字段参数化 (outcome/side/token_side/token_id/vord/apply_fill + book_depth 经 traded.book)。
5. **分期:**
   - **Phase A (M1, 本 ADR 落地):** 结构地基 + `SelectSide` 桩恒 {Yes, Buy} → **逐位等价回归** (行为零变)。
   - **Phase B (M1, 老韩 RM checklist B-1..B-8 绿后):** `SelectSide` 真双边选边 (各边算 fair_NO=1−fair_YES / edge_ci / Kelly f*, 选 f* 大者; 绝不同时下两边) → **买 NO**。**RM 本体零改** (老韩核实: condition_exposure 按 condition_id 聚合天然合并两边 notional, 满足 cap 合并; 9 gate token-agnostic)。
   - **M2:** sell-to-open 空头 (side=Sell), 需老韩重裁 condition cap signed-sum 语义 (C1)。

---

## Consequences / 放行条件 (老郭 C1-C6)

| # | 条件 | 状态 |
|---|---|---|
| C1 | per-condition cap signed-sum: M1 买入天然合并 (零改 RM); **M2 sell-to-open 负 delta 会静默架空 cap → M2 前必重裁** | M1 安全; M2 红线已挂老韩 |
| C2 | book_depth/price/4ts 选边切换 (老周 5 处 + 第 6 处 intent.book_depth_l1) | ✅ Phase A 经 `feat=traded.book` 别名天然覆盖 |
| C3 | 决策用陈旧对边 book 但 intent 4ts 只透传被交易边 → 旁路留痕 (audit 看着新鲜实则用陈旧对边) | Phase B 落 (de-vig 用对边时记 freshness) |
| C4 | 逐位等价回归 + 反向桩测试 (验选 NO 全字段切对边) | Phase A: 30 paper_loop 测试逐位等价 ✅; **反向-fill 测试延 Phase B** (M1 harness sizing 恒返 suggested_notional=0 + NO fair 是 Phase B 才实现 → Phase A 建不起 NO intent; 反向测试需 Phase B NO 逻辑) |
| C5 | 命名消歧: `paper::BinaryMarketSnapshot` (决策入参) ≠ `state_provider BinaryMarketBookView` (ADR-040 看板) | ✅ 用 `paper::BinaryMarketSnapshot` |
| C6 | 术语统一: 买 NO = Phase B (M1 层); sell-to-open = M2 | ✅ 本 ADR + 各 doc |

**改动收敛:** Phase A 仅 `paper_loop` + 1 新头文件 `binary_market_snapshot.hpp`; hub/RM/sizing/signer/matcher/ledger 全不动。

**Phase A 验收 (硬门禁):** `SelectSide` 桩恒 YES/Buy → paper daemon fills/rejects/quote 与改造前逐位等价。全量 serial 1071/1071 绿 (30 paper_loop 测试行为不变)。

---

## 待 Phase B (老韩 checklist + 小袁/小梁逻辑)

- `SelectSide` 真选边 (fair_NO=1−fair_YES 严格推导 + 各边 edge_ci/Kelly f* + per-book slippage)
- 买 NO 全链路: 12 个 RM 专项单测 (test_risk_gateway_buy_no.cpp, 含 T-NO-9 合并越 cap) + 3 paper_loop 集成测 + C4 反向-fill 测试
- C3 对边陈旧留痕
- 喂数侧选边切换 (book_depth/price/ts → NO book)
