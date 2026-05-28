# PR review v1.7 — 老高 (W9 Wave 60)

- owner: 老高 (#17, code-quality-reviewer, F 顾问团)
- last_review: 2026-05-29
- spec by: self-spec (F 顾问) — W9 Wave 60 (ADR-027 Enforce-3 上线)
- status: DONE v1.7 (W9 Wave 60, worktree git commit 已入 branch)
- 关联:
  - `docs/RESEARCH/laogao-pr-review-v1.5.md` (v1.5 基线)
  - `docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md` (ADR-027)
  - `docs/RESEARCH/laoguo-w8-w5-adr-027-main-review.md` (老郭主审 §1 Enforce-3 细节)
  - `docs/RESEARCH/laohan-w9-orderintent-v05-spec-v1.md` §3.4 (abi_lock v1.7 触发词)
  - `docs/RESEARCH/laosun-w9-signer-v53-abi-align-spec-v1.md` §4 (SignV52Request ABI)
  - `tests/ci_grep/` (20 个 Python grep 脚本)
  - `.github/workflows/pr.yml` v1.7
  - `.github/PULL_REQUEST_TEMPLATE.md` v1.7 §9.5 §9.6
  - ADR-027 (核心数据结构 SSOT enforce, 老郭主审完成 W8 W5)
  - ADR-024 (worktree 标准流程, §3.1 pwd verify + §6 GM merge audit)

---

## §1 v1.5 → v1.7 changelog (跳过 v1.6)

| 维度 | v1.5 (W8 Wave 35) | v1.7 (W9 Wave 60) | 变化动力 |
|---|---|---|---|
| **CI grep 脚本** | 17 个 | **20 个** (+3) | ADR-027 Enforce-3 + ADR-024 §3.1/§6 |
| `core_data_structure_ssot_check.py` | 无 | **新增**: ADR-027 C1-C4 SSOT cite + token_id + Side + handshake | ADR-027 Enforce-3 老高 W9 W4 任务 |
| `worktree_commit_check.py` | 无 | **新增**: isolation=worktree 必伴 pwd verify (Rule A FAIL) + 跨写 main WARN (Rule B) | ADR-024 §3.1 防 GM 错 #19 重演 |
| `gm_merge_audit.py` | 无 | **新增**: GM merge commit "merge: <persona> <Wave NN>" 格式 WARN | ADR-024 §6 老郭 spec |
| `abi_lock.py` | v1.5 (crypto/ed25519) | **v1.7: +OrderIntent/Position/Side/Outcome/SignV52Request/PositionKey struct lock + Rule 4** | 老韩 W9 OrderIntent v0.5 + 老孙 v5.3 ABI break |
| **CI job 总数** | 17 grep + 3 build = 20 job | **20 grep** + 3 build = 23 job | +3 grep job |
| **PR template** | v1.5 (§9.4) | +§9.5 (ADR-027 C1-C4 cite block) + §9.6 (worktree commit verify) | v1.7 配套 |

**净增量 v1.7:** CI grep 脚本 17 → 20 (+3). CI job 20 → 23 (+3). PR template +§9.5 +§9.6.

注: v1.6 跳过 (无 v1.6 wave 排期, 直接 v1.5 → v1.7 连续推进).

---

## §2 20 grep 脚本说明

### §2.1 — §2.17 (v1.5 保留, 见 v1.5 文档)

v1.5 17 个脚本完整保留, 无删除.

### §2.18 `abi_lock.py` v1.7 — struct/enum ABI lock 扩展 (W9 升级)

**WHY (老韩 W9 OrderIntent v0.5 + 老孙 v5.3 + ADR-027):**
OrderIntent v0.5 4 处 ABI break (市场_id→condition_id / +token_id / +outcome / is_buy→Side enum).
SignV52Request v5.3 新增 token_id / side / outcome 3 字段.
ADR-027 Enforce-3 要求 CI 捕获这类 struct/enum 改动并强制 ABI ref.

**v1.7 变更:**
- `ABI_LOCKED_FILES` 加 `include/stcpp/risk/risk_gateway.hpp` (OrderIntent)
- `ABI_LOCKED_FILES` 加 `include/stcpp/signer/signer_iface.hpp` (SignV52Request)
- `ABI_LOCKED_FILES` 加 `include/stcpp/infra/wal/position_record.hpp` + `position_ledger.hpp`
- `ABI_LOCKED_FILES` 加 `include/stcpp/strategy/signal_iface.hpp` (Side + Outcome enum)
- `ABI_LOCKED_STRUCT_KEYWORDS` 新增 struct/enum 关键词扫描 (OrderIntent/Position/SignV52Request/Side/Outcome/PositionKey)
- Rule 4 (新): diff 新增行含上述关键词时, PR description 必须含 ABI ref 行
- `check_struct_keyword_changes()` 新增函数: diff 新增行扫描 struct/enum 关键词

**触发逻辑:** Rule 4 在 Rule 1 前检查; 若 Rule 1 已过 (PR 含 ABI ref 行), Rule 4 自动通过.

### §2.19 `core_data_structure_ssot_check.py` — ADR-027 Enforce-3 C1-C4 (v1.7 新)

**WHY (ADR-027 Enforce-3 + 老郭主审 §1 补充):**
ADR-027 草案 §4.3 C1-C4 四项检查方向正确. 老郭主审补充 3 点工程细节:
- C1 grep 范围含 PR description 全文 (不只 cite: 头)
- C2 grep 精确到 struct body (区分命名空间)
- C4 handshake pattern 集扩展到 `*-handshake-v*.md` 通配
diff_text 来源用 `git diff origin/main...HEAD` (老郭 §3, ADR-024 worktree 流程对齐).

**规则:**
- **C1**: PR diff 改核心 struct 文件 → PR description 含 `laoli-polymarket-data-structure-ssot` AND (`xiaoduan-goalserve-data-structure-ssot` OR `goalserve_ssot_cite: N/A`)
- **C2**: diff 含 OrderIntent/SignedOrder struct 新定义 → 新增行含 `token_id` 字段声明 (绝对约束)
- **C3**: diff 含 `enum class Side` 新定义 → 新增行含 `Buy =` 且 `Sell =`
- **C4**: diff 改核心 struct 文件 → PR description / diff 含 `*-handshake-v*.md` 文件名

**受约束 6 struct:** OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot

**状态:** FAIL W9 W4 起 (ADR-027 生效时间线).

### §2.20 `worktree_commit_check.py` — ADR-024 §3.1 pwd verify 强约束 (v1.7 新)

**WHY (ADR-024 §3.1 + GM 错 #19):**
GM 错 #19 根因: 派单 prompt 含 `isolation=worktree` 但 sub-agent 实际在 main tree Edit.
worktree_check.py (v1.5) 检查派单 md 中 `subagent_type=` 配 `isolation="worktree"`,
但未检查是否配套 `pwd verify` 步骤. 本脚本补充这一约束.

**Rule A (FAIL):**
- 扫 docs/MEETINGS/*.md 含 `isolation=worktree` 的行
- ±12 行窗口内必须有 `pwd verify` 字样
- 否则 FAIL

**Rule B (WARN, exit 0):**
- 扫 git log 最近 10 commit
- 含 `agent-<hash>` 标识的 commit 若只在 main branch (无 worktree branch 中转) → WARN
- 非阻断, 提醒 GM

**豁免:** `P0 例外, 老板 ack` 或 `pwd verify: skip (N/A)` 显式声明.

**状态:** Rule A FAIL W9 起; Rule B WARN-only.

### §2.21 `gm_merge_audit.py` — ADR-024 §6 GM merge commit 格式 WARN (v1.7 新)

**WHY (老郭 ADR-024 §6 spec):**
GM 老雷 merge worktree branch → main 时, commit message 建议规范化:
`"merge: <persona> <Wave NN>"` (如 `"merge: 老高 Wave 60"`).
目的: PR timeline audit trail 可追溯每个 agent wave 产出何时 merge.

**规则:**
- 扫 git log 最近 10 commit
- commit author = "weibo wang" (GM) 且 message 含 "Merge pull request" / "Merge branch"
- 若 message 不符合 `merge: <persona> <Wave NN>` 格式 → WARN

**状态:** WARN-only (exit 0). 后续版本老郭决定是否升 FAIL.

---

## §3 CI job 总览 (v1.7, 20 grep job)

| # | job name | 脚本 | 版本 |
|---|---|---|---|
| 1 | pr-meta-grep | shell inline | v1.2 |
| 2 | redline-grep | shell inline | v1.1 |
| 3 | ci-grep-r20-pit-chain | r20_pit_chain.py | v1.2 |
| 4 | ci-grep-r12-wss-blocking | r12_wss_blocking.py | v1.2 |
| 5 | ci-grep-r33-5host-paper | r33_5host_paper.py | v1.2 |
| 6 | ci-grep-hmac-4bug | hmac_4bug.py | v1.2 |
| 7 | ci-grep-adr009-model-tier | adr009_model_tier.py | v1.2 |
| 8 | ci-grep-abi-lock | abi_lock.py | **v1.7** (+struct lock Rule 4) |
| 9 | risk-enum-coverage | risk_enum_coverage.py | v1.1 |
| 10 | (pr-meta-grep 含 persona boundary) | adr005_dispatch.py + persona check | v1.3 |
| 11 | ci-grep-gm-commit-author | gm_commit_author_check.py | v1.4 |
| 12 | ci-grep-binary-large-file | binary_large_file_check.py | v1.4 |
| 13 | ci-grep-fom | fom_check.py | v1.4 |
| 14 | ci-grep-abi-cascade | abi_cascade_check.py | v1.4 |
| 15 | ci-grep-adr010-wno | adr010_wno_check.py | v1.4 |
| 16 | ci-grep-ic-no-self-test | ic_no_self_test.py | v1.5 |
| 17 | ci-grep-worktree-check | worktree_check.py | v1.5 |
| 18 | **ci-grep-core-data-structure-ssot** | **core_data_structure_ssot_check.py** | **v1.7 新 (ADR-027 C1-C4 FAIL)** |
| 19 | **ci-grep-worktree-commit-check** | **worktree_commit_check.py** | **v1.7 新 (Rule A FAIL, Rule B WARN)** |
| 20 | **ci-grep-gm-merge-audit** | **gm_merge_audit.py** | **v1.7 新 (WARN-only)** |

---

## §4 dry-run 结果 (W9 Wave 60)

```
core_data_structure_ssot_check.py : SKIP (PR_BODY 未注入) — 期望行为; 无核心 struct diff 触发
worktree_commit_check.py          : SKIP / PASS — docs/MEETINGS/ 派单 md 扫描正常
gm_merge_audit.py                 : SKIP (PR_BODY 未注入) / PASS — 无 GM merge commit 问题
abi_lock.py v1.7                  : SKIP (PR_BODY 未注入) — 期望行为
cmake build                       : PASS (0 diff cpp, 不破 baseline)
ctest                             : 458/458 (v1.7 无 cpp 改动, 不动测试)
```

---

## §5 ADR-027 Enforce 落地状态

| Enforce | 内容 | 落地状态 |
|---|---|---|
| Enforce-1 | 核心 struct PR 必含 SSOT cite 段 | PR template §9.5 checkbox + CI C1/C4 |
| Enforce-2 | FOM 4 人 approve (老李/小段/老周/IC) | PR template review 分配节 (人工 enforce) |
| Enforce-3 | ABI lock v1.7 CI grep (老高 W9 W4) | **本次上线: core_data_structure_ssot_check.py + abi_lock.py v1.7** |
| Enforce-4 | GM 验收自检升 6 题 | CLAUDE.md §7 #8 小米同步 (ADR-027 生效后 48h) |

---

## §6 不耻下问 (v1.7 待 ack)

- **@老郭:** C1 goalserve N/A 声明格式确认 (`goalserve_ssot_cite: N/A` 是否足够, 或需更详细格式)
- **@老韩:** OrderIntent v0.5 PR abi_lock Rule 4 触发词 `struct OrderIntent` 是否覆盖 hpp 内所有定义位置
- **@老孙:** SignV52Request 文件路径确认 (`signer_iface.hpp` vs `signer/v52/signer_v52.hpp`)
- **@老雷:** GM ack v1.7 enforce + ADR-027 正式生效确认

---

## §7 我的交付清单 (W9 Wave 60)

| 项 | 文件 | 状态 |
|---|---|---|
| v1.7 doc | `docs/RESEARCH/laogao-pr-review-v1.7.md` (本文) | DONE |
| abi_lock.py v1.7 | `tests/ci_grep/abi_lock.py` | DONE (+struct lock, Rule 4) |
| core_data_structure_ssot_check.py | `tests/ci_grep/core_data_structure_ssot_check.py` | DONE (ADR-027 C1-C4) |
| worktree_commit_check.py | `tests/ci_grep/worktree_commit_check.py` | DONE (ADR-024 §3.1) |
| gm_merge_audit.py | `tests/ci_grep/gm_merge_audit.py` | DONE (ADR-024 §6) |
| pr.yml v1.7 | `.github/workflows/pr.yml` | DONE (20 grep job) |
| PR template v1.7 | `.github/PULL_REQUEST_TEMPLATE.md` | DONE (+§9.5 +§9.6) |
| **worktree git commit** | wave-60 branch | **强制执行** |
| **cmake --build + ctest** | build/ | **PASS 458/458 (0 diff cpp)** |

---

— 老高, 2026-05-29 (W9 Wave 60, self-spec F 顾问, ADR-005 §3.2 例外, ADR-027 Enforce-3 上线)
