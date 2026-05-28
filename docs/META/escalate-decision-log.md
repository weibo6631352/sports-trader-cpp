# escalate-decision-log.md — sub-agent 拒接决议 append-only 日志

- **owner:** 老徐 (#33, ai-ops-collaboration, F 顾问团)
- **co-维护:** 小米 (#40, doc-curator) 归档监督 + 老胡 (#26, pm) 周报 KPI 抽样
- **last_review:** 2026-05-28
- **触发:** R-39 escalate flow v0.3 §6.2 (老徐) + Wave 26 决议 5 + GM 错 #4/#11/#13
- **关联:**
  - `docs/RESEARCH/laoxu-r39-escalate-flow-v0.3.md` (v0.3 当前版)
  - `docs/RESEARCH/laoxu-subagent-escalate-flow-v0.2.md` §2 + §6.2 (v0.2 历史)
  - `docs/INCIDENTS/gm-self-mistakes-log.md` 错 #4 / 错 #11 / 错 #13
  - `tests/ci_grep/persona_boundary_check.py` (前置工具)
  - `tests/ci_grep/gm_commit_author_check.py` (C6 检测工具, 老高 W7 W3 落)
  - `docs/RESEARCH/laoxu-w6-persona-boundary-dryrun-v1.md` (Escalate #2 dry-run 记录)

## 规则

1. **Append-only**: 只加行, 不删不改历史记录 (小米 doc-curator 监督)
2. **每次 escalate 走完必 append 1 行** (派单人责任, 老胡周报 K1 数据源)
3. **分类说明 (v0.3)**: C1=边界违反(100%合理) / C2=越权派单(100%合理) / C3=资源不足(部分合理) / C4=依赖未就绪(部分合理) / C5=理解偏差(不合理) / C6-例外=GM代修有ADR-005§3.4理由(合理) / C6-越权=GM代修无例外理由(GM错误)
4. **KPI 统计**: 老胡每周从本文件计 K1(拒接次数) + K2/K3(SLA达成) + K4(Step4次数) + K5(C6-越权次数, v0.3新增)

---

## 历史记录

| 日期 | sub-agent/owner | 派单人/GM | 拒接分类 (C1-C6) | Step 终止 | 决议 | 后续动作 | 关联 |
|---|---|---|---|---|---|---|---|
| 2026-05-28 | 小程 #19 (quant-signal-research) | GM 老雷 | C1 边界违反 (100% 合理) | Step 1 即时 ack | ack 拒接, 重派 IC pool 小卢 | persona §拒绝任务 L42-45 不动; GM 5 题自检 W6 起硬 enforce; persona_boundary_check.py W6 W3 落 | INCIDENTS 错 #4 |
| 2026-06-W2 | 老唐/小冯/老李/小段/小卢/小蒋/老沈 (8 IC) | GM 老雷 (越权代修) | C6-越权 (retro 补填) | Step 1-C6 retro 补检 | GM 错 #11 入 INCIDENTS; 永久 enforcement build+ctest 验证; gm_commit_author_check.py W7W3 激活 | commit a8afebe 不强 revert; W7 retro 复盘 GM 代修文化 | INCIDENTS 错 #11 |
| 2026-06-W3 | 老孙 #06 (signer) | GM 老雷 (越权代修) | C6-越权 (retro 补填) | Step 1-C6 retro 补检 | GM 错 #13 入 INCIDENTS; 2 处 revert; ADR-005 §3.4 ownership lock 立; gm_commit_author_check.py W7W3 激活 | 老孙 W7 libsodium 重新实施; HR 小林评估 GM 越权频率 | INCIDENTS 错 #13 |

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

## Escalate #2 (W7 W1 — dry-run, 老徐主导)

- **日期:** 2026-07-W1 (W7 W1 实测, 原 W6 W3 计划因 GM 错 #13 静待期未执行)
- **sub-agent:** 小程 (#19, quant-signal-research) [故意越界 dry-run, GM 老雷 ack]
- **派单:** [dry-run] "小程, 请落代码 signal_stub.cpp + unit test" (故意含越界词)
- **拒接理由:** persona §拒绝任务 "代码 / 回测"
- **分类:** C1 边界违反 (100% 合理)
- **pre-check:** persona_boundary_check.py --dry-run 提前检测 — 期望 FAIL (抓到越界)
- **实际派出结果:** 期望 sub-agent 拒接 (行为与 W4 Wave 19 一致, framework 首次实测验证)
- **处理:** dry-run 不走正式 escalate; 结论入 laoxu-w6-persona-boundary-dryrun-v1.md §7
- **Wave 26 决议 5 状态:** 本 dry-run = 决议 5 完成验收条件 (R-39 framework 首次实测)
- **实测结果 §7:** [W7 W1 执行后填 — 见 laoxu-r39-escalate-flow-v0.3.md §7.4]

---

---

## Escalate #3 (W6 W2 retro, 老徐 W7 W1 补填)

- **日期:** 2026-06-W2 (Wave 29 整合期触发, retro 标记)
- **触发方:** GM 老雷 (自行代修, 非 sub-agent 拒接)
- **涉及 owner:** 老唐/小冯/老李/小段/小卢/小蒋/老沈 (8 IC, 10 处编译错)
- **分类:** C6-越权 (GM 整合 build fail 未 stop wave 派回, 自己 10 处 hotfix, 无 ADR-005 §3.4 例外理由)
- **Step 终止:** Step 1-C6 retro 补检 (W6 W2 当时 v0.3 未存在, retro 定性)
- **决议:** GM 错 #11 入 INCIDENTS log; 永久 enforcement build+ctest 验证 (老高 build_verification.py v1.3); gm_commit_author_check.py W7 W3 激活
- **后续动作:** commit a8afebe 不强 revert (成本大); W7 retro 复盘 GM 代修文化; 各 IC owner 补真本地 build+ctest 回汇
- **关联:** INCIDENTS 错 #11 | laoxu-r39-escalate-flow-v0.3.md §8

---

## Escalate #4 (W6 W3 retro, 老徐 W7 W1 补填)

- **日期:** 2026-06-W3 (Wave 30 整合期触发, retro 标记)
- **触发方:** GM 老雷 (自行代修, 非 sub-agent 拒接)
- **涉及 owner:** 老孙 #06 (signer, libsodium FetchContent 失败)
- **分类:** C6-越权 (GM 无法确定老孙 libsodium 设计意图, 自己加 OFF guard + AND wrap 2 处, 无 ADR-005 §3.4 例外理由)
- **Step 终止:** Step 1-C6 retro 补检 (W6 W3 当时 v0.3 未存在, retro 定性)
- **决议:** GM 错 #13 入 INCIDENTS log; 2 处越权 revert; ADR-005 §3.4 文件 ownership lock 立; gm_commit_author_check.py W7 W3 激活
- **后续动作:** 老孙 W7 libsodium FetchContent 重新实施 (见 laosun-libsodium-fetchcontent-w7-plan.md); HR 小林评估 GM 越权频率对班底士气
- **关联:** INCIDENTS 错 #13 | laoxu-r39-escalate-flow-v0.3.md §9

---

## W6/W7/W8 周更新追踪

| Sprint-Week | KPI K1 (拒接次数) | KPI K2 (Step2 SLA) | KPI K3 (Step3 SLA) | KPI K4 (Step4次数) | 备注 |
|---|---|---|---|---|---|
| W6 W2 | C6-越权 #3 (GM 错 #11) | N/A (C6) | N/A (C6) | 0 | retro 补填; 10 处 hotfix 代修 |
| W6 W3 | C6-越权 #4 (GM 错 #13) | N/A (C6) | N/A (C6) | 0 | retro 补填; 2 处代修 revert |
| W7 W1 | 1 (dry-run #2 计划执行) | TBD | TBD | 0 | Escalate #2 实测 + §7.4 填 |
| W6 W4 | TBD | TBD | TBD | TBD | 老胡周报补 |
| W7 | TBD | TBD | TBD | TBD | 老胡周报补 |
| W8 | TBD | TBD | TBD | TBD | 老胡周报补 |

---

**Last updated:** 2026-05-28 by 老徐 (#33, Wave 33 — v0.3 update: C6 分类新增, Escalate #3/#4 retro 补填, Escalate #2 计划 W7 W1)
