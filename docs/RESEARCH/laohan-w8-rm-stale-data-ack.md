---
owner: 老韩
last_review: 2026-05-28
topic: Wave 37 quick review — 老沈 b34dbad STALE_DATA >= 修 ack/拒决议
---

# Wave 37 RM STALE_DATA `>=` 修 — 老韩决议

## §1 spec 原意

**查阅路径:** `laohan-riskmanager-design-v0.2.md` §3.7 / §6 + `laohan-riskmanager-design-v0.3.md` §14.

v0.2 line 147 原文:
> "单源 freshness **> HALT 阈值** → 状态 → HALTED, evaluate 返回 REJECTED(STALE_DATA)."

v0.3 §14 表格 line 202 原文:
> "MarketState 切到 INPLAY_HOT_CRIT 且 freshness **> 200ms** → WARNING"

两版 spec 全程用严格大于 `>`. halt_ms 设计语义:

**"超过 halt_ms 才触发"** — halt_ms 是 exclusive upper bound, 不是 inclusive threshold.

INPLAY_HOT_CRIT halt_ms = 800ms 的物理含义:
- freshness = 799ms → APPROVED (< 800ms)
- freshness = 800ms → APPROVED (= 800ms, 临界值 **不触发**, `>` 语义)
- freshness = 801ms → STALE_DATA (> 800ms, 触发)

结论: spec 原意 **800ms 是 excluded**, `>` 实现正确.

## §2 ADR / spec 一致性确认

`risk_gateway.cpp` 当前实现 line 229: `if (fs_it->second > th.halt_ms)` — 与 v0.2/v0.3 spec 一致.

无独立 risk_gateway.hpp 存档 (hpp 在 include 路径未见); halt_ms 字段无 inclusive/exclusive 显式注释 — 这是 **已知注释缺口**, 本次顺带修复 (§4).

R-20 (数据时间戳) 与本 off-by-one 无交集, 不影响本决议.

## §3 决议

**Option B: 拒 `>=` 修.**

理由 (< 50 字): spec v0.2/v0.3 全程用 `>` — halt_ms 是 exclusive bound, 800ms 应 APPROVED. 老沈改 `>=` 收紧了一个 tick, 违反原意. 回退 + 测试 800ms 改 APPROVED.

## §4 派回老沈 (拒)

**老沈 W8 W3 行动项:**

1. worktree 回退: `fs_it->second >= th.halt_ms` → `fs_it->second > th.halt_ms`
2. 测试修正: 800ms case 由 STALE_DATA 改回 APPROVED (与 `>` 语义一致)
3. 保留 799ms → APPROVED, 801ms → STALE_DATA 两 case (这两个与 `>` / `>=` 均符合, 继续保留)

**老韩 W8 W3 行动项 (我自己):**

1. ADR clarification: 在相关 ADR 或 risk_gateway.cpp 顶部注释加 1 行:
   `// halt_ms is exclusive threshold: freshness > halt_ms triggers STALE_DATA (= halt_ms is APPROVED)`
2. `laohan-riskmanager-design-v0.3.1.md` 加 §14 footnote 同步 "exclusive" 标注.

## §5 不耻下问 — 回执

- @老沈 (worktree branch worktree-agent-a3e72e7d43343466b, commit b34dbad): **拒**, 请按 §4 回退 + 改测试.
- @老雷 (GM): b34dbad **等待老沈修正后** 再 merge, 当前不 merge.
- @老周: 无需架构 vote, spec 已明确, 直接决.

## §6 审计留存

| 字段 | 值 |
|---|---|
| 决议类型 | 拒 (Option B) |
| 改动文件 | `src/stcpp/risk/risk_gateway.cpp` line 229 |
| spec 依据 | v0.2 §6 line 147 + v0.3 §14 line 202 |
| 老韩签字 | 老韩 W8 Wave 37 2026-05-28 |
