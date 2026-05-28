# 员工花名册 (Employee Registry)

> **Owner:** 小林 (HR)
> **维护频率:** 任何入/离/状态变更 24h 内更新
> **建立背景:** 2026-05-28 用户指令"新招聘的同事必须通过人事注册登记。 不然时间长你都忘了" → GM 错 #6 制度缺失补救
> **登记原则:**
> - **每个 persona** (含 IC pool 10 人) **独立行**
> - 入职日 / 工号 / 战斗单元 / 当前状态 / 离职日 (如适用) 必填
> - **新人入职前** HR 必须先在本表登记, 才能在 `.claude/agents/NN-xxx.md` 建 persona file 并出 prompt
> - file #35 IC pool 10 人各自独立一行 (小卢-01 ... 小卢-10), 不混着写

---

## 总裁办公室 (President's Office) — 最高管理层

| 工号 | 入职 | Persona | name (file) | 单元 | 职位 | 当前状态 |
|---|---|---|---|---|---|---|
| P-00 | 2026-05-29 | 总裁 (Claude) | president (新建, 待小米/老徐落地) | 总裁办公室 | **总裁 (President)** | Active — 老板 verbatim 任命 2026-05-29 |
| P-01 | (目标 2026-07-01) | 副总裁 (VP, 待命名) | vice-president (入职后建 persona file) | 总裁办公室 | **副总裁 (Vice President)** | Pending Hire (P0 招聘, JD 2026-05-29 发布) |

> **HR 小林 (E-046) 签字 — P-00 登记确认: 2026-05-29**
> P-00 为老板 verbatim 任命 (2026-05-29), 总裁 ack 后 CLAUDE.md / AGENT.md 同步更新.
> P-01 入职日确认后转 Active, 届时建 persona file + 分配正式中文名.

---

## 现有员工 (founding cohort 2026-05-28)

### A. 系统工程部 (Owner: 老周)

| 工号 | 入职 | Persona | name (file) | 单元 | **职位** | 当前状态 |
|---|---|---|---|---|---|---|
| E-001 | 2026-05-28 | 老周 | cpp-chief-architect | A | **Manager** | Active (主管, ADR-005) |
| E-002 | 2026-05-28 | 小马 | cpp-hot-path-engineer | A | Active |
| E-003 | 2026-05-28 | 老陈 | cpp-network-engineer | A | Active |
| E-004 | 2026-05-28 | 小赵 | cpp-serialization-engineer | A | Active |
| E-005 | 2026-05-28 | 老王 | cpp-persistence-engineer | A | Active |
| E-006 | 2026-05-28 | 老孙 | crypto-signing-expert | A | Active |
| E-007 | 2026-05-28 | 老李 | polymarket-protocol-expert | A | Active |
| E-008 | 2026-05-28 | 小田 | sports-market-expert | A | Active (兼 D 数据仓库) |
| E-010 | 2026-05-28 | 老吴 | linux-sre-devops | A | Active |
| E-011 | 2026-05-28 | 小郑 | observability-engineer | A | Active |
| E-039 | 2026-05-28 | 老姜 | performance-engineer | A | Active |
| E-041 | 2026-05-28 | 小石 | data-structures-expert | A | Active |
| E-042 | 2026-05-28 | 小肖 | senior-algorithm-engineer-a | A | Active |
| E-043 | 2026-05-28 | 小颜 | senior-algorithm-engineer-b | A | Active |
| E-035-01 | 2026-05-28 | 小卢-01 | senior-cpp-ic-pool (#35) | A | Active (IC pool) |
| E-035-02 | 2026-05-28 | 小卢-02 | senior-cpp-ic-pool (#35) | A | Active (IC pool) |
| E-035-03 | 2026-05-28 | 小卢-03 | senior-cpp-ic-pool (#35) | A | Active (IC pool) |
| E-035-04 | 2026-05-28 | 小卢-04 | senior-cpp-ic-pool (#35) | A | Active (IC pool) |
| E-035-05 | 2026-05-28 | 小卢-05 | senior-cpp-ic-pool (#35) | A | Active (IC pool) |
| E-035-06 | 2026-05-28 | 小卢-06 | senior-cpp-ic-pool (#35) | A | Active (IC pool) |
| E-035-07 | 2026-05-28 | 小卢-07 | senior-cpp-ic-pool (#35) | A | Active (IC pool) |
| E-035-08 | 2026-05-28 | 小卢-08 | senior-cpp-ic-pool (#35) | A | Active (IC pool) |
| E-035-09 | 2026-05-28 | 小卢-09 | senior-cpp-ic-pool (#35) | A | Active (IC pool) |
| E-035-10 | 2026-05-28 | 小卢-10 | senior-cpp-ic-pool (#35) | A | Active (IC pool) |

### B. 风控合规部 (Owner: 老韩)

| 工号 | 入职 | Persona | name (file) | 单元 | 当前状态 |
|---|---|---|---|---|---|
| E-009 | 2026-05-28 | 老韩 | risk-engineer | B | **Manager** | Active (主管, ADR-005) |
| E-027 | 2026-05-28 | 老沈 | security-engineer | B | Active |
| E-029 | 2026-05-28 | 老黄 | compliance-legal | B | Active (合规延后, 当前低载) |
| E-038 | 2026-05-28 | 老唐 | audit-expert | B | Active |

### C. 量化研究部 (Owner: 小梁)

| 工号 | 入职 | Persona | name (file) | 单元 | 当前状态 |
|---|---|---|---|---|---|
| E-018 | 2026-05-28 | 小梁 | financial-expert | C | **Manager** | Active (主管, ADR-005) |
| E-019 | 2026-05-28 | 小程 | quant-signal-research | C | Active |
| E-020 | 2026-05-28 | 小蒋 | quant-backtest | C | Active |
| E-021 | 2026-05-28 | 小袁 | quant-microstructure | C | Active |
| E-030 | 2026-05-28 | 老彭 | betting-industry-expert | C | Active |

### D. 数据基础设施部 (Owner: 小余)

| 工号 | 入职 | Persona | name (file) | 单元 | 当前状态 |
|---|---|---|---|---|---|
| E-022 | 2026-05-28 | 小余 | data-etl | D | **Manager** | Active (主管, ADR-005) |
| E-023 | 2026-05-28 | 小董 | data-stats | D | Active |
| E-024 | 2026-05-28 | (小田, 见 E-008) | data-warehouse | D | Active (兼 A) |
| E-037 | 2026-05-28 | 小段 | goalserve-api-watch | D | Active |
| E-034 | 2026-05-28 | 小冯 | api-watch-general | D | Active |

### E. 产品业务保障部 (Owner: 老胡)

| 工号 | 入职 | Persona | name (file) | 单元 | 当前状态 |
|---|---|---|---|---|---|
| E-026 | 2026-05-28 | 老胡 | pm-project-manager | E | **Manager** | Active (主管, ADR-005) |
| E-025 | 2026-05-28 | 小颖 | requirements-analyst | E | Active |
| E-036 | 2026-05-28 | 小杜 | product-manager | E | Active |
| E-028 | 2026-05-28 | 小宋 | test-replay-engineer | E | Active |
| E-012 | 2026-05-28 | 小苏 | frontend-engineer | E | Active |
| E-040 | 2026-05-28 | 小米 | doc-curator | E | Active |
| E-046 | 2026-05-28 | 小林 | hr-talent-manager | E (虚线) | Active (HR Owner, GM 直属) |
| E-047 | 2026-05-28 | 小尤 | ux-experience-evaluator | E | Active |
| E-048 | 2026-05-28 | 小宫 | dogfood-tester | E | Active |

### F. 顾问团 (老雷直属)

| 工号 | 入职 | Persona | name (file) | 单元 | 当前状态 |
|---|---|---|---|---|---|
| E-013 | 2026-05-28 | 老张 | rust-advisor | F | **Inactive** (撤 Rust 决议, 2026-05-28 起 Standby) |
| E-014 | 2026-05-28 | 老何 | modern-cpp-advisor | F | Active |
| E-015 | 2026-05-28 | 老钱 | cpo-product-strategy | F | Active (CPO 平级) |
| E-016 | 2026-05-28 | 老郭 | chief-architecture-reviewer | F | **F-Coordinator** | Active (一票否决权 + 顾问团协调, ADR-005) |
| E-017 | 2026-05-28 | 老高 | code-quality-reviewer | F | Active |
| E-031 | 2026-05-28 | 小邓 | ml-engineer | F | Active (ML v2 / M4.5 后激活) |
| E-032 | 2026-05-28 | 老叶 | defi-onchain-advisor | F | Standby (上链 deferred 至 M4.5) |
| E-033 | 2026-05-28 | 老徐 | ai-ops-collaboration | F | Active |
| E-044 | 2026-05-28 | 小白 | ai-llm-advisor | F | Active |

### G. GM

| 工号 | 入职 | Persona | name (file) | 单元 | 当前状态 |
|---|---|---|---|---|---|
| E-045 | 2026-05-28 | 老雷 | professional-manager | G | Active (GM) — **待总裁 P-00 ack 选项 A/B/C 后更新状态 (HR 推荐 A: Superseded by P-00)** |

---

## 待入职 (Pending Hires)

| 候选工号 | Persona (临名) | name (file 计划) | 单元 | JD 状态 | 评委 | 目标入职 | 兜底 | 当前 |
|---|---|---|---|---|---|---|---|---|
| **P-01** | **副总裁 (VP, 待命名)** | **vice-president** | **总裁办公室** | **JD 已发布 2026-05-29 (P0)** | 小林初面 / 老雷+老郭深度 / 老胡+老钱 / 总裁+老板终面 | **2026-07-01** | 2026-07-15 | 老板 verbatim 指令, 互相监督机制必要角色 |
| E-049 | 老冀 | onchain-ops-engineer | A | **JD 未发布 (5/29 评委追单 P0)** | 老周 / 老韩 / 小梁 (待 ack) | 2026-06-30 | 2026-07-15 | HC-01 候选人池 0 |
| E-050 | 小秦 | strategy-execution-engineer | C | **JD 未发布 (5/29 评委追单 P0)** | 老周 / 小梁 / 老韩 (待 ack) | 2026-06-30 | 2026-07-15 | HC-02 候选人池 0 |
| E-051 | 小吕 | quant-engineer | C | **W4 起草 (HR 主动提前)** | 小梁 / 老韩 (待派) | 2026-08-01 (提前自 Q3) | 2026-09-01 | HC-03, M4.5 gate 倒推 |
| E-052 | 待命名 | wal-storage-engineer | A | **W4 EOW 起草 (老周提请, 老板 5/28 ack)** | 老周 / 老王 / 老姜 | 2026-08-15 | 2026-09-15 | HC-04, 老王单点 P1 红 |
| E-053 | 待命名 | observability-2 | A | **W4 EOW 起草 (老周+老吴提请, 老板 5/28 ack)** | 老周 / 小郑 / 老吴 | 2026-08-15 | 2026-09-15 | HC-05, 小郑单点 P1 红 |
| E-054 | 待命名 | ml-data-engineer | D | **W4 EOW 起草 (小余+小邓提请, 老板 5/28 ack)** | 小余 / 小邓 / 小田 | 2026-08-15 | 2026-09-15 | HC-06, 跨边界 |
| E-055 | 待命名 | qa-integration-engineer | E | **W4 EOW 起草 (老胡+老周提请, 老板 5/28 ack)** | 老胡 / 小宋 / 老周 | 2026-08-15 | 2026-09-15 | HC-07, 小宋单点 P1 红 |
| E-056 | 待命名 | data-structures-ic | A | **W9 JD 发布 (小林 Wave 48 P0, 2026-05-29)** | 老周 / 老郭 / 小石 | 2026-08-01 | 2026-08-22 | 数据结构 IC, ADR-027 §4.2 FOM approve 缺位 P0 |
| E-057 | 待命名 | test-replay-engineer-2 | E | **W9 JD 发布 (小林 Wave 48 P1, 2026-05-29)** | 小宋 / 老胡 | 2026-08-15 | 2026-09-01 | HC-08 test-replay, Sprint-3 W10 onboarding, R-002 P1 |

---

## 离职 / 状态变更 log (Empty)

> 任何 persona 变更 (Inactive / 离职 / 转岗) 在此 append, 包括日期 + 原因 + 影响.
> 当前: 无 (founding cohort 全员 Active 第 1 天).

---

## HR 注册流程 SOP (新人入职硬流程)

1. **GM 批准 HC** → 在 `docs/HIRING/backlog.md` 上调度
2. **小林 (HR) 起草 JD** (≤ 1 周内)
3. **3 评委 ack** (Owner + 1 跨单元 + 1 顾问)
4. **JD 发布 + 候选人池建立**
5. **入职日 - 7 天**: HR 把新员工 append 到本 registry (状态 Pending → Active)
6. **入职日**: HR 建 `.claude/agents/NN-<role>.md` persona file
7. **入职日 + 1 天**: GM 在派单时优先用新人, 测试 persona 边界
8. **入职日 + 7 天**: HR 跑 1:1, 评估融入 (status 由 Pending Active 转 Active)

**离职 / 转岗 SOP:**
- 任何 persona 状态变更 → append "离职 / 状态变更 log" 段
- 5 个工作日内 GM 在 AGENT.md 同步, 小米 docs 同步
- 历史 persona file **不删**, 加 `**Deprecated since YYYY-MM-DD**` 头部声明

---

## CI / 自动核数学

- `tests/ci_grep/registry_consistency.py` (W4 小宋 加): registry persona 数 = AGENT.md persona 数 = `.claude/agents/*.md` persona 数 (file #35 算 10) ✓
- 任意 commit 改 AGENT.md 或 `.claude/agents/` 必须连带改 registry, 否则 CI fail

---

**首次建立:** 2026-05-28 by 老雷 + 小林 (用户指令"新招聘必通过人事注册登记"触发)
**最后更新:** 2026-05-29 by 小林 (HR) — Wave 9: 加 P-00 总裁就任 + P-01 副总裁 Pending Hire (老板 verbatim 任命)
**维护人:** 小林 (HR), 总裁 P-00 review (原 GM 老雷 review 职权顺延至总裁, 待选项 A/B/C ack)
