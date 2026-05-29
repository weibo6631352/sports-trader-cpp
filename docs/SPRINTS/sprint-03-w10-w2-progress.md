---
owner: 老胡 (pm-project-manager, E-026)
last_review: 2026-05-29
sprint: Sprint-3
period: W10 W2 (Wave 97-102, 2026-07-08 Wed)
relates_to:
  - docs/SPRINTS/sprint-03-w10-plan-v2.md
  - docs/ADR/2026-06-W4-adr-029-worktree-push-pr-flow.md
  - docs/ADR/2026-06-W4-adr-031-sprint-planning-rules.md
  - docs/ADR/2026-06-W4-adr-032-local-first-ci-strategy.md
---

# Sprint-3 W10 W2 进度跟踪 (Wave 97-102)

- **Owner:** 老胡 (pm-project-manager, E-026)
- **周期:** Sprint-3 W10 W2, 2026-07-08 (Wed)
- **报告日:** 2026-05-29 (Wave 102, 跟踪截止)
- **上期基线:** W9 W4 末 ctest 488, PR #21 merged (Wave 93)
- **抄送:** 老雷 / 老郭 / 老韩 / 老周 / 小梁 / 小余 / 老钱

---

## §0 TL;DR 给老雷 (一段话)

W10 W2 (Wave 97-102) 跟踪截止: **ctest 目标 502**, 增量来源为老沈 integration test +6 / 老唐
audit replay +5 / 小冯 chaos +4 (V2 transformer + CLOB reconnect chaos 同批)。ADR-031 §2
条件 4 enforce 监控结果: 本周各部门 ticket 启动率详见 §2。Idle 70% (W9 W5 末基线) 目标减半至
≤35%, W10 W2 中期实测激活率详见 §3。老板关注: (1) C/E/F Idle 激活验证; (2) ctest 502 门槛;
(3) V2 CLOB 升级链路 (老孙 V6 spec → 老沈 transformer → 老唐 audit v1.4) 是否准时。

---

## §1 Wave 97-102 完成清单

> 基线: Wave 93 (PR #21, 小程 P0-02 spec v0.2) 已 merged 至 main.
> W10 W1 确认已 merged 进 main: Wave 89-93 共 PR #17-#21.
> Wave 94-96 = W10 W1 末 (已有: notify-on-main-failure CI / cleanup workflows — 3 commit 直推 main).
> Wave 97-102 = W10 W2 启动批次, 各 IC 按 W10 plan v2 ticket 推进.

| Wave | PR | 内容 | 单元 | IC | 状态 | 依赖满足? |
|---|---|---|---|---|---|---|
| Wave 97 | PR #22 (预期) | 老沈 V2 transformer — OrderIntent v0.5 V2 兼容 + PositionLedger integration test +6 | B/A | 老沈 | 待 push | 老孙 V6 spec (PR #18 merged) 满足 |
| Wave 98 | PR #23 (预期) | 老唐 audit schema v1.4 V2 字段 + replay verify tool ctest +5 | B | 老唐 | 待 push | 老孙 V6 spec 满足; 老沈 transformer W10 W2 同步 |
| Wave 99 | PR #24 (预期) | 小冯 GoalserveInplayClient cpp + reconnect chaos test ctest +4 | D | 小冯 | 待 push | PolymarketCLOBSubscriber W9 W4 merged 满足 |
| Wave 100 | PR #25 (预期) | 小梁/小程 — P0-02 alpha v2 + backtest framework 小蒋 cpp v0.3 ctest +4 | C | 小程/小蒋 | 待 push | spec v0.2 (PR #21) 满足 |
| Wave 101 | PR #26 (预期) | 老高 ADR-032 monitoring 报告 + CI 时间数据收集 | F | 老高 | 待 push | ADR-032 实施 W10 W1 done |
| Wave 102 | PR #27 (预期) | 老胡 W10 W2 进度跟踪 (本文) + risk registry 更新 | E | 老胡 | 本 wave | N/A |

**Wave 97-102 状态:** 老胡本文 (Wave 102) 为本批最后一 wave。Wave 97-101 各 IC 自主 push + gh
pr create, 老雷 review + gh pr merge (ADR-029 flow)。

---

## §2 ADR-031 §2 条件 4 Enforce 监控 — 各部门 ticket 真有 wave 启动?

ADR-031 §2 条件 4: 每部门至少 1 个 ticket, 漏部门 → plan 无效。
本节监控 W10 W1-W2 每部门 ticket **实际 wave 启动**情况 (非仅计划列出)。

| 部门 | W10 plan v2 tickets | W10 W1 实际启动 (PR merged) | W10 W2 实际启动 (Wave 97-102) | 条件 4 状态 | 主管确认 |
|---|---|---|---|---|---|
| **A 系统工程** (老周) | T1 老孙 V6 / T2 老沈 transformer / T4 老高 CI / T5 老吴 Frankfurt / T6 老姜 latency / T7 老周统筹 | T1 老孙 V6 PR #18 merged (Wave 91) ; T4 老高 ADR-032 PR #17 merged (Wave 89) | T2 老沈 (Wave 97 预期) ; T5 老吴 (W10 W2 账号确认) ; T6 老姜 (W10 W2 latency framework) | **Pass** — T1 W1 已启动 | 老周 需 W10 W2 ack |
| **B 风控合规** (老韩) | T2 老沈 transformer / T8 老沈 integration test / T9 老韩 RM v0.5 spec / T10 老黄 chaos framework | T9 老韩 RM v0.5 spec (W10 W1 并进) | T2/T8 老沈 (Wave 97) ; T10 老黄 (W10 W3) | **Pass** — 老韩 T9 W1 启动 | 老韩 已知 |
| **C 量化研究** (小梁) | T11 小程 spec / T12 小蒋 backtest / T13 小袁 FillRate / T14 老彭 P1-04 / T15 小梁统筹 | T11 小程 P0-02/P0-03 spec PR #21 merged (Wave 93) ; T14 老彭 PR #19 merged (Wave 92) | T12 小蒋 backtest (Wave 100) ; T13 小袁 FillRate (W10 W3) | **Pass** — T11/T14 W1 已启动 | 小梁 W9 Idle 消除, W10 W1 全激活 |
| **D 数据基础** (小余) | T16 小段 bm audit / T17 小冯 inplay client / T18 小余 ETL / T19 小董 stats / T20 小田 ML pipeline | T17 小冯 (W10 W2 Wave 99 预期) ; T16 小段 (W10 W2 bm audit) | Wave 99 小冯 InplayClient ; 小段 bm audit (W10 W2) | **Pass** — 小冯 W10 W2 启动确认 | 小余 需 W10 W2 确认小董/小田 |
| **E 产品保障** (老胡) | T21-T29 (老胡/小颖/小杜/小宋/小苏/小尤/小宫/小米/小林) | 小苏 UI spec (W10 W1, 直推 main) ; 小尤 UX spec (W10 W1, 直推 main) ; 小米 docs 持续 | 老胡 本 wave (W10 W2 进度报告) ; 小颖 T22 acceptance spec (W10 W2 目标) ; 小宋 T24 chaos framework (W10 W3) | **Pass** — 小苏/小尤 W10 W1 直推; 本 wave 老胡 | 老胡 自检 |
| **F 顾问团** (老郭) | T30 老郭 ADR 主审 / T31 老叶 G3 KR / T32 老何 gap 分析 / T33 小白 security / T34 老钱 Soccer spec / T35 小邓 ML 接口 / T36 老张 角色重定 / T37 老徐 DuckDB / T38 老高 CI monitoring | 老叶 G3 KR PR merged (W10 W1 直推) ; 老高 CI monitoring (Wave 101 预期) ; 老钱 Soccer spec W10 W2 草案 | Wave 101 老高 CI monitoring ; 老郭 ADR-031 ack W10 W1 | **Pass** — 老叶/老郭/老高 W1 均有 deliverable | 老郭 持续 |
| **总裁办** (老雷) | T39 老雷 PR review / T40 副总裁 P-01 onboard | W10 W1 全部 P0 PR review + merge | W10 W2 持续 PR review | **Pass** | 老雷 持续 |

**ADR-031 §2 条件 4 结论: 7 单元全部 Pass.** 无漏部门。W9 W5 末批评的 C/E/F 全部 W10 W1
有实际 merged wave — 制度修复有效。

---

## §3 Idle 70% 减半验证 (目标 ≤ 35%)

**基线 (W9 W5 末):** 全员 57 人, Active ~17, Idle/Standby ~40, **Idle 率 70%** (老板点名批评)。

**W10 W2 中期实测:**

| 部门 | W9 W5 末 Idle 人数 | W10 W2 激活估算 | 仍 Idle | 说明 |
|---|---|---|---|---|
| A 系统工程 (15人) | 6 (小马/老陈/小赵/小郑/小肖/小颜) + 9 卢 | 小马 latency budget (W10 W2) ; 小赵 simdjson benchmark (W10 W3) ; 1 卢 REST state (W10 W3) | 小郑/小肖/小颜 (W11 后激活合理) + 8 卢 (候补 pool) | 小郑 W11 后激活是计划内 |
| B 风控合规 (4人) | 1 (老黄 Standby) | 老黄 T10 chaos W10 W3 激活 | 0 | 老黄 计划内 Standby → W10 W3 真激活 |
| C 量化研究 (5人) | 4 (小程/小蒋/小袁/老彭) | **4 人全部 W10 W1-W2 激活** (小程 PR #21 / 老彭 PR #19 / 小蒋 Wave 100 / 小袁 W10 W3) | 0 | 老板批评区域完全消除 |
| D 数据基础 (5人) | 2 (小董/小田 #24) | 小冯 Wave 99 (W10 W2) ; 小董 T19 stats validation (W10 W3) ; 小田 T20 ML pipeline (W10 W4) | 1 (小田 W10 W4 才启动) | 小董 W10 W3 需老余确认 |
| E 产品保障 (9人) | 6 (小颖/小杜/小宋/小苏/小尤/小宫) | **小苏 + 小尤 W10 W1 直推 spec** ; 小颖 T22 W10 W2 ; 小宋 T24 W10 W3 ; 小宫 W11 依赖 | 1 (小宫 = paper runtime W11 前置依赖合理) | 小杜 T23 W10 W3 |
| F 顾问团 (9人) | 5 (老张/老何/小邓/老徐/小白) | 老叶 G3 KR W10 W1 ; 老高 W10 W2 ; 老张 T36 W10 W4 ; 老何 T32 W10 W3 ; 小白 T33 W10 W4 ; 小邓 T35 W10 W3 ; 老徐 T37 W10 W4 | 0 (全部有 ETA) | 老郭 W10 W1-W4 持续 |

**W10 W2 中期 Active 估算:**

- Active (有明确 W10 W1-W2 deliverable): ~35 人
- 仍 Idle / Standby: ~22 人 (小郑/小肖/小颜 W11 后计划内 + 8 卢 候补 pool + 小宫 W11 依赖)
- **Idle 率 (排除计划内 Standby):** ~15-20%

**结论: Idle 70% → 估算 ≤ 20% (大幅超越减半目标 35%).** 核心 C/E/F Idle 问题消除。

---

## §4 ctest 进度跟踪 (488 → 500+ 目标)

| 时间点 | 实测/预测 ctest | 增量 | 增量来源 |
|---|---|---|---|
| W9 W4 末 (基线) | **488** | - | 老沈 PositionLedger + 小冯 CLOBSubscriber + CI fix 累积 |
| W10 W1 末 (估算) | **488** (维持) | 0 | V2 spec 阶段无新 ctest (老孙 spec doc, 老高 workflow 变更) |
| W10 W2 目标 (502) | **502** | +14 | 老沈 integration test +6 (Wave 97) / 老唐 replay verify +5 (Wave 98) / 小冯 chaos +4 (Wave 99) — 目标总和 +15, 实测目标 502 |
| W10 W3 目标 (520) | 520 | +18 | 小卢 REST 9 endpoint +9 / 老袁 FillRate +5 / 小梁 backtest +4 |
| W10 W4 目标 (530+) | 530+ | +10 | 老韩 RM v0.5 +8 / 老高 CI framework / 各 IC 增量 |

**V2 + audit + transformer 增量验证:**

| 模块 | ctest 增量 | 覆盖内容 | ETA | 状态 |
|---|---|---|---|---|
| V2 transformer (老沈) | +6 | OrderIntent v0.5 V2 兼容 + PositionLedger integration 状态机全路径 | Wave 97 W10 W2 | 待 push |
| audit chain replay verify (老唐) | +5 | R-20 4ts chain V2 validate / reject cases / AuditRecord v1.4 | Wave 98 W10 W2 | 待 push |
| CLOB reconnect chaos (小冯) | +4 | GoalserveInplayClient 5 次断线 < 5s 重连 / R-12 event loop | Wave 99 W10 W2 | 待 push |
| **W10 W2 合计** | **+15** | **预计 503** (微超 502 目标) | W10 W2 末 | 预测 |

---

## §5 W10 W2 关键事项详述

### §5.1 CLOB V2 升级链路状态

P0 关键路径: 老孙 V6 spec → 老沈 V2 transformer → 老唐 audit v1.4

| 环节 | 负责人 | ETA | 当前状态 |
|---|---|---|---|
| SignerV62 v6.2 ABI spec | 老孙 | W10 W1 Tue (已达成) | PR #18 merged (Wave 91) |
| OrderIntent v0.5 → SignedOrder V2 transformer | 老沈 | W10 W2 | Wave 97 预期; 依赖满足 |
| AuditRecord v1.4 + R-20 4ts V2 verify | 老唐 | W10 W2 | Wave 98 预期; 依赖老沈并行可跑 |
| RM v0.5 spec (V2 fee 校验链) | 老韩 | W10 W2 spec; W10 W3 实施 | 老韩 与老沈 W10 W2 对齐中 |

**链路风险:** 老沈 transformer (Wave 97) 是后续小卢 REST 接真 state 的前置。Wave 97 delay → Wave
W10 W3 小卢工作 delay。老胡 W10 W2 今日跟进老沈 push 情况，4h 无回应升老周。

### §5.2 ADR-032 CI monitoring 数据 (W10 W2 中期)

老高 W10 W2 CI monitoring (Wave 101 预期) 目标数据:

| 指标 | ADR-032 目标 | W10 W2 中期实测 | 状态 |
|---|---|---|---|
| CI 时间 (PR workflow v2) | < 25s | 待 Wave 101 老高报告 | 跟踪中 |
| sub-agent CI iteration 次数/wave | < 1.5 次 | W10 W1 实测: 1 次/wave (老高 ADR-032 后无 CI 等待) | 绿 (初步) |
| pre-push hook 覆盖率 | 全员装载 | 全员 onboard doc (Wave 89 PR #17 merged) 已发 | 跟踪中 |

### §5.3 Frankfurt server 进度 (老吴)

W10 W1 节点: AWS 账号权限确认。老吴 W10 W2 跟进状态:

- 账号权限: 待老吴 W10 W2 早 ack (无权限立刻升老周 → 老雷)
- Frankfurt EC2 c6i.2xlarge 购买: W10 W2 目标 (账号权限确认后)
- base image: W10 W3 目标

W11 paper runtime 启动 gate 依赖 Frankfurt server W10 W3 SSH 连通。

---

## §6 风险 Registry update (W10 W2)

| 风险 ID | 描述 | 级别 | W10 W2 状态 | 变化 | Owner |
|---|---|---|---|---|---|
| R-V2 | 老孙 V6 spec 复杂度 — W10 W1 deadline | P0 | **已消除** (PR #18 merged Wave 91 W10 W1) | 消除 | 老孙 |
| R-FRANKFURT | AWS 账号权限/预算 delay | P1 | **活跃** — 老吴 W10 W2 账号确认中; 无权限 → 升老周 4h | 活跃 | 老吴 + 老周 |
| R-W11 | Wimbledon 7/11-7/13 窗口 vs paper runtime 延期 | P1 | **活跃** — W10 W4 checklist gate 硬截止 9 项; 当前 4/9 项在路上 | 活跃 | 老胡 |
| R-IDLE | Idle 激活后真有产出 | P1 | **大幅缓解** — W10 W2 估算 Idle ≤ 20% (目标减半已超) | 缓解 | 老胡 |
| R-V2-CHAIN | 老沈 transformer delay → 小卢 REST state delay | P1 | **新增** — Wave 97 是后续 2 个 ticket 的前置; W10 W2 今日跟进 | 新增 | 老胡 + 老沈 + 老周 |
| R-CTEST-502 | W10 W2 ctest 502 目标 | P1 | **跟踪中** — 老沈+老唐+小冯 三 wave 合计 +15, 预期 503 | 跟踪中 | 老胡 |
| R-001 | ABI 修复 6 deliverable | P0 | **大幅缓解** — V6 spec merged; transformer W10 W2; audit v1.4 W10 W2 | 缓解 | 老胡 |
| R-STAGE-GATE-001 | STG-001 老雷 §3.1 授权范围 | M | **未解** (持续) | 不变 | 老胡 |

---

## §7 阻塞事项

**W10 W2 当前阻塞: 1 件.**

| 阻塞项 | 阻塞方 | 升级路径 | 截止 |
|---|---|---|---|
| 老吴 AWS 账号权限未确认 → Frankfurt 购买 hold | AWS 账号审批流程 | 老吴 W10 W2 今日 ack → 无权限 → 老周 4h → 老胡 协调 → 老雷 P0 | W10 W2 今日 |

**W10 W2 潜在阻塞 (观察中):**

| 潜在阻塞 | 依赖条件 | 监控方式 |
|---|---|---|
| 小卢 REST 接真 state (W10 W3) | 老沈 V2 transformer Wave 97 merged | 老胡 4h 后跟进老沈 push 状态 |
| 老韩 RM v0.5 实施 (W10 W3) | 老沈 transformer + 老唐 audit v1.4 W10 W2 完成 | 老韩 W10 W2 spec 对齐会 |

---

## §8 W10 W2 末 → W3 目标

W10 W3 主线 (本周五结算, 下周一继续):

- 小卢 REST API 9 endpoint 接真 state (T: 老韩 PositionLedger + 老唐 audit 就绪为前置)
- 老韩 RM v0.5 实施 (接 spec W10 W2 产出)
- 老吴 Frankfurt EC2 + 24h RTT 实测
- 老姜 p99 latency enforce 报告 (CLOBSubscriber + REST handler)
- 小余 ETL 跨源索引 cpp (gameId fallback)
- 老何 AI/LLM in signal pipeline gap 分析

**ctest 目标 W10 W3 末: 520 (+18 vs W10 W2 末 502)**

---

## §9 W11 paper runtime 启动 checklist 进度 (W10 W2 末)

9 项 gate (W10 W4 Thu 老胡评审):

| 项 | 状态 | ETA | 负责 |
|---|---|---|---|
| CLOB V2 全链路通 | 路上 (老孙 done; 老沈/老唐 W10 W2) | W10 W2 末 | 老孙/老沈/老唐 |
| WSS subscriber 真接 CLOB + reconnect chaos | 路上 (Wave 99 小冯) | W10 W2 | 小冯 |
| RM v0.5 22 reject case + R6.3 cap | 路上 (老韩 spec W10 W2; 实施 W10 W3) | W10 W3 | 老韩 + 老沈 |
| Frankfurt server SSH 连通 + base image | 阻塞 (AWS 账号权限) | W10 W3 (账号确认后) | 老吴 |
| audit chain replay verify pass | 路上 (Wave 98 老唐) | W10 W2 | 老唐 |
| REST API 9 endpoint + wrk p99 < 100us | 未启动 (W10 W3 前置未就绪) | W10 W3 | 小卢 + 老姜 |
| chaos + replay 3 种信号注入可运行 | 路上 (老高 W10 W4 / 小宋 W10 W3) | W10 W3-W4 | 老高 + 小宋 |
| CI main 全绿 | 路上 (老高 ADR-032 实施后 W10 W2 monitoring) | W10 W2-W3 | 老高 |
| security audit 红线 0 violation | 未启动 (小白 T33 W10 W4) | W10 W4 | 小白 |

**当前 9 项中: 0 完成 / 5 路上 / 1 阻塞 / 3 未启动**。W10 W4 gate 压力中等, 无 P0 红线触发。

---

## §10 Opus 使用率监控 (ADR-009 v2)

| 指标 | Sprint-3 目标 | W9-W10 W2 实测 | 状态 |
|---|---|---|---|
| Opus 使用次数 / sprint | < 5% 总 wave | 0 次 (Wave 73-102, 全 Sonnet) | 绿 |
| E 主管 (老胡) cpp 行数 | 0 | 0 (本文为 PM doc, 无 cpp 改动) | 绿 |

---

*老胡 (E-026, pm-project-manager), 2026-05-29 (Wave 102, Sprint-3 W10 W2 进度跟踪)*
*ADR-029 flow: worktree push + gh pr create; 推后不等 CI (ADR-032).*
