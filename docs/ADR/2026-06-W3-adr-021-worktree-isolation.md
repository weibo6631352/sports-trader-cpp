# ADR-021: sub-agent worktree 物理隔离 (W8 起强 enforce)

- **ID:** ADR-021
- **Date:** 2026-06-W3 (W7 冷静周末)
- **Status:** Accepted (老板 ACK 选 C: W7 容忍 + W8 起强)
- **触发:** 用户 "又在冲突了" → 老板拍板选项 C

---

## 1. 背景

W6 W3 + W7 重复出现 sub-agent 同 working tree 抢占:
- 老孙 顺手修小卢 single_instance.cpp
- tests/unit/CMakeLists.txt 4-5 IC 改重叠 (W6 W2 + W6 W3)
- 小宋 W7 报告 "ninja 缓存脏状态" (老唐 audit_emitter friend 删除 in-flight 触发)
- GM 错 #15: 派 Wave 33 7 sub-agent 没用 `isolation: worktree`

**根因:** Claude Code Agent tool **默认共享同一 working directory**, sub-agent 并行写文件无 git lock / branch isolation, 必然抢占.

**Agent tool 已提供 `isolation: "worktree"` 选项** — 每个 sub-agent 独立 git worktree (branch 自动管理), 互不抢占.

## 2. 决策

**W8 起所有 Agent tool 调用强制 `isolation: "worktree"` (条件 enforce):**

```python
# OLD (W7 及之前):
Agent(
    subagent_type="...",
    model="sonnet",
    prompt=...
)

# NEW (W8 起):
Agent(
    subagent_type="...",
    model="sonnet",
    isolation="worktree",  # ← 强约束
    prompt=...
)
```

**适用范围:**
- ✅ 所有 IC 写代码 / 写文档 / 写测试 任务
- ✅ 所有主管 / 协调人 / 顾问 review 任务 (即使 review only, 避免无意 touch)
- ⚠️ 例外: 紧急 P0 < 2h 响应 (ADR-005 §3.2) 可不带 worktree (但必须显式标 "P0 例外, 老板 ack")

## 3. 工作流变更

**W8 起 wave 推进流程:**

```
Step 1. GM 派 wave (Agent tool 必带 isolation: "worktree")
        ↓
Step 2. 各 sub-agent 自动在独立 worktree 工作
        ├ 自动 git checkout -b agent-<persona>-<wave>
        ├ 在独立 path 写文件 (与 main working tree 物理隔离)
        └ 完成后自动 commit to branch
        ↓
Step 3. sub-agent 完成回汇, 报告 branch name
        ↓
Step 4. GM 整合 (本人 + 老胡 协助):
        ├ 各 sub-agent branch merge into main (或先 review)
        ├ merge conflict 立刻 派回 原 owner 修 (GM 不代修, #13 教训)
        ├ 整合后 ctest + lint 验证
        └ commit + push
```

## 4. W7 Wave 33 处理 (老板拍板选 C, 单次容忍)

- Wave 33 **不撤回, 不重派** (已 spent token)
- GM **不自己 hotfix 冲突** (严守 #13 教训)
- 冲突文件冲突 → 派回原 owner 修 + 等回汇
- W7 整合 commit 后, W8 W1 起所有 wave 强 worktree

## 5. CLAUDE.md §10 加约束

```
- **W8 起 sub-agent 必用 worktree (ADR-021):**
  - GM 派 Agent tool 必传 `isolation: "worktree"` 参数
  - 老高 PR v1.5 加 grep: 派单 prompt 文件 grep `Agent(` 必紧跟 `isolation="worktree"` (允许例外: P0 < 2h)
  - 老胡周报 §11 加 "worktree 使用率" KPI (W8 起目标 100%, P0 例外标注)
```

## 6. 与 ADR-005 §3.4 FOM 协同

ADR-005 §3.4 FOM 是**逻辑层** 文件 ownership 约束 (派单 prompt 列允许文件).

ADR-021 worktree 是**物理层** 文件隔离 (sub-agent 独立 git worktree, 写不到 main tree).

**双层防御:**
- FOM (逻辑) 防 sub-agent "顺手修" (老孙 W6 W3 案例)
- worktree (物理) 防 sub-agent 并行写抢占 (Wave 33 5 IC 重叠 case)

## 7. KPI (老胡 周报 §11)

- worktree 使用率 (W8 起 目标 100%)
- 文件抢占次数 (W8 起目标 0)
- P0 例外 (不带 worktree) 次数 + 理由 (W8 起每周 ≤ 1)

## 8. 落地动作

- [x] ADR-021 立 (本文件)
- [ ] CLAUDE.md §10 加 worktree 约束 (W7 末 GM 落)
- [ ] 老高 PR v1.5 加 worktree grep (W8 起)
- [ ] 老胡 周报 §11 加 worktree KPI (W8 W1 首期)
- [ ] W8 起 GM 所有 Agent 调用必带 isolation="worktree"

## 9. 老板原话 verbatim 入约束

> 选 C: 等齐 Wave 33 + W8 起强 enforce worktree (Wave 33 单次容忍)
> 风险标注: "Wave 33 整合时 GM 仍可能越权"

**GM 公开承诺:** Wave 33 整合时**严守 #13 教训**, 不越权代修. 冲突 → 派回 owner. GM 整合仅做:
- ctest 验证
- lint 验证
- diff stat 验证
- 不修代码

## 10. GM 错 #15 关联

ADR-021 = GM 错 #15 永久 enforcement (worktree 物理隔离).

---

**最后更新:** 2026-06-W3 by 老雷 (GM, 老板选 C 决议触发)
