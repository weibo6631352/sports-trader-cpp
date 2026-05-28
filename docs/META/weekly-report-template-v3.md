# Sprint-N WeekN 周报模板 v3

- **Owner:** 老胡 (E-026, pm-project-manager, E 单元主管)
- **触发:** ADR-020 (IC 不自测) + ADR-021 (worktree 隔离) W8 起 enforce — 新增 §10 Tester KPI + §11 worktree KPI
- **v2 → v3 变更摘要:**
  - 加 §10 Tester KPI (ADR-020 §8 配套, W8 首期)
  - 加 §11 worktree KPI (ADR-021 §7 配套, W8 首期)
  - §7 标题细化为 "Build Verification + Hotfix KPI"
  - §6 下周 backlog 格式加 Wave N-A / N-B 2 阶段标注 (ADR-020 派单流程)
- **生效:** 2026-06-W3 (W8 W1 起首套)
- **关联:**
  - `docs/META/weekly-report-template-v2.md` (v2 原文)
  - `docs/ADR/2026-06-W3-adr-020-ic-tester-separation.md` §8
  - `docs/ADR/2026-06-W3-adr-021-worktree-isolation.md` §7

---

## 周报标题命名

`docs/SPRINTS/sprint-NN-wN-progress.md` (例: `sprint-02-w7-w3-progress.md`)

---

## §1 本周完成

- 列本周交付的 ticket (Agreed / Compromised / Escalated 三态明示)
- 按主管 / 单元分组 (ADR-005 后, 不再按 owner 个人, 按 5 单元)
- 每条带 commit hash / 行数 / 测试数 / 红线 enforce 状态
- **格式示例:**
  ```
  ### A 系统工程部 (老周)
  - W7-A-01 老唐 AuditEmitterPool 重构 (275 行, 1 sanity test ✓, commit 9d3f35d, ADR-020 例外 sanity_check)
  ```

---

## §2 本周阻塞

- blocker 升级链: IC → 主管 (4h ack) → 老胡协调 (48h) → 老雷 (P0 2h)
- 列每条 blocker 当前层 + ack 状态 + ETA

---

## §3 风险登记 update

- 引用 `docs/RESEARCH/laohu-risk-registry-vN.md` 最新版本号
- 列本周 update 的风险 (新增 / 升级 / 降级 / 关闭)
- Top 5 风险快照 (I × P)

---

## §4 本周 SSOT 版本演进 (R-41 + GM 错 #9 配套, 永久 enforcement 第 3 条)

### §4.1 本周 vN → v(N+1) 推翻清单

| owner | 单元 | SSOT 文档 | vN | v(N+1) | 推翻结论 | 影响下游 |
|---|---|---|---|---|---|---|
| ... | ... | ... | ... | ... | ... | ... |

### §4.2 跨 wave 决议追溯

### §4.3 数据源 owner 版本变更 watch list

---

## §5 主管派单 KPI (ADR-005 §8)

### §5.1 主管派单覆盖率

| KPI | 目标 | 本周实测 | 状态 |
|---|---|---|---|
| 主管派单覆盖率 | 100% | N% | TBD |
| Sonnet 派单率 (ADR-009 v2) | 100% | N% | TBD |
| ctest 通过率 | 100% | N/N | TBD |
| 主管帽 cpp 行数 | = 0 | 0 | TBD |

### §5.2 协商会次数

| KPI | 目标 | 本周实测 | 状态 |
|---|---|---|---|
| 协商会次数 | 每周 1 次 | N 次 | TBD |

---

## §6 下周 backlog

- 列下周 (W+1) 派单, 按 5 单元 + 主管分组
- **ADR-020 派单 2 阶段标注:** Wave N-A (IC 写码) / Wave N-B (Tester 写测试)
- 标 P0 / P1 / P2 + Agreed / Compromised / Escalated
- 跨单元接口 ASK 列表

---

## §7 Build Verification + Hotfix KPI (GM 错 #11 配套, 永久 enforcement)

| KPI | W6 W2 基线 | W7 实测 | W8 目标 | 状态 |
|---|---|---|---|---|
| GM hotfix 率 | 62.5% | 0% | 0% | TBD |
| 派单 prompt build+ctest 约束覆盖率 | 0% | 100% | 100% | TBD |
| GM hotfix 次数 | 10 次 | 0 | 0 | TBD |

---

## §8 Commit Hygiene KPI (GM 错 #12 配套)

| KPI | W6 W2 基线 | W7 实测 | W8 目标 | 状态 |
|---|---|---|---|---|
| 误推临时 build dir 次数 | 1 次 | 0 | 0 | TBD |
| GM 代修 src/include 次数 | N | 0 | 0 | TBD |

---

## §9 GM 代修次数 (GM 错 #13 配套)

| KPI | W6 W2 基线 | W6 W3 | W7 | W8 目标 |
|---|---|---|---|---|
| GM 越权代修 src/include | 10+ | 2 | 0 | 0 |

---

## §10 Tester KPI (ADR-020 §8 配套 — W8 首期)

> **强制约束 (ADR-020):** W8 起 IC 不自写测试, Tester 专写测试. 本节追踪 ADR-020 执行质量.

| KPI | W7 基线 | W8 W1 | W8 目标 | 状态 |
|---|---|---|---|---|
| IC 自测违规 PR 数 | 1 (老唐 sanity_check, ADR-020 §2 例外) | TBD | 0 | TBD |
| Tester 接单 SLA (IC 完成 → Tester 派单, 期望 ≤3 天) | W8 W2 首期 | — | ≤3 天 | 待建基线 |
| Tester 发现 IC bug 数 | W7 0 (ADR-020 前) | TBD | W8-W10 retro 期望 ≥3 | 待建基线 |
| 测试覆盖率 (全仓 lcov) | ~70% (W7 IC 自测估算) | TBD | W10 末 ≥80% | 待建基线 |

**W8 W1 初始行动:**
- 小宋 W8 W1 retro 审查 W3-W7 400+ IC 自测 (ADR-020 §6 cascade)
- 所有新 IC 派单 prompt 必含 "不写 ctest 测试" hard 约束

---

## §11 worktree KPI (ADR-021 §7 配套 — W8 首期)

> **强制约束 (ADR-021):** W8 起所有 Agent tool 调用强制 `isolation: "worktree"`. 本节追踪执行率.

| KPI | W7 基线 | W8 W1 实测 | W8 目标 | 状态 |
|---|---|---|---|---|
| worktree 使用率 | 0% (Wave 33 GM 错 #15, 老板单次容忍) | 100% (本 wave 34 5 派单全 worktree) | 100% | 绿 |
| 文件抢占次数 | 1 (老唐 ninja cache 脏, 自然恢复, 非 GM 越权) | 0 | 0 | TBD |
| P0 例外 (不带 worktree) 次数 | — | 0 | ≤1/week | TBD |

---

## §12 (原 §9) 主管月度轮值 GM 助理 status (ADR-005 §4.3)

- 当月轮值主管 (6 月老周 → 7 月老韩 → 8 月小梁 → 9 月小余 → 10 月老胡)

---

## §13 完成汇报

- 本周关键交付 1-3 句话摘要
- W+1 启动 / 下次主管周同步预告
- 周报 owner 签字 (老胡) + 归档 (小米)

---

## 模板使用约束

1. **强制段:** §1 / §2 / §3 / §4 / §5 / §6 / §7 / §8 / §9 / **§10 (Tester KPI — ADR-020 W8 起)** / **§11 (worktree KPI — ADR-021 W8 起)** / §13
2. **可选段:** §12 (仅当月有轮值主管时填)
3. **每周五 EOD:** PM 老胡交周报 → 周日 EOD 5 主管 + 老郭 24h ack → 周一上午主管周同步引用
4. **ADR-020 派单约束:** §6 backlog 列派单必须标 Wave N-A (IC 写码) / Wave N-B (Tester 写测试)
5. **ADR-021 worktree 约束:** §11 每周必填 worktree 使用率; P0 例外必须显式标 "P0 例外, 老板 ack"

---

**最后更新:** 2026-06-W3 by 老胡 (E-026, PM)
**生效:** 2026-06-W3 (W8 W1 起首套)
**v2 → v3 触发:** ADR-020 (IC 不自测) + ADR-021 (worktree 隔离) W8 enforce
