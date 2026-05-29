#!/usr/bin/env python3
"""
doc_governance_check.py — ADR-028 §5 文档治理 CI 检查
spec-owner: 小米 #40 (doc-curator)
impl-owner: 老高 #16 (code-quality-reviewer)
wave: W9 W4

用途:
  检查 docs/ 下所有 .md 文件的 frontmatter 合规性.
  输出: stdout 人类可读报告 + exit code (0=pass, 1=有违规).
  CI 模式: PR 时自动触发, 每周自动跑.

规则:
  C1: 每 md 必含 frontmatter (owner, last_review, status)
  C2: status 必须是五枚举之一 (SSOT/Draft/Archive/Outdated/Retracted)
  C3: SSOT 文档 last_review 不超 3 月 → 黄色警告; 超 6 月 → 红色错误
  C4: Retracted doc 必含 ## RETRACTED 区段
  C5: 新版替代旧版 — 旧版 status 自动降 Archive (当前: 报告 Outdated/SSOT 并存冲突)

运行方式:
  python3 tests/ci_grep/doc_governance_check.py [--docs-root docs] [--strict]

  --strict: C3 黄色警告也计入 exit code 1 (默认只有红色才 fail)
  --docs-root: docs 目录路径 (默认 docs/, 相对于脚本所在 repo root)

ADR 引用: ADR-028 §5, 2026-06-W4
"""

import argparse
import os
import re
import sys
from datetime import date, datetime
from pathlib import Path

# ---------------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------------

VALID_STATUSES = {"SSOT", "Draft", "Archive", "Outdated", "Retracted"}

# SSOT 文档: 超过此天数未 review → 黄色警告
SSOT_WARN_DAYS = 90   # 3 个月

# SSOT 文档: 超过此天数未 review → 红色错误
SSOT_ERROR_DAYS = 180  # 6 个月

# frontmatter 字段检测正则 (宽松匹配, 兼容 markdown 列表风格 "- **owner:** xxx" 和 YAML 风格 "owner: xxx")
_RE_OWNER = re.compile(
    r'(?:^|\n)\s*[-*]?\s*\*{0,2}owner\*{0,2}\s*:',
    re.IGNORECASE,
)
_RE_LAST_REVIEW = re.compile(
    r'(?:^|\n)\s*[-*]?\s*\*{0,2}last[_\s]review\*{0,2}\s*:',
    re.IGNORECASE,
)
_RE_STATUS = re.compile(
    r'(?:^|\n)\s*[-*]?\s*\*{0,2}status\*{0,2}\s*:',
    re.IGNORECASE,
)

# 提取 status 值 (取冒号后第一个单词)
_RE_STATUS_VALUE = re.compile(
    r'(?:^|\n)\s*[-*]?\s*\*{0,2}status\*{0,2}\s*:\s*(\S+)',
    re.IGNORECASE,
)

# 提取 last_review 值 (ISO date YYYY-MM-DD)
_RE_LAST_REVIEW_VALUE = re.compile(
    r'(?:^|\n)\s*[-*]?\s*\*{0,2}last[_\s]review\*{0,2}\s*:\s*([\d]{4}-[\d]{2}-[\d]{2})',
    re.IGNORECASE,
)

# C4: Retracted 文档须含此区段
_RE_RETRACTED_SECTION = re.compile(
    r'^##\s+RETRACTED',
    re.MULTILINE | re.IGNORECASE,
)


# ---------------------------------------------------------------------------
# 数据类
# ---------------------------------------------------------------------------

class Violation:
    """单条违规记录."""

    LEVELS = {"RED": 2, "YELLOW": 1, "INFO": 0}

    def __init__(self, path: str, rule: str, level: str, message: str):
        self.path = path
        self.rule = rule      # C1~C5
        self.level = level    # RED / YELLOW / INFO
        self.message = message

    def __str__(self) -> str:
        return f"[{self.level}][{self.rule}] {self.path}: {self.message}"


# ---------------------------------------------------------------------------
# 检查函数
# ---------------------------------------------------------------------------

def check_file(fpath: Path, today: date) -> list[Violation]:
    """对单个文件执行 C1-C5 检查, 返回 Violation 列表."""
    violations: list[Violation] = []

    try:
        content = fpath.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        violations.append(Violation(str(fpath), "C1", "RED", f"无法读取文件: {e}"))
        return violations

    path_str = str(fpath)

    # --- C1: 必含 owner / last_review / status ---
    missing_fields = []
    if not _RE_OWNER.search(content):
        missing_fields.append("owner")
    if not _RE_LAST_REVIEW.search(content):
        missing_fields.append("last_review")
    if not _RE_STATUS.search(content):
        missing_fields.append("status")

    if missing_fields:
        violations.append(Violation(
            path_str, "C1", "RED",
            f"缺少 frontmatter 字段: {', '.join(missing_fields)}",
        ))
        # C1 不通过则 C2~C5 跳过 (无法解析)
        return violations

    # --- C2: status 枚举合法 ---
    status_match = _RE_STATUS_VALUE.search(content)
    status_raw = ""
    if status_match:
        # 取枚举词: 可能带括号或中文注释, 取纯 ASCII 部分
        raw = status_match.group(1).strip()
        # 去掉末尾括号及其内容
        status_raw = re.sub(r'\s*\(.*', '', raw).strip()

    if status_raw not in VALID_STATUSES:
        violations.append(Violation(
            path_str, "C2", "RED",
            f"status 值 '{status_raw}' 不合法, 必须是 {sorted(VALID_STATUSES)} 之一",
        ))

    # --- C3: SSOT last_review 时效检查 ---
    if status_raw == "SSOT":
        review_match = _RE_LAST_REVIEW_VALUE.search(content)
        if review_match:
            review_date_str = review_match.group(1)
            try:
                review_date = datetime.strptime(review_date_str, "%Y-%m-%d").date()
                delta_days = (today - review_date).days
                if delta_days > SSOT_ERROR_DAYS:
                    violations.append(Violation(
                        path_str, "C3", "RED",
                        f"SSOT last_review 已过 {delta_days} 天 (超 {SSOT_ERROR_DAYS} 天红线)",
                    ))
                elif delta_days > SSOT_WARN_DAYS:
                    violations.append(Violation(
                        path_str, "C3", "YELLOW",
                        f"SSOT last_review 已过 {delta_days} 天 (超 {SSOT_WARN_DAYS} 天黄线)",
                    ))
            except ValueError:
                violations.append(Violation(
                    path_str, "C3", "YELLOW",
                    f"last_review 日期格式无法解析: '{review_date_str}', 期望 YYYY-MM-DD",
                ))
        else:
            # C1 通过说明字段存在, 但值无法提取为日期
            violations.append(Violation(
                path_str, "C3", "YELLOW",
                "SSOT 文档 last_review 字段存在但无法提取 ISO 日期",
            ))

    # --- C4: Retracted 文档必含 ## RETRACTED 区段 ---
    if status_raw == "Retracted":
        if not _RE_RETRACTED_SECTION.search(content):
            violations.append(Violation(
                path_str, "C4", "RED",
                "status=Retracted 但文档缺少 '## RETRACTED' 区段",
            ))

    # --- C5: Outdated 文档提示 (信息级, 不 fail) ---
    # C5 逻辑: 如果 status=SSOT 但文件名含旧版本编号特征 (v0.x / v1 后有 v2 同 prefix),
    # 这需要跨文件关联分析, 超出单文件 check 范围.
    # 当前实现: 对 status=Outdated 的文档发 INFO 提示让 owner 确认是否已可删.
    if status_raw == "Outdated":
        violations.append(Violation(
            path_str, "C5", "INFO",
            "status=Outdated — 请 owner 确认新 SSOT 已落 main, 并在 W9 W3 前执行归档或删除",
        ))

    return violations


def collect_md_files(docs_root: Path) -> list[Path]:
    """递归收集 docs_root 下所有 .md 文件."""
    return sorted(docs_root.rglob("*.md"))


# ---------------------------------------------------------------------------
# 主函数
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="ADR-028 §5 文档治理 CI 检查 (spec: 小米, impl: 老高)",
    )
    parser.add_argument(
        "--docs-root",
        default="docs",
        help="docs/ 目录路径 (相对于 cwd 或绝对路径)",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="严格模式: YELLOW 警告也导致 exit code 1",
    )
    parser.add_argument(
        "--summary-only",
        action="store_true",
        help="只输出汇总统计, 不列每条违规",
    )
    args = parser.parse_args()

    docs_root = Path(args.docs_root)
    if not docs_root.exists():
        print(f"ERROR: docs-root 不存在: {docs_root}", file=sys.stderr)
        return 1

    today = date.today()
    all_files = collect_md_files(docs_root)
    total = len(all_files)

    all_violations: list[Violation] = []
    files_with_violations: set[str] = set()
    files_clean = 0

    for fpath in all_files:
        viols = check_file(fpath, today)
        if viols:
            all_violations.extend(viols)
            files_with_violations.add(str(fpath))
        else:
            files_clean += 1

    # 分级
    red_viols   = [v for v in all_violations if v.level == "RED"]
    yellow_viols = [v for v in all_violations if v.level == "YELLOW"]
    info_viols  = [v for v in all_violations if v.level == "INFO"]

    coverage_pct = files_clean / total * 100 if total > 0 else 0.0

    # 输出
    print("=" * 72)
    print("doc_governance_check.py — ADR-028 §5 文档治理检查")
    print(f"run date   : {today}")
    print(f"docs root  : {docs_root.resolve()}")
    print(f"total md   : {total}")
    print(f"clean files: {files_clean} ({coverage_pct:.1f}%)")
    print(f"violations : RED={len(red_viols)} YELLOW={len(yellow_viols)} INFO={len(info_viols)}")
    print("=" * 72)

    if not args.summary_only:
        # RED 先输出
        if red_viols:
            print("\n--- RED (必须修复) ---")
            for v in red_viols:
                print(f"  {v}")

        if yellow_viols:
            print("\n--- YELLOW (需关注) ---")
            for v in yellow_viols:
                print(f"  {v}")

        if info_viols:
            print("\n--- INFO (知晓) ---")
            for v in info_viols:
                print(f"  {v}")

    print()
    if not red_viols and not yellow_viols:
        print("PASS: 无违规.")
    elif red_viols:
        print(f"FAIL: {len(red_viols)} 条红色违规需要修复.")
    else:
        print(f"WARN: 无红色违规, 但有 {len(yellow_viols)} 条黄色警告.")

    # 规则汇总
    print()
    print("规则覆盖:")
    for rule, desc in [
        ("C1", "frontmatter 必含 owner/last_review/status"),
        ("C2", "status 五枚举合法性"),
        ("C3", "SSOT last_review 时效 (3月黄/6月红)"),
        ("C4", "Retracted 文档必含 ## RETRACTED 区段"),
        ("C5", "Outdated 文档 owner 确认提示"),
    ]:
        count_red = sum(1 for v in red_viols if v.rule == rule)
        count_yel = sum(1 for v in yellow_viols if v.rule == rule)
        count_inf = sum(1 for v in info_viols if v.rule == rule)
        print(f"  {rule}: {desc} — RED={count_red} YELLOW={count_yel} INFO={count_inf}")

    # exit code
    if red_viols:
        return 1
    if args.strict and yellow_viols:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
