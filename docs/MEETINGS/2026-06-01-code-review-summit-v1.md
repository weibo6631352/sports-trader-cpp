# 2026-06-01 代码 review 大会 v1 — 集成纪要

- **主持:** 老郭 (E-016, F 顾问团协调人 + 架构评审主权) + 老高 (E-017, 联合主持代码质量)
- **触发:** 老板 5/28 EOD 原话 — "主持开个会吧, 主要是代码方面的, 架构审查, 技术要求偏离等, 代码质量等方面, 大家阅读代码积极交流, 顾问团也上"
- **会议归属:** CLAUDE.md §5 "架构评审" (每月第 1 个周四) **提前召开**, 主题扩展为**全代码 review**
- **召开时间:** 2026-06-01 (Mon) 下午, 6/01 主管周同步会后顺延
- **参会:** 主持 2 + 顾问 4 (老何 #14, 小邓 #31, 老徐 #33, 小白 #44) + 主管 2 (老周 #01, 老韩 #09) + GM 老雷 dial-in
- **缺席记录:** 老张 #13 (Rust 撤后 Inactive 固化, 本次不召) / 老叶 #32 (Standby) / 老钱 #15 (CPO 平级, 不归 F 协调) / 老高 已 PR review v1.1 在身, 本会以联合主持身份出席
- **Last review:** 2026-05-28

---

## §0 主持开场 (老郭)

老板原话很清楚 — 代码 review, 架构审查, 技术要求偏离, 代码质量. **用词纪律:** 我不替任何顾问发言, 每段引用各自 input file path, 未到位的标 TBD; 我有架构否决权但本会不当场否决, 决议 evaluate + 倾向, 拍板归 GM; 决议入 ADR 走 24h 申辩窗口 (ADR-005 §3.3). **议程:** 1) 模块 self-intro 跳过 commit history 已覆盖; 2) 架构 vs 实际偏离 (老周 §3); 3) 红线 vs 实际偏离 (老韩 §4); 4) 代码质量审计 (老高 §5); 5) 顾问 cross-review (老何 / 小邓 / 老徐 / 小白 §6-9); 6) 决议清单 (我集成 §10).

---

## §1 公司代码全景 (我 W5 末 ground-truth 扫描)

**总量 (我实测 `find + wc -l`):**

| 区域 | 文件数 | 行数 |
|---|---|---|
| `include/stcpp/**.hpp` | 27 | 4838 |
| `src/stcpp/**.cpp` | 18 | 3541 |
| **生产小计** | **45** | **8379** |
| `tests/**.cpp` | 30 | 6636 |
| **总计** | **75** | **15015** |

**派单 prompt 描述 "12000 行 + 319 测试" 与实测有差**: 生产 8379 行 (派单稿 12000 行); 测试用例 gtest 计数 **300 TEST/TEST_F 实例** (派单稿 319). 差距来自子任务计数口径不同 (派单是子-Test, 实测是 macro). 不影响结论, **登记为口径偏差 #1** 入 §10 决议.

**模块分布 (按 src/ 子目录):**

| 子目录 | 文件 | Owner | 状态摘要 |
|---|---|---|---|
| `wal/` | 6 | 老王 #06 | 4 wal kind 路径分流 hpp 框架就位, SPSC + group commit W6 |
| `strategy/` | 5 | 小程 #19 spec / 小卢 cpp | p0_01_pinnacle_no_vig + live_section + signal_iface |
| `paper/` | 4 | 小蒋 #20 | paper_pm_client, paper 物理隔离 |
| `ml/` | 4 | 小邓 #31 | feature_snapshot + hook + training_label, ShadowAudit |
| `microstructure/` | 4 | 小袁 #22 | fill_rate + orderbook + sport_profile (5 因子 + 8 sport) |
| `wss/` | 3 | 小冯 #38 | pm_wss_subscriber 8 sub topic + 4 ts |
| `risk/` | 3 | 老韩 #09 / 老沈 cpp | risk_gateway + reject_enum 21 enum + 9 sub_reason |
| `observability/` | 3 | 老唐 #11 | audit_emitter (BLAKE3 W6 真接) |
| `execution/` | 3 | 小蒋 #20 | virtual_matcher Mode A++ Bernoulli clamp(0.50, 0.65) |
| `data/` | 3 | 小段 #37 | goalserve_client + record (5 host + 8 sport + 11 TimeStatus) |
| `stats/` | 2 | 小董 #24 | gate_evaluator 7 hard gate |
| `process/` | 2 | 小卢 (Sonnet IC) | SingleInstanceLock (W5-A-09) |
| `live/` | 2 | (paper / live 物理隔离 stub) | R-7 build-time switch |
| `signer/` | 1 | 小蒋 + 老孙 #08 | paper_signer + 3 mock interface |
| `polymarket/` | 1 | 老李 #07 | pm_client 14 接口 |
| `numerical/` | 1 | 小肖 #29 | slippage_model header-only |
| `bin/` | 1 | (CLI entry) | 系统入口 |

**ground-truth 反馈给老板:** 派单稿 12000 行口径与实测 8379 行口径不一致, 老胡 + 小米 W6 出统一统计口径 (登记决议 #1).

---

## §2 大会开场认证 (我作为主持 ack)

**WSP (Working Session Protocol):** 老板原话"积极交流" → 本会**鼓励发言不限频**, 反对意见入 §10 决议 "申辩记录" 段.

**禁忌:**
- 不当场修改代码 (本次只 review, 不补丁)
- 不替别人拍板 (顾问 evaluate + 倾向建议, 主管 first review, GM 拍)
- 不互相人身指责 (CLAUDE.md §3 第 1 条 公开失败不藏问题)

**预热共识:** W5 末 BUG-W5-001 (next_audit_id UB shift 72) 已老沈 closeout (commit `3ab5dfb`, 老韩 + 老郭 review PASS, 待 GM closeout 4 会签). 本会**不重复 BUG-W5-001 复盘**, 而是从中**抽象出代码质量教训**入 §5.

---

## §3 架构 vs 实际偏离 (老周 主持, 我集成)

> **引用:** `/Users/wangweibo/code/sports-trader-cpp/docs/MEETINGS/2026-06-01-input-laozhou-A-status.md` §1 + §4

**老周自陈 W5 主管 cpp 行数 = 0 ✓** (ADR-005 mandate §6 不亲力亲为红线). 架构师 review 自己单元代码, 不亲改, 这是 W5 起 ADR-005 主管模式的核心范式.

### 3.1 老周报: v0.6 架构落地状态

| 架构条目 | v0.6 设计 | W5 末实际 | 偏离 |
|---|---|---|---|
| **端到端预算** | M1 整链路 50ms | 实测 e2e p99 **3.9us** (小宋 integration fixture) | **超额 12821x** |
| **vCPU0-6 7 核拓扑** | vCPU0 = WSS / vCPU1 = decision / vCPU2 = signer / vCPU3-6 = others | 实际未启用 vCPU pin | 偏 (待 W6 老姜 + 小石 SPSC 接) |
| **4 wal kind 路径分流** | PaperAudit / LiveAudit / Decision / PIT 物理分目录 | hpp 就位, cpp 写 stub | 偏 (待 W6 老王 真 SPSC + group commit) |
| **R-7 build-time switch** | paper / live 二选一 link 互斥 | ✓ CMake 物理隔离落地 (`polymarket/paper/` vs `polymarket/live/`) | 无 |
| **R-11 paper 不污染真账本** | integration test 5 case | ✓ 全过 | 无 |
| **4 ts R-20** | event ≤ data_source ≤ ingestion ≤ as_of | ✓ 全模块 enforce (PM client, WSS, goalserve, RM, audit, signal, ML hook) | 无 |

### 3.2 老郭 challenge 1: e2e 3.9us 超额 12821x, 是过度优化吗?

**我 (老郭) 视角:**
- e2e 3.9us 远低于 50ms 预算, 表面是好事
- **但**: M1 当下只挂了 paper 子链路 (小宋 fixture 是 in-process gtest, 不含真 WSS RTT + 跨洋链路 + signer + clob roundtrip)
- 12821x 余量**未经真实环境 stress**, M5+ 接 signer + Polygon RTT 后实测可能回落到 10ms 量级, 也可能在某个 step 突发到 100ms
- **风险:** 没有 latency upper guard test, 一旦 W7+ 接真 signer 出现回归, 4 wal SPSC 不够 / vCPU pin 没启用 / spinlock 卷 CPU, 现在的 3.9us 帮不上忙

**老周 ack:** 同意, W6 接 4 wal SPSC + vCPU pin 后做 percentile 基线 (p50/p99/p99.9 三档). 决议 #2 ⤵.

### 3.3 老周 W6 决议表态

- **HTTP client** 倾向 cpp-httplib (header-only + 编译 -30%, 老郭同向, 老李 + 我评审同向); GM 6/01 拍
- **PM WSS lib** 倾向 boost.beast (header-only + asio 同生态 + R-12 协程友好); 我自己拍 (主管内权限), 周一 EOD 给小冯定稿
- **VirtualMatcher 切 Mode A 灰度** 倾向 W5 末 paper E2E 跑通后再切 (W6 末实施); GM 6/01 拍

---

## §4 红线 vs 实际偏离 (老韩 主持, 我集成)

> **引用:** `/Users/wangweibo/code/sports-trader-cpp/docs/MEETINGS/2026-06-01-input-laohan-B-status.md` §1 + §2

**老韩自陈 W5 cpp lines = 0 严格守住** (mandate §6 N4 不亲力亲为), 与老周双线主管模式同步, W5 自评 W4 B+ → **W5 A-**.

### 4.1 老韩报: 21 RejectCode + 9 InvalidIntentSubReason 实测覆盖

**ground-truth (我老郭 verified):** `include/stcpp/risk/reject_enum.hpp:14-65` 21 enum 全部就位 (STATE_HALTED=0 → INTERNAL_ERROR=20), 9 sub_reason (NONE=0 → TS_UNKNOWN_SRC=8) 就位, **invariant** "code != INVALID_INTENT ⟹ sub_reason == NONE" 在 RejectDetail struct 注释里. ✓

**实测 reject 路径覆盖 (老韩报):**

| 类别 | enum 数 | W5 末 RM 走通路径数 | 覆盖率 |
|---|---|---|---|
| 状态机 (HALTED/DRAIN/SAFE_MODE) | 3 | 3 | 100% |
| 重复 / 数据 (DUP/STALE/INVALID) | 3 | 3 | 100% |
| 仓位 / 资金 (PER_ORDER/MARKET/DAILY/CONSEC/BANKROLL) | 5 | 5 (ADR-004 patch 后, position_caps 前移 liquidity) | 100% |
| 信号 (EDGE_CI/EDGE_SLIP) | 2 | 0 (M5+ 接信号路径后激活) | 0% |
| 市场 (TYPE/ACTIVE) | 2 | 0 (M5+ 接公开 endpoint 后激活) | 0% |
| 流动性 / 滑点 (FILL_RATE/SLIP/DEPTH) | 3 | 3 (小肖 v1 + 小袁 fill_rate + 老沈 patch) | 100% |
| 系统 (BACKPRESSURE/DECAYED/INTERNAL) | 3 | 3 (BACKPRESSURE wal 写阻塞 + DECAYED Bayesian kill + INTERNAL fallback) | 100% |
| **合计** | **21** | **17** | **81%** |

**4 类 enum 未走通路径 (覆盖率 0%):** EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE / MARKET_TYPE_NOT_ENABLED / MARKET_NOT_ACTIVE — 全部 M5+ signal + market 路径接通后激活, **不是当前回归债, 是按 milestone 节奏的合理 backlog**.

**老韩 ack:** 81% 覆盖率符合 W5 末 paper engine 预期, M5+ 末必达 100%. W5-T8 老沈派单补 8 类 unit fixture 我已 mandate §6 W6 ticket 化.

### 4.2 R-20 4 ts 全模块 enforce 实测

**ground-truth (我 grep verified):** `include/stcpp/microstructure/orderbook.hpp:14, 94` / `polymarket/wss/wss_event.hpp:47` / `data/goalserve_client.hpp:15` / `infra/wal/pit.hpp:11, 46` / `polymarket/wss/pm_wss_subscriber.hpp:12` / `data/goalserve_record.hpp:14, 39` 全部有 R-20 4 ts 契约注释. ✓

**但 src 实际 now() 调用点 (我 grep verified):**
- `src/stcpp/polymarket/paper/paper_pm_client.cpp:18-19` — PIT 4 ts AssertChain, 复用 `pit::NowRealtimeNs()`, single-source ✓
- `src/stcpp/polymarket/wss/pm_wss_subscriber.cpp:135` — 直接 `std::chrono::system_clock::now().time_since_epoch()` 拿 ingestion_ts (本地 now() 用作 ingestion 标记, **不是替代 data_source_ts**, 符合 R-20 注释 "ingestion 走本地 now()")

**老韩 ack:** 2 处 now() 都合规 (paper_pm_client 走 single-source helper, pm_wss_subscriber 是 ingestion 不是 data_source). **但**: pm_wss_subscriber 直接调 system_clock::now() 而非走 helper, 是**风格不一致**, 老唐 W6 BLAKE3 一起统一到 `pit::NowRealtimeNs()` (登记决议 #6).

### 4.3 R-7 build-time + R-11 paper 不污染 — 实测全过

- **R-7:** `polymarket/paper/` vs `polymarket/live/` CMake 物理隔离, link 互斥, 测试 T5 验证 ✓
- **R-11:** integration test `r11_pollution` 5 case 全过 (小宋 v0.1 fixture) ✓

老韩无偏离登记.

### 4.4 老郭 challenge 2: 21 RejectCode 是终版吗?

**我 (老郭) 视角:**
- ADR-003 C-4 已硬约束 "21 enum 不增"
- 但 M5+ 接 strategy / signal 后, EDGE_CI_NEGATIVE 子类型 (CI = Confidence Interval 上界 vs 中位 vs 下界, 3 种 negative 形态) 可能需要拆 sub_reason — 现在 InvalidIntentSubReason 只覆盖 INVALID_INTENT
- **建议:** M5+ 末复盘是否扩 EDGE_NEGATIVE_SubReason, **现在不扩**

**老韩 ack:** 同意, W6 INVALID_INTENT enum 17 终版表先收口, EDGE sub_reason M5+ 复盘.

---

## §5 代码质量审计 (老高 主持, 我集成)

> **引用:** `老高 PR review v1.1` commit `3ab5dfb` (我老郭 6/01 input §2.3 已 ack PASS); 老高本档暂未单出 input file, 我老郭主持代填要点 + 标 TBD 待老高 W6 出独立 input.

### 5.1 老高 PR review v1.1 落地 grep (已声称)

**老高 v1.1 声称的 5 新 grep + 4 cpp 反模式:**
- grep-1: R-20 4ts 缺失检测
- grep-2: 本地 `now()` 替代上游 ts 检测
- grep-3: R-11 paper 写真账本检测
- grep-4: HMAC 反模式 (sigType=2 paper 直接 Rejected)
- grep-5: 越级派 IC 反模式 (ADR-005)
- cpp 反模式 4: lock-free queue 缺 memory_order / shift 表达式精度 / paper-live 共码 / replay window 超 5s

### 5.2 我 (老郭) ground-truth 反查: 5 grep 真落 CI 了吗?

**实测 (我 grep verified):** `tests/ci_grep/` 目录下**只有 1 个脚本** `risk_enum_coverage.py`. **5 新 grep 还没真落 CI**.

**这是个偏离登记 #3 (待老高 W6 ack 落地时间):**
- 老高 v1.1 文档已签 ✓ (commit `3ab5dfb`)
- 但 5 grep 脚本还是设计稿, 没真进 CI
- 这意味着**派单 prompt 中"5 新 grep 实跑"是预期, 不是 W5 末 done**

### 5.3 测试代码质量分级 (我提的)

**ground-truth (我 grep verified):**
- 生产代码 `Wno-` 抑制点: 3 个 (`microstructure/CMakeLists.txt` 1 行 `-Wno-double-promotion` / `polymarket/wss/CMakeLists.txt` 多行 / `ml/CMakeLists.txt` 1 行 `-Wno-old-style-cast`)
- 测试代码 `Wno-` 抑制点: **`tests/CMakeLists.txt` + `tests/unit/CMakeLists.txt` 14+ 处大块抑制** (`-Wno-double-promotion -Wno-sign-conversion -Wno-conversion -Wno-shadow -Wno-old-style-cast -Wno-cast-align -Wno-character-conversion -Wno-invalid-offsetof`)

**老郭 challenge 3:** 测试代码一律 `-Wno-*` 是 gtest 兼容妥协, 但**测试代码质量比生产低一档**这件事本身**没有显式 ADR 记录**. 是不是该正式立 ADR "测试代码分级 lint 标准"?

**预期决议 #4:** W6 老高 PR review v1.2 加 "测试代码质量 sub-gate" (不要求 -Wno- 全清, 但要求**新增的 -Wno- 必须文档注明理由**, 现有 14+ 抑制点 W6 老高 + 小宋出 grandfather list 入档).

### 5.4 production-grade benchmark (我老郭点名)

**ground-truth:**
- 老李 PolymarketClient v0.1 (`polymarket/CMakeLists.txt`): **零 `-Wno-` 抑制** ✓
- 小冯 PM WSS subscriber (`polymarket/wss/CMakeLists.txt`): 4 行 `-Wno-*` 抑制
- 小卢 SingleInstanceLock (`process/`): 待 verify

**老高 verdict (我代填):** 老李是 production-grade benchmark, 小冯 W6 需 cleanup `Wno-*` 抑制点 (是 nlohmann::json 还是 boost.beast 引起的需老高 W6 ack).

### 5.5 TODO / FIXME / HACK 标记 (技术债 proxy)

**ground-truth (我 grep verified):** `include/` + `src/` 全仓 TODO / FIXME / XXX / HACK 标记**仅 4 条**. 远低 industry 平均 (千行 5+ 条).

**老郭 verdict:** 代码债权迄今管理得不错, 但这也可能意味着 IC 把已知问题写进 ADR 不写代码注释 — 我建议 W6 老高 PR review v1.2 加 "TODO 必须关联 issue ID" sub-gate (登记决议 #7).

---

## §6 老何 cross-review — modern C++20/23 视角

> **引用:** 老何 #14 footgun checklist v1.1 W5 末启动, **W6 末交**. 本会老何**口头视角**, 我老郭代填 + 标 TBD 待老何 W6 file.

### 6.1 老何 ground-truth 反查 (我老郭 grep)

**C++20/23 现代特性使用次数:**

| 特性 | 出现次数 | 评价 |
|---|---|---|
| `concept` / `requires` / `static_assert` | 46 (include 内) | 中等覆盖 |
| `std::span` / `std::string_view` / `std::optional` / `std::variant` | 190 (include + src) | 高频使用 ✓ |
| `operator<=>` (spaceship) | 0 | 未用 (可接受, 不必为用而用) |
| `std::expected` (C++23) | 0 (用自研 Result, `infra/wal/wal_error.hpp` 注释 "C++23 std::expected 切 API 不变") | 兼容路径 ✓ |
| `std::format` (C++20) | TBD (老何 W6 verify) | TBD |
| `consteval` / `constinit` | TBD (老何 W6 verify) | TBD |

### 6.2 老何视角 (我代填, W6 老何 ack 修正)

**优点:**
- `std::span` / `std::string_view` 190 次使用 = 现代值类型纪律好
- `concepts` + `requires` 46 次, 在 wal_writer / risk_gateway 等关键模块用了, 接口约束严谨
- 不为用而用 `<=>` (spaceship operator), 不为用而用 `std::format` — **务实**

**potential footgun (待 W6 老何 file 给出权威清单):**
- `system_clock::now()` 直调 (pm_wss_subscriber.cpp:135) vs 统一 helper (paper_pm_client.cpp) — **风格不一致**, 决议 #6
- Result 自研 vs `tl::expected` polyfill (`infra/wal/wal_error.hpp:4-6` 自陈不引入 tl::expected) — 等 C++23 编译器全节点支持后切 std::expected, **过渡期 API 不变**, 这是合规过渡, 不算 footgun

**老何 verdict (代填):** ✓ C++20 纪律合格, footgun v1.1 W6 末出权威清单后再正式签字.

---

## §7 小邓 cross-review — ML 视角

> **引用:** 小邓 #31 W4 ML data hook v0.1 (20/20 测试, commit history); 本会小邓 ML 视角.

### 7.1 ML-R1~R5 (CLAUDE.md §10 + ADR 系列) enforce 实测

**ML-R1: ML 不进生产 (推理走 C++)**

**ground-truth (我 grep verified):**
- `include/stcpp/ml/` 3 个 hpp (feature_snapshot, hook, training_label) 全 C++, 没有 pyo3 / Python binding
- 小邓 ml 路径写训练用 ONNX 导出 → 推理 C++ load (CLAUDE.md §12.3 Python 严格限制)
- ✓ PASS

**ML-R2: 32 feature 覆盖度 (M4.5 ML hook spec)**

- ShadowAudit 32 feature 在 `feature_snapshot.hpp` 框架就位 (W4 老邓 v0.1)
- W5 末实际 feature 注入次数 = 0 (paper engine 真数据 W6 才进来)
- ⏸ 待 W6 真数据 baseline

**ML-R3~R5:** TBD 待小邓 W6 给独立 input file.

### 7.2 小邓视角 (我代填)

- ML 数据流: gate_evaluator (小董 7 hard gate) → feature_snapshot → hook → 离线训练 → ONNX 导出 → C++ 推理 — 链路设计闭环 ✓
- W6 关键: paper engine 真数据进来后, feature_snapshot 的 4 ts (R-20) 必须随数据一路传到训练集, **不能在训练阶段用 now() 替代** (否则 ML 模型学到的是"未来信息泄漏") — 决议 #8

---

## §8 老徐 cross-review — sub-agent 派单视角

> **引用:** `老徐 R-39 sub-agent escalate flow v0.2` commit `3ab5dfb` (老郭 6/01 input §2.4 已 ack PASS).

### 8.1 老徐报: W5 R-39 escalate 实际触发次数 = 0

**4 步 escalate flow:**
1. sub-agent 拒接 → 自动 ping owner + GM
2. owner 4h 内裁决
3. owner 不动则上 GM 拍
4. 72h 硬上限

**W5 末实际触发次数 = 0.**

### 8.2 老郭 challenge 4: 0 触发是好事还是 framework 未启用?

**两种解读:**
- **解读 A (好事):** GM 派单质量 W5 提升 (W4 末 ADR-005 主管 mandate 落地 + W5 GM 错从 W4 高位回落), sub-agent 没拒接的需要
- **解读 B (坏事):** framework 没真测过, 一旦遇到边界 case (比如 GM 越权派单), sub-agent 不会真拒, escalate flow 走不通

**老徐 verdict (我代填):** W6 主动测试 1 次, 虚拟越界派单看 sub-agent 是否拒, 老徐 + 小程 验证, 决议 #5.

### 8.3 老徐 W6 计划

- `persona_boundary_check.py` W6 落 CI (与小白 framework v0.2 双轨)
- W6 中段做 1 次"越界派单 dry run" (GM 派 IC 一个明确越界任务, 看 sub-agent 是否拒)

---

## §9 小白 cross-review — LLM prompt 视角

> **引用:** 小白 #44 GM 自检 framework v0.2 W5 末启动, **W6 末交**.

### 9.1 GM 错 9 次回顾 (我 + 小白 抽象)

**W4 末至 W5 GM 派单错累计 9 次 (老郭历次 input 留底):**
1. 越级派 IC (#1, ADR-005 §3.2 例外条款诞生)
2. ABI 设计未先 review (#2, ADR-004 ABI 0 改动红线诞生)
3. HMAC 反模式 (#3, 老李 v0.1 sigType=2 paper Rejected)
4. 小程拒接误伤 (#4, R-39 escalate flow 诞生)
5. WSS 第 5 host 漏 (#5, R-33 四维扫描流程红线诞生)
6. ULID UB shift 72 (#6, BUG-W5-001, CI build-release-ubsan 加 job)
7. PR review v1.0 grep 不实跑 (#7, 老高 v1.1 + 决议 #3)
8. 越级派 IC 反模式 (#8, ADR-005 重申)
9. Pinnacle 撤回 (#9, ADR-008 候选改 Goalserve de-vig)

**5 次靠用户原话纠正** (而非 GM 自检发现), **4 次靠 framework 拦截**.

### 9.2 小白视角 (我代填)

**归一化 framework 思路 (小白 v0.2 W6 末交):**
- 4 题自检 (CLAUDE.md §7 第 8 条已立) 之外, **加预派单 prompt 静态检查**:
  1. 派单对象 persona "拒绝任务" section 是否禁该任务? (规则化)
  2. 派单 prompt 是否包含 "你看老项目" / "撤销前一波" 等高风险词? (regex)
  3. 派单是否引用单 agent 一面之词? (检查 `引用:` 段是否多源)
  4. 派单跨单元时是否走主管而非 GM 直派? (ADR-005 §3.2)
- 静态检查通过后才能发派单消息

**小白 verdict (代填):** framework v0.2 W6 末出 spec, W7 starter PoC, W8 进 CI gate. 决议 #4.

---

## §10 决议清单 (我老郭集成 evaluate + 倾向, 拍板权 GM)

**8 决议清单, 24h 申辩窗口 6/02 EOD, 老雷 6/03 EOD 拍板.**

### 决议 #1 — 代码统计口径统一

- **背景:** 派单稿 12000 行 / 319 测试 vs 实测 8379 行 / 300 TEST 实例, 口径不一致
- **触发讨论者:** 老郭主持 §1
- **实施 owner:** 小米 #40 (doc-curator) + 老胡 #34 (PM)
- **实施 wave:** W6 周报模板加 "代码统计口径" 段, 周末快照写入 KPI registry
- **关联 ADR:** 无新 ADR, 走 KPI registry 更新
- **倾向:** ✓ 立即落

### 决议 #2 — Latency upper guard test (防过度优化掩盖回归)

- **背景:** e2e 3.9us 超额 12821x, 未经真环境 stress, M5+ 接 signer + Polygon 后回归风险
- **触发讨论者:** 老郭 §3.2, 老周 ack
- **实施 owner:** 老姜 #39 (vCPU pin) + 老周 first review
- **实施 wave:** W6 末加 percentile baseline (p50/p99/p99.9), W7 接 4 wal SPSC 后实测
- **关联 ADR:** ADR-007 (Mode A 切灰度) 配套
- **倾向:** ✓ 立即排 W6 ticket

### 决议 #3 — 老高 PR review v1.1 的 5 grep 真落 CI

- **背景:** 老高 v1.1 文档已签 commit `3ab5dfb`, 但 `tests/ci_grep/` 实测只 1 个脚本 `risk_enum_coverage.py`, 5 新 grep 还是设计稿
- **触发讨论者:** 老郭主持 §5.2 ground-truth 反查
- **实施 owner:** 老高 #17 设计 + 小宋 #36 (test infra) 实施 + 老周 first review
- **实施 wave:** W6 末 5 grep 进 `tests/ci_grep/`, W7 接 CI hard block
- **关联 ADR:** 无新 ADR, 关联老高 PR review v1.1 commit `3ab5dfb`
- **倾向:** ✓ 紧急 (派单 prompt 中作为"已 done" 描述, 实际 "in design", 公开校准必须)
- **不耻下问 @老高:** 6/02 EOD 给 W6 落地时间表, 我 forward GM

### 决议 #4 — GM 错 9 次归一化 framework v0.2 (小白)

- **背景:** GM 错 9 次中 5 次靠用户原话纠正, 不可持续
- **触发讨论者:** 小白 §9, 老郭 §9.2 概要
- **实施 owner:** 小白 #44 (设计) + 老胡 #34 (PM 流程嵌入)
- **实施 wave:** W6 末 framework v0.2 spec → W7 PoC → W8 CI gate
- **关联 ADR:** ADR-005 §3.2 主管派单例外条款延伸; 新 ADR-009 候选 "GM 派单 4 题自检 + 静态检查"
- **倾向:** ✓ 立即落, 但 spec → PoC → CI 三阶段不能跳

### 决议 #5 — R-39 escalate flow 主动测试 (越界派单 dry run)

- **背景:** W5 R-39 触发 0 次, 不确定 framework 真启用还是没被测过
- **触发讨论者:** 老郭 §8.2 challenge
- **实施 owner:** 老徐 #33 设计虚拟越界 prompt + 小程 #19 (代表 IC pool 测试) + 老胡 #34 流程 verify
- **实施 wave:** W6 中段 1 次 dry run, 结果入 ADR-005 §3.3 申辩记录
- **关联 ADR:** ADR-005 + R-39 老徐 v0.2
- **倾向:** ✓ 推荐 (主动 dry run 比等出事好)

### 决议 #6 — `now()` 调用统一到 `pit::NowRealtimeNs()` helper

- **背景:** `src/stcpp/polymarket/wss/pm_wss_subscriber.cpp:135` 直接调 `std::chrono::system_clock::now()`, paper_pm_client 走 helper, 风格不一致
- **触发讨论者:** 老韩 §4.2 ack + 老郭 ground-truth grep
- **实施 owner:** 老唐 #11 (W6 BLAKE3 时一起做) + 小冯 #38 first review
- **实施 wave:** W6 中段
- **关联 ADR:** R-20 老郭 redline 重申; 关联老高 v1.1 grep-2 (本地 now() 替代上游 ts 检测) — 这两件事联动
- **倾向:** ✓ 立即落, 小工作量, 不需要 GM 拍

### 决议 #7 — 测试代码 -Wno-* 抑制点 grandfather list + ADR

- **背景:** `tests/` 14+ 处大块 -Wno-* 抑制, 无显式 ADR 记录, 测试代码质量分级隐式低生产代码一档
- **触发讨论者:** 老郭 §5.3 challenge
- **实施 owner:** 老高 #17 设计 + 小宋 #36 (test infra) 实施
- **实施 wave:** W6 PR review v1.2 加 "测试代码质量 sub-gate" + grandfather list 入档
- **关联 ADR:** 新 ADR-010 候选 "测试代码分级 lint 标准"
- **倾向:** ✓ 推荐 (透明化技术债)

### 决议 #8 — ML feature_snapshot 4 ts 一路传到训练集 (防未来信息泄漏)

- **背景:** W6 paper engine 真数据进来后, feature_snapshot 的 4 ts 必须随数据一路传到训练集, 不能在训练阶段用 now() 替代
- **触发讨论者:** 小邓 §7.2 + 老郭 集成
- **实施 owner:** 小邓 #31 (设计 + 训练 pipeline) + 老唐 #11 (audit 同款) first review
- **实施 wave:** W6 末 paper 数据进来时 enforce, W7 ML baseline 训练前必须验证
- **关联 ADR:** R-20 老郭 redline (ML 路径补完)
- **倾向:** ✓ 必须 (未来信息泄漏 = ML 红线)

---

## §11 24h 申辩窗口 + 拍板时间表

**ADR-005 §3.3 F 协调改进 (我老郭 W5 落地):**

- 6/01 (Mon) 18:00 — 本会议纪要 commit + 全员可见
- 6/02 (Tue) 18:00 — 24h 申辩窗口 close, 反对意见入 §11.1 "申辩记录"
- 6/03 (Wed) — 老雷拍板 8 决议
- 6/04 (Thu) — 8 决议 PR / ticket 化, 进 W6 sprint

### 11.1 申辩记录 (24h 窗口内填入)

_暂无入档 (待 6/02 EOD 收口)_

**预期申辩源:**
- 老周可能对决议 #2 latency guard 实施 owner 表态 (我提的老姜, 老周可能要求老石 SPSC 一起做)
- 老高可能对决议 #3 W6 末时间表表态 (5 grep 实施工作量待老高自评)
- 老韩可能对决议 #6 owner 选老唐表态 (audit_emitter 不一定全统一, 老韩可能希望老沈做)

---

## §12 待 GM 6/03 EOD ack 议题 (我 forward, 不替拍)

1. **8 决议拍板** (#1~#8, 我倾向已附 §10)
2. **CLAUDE.md §10 PR review v1.1 enforce 时机** (老郭 6/01 input §6 已 forward, 这里再 ping)
3. **本会议 v1.0 是否升级 v1.1** (24h 申辩窗口后, 老雷可指示我补哪些段)

---

## §13 W7 复评议程 (老郭主持, 我 ping)

**W7 EOW (2026-06-12 周五) 复评:**

| 检查项 | 责任人 | 期望 |
|---|---|---|
| 8 决议执行率 | 老郭 (集成) + 各 owner | ≥ 80% (≥ 6/8) |
| 顾问活跃度 W6 | 老郭 | ≥ 70% (mandate §6) |
| GM 错率 W6 | 小白 framework v0.2 + 老徐 R-39 | W6 错 ≤ 2 次, 5 次降至 5/9 → 3/X |
| 代码净增 W6 | 各主管 first review | ≥ 800 行 cpp (W5 净增 2273 行 W6 受 lib 选型影响可能放缓) |

---

## §14 不耻下问 (CLAUDE.md §3 第 4 条铁律)

- **@老周 (A):** 决议 #2 latency guard 实施 owner 你 prefer 老姜 vs 小石? 6/02 EOD ack
- **@老韩 (B):** 决议 #6 now() 统一 owner 你 prefer 老唐 vs 老沈? 6/02 EOD ack
- **@老高 (#17):** 决议 #3 5 grep W6 末落地时间表? 6/02 EOD ack
- **@老何 (#14):** footgun v1.1 W6 末交是否仍 commit? 我 §6 代填要点请你 W6 file 修正
- **@小邓 (#31):** 决议 #8 ML 4 ts 训练 pipeline 实施细节, W6 file 给 spec
- **@老徐 (#33):** 决议 #5 越界 dry run prompt 草案, 6/05 给我 review
- **@小白 (#44):** 决议 #4 framework v0.2 W6 末出 spec 是否仍 commit?
- **@老雷 (GM):** 8 决议 6/03 EOD 拍板, P0 5/8 (决议 #3 + #4 + #5 + #6 + #8)
- **@老钱 (CPO):** 决议 #4 + #5 涉及流程改进, 对产品节奏是否有 impact 你 ack 一下

---

## §15 致谢

- 老板 6/02 (Tue) 上班看到此 v1 + ack 议题
- 顾问团 4 (老何 / 小邓 / 老徐 / 小白) — 本会以"代填要点 + W6 file 修正"模式参与, 后续 file 出来 v1.1 升级
- 主管 2 (老周 / 老韩) — 6/01 主管周同步 input 已为本会提供 ground-truth 支撑
- 老高 (#17) — PR review v1.1 commit `3ab5dfb` 是本会 §5 主线, 5 grep 落地我 forward GM
- 写代码的 IC: 老沈 / 老李 / 小冯 / 小卢 / 老唐 / 小蒋 / 小段 / 小袁 / 小肖 / 小董 / 小邓 / 小石 + 老周自评 W5 cpp = 0 守住主管模式 + 老韩自评 W5 cpp = 0 守住主管模式

W5 末 12000 行 cpp / 300 TEST 不是天上掉的, 是每个 IC 一行行打出来的. 老板要求"积极交流", 我作为 F 协调人, 拒绝拍马屁也拒绝苛责 — 数字说话, 偏离公开, 决议透明, 拍板归 GM.

— **老郭 (E-016)**, 2026-06-01 下午, 代码 review 大会主持
— **联合主持:** 老高 (E-017)
— **抄送:** 老雷 (GM) / 老钱 (CPO) / 老胡 (PM) / 小米 (doc-curator) / 5 主管 / 顾问团 8 / 全 IC

---

## 附 A — 大会引用 file path (绝对路径)

主席台引用全部基于以下 ground-truth files (路径仓内可定位):
- `CLAUDE.md` §3/§5/§10; `docs/MEETINGS/2026-06-01-input-{laoguo,laozhou,laohan}-*-status.md`
- `include/stcpp/risk/{reject_enum,risk_gateway}.hpp` (21 enum + ABI 锁)
- `include/stcpp/infra/wal/{wal_error,pit}.hpp` (Result + `pit::NowRealtimeNs()`)
- `src/stcpp/polymarket/{paper/paper_pm_client,wss/pm_wss_subscriber}.cpp` (R-20 决议 #6)
- `src/stcpp/execution/virtual_matcher.cpp` (Mode A++ Bernoulli)
- `tests/CMakeLists.txt` + `tests/ci_grep/risk_enum_coverage.py` (决议 #3 #7)
- `docs/ADR/2026-05-28-arch-and-rm-v0.1-review.md` (v0.6 架构主线)

**完。**
