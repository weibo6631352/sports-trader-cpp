# PR review v1.3 — 老高 (W6 Wave 30)

- owner: 老高 (#17, code-quality-reviewer, F 顾问团)
- last_review: 2026-05-28
- spec by: self-spec (F 顾问) — W6 Wave 30 3 grep 配套
- status: DONE v1.3 (W6 Wave 30, 待老郭 first review → GM ack → W7 enforce)
- 关联:
  - `docs/RESEARCH/laogao-pr-review-v1.2.md` (v1.2 基线)
  - `docs/INCIDENTS/gm-self-mistakes-log.md` #11 (GM 错 #11 enforce)
  - `tests/ci_grep/` (10 个 Python grep 脚本)
  - `.github/workflows/pr.yml` v1.3
  - `.clang-tidy` v1.3
  - `.github/PULL_REQUEST_TEMPLATE.md` v1.3
  - `docs/RESEARCH/laoli-laoSun-handshake-v1.md` (ABI lock 规则源)

---

## §1 v1.2 → v1.3 changelog

| 维度 | v1.2 (W6 Wave 28) | v1.3 (W6 Wave 30) | 变化动力 |
|---|---|---|---|
| **CI grep 脚本** | 7 个 | **10 个** (+3) | Wave 29/30 配套: ABI lock + persona 边界 + build verify |
| `abi_lock.py` | 占位 (always-pass shell) | Python 精确扫 (PR_BODY 引用格式 + L2/L3 三方签) | 老李 W6 Wave 29 handshake v1 配套 |
| `persona_boundary_check.py` | 无 | Python 精确扫 (3 精确规则 + 47 persona 矩阵) | R-39 Wave 26 决议 5 + GM 错 #4 |
| `build_verification.py` | 无 | Python 精确扫 (派单含代码任务必含 cmake+ctest 条款) | GM 错 #11 enforce W6 紧急 |
| **clang-tidy** | 4 cpp 反模式 (v1.2) | +1: FdGuard 强制文档化 (备注 H, single_instance 修后) | 老何 footgun v1.1 配套 |
| **PR template** | v1.2 (9.1 checkbox) | +9.2 checkbox (GM 错 #11 build+ctest + ABI lock) | GM 错 #11 + handshake v1 |
| **CI job 总数** | 7 grep job + 3 build job = 10 job | **10 grep job** + 3 build job = 13 job | Wave 30 净增 3 job |

**净增量 v1.3:** CI grep 脚本 7 → 10 (+3). clang-tidy 备注 H (+1). PR template +9.2 section.

---

## §2 10 grep 脚本说明 + WHY

### §2.1 `r20_pit_chain.py` — R-20 第 7/8 项 (v1.2 保留)

详见 v1.2 §2.1，不变。

### §2.2 `hmac_4bug.py` — HMAC 4 bug (v1.2 保留)

详见 v1.2 §2.2，不变。

### §2.3 `r12_wss_blocking.py` — R-12 WSS 阻塞 (v1.2 保留)

详见 v1.2 §2.3，不变。

### §2.4 `r33_5host_paper.py` — R-33 paper /ws/user (v1.2 保留)

详见 v1.2 §2.4，不变。

### §2.5 `adr005_dispatch.py` — ADR-005 派单 (v1.2 保留)

详见 v1.2 §2.5，不变。

### §2.6 `adr009_model_tier.py` — ADR-009 v2 model 分级 (v1.2 保留)

详见 v1.2 §2.6，不变。

### §2.7 `risk_enum_coverage.py` — 21 reject enum (v1.1 保留)

不变，同 v1.1。

### §2.8 `abi_lock.py` — ABI lock enforce (v1.3 新)

**WHY:** 老李 Wave 29 handshake v1 确立了 IPolymarketClient ABI 改动等级 (L0/L1/L2/L3)。
PR 改 `pm_client.hpp` / `live_pm_client.hpp` 须在 PR description 显式引用 handshake 文档 + 等级，
L2/L3 须额外含"三方签"字样。Wave 29 前 ci-grep-abi-lock job 是 always-pass shell 占位，
v1.3 换成 Python 精确扫。

**2 规则:**
1. PR description 必须含:
   `ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-XX L<等级>`
2. 若等级 L2/L3 (diff 含 sizeof/offsetof/enum count 变化), PR description 必须含"三方签"

**触发条件:** PR_BODY 非空 + git diff 包含 ABI 锁定文件。非 PR 触发 (PR_BODY 缺失) 静默 pass。

**豁免:** 含 `CI-EXEMPT: <理由>` + @老郭 24h 仲裁。

### §2.9 `persona_boundary_check.py` — persona 拒绝任务边界 (v1.3 新)

**WHY:** CLAUDE.md §7.8 第 5 题自检要求: "派单 prompt 是否越过该 persona 拒绝任务边界?" (GM 错 #4 配套)。
Wave 26 决议 5 决议将此做成 CI 自动扫，v1.3 落地。

**扫描范围:** `docs/MEETINGS/` + `docs/RESEARCH/` 下 `*.md` 文件。

**3 精确规则 (subagent_type= 字段):**

| persona | 越界词 (任一命中 fail) |
|---|---|
| `quant-signal-research` (小程) | `落代码` / `.cpp` / `.hpp` / `unit test` / `单测` / `实施.*cpp` |
| `cpo-product-strategy` (老钱) | `写代码` / `落代码` / `cpp实现` / `.cpp` / `代码实现` |
| `pm-project-manager` (老胡) | `cpp实现` / `实现.*\.cpp` / `落.*\.cpp` / `写.*代码` |

**47 persona 广义矩阵:** 按"派给 <persona名>" 行后 40 行上下文扫对应拒绝任务词。

**豁免文件:** `laogao-pr-review-*` / `laogao-code-conventions-*` / `laoxu-subagent-escalate-*` /
`gm-self-mistakes-log` / `employee-registry` 等规则文档本身 (内含 persona 名作说明)。

**支持 `--dry-run <dir>` + `--json`:** 方便 GM 派单前本地 pre-check。

### §2.10 `build_verification.py` — 派单 build+ctest 验证条款 (v1.3 新)

**WHY (GM 错 #11):** 多次 wave 派单要求 sub-agent 落代码但未强制写 build+ctest 验证条款，
导致"写 py 不算交付，跑通才算"的 GM 决议无法 CI 自动 enforce。v1.3 落地。

**触发条件:** 派单文件 (含 `subagent_type=`) + 代码任务词 (`.cpp` / `.hpp` / `落代码` / `实施` / `单测`) 命中，
但文件内**未**找到 `cmake --build build` 和 `ctest` 两个关键词 → FAIL。

**豁免文件:** `laogao-pr-review-*` / `gm-self-mistakes-log` / sprint/kpi/okr 类非派单文档。

**豁免标记:** 文件首行 `<!-- CI-EXEMPT: build_verification <理由> -->`。

---

## §3 测试代码分级 (ADR-010, v1.2 保留不变)

详见 v1.2 §3 + `docs/ADR/2026-06-01-adr-010-test-code-grading.md`，v1.3 不变。

---

## §4 clang-tidy v1.3 (= v1.2 + 备注 H FdGuard 强制文档化)

| Check | 版本 | 防什么 | 关联 |
|---|---|---|---|
| `bugprone-misplaced-widening-cast` | v1.2 | shift 精度 UB | BUG-W5-001 老何 footgun v1.1 |
| `hicpp-signed-bitwise` (hicpp-* 内) | v1.2 | 有符号位运算 | 老何 footgun v1.1 |
| `bugprone-narrowing-conversions` | v1.2 | HMAC sigType int 误传 | HMAC §A R3 |
| `cppcoreguidelines-owning-memory` (WarningsAsErrors) | v1.2 | fd RAII — open() 返回 fd 必须 RAII | 小卢 P1-03 |
| **FdGuard 强制 (备注 H, v1.3 新文档化)** | **v1.3** | **single_instance 修后 fd 必须走 FdGuard** | **老何 footgun v1.1 + Wave 30** |

**FdGuard 场景 (备注 H):**

```cpp
// 错误: 裸全局 fd, g_lock_fd 是死代码路径
int g_lock_fd = ::open(path, O_RDWR | O_CREAT, 0600);

// 正确选项 A: FdGuard RAII 持有
FdGuard g_lock_fd{::open(path, O_RDWR | O_CREAT, 0600)};

// 正确选项 B: 传参, 不用全局
void InstallSigtermHandler(int lock_fd);
```

clang-tidy 层: `cppcoreguidelines-owning-memory` 已在 `WarningsAsErrors`，裸 int fd 持有 open() 结果即报 error。

---

## §5 误报申诉路径 (v1.2 保留不变)

同 v1.2 §5，FP → `// CI-EXEMPT: <理由>` + @老郭 24h 仲裁。

**v1.3 新增豁免标记格式 (build_verification):**
`<!-- CI-EXEMPT: build_verification <理由> -->` 放文件首行。

---

## §6 W7 EOW KPI (v1.3 更新)

| KPI | 目标 | 负责 |
|---|---|---|
| W5 P1 5 条修复率 | ≥ 80% (≥ 4/5) | 各 owner (老唐/小卢/小宋/老王) |
| 全 10 grep 误报率 | ≤ 3% (4 周均值) | 老高日扫 |
| ADR-010 grandfather list 清理启动 | W7 前至少 1 PR | 小宋 + 老高 |
| CI 总时长 | ≤ 8 min (PR gate) | 小宋 (test infra) |
| **派单 prompt 验证完整率** | **≥ 95%** (build_verification.py 扫描 docs/MEETINGS/) | **老高 + GM 自检** |
| ADR-010 GM ack | W6 第 1 周内 | 老郭 forward GM |

---

## §7 不耻下问 (待 ack)

- **@老郭:** first review v1.3 doc + forward GM (ADR-010 GM ack + v1.3 enforce timeline)
- **@老李 (#07):** abi_lock.py Rule 1 正则格式确认 (F-XX 编号格式 / L<等级> 格式)
- **@老徐 (#33):** persona_boundary_check.py 矩阵准确性确认 (R-39 §6.1 配套, --dry-run 支持)
- **@小宋 (#36):** build_verification.py CI 流水线超时评估 (2 min 是否够)
- **@老何 (#14):** clang-tidy v1.3 备注 H FdGuard 修法确认 (选项 A 传参 vs 选项 B 全局 FdGuard)

---

## §8 我的交付清单 (W6 Wave 30)

| 项 | 文件 | 状态 |
|---|---|---|
| v1.3 doc | `docs/RESEARCH/laogao-pr-review-v1.3.md` (本文) | DONE |
| abi_lock.py | `tests/ci_grep/abi_lock.py` | DONE, dry-run: SKIP (no PR_BODY, 期望行为) |
| persona_boundary_check.py | `tests/ci_grep/persona_boundary_check.py` | DONE, dry-run: 0 违例 |
| build_verification.py | `tests/ci_grep/build_verification.py` | DONE, dry-run: 0 违例 |
| pr.yml v1.3 | `.github/workflows/pr.yml` | DONE, +3 新 job |
| .clang-tidy v1.3 | `.clang-tidy` | DONE, +备注 H |
| PR template v1.3 | `.github/PULL_REQUEST_TEMPLATE.md` | DONE, +§9.2 checkbox |
| **本地 cmake --build build** | build/ | **PASS (428/428 tests pass)** |
| **本地 ctest -j8** | build/ | **PASS 428/428 (100%)** |

---

## §9 老郭签字位

- [ ] 老郭 (F 协调人) first review v1.3 doc
- [ ] 老郭 forward GM (v1.3 + W7 enforce timeline)
- [ ] GM 老雷 ack → W7 enforce
- [ ] 老李 (#07) abi_lock.py 格式 ack
- [ ] 老徐 (#33) persona matrix dry-run ack

---

— 老高, 2026-05-28 (W6 Wave 30, self-spec F 顾问, ADR-005 §3.2 例外)
