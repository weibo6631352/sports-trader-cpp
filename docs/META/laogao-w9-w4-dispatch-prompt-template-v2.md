# 派单 prompt 模板 v2 — 老高 (W9 W4)

- **owner:** 老高 (#17, code-quality-reviewer, F 顾问团)
- **last_review:** 2026-05-29
- **status:** SSOT (v2, 替换 ADR-024 §3 模板)
- **触发:** ADR-029 (老板 verbatim 2026-05-29: "让他们自己在自己的 worktree 拉取合并推送才合理吧")
- **关联:**
  - `docs/ADR/2026-06-W3-adr-024-worktree-standard-workflow.md` §3 (v1 基线, ADR-029 fallback)
  - `docs/ADR/2026-06-W4-adr-029-worktree-push-pr-flow.md` (ADR-029, v2 来源)
  - `docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md` (ADR-027 cite 强 enforce)

---

## §1 何时用 v2 vs v1

| 版本 | 场景 | 备注 |
|---|---|---|
| **v2 (本文件)** | 所有 IC 派单 (W9 W4 起) | sub-agent 自己 push + gh pr create |
| **v1 (ADR-024 §3)** | ADR-029 §4 例外场景 | P0 hotfix / ADR 立项 / ABI breaking 大改 |

**默认用 v2.** 例外场景在派单 prompt 标注 `flow: adr-024-fallback`.

---

## §2 模板正文 (原文嵌入每次 IC 派单 prompt 末尾)

以下模板**必须原文嵌入**每次 IC 派单 prompt 末尾. 禁止省略或缩写.

---

```
---
**ADR-021 / ADR-024 / ADR-029 worktree + push + PR flow (强 enforce, W9 W4 起):**

**Step 0. pwd verify (GM 错 #19 教训)**
  pwd
  # 预期: .../sports-trader-cpp/.claude/worktrees/agent-<your-id>
  # 非预期 → 停止, 不执行后续任何 git 操作

  仅 Edit / Write worktree path 文件:
    允许: .claude/worktrees/agent-<your-id>/... 路径下的文件
    禁止: /Users/.../sports-trader-cpp/ 根目录下非 worktree 路径的文件

**Step 1. git add + commit (GM 错 #20 教训)**
  git add -A
  git commit -m "<type>(<scope>): <summary> (<persona> Wave NN)"
  # 例: "feat(risk): OrderIntent v0.5 ABI align (老韩 Wave 68)"
  # 必含 persona + Wave NN

**Step 2. git fetch + merge origin/main (ADR-029 新流程)**
  git fetch origin
  git merge origin/main --no-edit
  # conflict 处理:
  #   第 1 次 conflict: 自己解 → git add <文件> → git commit → 继续
  #   第 2 次 conflict: 停止, 回汇 GM "conflict resolved 失败, 需 GM 介入"

**Step 3. git push origin (ADR-029 新流程)**
  git push origin worktree-agent-<your-id>

**Step 4. gh pr create (ADR-029 新流程)**
  gh pr create \
    --title "<type>(<scope>): <summary> (<persona> Wave NN)" \
    --body "## Summary
  - <1-3 bullet points>

  ## ADR cite
  - ADR-027: <polymarket SSOT cite 或 N/A (非核心 struct 改动)>
  - ADR-024: worktree commit + push done
  - ADR-029: new flow ✓

  ## ctest
  - ctest 结果: <NN/441 PASS 或 CI pending>

  ## Worktree
  - branch: worktree-agent-<your-id>
  - commit: <git log -1 --format=%h HEAD>"

**Step 5. 回汇 (必含以下 3 项)**
  - commit hash: <git log -1 --format="%h" HEAD>
  - PR URL: <gh pr view --json url -q .url>
  - ctest 状态: <NN/441 PASS 或 CI pending>

**例外 (不走 Step 2-4, 只走 Step 0-1 + 回汇):**
  - 派单 prompt 标注 flow: adr-024-fallback
  - 理由: P0 hotfix / ADR 立项 / ABI breaking 大改
---
```

---

## §3 ADR-027 cite 补充说明 (强 enforce)

PR body `ADR-027:` 字段填写规则:

| 情况 | 填写内容 |
|---|---|
| 改了核心 struct 文件 (OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot) | `ADR-027: laoli-polymarket-data-structure-ssot §<N> + <handshake-vN.md>` |
| 改了 Side / Outcome enum | `ADR-027: laoli-polymarket-data-structure-ssot §<N> (Side/Outcome)` |
| 无核心 struct 改动 | `ADR-027: N/A (无核心 struct 改动)` |

填 N/A 须带括号说明. 不填 → CI `core_data_structure_ssot_check.py` FAIL.

---

## §4 v1 → v2 Changelog

| 维度 | v1 (ADR-024 §3) | v2 (本文件, ADR-029) |
|---|---|---|
| **git commit** | Step 3 (不变) | Step 1 (不变) |
| **git fetch** | 无 | **Step 2 新增** |
| **git merge origin/main** | 无 | **Step 2 新增** |
| **git push origin** | 无 | **Step 3 新增** |
| **gh pr create** | 无 | **Step 4 新增** |
| **回汇内容** | branch + hash | **hash + PR URL + ctest** |
| **conflict 处理** | GM 全权 | **sub-agent 第 1 次; GM 第 2 次** |
| **ADR-027 cite** | PR template §9.5 (已有) | **PR body 模板内嵌** |
| **适用范围** | 全员 (W8 W2 起) | 全员 (W9 W4 起, 例外见 §1) |

**净增量:** sub-agent 承接 Step 2-4 (fetch + merge + push + pr create). GM 职责缩减为 review + merge.

---

## §5 快速参考 (一行命令)

sub-agent worktree 完成后, 5 条命令序列:

```bash
# 1. verify
pwd

# 2. commit
git add -A && git commit -m "feat(scope): summary (Persona Wave NN)"

# 3. sync
git fetch origin && git merge origin/main --no-edit

# 4. push
git push origin worktree-agent-$(basename $(pwd))

# 5. PR
gh pr create --title "feat(scope): summary (Persona Wave NN)" --body "..."
```

---

**最后更新:** 2026-05-29 by 老高 (#17, code-quality-reviewer, F 顾问团, Wave 73)
