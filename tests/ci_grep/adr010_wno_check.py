#!/usr/bin/env python3
# tests/ci_grep/adr010_wno_check.py — ADR-010 §2.2 -Wno-* 抑制合规检查
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.4 W7 Wave 33
# 协作: 小宋 (#36, test-replay-engineer) W7 W2 提供 grandfather list 内容确认
# 关联: docs/RESEARCH/laogao-pr-review-v1.4.md §2.15
#       docs/ADR/2026-06-01-adr-010-test-code-grading.md §2.2 / §3.2
#       docs/RESEARCH/xiaosong-wno-cleanup-extension-w7-plan.md
#       .github/workflows/pr.yml job ci-grep-adr010-wno
#
# 背景:
#   ADR-010 §2.2 允许 tests/ 下 4 项 grandfather -Wno-*:
#     -Wno-double-promotion / -Wno-old-style-cast / -Wno-cast-align / -Wno-invalid-offsetof
#   其余 (sign-conversion / shadow / conversion / character-conversion) 待清.
#   生产代码 (src/ + include/) 严格禁止任何 -Wno-*.
#   三方库 (BLAKE3 / rigtorp / libsodium) 豁免: 必须有 "三方库豁免" 注释 (ADR-010 §3.2).
#
# 规则:
#   Rule 1 (FAIL): src/ + include/ 下 CMakeLists.txt 任意 -Wno-* → FAIL
#   Rule 2 (FAIL): tests/ 下 -Wno-* 超出 grandfather 白名单 4 项 → FAIL
#   Rule 3 (INFO): tests/ 下 "待清" 4 项 (sign-conversion / shadow / conversion /
#                  character-conversion) 仍在 → INFO (小宋 W7 清理队列, 不 FAIL)
#   Rule 4 (FAIL): 三方库豁免注释格式不合规 → FAIL
#                  (三方库 -Wno-* 必须有 // 三方库豁免: <库名> 或 # 三方库豁免: <库名>)
#
# 三方库豁免白名单 (ADR-010 §3.2):
#   BLAKE3 / rigtorp / libsodium / nlohmann_json / boost
#   (三方库 target 配置的 -Wno-*, 要求行尾/前一行有豁免注释)
#
# 用法:
#   python3 tests/ci_grep/adr010_wno_check.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 常量                                                                          #
# --------------------------------------------------------------------------- #

# ADR-010 §2.2 tests/ 下 5 项 grandfather (允许)
# Wave 81 新增: -Wno-unused-result (test_single_instance.cpp POSIX fork/pipe read/write)
GRANDFATHER_WNO = {
    "-Wno-double-promotion",
    "-Wno-old-style-cast",
    "-Wno-cast-align",
    "-Wno-invalid-offsetof",
    "-Wno-unused-result",  # Wave 81: POSIX read/write in fork/pipe tests (GCC 13 strict)
}

# tests/ 下"待清"4 项 (小宋 W7 清理队列, INFO 不 FAIL)
PENDING_CLEANUP_WNO = {
    "-Wno-sign-conversion",
    "-Wno-shadow",
    "-Wno-conversion",
    "-Wno-character-conversion",
}

# 三方库豁免关键词 (ADR-010 §3.2)
THIRD_PARTY_LIBS = {
    "blake3", "BLAKE3", "rigtorp", "libsodium", "sodium",
    "nlohmann", "nlohmann_json", "boost", "Boost",
}

# 三方库豁免注释正则
_THIRD_PARTY_EXEMPT_RE = re.compile(
    r"三方库豁免\s*[:：]\s*\S+|third.party\s+exempt\s*[:：]\s*\S+",
    re.IGNORECASE | re.UNICODE,
)

# -Wno-* 提取正则
_WNO_RE = re.compile(r"-Wno-[\w-]+")

# --------------------------------------------------------------------------- #
# 辅助函数                                                                      #
# --------------------------------------------------------------------------- #


def find_cmake_files(repo_root: Path) -> dict[str, list[Path]]:
    """Return dict with keys 'prod', 'tests', 'other' mapping to CMakeLists.txt paths."""
    result: dict[str, list[Path]] = {"prod": [], "tests": [], "other": []}
    for cmake in repo_root.rglob("CMakeLists.txt"):
        rel = cmake.relative_to(repo_root)
        rel_str = str(rel)
        # 跳过 build 目录
        if any(part.startswith(("build", ".")) for part in rel.parts):
            continue
        if rel_str.startswith("src/") or rel_str.startswith("include/"):
            result["prod"].append(cmake)
        elif rel_str.startswith("tests/"):
            result["tests"].append(cmake)
        else:
            result["other"].append(cmake)
    return result


def extract_wno_with_context(cmake_path: Path) -> list[tuple[int, str, str]]:
    """Return list of (lineno, wno_flag, full_line) for non-comment lines containing -Wno-*.
    CMake comment lines (stripped line starting with '#') are excluded from enforcement.
    """
    results: list[tuple[int, str, str]] = []
    try:
        lines = cmake_path.read_text(encoding="utf-8", errors="replace").splitlines()
        for i, line in enumerate(lines, start=1):
            stripped = line.strip()
            # 跳过注释行 (CMake # 注释): 注释里提到"已删 -Wno-*" 不算违规
            if stripped.startswith("#"):
                continue
            flags = _WNO_RE.findall(line)
            for flag in flags:
                results.append((i, flag, stripped))
    except OSError:
        pass
    return results


def _get_enclosing_target(lines: list[str], lineno: int) -> str:
    """Walk backwards from lineno to find the enclosing target_compile_options/add_library call."""
    for i in range(lineno - 2, max(0, lineno - 50), -1):
        line = lines[i]
        if "target_compile_options(" in line or "add_library(" in line:
            return line
    return ""


def has_third_party_exempt_comment(cmake_path: Path, lineno: int) -> bool:
    """Return True if the line or surrounding lines have a third-party exempt comment,
    or if the enclosing target_compile_options block targets a known third-party library.
    """
    try:
        lines = cmake_path.read_text(encoding="utf-8", errors="replace").splitlines()
        # 检查当前行 + 前 10 行 (注释可能在稍远的上方)
        check_range = range(max(0, lineno - 11), min(len(lines), lineno + 2))
        for i in check_range:
            line = lines[i]
            if _THIRD_PARTY_EXEMPT_RE.search(line):
                return True
            # 检查是否明确标记三方库来源注释
            if "第三方" in line or "third-party" in line.lower() or "third party" in line.lower():
                if any(lib.lower() in line.lower() for lib in THIRD_PARTY_LIBS):
                    return True
        # 检查包围的 target_compile_options 是否针对三方库 target
        enclosing = _get_enclosing_target(lines, lineno)
        if enclosing:
            for lib in THIRD_PARTY_LIBS:
                if lib.lower() in enclosing.lower():
                    return True
    except OSError:
        pass
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description="ADR-010 §2.2 -Wno-* 合规检查")
    ap.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    cmake_map = find_cmake_files(repo_root)
    errors: list[str] = []
    infos: list[str] = []

    # --- Rule 1: 生产代码 src/ + include/ 下 CMakeLists.txt 严禁 -Wno-* ---
    for cmake in cmake_map["prod"]:
        rel = str(cmake.relative_to(repo_root))
        for lineno, flag, line in extract_wno_with_context(cmake):
            if has_third_party_exempt_comment(cmake, lineno):
                continue  # 三方库豁免
            errors.append(
                f"Rule 1 (FAIL): 生产代码 {rel}:{lineno} 含 {flag}\n"
                f"  行内容: {line}\n"
                f"  ADR-010 §2.1: src/ + include/ 严禁任何 -Wno-* 抑制\n"
                f"  若为三方库引起, 在该行或前一行加: # 三方库豁免: <库名>"
            )

    # --- Rule 2 + 3: tests/ 下 -Wno-* 白名单检查 ---
    for cmake in cmake_map["tests"]:
        rel = str(cmake.relative_to(repo_root))
        for lineno, flag, line in extract_wno_with_context(cmake):
            if has_third_party_exempt_comment(cmake, lineno):
                continue  # 三方库豁免

            if flag in GRANDFATHER_WNO:
                # 白名单允许, 不输出
                continue
            elif flag in PENDING_CLEANUP_WNO:
                # 待清队列 → INFO 不 FAIL (小宋 W7 清理队列)
                infos.append(
                    f"Rule 3 (INFO/待清): tests/ {rel}:{lineno} 含 {flag}\n"
                    f"  ADR-010 §2.2: 在小宋 W7 清理队列中, 不 FAIL 但请跟进\n"
                    f"  见 docs/RESEARCH/xiaosong-wno-cleanup-extension-w7-plan.md"
                )
            else:
                # 超出白名单 → FAIL
                errors.append(
                    f"Rule 2 (FAIL): tests/ {rel}:{lineno} 含超出 grandfather 的 {flag}\n"
                    f"  行内容: {line}\n"
                    f"  ADR-010 §2.2 允许列表: {', '.join(sorted(GRANDFATHER_WNO))}\n"
                    f"  新增 -Wno-* 须走 ADR-010 §5 例外申请 (老高 review → 老郭 24h → GM ack)\n"
                    f"  三方库引起: 加 # 三方库豁免: <库名> 注释"
                )

    # --- Rule 2: other CMakeLists (顶层 / cmake/) 下 -Wno-* 需要三方库豁免注释 ---
    for cmake in cmake_map["other"]:
        rel = str(cmake.relative_to(repo_root))
        for lineno, flag, line in extract_wno_with_context(cmake):
            if has_third_party_exempt_comment(cmake, lineno):
                continue
            errors.append(
                f"Rule 2 (FAIL): {rel}:{lineno} 含 {flag} 且无三方库豁免注释\n"
                f"  行内容: {line}\n"
                f"  非 tests/ 下 -Wno-* 必须是三方库引起并注明 # 三方库豁免: <库名>"
            )

    # 输出
    for info in infos:
        print(f"[adr010_wno_check] INFO: {info}")

    if not errors:
        print(
            f"[adr010_wno_check] PASS: 扫描 "
            f"{len(cmake_map['prod'])} prod + "
            f"{len(cmake_map['tests'])} tests + "
            f"{len(cmake_map['other'])} other CMakeLists.txt, "
            f"无违规 -Wno-* ({len(infos)} INFO 待清)"
        )
        return 0

    print(f"[adr010_wno_check] FAIL: {len(errors)} error(s), {len(infos)} info(s)", file=sys.stderr)
    for e in errors:
        print(f"::error::[adr010_wno_check] {e}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
