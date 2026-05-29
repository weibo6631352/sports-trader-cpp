#!/usr/bin/env bash
# .git/hooks/pre-push — ADR-032 §3 策略 1: 本地 pre-push hook
#
# 老高 Wave 89 实施 (ADR-032 §3 策略 1 落地)
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
# 使用:
#   自动: git push 时触发
#   跳过 (紧急): git push --no-verify (须有 P0 理由 + incident)
#
# 维护人: 老高

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

echo "[pre-push] ADR-032 §3 策略 1 本地检查开始..."

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
# 2. CTest (已有 build/ 才跑)
# ---------------------------------------------------------------------------
if [ -d build ] && [ -d build/tests ]; then
    echo "[pre-push] 2/5 ctest..."
    if ! ctest --test-dir build --output-on-failure -j 4 2>&1 | tail -10; then
        echo "[pre-push] FAIL: ctest 失败 — 修 test fail 后再 push"
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

    # 4a. ABI lock (ADR-027 Enforce-3)
    if [ -f "tests/ci_grep/abi_lock.py" ]; then
        if ! PR_BODY="local-pre-push N/A" "$PYTHON_BIN" tests/ci_grep/abi_lock.py 2>&1 | tail -3; then
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

    # 4d. ADR-027 SSOT cite
    if [ -f "tests/ci_grep/core_data_structure_ssot_check.py" ]; then
        if ! PR_BODY="local-pre-push N/A" "$PYTHON_BIN" tests/ci_grep/core_data_structure_ssot_check.py 2>&1 | tail -3; then
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
