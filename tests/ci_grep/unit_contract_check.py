#!/usr/bin/env python3
# tests/ci_grep/unit_contract_check.py — P0-2 单位契约防复发 grep 守护
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团, tests/ci_grep/ 守护)  v1.0
# 关联: 老郭架构裁定 钳制-3 (P0-2 单位失配 c5 收尾)
#       P0-2 修复链: c1/c2 (cap 字段改 domain::MicroPUSD 强类型) +
#                    c3 (sizing 用 .to_pusd() / RM 直接 micro 比 / paper from_pusd 同源)
#
# 背景 (要防什么 bug 复发):
#   RiskConfig 的 cap 字段 (per_order_cap_usdc / market_exposure_cap_usdc /
#   per_outcome_cap_usdc) 曾被 sizing 当 whole pUSD、被 RM 当 micro，差 1e6
#   靠魔法 clamp (min(notional, 10.0)) 摁住。c2 把 cap 改成 MicroPUSD 强类型,
#   编译期挡住了「直接 static_cast<double>(cfg.*cap*)」主路径。本 grep 守护
#   编译器挡不住的残留逃生口 (raw .v 当 whole 用) + 做一道 build-independent tripwire。
#
# 为什么类型系统挡了主路径还要 grep:
#   - static_cast<double>(cfg.per_order_cap_usdc) 已编译不过 (MicroPUSD 无隐式 double).
#   - 但 cfg.per_order_cap_usdc.v 是 int64, 隐式可比/可乘, 编译器放行 → 这是真残留风险:
#     sizing 若读 .v (micro) 当 whole pUSD 比 notional, 单位失配静默复发 (差 1e6).
#   - 故 grep 真正的活: 在 sizing 里禁 cap 字段裸 .v (必须 .to_pusd()).
#
# ⚠️ 不误杀的合法 ×1e6 边界转换 (paper 侧一堆, 全合法, 不禁):
#   - paper_loop  notional_usdc * 1'000'000.0           (whole→size micro)
#   - paper_loop  cfg_.bankroll_usdc * 1'000'000.0      (bankroll whole→micro)
#   - paper_loop  book_depth_l1 * 1'000'000.0           (depth whole→micro)
#   - paper_loop  cfg_.per_order_cap_usdc * 1'000'000.0 (PaperLoopConfig cap 是 whole pUSD, c5 兜底)
#   - paper_loop  as_of_now / 1'000'000LL               (ns→ms, 根本不是金额)
#   - paper_daemon from_pusd(cfg_.paper_loop.*_cap_usdc)(唯一合法 cap→micro 转换白名单)
#   → 故 ×1e6 / from_pusd 仅在 risk_gateway / sizing_calculator 两文件里对 cap 才算违规;
#     paper_loop / paper_daemon 的边界转换全豁免。
#
# 规则 (仅扫 3 个 enforced 文件; 注释行 + // unit-contract-ok 行豁免):
#   F1 (FAIL): sizing_calculator.cpp 里 cap 字段裸 .v (cfg*.<cap>_usdc.v)
#              → sizing 读 cap 必须 .to_pusd(), 禁 raw .v (micro 当 whole 比 = P0-2 复发).
#   F2 (FAIL): 任一 enforced 文件里 static_cast<double>(...<cap>_usdc...)
#              → c2 后已编译不过; 保留为 tripwire, 拦「加 .v 进 cast 让它编过」的人.
#   F3 (FAIL): risk_gateway.cpp / sizing_calculator.cpp 里 cap 字段同表达式 × 1e6
#              → 这两文件永不该 scale cap (RM 直接 micro 比; sizing .to_pusd() 降量纲).
#                cap→micro 边界转换的唯一合法落点是 paper_daemon from_pusd.
#
# 豁免机制:
#   - 注释行 (strip 后以 // 或 * 或 # 起) 不计.
#   - 行尾/行内含 `unit-contract-ok` 标记 → 显式豁免 (照搬项目 三方库豁免 逃生口模式).
#     用于将来确有合法例外时手动放行, review 时老高把关.
#
# 契约 (与现有 ci_grep 一致):
#   - 全文件扫 (3 个固定 enforced 文件), 不依赖 git diff 范围.
#   - 无违规 exit 0 + PASS; 有违规 exit 1 + ::error:: 行 (GitHub Actions annotation).
#
# 用法:
#   python3 tests/ci_grep/unit_contract_check.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 常量                                                                          #
# --------------------------------------------------------------------------- #

# 受 P0-2 单位契约约束的 3 个文件 (老郭 钳制-3 指定)
ENFORCED_FILES = {
    "sizing": "src/stcpp/sizing/sizing_calculator.cpp",
    "rm": "src/stcpp/risk/risk_gateway.cpp",
    "paper": "src/stcpp/paper/paper_loop.cpp",
}
# paper_daemon: from_pusd 是唯一合法 cap→micro 白名单, 不在 enforced 禁令内, 仅供文档定位
PAPER_DAEMON = "src/stcpp/app/paper_daemon.cpp"

# RiskConfig 的 cap 字段名 (P0-2 直接受害者)
CAP_FIELDS = (
    "per_order_cap_usdc",
    "market_exposure_cap_usdc",
    "per_outcome_cap_usdc",
)
_CAP_ALT = "|".join(CAP_FIELDS)

# 显式豁免标记 (照搬 三方库豁免 逃生口)
_EXEMPT_MARK = "unit-contract-ok"

# F1: sizing 里 cap 字段裸 .v  (e.g. cfg.per_order_cap_usdc.v / cfg_.per_outcome_cap_usdc .v)
_F1_CAP_RAW_V = re.compile(rf"\b(?:{_CAP_ALT})\s*\.\s*v\b")

# F2: static_cast<double>( ... <cap>_usdc ... )  跨任一 enforced 文件
_F2_CAST_CAP = re.compile(rf"static_cast\s*<\s*double\s*>\s*\([^)]*(?:{_CAP_ALT})")

# F3: cap 字段同表达式出现 × 1e6 字面量 (1'000'000 或 1000000), 任一侧
#   匹配 "<cap> ... * 1'000'000" 或 "1'000'000 ... * <cap>" 同行
_ONE_E6 = r"1'?000'?000(?:\.0+)?L?L?"
_F3_CAP_SCALE_A = re.compile(rf"\b(?:{_CAP_ALT})\b[^;]*\*\s*{_ONE_E6}")
_F3_CAP_SCALE_B = re.compile(rf"{_ONE_E6}\s*\*[^;]*\b(?:{_CAP_ALT})\b")

# F4 (A1 老郭钳-2): ledger/matcher 里 fill_size_usdc 同表达式 × 1e6 字面量.
#   A1 后 fill_size_usdc 是裸 int64 micro (无类型抓手), double→micro 唯一走 to_micro_pusd();
#   任何 fill_size 旁的裸 ×1e6/llround(x*1e6) = 绕开 helper = 单位债复发信号. 合法 micro→pUSD
#   换算 (pnl_fee / ml training_label) 走 // unit-contract-ok 显式豁免.
F4_FILES = {
    "matcher": "src/stcpp/execution/virtual_matcher.cpp",
    "wal_ledger": "src/stcpp/infra/wal/position_ledger.cpp",
    "risk_ledger": "src/stcpp/risk/position_ledger.cpp",
}
_F4_FILLSIZE_SCALE = re.compile(rf"\bfill_size_usdc\b[^;]*\*\s*{_ONE_E6}")
_F4_FILLSIZE_SCALE_B = re.compile(rf"{_ONE_E6}\s*\*[^;]*\bfill_size_usdc\b")

# F5 (P1-9 老韩 spec §1.4): risk_gateway.cpp 里 order_size_usdc 裸 (double)size_pUSD_micro 当 whole 喂.
#   size_pUSD_micro 是 micro(1e-6); SlippageModel.order_size_usdc 是 whole pUSD (ρ=order/depth 需同量纲).
#   裸 cast = micro 当 whole, 差 1e6 → liquidity gate 全量误拒 EXCEED_BOOK_DEPTH (P1-9 复发).
#   正确: domain::MicroPUSD::from_micro(...size_pUSD_micro).to_pusd() (含 .to_pusd() → 放行).
_F5_SIZE_RAW_DOUBLE = re.compile(
    r"order_size_usdc\s*=\s*(?:static_cast<double>|\(double\))\s*\(\s*[\w.\->]*size_pUSD_micro"
)

# F6 (A5 老韩 spec §4): paper_loop.cpp 里 set_daily_pnl(...) 喂入必须是 micro pUSD (×1e6).
#   daily_pnl 在 whole pUSD 域算 (MtM − fee), set_daily_pnl 收 signed micro → 必 ×1e6。
#   漏 ×1e6 → 喂入小 1e6 → DD 阈值不咬 (P0-2/P1-9 同型单位 bug 复发, 风控静默失效)。
#   守护: set_daily_pnl( 调用行必须含 1'000'000 (×1e6) 或 unit-contract-ok 标记, 否则 FAIL。
_F6_SET_DAILY_PNL = re.compile(r"set_daily_pnl\s*\(")
_F6_HAS_SCALE = re.compile(r"1'?000'?000|1e6|unit-contract-ok")

_COMMENT_PREFIXES = ("//", "*", "/*", "#")


# --------------------------------------------------------------------------- #
# 辅助                                                                          #
# --------------------------------------------------------------------------- #


def _is_comment_or_exempt(line: str) -> bool:
    stripped = line.strip()
    if not stripped:
        return True
    if stripped.startswith(_COMMENT_PREFIXES):
        return True
    if _EXEMPT_MARK in line:
        return True
    return False


def _scan_lines(path: Path) -> list[tuple[int, str]]:
    try:
        return list(enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1))
    except OSError:
        return []


# --------------------------------------------------------------------------- #
# main                                                                          #
# --------------------------------------------------------------------------- #


def main() -> int:
    ap = argparse.ArgumentParser(description="P0-2 单位契约防复发 grep 守护")
    ap.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    errors: list[str] = []
    missing: list[str] = []

    sizing_path = repo_root / ENFORCED_FILES["sizing"]
    rm_path = repo_root / ENFORCED_FILES["rm"]
    paper_path = repo_root / ENFORCED_FILES["paper"]

    for label, rel in ENFORCED_FILES.items():
        if not (repo_root / rel).is_file():
            missing.append(rel)
    scanned = len(ENFORCED_FILES) - len(missing)

    # --- F1: sizing 里 cap 字段裸 .v ---
    if sizing_path.is_file():
        for lineno, line in _scan_lines(sizing_path):
            if _is_comment_or_exempt(line):
                continue
            if _F1_CAP_RAW_V.search(line):
                errors.append(
                    f"F1 (FAIL): {ENFORCED_FILES['sizing']}:{lineno} cap 字段裸 .v\n"
                    f"  行内容: {line.strip()}\n"
                    f"  sizing 读 cap 必须 .to_pusd() (micro→whole 同量纲), 禁 raw .v.\n"
                    f"  raw .v 是 micro int64, 当 whole pUSD 比 notional = P0-2 单位失配复发 (差 1e6).\n"
                    f"  确有合法例外: 行尾加 // {_EXEMPT_MARK}: <理由> (老高 review)."
                )

    # --- F2: static_cast<double>(...<cap>...) 跨 3 文件 ---
    for rel, path in (
        (ENFORCED_FILES["sizing"], sizing_path),
        (ENFORCED_FILES["rm"], rm_path),
        (ENFORCED_FILES["paper"], paper_path),
    ):
        if not path.is_file():
            continue
        for lineno, line in _scan_lines(path):
            if _is_comment_or_exempt(line):
                continue
            if _F2_CAST_CAP.search(line):
                errors.append(
                    f"F2 (FAIL): {rel}:{lineno} static_cast<double>(...cap...)\n"
                    f"  行内容: {line.strip()}\n"
                    f"  c2 后 cap 是 MicroPUSD, 该 cast 本应编译不过; 出现 = 有人加 .v 绕过类型门.\n"
                    f"  cap→whole 用 .to_pusd(); cap→micro 边界转换走 paper_daemon from_pusd."
                )

    # --- F3: risk_gateway / sizing 里 cap × 1e6 ---
    for rel, path in (
        (ENFORCED_FILES["rm"], rm_path),
        (ENFORCED_FILES["sizing"], sizing_path),
    ):
        if not path.is_file():
            continue
        for lineno, line in _scan_lines(path):
            if _is_comment_or_exempt(line):
                continue
            if _F3_CAP_SCALE_A.search(line) or _F3_CAP_SCALE_B.search(line):
                errors.append(
                    f"F3 (FAIL): {rel}:{lineno} cap 字段同表达式 × 1e6\n"
                    f"  行内容: {line.strip()}\n"
                    f"  RM/sizing 永不该 scale cap: RM 直接 micro 比 size; sizing .to_pusd() 降量纲.\n"
                    f"  cap→micro 唯一合法落点 = {PAPER_DAEMON} from_pusd (whole pUSD 配置入口)."
                )

    # --- F4 (A1): matcher/ledger 里 fill_size_usdc × 1e6 (绕开 to_micro_pusd) ---
    for rel in F4_FILES.values():
        path = repo_root / rel
        if not path.is_file():
            continue
        for lineno, line in _scan_lines(path):
            if _is_comment_or_exempt(line):
                continue
            if _F4_FILLSIZE_SCALE.search(line) or _F4_FILLSIZE_SCALE_B.search(line):
                errors.append(
                    f"F4 (FAIL): {rel}:{lineno} fill_size_usdc 同表达式 × 1e6 (绕开 to_micro_pusd)\n"
                    f"  行内容: {line.strip()}\n"
                    f"  A1: fill_size 是裸 int64 micro; double→micro 唯一走 domain::to_micro_pusd().\n"
                    f"  合法 micro→pUSD 换算 (pnl_fee/training_label): 行尾加 // {_EXEMPT_MARK}: micro→pUSD."
                )

    # --- F5 (P1-9): risk_gateway.cpp 里 order_size_usdc 裸 (double)size_pUSD_micro 当 whole 喂 ---
    for lineno, line in _scan_lines(rm_path):
        if _is_comment_or_exempt(line):
            continue
        # 同行含 to_pusd = 正确写法 (from_micro(...).to_pusd()), 放行
        if "to_pusd" in line:
            continue
        if _F5_SIZE_RAW_DOUBLE.search(line):
            errors.append(
                f"F5 (FAIL): {ENFORCED_FILES['rm']}:{lineno} order_size_usdc 裸 (double)size_pUSD_micro\n"
                f"  行内容: {line.strip()}\n"
                f"  size_pUSD_micro 是 micro(1e-6); SlippageModel.order_size_usdc 是 whole pUSD.\n"
                f"  必须 domain::MicroPUSD::from_micro(...).to_pusd() (micro→whole 唯一通道).\n"
                f"  裸 cast → ρ=order/depth 差 1e6 → liquidity gate 全量误拒 (P1-9 复发)."
            )

    # --- F6 (A5): paper_loop.cpp set_daily_pnl 喂入必须 ×1e6 (whole pUSD → micro) ---
    for lineno, line in _scan_lines(paper_path):
        if _is_comment_or_exempt(line):
            continue
        if _F6_SET_DAILY_PNL.search(line) and not _F6_HAS_SCALE.search(line):
            errors.append(
                f"F6 (FAIL): {ENFORCED_FILES['paper']}:{lineno} set_daily_pnl 喂入疑似漏 ×1e6\n"
                f"  行内容: {line.strip()}\n"
                f"  daily_pnl 在 whole pUSD 域算 (MtM−fee); set_daily_pnl 收 signed micro → 必 ×1e6.\n"
                f"  漏乘 → 喂入小 1e6 → DD 阈值不咬 (风控静默失效, P0-2/P1-9 同型). 行内加 × 1'000'000."
            )

    # 输出
    if missing:
        for m in missing:
            print(
                f"[unit_contract_check] WARN: enforced 文件缺失 {m} "
                f"(重命名/移动? 请同步本脚本 ENFORCED_FILES)",
                file=sys.stderr,
            )

    if not errors:
        print(
            f"[unit_contract_check] PASS: 扫描 {scanned} enforced 文件 "
            f"(F1 sizing.v / F2 cast-cap / F3 cap×1e6 / F4 fill_size×1e6 / F5 order_size 裸micro / F6 daily_pnl×1e6), 无 P0-2 单位失配反模式. "
            f"paper_loop/paper_daemon 合法 ×1e6/from_pusd 边界转换已豁免."
        )
        return 0

    print(
        f"[unit_contract_check] FAIL: {len(errors)} 处 P0-2 单位契约违规", file=sys.stderr
    )
    for e in errors:
        print(f"::error::[unit_contract_check] {e}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
