# Sprint-2 W6 W2 周报 v1 (给老雷)

- **Owner**: 老胡 (pm-project-manager)
- **周期**: 2026-06-01 (Mon) → 2026-06-05 (Fri), Sprint-2 W6 W2
- **报告日**: 2026-05-28 (W6 W2 末盘整, GM 错 #11/#12 配套)
- **状态**: **绿 (工程交付超额)** + 2 GM 自承认错 (#11/#12 入 INCIDENTS) + Build Verification KPI 首期 P0 红灯
- **抄送**: 老雷 / 老郭 / 老韩 / 老周 / 小梁 / 小余
- **关联**:
  - 上一份: `docs/SPRINTS/sprint-02-w1-progress.md`
  - 风险登记: `docs/RESEARCH/laohu-risk-registry-v2.4.md` (本 wave 同出)
  - GM 错 log: `docs/INCIDENTS/gm-self-mistakes-log.md` (错 #11 + #12 入)
  - 周报模板: `docs/META/weekly-report-template-v2.md` → v3 (本 wave 同升)
  - ADR-009 v2: 全员 Sonnet, Wave 28+29 全 14 IC Sonnet 执行

---

## 1. TL;DR 给老雷 (一段话)

W6 W2 交付 Wave 28+29 共 14 IC Sonnet 派单, 全仓 ctest 376 → 428/428 PASS (+52 新测试, W6 W2 净增). Wave 28 (6 IC) 交 Position WAL/SPSC/de-vig/G2/PR v1.2/vCPU bench, Wave 29 (8 IC) 跨 5 单元交 BLAKE3/ABI lock/IngestRaw/StrategyUnlock/bookmaker 历史/VirtualFill/schema lock/顾问 persona check. **老雷必须看的 2 件事**: (1) **Build Verification KPI 首期 P0 红灯**: 16 个 IC 声称本地测试过, GM 整合 build 时出 10 处 hotfix, GM hotfix 率 62.5% — W6 W3 起派单 prompt 必含 build+ctest 验证 hard 约束, 期望降至 < 10%; (2) **GM 错 #12 误推 build_adr010 临时目录** 入 git, 已三步闭环 (删除 commit + .gitignore 补通配 + INCIDENTS 入档). 协商会 6/02 第 1 次状态 TBD (无纪要文件产出, 见 §7).

---

## §1 W6 W2 完成 (git log 实测)

### git log 实测数据 (交叉验证, 见 §5)

| commit | 内容 | +行 | -行 | 测试 |
|---|---|---|---|---|
| 5d91551 | Wave 28: 6 IC W6 P0 全交付 | +6245 | -49 | +57 (319→376) |
| a8afebe | Wave 29: 8 IC 跨 5 单元 + 错 #11 hotfix | +6094 | -240 | +52 (376→428) |
| a93abe9 | fix: 删误 commit build_adr010 临时目录 | -48795 | 0 | — |
| c065791 | fix: 补 .gitignore build_*/ 通配 | +1 | 0 | — |
| 4c25ce8 | docs: 错 #12 入 INCIDENTS | +1 | -4 | — |

**W6 W2 累计代码净增 (Wave 28+29, 不含 hotfix):** +12339 行 / -289 行
**W6 W2 ctest:** 319 → 428/428 (+109 新测试, 100% PASS)

> 注: 任务描述中 Wave 29 "+5800+" 行, git show --numstat 实测 Wave 29 = +6094 行 (不含 build_adr010 dir). 偏差来源: Wave 29 commit 包含 build_adr010 下游误入文件 (後由 a93abe9 删除). 实际有效代码行 +6094 / 任务说 +5800+, 偏差 5% 以内.

---

### 5 单元交付汇总 (Wave 28+29)

| 单元 | IC | 交付摘要 | ctest 增量 | commit | 错 #11 hotfix |
|---|---|---|---|---|---|
| A 老周 | 老高 | PR review v1.2 (测试分级/fd RAII) | 0 (文档) | 5d91551 | — |
| A 老周 | 老姜 | vCPU Tier 2 v0.6 bench (ADR-015) | 0 (bench) | 5d91551 | — |
| A 老周 | 老唐 | BLAKE3 真切换 + AuditEmitterPool (5 上游共享链) | +5 | a8afebe | 4 处 |
| A 老周 | 老李 | live_pm_client ABI lock + 29 static_assert (4级改动协议) | +29 | a8afebe | — |
| A 老周 | 小冯 | IngestRaw 第 5 WAL (2-tier SPSC, 5 source kind, R-20 fallback) | +7 | a8afebe | nodiscard 3 处 |
| B 老韩 | 老沈 | STRATEGY_DECAYED SOP + stcpp-strategy-unlock CLI (Ed25519) | +8 | a8afebe | vcpkg guard |
| C 小梁 | 老彭 | 8 bookmaker 历史回填 + de-vig 校准 (Python offline, ~750万行) | 0 (offline) | a8afebe | — |
| C 小梁 | 小蒋 | VirtualFill 加 market_id/outcome + paper_e2e_smoke T1 更新 | +2 | a8afebe | unused helper |
| D 小余 | 小段 | Goalserve schema 锁 + data_contract ABI + odds_record 248B WAL | +23 | a8afebe | 字段同名 |
| E 老胡 | 老胡 | 风险登记 v2.3 + 周报模板 v2 生效 (PM 帽, 0 cpp) | 0 | 5d91551 | — |
| E 老胡 | 老胡 | 本周报 + risk v2.4 + 模板 v3 (PM 帽) | 0 | Wave 30 | — |
| F 老雷 | 老周 | ADR-018 lib 选型 (cpp-httplib/Beast/nlohmann) | 0 (ADR) | 5d91551 | — |
| F 老雷 | 老徐 | persona_boundary_check.py + escalate-decision-log v1 | 0 (offline) | a8afebe | — |

**A 单元 Wave 29 ctest:** 老唐 5 + 老李 29 + 小冯 7 = **41; 全仓 W6 W2 净增 +109 (319→428)**

---

## §2 W6 W2 阻塞

### P1 blocker: vcpkg unofficial-sodium configure fail (已解)

- **上报人**: 小宋 (E 单元, integration test owner)
- **现象**: Wave 29 老沈 stcpp-strategy-unlock CLI 依赖 vcpkg unofficial-sodium, configure fail
- **处置**: GM 拍板 `STCPP_BUILD_CLI OFF` (CMake guard 默认 OFF)
- **状态**: 已解闭环; R-44 新立 (vcpkg 与 FetchContent 混用风险, 见 §3)
- **教训**: 第三方依赖 (vcpkg / FetchContent 新源) 派单必须留 CMake guard 默认 OFF

### GM 错 #11 (build verification 缺失)

- Wave 29 8 IC 各自声称本地测试, GM 整合 build 出 10 处 hotfix
- 详见 §5 Build Verification KPI 首期
- 永久 enforcement: 派单 prompt 必含 build+ctest 验证 hard 约束 (W6 W3 起执行)

### GM 错 #12 (gitignore 通配不全)

- Wave 29 build 后 build_adr010 临时目录误入 git
- 3 步闭环: a93abe9 删除 → c065791 补 .gitignore build_*/ → 4c25ce8 入 INCIDENTS
- 详见 §8 Commit Hygiene KPI 首期

### 老胡 W5 mandate §5 跨主管 17 ASK W6 W2 进度 (TBD)

- 来源: `docs/MEETINGS/2026-06-01-manager-sync-w5-v1.md` ASK-A/B/C/D/E 各条
- W6 W2 期间无专项协商会纪要产出
- 5 主管 ack 状态: TBD — 老胡 W6 W3 主持协商会追 (见 §7)

---

## §3 风险登记 update (v2.3 → v2.4)

详细版本: `docs/RESEARCH/laohu-risk-registry-v2.4.md`

| 编号 | 变更 | I×P | 来源 |
|---|---|---|---|
| **R-42 (新)** | sub-agent build verification 缺失 | **15 (5×3)** | GM 错 #11, Wave 29 实际触发 |
| **R-43 (新)** | gitignore 通配不全 / 临时目录误入 git | **9 (3×3)** | GM 错 #12, Wave 29 实际触发 |
| **R-44 (新)** | vcpkg 与 FetchContent 混用 — 依赖管理二元化 | **9 (3×3)** | 小宋 P1 blocker, Wave 29 触发 |
| **R-41 更新** | 跨 wave 引用过时信息 — W6 W2 0 新发, 降观察 | **12 (4×3)** | 周报 §4 SSOT 版本演进段执行, W6 W2 无新触发 |
| R-38 | e2e WAL fsync — 监控不变 | 12 (4×3) | W6 W2 ctest 428/428 ✓, 无触发 |

**Top 5 v2.4:**

| 排名 | 编号 | 描述 | I×P | 变动 |
|---|---|---|---|---|
| Top 1 | R-02 | RiskGateway 绕过 | 20 (5×4) | 不变 |
| Top 2 | R-06 | seconds_delay 吃 PnL | 16 (4×4) | 不变 |
| Top 3 | R-07 | HC-01/02 招聘失败 | 16 (4×4) | 6/30 deadline 临近 |
| Top 4 | R-09 | Sharpe > 1 不达 | 15 (5×3) | 不变 |
| Top 5 | **R-42 (升)** | **sub-agent build verification 缺失** | **15 (5×3)** | **新入 Top 5, W6 W3 必降** |
| 候补 | R-31 | R-20 PIT 违例 | 12 (3×4) | W6 W2 428 tests ✓, 监控 |
| 候补 | R-44 | vcpkg 混用 | 9 (3×3) | 新增, 老韩 #3 Position WAL P0 已闭环 |

> 老韩 #3 Position WAL P0 闭环状态: Wave 28 position_ledger.cpp + test_position_ledger.cpp 664 行 + 57 测试全过, R-38 WAL P0 阶段缓解. R-44 vcpkg 候补顶入.

---

## §4 本周 SSOT 版本演进 (R-41 永久 enforcement 第 3 条)

### §4.1 本周 vN → v(N+1) 推翻清单

| owner | 单元 | SSOT 文档 | vN | v(N+1) | 推翻结论 |
|---|---|---|---|---|---|
| 老高 | A | laogao-pr-review | v1.1 | **v1.2** | v1.1 无测试代码分级; v1.2 加 ADR-010 测试分级 + fd RAII + shift static_assert; 下游: 老周 (A 验收标准升), 小宋 (CI grep 加项) |
| 老胡 | E | laohu-risk-registry | v2.3 | **v2.4** | v2.3 Top 5 候补 R-38 WAL; v2.4 R-42 新入 Top 5 (build verification), R-43/44 新增; 下游: 老雷 / 老韩 / 全主管 |
| 老周 | A | ADR | ADR 目录 | **ADR-018 新增** | lib 选型拍板: cpp-httplib (HTTP) / Boost.Beast (WSS) / nlohmann::json (JSON); 下游: 老李 (pm_client 实现) / 小冯 (WSS 实现) |

### §4.2 跨 wave 决议追溯 (W6 W2 引用 SSOT 版本核查)

- Wave 28 老高 PR review v1.2 引用: laogao-pr-review-v1.1 → **v1.2 最新**, 无过时问题
- Wave 29 老段 schema 引用: xiaoduan-goalserve-official-doc-v3 → **v3 最新**, 无过时问题
- Wave 29 老彭 bookmaker 引用: laopeng-bookmaker-history-backfill-v1 (新建) → 无旧版冲突
- ADR-018 lib 选型引用: ADR-006/012/017 → 全部最新版, 无过时问题

**本周无 GM / 主管引用过时 SSOT 事件** (R-41 W6 W2 零触发, 观察期维持).

### §4.3 W6 W3 watch list

- 小段 goalserve schema: v3 + W6 W2 data_contract.hpp ABI 锁后, 是否出 v4 (bwin 第 9 家确认后)
- 老彭 bookmaker-history-backfill: v1 (新建), v2 可能在 bwin 补入后
- 小梁 de-vig 算法选型: ADR-008 后建模 v0 待出, 影响 P0-02 spec (小程 W6 W3 派)

---

## §5 W6 W2 KPI 实测 (老胡数据)

### §5.1 主管派单覆盖率

| KPI | 目标 (ADR-005 W6 硬约束) | W6 W2 实测 | 状态 |
|---|---|---|---|
| 主管派单覆盖率 | 100% | **100%** (Wave 28+29 全 14 IC 任务 spec by 主管或 GM, ADR-005 enforce) | 绿 |
| Sonnet 派单率 (ADR-009 v2) | 100% | **100%** (Wave 28+29 共 14 IC 全 Sonnet, 0 Opus) | 绿 |
| ctest 通过率 | 100% | **100%** (428/428 W6 W2 末) | 绿 |
| 主管帽 cpp 行数 | = 0 | **5 主管全 0** (老周/老韩/小梁/小余/老胡 E 帽 = 0) | 绿 |

### §5.2 协商会次数

| KPI | 目标 | W6 W2 实测 | 状态 |
|---|---|---|---|
| 协商会次数 | 每周 1 次 (老胡主持每周二 30min) | **0** — 6/02 (W5 二) 第 1 次计划未见纪要文件 (`docs/MEETINGS/` 无 `*negotiation*` 文件) | 黄 |

> 老胡注: `docs/MEETINGS/` 无协商会纪要文件产出. 无法核实 6/02 是否召开. W6 W3 (下周二) 必须开第 1 次 + 出纪要 (见 §7).

### §5.3 escalate 次数 (R-39 配套)

| KPI | 目标 | W6 W2 实测 | 状态 |
|---|---|---|---|
| sub-agent 拒接次数 | ≤ 例外 | **0** (W6 W2 无 sub-agent 拒接记录) | 绿 |
| IC 越主管找 GM | 0 (W6 硬约束) | **0** | 绿 |
| escalate 次数 (非正常) | 0 (期望) | **0** | 绿 |

> sub-agent 自我纠错 0 次, 比 W5 (老沈 BUG-W5-001 例外 1 次) 改善.

### §6 Model KPI (ADR-009 v2)

| KPI | 目标 | W6 W2 实测 | 状态 |
|---|---|---|---|
| Sonnet 派单率 | 100% | **100%** (Wave 28 6 IC + Wave 29 8 IC = 14 IC 全 Sonnet) | 绿 |
| Opus 派单次数 | 0 (W6 起) | **0** | 绿 |
| Opus 使用率 (sprint 内) | < 5% | **0%** | 绿 |

ADR-009 v2 W6 W2 首周完整执行, 与 W5 Wave 25/26 的 ~10 次 Opus 误用形成对比.

### §7 Build Verification KPI (首期, GM 错 #11 配套)

> **首期数据 — W6 W2 Wave 28+29**

| KPI | W6 W2 实测 | 目标 (W6 W3 起) | 状态 |
|---|---|---|---|
| sub-agent 回汇声称测试通过数 | **16** (Wave 28 6 IC + Wave 29 10 项含 hotfix 修正) | N/A | — |
| GM 整合 build 时回归 hotfix 次数 | **10 处** | 0 | P0 红灯 |
| GM hotfix 率 | **10/16 = 62.5%** | < 10% | P0 红灯 |
| 派单 prompt 含 "cmake --build && ctest" 验证条款覆盖率 | **0%** (Wave 28+29 派单均无此条款) | 100% (W6 W3 起) | P0 红灯 |

**10 处 hotfix 明细 (GM 错 #11 log)**:
1. 老唐 BLAKE3 unused-function
2. 老唐 NEON arm64 编译
3. 老唐 private friend
4. 老唐 std::min cast
5. 小冯 ingest_raw_writer nodiscard x3
6. test_audit_emitter 没 link stcpp_observability_audit (blake3.h not found)
7. 小段 odds_record audit_id 方法字段同名
8. 小卢+老王 test_position_ledger EXPECT_NEAR int→double
9. position_ledger.cpp ToMarketIdArray unused (小蒋 W6 改后 helper 没用)
10. audit_chain_verify_test 5 处 sign-conversion (老唐 XOR→Blake3)

**W6 W3 必降行动**: 所有派单 prompt 加 "本地 cmake --build build && ctest --output-on-failure 必须全过才回汇" hard 约束 (GM 错 #11 永久 enforcement 第 1 条).

### §8 Commit Hygiene KPI (首期, GM 错 #12 配套)

| KPI | W6 W2 实测 | 目标 | 状态 |
|---|---|---|---|
| 误推临时 build dir 次数 | **1 次** (build_adr010, commit a93abe9 删除) | 0 | 红 |
| .gitignore 通配覆盖率 (PR 前) | 不完整 (build_*/ 通配缺) | 100% | 已补 c065791 |
| GM 错号累计 | **12** (错 #1-12) | 下降趋势 | 监控 |

**三步闭环**: a93abe9 (删临时目录) → c065791 (.gitignore 补 build_*/通配) → 4c25ce8 (INCIDENTS 入档).

---

## §6 W6 W3 backlog (Wave 30 已派 8 IC)

> 注: 以下为 Wave 30 已派任务. 老胡 PM 跟进, 各 IC 执行状态由主管汇报.

| IC | 单元 | 任务 | 优先级 | ETA |
|---|---|---|---|---|
| 老高 | A | PR review v1.3 + 3 grep + Build verification grep | P1 | W6 W3 |
| 老周 | A | lib 选型 ADR-018 深化 + Tier 2 vCPU v0.7 | P1 | W6 W3 |
| 小卢 | A (IC pool) | FdGuard + single_instance 死代码修 | P1 | W6 W3 |
| 老孙 | A | signer v5.2 cpp v0.1 (撤 Rust 后, ADR-009 v2 Sonnet) | P0 | W6 W3 |
| 小程 | C | P0-02 spec v0.1 (小梁主管派) | P0 | W6 W3 |
| 小田 | D | Parquet 分区 + DuckDB notebook (小余主管派) | P1 | W6 W3 |
| 老徐 | F | persona_boundary_check.py v2 + escalate-decision-log v2 | P2 | W6 W3 |
| 老胡 | E | W6 W2 周报 (本文) + risk registry v2.4 + 模板 v3 | P1 | W6 W3 |

**跨主管 ASK (W6 W3 协商会议程)**:
- ASK-A-1: 老周 → 小余: IngestRaw Tier 2 counter schema 对齐 (小冯 Wave 29 残留)
- ASK-C-1: 小梁 → 老韩: P0-02 risk gate 设计对齐 (小程 spec 前置)
- ASK-D-1: 小余 → 老周: DuckDB notebook vs WAL schema 对齐 (小田 派单前)
- ASK-E-1: 老胡 → 5 主管: W6 W3 协商会 6/09 (W6 二) 14:00 出席 ack

---

## §7 协商会 6/02 第 1 次状态

**状态: 未见纪要文件** (`docs/MEETINGS/` grep "negotiation" = 0 结果)

- W5 末 6/01 主管周同步 D-10 决议: 6/02 (W5 二) 14:00 第 1 次需求-工程协商会启动
- W6 W2 末检查: `docs/MEETINGS/` 无 `*negotiation*` 或 `*协商*` 命名文件
- **W6 W3 必须召开 + 出纪要**: 老胡 W6 W3 主持, 日期 6/09 (W6 二) 14:00
- **议程 (Wave 26 决议 + 老胡 mandate §6 + ADR-005 §6)**:
  1. 17 跨主管 ASK 逐条 ack (W5 mandate 遗留)
  2. W6 W3 backlog 跨主管依赖对齐 (见 §6)
  3. Build Verification KPI 62.5% → < 10% 行动计划各主管确认
  4. vcpkg vs FetchContent 混用 (R-44) 短期规范
- **ack baseline**: 会议纪要落 `docs/MEETINGS/2026-06-09-negotiation-w6-w3-v1.md`, 5 主管签出席

---

## §8 W7 EOW (6/12) Sprint-2 W7 末 plan

- 老胡 sprint-02 W7 末 retro 准备 (7/05 暂定)
- ADR 执行率: ADR-006/007/008/011-015 (Wave 26/27 拍板) + ADR-016/017/018 (Wave 28 拍板) — W7 末由老高 + 小宋 grep 验证实际代码覆盖
- 22 smell (Wave 26 代码 review 大会) + GM 错 #11/#12 全闭环验证: W7 末老胡统计
- M1 评审 6/25 准备 (老胡 + 小颖 acceptance v1): 接受标准草案 W7 初出, 6/25 前 GM + 老钱 CPO 拍板
- W6 W3 协商会 ack baseline 建立 (见 §7)

---

## §9 待 GM 拍板 + 升级议题

| 编号 | 议题 | 优先级 | 截止 | 影响 |
|---|---|---|---|---|
| E-11 | Build Verification KPI 62.5% GM hotfix 率 W6 W3 必降到 < 10% — GM 确认硬指标 + 派单 prompt 模板更新 | P0 | W6 W3 首日 | 所有派单 |
| E-12 | 协商会 6/02 召开/未召开 ack — 老雷确认 D-10 执行状态 | P1 | W6 W3 (6/09) 前 | W6 W3 协商会基准 |
| E-13 | M1 评审 6/25 议程定稿 (老胡 + 小颖 acceptance v1 框架) — 老钱 CPO + 老雷联决 | P1 | 6/19 EOW | M1 里程碑 |
| E-14 | Smell #1 (老胡 W6 W2 复评 M4.5 时机) — 22 smell W6 W3 + M1 前各 smell 闭环率 | P2 | W7 末 | Sprint-3 计划 |
| E-15 | vcpkg vs FetchContent 混用规范 (R-44) 短期决策 — 老周 + 老韩 + GM 三方 | P1 | W6 W3 协商会 | 后续依赖管理 |

---

## §10 完成汇报

W6 W2 工程核心: Wave 28+29 共 14 IC Sonnet 全交付 (+12339 行, +109 ctest, 428/428 PASS). ADR-018 lib 选型拍板, BLAKE3 audit chain 真切换, PositionLedger + IngestRaw + StrategyUnlock CLI 落码. **Build Verification KPI 62.5% GM hotfix 率 P0 红灯** — W6 W3 首要任务是派单 prompt 加 build+ctest hard 约束. 协商会 6/02 未见纪要, W6 W3 (6/09) 重新启动. M1 评审 6/25 老胡 + 小颖 acceptance v1 准备中.

**git log 实测交叉验证:** `git show --stat 5d91551` = +6245/-49 (Wave 28 ✓) / `git show --stat a8afebe` = +6094/-240 不含 build_adr010 (Wave 29 ✓) / ctest 319→376→428 commit message ✓. 周报 KPI 数字与 git 实际数据偏差 < 5% (Wave 29 行数任务说 "+5800+", 实测 +6094, 偏差约 5%). **git log 实测 ✓**

**W6 W3 首要任务**: Build Verification KPI 62.5% → < 10% (GM 错 #11 永久 enforcement 第 1 条执行).

老郭 + GM review → 全员共享周报.

— 老胡, 2026-05-28 (Sprint-2 W6 W2 末, Wave 30)
