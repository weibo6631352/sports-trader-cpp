# PR review v1.2 — 老高 (W6 Wave 28)

- owner: 老高 (#17, code-quality-reviewer, F 顾问团)
- last_review: 2026-06-01
- spec by: 老郭 (F 协调人, Wave 26 决议 #3 + 决议 #7 forward; 多 W5 Smell 配套)
- status: Draft v1.2 (W6 Wave 28, 待老郭 first review → GM ack → W7 enforce)
- 关联:
  - `docs/RESEARCH/laogao-pr-review-v1.1.md` (v1.1 基线)
  - `docs/MEETINGS/2026-06-01-code-review-summit-v1.md` 决议 #3 + #7
  - `docs/MEETINGS/2026-06-01-input-laogao-code-quality.md` W5 5 P1 + 8 P2
  - `docs/ADR/2026-06-01-adr-010-test-code-grading.md` (本文配套)
  - `tests/ci_grep/` (7 个 Python grep 脚本)
  - `.github/workflows/pr.yml` v1.2
  - `.clang-tidy` v1.2
  - `.github/PULL_REQUEST_TEMPLATE.md` v1.2

---

## §1 v1.1 → v1.2 changelog

| 维度 | v1.1 (W5) | v1.2 (W6) | 变化动力 |
|---|---|---|---|
| **CI grep 脚本** | 1 个 (risk_enum_coverage.py) | **7 个** (+6) | 决议 #3: 5 grep 真落 CI (v1.1 只有设计稿) |
| R-20 第 7 项 grep | shell grep (误报 2 例, 注释行命中) | Python 精确扫 (`r20_pit_chain.py`) | 老高 W5 input §1 备注: 收窄为赋值语句 |
| R-20 第 8 项 grep | shell grep | Python (同 `r20_pit_chain.py`) | 合并入精确扫脚本 |
| R-12 WSS grep | shell grep (4 条) | Python 精确扫 (`r12_wss_blocking.py`) | WSS 路径判断更准, SPIN_OK 豁免规则化 |
| R-33 grep | shell grep (2 条) | Python 精确扫 (`r33_5host_paper.py`) | paper 路径判断更准, 注释行排除 |
| HMAC 4 bug grep | shell grep (4 条) | Python 精确扫 (`hmac_4bug.py`) | HMAC_BASE_STRING_TEST 豁免规则化 |
| ADR-005 派单 grep | shell grep (pr-meta-grep job 内联) | 独立 Python `adr005_dispatch.py` + CI 内联并存 | 本地 dry-run 支持 |
| ADR-009 model tier | 无 | Python `adr009_model_tier.py` (新) | ADR-009 v2 全员 Sonnet, Opus 收口 |
| **clang-tidy** | 4 cpp 反模式 (v1.1) | +4: shift widening-cast + signed-bitwise + narrowing-conv + fd RAII | BUG-W5-001 老何 footgun + 小卢 P1-03 |
| **测试代码分级** | 无显式标准 | **ADR-010 候选** (grandfather list + 禁新增) | 决议 #7, 透明化技术债 |
| **PR template** | 7 节 | +2 checkbox (ADR-009 opus + R-20/R-12/R-33/HMAC 自检) | v1.2 Smell 配套 |

**净增量:** CI grep 脚本 1 → 7 (+6). clang-tidy CheckOptions +3. PR template +2 checkbox.

---

## §2 7 grep 脚本说明 + WHY

### §2.1 `r20_pit_chain.py` — R-20 第 7/8 项

**第 7 项:** `data_source_ts = <本地时钟>` 赋值命中即 P0。

精度升级: v1.1 shell grep `data_source_ts.*now()` 命中注释行 2 例 (goalserve_record.hpp + wss_event.hpp 文档注释)。v1.2 改 Python，仅匹配赋值语句，排除注释行和 CI-EXEMPT 行。

**第 8 项:** `event_ts = ingestion_ts` 互赋命中即 P0。

WHY: 4 ts 语义不可混。`event_ts` 是事件发生上游标，`ingestion_ts` 是本地落地时。

### §2.2 `hmac_4bug.py` — HMAC 4 bug

参考老李 `laoli-polymarket-backend-requirements-v1.md` §5 + `laoli-polymarket-endpoint-matrix-v3.md` §A R1-R4。

4 bug 对应关系:
- Bug 1: `request_path + "?" + query` — Python 多模式匹配
- Bug 2: `paramType / param_type / param-type` — 精确词边界匹配
- Bug 3: `sigType=2` / JSON `"sigType": 2` — 多格式覆盖
- Bug 4: Python `rstrip(b"=")` / `replace("=", "")` — 跨语言 (C++ 和 .py 文件均扫)

豁免: `HMAC_BASE_STRING_TEST` 注释行 (URL builder 单测合法拼接)。

### §2.3 `r12_wss_blocking.py` — R-12 WSS 阻塞

扫描路径判断: `is_wss_path()` 函数，匹配 `/wss/` 目录或 `ws_handler` 文件名。`tests/` 路径自动排除 (mock WSS handler 测试允许使用锁)。

4 反模式:
1. `std::lock_guard / unique_lock / scoped_lock / shared_lock` + `// SPIN_OK` 豁免
2. `.blocking_read / .blocking_write / recv( / read_until(`
3. `synchronous_http / http_client::Get/Post / curl_easy_perform`
4. `fsync( / fdatasync(`

### §2.4 `r33_5host_paper.py` — R-33 paper /ws/user

2 规则独立函数，路径判断分离:
- `is_paper_path()`: 匹配 `/paper/` 目录或 `paper_*.cpp/hpp`
- Rule 1 (paper + /ws/user): 只扫 paper 路径
- Rule 2 (旧 clob host): 扫全部 src/ + include/，排除 docs/ 和 tests/

### §2.5 `adr005_dispatch.py` — ADR-005 派单

CI 内联 (pr-meta-grep job shell grep) + 独立 Python 脚本并存。

Python 版支持本地 dry-run:
```bash
echo "spec by 老周 (A 单元 W6-A-05)" | python3 tests/ci_grep/adr005_dispatch.py
# 或
PR_BODY="spec by 老郭 (W6-F-01 forward)" python3 tests/ci_grep/adr005_dispatch.py
```

### §2.6 `adr009_model_tier.py` — ADR-009 v2 model 分级

扫描 `.claude/agents/*.md` + `docs/**/*.md` + `.github/**/*.md`。

检测: `model: opus` 后 20 行内无 `例外:` 段 → FAIL。

不允许的理由 (老板二次校正 2026-05-28):
- "管理层身份"
- "我觉得 Sonnet 不够好"
- "task 复杂"

### §2.7 `risk_enum_coverage.py` — 21 reject enum (v1.1 保留)

不变，同 v1.1。见 `tests/ci_grep/risk_enum_coverage.py`。

---

## §3 测试代码分级 (ADR-010 候选)

详见 `docs/ADR/2026-06-01-adr-010-test-code-grading.md`。

**执行摘要:**
- 生产代码 `src/ + include/`: 零 `-Wno-*` 抑制 (严格)
- 测试代码 `tests/`: grandfather list 允许 4 项 (gtest 框架引起)
- 禁止新增: `-Wno-sign-conversion / -Wno-shadow / -Wno-conversion`
- PR v1.2 gate: 测试代码新增非 grandfather list 的 `-Wno-*` → reject

---

## §4 clang-tidy v1.2 新增 4 项

| Check | 版本 | 防什么 | 关联 |
|---|---|---|---|
| `bugprone-misplaced-widening-cast` | v1.2 | shift 精度: `(1 << 40)` int 域 UB → 应 `1ULL << 40` | BUG-W5-001 老何 footgun v1.1 |
| `hicpp-signed-bitwise` (已在 hicpp-*) | v1.2 显式记录 | 有符号类型位运算 = 实现定义行为 | 老何 footgun v1.1 shift-signed |
| `bugprone-narrowing-conversions` | v1.2 | HMAC sigType int 误传防护 (配合 easily-swappable-parameters) | HMAC §A R3 + v1.2 sigType 配套 |
| `cppcoreguidelines-owning-memory` (已在 WarningsAsErrors) | v1.2 显式关联 | fd RAII — `::open()` 返回 fd 必须 RAII 持有 (FdGuard) | 小卢 P1-03 g_lock_fd_for_handler 死代码 |

**shift static_assert 规范 (配套):**

生产代码含可变 shift width 时，必须有:
```cpp
static_assert(N < 64, "shift width must be < 64 to avoid UB");
uint64_t x = uint64_t{1} << N;  // 正确
```

---

## §5 误报申诉路径

| 步骤 | 操作 | 时限 |
|---|---|---|
| 1 | CI 报红 → PR author 先判断是否真违规 | 即时 |
| 2 | 确认 FP → 代码行加 `// CI-EXEMPT: <理由>` | 同次 push |
| 3 | PR description 加 `@老郭 FP 申诉: <grep 名> line XX` | 同次 push |
| 4 | 老郭 24h 内裁决 | 24h |
| 5a | 裁决"坚持 enforce" → author 修复代码 | - |
| 5b | 裁决"确认 FP" → 老高升级 grep 加豁免规则, 走 PR 改 `tests/ci_grep/*.py` | 下一个 PR |
| 6 | 改 grep 规则的 PR 必经老高 + 老郭 + 老雷 三方任一签字 | - |

**总体目标误报率 (W7 enforce 后 4 周内):** 全 7 grep 加权 ≤ 3%。

---

## §6 W7 EOW KPI (老胡周报)

| KPI | 目标 | 负责 |
|---|---|---|
| W5 P1 5 条修复率 (P1-01~P1-05) | ≥ 80% (≥ 4/5) | 各 owner (老唐/小卢/小宋/老王) |
| 全 7 grep 误报率 | ≤ 3% (4 周均值) | 老高日扫 |
| ADR-010 grandfather list 清理启动 | W7 前至少 1 PR (tests/ -Wno-sign-conversion 开始删) | 小宋 + 老高 |
| CI 总时长 | ≤ 8 min (PR gate) | 小宋 (test infra) |
| ADR-010 GM ack | W6 第 1 周内 | 老郭 forward GM |

---

## §7 不耻下问 (待 ack)

- **@老郭:** first review v1.2 doc + forward GM (ADR-010 + CLAUDE.md §10 upgrade)
- **@老李 (#07):** HMAC `hmac_4bug.py` bug 1 正则准确性确认 (request_path 拼接模式)
- **@老何 (#14):** footgun v1.1 shift static_assert 具体建议 — 我 §4 备注 E 代填, 请 W6 ack 修正
- **@小卢 (IC pool):** P1-03 FdGuard 修法确认 — 加 `lock_fd` 参数 vs 删 close 分支哪个方案
- **@小宋 (#36):** CI workflow PR + grandfather list 清理 W7 timeline 是否可接
- **@小冯 (#38):** WSS CMakeLists.txt 4 行 -Wno-* 来源确认 (boost.beast vs nlohmann::json)
- **@老韩 (#09):** ADR-014 BLAKE3 stub 严禁进 live build — CI grep 方案是否需独立 blake3_stub.py?

---

## §8 我的交付清单 (W6 Wave 28)

| 项 | 文件 | 状态 |
|---|---|---|
| v1.2 doc | `docs/RESEARCH/laogao-pr-review-v1.2.md` (本文) | DRAFT, 待老郭 review |
| r20_pit_chain.py | `tests/ci_grep/r20_pit_chain.py` | DONE, smoke: 0 违例 |
| r12_wss_blocking.py | `tests/ci_grep/r12_wss_blocking.py` | DONE, smoke: 0 违例 |
| r33_5host_paper.py | `tests/ci_grep/r33_5host_paper.py` | DONE, smoke: 0 违例 |
| hmac_4bug.py | `tests/ci_grep/hmac_4bug.py` | DONE, smoke: 0 违例 |
| adr005_dispatch.py | `tests/ci_grep/adr005_dispatch.py` | DONE, 本地 dry-run ✓ |
| adr009_model_tier.py | `tests/ci_grep/adr009_model_tier.py` | DONE, smoke: 0 违例 |
| pr.yml v1.2 | `.github/workflows/pr.yml` | DONE, +5 新 job |
| .clang-tidy v1.2 | `.clang-tidy` | DONE, +4 check 注释 |
| ADR-010 候选 | `docs/ADR/2026-06-01-adr-010-test-code-grading.md` | DONE, 待老郭 first review |
| PR template v1.2 | `.github/PULL_REQUEST_TEMPLATE.md` | DONE, +2 checkbox |
| CLAUDE.md §10 草拟 | 本文 §8.2, 待老郭 forward GM | 不直接改 (越权) |

---

## §8.2 CLAUDE.md §10 升级建议 (老郭 forward GM)

建议在 §10 给 sub-agent 操作约定加一条 (位置: §10 末尾, 升级 v1.1 → v1.2):

> **PR 提交必经老高 v1.2 review (W7 起 enforce)**:
> - 每个 PR 必带 §0 "spec by <主管>" 派单链声明 (ADR-005 CI grep enforce)
> - CI `pr-meta-grep` 检查派单声明 + persona 边界
> - CI `ci-grep-r20-pit-chain` / `ci-grep-r12-wss-blocking` / `ci-grep-r33-5host-paper` /
>   `ci-grep-hmac-4bug` / `ci-grep-adr009-model-tier` 5 个 Python grep job
> - CI `redline-grep` 保留 v1.1 shell grep (R-20 1-6 + HMAC + R-11 + R-33 + 老项目)
> - CI `risk-enum-coverage` 21 reject enum 覆盖
> - 老高人脑 review: 命名 / WHY 注释 / 红线 §9 / 测试代码分级 (ADR-010)
> - FP 申诉: PR 加 `// CI-EXEMPT: <理由>` + 提单 @老郭 24h 仲裁

走法: 老郭 W6 EOW 同步上呈, GM 拍板进 CLAUDE.md, 我不直接改。

---

## §9 老郭签字位

- [ ] 老郭 (F 协调人) first review v1.2 doc
- [ ] 老郭 forward GM (CLAUDE.md §10 升级 v1.2 + ADR-010 GM ack)
- [ ] GM 老雷 ack → W7 enforce
- [ ] 老何 (#14) footgun v1.1 shift 协同 ack (W6 footgun file 修正 §4 备注 E)
- [ ] 老韩 (#09) BLAKE3 stub CI grep 方案 ack (ADR-014 配套)

---

— 老高, 2026-06-01 (W6 Wave 28, self-spec F 顾问, ADR-005 §3.2 例外)
