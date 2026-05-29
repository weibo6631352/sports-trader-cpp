#!/usr/bin/env bash
# .git/hooks/pre-push — ADR-032 §3 策略 1: 本地 pre-push hook
#
# 老高 Wave 89 实施 (ADR-032 §3 策略 1 落地)
# 老高 Wave 101 升级 (Task #123: branch deletion 例外 + SEGFAULT retry)
# 老高 修复 (PR_BODY 从 push 范围 commit message 派生 — 解直推 main 改 ABI 锁定文件 blocker)
# cite: docs/ADR/2026-06-W4-adr-032-local-first-ci-strategy.md §3 策略 1
#
# 目标: push 前本地跑 ~30s, fail 则 reject push (不让坏代码上远端)
# 覆盖: cmake build + ctest + clang-format dry-run + 5 grep checks
#
# ADR-032 约束:
#   - 本地 pre-push 是"裁判", 远端 CI 是补充 (Linux specific)
#   - sub-agent 不等远端 CI (ADR-032 §3 策略 3)
#   - 不加 -Wno- 全库 (ADR-032 §3 + W81 反例教训)
#
# 例外 (Wave 101 Task #123):
#   - branch deletion push (LOCAL_SHA = 0...0) 整个 hook 跳过
#   - SEGFAULT/SIGABRT/SIGTRAP 偶发: ctest 失败时自动 retry 最多 3 次 (老彭 Wave 94 + 小尤 Wave 96 实证)
#
# PR_BODY 来源 (本次修复):
#   ADR-039 直推 main 工作流下, PR description 不存在, 但治理 (ABI ref + 三方签)
#   已走在 commit message 里. 本 hook 从本次 push 范围 (REMOTE_SHA..LOCAL_SHA) 的
#   commit message 派生 PR_BODY 传给 abi_lock.py / core_data_structure_ssot_check.py.
#   治理不放水: ABI ref + 三方签 必须真实存在于被推送的某个 commit message 里;
#   CI 环境仍用真实 PR body (脚本逻辑不改, 只改 PR_BODY 来源).
#
# 使用:
#   自动: git push 时触发
#   跳过 (紧急): git push --no-verify (须有 P0 理由 + incident)
#
# 维护人: 老高

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

# ---------------------------------------------------------------------------
# stdin 读取 (git hook 标准协议): <local_ref> <local_sha> <remote_ref> <remote_sha>
#   - branch deletion: LOCAL_SHA = 0...0 → 跳过全部检查 (Wave 101 Task #123)
#   - 否则记录本次 push 的 SHA 范围, 用于派生 PR_BODY
#     PUSH_LOCAL_SHA  = 最后一条 ref 的 local_sha (push 范围终点, 即将上远端的 HEAD)
#     PUSH_REMOTE_SHA = 最后一条 ref 的 remote_sha (新分支为 0...0)
# git 在调 hook 前已把 stdin 接上, 一次 push 多分支则逐行读 (取最后一条作范围).
# ---------------------------------------------------------------------------
ZERO_SHA="0000000000000000000000000000000000000000"
PUSH_LOCAL_SHA=""
PUSH_REMOTE_SHA=""
while read -r LOCAL_REF LOCAL_SHA REMOTE_REF REMOTE_SHA; do
    # 空行 (无 ref) 跳过
    [ -z "${LOCAL_SHA:-}" ] && continue
    if [ "$LOCAL_SHA" = "$ZERO_SHA" ]; then
        echo "[pre-push] branch deletion push (${REMOTE_REF}), 跳过所有检查"
        exit 0
    fi
    PUSH_LOCAL_SHA="$LOCAL_SHA"
    PUSH_REMOTE_SHA="$REMOTE_SHA"
done

echo "[pre-push] ADR-032 §3 策略 1 本地检查开始..."

# ---------------------------------------------------------------------------
# PR_BODY 派生: 从本次 push 范围的 commit message 拼接 (替代旧硬编码 "N/A")
#   - 终点: PUSH_LOCAL_SHA (无则回退 HEAD)
#   - 起点: PUSH_REMOTE_SHA (远端已有此分支); 为 0...0 (新分支) 或不可解析时, 用 origin/main
#   - range = <start>..<end>; 任一步读不到 → 保守回退 origin/main..HEAD
#   - 最终仍读不到 → 保守用 "local-pre-push N/A" 哨兵 (非空, 确保 ABI 检查真正执行而非 SKIP)
# 治理不放水: ABI ref + 三方签 必须真实在被推送的某条 commit message 里, 否则 abi_lock FAIL.
# 宁可拦不可漏: 边界回退一律落到"可解析的范围", 绝不给空 PR_BODY (空会触发脚本 SKIP).
# ---------------------------------------------------------------------------
derive_pr_body() {
    local end_sha="${PUSH_LOCAL_SHA:-HEAD}"
    local start_sha="$PUSH_REMOTE_SHA"
    local range=""

    if [ -n "$start_sha" ] && [ "$start_sha" != "$ZERO_SHA" ] \
        && git rev-parse --verify --quiet "${start_sha}^{commit}" >/dev/null 2>&1; then
        # 远端已有该分支: push 新增范围 = remote_sha..local_sha
        range="${start_sha}..${end_sha}"
    elif git rev-parse --verify --quiet "origin/main^{commit}" >/dev/null 2>&1; then
        # 新分支 / remote_sha 不可解析: 回退 origin/main..<end>
        range="origin/main..${end_sha}"
    fi

    local body=""
    if [ -n "$range" ]; then
        body="$(git log --format='%B' "$range" 2>/dev/null || true)"
    fi
    # range 为空, 或范围内无新 commit (如重推已 merge 的旧 commit) → 回退 origin/main..HEAD
    if [ -z "$body" ]; then
        body="$(git log --format='%B' "origin/main..HEAD" 2>/dev/null || true)"
    fi
    # 仍读不到 → 非空哨兵 (脚本见空 PR_BODY 会 SKIP, 给哨兵确保 ABI 检查真正执行)
    if [ -z "$body" ]; then
        body="local-pre-push N/A"
    fi
    printf '%s' "$body"
}

PR_BODY_DERIVED="$(derive_pr_body)"

# ---------------------------------------------------------------------------
# 1. CMake build (paper mode, 已有 build/ 则增量; 无则提示)
# ---------------------------------------------------------------------------
if [ -d build ]; then
    echo "[pre-push] 1/5 cmake build (增量)..."
    if ! cmake --build build --parallel 4 2>&1 | tail -5; then
        echo "[pre-push] FAIL: cmake build 失败 — 修 build error 后再 push"
        exit 1
    fi
    echo "[pre-push] 1/5 build OK"
else
    echo "[pre-push] WARN: build/ 不存在, 跳过 build 检查"
    echo "  提示: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSTCPP_EXEC_MODE=paper -DBUILD_TESTING=ON"
fi

# ---------------------------------------------------------------------------
# 2. CTest (已有 build/ 才跑) — SEGFAULT retry 最多 3 次 (Task #123)
#    老彭 Wave 94 + 小尤 Wave 96 实证: 偶发 SEGFAULT (exit code 139) 非真 fail
#    retry 3 次: 两次 SEGFAULT 后仍失败才 reject push
# ---------------------------------------------------------------------------
if [ -d build ] && [ -d build/tests ]; then
    echo "[pre-push] 2/5 ctest (SEGFAULT retry=3)..."
    CTEST_MAX_RETRY=3
    CTEST_ATTEMPT=0
    CTEST_PASS=0
    while [ "$CTEST_ATTEMPT" -lt "$CTEST_MAX_RETRY" ]; do
        CTEST_ATTEMPT=$((CTEST_ATTEMPT + 1))
        CTEST_OUTPUT=$(ctest --test-dir build --output-on-failure -j 4 2>&1) && CTEST_EXIT=0 || CTEST_EXIT=$?
        if [ "$CTEST_EXIT" -eq 0 ]; then
            CTEST_PASS=1
            break
        fi
        # exit 139 = SIGSEGV; exit 134 = SIGABRT (ASAN/UBSAN 偶发);
        # exit 8 = ctest 偶发 SIGTRAP (并发 test 在 -j 高负载下竞态, 单跑 5/5 PASS, 非真 fail)
        if [ "$CTEST_EXIT" -eq 139 ] || [ "$CTEST_EXIT" -eq 134 ] || [ "$CTEST_EXIT" -eq 8 ]; then
            echo "[pre-push] 2/5 ctest attempt $CTEST_ATTEMPT/$CTEST_MAX_RETRY — SEGFAULT/SIGABRT/SIGTRAP (exit $CTEST_EXIT), retry..."
            continue
        fi
        # 非 SEGFAULT fail — 不 retry, 直接 reject
        echo "$CTEST_OUTPUT" | tail -10
        echo "[pre-push] FAIL: ctest 失败 (exit $CTEST_EXIT) — 修 test fail 后再 push"
        exit 1
    done
    if [ "$CTEST_PASS" -eq 0 ]; then
        echo "$CTEST_OUTPUT" | tail -10
        echo "[pre-push] FAIL: ctest 经 $CTEST_MAX_RETRY 次 retry 仍 SEGFAULT — 检查 ASAN/UBSAN 报告"
        exit 1
    fi
    echo "[pre-push] 2/5 ctest OK"
else
    echo "[pre-push] WARN: build/tests 不存在, 跳过 ctest"
fi

# ---------------------------------------------------------------------------
# 3. clang-format dry-run (本地版本, 18+ compat)
# ---------------------------------------------------------------------------
CF_BIN=""
for candidate in \
    "/opt/homebrew/opt/llvm/bin/clang-format" \
    "$(which clang-format 2>/dev/null || true)" \
    "/usr/local/bin/clang-format" \
    "/usr/bin/clang-format"; do
    if [ -x "$candidate" ]; then
        CF_BIN="$candidate"
        break
    fi
done

if [ -n "$CF_BIN" ]; then
    echo "[pre-push] 3/5 clang-format dry-run ($("$CF_BIN" --version 2>&1 | head -1))..."
    # 只检查本次 push 改动的文件 (相对 HEAD 或全量 if 无 origin/main)
    CHANGED_FILES=""
    if git rev-parse --verify origin/main >/dev/null 2>&1; then
        CHANGED_FILES=$(git diff --name-only "$(git merge-base HEAD origin/main)" HEAD \
            2>/dev/null | grep -E '\.(hpp|cpp)$' || true)
    else
        CHANGED_FILES=$(git diff --name-only HEAD~1 HEAD 2>/dev/null \
            | grep -E '\.(hpp|cpp)$' || true)
    fi

    if [ -n "$CHANGED_FILES" ]; then
        FORMAT_FAIL=0
        while IFS= read -r f; do
            if [ -f "$f" ] && ! "$CF_BIN" --dry-run --Werror "$f" 2>/dev/null; then
                echo "[pre-push]   FORMAT FAIL: $f"
                FORMAT_FAIL=1
            fi
        done <<< "$CHANGED_FILES"
        if [ "$FORMAT_FAIL" -eq 1 ]; then
            echo "[pre-push] FAIL: clang-format 未通过 — 跑 'clang-format -i <file>' 修格式后再 push"
            exit 1
        fi
    else
        echo "[pre-push] 3/5 clang-format: 无 .cpp/.hpp 改动, skip"
    fi
    echo "[pre-push] 3/5 clang-format OK"
else
    echo "[pre-push] WARN: clang-format 未找到, 跳过格式检查"
    echo "  提示: brew install llvm (或 apt install clang-format-19)"
fi

# ---------------------------------------------------------------------------
# 4. Python grep checks (5 项红线 — 与远端 CI 一致)
#    PR_BODY 走本次 push 范围派生 (见顶部 derive_pr_body), 非硬编码 N/A.
# ---------------------------------------------------------------------------
echo "[pre-push] 4/5 Python grep checks (5 项)..."

PYTHON_BIN=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1; then
        PYTHON_BIN="$candidate"
        break
    fi
done

if [ -z "$PYTHON_BIN" ]; then
    echo "[pre-push] WARN: python3 未找到, 跳过 grep checks"
else
    GREP_FAIL=0

    # 4a. ABI lock (ADR-027 Enforce-3) — PR_BODY 从 push 范围 commit message 派生
    if [ -f "tests/ci_grep/abi_lock.py" ]; then
        if ! PR_BODY="$PR_BODY_DERIVED" "$PYTHON_BIN" tests/ci_grep/abi_lock.py 2>&1 | tail -3; then
            echo "[pre-push]   FAIL: abi_lock.py"
            GREP_FAIL=1
        fi
    fi

    # 4b. Risk enum coverage (22/22)
    if [ -f "tests/ci_grep/risk_enum_coverage.py" ]; then
        if ! "$PYTHON_BIN" tests/ci_grep/risk_enum_coverage.py 2>&1 | tail -3; then
            echo "[pre-push]   FAIL: risk_enum_coverage.py"
            GREP_FAIL=1
        fi
    fi

    # 4c. ADR-010 -Wno-* compliance
    if [ -f "tests/ci_grep/adr010_wno_check.py" ]; then
        if ! "$PYTHON_BIN" tests/ci_grep/adr010_wno_check.py 2>&1 | tail -3; then
            echo "[pre-push]   FAIL: adr010_wno_check.py"
            GREP_FAIL=1
        fi
    fi

    # 4d. ADR-027 SSOT cite — PR_BODY 从 push 范围 commit message 派生
    if [ -f "tests/ci_grep/core_data_structure_ssot_check.py" ]; then
        if ! PR_BODY="$PR_BODY_DERIVED" "$PYTHON_BIN" tests/ci_grep/core_data_structure_ssot_check.py 2>&1 | tail -3; then
            echo "[pre-push]   FAIL: core_data_structure_ssot_check.py"
            GREP_FAIL=1
        fi
    fi

    # 4e. ADR-024 worktree+pwd verify
    if [ -f "tests/ci_grep/worktree_commit_check.py" ]; then
        if ! "$PYTHON_BIN" tests/ci_grep/worktree_commit_check.py 2>&1 | tail -3; then
            echo "[pre-push]   FAIL: worktree_commit_check.py"
            GREP_FAIL=1
        fi
    fi

    if [ "$GREP_FAIL" -eq 1 ]; then
        echo "[pre-push] FAIL: grep 检查失败 — 修 grep fail 后再 push"
        exit 1
    fi
    echo "[pre-push] 4/5 grep checks OK"
fi

# ---------------------------------------------------------------------------
# 5. 禁止 -Wno-unused-result 全库 (W81 反例)
# ---------------------------------------------------------------------------
echo "[pre-push] 5/5 反模式检查 (no -Wno-unused-result 全库)..."
if grep -rn "\-Wno-unused-result" CMakeLists.txt src/ include/ 2>/dev/null | grep -v "三方库豁免\|ADR-010\|PRIVATE"; then
    echo "[pre-push] FAIL: 发现 -Wno-unused-result 全库 (W81 反例 907ef7a 不可重复)"
    echo "  正确做法: 在调用点 (void)::read() 或用 ADR-010 §3.2 三方库豁免 PRIVATE"
    exit 1
fi
echo "[pre-push] 5/5 反模式检查 OK"

echo "[pre-push] 全部检查通过 — push 继续"
echo "[pre-push] ADR-032 提醒: push 后立刻 gh pr create, 不等 CI"
