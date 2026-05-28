# ADR-022: IC 自写测试 + Tester review 双层 (撤回 ADR-020)

- **ID:** ADR-022
- **Date:** 2026-06-W3 (W8 W1, ADR-020 立后 1 天撤)
- **Status:** Accepted (老板 verbatim 二次校正)
- **Supersedes:** ADR-020 (IC ≠ Tester 职责分离 — 撤回)

---

## 1. 老板 verbatim 校正

> "测试自己写就好了, 不需要别人写, 别人不了解你的代码就无法写出优秀的测试"

(W8 W1, 2026-06-W3, GM 错 #17 触发)

## 2. ADR-020 撤回背景

ADR-020 (W7 末立): IC ≠ Tester 职责分离, IC 不写 ctest, Tester (小宋/HC-08/老彭) 写测试.

**GM 错 #17:** 误判老板 W6 W3 "自己写自己当裁判不合理" 的真意.

老板真意 (W8 W1 校正后明确):
- **不反对 IC 写测试** (写代码的人最懂代码细节)
- **反对的是 IC 自己当唯一裁判** (没有 review / 没有盲点扫描)
- 正确模式: **IC 自测 + Tester review** (双层质量保证)

## 3. 决策 (ADR-022)

### 3.1 IC 角色 (回到 W3-W7 模式 + Tester review 层)

- **IC 写代码 + 写测试** (恢复 W3-W7 模式)
- IC 测试覆盖自己的代码 case (最了解代码细节)
- IC 派单 prompt 仍带"本地 cmake+ctest 必过才回汇" (GM 错 #11 enforce)

### 3.2 Tester 角色 (变更: 不写测试, 改 review)

- **Tester (小宋 / HC-08 / 老彭) 不直接写测试**
- 角色改为:
  - **Review IC 写的测试**, 找盲点
  - **派回原 IC 补**, 由 IC 自己写补丁测试 (因 IC 最懂代码)
  - 提供 review checklist (覆盖矩阵 / 边界场景 / 异常 path / 并发)
- **小宋 W8 Wave 34 retro 审查仍有价值** (32 盲点 + 4 IC bug 候选) — 但**派回原 IC** 修, 不自己写

### 3.3 双层质量保证

```
Layer 1 (IC 自测):
  - IC 写代码时配套写 unit test
  - 覆盖自己设计的 happy path + 主要 case
  - 本地 cmake + ctest 必过才回汇

Layer 2 (Tester review):
  - Tester 接 IC PR 后 review 测试 (不写新测试)
  - 找盲点 (并发 / 边界 / 异常 / failure injection / DST 等)
  - 派回原 IC 补 (IC 自己写新 case)
  - Tester 提供 review checklist (per module domain)
```

## 4. 撤回项

### 4.1 立刻撤回

- ❌ ADR-020 §2 "IC 角色不写测试" — 改回 IC 写测试
- ❌ ADR-020 §3 "2 阶段 Wave (N-A IC, N-B Tester)" — 改回单 wave (IC 写代码 + 测试)
- ❌ ADR-020 §11 "IC 派单 prompt 不要求写 ctest" — 改回必含 ctest hard 约束
- ❌ 老高 W8 Wave 34 落的 `ic_no_self_test.py` grep — 改回**只 grep IC 是否漏写测试** (反向)

### 4.2 保留 (有价值)

- ✓ Tester 角色定义 (小宋 / HC-08 / 老彭) — 但职责改为 review
- ✓ 小宋 W8 W1 retro 审查的 32 盲点 + 4 IC bug 候选 — **派回原 IC** 补
- ✓ 测试覆盖率目标 70% → 80% — 仍守, 通过 IC 补盲点实现
- ✓ ADR-020 §9 GM 错 #16 教训 (误判老板原意) — 升级为 GM 错 #17 (ADR-022 触发)
- ✓ Tester KPI (老胡周报 §10): 改为
  - IC 测试覆盖率 (期望 ≥ 80%)
  - Tester review 发现盲点数 (期望 > 0)
  - 盲点派回 IC 修复 SLA (期望 ≤ 3 天)

## 5. W8 Wave 34 影响

- **老孙 libsodium cpp 实施** (本 wave 仍在跑):
  - 原 prompt: "不写 ctest" (按错的 ADR-020)
  - 实际: 老孙完成后, **W8 W2 老孙自己补测试**, 而非派小宋
  - 老孙 W8 W2 N-A 补丁: 写 Ed25519 真签 + IPC + audit_wal_kind 测试
  - 小宋 W8 W2 改为 review 老孙测试 + 找盲点 + 派回老孙补

- **小宋 retro 审查 32 盲点**:
  - 改派回原 IC 补 (老韩补 RM, 老唐补 audit, 小蒋补 signer, 老李补 PolymarketClient, 老王补 WAL, 小段补 Goalserve, 小石补 SPSC, 小冯补 IngestRaw, 小卢补 signal, 小邓补 ML, 小董补 gate)
  - 小宋持续 review + Sprint-3 持续盲点扫描

- **小宋 4 IC bug 候选** (RM-01/02 OR 断言 + AUDIT-01 弱断言 + SIGNER-01 future-ts):
  - 派回 老韩 (RM) / 老唐 (AUDIT) / 小蒋 (SIGNER) W8 W2 修

## 6. 老高 PR v1.5 ic_no_self_test 反转

**Old (ADR-020 v1):** 检测 IC 同时改 src + tests → WARN (不允许 IC 自测)

**New (ADR-022 v2):** 检测 IC 改 src 但**没改对应 tests/** → WARN (IC 必须自测, 漏写测试报警)

老高 W8 W2 出 v1.5.1 修补 (反转 grep 逻辑).

## 7. CLAUDE.md §10 + §7 更新

撤回 W7 末加的 "IC 不自测" 铁律. 改为:
- 铁律 #11: **IC 必须自测** (写代码同 Wave 配套 unit test)
- 铁律 #12: **Tester review 必须独立** (Tester 不能与 IC 同一人, 不能依赖 IC 自报"X/X PASS")

## 8. GM 错 #17 教训

- 老板说"自己当裁判不合理" — 实际意思是"裁判不止一个"
- GM 把"双层裁判"误读为"裁判换人" (IC 不写, Tester 写)
- 教训:
  - 老板 verbatim 解读不能跳逻辑 ("不合理"≠"不允许")
  - 立 ADR 前应与老板对齐核心诉求
  - W7 末 ADR-020 决策仓促, 没等老板二次 ack
- 永久 enforcement:
  - GM 立重大 ADR (跨流程影响) 前必须 verbatim 复述 + 老板二次 ack
  - 老胡 周报 §13 加 "GM ADR 立项前老板 ack 流程" KPI

## 9. KPI 更新 (老胡周报 §10 v2)

| KPI | ADR-020 (撤) | ADR-022 (新) |
|---|---|---|
| IC 测试覆盖率 | n/a (IC 不测) | ≥ 80% (W10 末) |
| IC 自测违规次数 | 期望 0 (IC 不测) | n/a (IC 必测) |
| Tester 发现盲点数 | (n/a, Tester 写测试) | > 3/wave |
| 盲点派回 IC 修 SLA | n/a | ≤ 3 天 |
| Tester review 通过率 | n/a | ≥ 90% (W10 末) |

## 10. 老板 verbatim 入约束 (双 verbatim)

W6 W3: "自己写自己当裁判不合理" (= 需要多层裁判, **不是不写**)
W8 W1: "测试自己写就好了, 不需要别人写, 别人不了解你的代码就无法写出优秀的测试"

任何后续派单 / ADR 必须遵守本 ADR-022 (撤 ADR-020).

---

**最后更新:** 2026-06-W3 by 老雷 (GM, 错 #17 触发)
**Supersedes:** ADR-020 (W7 末立, W8 W1 撤)
