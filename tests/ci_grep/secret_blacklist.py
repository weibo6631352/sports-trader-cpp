#!/usr/bin/env python3
# tests/ci_grep/secret_blacklist.py — secret / 凭证 / TLS insecure 反模式闸 (5 条)
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.0
# 关联:
#   docs/RESEARCH/xiaobai-live-external-data-security-v1.md §5 (CI grep 守护建议)
#   CLAUDE.md §8 红线: 私钥明文落盘 / 出现在日志 → 系统权限暂停
#   src/stcpp/debug_api/state_provider.hpp 头注 (黑名单字段源头杜绝 / allowlist 原则)
#   docs/RESEARCH/xiaobai-observability-api-security-v1.md §1 (黑名单)
#
# 缺口背景 (小白审计 §5): pr.yml / pre-push 现有 17+ grep job, 无一条查 secret 字段进
#   观测/日志路径. 真接 Polymarket WSS (user channel 携 auth) + Goalserve (URL 携 key) +
#   boost.beast TLS transport 前, 这是 P1 合规缺口. 本脚本补 5 条:
#
#   G1 secret 字段名进 debug_api 观测层 struct (state_provider allowlist 原则)
#   G2 secret/凭证变量被拼进日志 / 错误串 (LOG/cout/cerr/fprintf/throw, P-09)
#   G3 credentialed payload / URL 整体进日志 (user subscribe frame 含 auth; Goalserve URL 含 key)
#   G4 TLS insecure 反模式 (verify_none / SSL_VERIFY_NONE / --insecure / verify=false, §4.1 红线)
#   G5 secret 明文值硬编码进源码 (.env 值不应进 git)
#
# 任一命中 → FAIL (exit 1) block.
#
# 误杀防护:
#   - 纯注释行跳过 (// 或 /* 或 * 开头)
#   - 白名单字段 condition_id / token_id / sequence_no (协议公开事实, 非 secret) 不触发
#   - secret 字段构造 frame (out.append(cfg_.api_secret)) 不是日志 → G2 只匹配日志/异常 sink
#   - getenv / std::getenv 读环境变量是合规凭证流转 → G5 只匹配 = 后跟字面 hex 值
#   - 本脚本自身 (tests/ci_grep/**) 含模式字符串 → 全部 grep 排除自身
#
# 豁免: 行尾加 // CI-EXEMPT: <理由> (需老郭 / 老韩 24h 仲裁)
#
# 用法:
#   python3 tests/ci_grep/secret_blacklist.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 扫描范围                                                                      #
# --------------------------------------------------------------------------- #

_SRC_SUFFIXES = {".cpp", ".cc", ".cxx", ".hpp", ".h"}

# 扫描根 (相对 repo_root). debug_api 单列给 G1.
_SCAN_DIRS = ("src", "include")
_DEBUG_API_REL = "src/stcpp/debug_api"


def _is_excluded(rel: str) -> bool:
    """本脚本所在目录 (含模式字符串) + 测试 mock 排除, 防自我误杀。"""
    # tests/ci_grep/** 自身含所有 secret 模式字符串
    if rel.startswith("tests/"):
        return True
    return False


# --------------------------------------------------------------------------- #
# 白名单 (协议公开字段, 非 secret) — 命中这些不算违规                            #
# --------------------------------------------------------------------------- #
# condition_id / token_id / sequence_no 是 Polymarket 协议公开事实 (链上 positionId /
# ERC-1155 / book 序号), 与 secret 无关. 仅当一行只命中白名单字段时豁免。
_WHITELIST_RE = re.compile(r"\b(condition_id|token_id|sequence_no)\b")

# --------------------------------------------------------------------------- #
# secret 字段名核心词 (G1 / G2 共用)                                            #
# --------------------------------------------------------------------------- #
# 注: 不含裸 "key" (太宽, 命中 token_key / map key 等). 用 api_key / .key 边界控制。
_SECRET_FIELD_RE = re.compile(
    r"\b(?:"
    r"api_secret|apiSecret|api_passphrase|passphrase|"
    r"private_key|privateKey|WALLET_PRIVATE_KEY|secret_key|"
    r"POLYMARKET_API_SECRET|POLYMARKET_API_KEY|POLYMARKET_API_PASSPHRASE|"
    r"GOALSERVE_API_KEY|api_key|apiKey"
    r")\b"
)

# G2 日志 / 异常 sink: secret 变量被送进这些 = 泄露面
_LOG_SINK_RE = re.compile(
    r"(?:\bLOG\w*\s*[(<]|\blog\w*\s*[(<]"
    r"|std::(?:cout|cerr|clog)\b|\bcout\b|\bcerr\b"
    r"|\bfprintf\s*\(|\bprintf\s*\(|\bsnprintf\s*\("
    r"|throw\b[^;]*(?:runtime_error|logic_error|exception|string))"
)

# G3 credentialed payload / URL 标识符 (整体进日志 = 含 auth/key 泄露)
_CRED_PAYLOAD_RE = re.compile(
    r"\b(?:MakeUserSubscribeFrame|user_subscribe_frame|user_frame"
    r"|BuildUrl|build_url|full_url|request_url|send_payload"
    r"|auth_payload|auth_frame)\b"
)

# G4 TLS insecure 反模式
_TLS_INSECURE_RE = re.compile(
    r"verify_none"
    r"|SSL_VERIFY_NONE|VERIFY_NONE"
    r"|--insecure"
    r"|CURLOPT_SSL_VERIFYPEER\s*,\s*0(?:L|UL)?\b"
    r"|CURLOPT_SSL_VERIFYHOST\s*,\s*0(?:L|UL)?\b"
    r"|set_verify_mode\s*\(\s*[^)]*\bnone\b"
    r"|verify\s*=\s*false"
    r"|verify_peer\s*=\s*false"
)

# G5 secret 明文值硬编码: <SECRET_NAME> = "deadbeef..." (16+ hex) 或 = 0x...
_SECRET_VALUE_RE = re.compile(
    r"(?:WALLET_PRIVATE_KEY|POLYMARKET_API_SECRET|POLYMARKET_API_KEY"
    r"|POLYMARKET_API_PASSPHRASE|GOALSERVE_API_KEY)"
    r"\s*=\s*[\"']?(?:0x)?[0-9a-fA-F]{16,}"
)
# getenv 读取是合规凭证流转, 不是硬编码值
_GETENV_RE = re.compile(r"getenv")

_COMMENT_LINE_RE = re.compile(r"^\s*(?://|/\*|\*)")
_EXEMPT_RE = re.compile(r"CI-EXEMPT:")


def is_skipped(line: str) -> bool:
    """纯注释行 / 显式豁免行 跳过。"""
    if _COMMENT_LINE_RE.match(line):
        return True
    if _EXEMPT_RE.search(line):
        return True
    return False


def _only_whitelist(line: str) -> bool:
    """该行 secret 信号是否仅来自白名单字段 (condition_id/token_id/sequence_no)。"""
    return bool(_WHITELIST_RE.search(line)) and not _SECRET_FIELD_RE.search(line)


# --------------------------------------------------------------------------- #
# 5 条扫描                                                                      #
# --------------------------------------------------------------------------- #

def scan_g1_debug_api(line: str) -> str | None:
    """G1: secret 字段名进观测层 (仅 debug_api 路径调用)。"""
    if _SECRET_FIELD_RE.search(line) and not _only_whitelist(line):
        return ("G1 secret 字段名进 debug_api 观测层 "
                "(state_provider allowlist 原则, 黑名单字段物理上不应存在)")
    return None


def scan_g2_secret_in_log(line: str) -> str | None:
    """G2: secret 变量被拼进日志 / 异常串。"""
    if not _LOG_SINK_RE.search(line):
        return None
    if _only_whitelist(line):
        return None
    if _SECRET_FIELD_RE.search(line):
        return ("G2 secret / 凭证变量疑似进日志 / 异常串 "
                "(P-09: api_key/secret/passphrase/private_key 严禁落日志)")
    return None


def scan_g3_credentialed_payload(line: str) -> str | None:
    """G3: credentialed payload / URL 整体进日志。"""
    if _LOG_SINK_RE.search(line) and _CRED_PAYLOAD_RE.search(line):
        return ("G3 credentialed payload / URL 整体进日志 "
                "(user subscribe frame 含 auth / Goalserve URL 含 key — 严禁整条进日志)")
    return None


def scan_g4_tls_insecure(line: str) -> str | None:
    """G4: TLS insecure 反模式。"""
    if _TLS_INSECURE_RE.search(line):
        return ("G4 TLS insecure 反模式 "
                "(verify_none / VERIFY_NONE / --insecure / verify=false — §4.1 TLS 红线)")
    return None


def scan_g5_secret_value(line: str) -> str | None:
    """G5: secret 明文值硬编码进源码。"""
    if _GETENV_RE.search(line):
        return None  # getenv 读环境变量是合规凭证流转
    if _SECRET_VALUE_RE.search(line):
        return ("G5 secret 明文值疑似硬编码进源码 "
                "(.env 值不得进 git; 凭证只从 getenv 读)")
    return None


def scan_file(path: Path, repo_root: Path) -> list[tuple[int, str, str]]:
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []
    rel = str(path.relative_to(repo_root))
    in_debug_api = rel.startswith(_DEBUG_API_REL)

    violations: list[tuple[int, str, str]] = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if is_skipped(line):
            continue
        # G1 仅在 debug_api 观测层路径
        if in_debug_api:
            v = scan_g1_debug_api(line)
            if v:
                violations.append((lineno, v, line.rstrip()))
        for scanner in (
            scan_g2_secret_in_log,
            scan_g3_credentialed_payload,
            scan_g4_tls_insecure,
            scan_g5_secret_value,
        ):
            v = scanner(line)
            if v:
                violations.append((lineno, v, line.rstrip()))
    return violations


def main() -> int:
    ap = argparse.ArgumentParser(
        description="secret / 凭证 / TLS insecure 反模式扫描 (5 条, 小白审计 §5)"
    )
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root.resolve()

    all_violations: list[tuple[Path, int, str, str]] = []
    scanned_any = False
    for d in _SCAN_DIRS:
        root = repo_root / d
        if not root.exists():
            continue
        scanned_any = True
        for path in sorted(root.rglob("*")):
            if not path.is_file() or path.suffix not in _SRC_SUFFIXES:
                continue
            rel = str(path.relative_to(repo_root))
            if _is_excluded(rel):
                continue
            for lineno, label, line in scan_file(path, repo_root):
                all_violations.append((path, lineno, label, line))

    if not scanned_any:
        print("[secret_blacklist] PASS: src/ include/ 不存在, 跳过")
        return 0

    if not all_violations:
        print("[secret_blacklist] PASS: 0 secret/凭证/TLS-insecure violations found "
              "(5 条 G1-G5 全过)")
        return 0

    print(f"[secret_blacklist] FAIL: {len(all_violations)} violation(s):", file=sys.stderr)
    for path, lineno, label, line in all_violations:
        rel = path.relative_to(repo_root)
        print(f"  ::error file={rel},line={lineno}::{label}: {line}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引 (小白审计 §5 / CLAUDE.md §8 红线):\n"
        "  G1: secret 字段名不得进 debug_api 观测 struct — allowlist 原则 (只放安全字段).\n"
        "  G2: secret/凭证严禁拼进 LOG/cout/cerr/fprintf/throw — 发送前掩码或不打印 (P-09).\n"
        "  G3: user subscribe frame (含 auth) / Goalserve URL (含 key) 严禁整条进日志.\n"
        "      日志只记 endpoint 类型 + host, 不记完整 URL / payload.\n"
        "  G4: TLS 严禁 verify_none / --insecure / verify=false — 必须 verify_peer + host 校验.\n"
        "  G5: secret 明文值不得硬编码进 git — 凭证只从 getenv 读环境变量.\n"
        "  白名单 condition_id/token_id/sequence_no 是协议公开字段, 不触发.\n"
        "  合规例外: 行尾加 // CI-EXEMPT: <理由> 后提单 @老郭/@老韩 24h 仲裁.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
