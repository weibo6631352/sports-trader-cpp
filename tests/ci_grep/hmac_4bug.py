#!/usr/bin/env python3
# tests/ci_grep/hmac_4bug.py — HMAC 4 bug 反模式闸
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.2 W6 Wave 28
# 关联: docs/RESEARCH/laoli-polymarket-backend-requirements-v1.md §5 HMAC 4 bug
#       docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md §A R1-R4
#       docs/RESEARCH/laogao-pr-review-v1.2.md §2.2
#
# 4 bug 反模式:
#   Bug 1: HMAC base string 拼 querystring (request_path + "?" + query)
#          → 期望: request_path = path only (不拼 query)
#   Bug 2: 参数字段名 param_type / paramType / param-type (应是 asset_type)
#   Bug 3: sigType = 1 (正确), 严禁 sigType = 2 / signatureType = 2
#          (我们用 Magic 1-of-1 Safe, 不用 Polymarket proxy 形态)
#   Bug 4: base64 padding rstrip(b"=") 或 replace("=", "") 去除 = 号
#
# 扫描范围: src/ + include/ (全部 C++ 文件 + Python 文件)
# 豁免:
#   - tests/** (单测 mock fixture 合理豁免)
#   - 注释行
#   - 含 // CI-EXEMPT: <理由> 的行 (需老郭 24h 仲裁)
#   - 含 HMAC_BASE_STRING_TEST 注释的行 (URL builder 单测合法拼接)
#
# 用法:
#   python3 tests/ci_grep/hmac_4bug.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 反模式正则 (4 条)                                                             #
# --------------------------------------------------------------------------- #

_BUG1_PATH_QUERY_RE = re.compile(
    r"""request_path\s*\+\s*["\']?\?["\']?\s*\+\s*\w+   # request_path + "?" + var
    |base_string.*\?.*query                               # base_string 含 ?query 拼接
    """,
    re.VERBOSE,
)

_BUG2_PARAM_TYPE_RE = re.compile(
    r"\bparam_type\b|\bparamType\b|\bparam-type\b"
)

# sigType=2 / signatureType=2 — 禁止. sigType=1 是正确的.
_BUG3_SIG_TYPE2_RE = re.compile(
    r"""sigType\s*[:=]\s*2\b         # sigType: 2  or  sigType = 2
    |\bsignatureType\s*[:=]\s*2\b    # signatureType = 2
    |"sigType"\s*:\s*2               # JSON "sigType": 2
    |"signatureType"\s*:\s*2         # JSON "signatureType": 2
    """,
    re.VERBOSE,
)

_BUG4_BASE64_STRIP_RE = re.compile(
    r"""urlsafe_b64encode\s*\(.*\)\s*\.rstrip\s*\(  # Python: urlsafe_b64encode(...).rstrip(
    |b64encode\s*\(.*\)\s*\.replace\s*\(\s*["\x27]=["\x27]  # b64encode(...).replace("=", ...)
    """,
    re.VERBOSE,
)

_PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    ("HMAC-bug1 request_path+querystring (v3 §A R1)", _BUG1_PATH_QUERY_RE),
    ("HMAC-bug2 param_type/paramType (应 asset_type, v3 §A R2)", _BUG2_PARAM_TYPE_RE),
    ("HMAC-bug3 sigType=2 (应 sigType=1 Magic 1-of-1, v3 §A R3)", _BUG3_SIG_TYPE2_RE),
    ("HMAC-bug4 base64 padding strip (严禁去 =, v3 §A R4)", _BUG4_BASE64_STRIP_RE),
]

_COMMENT_LINE_RE = re.compile(r"^\s*(?://|\*|#)")
# 去除行内 // 注释后的内容 (保留字符串内的 //, 简单处理: 按第一个 // 截断)
_INLINE_COMMENT_RE = re.compile(r"\s*//.*$")


def is_exempt(line: str) -> bool:
    if _COMMENT_LINE_RE.match(line):
        return True
    if "CI-EXEMPT:" in line:
        return True
    if "HMAC_BASE_STRING_TEST" in line:
        return True
    return False


def strip_inline_comment(line: str) -> str:
    """Remove C++ inline comment (// ...) from a line, keeping code part only."""
    return _INLINE_COMMENT_RE.sub("", line)


CPP_EXTS = {".cpp", ".cc", ".cxx", ".hpp", ".h"}
PY_EXTS = {".py"}


def scan_file(path: Path) -> list[tuple[int, str, str]]:
    if path.suffix not in (CPP_EXTS | PY_EXTS):
        return []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []

    violations: list[tuple[int, str, str]] = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if is_exempt(line):
            continue
        # 对 C++ 文件剥离行内 // 注释再匹配, 避免注释中说明 "sigType=2 is a bug" 触发误报
        scan_line = strip_inline_comment(line) if path.suffix in CPP_EXTS else line
        for label, pat in _PATTERNS:
            if pat.search(scan_line):
                violations.append((lineno, label, line.rstrip()))
    return violations


def main() -> int:
    ap = argparse.ArgumentParser(description="HMAC 4 bug 反模式扫描")
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    scan_roots = [repo_root / "src", repo_root / "include"]

    all_violations: list[tuple[Path, int, str, str]] = []
    for root in scan_roots:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            # skip tests/ under src if any
            rel = str(path.relative_to(repo_root)).replace("\\", "/")
            if rel.startswith("tests/"):
                continue
            hits = scan_file(path)
            for lineno, label, line in hits:
                all_violations.append((path, lineno, label, line))

    if not all_violations:
        print("[hmac_4bug] PASS: 0 HMAC bug violations found")
        return 0

    print(f"[hmac_4bug] FAIL: {len(all_violations)} violation(s):", file=sys.stderr)
    for path, lineno, label, line in all_violations:
        rel = path.relative_to(repo_root)
        print(f"  ::error file={rel},line={lineno}::{label}: {line}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引:\n"
        "  bug 1: HMAC base string 只用 request_path (不含 ?query), 见老李 v3 §A R1.\n"
        "  bug 2: 字段名改 asset_type (Polymarket 官方名), 不用 param_type / paramType.\n"
        "  bug 3: sigType 必须 = 1 (Magic 1-of-1 Safe). sigType=2 是 Polymarket proxy 形态,\n"
        "         我们不用. 见老李 v3 §A R3.\n"
        "  bug 4: base64 padding = 号必须保留, 不得 rstrip/replace 去掉.\n"
        "  合规例外: 加 // CI-EXEMPT: <理由> 后提单 @老郭 24h 仲裁.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
