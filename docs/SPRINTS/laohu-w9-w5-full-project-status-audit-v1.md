---
owner: 老胡 (pm-project-manager, E-026)
last_review: 2026-05-29
sprint: Sprint-3
period: W9 W5 末 (2026-07-04, Wave 84)
触发: 老板 verbatim 2026-05-29 "定目标必须知道当前项目状态"
整改: W82 W10 plan (sprint-03-w10-plan.md) 由老胡 1 人定 → 降 Draft → 多人讨论会 W10 W1 ack 后 v2 final
ADR-029: 全程 worktree, push + gh pr create
ADR-027 cite: N/A (本文为 PM audit doc, 无核心数据结构 ABI 改动)
---

# 项目全量状态 Audit — W9 W5 末 (Wave 84)

- **Owner:** 老胡 (pm-project-manager, E-026)
- **报告日:** 2026-05-29 (Wave 84)
- **触发:** 老板 verbatim 2026-05-29 — "每个阶段的目标计划, 需要多人讨论后定, 避免一个人考虑不周, 很多部门都没安排活就很浪费, 定目标计划必须知道当前的项目状态, 结合新一轮的市场调研, 再制定目标计划"
- **抄送:** 老雷 / 老钱 (CPO) / 老郭 (F 协调) / 5 主管 (老周/老韩/小梁/小余/老胡) / 老高

---

## §0 整改声明

sprint-03-w10-plan.md (Wave 82, 老胡 1 人撰写) 已降级为 **Draft**。

W10 plan 须经多人讨论会 (W10 W1 启动, 主持: 老胡, 参与: GM + 5 主管 + 老郭 + 9 顾问列席) ack 后升 v2 final。

本文为 audit 输入材料, 非 plan 决议本身。

---

## §1 各部门 W9 W5 末状态 (全 57 persona)

> 状态定义: Active (有明确 deliverable 在跑) / Idle (无活或活已收尾待下一派单) / Blocked (依赖未满足) / Standby (计划内待激活)

### A. 系统工程部 (老周主管, 15 人)

| persona | 花名 | W9 W5 状态 | 当前 task / 输出 | 是否阻塞 |
|---|---|---|---|---|
| cpp-chief-architect | **老周** (主管) | Active | A 单元统筹, ABI v0.5 技术决策拍板, 跨主管接口协商 | 否 |
| cpp-hot-path-engineer | 小马 | Idle | W9 热路径 us 延迟优化未被派新活; W10 依赖老姜 latency perf framework 就绪后才有新任务 | 否 (Idle) |
| cpp-network-engineer | 老陈 | Idle | serialize_into 接口已锁 (W9 W3); W10 follow-up 待老周派活 | 否 (Idle) |
| cpp-serialization-engineer | 小赵 | Idle | simdjson/glaze 相关 W9 无新派单; W10 候选: JSON 序列化升级 | 否 (Idle) |
| cpp-persistence-engineer | 老王 | Active | WAL-B01 Sprint-3 W9 目标 (依赖 ADR-017 小石 SPSC framework); WAL-B02 W10 | Blocked: ADR-017 小石 W9 W1 接口锁定状态待确认 |
| crypto-signing-expert | 老孙 | Active | SignerV52 v5.3 ABI 对齐 (W9 W2/W3 merge); W10 signer 整合测试 | 否 |
| polymarket-protocol-expert | 老李 | Active | WSS Subscriber Spec v1 (W9 W3 deliverable, 覆盖 5 host + R-33 四维扫描) | 否 |
| sports-market-expert | 小田 #8 | Active (低载) | 盘口规则 / 结算规格; A/D 双单元兼 (小田归属仲裁 W9 状态待老雷最新确认) | 黄: 双主管归属模糊 |
| linux-sre-devops | 老吴 | Active | AWS 3-region RTT 实测 (PR #5 W9 W4 merged); W10 Frankfurt server 购买 + base image | 否 |
| observability-engineer | 小郑 | Idle | Prometheus 12 metric 框架 Sprint-3 W11+ 激活; W9 无新活 | 否 (Idle, 计划内) |
| performance-engineer | 老姜 | Active | W10 W2 hot path latency perf framework enforce (CLOBSubscriber event loop + REST handler p99) | 否 |
| data-structures-expert | 小石 | Active | ADR-017 SPSC ring framework 接口锁定; WAL-B01 依赖前置 | Blocked: ADR-017 接口确认时间窗口 |
| senior-algorithm-engineer-a | 小肖 | Idle | Kelly/定价相关 W9 无新派单; W10 候选: 价格模型 cpp | 否 (Idle) |
| senior-algorithm-engineer-b | 小颜 | Idle | 图/状态机 W9 无新派单; W10 候选: 调度状态机 | 否 (Idle) |
| senior-cpp-ic-pool | 小卢×10 | Active (部分) | 小卢 (1 人) W9 REST API Skeleton 6 endpoint (PR W9 W3); W10 W3 REST 接真 state 9 endpoint; 其余小卢 Idle W9 W5 末 | 其余 9 卢 Idle |

**A 单元 W9 W5 末汇总:** Active 7 人, Idle 6 人 (小马/老陈/小赵/小郑/小肖/小颜), Blocked 2 人 (老王/小石)。Idle 人数偏多, W10 多人讨论会需给出 W10 派活方案。

---

### B. 风控合规部 (老韩主管, 4 人)

| persona | 花名 | W9 W5 状态 | 当前 task / 输出 | 是否阻塞 |
|---|---|---|---|---|
| risk-engineer | **老韩** (主管) | Active | RM v0.5 整合 (W10 W1 owner + 统筹); ABI v0.5 OrderIntent + R6.3 per-outcome cap spec | 否 |
| security-engineer | 老沈 | Active | PositionLedger read API + DRAIN StateMachine (PR #3 W9 W4 merged); W10 RM v0.5 实施 IC | 否 |
| compliance-legal | 老黄 | Standby | 监管合规 M4.5 后激活 (GM 2026-05-28 地域合规暂 defer); W9 W5 末无活 | 否 (计划内 Standby) |
| audit-expert | 老唐 | Active | audit schema v1.3 (W9 W3); W10 W2 audit chain replay verify + R-20 4 ts chain verify | 否 |

**B 单元 W9 W5 末汇总:** Active 3 人, Standby 1 (老黄, 计划内)。B 单元载荷合理。

---

### C. 量化研究部 (小梁主管, 5 人)

> **⚠️ 老板批评重点区域 — W9 W2-W4 没派活**

| persona | 花名 | W9 W5 状态 | 当前 task / 输出 | 是否阻塞 |
|---|---|---|---|---|
| financial-expert | **小梁** (主管) | Active (低载) | C 单元统筹; Sprint-3 信号探索方向待 W10 多人讨论会决议 | 否 |
| quant-signal-research | 小程 | Idle | P0-02 spec W9 未被派活; ADR-008 de-vig 落代码 (W6 交付后 W9 无后续新活) | **W9 W2-W4 无活 P0** |
| quant-backtest | 小蒋 | Idle | backtest framework cpp v0.2 skeleton W8 落; W9 无新派单 | **W9 W2-W4 无活 P0** |
| quant-microstructure | 小袁 | Idle | FillRateModel v0.1 (W4 交付); W9 无 W10 候选 VirtualMatcher 切 Mode A 延后 | **W9 W2-W4 无活 P0** |
| betting-industry-expert | 老彭 | Idle | Goalserve 8-9 家历史回填 (W6 Wave 29 完成); W9 无后续新活 | **W9 W2-W4 无活 P0** |

**C 单元 W9 W5 末汇总:** 1 人低载, 4 IC 全部 Idle。**这是老板点名批评的最严重 Idle 区域。**

根因分析: Sprint-3 W9 全部算力集中在 A/B 单元 ABI 修复链路 (老沈/老孙/老唐/小卢/老高/老郭), C 单元信号研究与 ABI 修复无直接耦合, 但小梁未主动拆 W9 信号探索任务填充 C 单元。

---

### D. 数据基础设施部 (小余主管, 5 人)

| persona | 花名 | W9 W5 状态 | 当前 task / 输出 | 是否阻塞 |
|---|---|---|---|---|
| data-etl | **小余** (主管) | Active (低载) | ETL 跨源 mapping enforce 待小段 ack; Sprint-3 backlog 维护 | 否 |
| data-stats | 小董 | Idle | M4.5 gate evaluator framework (W6 Wave 29 完成); W9 stats validation framework 未被派活 | **W9 W2-W4 无活** |
| data-warehouse | 小田 #24 | Idle | Parquet 分区 W6 部分完成; W9 ML data pipeline 未被派活 | **W9 W2-W4 无活** |
| goalserve-api-watch | 小段 | Active | Goalserve full feed 探索持续 (W9 W2-W4 有活: Goalserve schema + SSOT 维护) | 否 |
| api-watch-general | 小冯 | Active | PolymarketCLOBSubscriber (PR #6 W9 W4 merged); W10 W2 reconnect chaos test | 否 |

**D 单元 W9 W5 末汇总:** Active 3 人 (小余/小段/小冯), Idle 2 人 (小董/小田 #24)。小段/小冯有活, 小董/小田 W9 W2-W4 无活。

---

### E. 产品业务保障部 (老胡主管, 9 人)

> **⚠️ M1-H PnL 看板 0/8 重灾区 — 小苏/小尤/小宫/小颖/小杜/小宋 W9 W2-W4 几乎无活**

| persona | 花名 | W9 W5 状态 | 当前 task / 输出 | 是否阻塞 |
|---|---|---|---|---|
| pm-project-manager | **老胡** (主管) | Active | Sprint-3 周报 / risk registry / 本 audit / W10 plan (Draft, 待多人讨论会) | 否 |
| requirements-analyst | 小颖 | Idle | acceptance spec v1 W7 末交付; W9 验收 spec v2 (M1 38 条 update) 未派 | **W9 无活** |
| product-manager | 小杜 | Idle | PRD 信号扩展 (Soccer 3-way / Tennis) W9 未派 | **W9 无活** |
| test-replay-engineer | 小宋 | Idle | integration test framework v0.1 W5 交付; W9 chaos/replay test framework 未派 | **W9 无活** |
| frontend-engineer | 小苏 | Idle | 前端 UI / PnL 看板 尚未启动; REST API W10 W3 就绪后才有真接口 | **W9 W2-W4 无活 (前置未就绪)** |
| doc-curator | 小米 | Active (低载) | docs/ SSOT 守护; Sprint-3 文档归档 | 否 |
| hr-talent-manager | 小林 | Active | 数据结构 IC JD (W9 W1 发布); 副总裁 JD + 7/1 onboarding plan | 否 |
| ux-experience-evaluator | 小尤 | Idle | UX 体感评估尚未启动; 无前端可评估 | **W9 无活 (前置未就绪)** |
| dogfood-tester | 小宫 | Idle | dogfood 需 paper runtime W11 就绪; W9 全 Idle | **W9 无活 (前置未就绪)** |

**E 单元 W9 W5 末汇总:** Active 3 人 (老胡/小米/小林), Idle 6 人。小苏/小尤/小宫 有前置依赖理由 (REST API/paper runtime 未就绪), 但小颖/小杜/小宋 无明显前置依赖, W9 可派活。

**E 单元主管 (老胡) 自检:** W9 未主动给小颖/小杜/小宋 派 W9 活, 是同一问题的 E 端体现。多人讨论会后需填充。

---

### F. 顾问团 (老郭协调, 9 人)

> **⚠️ 老板 verbatim "顾问们别闲着" W83 顾问意见箱启动后仍待落实**

| persona | 花名 | W9 W5 状态 | 当前 task / 输出 | 是否阻塞 |
|---|---|---|---|---|
| rust-advisor | 老张 | Inactive | Rust 退场后角色待重定; W9 无活 | 否 (角色模糊) |
| modern-cpp-advisor | 老何 | Idle | footgun checklist v1.1 W6 交付后 W9 无后续; AI/LLM in pipeline gap 未起 | **W9 无活** |
| cpo-product-strategy | 老钱 (CPO) | Active | 产品方向输入 (策略评审 / Stage-Gate G5 KR 待拍板); 平级 GM 直属 | 否 |
| chief-architecture-reviewer | **老郭** (F 协调) | Active | ADR-029 立项 (W9 W1); 顾问意见箱框架 (W83 Wave 53 激活 doc); CI enforce 跟进 | 否 |
| code-quality-reviewer | 老高 | Active | CI 累积修 3 轮 (Wave 74-77 完成); W10 W4 CI main 干净 + chaos test framework | 否 |
| ml-engineer | 小邓 | Idle | ML data hook v0.1 (W4 Wave 20 交付); W9 PositionManager 衔接 gap 未处理 | **W9 无活** |
| defi-onchain-advisor | 老叶 | Standby | M4.5 上链激活后才起; W9 Standby | 否 (计划内) |
| ai-ops-collaboration | 老徐 | Idle | R-39 escalate flow v0.2 (W5 Wave 24 交付); W9 工具栈升级建议未起 | **W9 无活** |
| ai-llm-advisor | 小白 | Idle | GM 自检 framework v0.2 (W6 交付); W9 security audit 未起 | **W9 无活** |

**F 单元 W9 W5 末汇总:** Active 3 人 (老钱/老郭/老高), Idle 5 人 (老张角色模糊 + 老何/小邓/老徐/小白 有能力但无派活), Standby 1 (老叶)。顾问意见箱 W9 W1 已启动 (老郭 Wave 53 framework doc), 但 9 顾问 W9 W5 末独立 deliverable 几乎为 0 (除老高 CI 修复)。

---

### 总裁办公室 (GM + 副总裁)

| 角色 | 花名 | W9 W5 状态 | 备注 |
|---|---|---|---|
| GM (professional-manager) | 老雷 | Active | 派单 / review / P0 事故指挥 / 多人讨论会主持 |
| 副总裁 | (招聘中) | 待入职 | 7/1 入职目标 (小林 W9 JD 发布) |

---

## §2 没安排活的部门 — 直接对应老板批评

### 2.1 Idle 汇总

| 部门 | Idle 人数 | Idle persona 名单 | 主要原因 |
|---|---|---|---|
| **C 量化研究** | **4/5** | 小程/小蒋/小袁/老彭 | Sprint-3 全算力集中 ABI 修复; 小梁未拆 W9 信号研究活 |
| **E 产品保障** | **6/9** | 小颖/小杜/小宋/小苏/小尤/小宫 | 小苏/小尤/小宫 有前置依赖 (合理); 小颖/小杜/小宋 无明显依赖 (需补活) |
| **F 顾问团** | **5/9** | 老张/老何/小邓/老徐/小白 | 老郭顾问意见箱 W9 W1 启动, 但 W9 W5 末个人 deliverable 仍 0 |
| **A 系统工程** | **6/15** | 小马/老陈/小赵/小郑/小肖/小颜 + 9 卢 | 小郑 W11 后激活合理; 其余 Idle 无 W10 候选 |
| **D 数据基础** | **2/5** | 小董/小田 #24 | stats validation / ML data pipeline 未被排入 W9 |

### 2.2 Idle 总计 (W9 W5 末)

**全员 57 人, Active 约 17 人, Idle/Standby 约 40 人。Idle 率 ~70%。**

这与老板批评"很多部门都没安排活就很浪费"完全吻合。W10 多人讨论会核心任务: 对这 40 人中的 Idle 人员, 按部门给出 W10 具体 ticket。

### 2.3 W10 backlog 候选 (待多人讨论会决议)

| 部门 | W10 候选 ticket | 建议 owner | 优先级 |
|---|---|---|---|
| C | P0-02 spec v0.2 (alpha v2 + C2 6¢ + 死区 25%) | 小程 | P0 |
| C | backtest framework cpp 实施 (接 WAL-B02 W10) | 小蒋 | P1 |
| C | FillRateModel cpp 完整 (VirtualMatcher Mode A 接入) | 小袁 | P1 |
| C | Sprint-4 信号探索方向 spec (Soccer 3-way / Tennis) | 小梁 (统筹) | P1 |
| C | 老彭 Goalserve odds quality 持续监控 (周报入 C 单元) | 老彭 | P2 |
| E | 验收 spec v2 (M1 38 条 update, ABI v0.5 对齐) | 小颖 | P0 |
| E | chaos + replay test W10 framework (给 W11 paper runtime 用) | 小宋 | P0 |
| E | PRD 信号扩展 (Soccer 3-way / Tennis / NBA props) | 小杜 | P1 |
| E | 前端 UI v1 6 endpoint (待 REST API W10 W3 就绪后启动) | 小苏 | P1 (W10 W4 启动) |
| E | UX 评估计划 + dogfood W11 checklist 起草 | 小尤 | P2 |
| F | 老张角色重定: C++20 架构兼容性独立审查 (老郭 W9 W1 顾问意见箱 ping 已发) | 老张 | P1 |
| F | AI/LLM in pipeline gap analysis (PoC 建议) | 老何 | P1 |
| F | ML pipeline + PositionManager 衔接接口 gap doc | 小邓 | P1 |
| F | 工具栈升级建议 v2 (DuckDB/Parquet ETL 深度集成) | 老徐 | P2 |
| F | security audit 预审 (G4 上线前必关闭的 gap 清单) | 小白 | P1 |
| A | 小马 hot path latency budget doc (配合老姜 W10 enforce) | 小马 | P2 |
| A | 小赵 simdjson/glaze 性能 benchmark (配合老姜 framework) | 小赵 | P2 |
| D | 小董 stats validation framework W10 (A/B test 框架给 paper runtime) | 小董 | P1 |
| D | 小田 #24 ML data pipeline W10 (Parquet 接 小邓 ONNX hook) | 小田 #24 | P1 |

**注: 以上候选 ticket 均为 audit 建议, 非 plan 决议。W10 多人讨论会负责最终决议。**

---

## §3 项目里程碑实际进度

| 里程碑 | W7 末 | W8 W5 末 | W9 W4 末 | W9 W5 末 (本报告) | 关键变化 |
|---|---|---|---|---|---|
| **M1 MVP (2026-11-30)** | 63% | 64% | 66% | **66%** | W9 W5 无新 merge; PnL 看板仍 0/8 |
| **M2 Sharpe (2026-12)** | 40% | 41% | 42% | **42%** | CLOB subscriber 为策略数据链路打基础 |
| **M4.5 paper gate (2027-05)** | 10% | 10% | 12% | **12%** | AWS RTT 实测 + PositionLedger 前置完成 |
| **M5 live (2027-11)** | 5% | 8% | 10% | **10%** | signer + CLOB 链路进度 |
| **M6 北极星 (T+36 月)** | 0% | 0% | 0% | **0%** | 尚未进入实盘 |

### 3.1 时间消耗计算

- Sprint 开始: 2026-06-01 (Sprint-1 启动日)
- M1 目标: 2026-11-30 (T+6 月)
- W9 W5 末: 2026-07-04
- 已消耗时间: 2026-06-01 → 2026-07-04 = 约 33 天
- M1 总时间: 2026-06-01 → 2026-11-30 = 约 182 天
- **时间消耗 = 33/182 ≈ 18%** (注: W9 末; 上一版周报用"30%"是按 T+0 = Sprint 立项日 2026-05-28 起算)

### 3.2 领先/落后幅度

- **时间消耗 18% (W9 W5 末), M1 工作进度 66%**
- **领先约 48pp** — 进度超前趋势良好
- 风险: M1-H PnL 看板 0/8 (8 条全未开始), 若 Sprint-3 结束仍 0/8, 领先幅度将大幅收窄

### 3.3 M1 关键路径 (W9 W5 → M1 达标)

```
ABI v0.5 全链路:
  老韩 RM v0.5 (W10 W1)
    → 老唐 audit chain replay verify (W10 W2)
      → 小卢 REST API 9 endpoint 接真 state (W10 W3)
        → W11 paper runtime 启动 (M4.5 首步)
          → 14 天 paper 跑 → M2 Sharpe gate 数据起步

PnL 看板 (M1-H):
  REST API W10 W3 就绪
    → 小苏 前端 UI v1 (W10 W4 - W11)
      → 小宫 dogfood (W11+)
```

---

## §4 当前 critical path

### 4.1 主链路

```
数据结构 SSOT
  → ABI v0.5 (OrderIntent + SignerV52 + AuditRecord)
    → RM v0.5 整合 (老韩 W10 W1)
      → audit chain replay verify (老唐 W10 W2)
        → REST API 9 endpoint 接真 state (小卢 W10 W3)
          → paper runtime W11 (老吴 Frankfurt + 老高 CI 干净)
```

### 4.2 下游 blockers (5 项)

| Blocker | 状态 | Owner | ETA | 升级路径 |
|---|---|---|---|---|
| AWS Frankfurt server 购买 | 等老吴 AWS 账号权限确认 + W10 W3 时间窗口 | 老吴 + 老周 | W10 W3 | 老吴 → 老周 → 老胡 4h 协调 → 老雷 |
| 数据结构 IC 8/1 入职 | 小林 W9 W1 JD 已发布; 招聘周期估 4 周 | 小林 | 2026-08-01 | 小林 → 老雷 (如招聘 delay) |
| CI 累积 fail | 老高 Wave 81 跑中; W10 W4 目标 CI main 全绿 | 老高 | W10 W4 | 老高 → 老郭 → 老胡 |
| WAL-B01 依赖 ADR-017 | 小石 SPSC framework 接口锁定 (W9 W1 confirm 状态不明) | 小石 + 老王 | W9 W5 末确认 | 老王 → 老周 → 老胡 |
| 多人讨论会 W10 W1 才能定 W10 plan | 本 audit 是输入; 会议 W10 W1 召开 | 老胡 (主持) | W10 W1 (Mon) | — |

### 4.3 Stage-Gate 状态

| Gate | 进度 | 触发条件 | ETA |
|---|---|---|---|
| G1 (M1 MVP ≥ 90%) | 66% → 需 34pp | 38 acceptance 中 34 条 ✓ | 2026-11-30 |
| G3 (M4.5 paper 14 天) | 12% | paper runtime W11 启动后计时 | Sprint-3 W11+ |
| STG-001 GM 拍板 §3.1 授权范围 | 未解 | 老雷 ack | 待 W10 W1 多人讨论会 |

---

## §5 各部门 W10 W1 候选 backlog (待多人讨论会决议)

> 以下为 audit 建议票, 非 plan 决议。W10 W1 多人讨论会后各主管拍各自单元活。

### C 量化研究 (老板批评 P0 补活)

| 候选 ticket | Owner 建议 | 优先级 | 依赖 |
|---|---|---|---|
| P0-02 spec v0.2 (alpha v2 + C2 6¢ + 死区 25% + Soccer/Tennis 扩展入口) | 小程 | P0 | 无 (spec 工作) |
| backtest framework cpp 实施 (接 WAL-B02 W10 依赖) | 小蒋 | P1 | WAL-B02 W10 W1 |
| FillRateModel cpp 完整 + VirtualMatcher Mode A 灰度接入 | 小袁 | P1 | 老郭 ADR-007 时机 ack |
| Sprint-4 信号探索方向 spec (Soccer 3-way / Tennis Moneyline / NBA props) | 小梁 统筹 | P1 | 老钱 CPO 产品方向 ack |
| Goalserve odds quality 持续监控 + vig 分布周报 | 老彭 | P2 | 无 |

### E 产品业务保障 (M1-H PnL 看板 0/8 补活)

| 候选 ticket | Owner 建议 | 优先级 | 依赖 |
|---|---|---|---|
| 验收 spec v2 (M1 38 条 update, ABI v0.5 新字段 token_id/outcome 对齐) | 小颖 | P0 | 无 (spec 工作) |
| chaos + replay test W10 framework (给 W11 paper runtime 预备) | 小宋 | P0 | 无 |
| PRD 信号扩展 (Soccer 3-way / Tennis / NBA props 盘口规则) | 小杜 | P1 | 老钱 CPO 方向 ack |
| 前端 UI v1 (PnL 看板 6 endpoint, REST API W10 W3 就绪后接) | 小苏 | P1 | REST API W10 W3 |
| UX 评估计划 v1 + W11 dogfood 准备 checklist | 小尤 | P2 | 无 |
| W11 dogfood 执行预热 (paper runtime 前置读 checklist + 了解系统) | 小宫 | P2 | 无 |

### F 顾问团 (老板 verbatim "顾问们别闲着")

| 候选 ticket | Owner 建议 | 优先级 | 依赖 |
|---|---|---|---|
| C++20 架构兼容性独立审查 v1 (Rust 退场后新职责落 doc) | 老张 | P1 | 老郭 routing 确认 |
| AI/LLM in pipeline — Sprint-3 PoC 点建议 (anomaly detection / signal augmentation 二选一) | 老何 | P1 | 无 |
| ML pipeline + PositionManager 衔接接口 gap doc (ONNX 推理接 C++ 路径) | 小邓 | P1 | 无 |
| 工具栈升级建议 v2 (DuckDB ETL 深度集成 + Parquet 分区方案) | 老徐 | P2 | 无 |
| security audit 预审 doc v1 (G4 上线前必关闭 security gap 清单) | 小白 | P1 | 无 |
| 老高 CI W10 W4 main 干净 + chaos test framework (已在 W10 plan) | 老高 | P0 | 已派 |
| 老钱 CPO 盘口扩展优先级 input (T+12 月后 Soccer/Tennis vs NBA 优先级数字论据) | 老钱 | P1 | 多人讨论会议程项 |

### D 数据基础设施 (小董/小田 #24 补活)

| 候选 ticket | Owner 建议 | 优先级 | 依赖 |
|---|---|---|---|
| stats validation framework W10 (A/B test 框架, 供 paper runtime 数据质量检验) | 小董 | P1 | 无 |
| ML data pipeline W10 (Parquet 接小邓 ONNX hook, 离线训练数据准备) | 小田 #24 | P1 | 小邓 ONNX hook 接口确认 |
| ETL 跨源 mapping enforce (小段 ack 后实施) | 小余 统筹 | P1 | 小段 ack |

### A 系统工程 (Idle 人员补活)

| 候选 ticket | Owner 建议 | 优先级 | 依赖 |
|---|---|---|---|
| 小马 hot path latency budget doc (配合老姜 W10 latency enforce) | 小马 | P2 | 老姜 framework W10 W2 |
| 小赵 simdjson 序列化 benchmark (在 REST handler + 事件解析中的 p99 contribution) | 小赵 | P2 | 老姜 perf framework |
| 小肖 Kelly 定价 spec 起草 (C 单元信号探索方向确定后衔接) | 小肖 | P2 | C 单元 W10 方向 ack |

---

## §6 多人讨论会议程 (W10 W1 召开)

- **主持:** 老胡 (E 主管, PM)
- **必参:** GM 老雷 + 5 主管 (老周/老韩/小梁/小余/老胡) + 老郭 (F 协调)
- **列席:** 老钱 (CPO) + 9 顾问 (老张/老何/老钱顾问角色/老高/小邓/老叶/老徐/小白) + 小林 (HR)
- **时长:** 90 min (建议)
- **地点:** 主管周同步 + 顾问列席扩大版

### 议程

| # | 议题 | 主讲 | 时长 | 产出 |
|---|---|---|---|---|
| 1 | §1-2 本 audit 项目状态 + Idle 汇总 (老板 verbatim 直接对应) | 老胡 | 15 min | 全员对齐当前 Idle 问题 |
| 2 | 顾问团 W9 W5 末 4 份调研 input (老李 WSS Spec / 小段 Goalserve SSOT / 老彭 vig 实证 / 老郭 顾问意见箱) | 老郭 aggregate | 15 min | 调研结论入 W10 plan |
| 3 | C 单元 W10 W1-W4 plan 决议 (P0-02 spec / backtest / FillRateModel / Sprint-4 方向) | 小梁 主持 | 15 min | C 单元 W10 confirmed backlog |
| 4 | E 单元 W10 W1-W4 plan 决议 (验收 spec v2 / chaos test / PRD / 前端启动时机) | 老胡 主持 | 10 min | E 单元 W10 confirmed backlog |
| 5 | F 顾问团 W10 独立 deliverable 认领 (老张/老何/小邓/老徐/小白 各表态) | 老郭 协调 | 10 min | F 单元 W10 顾问 deliverable list |
| 6 | A/D Idle 人员 W10 ticket 确认 (小马/小赵/小董/小田 #24) | 老周/小余 | 10 min | A/D W10 补活确认 |
| 7 | W10 plan v2 final 确认 (替代 sprint-03-w10-plan.md Draft) | 老胡 | 10 min | W10 plan v2 全员 ack |
| 8 | 副总裁 7/1 入职 onboard checklist 确认 | 小林 | 5 min | onboard checklist ack |

### 前置材料 (W10 W1 会议前各方准备)

| 材料 | 准备人 | 截止 |
|---|---|---|
| 本 audit doc (本文) | 老胡 | W10 W1 Mon 前 (已完成) |
| C 单元 W10 候选 backlog 小梁内部排序 | 小梁 | W10 W1 Mon 9:00 前 |
| F 顾问团 W9 意见箱汇总 (老郭 aggregate) | 老郭 | W10 W1 Mon 9:00 前 |
| 副总裁 onboard checklist 草稿 | 小林 | W10 W1 Mon 9:00 前 |
| 老钱 CPO 盘口扩展优先级 input (数字论据) | 老钱 | W10 W1 Mon 9:00 前 |

---

## §7 风险 Registry update (W9 W5 末)

| 风险 ID | 描述 | 级别 | W9 W5 末状态 | Owner |
|---|---|---|---|---|
| R-001 | ABI 修复 6 deliverable — PositionLedger/ABI v0.5 W9 已 merge, 剩 RM v0.5/audit replay W10 | P0 | **整改中** (W9 W4 进展 +, W10 W1-W3 收尾) | 老胡 |
| R-IDLE-C | C 单元 4 IC W9 W2-W4 全 Idle (老板批评直接点名) | **P0** | **新增 / 活跃** | 小梁 + 老胡 |
| R-IDLE-E | E 单元小颖/小杜/小宋 W9 无活 (M1-H 0/8 重灾区) | P1 | **新增 / 活跃** | 老胡 |
| R-IDLE-F | F 顾问团 5 人 W9 W5 末无 deliverable (老板 verbatim "顾问们别闲着") | P1 | **新增 / 活跃** | 老郭 |
| R-W10-CHAIN | 小卢 REST W10 W3 依赖 RM v0.5 + audit chain W10 W2 — 任一 delay → REST 推 W11 | P1 | 活跃 (持续跟踪) | 老胡 |
| R-W10-FRANKFURT | Frankfurt server 购买 delay → W11 paper runtime hold | P1 | 活跃 | 老吴 + 老周 |
| R-W10-CI | Wave 81 后 CI 仍有遗留失败 → W10 W4 CI 干净目标 miss | P1 | 活跃 (老高 W10 W4) | 老高 |
| R-STAGE-GATE-001 | STG-001 GM 老雷 §3.1 授权范围未拍板 → Stage-Gate hold | M | 未解 | 老胡 (跟进) |
| R-PLAN-001 | W10 plan 1 人定 (老板批评) → 已降 Draft, 多人讨论会 W10 W1 修正 | P0 | **整改中** | 老胡 |

---

## §8 完成汇报

- **ADR-029 流程:** 本 audit doc 在 worktree `agent-a671e495d8fd63466` 撰写, 走 push + gh pr create
- **ADR-027 cite:** N/A (本文为 PM audit doc, 无核心数据结构 ABI 改动)
- **主管 cpp 行数:** 0 (老胡 E 主管, 本 wave 全为 doc 产出)
- **Opus 使用:** 0 (全程 Sonnet 4.6, ADR-009 v2)

**本 audit 用途:** W10 W1 多人讨论会输入材料。会议 ack 后 W10 plan v2 final 由老胡更新 sprint-03-w10-plan.md。

---

*老胡, 2026-05-29 (Wave 84, Sprint-3 W9 W5 末全量状态 audit)*
