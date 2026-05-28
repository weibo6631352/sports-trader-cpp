# PR review v1.1 — 老高 (W5 Wave 24)

- owner: 老高 (#17, code-quality-reviewer, F 顾问团)
- last_review: 2026-05-28
- spec by: 老郭 (F 协调人 W5-F-01 forward, ADR-005 派单)
- status: Draft v1.1 (W5 EOW 待老郭 first review → GM ack → W6 enforce)
- 关联依赖:
  - `docs/RESEARCH/laogao-code-conventions-v1.md` (v1 代码规范, §1/§5/§7/§8/§9)
  - `docs/RESEARCH/laoguo-coordinator-mandate-v1.md` §4 W5-F-01
  - `docs/RESEARCH/laoli-polymarket-backend-requirements-v1.md` §5 HMAC 4 bug + §2 第 5 host
  - `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md` §A R1-R6 / §B SOP / §D 14 wire vector
  - `docs/RESEARCH/laohe-cpp-footgun-checklist-v1.md` (老何 v1, v1.1 W5 EOW 互锁)
  - `.github/workflows/pr.yml` (本文档落地)
  - `.clang-tidy` (本文档落地)
  - `.github/PULL_REQUEST_TEMPLATE.md` (本文档落地)
  - CLAUDE.md §7 (5 题自检) + §8 (红线) + §10 (sub-agent 约定)
  - ADR-005 派单链 (5 主管 + 1 协调人)

> 老郭 §4 W5-F-01 邀请: PR review v1.1 接 R-20 + R-11 + HMAC 4 反模式 + (W5 新增) R-12 / R-33 / ADR-005 / persona 边界. 我倾向"两手都要" — CI hard block (grep 命中即 fail) + PR template soft check (人脑 review). 本文 §1 v1 vs v1.1 差异, §2 每条规则 WHY, §3 PR template 升级, §4 误报与处置, §5 W6 enforce 节奏.

---

## §1 v1 vs v1.1 差异表 (W3 → W5 Wave 24)

| 维度 | v1 (W3) | v1.1 (W5) | 新增动力 |
|---|---|---|---|
| **CI grep job** | 1 个 (`redline-grep`) | 2 个 (拆 `pr-meta-grep` + `redline-grep`) | ADR-005 派单 grep 需 PR body context, 不与 src grep 混 |
| R-20 grep 项 | 7 (now / localtime / int / signal struct / intent struct / audit struct + 4ts 子项) | **8** (+ UPSTREAM_PAYLOAD 优先 + event_ts != ingestion_ts) | 上游 ts 不可本地伪造 (老吴 W3 R-20 漏点) |
| HMAC bug grep | 3 (rstrip / param_type / sigType=2) | **4** (+ bug 1 request_path 拼 querystring) | 老李 v3 §A R1 第 1 bug, v1 漏 |
| R-11 paper 污染 | 1 (字段同行扫) | **3** (+ write 真账本 WAL + paper_*.wal hardcode 真账本模块) | 老韩 RM v0.3 + 老蒋 paper engine 实施前置 |
| R-12 WSS 同步 | 0 (无 grep) | **4** (lock_guard / blocking_read / sync HTTP / fsync) | W5 Sprint-2 WSS conn 开干前置 |
| R-33 第 5 host | 0 (无 grep) | **2** (旧 clob host 当 market / paper 接 /ws/user) | 老李 v3 R-33 流程红线 |
| **ADR-005 派单 grep** | 0 | **1** (PR body 必带 "spec by 主管") | GM 错 #8 (越主管派 IC) 配套 |
| **persona 边界 grep** | 0 | **1** (小程/老钱/小杜/小苏 spec only 声明) | CLAUDE.md §7.8 第 4 题落地 |
| **clang-tidy 反模式** | 通用 check (bugprone/cert/concurrency) | + 4 类 cpp 反模式提示 (见 §3) | HMAC + R-12 + R-20 + R-11 cpp 仿写防护 |
| **PR template** | 6 节 + checklist | **7 节** (新增 §0 派单链) | ADR-005 CI grep 配套 |

**净增量:** CI grep job 1 → 2 (+1), grep 规则 11 → **20** (+9 = R-20 1 + HMAC 1 + R-11 2 + R-12 4 + R-33 2 + ADR-005 1 + persona 边界 1, 减去合并的 3 = 净 +9). clang-tidy CheckOptions 12 → 15 (+3 显式启用). PR template 6 → 7 节.

---

## §2 每条规则的 "为什么" (引 GM 错 / 红线 / ADR)

### §2.1 R-20 第 7/8 项 (UPSTREAM_PAYLOAD 优先)

**第 7 项:** `data_source_ts = std::chrono::... / now()` 命中即 P0.

**WHY:** CLAUDE.md §8 红线: "时间戳优先用数据源自带, 禁本地 now() 替代上游 ts". v1 grep 只查时间字段类型 (int vs int64), 不查赋值源. 但实际 bug = 工程师拿到上游 payload 没标 `event_ts`, 顺手赋 `data_source_ts = now()`, 编译能过但 PIT 全废 (Backtest vs Live 不一致). 老吴 W3 raw stream 文档明示过, v1 漏防.

**第 8 项:** `event_ts = ingestion_ts` 或反向赋值命中即 P0.

**WHY:** 4 ts 语义不可混 — `event_ts` 是事件发生(上游标), `ingestion_ts` 是本地落地. 直接 `=` 等价于伪造 PIT, 但是 R-20 §2.2 §6 隐式禁止, 无显式 grep. v1.1 显式拦.

### §2.2 HMAC bug 1 (request_path 拼 querystring)

**Bug:** `request_path = path + "?" + query` 进 HMAC base string.

**期望:** `request_path = path` only (不拼 query).

**WHY (引老李 v3 §A R1):** "任何带 cursor / next_cursor / asset_type 的 L2 GET 全 401". 这是 4 bug 里最隐蔽的 — Polymarket 文档没写明确, SDK 行为反人类. v1 漏防 grep, v1.1 加 (在 PM 路径下扫 `request_path + ? + query` 拼接).

### §2.3 R-11 升级 2 项 (paper write 真账本 + paper_*.wal hardcode)

**改动:** 旧 grep 只查"paper_xxx position 同行", 太宽松, paper engine 真写 `position.wal` 时不会触发 (因为变量名不带 `paper_` 前缀).

**新 grep:**
1. `src/stcpp/**/paper/**/*.cpp` 路径下 `write_to|fopen|fwrite|fsync` 拼 `position.wal | pnl_ledger.wal | nonce_ledger.wal` = P0
2. `src/stcpp/risk/**` + `src/stcpp/execution/nonce_*` 路径下 hardcode `paper_*.wal` 字面量 = P0 (真账本模块不可知道 paper WAL 路径)

**WHY:** ADR R-11 + 老周 v0.6 物理隔离. paper engine 已立 build-time `STCPP_EXEC_MODE` (CMake), 但 link-time 还有可能漏 (CMake 写错导致 paper target 链 live ledger lib). grep 兜底 source level.

### §2.4 R-12 WSS event loop 4 项

**4 grep:** `std::lock_guard` / `blocking_read` / 同步 HTTP / `fsync` 出现在 `src/**/wss/**` 路径 = P0.

**WHY:** CLAUDE.md §8: "WebSocket event loop 任何同步 REST / 阻塞 IO / 锁 > 100us → P0 (ADR R-12)". v1 无 grep enforce, 靠 review 人脑挡. W5 Sprint-2 老李 / 小冯 / 老周开干 WSS conn 前必须 CI 兜.

**豁免:** spin_lock < 100us 允许 — 但必须代码注释 `// SPIN_OK (< 100us 实测)`, grep 看 `std::lock_guard` 严格挡, spin_lock 不在挡列.

### §2.5 R-33 第 5 host 2 项

**2 grep:**
1. `wss://ws-subscriptions-clob.polymarket.com/ws/market` 出现在 src/include = P0 (旧 host, 应用第 5 host `sports-api.polymarket.com/ws`)
2. paper mode 路径 (`paper/**` 或 `paper_*`) 出现 `/ws/user` = P0 (paper 不接私有 HMAC channel)

**WHY:** 老李 v3 §C / 老李 backend-requirements §6 + R-33 流程红线. v3 R-33 复盘已明示, v2 漏第 5 host = 维度 1+4 漏扫, 是 W3 整周才补的事故. v1.1 CI 永久 enforce 不再靠记性.

### §2.6 ADR-005 派单 grep (PR description "spec by 主管")

**Grep:** PR body 必含一行 `spec by <主管/协调人/GM/CPO>` 或 `self-spec (F 顾问)`.

**WHY:** CLAUDE.md §7.8 第 5 题 (GM 自检) + §7.9 不绕主管派单. ADR-005 W4 末立 ("5 战斗单元 IC 任务必经主管"). GM 错累计 8 次, 越主管派 IC = 错 #8, CLAUDE.md 第 5 题硬 enforce 但还无机器查. v1.1 CI 兜.

**允许的 spec 源 (CI 接受):** 老周 / 老韩 / 小梁 / 小余 / 老胡 / 老郭 / 老雷 / 老钱 / self-spec (F 顾问).

**严格度:** body 文本任一处含上述任一短语即过, 不要求位置. 例外补丁: dependabot PR / .github/ 元 PR / 纯文档 PR 可填 self-spec.

### §2.7 persona 边界 grep (不写代码 persona spec-only 声明)

**Grep:** PR body 含 "spec by 小程 / 老钱 / 小杜 / 小苏" 时, 必带 "spec only, 不写代码" 声明.

**WHY:** CLAUDE.md §7.4 (不耻下问) + §10 (sub-agent 拒接边界). 4 个 persona 明确不写代码 (小程 #19 strategy researcher / 老钱 #15 CPO / 小杜 #28 PM coord / 小苏 #29 UX content). 若 PR 描述写 spec by 小程, 但同时 PR diff 含 cpp 改动, 那要么是越界 (小程不该写代码), 要么是错挂名 (代码 spec 应 by 老周/老韩等主管). CI 强制声明清楚是哪种.

### §2.8 clang-tidy 4 cpp 反模式 (走 grep 兜底)

clang-tidy 这层主要补充 cpp idiom 检查, 大头反模式依然在 grep 层. v1.1 显式启用:

| Check | 防什么 | 关联 |
|---|---|---|
| `readability-identifier-naming.MemberCase = lower_case` | 防 `paramType` (HMAC bug 2 camelCase 反模式) | HMAC §A R2 |
| `bugprone-easily-swappable-parameters` | 防 `sigType` int 参数误传 1/2 (HMAC bug 3) | HMAC §A R3 |
| `cppcoreguidelines-init-variables` | 防 `int64_t event_ts_ns;` 未初始化 (R-20 时间字段) | R-20 §6 |
| `readability-identifier-naming.GlobalConstantCase = UPPER_CASE` | host URL constexpr 必须全大写 (防 `const auto k_wss_clob = "..."` 与第 5 host 混) | R-33 |
| `bugprone-suspicious-string-compare` | 防 HMAC sig 用 strcmp 校验 (应用 timing-safe compare) | 老沈 安全 |

cpp 反模式 (HMAC 4 / R-12 / R-20 / R-11) 共 4 类, clang-tidy 检 idiom 层, grep 检字面量层, 双层兜底.

---

## §3 PR template 升级 (§0 派单链)

新增 §0 节, 必填项 2 个:

```
spec by: <主管/协调人/GM/CPO/self-spec (F 顾问)>
走主管层 ack: <yes / no / N/A>
```

**CI 行为:**
1. `pr-meta-grep` job 扫 PR body, 找 `spec by (老周|老韩|小梁|小余|老胡|老郭|老雷|老钱)` 或 `self-spec (F 顾问)`. 都不命中 → reject.
2. 找到 spec by 小程/老钱/小杜/小苏 → 检 "spec only, 不写代码" 声明, 缺 → reject.

**写法示范:**
- 工程 PR (5 战斗单元 IC): `spec by 老周 (A 单元 W5-A-03 派单 PMClient v0.2)` + `走主管层 ack: yes`
- F 顾问 self PR: `spec by 老郭 (F 协调人 W5-F-01 forward 老高 PR review v1.1)` + `走主管层 ack: N/A (F 顾问 self-spec)`
- GM 例外 PR: `spec by 老雷 (紧急 P0 hotfix / 跨单元统筹, ADR-005 §3.2 例外)` + `走主管层 ack: N/A`
- CPO 产品 PR: `spec by 老钱 (CPO 产品方向决策, GM 平级)` + `走主管层 ack: N/A`

---

## §4 误报率预估 + 处置 (false positive → 老郭仲裁)

| Grep | 预估误报率 | 典型 FP 场景 | 处置 |
|---|---|---|---|
| R-20 第 7 项 (data_source_ts = now()) | < 1% | 单测 mock fixture 故意伪造 ts | 单测路径 `tests/unit/**` exclude (现已 exclude 在 path 过滤里, 双查) |
| R-20 第 8 项 (event_ts = ingestion_ts) | < 1% | 启动时 bootstrap fixture | 同上 |
| HMAC bug 1 (querystring 拼) | < 5% | URL builder 单测拼字符串 | 单测路径 exclude + 注释 `// HMAC_BASE_STRING_TEST` 豁免 |
| HMAC bug 2/3/4 | < 1% | 几乎不可能, 字面量精确 | 真 FP → 提单老郭 |
| R-11 v1.1 (paper write 真账本) | 2-5% | 测试 fixture 跑通 paper E2E 时用 mock WAL 路径 | mock path 不叫 `position.wal`, 用 `paper_position_mock.wal` |
| R-12 WSS 4 项 | 5-10% | 单测路径 mock WSS handler 用 lock_guard 测 thread safety | 单测路径 exclude + 注释 `// SPIN_OK / TEST_ONLY` 豁免 |
| R-33 旧 host | < 1% | 文档 migration 例 (但 docs/ 已 exclude) | exclude 已覆盖 |
| ADR-005 派单 | 5% | dependabot / forge 提的元 PR 没 spec by | 加白名单 (dependabot 走 `self-spec (bot)`) |
| persona 边界 | < 2% | 真有 PM coord 在描述里说 "spec by 老胡, 小杜 review feedback" 这种二级提及 | grep 改成必须以 "spec by 小程/老钱/..." 开头才触发 (现在的 grep 就是 `spec by $p` 词组) |

**W5 EOW smoke test 已发现 1 例 FP (W3 遗留, 非 v1.1 引入):** `sports-tail-trader` 老项目红线 grep 在 `docs/SPRINTS/sprint-02-w3-progress.md` + `docs/SPRINTS/sprint-02.md` 命中 (sprint 复盘合法引用). v1.1 加 exclude 路径 `docs/MEETINGS/` + `docs/SPRINTS/` + `docs/RESEARCH/laogao-pr-review-*` (本 doc 也引用), 修正完毕. 现 main tree 全 grep 0 命中, smoke 通过.

**处置流程 (与老郭一致):**
1. CI 报红 → PR 作者先看是否真违规
2. 真 FP → 在 PR 加 `// CI-EXEMPT: <理由>` 注释 + 提单 `@老郭` 仲裁 + 抄 `@老高`
3. 老郭 24h 内裁决 (合理坚持 → enforce, 越界确认 FP → 老高升级 grep 加豁免规则, 走 PR 改 `.github/workflows/pr.yml`)
4. 改 grep 规则的 PR 必经老高 + 老郭 + 老雷 三方任一签字 (CI workflow 本身的 PR meta enforce)

**总体目标误报率 (W6 enforce 后 4 周):** 全 grep 加权 ≤ 3%. 超阈 → 升级豁免机制 / 收紧字面量.

---

## §5 W6 起 enforce 节奏 (待 GM ack)

- **W5 EOW (本周末):** 老郭 first review 本 v1.1 doc + pr.yml + clang-tidy + PR template. 我接修订意见, 出 v1.1.1 if 需要.
- **W6 Mon (老雷主管周同步):** 老郭上呈 GM, GM ack 进 CLAUDE.md §10 "PR 提交必经老高 v1.1 review" (可选, 待老郭 forward).
- **W6 Tue 起:** v1.1 enforce, 老 PR (W5 之前未合) 不溯及, 新 PR 必过. 头 2 周老高每日 0:00 UTC 扫 FP, 提 v1.1.x patch.
- **W8 EOW:** 第一次 v1.1 retrospective — 误报率统计 / 新增 / 删除 grep 规则 / clang-tidy 调整. 走老郭仲裁.
- **W12 / 月末:** v1.2 升级 (M2 milestone). 候选: R-1 (RM bypass) cpp 反模式 / R-7 (回测实盘不同处理) data lineage grep / replay 单测路径 grep.

---

## §6 CLAUDE.md §10 升级建议 (老郭 forward GM)

**建议在 §10 给 sub-agent 操作约定加一条 (位置: §10 末尾):**

> **PR 提交必经老高 v1.1 review (W6 起 enforce)**:
> - 每个 PR 必带 §0 "spec by <主管>" 派单链声明 (ADR-005)
> - CI `pr-meta-grep` 自动检查派单声明 + persona 边界
> - CI `redline-grep` 自动检查 R-20 (8) / R-11 (3) / R-12 (4) / R-33 (2) / HMAC 4 / 老项目
> - 老高人脑 review 看命名 / WHY 注释 / 红线 §9 / 性能反模式
> - FP 申诉路径: PR 加注释 + 提单 @老郭 + 24h 仲裁

**走法:** 老郭 W5 EOW 同步上呈, GM 拍板进 CLAUDE.md, 我不直接改 (老郭 first review, GM ack 才入主线).

---

## §7 我交付清单 (W5 EOW)

| 项 | 文件 | 状态 |
|---|---|---|
| v1.1 doc | `docs/RESEARCH/laogao-pr-review-v1.1.md` (本文) | DRAFT, 待老郭 review |
| CI grep 升级 | `.github/workflows/pr.yml` (2 job + 20 grep) | smoke test 0 误报 |
| clang-tidy 升级 | `.clang-tidy` (+3 显式 check + 4 cpp 反模式注释) | YAML 合法 |
| PR template 升级 | `.github/PULL_REQUEST_TEMPLATE.md` (+§0 派单链) | 待老郭 review |
| CLAUDE.md §10 草拟 | 本文 §6, 待老郭 forward GM | 不直接改 (越权) |

---

## §8 老郭签字位

- [ ] 老郭 (F 协调人) first review v1.1 doc
- [ ] 老郭 forward GM (CLAUDE.md §10 升级, 含派单 grep enforce)
- [ ] GM 老雷 ack → W6 enforce
- [ ] 与老何 v1.1 footgun checklist cross-check (W5 EOW, footgun 接 R-12/R-20/R-11 cpp 反模式)

---

— 老高, 2026-05-28 (Sprint-2 W5 Wave 24, ADR-005 派单 from 老郭 §4 W5-F-01)
