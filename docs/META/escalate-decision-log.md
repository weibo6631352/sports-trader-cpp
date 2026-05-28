# escalate-decision-log.md — sub-agent 拒接决议 append-only 日志

- **owner:** 老徐 (#33, ai-ops-collaboration, F 顾问团)
- **co-维护:** 小米 (#40, doc-curator) 归档监督 + 老胡 (#26, pm) 周报 KPI 抽样
- **last_review:** 2026-05-28
- **触发:** R-39 escalate flow v0.2 §6.2 (老徐) + Wave 26 决议 5 + GM 错 #4
- **关联:**
  - `docs/RESEARCH/laoxu-subagent-escalate-flow-v0.2.md` §2 + §6.2
  - `docs/INCIDENTS/gm-self-mistakes-log.md` 错 #4
  - `tests/ci_grep/persona_boundary_check.py` (前置工具)
  - `docs/RESEARCH/laoxu-w6-persona-boundary-dryrun-v1.md` (W6 W3 dry-run 记录)

## 规则

1. **Append-only**: 只加行, 不删不改历史记录 (小米 doc-curator 监督)
2. **每次 escalate 走完必 append 1 行** (派单人责任, 老胡周报 K1 数据源)
3. **分类说明**: C1=边界违反(100%合理) / C2=越权派单(100%合理) / C3=资源不足(部分合理) / C4=依赖未就绪(部分合理) / C5=理解偏差(不合理)
4. **KPI 统计**: 老胡每周从本文件计 K1(拒接次数) + K2/K3(SLA达成) + K4(Step 4次数)

---

## 历史记录

| 日期 | sub-agent | 派单人 | 拒接分类 | Step 终止 | 决议 | 后续动作 | 关联 |
|---|---|---|---|---|---|---|---|
| 2026-05-28 | 小程 #19 (quant-signal-research) | GM 老雷 | C1 边界违反 (100% 合理) | Step 1 即时 ack | ack 拒接, 重派 IC pool 小卢 | persona §拒绝任务 L42-45 不动; GM 5 题自检 W6 起硬 enforce; persona_boundary_check.py W6 W3 落 | INCIDENTS 错 #4 |

---

## Escalate #1 (W4 Wave 19)

- **日期:** 2026-05-28
- **sub-agent:** 小程 (#19, quant-signal-research)
- **派单:** P0-01 C++ stub 实施 (6 交付物: 5 cpp + test + CMake)
- **拒接理由:** persona §拒绝任务 "代码 / 回测" (`.claude/agents/19-quant-signal-research.md` L42-45)
- **分类:** C1 边界违反 (即时 ack, 100% 合理)
- **处理:** GM 错 #4 入 INCIDENTS log; retry 派 IC pool 小卢; GM 5 题自检 W6 起 enforce
- **时长:** <1h (sub-agent 自我纠错, Step 1 即止, 不走 escalate 流程)
- **persona_boundary_check 状态:** W4 Wave 19 时工具未落 (W6 W3 补落); 若已有工具则 pre-check 可提前拦截

---

## Escalate #2 (W6 Wave 30 — dry-run)

- **日期:** 2026-05-28 (W6 W3 dry-run 计划执行)
- **sub-agent:** 小程 (#19, quant-signal-research) [故意越界 dry-run, GM 老雷 ack]
- **派单:** [dry-run] "小程, 请落代码 signal_stub.cpp + unit test" (故意含越界词)
- **拒接理由:** persona §拒绝任务 "代码 / 回测"
- **分类:** C1 边界违反 (100% 合理)
- **pre-check:** persona_boundary_check.py --dry-run 提前检测 — 期望 FAIL (抓到越界)
- **实际派出结果:** 期望 sub-agent 拒接 (行为与 W4 Wave 19 一致, framework 首次实测验证)
- **处理:** dry-run 不走正式 escalate; 结论入 laoxu-w6-persona-boundary-dryrun-v1.md
- **Wave 26 决议 5 状态:** 本 dry-run = 决议 5 完成验收条件 (R-39 framework 首次实测)
- **注:** 实际派单需 GM 老雷 W6 W3 ack; 本条为预记录, 实测后补充结果

---

## W6/W7/W8 周更新追踪

| Sprint-Week | KPI K1 (拒接次数) | KPI K2 (Step2 SLA) | KPI K3 (Step3 SLA) | KPI K4 (Step4次数) | 备注 |
|---|---|---|---|---|---|
| W6 W3 | 1 (dry-run #2) | N/A | N/A | 0 | dry-run 首次实测 |
| W6 W4 | TBD | TBD | TBD | TBD | 老胡周报补 |
| W7 | TBD | TBD | TBD | TBD | 老胡周报补 |
| W8 | TBD | TBD | TBD | TBD | 老胡周报补 |

---

**Last updated:** 2026-05-28 by 老徐 (#33, Wave 30)
