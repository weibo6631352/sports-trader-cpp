---
name: adr-033-ci-automation-feedback
description: 全面自动化 CI 流程 + 反馈机制 — 6 项落地 + KPI 体系
owner: 老高 (#16, F 顾问团, code-quality-reviewer)
last_review: 2026-05-29
status: ACCEPTED
metadata:
  type: ADR
  id: ADR-033
---

# ADR-033: 全面自动化 CI 流程 + 反馈机制

- **ID:** ADR-033
- **Date:** 2026-05-29 (W10 W3)
- **Status:** ACCEPTED (老郭 主审 ACCEPTED, 老高 主实施)
- **触发:** 老板 5/29 verbatim "本地远端 worktree workflow 分支 联动 反馈 自动化"
- **关联:**
  - `docs/ADR/2026-06-W4-adr-032-local-first-ci-strategy.md` (ADR-032, 本地优先 CI)
  - `docs/ADR/2026-06-W4-adr-029-worktree-push-pr-flow.md` (ADR-029, push+PR 流程)
  - `.github/workflows/notify-on-main-failure.yml` (6 项第 4 项)
  - `.github/workflows/close-ci-fail-on-green.yml` (6 项第 6 项)

---

## §1 老板 Verbatim

> "本地远端 worktree workflow 分支 联动 反馈 自动化"

(2026-05-29, W10 W3, Task #123+125+老板 5/29 audit gap 合并波)

---

## §2 6 项自动化全套实施状态

| # | 项目 | 状态 | 落地位置 |
|---|---|---|---|
| 1 | `delete_branch_on_merge=true` (repo setting) | DONE | GitHub repo settings |
| 2 | `cleanup-old-runs.yml` (cron 周一, 失败 runs > 1d 删, 全部 > 7d 删) | DONE | `.github/workflows/cleanup-old-runs.yml` |
| 3 | `cleanup-stale-branches.yml` (cron daily, worktree-agent-* stale 删) | DONE | `.github/workflows/cleanup-stale-branches.yml` |
| 4 | `notify-on-main-failure.yml` (main CI fail → auto-create Issue) | DONE | `.github/workflows/notify-on-main-failure.yml` |
| 5 | pre-push hook 例外: branch deletion 跳过 + SEGFAULT retry 3 次 | DONE (Wave 101) | `.git/hooks/pre-push` |
| 6 | `close-ci-fail-on-green.yml` (main CI green → auto-close ci-fail Issue) | DONE (Wave 101) | `.github/workflows/close-ci-fail-on-green.yml` |

---

## §3 流程图

```
sub-agent push worktree branch
       |
       v
[pre-push hook] (本地, ~30s)
   - branch deletion? → 跳过全部检查 (EXIT 0)
   - cmake build → ctest (SEGFAULT retry ≤3) → clang-format → 5 grep
   - FAIL → reject push (修后 re-push)
   - PASS → push 继续
       |
       v
[远端 CI: PR Linux Checks + PR Checks]  ← sub-agent 不等 (ADR-032 策略 3)
   - Linux GCC 13 build + UBSAN
   - clang-format-19 pin
   - perf bench
       |
       v
[main merge (GM review + gh pr merge)]
       |
       +--[CI fail]--→ [notify-on-main-failure.yml]
       |                   → auto-create GitHub Issue (ci-fail + p0)
       |                   → assign 老高
       |
       +--[CI green]--→ [close-ci-fail-on-green.yml]
                           → 查同名 workflow open ci-fail Issues
                           → auto-close + comment (SHA + run URL)

[cleanup-old-runs.yml] (cron 周一)
   → 删 failure runs > 1d
   → 删 all runs > 7d (keep ≥5)

[cleanup-stale-branches.yml] (cron daily)
   → 删 worktree-agent-* branches (无 open PR + last commit > 1d)
```

---

## §4 KPI 体系

| KPI | 目标 | 监控 |
|---|---|---|
| MTTR (main CI fail Issue 存活时间) | <= 30 min (正常 CI 绿后自动关) | 老高 周报 §ci |
| ci-fail Issue 自动关闭率 | >= 90% | `gh issue list --label ci-fail --state closed` weekly |
| fail runs 清理成功率 | >= 95% (每周一 cron) | cleanup-old-runs.yml 日志 |
| stale branches 清理成功率 | >= 95% (每日 cron) | cleanup-stale-branches.yml 日志 |
| pre-push 本地拦截率 | >= 80% (本地 reject / 总 push) | 老高 抽样估算 W10 W2 数据 |
| SEGFAULT retry 触发率 | <= 5% (偶发才触发) | pre-push 日志 grep "SEGFAULT/SIGABRT" |

**Fail Issue dispatch SLA:** main CI fail → Issue 创建 <= 5 min (workflow_run latency).

---

## §5 pre-push hook 例外说明 (Task #123)

### 5.1 Branch deletion 例外

**触发条件:** `git push origin :branch-name` 或 `git push --delete origin branch-name`

**实证:** 老彭 Wave 94 — `git push --delete origin worktree-agent-xxx` 触发 pre-push,
hook 读 stdin 拿不到有效 LOCAL_SHA, grep 脚本误判 fail, push reject.

**修复:** hook 开头读 stdin — LOCAL_SHA = `0000...0` (40 个零) 即 branch deletion, 整个 hook `exit 0`.

**安全性:** branch deletion 不上代码, 跳过检查无风险.

### 5.2 SEGFAULT retry 例外

**触发条件:** `ctest` 返回 exit code 139 (SIGSEGV) 或 134 (SIGABRT)

**实证:**
- 老彭 Wave 94: ASAN 偶发 SIGABRT (134) on macOS arm64, 再跑 pass
- 小尤 Wave 96: SIGSEGV (139) 偶发于 test_paper_signer + UBSAN on Apple M2

**修复:** ctest 失败分支判断 exit code, 139/134 走 retry 最多 3 次; 其他 exit code 直接 reject (真 fail).

**约束:**
- 非 SEGFAULT fail (exit 1 = assert/test logic fail) → 不 retry, 直接 reject
- 3 次 retry 后仍 SEGFAULT → reject + 提示检查 ASAN/UBSAN 报告
- retry 间隔: 0 (直接重跑, 不 sleep)

---

## §6 Task #125 CI fail 通知 extended (未落 / 后续计划)

| 通知渠道 | 状态 | 说明 |
|---|---|---|
| GitHub Issue (ci-fail) | DONE (notify-on-main-failure.yml) | 已落 |
| Slack/Discord webhook | 待落 (后续 sprint) | 需总裁给 webhook URL; 老高 placeholder 已有 |
| 邮件 alert | 文档方案 | GitHub Watch settings → email; 老板 README 一节 |
| Issue auto-close on green | DONE (close-ci-fail-on-green.yml, Wave 101) | 本 ADR §2 第 6 项 |

Slack/Discord 落地条件: 总裁或老雷提供 webhook URL → 老高 1 wave 落 `notify-on-main-failure.yml` 扩展.

---

## §7 联动说明

- **ADR-032 §3 策略 1** (本地 pre-push) — 本 ADR §5 是其 Task #123 升级
- **ADR-032 §3 策略 3** (sub-agent 不等 CI) — 不等远端, 但 main CI fail 有 Issue 兜底
- **ADR-029 §3** (worktree push+PR 流程) — sub-agent push 后立刻 gh pr create, hook 不阻塞 branch deletion cleanup

---

## §8 不耻下问

- @老高 主审 + 全套实施 owner
- @老郭 ADR-033 主审 (ACCEPTED)
- @老雷 6 项全套实施 sign-off

---

**最后更新:** 2026-05-29 by 老高 (Wave 101, 6 项 DONE, ADR-033 ACCEPTED)
