# ADR-020: IC 与 Tester 职责分离 (写代码的不当自己裁判)

- **ID:** ADR-020
- **Date:** 2026-06-W3 (W7 冷静周末)
- **Status:** Accepted (老板 verbatim 拍板)
- **触发:** 老板原话 verbatim: "让他们不必测试, 完成之后专门测试人员写测试. 自己写自己当裁判不合理"

---

## 1. 背景

W3 → W7 公司一直派 IC 同时写 cpp + ctest 测试. 例:
- 老韩 W4 写 risk_gateway + 写 46 test
- 老唐 W4 写 audit_emitter + 写 24 test
- 小蒋 W4 写 paper_signer + 写 27 test
- 小卢 W6 写 single_instance + 写 7 test

**问题**: IC 自己写代码 + 自己写测试 = **自己当裁判**, 违反质量保证原则:
- IC 倾向写"我代码能过"的测试 (confirm bias)
- 测试覆盖盲点 = IC 思维盲点
- 边缘场景 / 异常路径 / 真实业务流靠 IC 想不全
- "测试通过"≠ "代码正确", 需要独立 tester 视角

**用户原话直接纠正:** "自己写自己当裁判不合理"

---

## 2. 决策

**IC 与 Tester 职责物理分离:**

| 角色 | 写 cpp 代码? | 写 ctest 测试? |
|---|---|---|
| **IC** (老李/老王/老唐/老沈/小卢/小蒋/小段/小冯/小肖/小袁/...) | ✓ 写 | ✗ **不写** |
| **Tester** (小宋 / HC-08 qa-integration / 老彭 历史校准) | ✗ 不写产品 cpp | ✓ **专写测试** |
| **顾问 review** (老高 PR / 老何 footgun / 老郭 architect) | ✗ | ✗ (review only, 不写) |
| **主管** (老周 / 老韩 / 小梁 / 小余 / 老胡) | ✗ (cpp = 0 ADR-005) | ✗ (spec only, 不写) |

**例外:**
- 单元测试**框架 / fixture** (e.g. mock object / test harness): 可由 IC 提供占位 stub, Tester 完善
- IC owner 的"自我 sanity check"(e.g. 简单 `static_assert` ABI lock): 允许, 但不算正式 ctest 测试
- 紧急 P0 hotfix (e.g. BUG-W5-001): IC 修复 + 写最小 regression test, Tester W+1 内审查/补全

## 3. 派单流程变革 (Wave 设计)

**Old (W3-W7):** Wave N 派 IC 写 cpp + 写测试 (一 Wave 完成)

**New (W8 起):** **2 阶段 Wave:**

```
Wave N-A (IC 写代码):
  - IC 派单 prompt 只要求写 cpp + header + CMake (不要求写 ctest)
  - Acceptance: "cmake --build 通过 + 严格 lint 0 warning + 无 Tester 测试"
  - IC 回汇时**禁止**提供"我写了 X 个测试"叙述
  
Wave N-B (Tester 写测试, 同 Sprint 内):
  - Tester 派单 prompt: "为 IC X owner 的模块 Y 写 ctest 测试 (列覆盖矩阵)"
  - Tester 独立思考边界 / 异常 / 业务场景 (不参考 IC 自测)
  - Tester acceptance: ctest N/N PASS + 覆盖率 ≥ 80% (gtest --gtest_filter)
  - Tester 发现 IC bug 派回 IC 修 (上游不动 tester)
```

**Wave 间隔:** 同 Sprint 内 (IC 完成后 1-3 天内 Tester 接), 避免 W6 W3 "10 hotfix 越权" 重演.

## 4. Tester 角色清单

**当前 Tester (W7):**
- 小宋 (test-replay-engineer, E-028): unit + sim + integration 主力
- HC-08 qa-integration-engineer (老胡 + 小宋 双联签, 2026-08-15 入职): integration + UAT + chaos
- 老彭 (betting-industry-expert, E-030): 历史回测数据 (validation)

**HC-08 入职后:**
- 小宋: unit + sim
- HC-08: integration + chaos + UAT + perf
- 老彭: 量化历史数据 validation

## 5. 与 ADR-005 / ADR-010 / ADR-015 协同

- **ADR-005 派单 3 层**: GM → 主管 → IC → **+ Tester (W8 起加层)**
- **ADR-010 测试代码分级**: 仍守 (生产 strict + 测试 grandfather), 但**测试代码归 Tester 不归 IC**
- **ADR-015 vCPU pin**: Tester 写 bench (老姜 现在已是 perf Tester 角色)

## 6. 历史回填 (W3-W7 已写 IC 自测如何处理)

W3-W7 IC 已写测试 (~400+ tests in tests/unit/) 不全部撤回, 但:
- **W8 起新代码强 ADR-020** (IC 派单 prompt 含"不写测试" hard约束)
- **历史测试 W8-W10 由小宋 / HC-08 retro 审查**, 发现盲点补测试
- 现有 IC 自测视为"placeholder", Tester 视角 retro 补全 (期望覆盖率从当前 ~70% 提到 ≥ 80%)

## 7. CI enforcement (老高 PR v1.4 → v1.5)

**新 grep:**
- `tests/ci_grep/ic_no_self_test.py`: PR 包含同 IC owner 的 `src/<X>.cpp` + `tests/.../<X>_test.cpp` 改动 → WARN (除非 Tester co-sign)
- PR template 加 checkbox: "本 PR 是否 IC 自测? 若是, 已 ack ADR-020 例外 + Tester 同 Sprint 内接 (issue ref)"

## 8. KPI (老胡周报 §10 Tester KPI)

- IC 自测违规 PR 数 (期望 0, ADR-020 enforce 起)
- Tester 接单 SLA (IC 完成 → Tester 派单, 期望 3 天)
- Tester 发现 IC bug 数 (W8 起 baseline, 期望 > 0 — 说明 ADR-020 价值)
- 测试覆盖率 (gtest_filter 跑全 + lcov, 期望 ≥ 80%)

## 9. GM 错 #16 (本 ADR 触发)

- **时间:** 2026-05-28 起到 2026-06-W3 (跨 W3-W7 持续 7 周)
- **错在哪:** GM 派单 prompt 一直让 IC 自写测试 + 自报"X/X PASS", 没建 Tester 角色分离
- **是用户直接纠正的:** "自己写自己当裁判不合理"
- **影响:** ~400+ tests 由 IC 自写, 测试质量 / 覆盖率 / 盲点未知, W8-W10 小宋 retro 审查
- **永久 enforcement:** ADR-020 (本) + CLAUDE.md §7 加铁律 #11 + 老高 PR v1.5 grep + 老胡周报 §10

## 10. W7 Wave 33 处理 (过渡)

- Wave 33 已派出, 不撤回
- 老唐 prompt 让加 1 个 test case (`AuditEmitterPool.PublicEmitWithInjectedChain`) — **撤回该要求, 改派小宋 W8 W1 接**
- 老王 / 老高 / 老沈 / 老郭 / 老徐 / 小宋 不写 cpp 测试 ✓ (本来就 doc/spec/grep, 不冲突)
- W7 Wave 33 仍以 GM 静态等齐, 整合后 W8 W1 派小宋接所有 IC 写的占位测试 retro

## 11. 落地动作

- [x] ADR-020 立 (本文件)
- [ ] CLAUDE.md §7 加铁律 #11 "IC 不自测" (GM W7 末落)
- [ ] CLAUDE.md §10 sub-agent 操作约定加 "IC 派单 prompt 不要求写 ctest" (GM W7 末落)
- [ ] 老高 PR v1.5 加 `ic_no_self_test.py` grep + PR template checkbox (W7 末或 W8)
- [ ] 老胡 周报 §10 Tester KPI (W8 W1 首期数据)
- [ ] 小宋 W8 W1 起接所有 W3-W7 IC 写的测试 retro 审查 (cascade 计划)
- [ ] HC-08 qa-integration JD 加 ADR-020 职责 (老胡 + 小林)

## 12. 用户原话 verbatim 入约束

> "让他们不必测试, 完成之后专门测试人员写测试. 自己写自己当裁判不合理"

任何后续 派单 / 制度调整 必须遵守本 ADR.

---

**最后更新:** 2026-06-W3 by 老雷 (GM, 触发 GM 错 #16 立)
