# ADR-023: IC 自测唯一裁判 (撤回 ADR-022 Tester review 层)

- **ID:** ADR-023
- **Date:** 2026-06-W3 (W8 W1, ADR-022 立后数小时撤)
- **Status:** Accepted (老板 verbatim 第三次校正)
- **Supersedes:** ADR-022 (Tester review 层 — 撤回)
- **Related (also superseded):** ADR-020 (IC ≠ Tester 已 ADR-022 撤)

---

## 1. 老板 verbatim 三连校正 (W6 W3 → W8 W1)

| 时点 | 老板 verbatim | GM 误读 | 实际意图 |
|---|---|---|---|
| W6 W3 | "自己写自己当裁判不合理" | GM 立 ADR-020: IC 不写测试 | (仅指出问题, 未指出方案) |
| W8 W1 (1) | "测试自己写就好了, 不需要别人写, 别人不了解你的代码就无法写出优秀的测试" | GM 立 ADR-022: IC 自测 + Tester review 双层 | (重新强调 IC 写, 但仍未明示 Tester) |
| W8 W1 (2) | **"Tester review 不需要"** | (无误读余地) | **IC 自测唯一裁判, 无 Tester review 层** |

## 2. ADR-023 决议 (最终版)

### 2.1 IC 角色 (回 W3-W7 模式, 唯一裁判)

- IC 写代码 + 写测试 + **自我裁判** (W3-W7 模式)
- 无 Tester review 层
- 派单 prompt: "本地 cmake + ctest 必过才回汇" (GM 错 #11 enforce 维持)

### 2.2 撤回 Tester 角色 (W7 末-W8 W1 实验性 + 立刻撤)

- ❌ 小宋 / HC-08 / 老彭 **不再作为 Tester review IC 测试**
- ❌ ADR-020 + ADR-022 Tester review 层 全撤
- 小宋角色回归 W3-W7: **test-replay-engineer 写自己 owner 的 integration + sim + replay + chaos** (与其他 IC 同地位, 不 review 别人测试)
- 老彭角色回归 W3-W7: **betting-industry 校准 historical 数据** (不 review 其他 IC)
- HC-08 招聘范围调整: **仅 integration + chaos + UAT** (不 review unit test)

### 2.3 小宋 W8 W1 retro 32 盲点 + 4 IC bug 候选处置

**老板"Tester review 不需要"后, 这些 retro 产出如何处理?**

**决议: 信息可选参考, 不强制流程**:
- 小宋 retro 报告作为 **IC 可选 review checklist** 存档 (`docs/RESEARCH/xiaosong-w8-retro-blindspots.md`)
- IC 自己决定是否吸收 (自我审查的辅助参考)
- 不强制派回 IC 修
- 不进 KPI (W7 末 §10 Tester KPI 全撤)
- 4 IC bug 候选 (RM-01/02 OR 断言 + AUDIT-01 弱断言 + SIGNER-01 future-ts) — 改为 GM 个人 backlog (M2 末 retro 时 IC 自己 review 是否修)

### 2.4 W8 Wave 34 影响

- 老孙 libsodium cpp 完成 ✓ (worktree 隔离 OK, ADR-021 维持)
- 老孙 W8 W2 补 unit test (按 W3-W7 模式 IC 自测, ADR-020/022 撤 后 IC 自测)
- 小宋 W8 W2 起 N-B Tester wave **取消**, 改为小宋写自己 owner (integration + sim)
- 老高 PR v1.5 `ic_no_self_test.py` (反转检 IC 漏写测试) 维持 ✓
- 老板 8 ACK 落地确认 (老胡 W7 周报) 中 ADR-020 项**全撤回**, 不再 enforce

## 3. 维持的 ADR

- ✓ **ADR-021 worktree 物理隔离** (W8 起强 enforce) — 不撤, 与 IC 自测无关, 仍解决文件抢占
- ✓ ADR-005 §3.4 文件 ownership lock — 不撤
- ✓ ADR-018 §X build switch 规范 — 不撤
- ✓ 老高 PR v1.4 + v1.5 grep — 维持 (除 `ic_no_self_test.py` 反转)
- ✓ GM 错 #11 build+ctest hard 约束 — 维持 (IC 写测试 + 跑通才回汇)

## 4. CLAUDE.md §7 + §10 撤回

撤回 ADR-020/022 加的:
- ❌ 铁律 #11 "IC 不自测"
- ❌ 铁律 #11 "IC 必须自测 + Tester review 独立"
- ✓ 改为铁律 #11 (final): **IC 写代码 + 写测试 + 自测过才回汇** (W3-W7 模式 + GM 错 #11 enforce)

## 5. 老胡周报 §10 调整

- ❌ 撤回 Tester KPI (review 通过率 / 盲点发现数 / 派回 SLA)
- ✓ 保留 IC 自测覆盖率 (W10 末 ≥ 80%, IC 自己 audit + 补盲点)
- ✓ 保留 GM 错 #11 build+ctest 验证 KPI

## 6. GM 错 #18 教训

GM 1 天 3 ADR (020 → 022 → 023), 频繁修订. 教训:

1. **老板 verbatim 解读要等明确意图** — W6 W3 "自己当裁判不合理"含糊, GM 不应自己猜方案, 应该问老板"不合理的处理建议是?"
2. **立 ADR 前快速验证** — ADR-020 立后 1 天就被撤. 立项前应该 ping 老板"我准备立 ADR-020 撤 IC 自测, ack?"
3. **不要"为立而立"** — ADR 是稳定决议, 1 天撤 = 不稳定 = GM 焦虑写 ADR

永久 enforcement:
- **GM 立重大 ADR 前必须**:
  1. Verbatim 复述老板原话
  2. 列 2-3 候选方案
  3. 问"老板倾向哪个?"
  4. 等老板 ack 再立 ADR
- 老胡周报 §13 加 "ADR 撤回率" KPI (W8 起目标 < 1/sprint)

## 7. 老板 verbatim 三次入约束

W6 W3: "自己写自己当裁判不合理" (诊断, 不是方案)
W8 W1 (1): "测试自己写就好了, 不需要别人写, 别人不了解你的代码就无法写出优秀的测试" (方案: IC 写)
W8 W1 (2): "Tester review 不需要" (Tester 层撤回)

**最终模式:** W3-W7 模式恢复 (IC 写代码 + 写测试, 自测唯一裁判), 加 GM 错 #11 enforce build+ctest 验证.

---

**最后更新:** 2026-06-W3 by 老雷 (GM, 错 #18 触发)
**Supersedes:** ADR-022 (Tester review) + ADR-020 (IC ≠ Tester)
