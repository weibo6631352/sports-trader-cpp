# 老韩 B 风控合规部主管 mandate v1

- **Owner:** 老韩 (E-009, risk-engineer, B 单元 Manager)
- **Date:** 2026-05-28
- **关联:** ADR-005 (主管制度) / ADR-004 (position_caps 先) / ADR-003 closeout / RM v0.3 + v0.3.1 / 老唐 audit v1.1 / `docs/SPRINTS/sprint-02.md` W5 派单 / HR pulse 2026-05-28 (我 7/10 黄)
- **Last review:** 2026-05-28
- **Deadline:** ADR-005 §5 要求 W4 EOW 必交, 本档落

> 接 GM 业务目标后, 自己拆任务派 IC, 不让 GM 越过我直接派老沈 / 老黄 / 老唐. 本档是我 B 单元的"作战手册".

---

## §1 B 单元成员清单 (4 人, registry 对齐)

| 工号 | persona | file (.claude/agents/) | 单元 | 当前职位 | 当前状态 |
|---|---|---|---|---|---|
| E-009 | 老韩 | 09-risk-engineer.md | B | **Manager** | Active (主管 ADR-005, 兼 RM owner) |
| E-027 | 老沈 | 27-security-engineer.md | B | Senior IC | Active (KMS / threat-model owner) |
| E-029 | 老黄 | 29-compliance-legal.md | B | IC | Active **低载** (合规延后, 撤地域 ADR 后 ToS / vendor 风险 only) |
| E-038 | 老唐 | 38-audit-expert.md | B | Senior IC | Active (audit schema / BLAKE3 / replay verifier owner) |

**单元定位 (RAIC 视角):**
- **老韩 = R** (Responsible, 主管 + RM owner, 决策 + 拍板 + first review)
- **老沈 = A** (Accountable, 安全 / KMS / SecureBuffer / threat model)
- **老唐 = A** (Accountable, audit schema / WAL record / replay)
- **老黄 = C** (Consulted, 合规边界 + ToS + vendor 风险, 当前低载)

**HR pulse 2026-05-28 自评:** 我 7/10 黄 (三角色叠加 — 主管 + RM owner + 跨单元 review), 老沈 / 老唐 / 老黄 见 §3.

---

## §2 我作为主管的 do (8) / don't (5)

### 2.1 Do (主管 8 项动作)

| # | 行为 | 频率 | 输出 |
|---|---|---|---|
| D1 | **接 GM 业务目标拆 IC 任务** (24h ack, 48h 拆出) | 每次 GM 派单 | 单元 backlog (Issue / ticket) |
| D2 | **RM C++ first review** (老唐 audit / 老沈 KMS / 老黄 ToS PR) | 每 PR | review 签字, 升老郭 / 老高 二审 |
| D3 | **跨单元接口对接** (与老周 RM 接 SlippageModel/WAL, 与小梁 signal output schema, 与老胡 integration test) | 周 1-2 次 | 接口契约文档 + 24h ack |
| D4 | **风控红线 enforce** (R-1/R-7/R-11/R-12/R-20 任一可单独叫停, 不需二次确认 — CLAUDE.md §6) | 触发即刻 | INCIDENT log + ADR if needed |
| D5 | **单元周会 + 1:1** (周五下午风控例会, 老沈/老唐每两周 1:1, 老黄按需) | 周 1 次例会 + bi-weekly 1:1 | 1:1 记录, KPI 反馈 |
| D6 | **RM 设计 spec owner** (v0.3 / v0.3.1 / v0.4 演进, 不写代码) | per Sprint | spec 文档 (`laohan-riskmanager-design-v*.md`) |
| D7 | **HR 联动 + HC 上呈** (HC-04 risk-quant Q3, 评估 HC-08 提前) | 月度 + 触发式 | HR pulse 反馈 + JD 评委 ack |
| D8 | **audit / 拒绝原因表 SSOT 维护** (21 enum + 9 INVALID_INTENT sub-reason, 任何改动经我) | 触发式 | RejectCode 枚举表 + audit 字典 |

### 2.2 Don't (5 项明确不做)

| # | 不做的 | 理由 | 谁做 |
|---|---|---|---|
| N1 | **不写策略本身** (策略 spec / Kelly 系数 / 阈值数值) | persona 边界 (`09-risk-engineer.md` "拒绝任务") + 主管不亲力亲为 (ADR-005) | 小梁 (C) 定阈值, 我 enforce |
| N2 | **不写签名实现 / KMS 代码** | persona 边界 + 老沈 / 老孙 (A) 主权 | 老孙 (signer), 老沈 (KMS spec) |
| N3 | **不替老雷拍战略** (例: paper-engine 9/12 首判窗口 / HC-04 提前与否 / 阈值红线数字) | ADR-005 §2.3 主管不替 GM 拍战略 | 老雷 (GM) + 老钱 (CPO) |
| N4 | **不写 C++ 代码** (例外: 架构原型 / <2h hotfix) | ADR-005 §2.2 第 6 条主管不亲力亲为 | 老沈 / 老唐 / IC pool (小卢) |
| N5 | **不替小梁 / 老周 / 老胡单方面承诺 IC 工作量** | ADR-005 §2.3 第 4 条 | 跨主管协商 24h ack |

---

## §3 单元内 IC 工作量评估 (0-10, 与 HR pulse 对齐)

| persona | 当前评分 | 颜色 | 主要负载 | 风险信号 | 我的干预动作 |
|---|---|---|---|---|---|
| **老韩 (我)** | **7/10** | 黄 | 主管 (协调 3 人) + RM owner (v0.3/v0.3.1 + v0.4 演进) + W4-01 evaluate() 1088 行 spec + ADR-004 配合 + W5 RG patch + 跨单元 review (小梁 signal output / 小蒋 paper / 老周 v0.6) | **三角色叠加** (主管 + IC owner + cross-review). v0.4 演进 + STRATEGY_DECAYED 实现到 W6 必出代码, 自己写+review+拍板叠加 | HR pulse 已上呈 (HC-04 Q3 保留, **建议 HC-08 risk-quant 提前评估**, 见 §8) |
| **老沈** | **6/10** | 黄/绿 | KMS v2 (撤地域 ADR 后简化到 AWS us-east-1 主 + us-west-2 备, 单 vendor) + threat-model v2 维护 + W4 末 PaperSigner mock review 已闭环 | 撤地域 ADR 后真 KMS unwrap deferred, 当前主要是 mock + threat-model maintenance, 负载下降 | W5 加派 R-7 build-time switch 联调 (与老孙 / 小蒋); W6+ 待 paper engine 联调结果定 Sprint-3 是否拉满 |
| **老唐** | **8/10** | 黄 | audit schema v1.1 (4 ts + 14 AET) 已交 + W4-02 audit_writer 896 行 + 24 测试 + **W5 真 BLAKE3 落地 + reason_sub_reason field id=51 联签 + replay verifier invariant** | W4 audit_writer 落地超额, W5 还要接真 BLAKE3 (撤 fake hash) + replay verifier 写完; Sprint-3 还有审计回放工具 | W5 派单见 §4 W5-B-02. 不再叠加新事, 跟我 first review 节奏 |
| **老黄** | **3/10** | 绿 | 撤地域 ADR + Sygnum deferred 后真活只剩 ToS audit + vendor 商务 review (Goalserve odds 商务方案 Compromised) | 当前低载, Sprint-2 W2 之后基本无 critical task | 不强加, 让老黄主推 Sprint-3 vendor ToS 系统化梳理 (Polymarket / Goalserve / Polygon / vendor KMS); 跟 HR 小林 evaluate 是否 Q4 临时调岗 |

**单元总负载:** 平均 6.0/10, 中位数 6.5. 比 A (老周 9/10 红) / C (小梁 8/10 黄+) 轻, 但我个人 7/10 是上限边界, **+ STRATEGY_DECAYED 实施 + multi-signal P0-02~P0-06 上线就会破 8**.

---

## §4 W5 派单 backlog v1 (2026-07-06 Mon → 2026-07-10 Fri)

> **方法论:** 不让 GM 拆, 我接 GM 上层目标 (M1 节点 + ADR-004 patch + audit 真 BLAKE3 + R-7 build-time switch 联调 + INVALID_INTENT enum 统一 patch) 后拆到 B 单元 IC + 跨单元请求.

### 4.1 B 单元内部派单 (4 项)

| # | Owner | 交付 | 截止 | 验收人 | 状态 |
|---|---|---|---|---|---|
| **W5-B-01** | 老韩 (我) | **ADR-004 closeout patch — RiskGateway::evaluate() 短路顺序 cpp 落地** (`check_position_caps_` 前移至 `check_liquidity_` 之前; `evaluate_priority_position_cap_before_liquidity_test.cc` 加测试; spec §3.10 加引用; 见 ADR-004 §6 落地动作 1-4) | 7/8 (Tue) | 老郭 + 老高 PR review | **Agreed** |
| **W5-B-02** | 老唐 + 老韩 (我 review) | **audit emitter 真 BLAKE3 落地** (W4-02 sketch 中 fake hash 替换成 libb3 实现 + `reason_sub_reason` field id=51 contract test + replay verifier invariant `code != 17 ⟹ sub_reason == 0`) | 7/10 (Fri) | 老韩 + 老郭 + 小宋 (replay 联调) | **Agreed** |
| **W5-B-03** | 老沈 + 老唐 (老韩 review) | **R-7 build-time switch 联调** (与老孙 PaperSigner mock + 小蒋 paper engine; 验证 paper build 与 live build 在 audit emitter / signer 路径分流正确, 不交叉; 联签 ADR-003 R-7 闸 2 闭环) | 7/10 (Fri) | 老郭 + 小蒋 + 老孙 | **Agreed** |
| **W5-B-04** | 老韩 (我) | **INVALID_INTENT enum 17/19 统一 patch** (v0.3.1 §3.10.x 写的是 enum 17, 与 spec 中其他位置可能漂移; 写 1 页 "RejectCode 21 enum 终版表" 落 `docs/RESEARCH/laohan-rejectcode-final-21-table-v1.md`, 修齐 v0.3 / v0.3.1 / ADR-004 / audit schema 之间数字一致) | 7/8 (Tue) | 老唐 + 老郭 + 小宋 | **Agreed** |

### 4.2 B 单元主管行政 (3 项, 我自己)

| # | 任务 | 截止 | 输出 |
|---|---|---|---|
| W5-B-M1 | 老沈 / 老唐 / 老黄 1:1 (bi-weekly) | 7/10 (Fri) EOD | 3 份 1:1 记录, 标红黄绿 + 干预 |
| W5-B-M2 | 风控周会 (周五下午) — W4 落代码战果复盘 + W5 R-7 闸 2 联调 + Sprint-3 backlog v0.1 | 7/10 (Fri) 14:00 | 会议纪要 + Sprint-3 B 单元 backlog v0.1 |
| W5-B-M3 | HC-04 risk-quant Q3 评委 ack + **HC-08 提前评估** (上呈 HR 小林, 见 §8) | 7/10 (Fri) | 评委 ack 邮件 + HC-08 提前可行性 1 页纸 |

### 4.3 待跨单元协调 (4 项, 见 §5)

- ASK-A1: 老周 v0.6 PREGAME_FAR 15000ms 取齐 + RM 接 SlippageModel / WAL / PaperSigner header
- ASK-C1: 小梁 signal output 接 RM RiskIntent schema
- ASK-E1: 老胡 integration test fixture (paper E2E 60s + audit chain verify)
- ASK-F1: 老郭 ADR-004 closeout + ADR-003 final closeout 协助

### 4.4 W5 不接 / 推迟

- v0.4 RM spec 演进 → **Sprint-3 启动**, W5 不动. 理由: W5 重点是 W4 代码 v0.1 收口 + M1 评审, v0.4 spec 演进会撕 RM 进 main 节奏.
- STRATEGY_DECAYED kill switch C++ 实施 → **W7+** 等小董 `bayes_decay_monitor.py` MVP (W3-10 派单) 出数据后再实施, W5 spec 已稳.

---

## §5 跨单元接口需求 (24h ack 期望)

| ASK# | 对方主管 | 我需要的 | 我提供的 | deadline | 升级路径 |
|---|---|---|---|---|---|
| **ASK-A1** | 老周 (A) | (a) v0.6 §17.6.1 PREGAME_FAR HALT 从 30000ms 改齐到 15000ms 确认 (ADR-003 C-3); (b) RM 接 SlippageModel header 路径 (小肖 `xiaoxiao-slippage-model-lib-v1.md`) + WAL writer (老王 W2-03) + PaperSigner mock (老孙 W3-01) 三接口稳定性承诺; (c) v0.6 整体架构 review RM v0.3.1 是否需 v0.4 演进 | RM v0.3.1 spec + ADR-004 patch + 21 enum 终版表 (W5-B-04) | 7/6 (Mon) ack, 7/8 (Tue) 落定 | 升老雷 |
| **ASK-C1** | 小梁 (C) | signal_engine 输出 schema (P0-01 / P0-02..) 必含 `signal_id` / `model_version` / `confidence` / `expected_edge_bps` / `inference_ts` / `feature_snapshot_id` (R-20 4 ts 配合); enum 边界对齐 INVALID_INTENT.sub_reason 不冲突 | RM 接 signal struct 字段清单 (从 RiskIntent 视角) + INVALID_INTENT sub_reason 5 子原因 | 7/6 ack, 7/9 落定 | 升老雷 |
| **ASK-E1** | 老胡 (E) | (a) integration test fixture: `paper_audit.wal verify` (W5-07bis 已派小宋, 我配合) — 我需要小宋出"60s E2E + chain verify + 4ts 单调 + AET 12 类全到位"测试模板; (b) Sprint-3 risk-quant 入职后 onboarding 计划 (HC-04 Q3) | RM evaluate() 21 enum + 9 sub_reason 测试矩阵 (我给小宋写测试 spec) | 7/6 ack, 7/10 落定 | 升老雷 |
| **ASK-F1** | 老郭 (F-Coord) | (a) ADR-004 closeout 协助 (W5-B-01 上线后 ADR §6 落地动作 1-5 关闭); (b) ADR-003 final closeout (C-1 ~ C-5 全闭); (c) Sprint-3 ADR-006 立 (STRATEGY_DECAYED kill switch 触发参数 review) | 已 patch 的 RG cpp + 21 enum 终版表 + audit verifier invariant 实现 | 7/8 (Tue) ADR-004 关; 7/10 (Fri) ADR-003 final closeout | 升老雷 |

**ack 期望:** 24h ack, 48h 给 first response, 不下走 §6 决策升级.

---

## §6 主管 KPI 自评 (基于 ADR-005 §2.2 主管 6 职责)

| 职责 | 自评 | 证据 / 行动 | 缺什么 |
|---|---|---|---|
| 1. 接 GM 目标拆任务 24h ack | **A-** | W5 派单 backlog (本档 §4) 自拆完成, 不让 GM 派 IC | 24h ack 机制需配自动化 (Sprint-3 跟小米 + 老胡 整 ticket 模板) |
| 2. 单元内排队 + 优先级 | **A** | §4 7 项 ticket 已优先级排序, ADR-004 patch (W5-B-01) > 真 BLAKE3 (W5-B-02) > R-7 联调 (W5-B-03) > enum patch (W5-B-04) | 待 Sprint-3 把"老唐 W5+ 真 BLAKE3"扩展到 audit replay 工具 |
| 3. review + 质量门禁 | **B+** | W4 老唐 audit + W3 老沈 KMS review 都过我; ADR-004 / ADR-003 整改三方联签 | W5 真 BLAKE3 + R-7 联调 review 节奏要快, 不能成 bottleneck (个人 7/10 风险) |
| 4. 跨单元接口对接 | **B** | §5 4 项 ASK 主动发起, 不等 GM 派 | 历史上跟 D 单元小余沟通不够 (R-20 4 ts 是老胡 + 老郭推的, 不是我推的) — 改: W5 跟小余建直接接口 |
| 5. KPI + 1:1 + 培养 | **B** | 老沈 / 老唐 / 老黄 bi-weekly 1:1 计划 (W5-B-M1) — 之前不规律, 现在固化 | 老黄低载 3/10 是个隐患, Q4 调岗 / 临时项目分配 还没想清楚, 跟小林 1:1 |
| 6. 不亲力亲为 | **B-** | W4-01 evaluate() 1088 行 + W4-02 audit_writer 我也写了, 这是亲力亲为 (虽然是 ADR-003 整改高优, 但越了主管边界) | **W5 起严格执行**: ADR-004 patch 我**只**写 spec 整改单 + first review, **不**自己改 cpp (让老唐 / IC pool 实施); 个人 KPI 自定 W5 cpp lines = 0 (例外: 架构原型 / <2h hotfix) |

**总评:** B+ (Manager 角色 1 周, 还在转型. ADR-005 §2.3 主管不写代码这条我 W4 没守住, W5 起守).

**红线自检 (CLAUDE.md §6):** 我 / 老黄 / 老郭 任一可单独叫停, 不需二次确认 — 本周无需触发.

---

## §7 W5 GM 派单约定 (给老雷)

按 ADR-005 §3.1, GM 派 B 单元任务 → 必经我:

1. **GM → 老韩 (拆) → 老沈 / 老唐 / 老黄 (IC)**, 不绕路.
2. **例外** (ADR-005 §3.2): 紧急 P0 (RM HALTED / 安全事件 / 用户原话 < 2h 响应) / 我本人 / 跨多单元统筹 (老郭 / 老胡 协调).
3. **GM 派单 5 题自检 (CLAUDE.md §7-8) — 我 ack 派单前再过一遍:**
   - ① 一面之词背书? ② 单 agent 替全员? ③ 看老项目 / 撤销方案? ④ 越 persona 拒绝任务边界? ⑤ 越主管直派 IC?
4. **我 ack 期望:** 24h 内 ack (Slack + 跟单), 48h 内拆出 IC 任务并发 §4 backlog format.
5. **GM 直接派老沈 / 老唐 / 老黄, 我有权 (ADR-005 §3.3) 把任务拉回, 自己拆, 不算抗令.**

---

## §8 给 HR 小林 的扩招建议 (HC-08 提前评估)

### 8.1 背景 (基于 HR pulse 2026-05-28)

- 我当前 7/10 黄, **三角色叠加**: 主管 (协调 3 IC) + RM owner (spec + ADR) + cross-unit review (小梁 signal / 小蒋 paper / 老周 v0.6).
- **HC-04 risk-quant** (Q3 2026, 9 月入职目标) — 当前 backlog 中是 P1, 我同意维持 Q3, **不提前**. 理由: 当前 RM v0.3 已稳, STRATEGY_DECAYED 实施期 W6-W8, risk-quant 9 月入职刚好接 multi-signal 实施.
- **HC-08 data-scientist** (Q4 2026, 当前 P2, 原岗位为 C 量化研究) — **我建议评估是否调整成 B 单元归属或提前到 Q3 末**.

### 8.2 HC-08 提前 / 调岗的理由

**M4.5 倒推 (9/12 paper 首判 → 10/29 M4.5 gate → Sprint-4 实盘 + multi-signal 上线):**

1. **STRATEGY_DECAYED kill switch 实施期** (W6-W8) 需要 risk-quant 写 `bayes_decay_monitor` C++ 实现 (现在小董 W3-10 是 py MVP), HC-04 risk-quant 9 月入职刚好接.
2. **multi-signal P0-02 ~ P0-06 上线** (Sprint-3/4) 后, RM evaluate() 必须按 signal_id 分维度做 sizing + cap, **每个 signal 独立 Kelly + drawdown circuit breaker** — HC-04 risk-quant 1 人无法同时承载 5-6 个 signal 的规则引擎.
3. **HC-08 data-scientist 原岗 C 量化研究做赔率定价建模** — 与 risk-quant 分担 multi-signal 实施有协同 (data-scientist 出 signal 衰减模型, risk-quant 出 RM 规则引擎). **如果 HC-08 提前到 Q3 末 (10 月) 入职**, 跟 HC-04 形成 quant-risk pair.

### 8.3 给小林的具体建议

| 建议 | 优先级 | 依据 |
|---|---|---|
| **HC-04 risk-quant Q3 维持** (9 月入职), 我做 hiring manager + 评委 (与小梁 deep) | P1 confirm | HR pulse 已批, 我 ack |
| **HC-08 data-scientist 评估提前到 Q3 末 (10 月入职)** 或调整成 B 单元辅佐岗位 (risk-data analyst) | P2 evaluate | 见 §8.2 multi-signal 倒推, 老韩 + 小梁 + 小林三方 1:1 (Sprint-3 W1) 拍 |
| **HC-14 compliance-analyst (Q1 2027, P3)** — 撤地域 ADR 后**继续延后**到合规地区迁移决议出之前不招 | P3 defer | 撤地域 ADR §$jurisdictional-deferral$, 真活只有 ToS, 老黄 3/10 还能扛 |

### 8.4 风险信号 (我答 HR 小林)

- **若 HC-04 9 月入职受阻** (候选人池不足) → W7-W8 (8 月中下旬) 老韩自己写 risk-quant 规则引擎 → 我 8/10 → 9/12 paper 首判窗口黄→红.
- **若 HC-08 不提前** → Sprint-4 multi-signal 上线 + STRATEGY_DECAYED 联调时 (10 月底 ~ 11 月) RM owner 我成 bottleneck, 升级老雷.

**HR 跟我 bi-weekly 1:1 节奏不变** (我列入 §4.2 W5-B-M3).

---

## 完成汇报

**§1 B 单元 4 人, registry 对齐** — 我 (E-009 主管) + 老沈 (E-027 senior IC) + 老黄 (E-029 IC, 低载) + 老唐 (E-038 senior IC, 高产). RAIC: R= 我, A= 老沈 / 老唐, C= 老黄.

**§2 do 8 / don't 5** — 主管做接派单 / first review / 跨单元 / 红线 enforce / 1:1 / spec owner / HR 联动 / SSOT; 不做策略数值 / 签名实现 / 替老雷拍战略 / 写 cpp / 替他主管承诺.

**§3 工作量:** 老韩 7/10 黄 / 老沈 6/10 黄绿 / 老唐 8/10 黄 / 老黄 3/10 绿. 单元均值 6.0, 我个人是上限边界, multi-signal 上来会破 8.

**§4 W5 派单** — 4 项 IC 任务 (W5-B-01 ADR-004 cpp patch / W5-B-02 真 BLAKE3 + invariant / W5-B-03 R-7 build-time 联调 / W5-B-04 enum 17 终版表) + 3 项主管行政 (1:1 / 周会 / HC 上呈) + 4 项跨单元 ASK. 全 Agreed, 0 Compromised, 0 Escalated.

**§5 跨单元 ASK 4 项:** ASK-A1 老周 v0.6 + 三接口稳定 / ASK-C1 小梁 signal schema / ASK-E1 老胡 integration test fixture / ASK-F1 老郭 ADR closeout. 24h ack, 7/10 全部落定.

**§6 KPI 自评:** B+ (转型期, 主要扣分项是 W4 我自己写了 evaluate() + audit_writer 1088 + 896 行, 违反"主管不写代码"). W5 cpp lines 自定 = 0, 严格守.

**§7 GM 派单约定:** 5 题自检 + 24h ack + 我有权拉回越级派单. 不绕主管.

**§8 HC 建议:**
- HC-04 risk-quant **Q3 维持**, 9 月入职接 STRATEGY_DECAYED + multi-signal
- **HC-08 data-scientist 评估提前到 Q3 末 (10 月)** 或调整 B 单元 risk-data analyst (Sprint-3 W1 三方 1:1 拍)
- HC-14 compliance Q1 2027 继续 defer

**不替老雷拍:** 9/12 paper 首判窗口 / 阈值数值 / HC-08 是否真提前 / Sprint-3 ADR-006 (STRATEGY_DECAYED kill switch) 触发参数 — 这些 §4 §5 §8 都标"建议", 等老雷 W5 评审拍.

**不耻下问 (本档过程):**
- @小林 HC-08 提前 / 调岗可行性 (§8.3 Sprint-3 W1 三方 1:1)
- @老周 v0.6 PREGAME_FAR 15000ms 取齐 + RM 三接口稳定承诺 (ASK-A1)
- @小梁 signal output schema 字段对齐 (ASK-C1)
- @老胡 W5-B-03 R-7 联调 integration test fixture (ASK-E1)
- @老郭 ADR-004 closeout + ADR-003 final closeout 协助 (ASK-F1)

---

**END.** 等 ADR-005 §5 deadline (W4 EOW) 老雷 + 小林 ack, 我 W5 (7/6 Mon) 按本档执行.

— 老韩 (E-009, B 单元 Manager), 2026-05-28
