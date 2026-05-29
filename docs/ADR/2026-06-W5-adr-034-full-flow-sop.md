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


## §2 完整流程 SOP — 最合理版 (老板 5/29 二次修正)

**核心原则: 拉 + 合 + 推, 代码不丢, 流程清晰. GM 只做 sync, 不 merge 不 push.**

### A. GM 派 wave 前 (1 步)

```bash
git fetch origin && git reset --hard origin/main
# 然后 Agent(isolation="worktree", model="sonnet", subagent_type=...)
```

### B. Sub-agent 全流程 (老板 verbatim "拉取 + 合并 → 写 → 提交 + 拉取 + 合并 + 推送")

```bash
# === 开展前 (老板五次修正: 同一 batch 同 base, 不必再 sync 不必等其他 wave) ===
pwd  # B.0 verify worktree path (GM 错 #19 防)

# === 工作 ===
# B.1 写代码/doc, 含 ADR-027 cite block

# === 完成时 (老板原话 "提交 + 拉取 + 合并 + 推送") ===
git add -A && git commit -m "<persona> Wave NN ..."   # B.2 提交
git fetch origin                                       # B.3 拉取 (代码不丢)
git merge origin/main --no-edit                        # B.4 合并 (无冲突 自动通过)

# === B.4 冲突处理 (老板 5/29 三次补丁) ===
# 若 B.4 输出 "CONFLICT (content): ..." 不要 abort, 自己解:
git diff --name-only --diff-filter=U                   # B.4a 看冲突文件 list

# B.4b 判断冲突文件 owner:
#   - 全 自己 owner (sub-agent 自己改的) → 自己解 (走 B.4c)
#   - 含 跨 owner (FOM ADR-005 §3.4) → 升 GM (走 B.4d)

# B.4c 自己解 (推荐):
#   - cat <冲突文件> 读 conflict marker (<<<<<<<, =======, >>>>>>>)
#   - 语义合并双方逻辑 (绝不 git checkout --theirs/ours 一边倒, 易丢代码)
#   - git add <冲突文件>
#   - git commit (merge commit, msg: "merge: resolve conflict in <files>")
#   - continue B.5

# B.4d 解不了 (跨 owner / 复杂 cpp / 语义不清):
git merge --abort                                       # 撤销 merge 保护代码不丢
# 回汇 GM 升级 (ADR-005 §3.2):
#   "Wave NN 遇 conflict 升 GM: 冲突文件 X (跨 owner persona Y), 我无法解."
# GM 派回原 owner 或 GM 介入手动 merge
# B.5 本地 verify (pre-push hook auto: cmake build + ctest + 5 grep + SEGFAULT retry x3)
git push origin worktree-agent-<id>                   # B.6 推送
gh pr create --title "..." --body "<cite + 决策>"     # B.7 PR
gh pr merge <pr_num> --auto --squash --delete-branch   # B.8 auto-merge (等 CI pass 自动)
# 回汇必含: PR URL + commit hash + local ctest 状态
```

### C. GM 派 wave 后 (老板修正: 极简, 无介入)

```bash
# 啥也不做. sub-agent 自己 auto-merge. GM 只看周报 / Issue / 重要事件.
```

### D. 错误处理 (代码不丢)

| 错误 | 处理 |
|---|---|
| sub-agent commit 后 push 前 fail | commit 在 worktree branch local, 重 push 不丢 |
| push 后 PR 没创建 | 派同 persona 新 wave 重做 gh pr create (commit 在 remote branch 可恢复) |
| PR auto-merge fail (CI fail) | GitHub Issue 通过 notify-on-main-failure 提醒 + GM 视情 admin merge 或回炉 |
| worktree 被自动 cleanup, push 未发生 | doc/code 丢失, 派新 wave 重做 (老板 verbatim "代码不能丢" 必修教训) |
| Merge conflict (B.4 触发) | sub-agent 自己语义合并 (B.4c) 或 git merge --abort + 升 GM (B.4d) — **禁 git checkout --theirs/ours 一边倒** |
| Merge conflict 2 次 | 升 GM 介入 (ADR-005 §3.2), 派回原 owner |

### E. 自动化已落 (老板 5/29 verbatim 全 record)


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
