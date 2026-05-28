# PR review v1.5 — 老高 (W8 Wave 35)

- owner: 老高 (#17, code-quality-reviewer, F 顾问团)
- last_review: 2026-05-28
- spec by: self-spec (F 顾问) — W8 Wave 35 (GM 错 #21 补救)
- status: DONE v1.5 (W8 Wave 35, worktree git commit 已入 branch)
- 关联:
  - `docs/RESEARCH/laogao-pr-review-v1.4.md` (v1.4 基线)
  - `docs/INCIDENTS/gm-self-mistakes-log.md` #21 (GM 错 #21: W8 Wave 34 worktree 漏 cp)
  - `tests/ci_grep/` (17 个 Python grep 脚本)
  - `.github/workflows/pr.yml` v1.5
  - `.github/PULL_REQUEST_TEMPLATE.md` v1.5 §9.4
  - ADR-021 (worktree 隔离, W8 起强约束)
  - ADR-023 (IC 自测唯一裁判, 撤 ADR-020/022 Tester review 层)
  - `docs/RESEARCH/laosun-key-management-v5.1.md` (老孙 ed25519 W8 落)

---

## §1 v1.4 → v1.5 changelog

| 维度 | v1.4 (W7 Wave 33) | v1.5 (W8 Wave 35) | 变化动力 |
|---|---|---|---|
| **CI grep 脚本** | 15 个 | **17 个** (+2) | ADR-021/023 配套 + GM 错 #21 补救 |
| `ic_no_self_test.py` | 无 | 新增: IC 改 src/include 但漏写 tests → WARN | ADR-023 反转版 (撤 ADR-020/022 Tester 层) |
| `worktree_check.py` | 无 | 新增: 派单 md subagent_type= 必含 isolation="worktree" → FAIL | ADR-021 W8 起强约束 |
| `abi_lock.py` | v1.4 (PM client 2 文件) | **v1.5: +ed25519.hpp + CMakeLists stcpp_crypto_ed25519 关键词** | 老孙 W8 crypto INTERFACE 落 |
| **CI job 总数** | 15 grep job + 3 build job = 18 job | **17 grep job** + 3 build job = 20 job | +2 job |
| **PR template** | v1.4 (§9.3) | +§9.4 (ADR-021/023 + abi_lock v1.5 checkbox) | v1.5 配套 |

**净增量 v1.5:** CI grep 脚本 15 → 17 (+2). CI job 18 → 20 (+2). PR template +§9.4.

---

## §2 17 grep 脚本说明

### §2.1 — §2.15 (v1.4 保留, 见 v1.4 文档)

v1.4 15 个脚本完整保留, 无变更。

### §2.16 `ic_no_self_test.py` — IC 漏写测试 WARN (v1.5 新, ADR-023)

**WHY (ADR-023):** ADR-020/022 要求独立 Tester review, 但 W3-W7 实际无专职 Tester (仅 IC).
ADR-023 决策: IC 自测是唯一裁判, Tester review 层撤销. 但 IC 必须为自己改的 src/include 写测试.
本 check 反转 ADR-020 逻辑 (原: IC 同写 src+tests = WARN; 现: IC 改 src 但漏写 tests = WARN).

**规则:**
- PR 改 `src/<X>.cpp` 或 `include/<X>.hpp`
- 但无对应 `tests/.../<X>_test.cpp` 改动
- → WARN (W8-W9 warning-only, W10 升 FAIL)

**3 种豁免:**
1. 路径含 "sanity" / "abi_lock" / "abi" (sanity check / ABI lock 文件)
2. PR_BODY 含 "P0 hotfix" + "老板 ack"
3. PR_BODY 含 "ADR-005 §3.2 例外" 或 "GM 紧急例外"

**状态:** W8-W9 WARN (exit 0); W10 起升 FAIL (脚本 exit 改 1 或 CI job 加 --fail-on-warn).

### §2.17 `worktree_check.py` — ADR-021 worktree 隔离 enforce (v1.5 新)

**WHY (ADR-021 + GM 错 #21):** W8 Wave 34 老高产出 6 项因 sub-agent 未 git commit 到 worktree branch,
GM force remove worktree 时漏 cp, 数据彻底丢失. ADR-021 规定 W8 起所有 sub-agent 派单必须带
`isolation="worktree"` 隔离. 本 check 从派单 prompt 文件层面 enforce.

**规则:**
- 扫 git diff 中改动的 `docs/MEETINGS/*.md` 文件
- 任意行含 `subagent_type=` 时, ±12 行窗口内必须有 `isolation="worktree"`
- 否则 FAIL

**豁免:** 含 "P0 例外, 老板 ack" 行 (紧急 P0 < 2h).

**状态:** FAIL W8 起强约束.

### §2.18 `abi_lock.py` v1.5 — crypto INTERFACE 加锁 (W8 升级)

**WHY (老孙 W8 Wave 34):** 老孙 W8 落 `include/stcpp/crypto/ed25519.hpp` + `stcpp_crypto_ed25519`
INTERFACE target. 这是 signer_v52 和 STRATEGY_DECAYED CLI 共用的 crypto 边界.
任何接口变更 (SecureBuffer / Ed25519::sign / verify 签名) = ABI 破坏, 必须走 handshake 文档引用.

**v1.5 变更:**
- `ABI_LOCKED_FILES` 加 `include/stcpp/crypto/ed25519.hpp`
- `ABI_LOCKED_CMAKE_KEYWORDS` 加 `stcpp_crypto_ed25519` (CMakeLists.txt 含此关键词触发)
- Rule 3 (新): CMakeLists.txt 含 crypto INTERFACE target 关键词 = ABI 边界改动, 触发 Rule 1 PR desc 检查

---

## §3 CI job 总览 (v1.5, 17 grep job)

| # | job name | 脚本 | 版本 |
|---|---|---|---|
| 1 | pr-meta-grep | shell inline | v1.2 |
| 2 | redline-grep | shell inline | v1.1 |
| 3 | ci-grep-r20-pit-chain | r20_pit_chain.py | v1.2 |
| 4 | ci-grep-r12-wss-blocking | r12_wss_blocking.py | v1.2 |
| 5 | ci-grep-r33-5host-paper | r33_5host_paper.py | v1.2 |
| 6 | ci-grep-hmac-4bug | hmac_4bug.py | v1.2 |
| 7 | ci-grep-adr009-model-tier | adr009_model_tier.py | v1.2 |
| 8 | ci-grep-abi-lock | abi_lock.py | **v1.5** (+ed25519/crypto) |
| 9 | risk-enum-coverage | risk_enum_coverage.py | v1.1 |
| 10 | (pr-meta-grep 含 persona boundary) | adr005_dispatch.py + persona check | v1.3 |
| 11 | ci-grep-gm-commit-author | gm_commit_author_check.py | v1.4 |
| 12 | ci-grep-binary-large-file | binary_large_file_check.py | v1.4 |
| 13 | ci-grep-fom | fom_check.py | v1.4 (W7 W4 激活 FAIL) |
| 14 | ci-grep-abi-cascade | abi_cascade_check.py | v1.4 |
| 15 | ci-grep-adr010-wno | adr010_wno_check.py | v1.4 |
| 16 | **ci-grep-ic-no-self-test** | **ic_no_self_test.py** | **v1.5 新 (WARN W8-W9, FAIL W10)** |
| 17 | **ci-grep-worktree-check** | **worktree_check.py** | **v1.5 新 (FAIL W8 起)** |

---

## §4 GM 错 #21 配套说明

**GM 错 #21 (W8 Wave 34):** 老高 Wave 34 worktree (aa726a8199af29d6e) 产出 6 项,
sub-agent 未 git commit 到 worktree branch. GM force remove worktree 时漏 cp 老高产出,
branch 也删除, 数据彻底丢失.

**丢失内容 (已在 Wave 35 全部重做):**
- `tests/ci_grep/ic_no_self_test.py` (ADR-023 反转版)
- `tests/ci_grep/worktree_check.py` (ADR-021 enforce)
- `docs/RESEARCH/laogao-pr-review-v1.5.md` (本文)
- `pr.yml` v1.5 升 (17 grep)
- PR template §9.4 ADR-021/023 checkbox
- `abi_lock.py` v1.5 ed25519/crypto

**防重演措施:** `worktree_check.py` CI 从派单层 enforce; 同时 GM 错 #21 已入 INCIDENTS log.
本次 Wave 35 完成时强制 `git add + git commit` 到 worktree branch (ADR-021 enforce 自身示范).

---

## §5 dry-run 结果 (W8 Wave 35)

```
ic_no_self_test.py   : SKIP (PR_BODY 未注入) — 期望行为; 无 src/include 改动 pass
worktree_check.py    : SKIP / PASS — docs/MEETINGS/ 派单 md 扫描正常
abi_lock.py v1.5     : SKIP (PR_BODY 未注入) — 期望行为; 0 cpp diff 无触发
cmake build          : PASS (0 diff cpp, 不破 baseline)
ctest                : 441/441 (v1.5 无 cpp 改动, 不动测试)
```

---

## §6 不耻下问 (v1.5 待 ack)

- **@老郭:** forward GM v1.5 ack + ADR-023 §2 sanity check ≤30 行豁免标准确认
- **@小宋:** Tester 名单 W3-W7 无 Tester 事实确认 (ADR-023 立依据)
- **@老雷:** GM 错 #21 ack + v1.5 enforce timeline (W10 ic_no_self_test 升 FAIL)
- **@老孙:** crypto INTERFACE ABI lock 触发词 `stcpp_crypto_ed25519` 是否完整覆盖所有下游

---

## §7 我的交付清单 (W8 Wave 35)

| 项 | 文件 | 状态 |
|---|---|---|
| v1.5 doc | `docs/RESEARCH/laogao-pr-review-v1.5.md` (本文) | DONE |
| ic_no_self_test.py | `tests/ci_grep/ic_no_self_test.py` | DONE (ADR-023 反转版) |
| worktree_check.py | `tests/ci_grep/worktree_check.py` | DONE (ADR-021 enforce) |
| abi_lock.py v1.5 | `tests/ci_grep/abi_lock.py` | DONE (+ed25519/crypto) |
| pr.yml v1.5 | `.github/workflows/pr.yml` | DONE (17 grep job) |
| PR template v1.5 | `.github/PULL_REQUEST_TEMPLATE.md` | DONE (+§9.4) |
| **worktree git commit** | wave-35 branch | **强制执行, 见 git log** |
| **cmake --build + ctest** | build/ | **PASS 441/441 (0 diff cpp)** |

---

— 老高, 2026-05-28 (W8 Wave 35, self-spec F 顾问, ADR-005 §3.2 例外, GM 错 #21 补救)
