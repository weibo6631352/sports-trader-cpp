# ADR-024: worktree 标准 8 步流程 (W8 W2 起强 enforce)

- **ID:** ADR-024
- **Date:** 2026-06-W3 (W8 W2)
- **Status:** Accepted
- **Owner:** 老郭 (F 顾问团协调人)
- **触发:** GM 错 #19/#20/#21 实证教训 + W8 Wave 35 老高重做跑通标准流程
- **last_review:** 2026-06-W3

---

## §1 背景

### §1.1 ADR-021 基础 (W7 末)

ADR-021 立 worktree 物理隔离: 老板选 C (W7 容忍 + W8 起强 enforce), 所有 IC 派单必带 `isolation="worktree"`. 解决 W6-W7 重复出现的 sub-agent 并行写文件抢占问题 (GM 错 #15 永久 enforcement).

ADR-021 建立了**物理隔离层**, 但未规定 sub-agent 完成后的 git commit 流程和 GM merge 流程细节.

### §1.2 W8 W1 实测发现 3 个 GM 错

ADR-021 上线后, W8 W1 实测发现 3 个流程漏洞:

| 错误编号 | 描述 | 根因 |
|---|---|---|
| **GM 错 #19** | Edit tool 跨 path 写 main tree: 老孙 ed25519 + signer_v52 两文件都写到 main working tree, 非 worktree | ADR-021 物理隔离约束 Edit tool, 但派单 prompt 未显式禁止; sub-agent 不自觉遵守 |
| **GM 错 #20** | 派单 prompt 未要求 sub-agent 在 worktree 内执行 `git add + commit`, GM 拿到 worktree 后需自己 cp 文件而非 git merge | 流程设计缺口: ADR-021 只要求 worktree 隔离, 未要求 sub-agent 完成时 commit |
| **GM 错 #21** | GM force remove worktree 时漏 cp 老高 6 项产出, 数据全丢, 老高 Wave 35 需完整重做 | 未 verify sub-agent 是否已 commit 到 branch, 直接 force remove; 无 merge commit 保护 |

三个错误构成**流程链断点**: 隔离有了, 但 commit → verify → merge → cleanup 的标准链路缺失.

### §1.3 W8 W2 Wave 35 实证 (老高重做)

Wave 35 老高按补齐后的标准流程重做, 实证结果:

- worktree branch `worktree-agent-a98a4fea4e0eb520d` sub-agent commit `4394693` 成功
- GM verify branch 有 commit → merge `2560966` into main (--no-ff)
- worktree clean remove, 0 数据丢失
- ctest 441/441 PASS

标准 8 步流程**实证跑通**. ADR-024 据此固化.

---

## §2 标准 8 步流程 (W8 W2 起强 enforce)

**所有 IC 派单 (isolation="worktree") 必须完整走完以下 8 步. 无例外.**

```
Step 1. GM 派单
   Agent(
       subagent_type="...",
       model="sonnet",
       isolation="worktree",     ← ADR-021 强约束
       prompt=<含 ADR-024 §3 模板>  ← ADR-024 强约束
   )

Step 2. sub-agent 在 worktree path 改文件
   ⚠ 强约束 (GM 错 #19 教训):
   - 仅 Edit / Write worktree path 文件 (.claude/worktrees/<agent-id>/...)
   - 禁止 Edit main tree 文件 (/Users/.../sports-trader-cpp/ 根目录下, 非 worktree path)
   - 如不确定当前 path, 先 pwd 确认

Step 3. sub-agent 完成时执行 git commit
   ⚠ 强约束 (GM 错 #20 教训): 必须 commit, 不可省略
   cd .claude/worktrees/<agent-id>
   git add -A
   git commit -m "<task summary> (<persona> Wave NN)"

Step 4. sub-agent 回汇 (必带以下两项)
   - worktree branch name: worktree-agent-<id>
   - commit hash: <7位 hash>

Step 5. GM verify commit 存在
   ⚠ 必须 verify, verify 通过才进 Step 6 (GM 错 #21 教训)
   git -C /path/to/repo log -1 worktree-agent-<id>
   预期: 看到 sub-agent 的 commit message + hash
   如果 branch 无 commit → 不进 Step 6, 派回 sub-agent 补 commit

Step 6. GM merge (不 cp)
   ⚠ 强约束 (GM 错 #21 教训): 用 git merge, 不用 cp
   git checkout main
   git merge --no-ff worktree-agent-<id> -m "merge: <persona> Wave NN"

Step 7. GM verify integration
   cmake --build build --parallel 4
   ctest -j 1 --output-on-failure
   git status  (预期: clean, 无 stray files)
   如 ctest 失败 → revert merge, 派回 owner 修

Step 8. cleanup (已 merge 后安全)
   git worktree remove .claude/worktrees/<agent-id>     (无 -f, 除非 pid locked)
   git branch -d worktree-agent-<id>                   (用 -d 不用 -D, 因已 merged)
```

**流程图:**

```
GM 派单 → sub-agent 改文件 → sub-agent git commit → 回汇 branch+hash
                                                          ↓
                              cleanup ← verify integration ← GM verify commit ← GM merge
```

---

## §3 派单 prompt 强约束模板 (W8 W2 起所有派单必带)

以下模板必须原文嵌入每次 IC 派单 prompt 末尾. 禁止省略或缩写.

```
---
**ADR-021 worktree + git commit (强 enforce, GM 错 #19/#20/#21 教训):**

1. 确认当前 path 在 worktree 内:
   ```bash
   pwd
   # 预期: .../sports-trader-cpp/.claude/worktrees/agent-<your-id>
   ```

2. 仅 Edit worktree path 文件, **不 Edit main tree** (GM 错 #19 教训):
   - 允许: .claude/worktrees/agent-<your-id>/... 路径下的文件
   - 禁止: /Users/.../sports-trader-cpp/ 根目录下非 worktree 路径的文件

3. 完成全部任务后执行 (GM 错 #20 教训):
   ```bash
   cd .claude/worktrees/agent-<your-id>
   git add -A
   git commit -m "<task summary> (<persona> Wave NN)"
   ```

4. 回汇必须包含:
   - worktree branch name: worktree-agent-<your-id>
   - commit hash: <git log -1 --format="%h" HEAD>
---
```

---

## §4 GM verify checklist (每次 merge 前必过)

```
☐ git log -1 worktree-agent-<id> 有 sub-agent commit?  (GM 错 #21 核心 check)
☐ commit message 含 persona + Wave NN?
☐ git diff main..worktree-agent-<id> --stat 文件清单符合预期?
☐ git diff main..worktree-agent-<id> 无意外 main tree 文件被改? (GM 错 #19 二次确认)
☐ git merge --no-ff (不 fast-forward, 保留 merge commit 可追溯)?
☐ ctest baseline 不破 (Step 7 验证)?
☐ git status clean after merge (无 stray files)?
```

**所有 7 项全部打 ☑ 才执行 cleanup (Step 8).** 任何一项 ☒ → 停止, 处理后重新过 checklist.

---

## §5 例外 (不需要走 8 步流程)

**例外场景一: 紧急 P0 hotfix (< 2h, ADR-005 §3.2)**

- GM 可自己 Edit main tree 1-2 行 (e.g. Ninja $(MAKE) → make)
- 必显式标注: "P0 hotfix, ADR-005 §3.2 例外, <理由>"
- 不计入 GM 错 #13 (越权代修)
- P0 例外 W8 起每周 ≤ 1 次 (老胡 KPI 监控)

**例外场景二: 主管 / 协调人 / GM 写自己职责范围文档**

- 顾问团 F / 老郭 / 老雷: 写 ADR / INCIDENTS / CLAUDE.md / 会议纪要
- 5 单元主管: 写本单元 OKR / KPI / Sprint backlog
- 不需要 worktree (非 IC 派单场景)

**非例外 (明确排除):**

- IC 写代码 / 测试 / 技术文档: 必须 worktree, 无例外
- 主管派 IC: 必须带 isolation="worktree"

---

## §6 老高 v1.6 grep 配套 (W8 W3 落地)

ADR-021 已要求老高 PR v1.5 加 worktree isolation grep. ADR-024 补 W8 W3 配套:

**新增 `worktree_commit_check.py`:**
- 扫描派单 prompt 文件, grep 必须同时含:
  - `isolation="worktree"` 或 `isolation: worktree`
  - `git add` + `git commit` 字样
- 缺失任意一项 → WARN (GM 错 #20 enforce)

**新增 `gm_merge_audit.py`:**
- 扫描 git log main, grep merge commit message 必须含 `merge: <persona> Wave`
- 缺失格式 → WARN (GM 错 #21 commit 追溯 enforce)

**与 v1.5 关系:** v1.6 是 v1.5 超集, 不撤回 v1.5 已有的 worktree isolation grep.

**落地人:** 老高 (W8 W3, @老高 见 §11)

---

## §7 工具限制 / 已知问题

**问题一: Edit tool 不限制 path (GM 错 #19 根因)**

Claude Code Agent tool 的 `isolation="worktree"` 参数给 sub-agent 独立 git worktree 物理目录, 但 Edit tool 本身不限制写入 path. sub-agent 如果知道 main tree 的绝对路径, 仍可 Edit main tree 文件.

缓解: 派单 prompt 强约束 (§3 模板) + 老高 grep (§6). 无法靠工具完全消除, 靠规范约束.

**问题二: worktree locked (pid 锁)**

claude agent pid 锁住 worktree 时, `git worktree remove` 报 locked 错误. 需 `-f` 或 `-f -f` 强制 remove.

缓解: 仅在 GM verify merge 已进 main 后才 force remove. force remove 后 branch 仍在, git branch -d 清理.

**问题三: force remove + branch deletion 不可逆**

cleanup (Step 8) 一旦执行不可逆. 如果 merge 未进 main 就 force remove → 产出全丢 (GM 错 #21 复现).

缓解: §4 checklist 第 1 项必须先 verify commit 存在, 第 5 项 verify --no-ff merge 完成, 才执行 cleanup.

---

## §8 ADR-021 + ADR-024 关系

| 层次 | ADR | 内容 |
|---|---|---|
| 物理隔离层 | ADR-021 (W7 末) | worktree 物理隔离, `isolation="worktree"` 强 enforce |
| 流程规范层 | ADR-024 (W8 W2, 本文件) | 8 步标准流程, commit 约束, verify checklist, merge 规范, cleanup 顺序 |

**两者互补, 不撤回 ADR-021.** ADR-024 是 ADR-021 的流程配套, 填补 commit → merge → cleanup 链路缺失.

双层防御 (与 ADR-005 §3.4 FOM 协同):
- FOM 逻辑层: 派单 prompt 列允许文件 ownership
- worktree 物理层 (ADR-021): sub-agent 独立 git worktree, 并行不抢占
- 流程规范层 (ADR-024): commit 到 branch, verify, merge, cleanup 标准化

---

## §9 GM 错 #19/#20/#21 永久 enforcement 落地

| GM 错 | 描述 | 永久 enforcement | 负责人 |
|---|---|---|---|
| **#19** | Edit tool 跨 path 写 main tree (老孙 ed25519 + signer_v52) | §3 派单 prompt 强约束模板必含 "仅 Edit worktree path"; 老高 v1.6 grep | 老郭 + 老高 |
| **#20** | 派单未要求 sub-agent git commit 到 worktree branch | §3 模板必含 git add + git commit 步骤; 老高 `worktree_commit_check.py` | 老郭 + 老高 |
| **#21** | force remove worktree 漏 cp 老高 6 项产出, 数据全丢 | §4 checklist 第 1 项: verify commit 存在才进 merge; `gm_merge_audit.py` | 老郭 + 老高 |

---

## §10 W8 W2 实证数据

Wave 35 老高重做, 按标准 8 步流程执行:

| 步骤 | 实证结果 |
|---|---|
| sub-agent worktree 内完成任务 | worktree path 正确, 无 main tree 污染 |
| sub-agent git commit | commit `4394693` on branch `worktree-agent-a98a4fea4e0eb520d` |
| GM verify commit 存在 | `git log -1 worktree-agent-a98a4fea4e0eb520d` 有 commit, 通过 |
| GM merge --no-ff | merge commit `2560966` into main, 保留 merge commit |
| GM ctest | ctest 441/441 PASS |
| cleanup | `git worktree remove` 成功, `git branch -d` 成功, 0 数据丢失 |

**结论:** 标准 8 步流程实证跑通, 无 GM 错 #21 重现, 数据完整.

---

## §11 不耻下问 / 后续行动

- **@老雷 (GM):** GM 错 #19/#20/#21 retro 书面确认 + ADR-024 ack. W8 W2 起所有 IC 派单 prompt 必带 §3 模板.
- **@老高:** PR v1.6 `worktree_commit_check.py` + `gm_merge_audit.py` 落地 (W8 W3, 优先级 P1).
- **@老胡:** 周报 §12 新增 worktree 流程 KPI: worktree 使用率 / commit 率 / P0 例外次数 / merge audit warn 次数.
- **@全 IC:** W8 W2 起派单 prompt 收到 §3 模板必执行 git commit, 回汇必带 branch + hash.
- **@老周 (A 系统工程部):** 老孙 GM 错 #19 (Edit 跨 path) 已记录, 下次派单确认 worktree path 意识.

---

## §12 落地 checklist

- [x] ADR-024 立 (本文件, 老郭 W8 W2)
- [ ] GM (老雷) ADR-024 ack + W8 W2 起派单加 §3 模板
- [ ] 老高 PR v1.6: `worktree_commit_check.py` + `gm_merge_audit.py` (W8 W3)
- [ ] 老胡 周报 §12 加 worktree 流程 KPI (W8 W3 首期)
- [ ] AGENT.md F 顾问团 老郭条目加 ADR-024 引用 (小米落)

---

**最后更新:** 2026-06-W3 by 老郭 (架构评审 + F 顾问团协调人, Wave 36)
