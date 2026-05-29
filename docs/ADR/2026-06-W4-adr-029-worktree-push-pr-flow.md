# ADR-029 — worktree push + PR flow (W9 W4 起强 enforce)

- **owner:** 老高 (#17, code-quality-reviewer, F 顾问团)
- **last_review:** 2026-05-29
- **status:** ACCEPTED
- **触发:** 老板 verbatim 2026-05-29 + GM merge 瓶颈实证 (ADR-024 §3 现状)
- **关联:**
  - `docs/ADR/2026-06-W3-adr-024-worktree-standard-workflow.md` (ADR-024, fallback)
  - `docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md` (ADR-027, cite 强 enforce)
  - `docs/ADR/2026-06-W4-adr-028-docs-governance-standard.md` (ADR-028, frontmatter 强 enforce)
  - `docs/META/laogao-w9-w4-dispatch-prompt-template-v2.md` (派单 prompt 模板 v2)
  - `tests/ci_grep/worktree_pr_check.py` (W9 W5 新增)
  - `tests/ci_grep/gm_merge_audit.py` (v2 升级, W9 W5)

---

## §1 老板 Verbatim — 约束基线

> "让他们自己在自己的 worktree 拉取合并推送才合理吧"

**老板原话 (2026-05-29)** — 直接约束本 ADR 决定方向.

三条硬约束 (老高解读, W9 W4 生效):

1. **sub-agent 自己 fetch + merge** — origin/main 更新由 sub-agent 在 worktree 内执行, 不需 GM 代劳.
2. **sub-agent 自己 push** — worktree branch push origin 由 sub-agent 完成, GM 不再手动 push.
3. **GM 只 review PR** — GM 的角色从 "执行 merge" 降为 "review + gh pr merge", 不再下场解 conflict / cleanup worktree.

---

## §2 现状问题 (ADR-024 §3 GM 瓶颈)

ADR-024 §3 标准 8 步流程中, Step 5-8 全部由 GM 执行:

```
ADR-024 现状:
  Step 5. GM verify commit 存在
  Step 6. GM merge --no-ff worktree-agent-<id>
  Step 7. GM verify integration (cmake build + ctest)
  Step 8. GM cleanup (git worktree remove + git branch -d)
```

**量化 GM 负担 (W9 实测):**

| 操作 | 每 wave GM 耗时 | 备注 |
|---|---|---|
| git log -1 verify commit | 30s | 复制 branch name + 执行 |
| git checkout main + merge --no-ff | 60s | 含 conflict 时 × 5-10 |
| cmake build + ctest | 3-10 min | ctest 441 tests |
| git worktree remove + branch -d | 30s | |
| **合计 (无 conflict)** | **~5 min / wave** | |
| **合计 (有 conflict)** | **~15-30 min / wave** | conflict 解决 + 重跑 |

Sprint-1 平均 3-5 wave/天. GM 花在 merge 机械操作上 > 30min/天.

**根本问题:** GM 是每个 wave 完成后的 merge 瓶颈. sub-agent 完成后等 GM 空闲才能入 main, 并行度受限.

**老板判断 (2026-05-29):** GM 总裁时间不该花在机械 merge 操作上. sub-agent 自己 push 才合理.

---

## §3 新流程 (W9 W4 起强 enforce)

### §3.1 sub-agent 完成时执行 8 步

```
sub-agent worktree 完成时 (ADR-029 新流程, W9 W4 起):

  Step 1. pwd verify (worktree path)
     pwd
     # 预期: .../sports-trader-cpp/.claude/worktrees/agent-<id>
     # 非预期 → 停止, 不执行后续步骤

  Step 2. git add -A
     git add -A

  Step 3. git commit
     git commit -m "<conventional commit message> (<persona> Wave NN)"
     # 例: "feat(risk): OrderIntent v0.5 ABI align (老韩 Wave 68)"
     # 必含 persona + Wave NN

  Step 4. git fetch origin
     git fetch origin
     # 拉最新 origin/main

  Step 5. git merge origin/main --no-edit
     git merge origin/main --no-edit
     # sub-agent 自己解 conflict (见 §5)
     # --no-edit: conflict 已解时跳过 commit message 编辑

  Step 6. git push origin worktree-agent-<id>
     git push origin worktree-agent-<id>
     # push 到远端 worktree branch

  Step 7. gh pr create
     gh pr create \
       --title "feat(<scope>): <summary> (<persona> Wave NN)" \
       --body "$(cat <<'EOF'
     ## Summary
     - <1-3 bullet points>

     ## ADR cite
     - ADR-027: <cite 或 N/A (非核心 struct 改动)>
     - ADR-024: worktree commit + push done
     - ADR-029: new flow dogfood ✓

     ## ctest
     - [ ] local ctest pass (或 CI 跑过)
     - ctest 结果: <NN/441 PASS 或 CI link>

     ## Worktree
     - branch: worktree-agent-<id>
     - commit: <7位 hash>
     EOF
     )"

  Step 8. 回汇 GM
     回汇必须包含:
     - commit hash: <7位 hash>
     - PR URL: https://github.com/.../pull/<N>
     - ctest 状态: <NN/441 PASS 或 "CI pending">
```

### §3.2 GM 职责 (仅 review + merge)

```
GM (ADR-029 新流程):

  收到 sub-agent 回汇 (commit hash + PR URL + ctest 状态)
  →
  gh pr view <PR URL>              # 看 changelog
  gh pr review <N> --approve       # 或 --request-changes
  gh pr merge <N> --squash         # 一般合并
    (或 --merge --no-ff)           # P0 重要合并, 保留完整 commit 树
  →
  GM 不下场:
    - 不写 cpp
    - 不解 conflict
    - 不 cleanup worktree (sub-agent 完成后 GM 可 batch cleanup, 或留 sub-agent cleanup)
```

### §3.3 流程对比

```
ADR-024 流程 (旧):
  sub-agent → commit → 回汇 hash
                              ↓
  GM verify → GM merge → GM ctest → GM cleanup

ADR-029 流程 (新):
  sub-agent → commit → fetch → merge origin/main → push → gh pr create → 回汇 hash+PR
                                                                                    ↓
                                                                     GM review → GM gh pr merge
```

---

## §4 例外 (仍走 ADR-024, GM 直接 merge)

以下场景 ADR-029 新流程**不适用**, 继续走 ADR-024 §3 8 步流程:

| 场景 | 理由 | 走哪个流程 |
|---|---|---|
| **ADR 立项 / 撤回** | 公司宪法变更, 老郭主审, 老雷拍板, 不走 PR merge 自动化 | ADR-024 + GM 直接 merge |
| **红线事故 P0 hotfix** | < 2h 紧急, ADR-005 §3.2 例外; GM 可直接 Edit main tree | ADR-024 P0 例外 |
| **ABI breaking 大改** | e.g. OrderIntent v0.5 (老韩 Wave 68); 需 GM 人工验 ABI audit trail | ADR-024 + GM 手动 merge |
| **主管 cpp=0 doc only (低风险)** | 无 code change, GM 可直接 fast merge (无 ctest risk) | ADR-024 或 GM fast merge |
| **sub-agent conflict 升级 (第 2 次)** | §5 规则: 第 2 次 conflict 升级 GM | ADR-024 §3 GM 代解 |

**判断规则:** 以上 4 种场景任一命中 → ADR-029 不适用, 派单 prompt 标注 `flow: adr-024-fallback`.

---

## §5 Conflict 处理规则

sub-agent 在 Step 5 (`git merge origin/main --no-edit`) 遭遇 conflict 时:

### §5.1 第 1 次 conflict — sub-agent 自己解

```
处理步骤:
  1. git status → 看 conflict 文件列表
  2. 逐文件解 conflict (选择保留哪一方, 或合并)
     - doc conflict: 合并两方内容 (保留新旧信息)
     - code conflict: 选新版本 (origin/main 为准, 除非 sub-agent 有必要改)
  3. git add <已解 conflict 文件>
  4. git commit -m "merge: resolve conflict with origin/main (<persona> Wave NN)"
  5. 继续 Step 6 (push)
  6. 回汇时标注: "conflict resolved (1 file, <文件名>)"
```

### §5.2 第 2 次 conflict — 升级 GM

```
条件: 同一 wave 内第 2 次遭遇 conflict (resolve 失败或 re-conflict)
处理:
  1. sub-agent 停止 Step 5-6-7
  2. 回汇 GM: "conflict resolved 失败, 需 GM 介入"
     含: conflict 文件列表 + 冲突描述
  3. GM 介入: 走 ADR-024 §3 Step 5-6 (GM 手动 merge + 解 conflict)
  4. GM 解完后通知 sub-agent: "可继续"
     (sub-agent 不再 re-push, GM 直接 merge 入 main)
```

### §5.3 GM 解 conflict 后禁止事项

- GM 不替 IC 修代码逻辑 (GM 错 #13 教训)
- 若 conflict 根因是 IC 设计问题 → 派回原 IC 重做
- conflict resolve 完毕 → GM merge + 记录 "conflict: 1 (GM 解)" 入 PR body

---

## §6 与 ADR-024 关系

| 维度 | ADR-024 | ADR-029 |
|---|---|---|
| **定位** | 8 步流程基线 (W8 W2 起) | ADR-024 §3 Step 5-8 升级版 (W9 W4 起) |
| **merge 执行者** | GM | sub-agent (自己 push) + GM (gh pr merge) |
| **conflict 解决** | GM | sub-agent 第 1 次; GM 第 2 次升级 |
| **cleanup** | GM (Step 8) | 可延后 (branch 留到 PR merge 后 GM batch cleanup) |
| **可追溯** | git log merge commit | GitHub PR timeline |
| **fallback** | N/A (基线) | 例外走 ADR-024 (§4) |

**ADR-024 不撤回.** ADR-029 是 ADR-024 的 Step 5-8 升级, ADR-024 作 fallback (P0 hotfix / 红线 / ABI breaking 场景).

---

## §7 与 ADR-027 / ADR-028 联动

### §7.1 ADR-027 cite 在 PR body 强 enforce

ADR-027 Enforce-3 要求 PR description 含核心 struct cite 块. ADR-029 §3.1 Step 7 PR body 模板已内嵌 `ADR-027: <cite 或 N/A>` 字段.

CI grep `core_data_structure_ssot_check.py` (v1.7) 在 PR 上自动验证. 缺 cite 行 → FAIL.

### §7.2 ADR-028 frontmatter 在 PR body 强 enforce

ADR-028 规则 1 要求所有 `docs/*.md` 有 frontmatter. sub-agent 新建文档时 PR body 须含文件清单; CI `docs_frontmatter_check.py` (W9 W3 落地) 验证新增 md 有 frontmatter.

### §7.3 PR template (老高 v1.7 已落)

`.github/PULL_REQUEST_TEMPLATE.md` v1.7 已含 §9.5 (ADR-027 C1-C4 cite block) + §9.6 (worktree commit verify). ADR-029 Step 7 PR body 模板与 PR template 兼容 (超集, 不冲突).

---

## §8 Timeline

| 时间 | 行动 | 负责人 |
|---|---|---|
| W9 W4 Mon (本 wave) | ADR-029 立项 + 派单 prompt 模板 v2 | 老高 (本 wave dogfood) |
| W9 W4 Tue | GM 更新派单 prompt (新 wave 用新流程) | GM 老雷 |
| W9 W4 Wed | 老高 CI grep 加 `worktree_pr_check.py` (PR title 验证) | 老高 W9 W5 |
| W9 W5 | 全 sub-agent 新 wave 已用新流程 (push + gh pr create) | 全员 |
| W9 W5 retro | 统计: PR 创建率 / conflict 次数 / GM merge 时间节省 | 老胡 KPI |

---

## §9 CI grep 升级计划 (老高 W9 W5 实施)

### §9.1 新增 `worktree_pr_check.py`

**文件:** `tests/ci_grep/worktree_pr_check.py`

**规则:**

```
Rule P1 (FAIL): PR title 格式
  PR title 必须符合: "<type>(<scope>): <summary> (<persona> Wave NN)"
  例: "feat(risk): OrderIntent v0.5 ABI align (老韩 Wave 68)"
  缺少 "Wave NN" 格式 → FAIL

Rule P2 (FAIL): PR body 含 commit hash
  PR body 必须含 7 位 hex commit hash
  grep: r"commit\s*:\s*[0-9a-f]{7}"
  缺失 → FAIL (回汇规范 §3.1 Step 8)

Rule P3 (WARN): PR body 含 worktree branch
  PR body 建议含 "branch: worktree-agent-<id>"
  缺失 → WARN (非阻断)
```

**状态:** W9 W5 落地 (FAIL = Rule P1 + P2).

### §9.2 `gm_merge_audit.py` v2

**升级方向:** 从 "GM merge commit message audit" 扩展为 "PR merge audit":

```
v1 (ADR-024 §6): git log 扫 GM merge commit message 格式
v2 (ADR-029 §9.2):
  - 新增: 扫 GitHub PR merge event (gh pr list --state merged)
  - 验证: merged PR title 含 "Wave NN"
  - 验证: merged PR 有 review approved (不是 GM 直接 merge 无 review)
  - fallback: gh 命令不可用时 fallback 到 v1 git log 扫描
```

**落地时间:** W9 W5 (同 worktree_pr_check.py 一并提 PR).

---

## §10 落地 checklist

- [x] ADR-029 立项 (本文件, 老高 W9 W4)
- [x] 派单 prompt 模板 v2 (`docs/META/laogao-w9-w4-dispatch-prompt-template-v2.md`)
- [x] 本 wave dogfood: 老高自己执行 ADR-029 新流程 (push + gh pr create)
- [ ] GM (老雷) ADR-029 ack + W9 W4 Tue 起派单用新模板
- [ ] `worktree_pr_check.py` Rule P1/P2 落地 (老高 W9 W5)
- [ ] `gm_merge_audit.py` v2 PR merge audit 落地 (老高 W9 W5)
- [ ] 老胡周报 §KPI 新增: PR 创建率 / conflict 次数 / GM merge 时间节省 (W9 W5 首期)

---

**最后更新:** 2026-05-29 by 老高 (#17, code-quality-reviewer, F 顾问团, Wave 73)
