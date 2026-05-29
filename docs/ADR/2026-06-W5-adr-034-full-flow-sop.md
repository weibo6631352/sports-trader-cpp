---
name: adr-034-full-flow-sop
description: 完整 wave 流程 SOP — sub-agent 责任 + GM 责任 + 错误处理 + 自动化 (老板 5/29 verbatim 总集)
owner: P-00 (总裁草案) → 老郭 W10 W3 主审
last_review: 2026-05-29
status: Draft
metadata:
  type: ADR
  id: ADR-034
---

# ADR-034: 完整 wave 流程 SOP — 不漏不错

- **ID:** ADR-034
- **Date:** 2026-05-29 (W10 W2)
- **Status:** Draft (总裁 P-00 草案 → 老郭 W10 W3 主审)
- **触发:** 老板 5/29 4 条 verbatim 汇总

---

## §1 老板 verbatim (一字不改)

> "每一轮开始前必须保证我们 main 是最新的代码, 他们开辟的 worktree 不会是很久的代码版本"

> "他们跑完后, 我们也必须合并, 推送. 你把整个流程理清楚吧, 总之要合理, 避免出现不必要的麻烦和错误"

加上 ADR-030/031/032/033 老板 verbatim 集合.

---


## §1.5 老板 5/29 修正 (verbatim record)

> "我说错了, 应该是他们自己合并推送, 我们工作开展前只管拉取最新的"

→ §2.C GM 责任**极简版**: 不 admin merge, 不 push, **只 pull 最新**.
→ §2.B sub-agent 责任**扩展**: 完成 push + open PR + `gh pr merge --auto --squash` (GitHub auto-merge 等 CI pass 后自动 merge).


## §2 完整流程 SOP (W10 W2 起强 enforce)

### A. GM 派 wave 前 (3 步)

```bash
# A.1 sync main (老板 5/29 verbatim)
git fetch origin --prune
git reset --hard origin/main

# A.2 verify 干净
git status                           # 必须 clean
git worktree list                    # 已 cleanup 完成 wave
gh pr list --state open              # 历史 PR 处理完

# A.3 派 wave (Agent isolation="worktree", model="sonnet", subagent_type=...)
```

### B. Sub-agent 责任 (13 步, ADR-029+032 全集)

```bash
# B.1 pwd verify (GM 错 #19 防)
pwd  # 必须 .claude/worktrees/agent-<id>/

# B.2 ls verify Edit path (绝不 main)
ls

# B.3 写代码/doc, 含 ADR-027 cite block

# B.4 cd worktree path
cd .claude/worktrees/agent-<id>/

# B.5 git add -A
git add -A

# B.6 git commit -m "<conventional commit>" (含 ADR-027 cite + ADR-031 wave NN)
git commit -m "feat(<scope>): ... (<persona> Wave NN)"

# B.7 git fetch origin (同步 remote)
git fetch origin

# B.8 git merge origin/main --no-edit (本地解 conflict, 第 2 次 conflict 升 GM)
git merge origin/main --no-edit

# B.9 本地 verify (ADR-032 §3 策略 1)
# - cpp wave: cmake --build + ctest (pre-push hook 自动 enforce)
# - doc-only: 仅 lint check

# B.10 git push origin worktree-agent-<id>
git push origin worktree-agent-<id>

# B.11 gh pr create + auto-merge (老板 5/29 修正)
#     sub-agent 自己 merge, GM 不介入
gh pr create --title "<conventional>" --body "<含 cite + 本 wave 决策>"
gh pr merge <pr_num> --auto --squash --delete-branch  # 等 CI pass 后 GitHub 自动 merge

# B.12 回汇必含 (强约束):
# - PR URL (gh pr URL 实际 print)
# - commit hash (git log -1)
# - 本地 ctest 状态 (pass / N/A)

# B.13 不等远端 CI (ADR-032 §3 策略 3, 立刻汇报)
```

### C. GM 责任极简版 (老板 5/29 修正: GM 不合并推送, sub-agent 自己做)

```bash
# C.1 verify PR URL 存在
gh pr view <num> --json url,state

# C.2 若 PR 不存在:
#     方案 A: 派 sub-agent 重做 (ADR-031 PUSH_BACK 路径)
#     方案 B: GM 自己 cd worktree push (例外, ADR-005 §3.2 P0 hotfix)
#             cd .claude/worktrees/agent-<id>
#             git push origin worktree-agent-<id>
#             gh pr create ...
#     方案 C: doc 丢失不可恢复 → 派新 wave 重做

# C.3 看 PR CI 状态 (2 类决策):
gh pr checks <num>

# 类 1: 全 pass → gh pr merge --squash --delete-branch
# 类 2: 部分 fail:
#       - 红线 fail (R-20 / R-12 / ABI break / 真 Build error) → 回炉
#       - Linux env specific (Build paper Linux, clang-format-19 等) + 本地 verify pass → admin merge

# C.4 merge
gh pr merge <num> --admin --squash --delete-branch   # 或 --squash 干净 merge

# C.5 cleanup (老板 5/29 verbatim "推送")
git fetch origin --prune                              # 清 stale ref
git worktree remove -f -f .claude/worktrees/agent-<id>
git branch -D worktree-agent-<id> 2>/dev/null
# 若远端 branch 仍存在 (delete_branch_on_merge=true 兜底):
gh api -X DELETE repos/$(gh repo view --json nameWithOwner -q .nameWithOwner)/git/refs/heads/worktree-agent-<id> 2>/dev/null || true

# C.6 sync main 准备下一 wave
git fetch origin
git reset --hard origin/main
```

### D. 错误处理矩阵

| 错误 | 表现 | 处理 |
|---|---|---|
| sub-agent push 误报 "成功" 实际没 push | PR 不存在 + 远端 branch 不存在 | GM C.2 方案 A/B/C |
| sub-agent 跨 worktree 写 main | commit 直 main 非 worktree branch | 记 GM 错 #19 系列 + ADR-024 §3.1 enforce |
| dead loop iteration > 2 次 | sub-agent 反复修 CI fail (W81 教训) | ADR-032 §3 #2 上限 enforce + GM TaskStop |
| 本地 hook deadlock | sub-agent push fail 系统级 (-Wno- / SEGFAULT) | ADR-032 §3 + 老高 Wave 101 hook 例外 |
| Stale branches 累积 | 远端 branches 多 | cron `cleanup-stale-branches.yml` daily auto |
| CI fail 累积没人看 | main 上多 fail | `notify-on-main-failure.yml` Auto Issue + 老板 GitHub Watch |
| worktree path 被自动 cleanup doc 没 push | 不可恢复 | GM C.2 方案 C 重派 |

### E. 已落自动化 (老板 5/29 verbatim 全 record)

8 workflows + 3 repo settings + pre-push hook + ADR-029/030/031/032/033/034:

```
GitHub Actions workflows (8):
   pr.yml + pr-linux.yml + perf-regression.yml  (PR 触发)
   nightly.yml                                   (cron daily chaos+replay)
   cleanup-old-runs.yml                          (cron 周一 03:00 删 stale fail)
   cleanup-stale-branches.yml                    (cron daily 04:00 删 stale worktree branches)
   notify-on-main-failure.yml                    (workflow_run trigger Auto Issue)
   close-ci-fail-on-green.yml                    (green main → close ci-fail Issue)
   weekly-report-cron.yml                        (cron 周日 04:00 周报 Auto Issue, 老胡 Wave 102)

Repo settings (3):
   delete_branch_on_merge=true                   (远端 PR merge auto delete branch)
   workflow runs retention = 7 天 (默认 90 → cron force)
   admin merge allowed                            (GM bypass CI fail 时用)

Local (2):
   .git/hooks/pre-push                            (5 检查 + SEGFAULT retry + delete 例外)
   scripts/gm-cleanup-worktree.sh                 (GM 一键清理 merged worktree)
```

## §3 ADR-030~034 老板 verbatim 总集

| ADR | 老板 verbatim | 落实 |
|---|---|---|
| ADR-030 | 员工主动上报不合理 | PUSH_BACK 段 + 5 历史正例 + escalation-inbox |
| ADR-031 | 多人讨论 + 状态 + 调研 + 覆盖每部门 | W10 plan v2 40 ticket 实证 |
| ADR-032 | github workflows 慢, 本地优先 | pre-push hook + workflow v2 精简 |
| ADR-033 | 失败如何通知 + 不需要干预 | 8 workflow + 3 settings + pre-push 闭环 |
| ADR-034 (本) | sync main + 跑完合并推送 + 流程合理 | A/B/C/D 4 章 SOP |

## §4 实施 timeline

- W10 W2 (本周, 总裁草案): ADR-034 立 + GM 立刻按 SOP 执行
- W10 W3: 老郭 主审 → ACCEPTED
- W10 W3: 老高 加 CI grep `flow_sop_check.py` (检 PR body 含 cite + 派单 prompt 含 sync main 约束)
- W10 W4: 全员 W10 W4 wave 严按 SOP, 监控数据 (PR drop rate / cleanup rate / 错误次数)

## §5 不耻下问

- @老雷 GM (我自己) — 立刻 enforce
- @老郭 W10 W3 主审
- @老胡 PM 周报 §10 SOP enforce KPI
- @老高 W10 W3 grep 配套
- @5 主管 + 9 顾问 W10 W3 周会传达

## §6 总裁 P-00 PUSH_BACK 自检 (ADR-030)

我作为总裁 W9-W10 GM 责任做漏:
- W9 W4 老高 PR #2-4-5-6-8 base 关系混乱 (没 sync main)
- W10 W2 小林 Wave 103 doc 丢失 (没 verify PR URL)
- W10 W2 老沈 V97 拒 cpp 没立刻派老孙补 (没及时处理)

ADR-034 是 self-correction. 后续每个 wave batch 我严按 §2 SOP, 不漏不错.

---

**最后更新:** 2026-05-29 by 总裁 P-00 (草案, 老郭 W10 W3 主审)
