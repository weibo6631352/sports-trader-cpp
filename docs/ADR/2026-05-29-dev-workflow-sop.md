# ADR-039: 开发工作流 SOP (制度 SSOT) + 自动化

- **ID:** ADR-039
- **Date:** 2026-05-29
- **Owner:** 老雷 (GM); 老吴 (CI/自动化) + 老高 (质量门) + 老郭 (架构门) + 小米 (doc) 维护
- **Status:** Accepted
- **触发 (老板 verbatim 2026-05-29):** "完善一下制度吧, 定规范, 能自动化的尽量自动化, 大家工作流确实很随意。" + "我们不会自动评审吗" + "pr 完成是不是需要关闭?" + "把状态弄对, 以后不要再遗漏了"
- **背景:** 制度文档不缺 (PR 模板 v1.7 + 一堆 ADR), 缺的是**统一入口 + 自动强制**。本 ADR 是工作流**唯一 SSOT 索引**, 并固化本会话暴露的随意点。

---

## 1. 本会话暴露的"随意"(反面教材, 禁止重演)

| 随意行为 | 谁 | 正确做法 |
|---|---|---|
| sub-agent "不 push, 主工作区改" | GM 早期派单 | **一切代码改动必经 worktree → PR**, 无"主工作区直接改" |
| `git push --no-verify` 绕过 gate | 老高 (Wave3) | **禁止 --no-verify**(除非 documented 紧急 + 逐项说明每个被跳检查为何安全) |
| 只跑自己新增测试就报"完成" | 老沈 (Wave3) | 报完成必跑**全量 ctest**, 贴通过数(见 [wave3 lessons](../INCIDENTS/2026-05-29-wave3-precommit-lessons.md)) |
| PR 堆积无人评审 / merge | GM | 每个 PR 必达终态(merged/closed), 不 dangling(§3) |
| #26 被遗忘 | GM | PR 周度清扫(§6) |
| 评审全靠手动临时派 | GM | 评审自动化(§4)+ SOP 化(§5) |

---

## 2. PR 生命周期 (强制, 无例外)

```
worktree(ADR-029, isolation=worktree)
  → 分支开发
    → push (pre-push gate 自动跑: build + 全量 ctest + clang-format + grep, ADR-032)
      → gh pr create (用 PR 模板, Definition of Done 勾全)
        → 自动评审 (claude-review.yml, §4) + reviewer 评审 (§5)
          → 评审通过 → merge (--squash --delete-branch) ← GitHub 自动关闭 PR
            → 终态: MERGED (或 CLOSED 若废弃)
```

**铁律:**
- ❌ 不直接 commit/push main(branch protection enforce, §4)
- ❌ 不 `--no-verify` 绕 gate
- ❌ 不"主工作区改不 push"
- ✅ "完成 PR" = **merge**(自动关闭), 不是手动 close; 手动 close 仅用于**废弃**该改动

## 3. 无 dangling PR (老板"把状态弄对")

- 每个 PR 必须走到终态: **MERGED**(完成)或 **CLOSED**(废弃), 不允许长期 OPEN 无动作
- 评审打回的 PR: owner 限时整改 → 重新评审 → merge; 或明确废弃 → close
- **周度 PR 清扫**: 老胡周报 §6 列所有 OPEN PR + 停留天数 + 阻塞点 + owner; > 7 天无动作的升级

## 4. 自动化清单 (能自动的全自动)

| 自动化 | 文件/机制 | 作用 |
|---|---|---|
| **pre-push gate** | `.git/hooks/pre-push` (ADR-032) | push 前自动 build+全量 ctest+clang-format+5 grep checks, 不过不让 push |
| **PR CI** | `.github/workflows/pr.yml` / `pr-linux.yml` | PR 自动 build+test(Linux) |
| **性能回归门** | `perf-regression.yml` | 热路径 p99 回归 > 10% 拦 PR; 观测 API 开关双跑 delta ≤ 2%(ADR-038) |
| **🆕 自动代码评审** | `claude-review.yml` (本 ADR 新增) | 每个 PR 自动 Claude 评审(Sonnet 第一道), 贴 review comment, [BLOCKING] 标重大问题 |
| **🆕 CODEOWNERS** | `.github/CODEOWNERS` (本 ADR 新增) | 红线路径(risk/signer)自动请求 owner 评审 |
| **nightly / 通知** | `nightly.yml` / `notify-on-main-failure.yml` | 夜跑 + main 失败告警 |
| **自动清理** | `cleanup-stale-branches.yml` / `cleanup-old-runs.yml` | 陈旧分支/run 自动清 |
| **green 自动关 fail PR** | `close-ci-fail-on-green.yml` | CI 转绿自动关 fail 标记 |
| **周报** | `weekly-report-cron.yml` | 老胡周报(含 §6 PR 清扫 + idle + Opus 监控) |
| **branch protection** | GitHub repo 设置(§7 待老板开) | main 禁直接 push, 必 PR + 必过 CI |

**ANTHROPIC_API_KEY**: `claude-review.yml` 需此 repo secret 才生效, 老板在 Settings → Secrets and variables → Actions 添加(未配时不阻塞, 评审降级到 §5 agent 评审)。

## 5. 评审 SOP (两道, 自动 + agent)

1. **第一道 — 自动 (claude-review.yml)**: 每个 PR 自动 Claude(Sonnet)评审, 覆盖正确性/性能/安全/红线/提交质量, 贴 comment。便宜(~$0.01-0.05/PR)、即时、零遗漏。
2. **第二道 — reviewer agent (代码 PR 必走)**:
   - **质量门**: 老高(code-quality-reviewer)— 命名/API/红线/提交质量
   - **架构/ABI 门**: 老郭(chief-architecture-reviewer)— ABI 变更/跨层/不可逆 schema
   - **红线路径加签**: risk/ → 老韩(RM 主权); signer/crypto → 老沈+老孙; 数据 schema → 小余
   - 文档型 PR: 第一道 + GM/小米 即可, 免第二道
3. **通过即 merge**, GM/主管不再临时起意才评审 —— 评审是流水线固定环节。

## 6. Definition of Done (报"完成"前, PR 模板强制勾)

见 `.github/pull_request_template.md`(老高 v1.7)+ [wave3 lessons 8 条清单](../INCIDENTS/2026-05-29-wave3-precommit-lessons.md)。核心三条:
1. 跑**全量 ctest**(不只自己的), 贴通过数
2. 改契约 → grep 所有下游消费方一起改
3. 本地 pre-push gate 全过, 未 --no-verify

## 7. 待老板/admin 一次性开启 (GitHub 设置)

- [ ] 加 `ANTHROPIC_API_KEY` secret(激活 claude-review.yml 自动评审)
- [ ] main branch protection: require PR + require status checks(pr-linux / claude-review)+ 禁 force push + 禁直接 push
- [ ] 开 auto-merge(PR 满足条件自动合, `gh pr merge --auto`)

> GM 已尝试用 gh 设 branch protection; 若权限不足见 §8 输出, 需老板在 repo Settings 手动开。

## 8. 落地

- [x] ADR-039 立(本文件, 工作流 SSOT 索引)
- [x] `.github/workflows/claude-review.yml` 自动评审
- [x] `.github/CODEOWNERS` 红线路径自动请评审
- [ ] 老板开 §7 三项 GitHub 设置
- [ ] 老胡周报加 §6 PR 周度清扫段
- [ ] 老吴 评估本地 pre-commit hook(把 gate 再前移一步, 缩短反馈环)

## 9. 完整工作流 + main/远端同步时机 (老板 2026-05-29 "把所有环节梳理清楚")

### 9.0 第一原则: origin/main 是唯一真相

**本地 `main` 只是个会过期的缓存指针, 不可信。** 任何"基于 main"的动作前, 必须 `git fetch` 并确认本地 main 已快进到 `origin/main`(或直接基于 `origin/main` 开分支)。这次连环事故的根因就是误信了落后/被污染的本地 main。

### 9.1 全环节 + 同步点(⟲ = 必须同步远端的时机)

| # | 环节 | 谁 | 动作 | 同步? |
|---|---|---|---|---|
| 1 | **派单前** | dispatcher | `git fetch origin`(拿最新远端 snapshot) | ⟲ **同步点 A** |
| 2 | **开 worktree** | dispatcher | 每个 worktree 从 `origin/main` 起: `git worktree add -b feat/X <path> origin/main` —— 不依赖本地 main 指针 | (基于 A 的快照) |
| 3 | **并行开发** | IC (各自 worktree) | 写 → `add` → `commit`。只动自己分支, **不 fetch/reset/rebase/checkout 别的分支, 不碰共享 ref** | — |
| 4 | **push 前** | IC | `git fetch origin && git rebase origin/main` → 自解冲突 → `cmake --build && ctest` 全量复跑绿 | ⟲ **同步点 B(关键)** |
| 5 | **push** | IC | 普通 `git push -u origin feat/X`; pre-push gate(build+全量ctest+format+grep)自动把关 | — |
| 6 | **PR + 评审** | 系统 + reviewer | claude-review 自动评审 + reviewer agent(§5) | — |
| 7 | **merge 前** | IC | 若评审期间 origin/main 又前进 → 再 `git rebase origin/main` 解冲突复跑(require-up-to-date) | ⟲ **同步点 C(按需)** |
| 8 | **merge** | reviewer/GM | `gh pr merge --squash --delete-branch`; GitHub 自动关 PR | — |
| 9 | **下一轮前** | dispatcher | 回到环节 1(`git fetch`); 本地 main 用前 `git pull --ff-only` 快进, 快进失败=分叉, 先解决 | ⟲ **回到同步点 A** |

### 9.2 三个同步时机(精炼回答老板)

- **A 派单/开工前**: dispatcher `fetch` 一次, 所有并行 worktree 共享这一最新远端快照。
- **B push 前**: IC 自己 `fetch + rebase origin/main + 解冲突 + 全量复跑`(从开工到完工 origin/main 可能被 sibling 推进了)。**这一步之前缺失, 是 #31/#32 冲突要 GM 手工解的根因。**
- **C merge 前(按需)**: 评审耗时久、其间 origin/main 又动 → 再 rebase 一次, 保证 merge 时是最新 base。

> **本地 main 同步时机 = 每次要基于它做事之前**(派单、建分支、快进), 一律先 `fetch` + 校验 `HEAD == origin/main`。平时本地 main 落后无所谓, 用前必同步。

### 9.3 铁律(本会话事故固化)

1. **作者职责**: push 前/merge 前的 rebase + 解冲突 + 复跑由**作者自己做**, 不甩给 GM 在 merge 时替解(替解 = 掩盖缺口, 这次 GM 犯了)。
2. **禁 agent 在共享工作树做 git 手术**(`reset --hard`/`rebase`/`push --force`/切别的分支)—— #30 共享树污染的直接原因 = 派单让 agent 做 git 手术。agent 只在自己 worktree 普通流程。
3. **dispatcher 工作树洁癖**: `checkout`/`branch` 前清 uncommitted 残留; `--ff-only` 失败必须察觉处理, 不静默继续。
4. **每个动作前校验**: 基于 main 的操作前 `[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ]`。

## 10. 已知漏洞 + 待办(老板"看看有没有遗漏/不合理")

| 漏洞 | 影响 | 建议 | owner |
|---|---|---|---|
| **CMakeLists.txt 单点 append** | 每个并发 PR 同处加 test target → 必冲突 | CMake 测试**自动发现**(`file(GLOB)`/self-register), 根除冲突 | 老吴+老高 |
| **branch protection 私有 repo 不可用** | 无法自动 enforce 必 PR/必最新/禁直推(需 GitHub Pro) | 老板定: 升 Pro / 靠 CI+纪律(§9) | 老板 |
| **作者漏做提交前 rebase 解冲突** | GM 替解掩盖缺口 | 入 Definition of Done + CI require-up-to-date | 老高(模板)+全员 |
| **并发 background agent 过多 + git 手术** | 多 agent 同写共享区/做手术 → 污染(本次 #30) | 同区文件同时 1 写者; §9 禁手术 | GM 派单纪律 |
| **本地 build/ 缓存陈旧** | 文件增删后 incremental build 失败 → 卡 push | reconfigure(`cmake -B build`)或 CI 干净环境构建 | 老吴 |

## 11. #30 共享树污染事故(post-mortem 摘要)

- **现象**: 小冯 #30 整改 agent 被派去在共享树做 `reset --hard`/rebase 手术 → 污染本地 repo + #30 分支"领先10落后2"; GM 本地 HEAD 反复错位; stale build 卡 push。
- **未扩散**: **origin/main 全程干净完整**(所有 PR + 全部 test target 在), 仅 GM 本地受污染, 已 `reset --hard origin/main` 修复。
- **小冯的"删除"是对的**: 删 `kAdapterBookDepth=20` 改用 SSOT `kBookDepthLevels=5`(老高评审要求), 非乱删冲突。
- **根因链**: 派单让 agent 做 git 手术 + dispatcher 残留未清 + 未每步校验 HEAD + 作者未提交前 rebase 解冲突 + stale build。
- **整改**: §9 全环节 + 同步时机入制度; #30 作废, 待小冯在隔离 worktree、从 origin/main、普通流程干净重做。

---

**最后更新:** 2026-05-29 by 老雷 (GM) — 完善制度+自动化 + 全环节同步时机(§9)+ #30 事故教训
