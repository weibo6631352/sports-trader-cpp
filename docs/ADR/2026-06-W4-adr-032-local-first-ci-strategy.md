---
name: adr-032-local-first-ci-strategy
description: 本地优先 CI 策略 — 远端精简 + 本地 pre-push hook + sub-agent 不等远端 (老板 5/29 verbatim)
owner: P-00 (总裁草案) → 老高 W10 W1 主审 + 实施
last_review: 2026-05-29
status: Draft
metadata:
  type: ADR
  id: ADR-032
---

# ADR-032: 本地优先 CI 策略 — 远端精简, sub-agent 不等远端

- **ID:** ADR-032
- **Date:** 2026-05-29 (W9 W5)
- **Status:** Draft (总裁 P-00 草案 → 老高 W10 W1 主审 + 实施)
- **触发:** 老板 5/29 verbatim 4 问

---

## §1 老板 verbatim (一字不改)

> "github workflows 远端运行太慢了, 有必要原地等待远端结果吗, 我们的 github workflows 是否太长了, 有些东西是否在本地验证更加高效, 远端和本地环境不一样吗, 看老高等了好久"

(2026-05-29, W9 W4 老高 Wave 75/77 + W81 CI iteration 3 次, 总裁观察验证)

## §2 现状问题诊断

### 问题 1: 远端慢

- 每个 PR CI run 30-60s (Linux build + 24 grep jobs)
- 多个 PR 排队 (Wave 81 跑中, Wave 84-88 几乎同时 push, CI 排队)
- sub-agent push 后等 CI = 浪费 token + wall time

### 问题 2: workflow 太长 (24+ job)

当前 jobs (pr.yml v1.7, 老高 Wave 60+74):
- 17 grep job (Python redline + ABI + cite + grep)
- 3 build job (paper ASAN/UBSAN + release UBSAN + RM 单测)
- 1 perf bench
- 1 R-12 PIT
- 1 ABI lock
- 1 FOM ref check
- 1 ADR-021 worktree
- 1 ADR-024 pwd verify
- 1 ADR-027 cite
- ...

**24+ job 大部分本地可跑** (Python script + grep + clang-format dry-run + ctest).

### 问题 3: 本地 vs 远端环境差异

| 项 | 本地 (mac) | 远端 (Ubuntu) |
|---|---|---|
| OS | macOS arm64 | Ubuntu x86_64 24.04 |
| Compiler | Apple clang 16+ | GCC 13 / clang 18 |
| clang-format | 22.1.6 (brew llvm) | 18.1.3 (apt) |
| Build flags | host default | -Wall -Werror -Wextra |
| Linux-only | n/a | gmock / glibc warnings |
| Perf tools | mac SDK | Linux perf |

→ **clang-format mac 22 vs CI Linux 19 是 fail 来源** (Wave 74 老高 install clang-format-19 fix).

### 问题 4: sub-agent 阻塞等远端

ADR-029 §3 Step 5.4: "CI fail → sub-agent 自修". 实际上:
- sub-agent push → 等 CI 30-60s → 看 fail → 修 → 再 push → 再等
- 3 次 iteration = 3-5 min wall time, 大量 token 浪费

## §3 ADR-032 新策略 (W10 W1 起强 enforce)

### 策略 1: 本地 pre-push hook (强 enforce)

`.git/hooks/pre-push` (老高 W10 W1 实施):
```bash
#!/bin/bash
# 本地必跑 (< 30s 完成)
set -e

# 1. cpp build (paper mode)
cmake --build build --parallel || exit 1

# 2. ctest 全跑
ctest -j 4 --test-dir build || exit 1

# 3. clang-format (本地版本, 18+ compat 而非 strict 19)
clang-format --dry-run --Werror $(git diff --name-only HEAD..origin/main | grep -E '\.(hpp|cpp)$') || exit 1

# 4. Python grep (本地必跑, 与远端一致)
python3 tests/ci_grep/abi_lock.py || exit 1
python3 tests/ci_grep/risk_enum_coverage.py || exit 1
python3 tests/ci_grep/adr010_wno_check.py || exit 1
python3 tests/ci_grep/core_data_structure_ssot_check.py || exit 1
python3 tests/ci_grep/worktree_commit_check.py || exit 1

echo "✅ 本地 pre-push 全过"
```

效果:
- sub-agent push 前自动跑 30s
- 失败 push reject, sub-agent 立刻看错修, 不 round-trip 远端
- 老板"本地验证更加高效" 100% 落实

### 策略 2: 远端 CI 精简 (只跑差异/平台 specific)

`.github/workflows/pr.yml` v2 (老高 W10 W1 实施):

**只保留 Linux specific** (本地跑不了的):
- `ci-build-paper-linux` (Ubuntu GCC 13 build verify)
- `ci-build-release-linux` (Ubuntu UBSAN)
- `ci-ctest-linux` (Linux ctest 全跑, 含 gmock 兼容)
- `ci-clang-format-linux` (Linux clang-format-19 specific)
- `ci-perf-bench` (Linux perf 工具)

**移除 (本地已跑)**:
- 17 Python grep job → 移除 (本地 pre-push 已跑)
- ABI lock → 移除 (本地)
- Cite check → 移除 (本地)
- pwd verify → 移除 (本地)

剩 5-6 job, 每 PR 远端 ~15-25s.

### 策略 3: sub-agent 不等远端 (ADR-029 §3 Step 5 升级)

ADR-029 §3 Step 5.4 update:

```
原: sub-agent push → 等 CI → 看 fail → 修
新: sub-agent push → 立刻继续下一 task (不等)
   - 本地 pre-push 已跑 95% 检查 (本地 verify 是裁判)
   - 远端 CI 是补充 (Linux specific verify)
   - CI fail → GM merge 时决定 (admin merge 或回炉)
   - sub-agent 不为远端 CI iteration 浪费时间
```

### 策略 4: GM merge 策略

- CI 全 pass → `gh pr merge --squash` (干净 merge)
- CI 部分 fail (Linux env / perf 等) → GM 视情 `--admin --squash` (本地 verify pass + 非红线)
- 红线 fail (Build paper fail = 真 bug) → 回炉 sub-agent 修

## §4 实施 timeline

- W10 W1 Mon: 老高 W10 W1 实施 pre-push hook + workflow v2 (本 ADR ack 后)
- W10 W1 Tue: 全员 W10 wave 起用新策略 (本地 pre-push + 远端精简)
- W10 W2: 老高 监控效果 + 周报 数据 (PR CI 时间 / iteration 次数 / sub-agent wall time)

## §5 风险

- Risk 1: sub-agent 没装 clang-format/python/cmake → 老高 W10 W1 检查 docker/onboard plan
- Risk 2: 本地 mac vs Linux 真有差异 → CI Linux specific 还在, 守底线
- Risk 3: 红线 grep 漏跑 → 老高 pre-push 必含红线 grep

## §6 与现有 ADR 联动

- **ADR-024 (worktree+pwd verify)**: 不变
- **ADR-029 (push+PR 流程)**: §3 Step 5 update (sub-agent 不等 CI)
- **ADR-030 (员工主动上报)**: 员工发现本地脚本 bug → PUSH_BACK
- **ADR-031 (Sprint 4 必要条件)**: W10 plan 必含本 ADR 落地 ticket

## §7 W82 PR #7 W10 plan 整改 (ADR-031 §5 衔接)

老胡 W10 plan v2 (W10 W1 多人讨论会 ack) 必含:
- 老高 W10 W1 实施 ADR-032 策略 1+2+3
- 全员 W10 wave 用新策略
- 老高 W10 W2 监控数据

## §8 不耻下问

- @老高 W10 W1 主审 + 实施 owner
- @老郭 ADR-032 主审 ACCEPTED 流程
- @老胡 W10 plan v2 含本 ADR ticket
- @全员 W10 wave 用新策略 (老高 onboard)

## §9 总裁 P-00 PUSH_BACK 自检 (ADR-030)

我作为总裁 W8 W4 立 ADR-029 时没考虑"本地优先"原则, 导致 W9 W2-W5 老高 CI iteration 3-5 次浪费时间. 老板 5/29 verbatim 验证. 本 ADR-032 是 self-correction.

PUSH_BACK: 总裁失职 — W82 PR #7 W10 plan 1 人定 + ADR-029 没含 pre-push hook = 同期 2 项错位. ADR-031 + ADR-032 同期立项是补救.

---

**最后更新:** 2026-05-29 by 总裁 P-00 (草案, 老高 W10 W1 主审 + 实施)
