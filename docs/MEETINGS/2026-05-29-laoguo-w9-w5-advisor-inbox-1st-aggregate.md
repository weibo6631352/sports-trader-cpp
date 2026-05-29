---
owner: 老郭 (#46, F 协调人)
last_review: 2026-05-29
status: FINAL
doc_type: meeting-aggregate
wave: W9 W5
related:
  - docs/MEETINGS/2026-05-29-laoguo-advisor-pool-activation.md (Wave 53 framework)
  - docs/ADR/2026-06-W4-adr-029-worktree-push-pr-flow.md (ADR-029, 本文流程基线)
  - docs/OKR/laoqian-w8-w5-profitability-kr-v1.md (老钱 KR spec v1)
---

# 顾问团 第 1 期意见箱 Aggregate — W9 W5

**主持:** 老郭 (#46, F 协调人)
**日期:** 2026-05-29
**触发:** GM verbatim "顾问们也别闲着, 你们应该提需求提意见" + "结合新一轮的市场调研, 再制定目标计划"
**流程依据:** ADR-029 (push + PR flow); Wave 53 framework §2.1 意见箱节奏

---

## §1 9 顾问 第 1 期意见 (老郭 aggregate)

意见 ID 格式: `ADV-202605-NN`

---

### 老张 (#01, 架构兼容顾问) — Rust 退场后角色重定

**ADV-202605-01 [改进 / P2]**
Rust 已 GM 终决退场 (2026-05-28)。现存老孙 signer v1/v2/v3 + 老张 crate 选型文档作为知识沉淀保留，但实施走 C++（老孙 v4 重写）。老张顾问角色应正式重定：退场 Rust 顾问职能，转为 C++ 架构兼容性独立审查 (spec doc only + ADR cross-check)，Sprint-4 重评是否需要专职 modern-cpp advisor 补位。

**ADV-202605-02 [风险 / P2]**
老孙 signer v4 C++ 重写进度需监控。Rust→C++ 切换若 signer 上线节点滑坡，直接影响 G4 (M5 live 首笔成交) 时间线。建议老周 A 主管在 Sprint-2 W5 前给出 signer v4 交付 ETA，并在架构评审中列为追踪项。

---

### 老何 (#44, AI/LLM advisor) — AI/LLM gap

**ADV-202605-03 [需求 / P0]**
当前 signal pipeline (老彭 inplay alpha v2 + pregame de-vig 聚合) 为纯规则驱动。建议在 Sprint-3 启动 anomaly detection PoC: 用 ML 模型检测 odds 异常跳动（非 Goalserve 正常推送模式），作为信号质量过滤层接入 pregame alpha 前端。接口: 异常 score 输出给 C++ signal filter，无需改热路径。

**ADV-202605-04 [需求 / P1]**
sub-agent prompt engineering 质量影响系统并发效率（W3 仲裁记录已有多例 agent 边界越界）。建议老郭 + 老何在 W10 前输出 "顾问团 prompt calibration checklist v1"，减少跨单元派单 misfire 率。量化目标: 越界 wave 占比从 W3 实测约 15% 降至 < 5%。

---

### 老钱 (#15, CPO + strategy advisor) — 赛道扩展优先级

**ADV-202605-05 [需求 / P1]**
T+12 月赛道扩展三选一: Soccer / NBA / Tennis。CPO 推荐 **Soccer (足球) 优先**，数字依据: Polymarket 体育市场 Soccer 盘口日均成交量约为 NBA 的 2.3 倍（2025Q4 链上数据估算），且 Goalserve Soccer inplay feed 已在合同范围内无额外授权费用。Tennis 盘口流动性薄 (< $50K/日)，Moneyline 扩展价值低。建议 W10 多人讨论会确认路线图。

**ADV-202605-06 [风险 / P1]**
老钱 KR spec v1 (laoqian-w8-w5-profitability-kr-v1.md) 中 G3 OOS Sharpe ≥ 0.5 门槛待老叶独立审查（见 ADV-202605-11）。CPO 提醒: v1 已发出，老韩 + 小梁 ack 截止 2026-05-31，若两方均未 ack → 升老胡协调。

---

### 老郭 (#46, F 协调人) — 跨单元架构风险自评

**ADV-202605-07 [风险 / P0]**
**A 部 (老周) 与 D 部 (小余) 数据 schema 变更通知机制存在执行盲区。**
CLAUDE.md 红线 §8 明确: "数据 schema 静默变更（不通知下游）→ 责任人承担事故"。当前 ADR-027 (core-data-structure-ssot-enforce) 覆盖 OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot 六个核心 struct，CI grep 验证 PR cite。但 Goalserve inplay feed schema (小段 D-04) 与 ETL ingestion 层 (小余) 的变更通知机制**尚未有 ADR 记录**。若 Goalserve 上游改字段 → 小段静默吸收 → 小余 ETL 无感知 → 老彭 alpha 读到脏数据，形成 R-20 (4 时间戳契约) 漏洞。建议: 老郭 routing 给老周 + 小余，W10 前补 ADR 覆盖 Goalserve schema 变更通知流程。

**ADV-202605-08 [改进 / P1]**
ADR-029 (worktree push + PR flow) W9 W5 刚落地，顾问团 9 人本身未完成新流程 onboarding。建议老郭 W10 W1 前确认 9 顾问均读 ADR-029 + 派单 prompt 模板 v2，避免顾问 wave 走旧 ADR-024 流程产生 PR 格式不合规。

---

### 老高 (#16, CI/code quality advisor) — CI 流程优化

**ADV-202605-09 [改进 / P1]**
当前 CI grep 检查已有: `core_data_structure_ssot_check.py` (ADR-027) + `docs_frontmatter_check.py` (ADR-028) + `gm_merge_audit.py` v1 + W9 W5 新增 `worktree_pr_check.py` + `gm_merge_audit.py` v2。**W9 W2 + W74-77 的多轮 grep 规则有 2 处重叠检查**（ADR-027 cite 在 `core_data_structure_ssot_check.py` 和 `gm_merge_audit.py` 各扫一次，重复且结果不一致时制造 false FAIL）。建议 W10 合并为统一 `ci_audit_unified.py`，拆分 rule 层与检查层，估算节省 CI 运行时间约 40s/PR（当前 ctest 441 tests 已是瓶颈，不宜再叠加冗余 grep 开销）。

---

### 小邓 (#31, ML advisor) — ML signal pipeline 衔接

**ADV-202605-10 [风险 / P1]**
PositionManager 已从架构撤回（W32 仲裁决议），当前实现为 PositionLedger + StateMachine。**ML 离线训练输出（ONNX 模型）的 C++ 推理接口与新架构的衔接点尚未明确。** 具体 gap: ONNX runtime 输出 signal score 后，应接入 StateMachine 的哪个状态转换节点？是 position entry trigger 还是 signal pre-filter？若无明确接口定义，ML 训练目标函数可能与 C++ 推理调用方不对齐，导致 G3 paper 跑出的 signal 质量失真。建议小梁 C 主管在 W10 前明确 ML→StateMachine 接口契约，小邓配合写 audit/replay 对接规范。

---

### 老叶 (#18, 金融 advisor) — G3/G4/G5 KR 金融审查

**ADV-202605-11 [风险 / P0]**
老钱 KR spec v1 中 **G3 OOS Sharpe ≥ 0.5 门槛偏宽**。金融视角: Sharpe 0.5 在 14 日、50 trades 样本下 bootstrap CI 下界可能接近 0 甚至为负（依赖波动率假设）。对比业界标准: 上线前 paper 验证通常要求 OOS Sharpe ≥ 1.0（月频）。建议老叶 output "G3 KR 金融审查意见 v1"（截止 2026-05-31），与老钱 spec v2 起草时间对齐，老雷 + 王经理联决前必须有老叶独立意见作为第三方背书。

**ADV-202605-12 [风险 / P1]**
G5 月净 PnL ≥ $10K 对应月收益率约 10%（$100K 本金假设），年化 120%。这一数字在 Polymarket 体育市场流动性约束下存在执行风险: 日均成交量 < $1M 的盘口，$100K 本金月周转 10% 意味着单日下注规模约 $500-$1K，在薄流动性盘口滑点不可忽略。建议老叶在 G3 阶段末（Sprint-3 W11+）提交流动性约束下的 PnL 可达性独立验证报告。

---

### 老徐 (#42, 外部工具栈 advisor) — 工具栈升级

**ADV-202605-13 [改进 / P2]**
DuckDB CLI + Parquet 已在老姜 W4 W2 inventory 中列为 ETL pipeline 候选。当前 D 部 ETL (小余主管) 数据落盘格式尚未标准化（W9 实测以 raw JSON 落盘为主）。建议 W10 由小余 D 主管评估 DuckDB-backed Parquet 作为 ETL 中间层存储格式的可行性（写入性能 + 4 时间戳契约 R-20 字段存储）。老徐可提供工具选型 spec，实施由 D 部执行，老徐不越位。时间节点建议: Sprint-2 末（W11）落地。

---

### 小白 (#43, security audit advisor) — security audit 优先级

**ADV-202605-14 [风险 / P0]**
G4 (M5 live 首笔成交) 上线前 security audit 必须完成。三块优先级排序（小白评估）: **私钥管理 (P0) > API 认证 (P1) > 网络暴露面 (P2)**。关键 gap: 老孙 signer v4 C++ 重写后，私钥签名流程的内存安全 + 密钥不落盘验证尚无独立 audit 记录。建议 W11 paper 启动期同步启动 security audit framework，以 checklist 形式覆盖: (1) 私钥生命周期管理; (2) API secret .env 隔离验证; (3) Polymarket CLOB REST 认证 header 不泄露 audit。**G4 上线前 security audit 报告为 Stage-Gate 必要输入（老胡 §4.2 议程 step）。**

---

## §2 意见分级汇总

### P0 — 本月响应 (截止 2026-05-31 ~ 2026-06-10)

| ID | 顾问 | 内容摘要 | 派回主管 | 截止 |
|---|---|---|---|---|
| ADV-202605-03 | 老何 | AI anomaly detection PoC 接 signal pipeline | 老周 A | W10 ack |
| ADV-202605-07 | 老郭 | Goalserve schema 变更通知机制 ADR 缺口 | 老周 + 小余 | W10 前补 ADR |
| ADV-202605-11 | 老叶 | G3 OOS Sharpe ≥ 0.5 偏宽，需独立金融审查 | 小梁 C | 2026-05-31 |
| ADV-202605-14 | 小白 | G4 上线前 security audit framework 启动 | 老韩 B | W11 启动 |

### P1 — W10 响应 (2026-06-10 前 ack)

| ID | 顾问 | 内容摘要 | 派回主管 |
|---|---|---|---|
| ADV-202605-05 | 老钱 | Soccer 优先赛道扩展，W10 多人讨论会确认 | 老钱 CPO 自决 + 老雷联决 |
| ADV-202605-06 | 老钱 | KR spec v1 老韩 + 小梁 ack 跟进 | 老胡 协调 |
| ADV-202605-09 | 老高 | CI grep 合并优化 (W10 ABI lock + unified) | 老周 A (CI 依附系统工程) |
| ADV-202605-10 | 小邓 | ML→StateMachine 接口契约明确 | 小梁 C |
| ADV-202605-12 | 老叶 | G5 PnL 可达性流动性约束验证 | 小梁 C + 老韩 B |

### P2 — Sprint-4 backlog

| ID | 顾问 | 内容摘要 |
|---|---|---|
| ADV-202605-01 | 老张 | Rust 顾问角色重定 / Sprint-4 modern-cpp advisor 评估 |
| ADV-202605-02 | 老张 | signer v4 C++ 进度监控 |
| ADV-202605-13 | 老徐 | DuckDB Parquet ETL 升级 Sprint-2 末落地评估 |

---

## §3 派回主管 — 与 W10 W1 多人讨论会联合决议

以下条目老郭已完成第一轮 routing，主管须在 W10 W1 多人讨论会前 ack（24h ack 规则，ADR-005 §3）：

| 派回主管 | 条目 | 要求 | 截止 |
|---|---|---|---|
| **小梁 (C 主管)** | ADV-202605-11 (老叶 G3 KR 审) | 接收老叶 G3 金融审查意见 v1 → 与老钱 spec v2 合并 → 联决前背书 | 2026-05-31 |
| **小梁 (C 主管)** | ADV-202605-10 (小邓 ML 衔接) | 明确 ML→PositionLedger/StateMachine 接口契约，给小邓 spec brief | W10 W1 |
| **老韩 (B 主管)** | ADV-202605-14 (小白 security audit) | 确认 W11 paper 启动期同步开启 security audit framework，指派老沈/老黄协助小白 | W10 W1 |
| **老周 (A 主管)** | ADV-202605-03 (老何 AI signal) | 评估 anomaly detection PoC 接入 signal pipeline 的架构约束（不改热路径）| W10 W2 |
| **老周 + 小余** | ADV-202605-07 (老郭 schema gap) | 联合起草 Goalserve schema 变更通知 ADR，老郭主审 | W10 W3 前 |
| **老钱 CPO** | ADV-202605-05/06 (sport 扩展决议) | W10 多人讨论会议程提案：Soccer 优先赛道 + KR spec v2 联决 | W10 W1 议程确认 |

**升级路径:** 上表任一主管 48h 未 ack → 老胡协调 → 再 48h 未下 → 老雷介入 (CLAUDE.md §6 Blocker 升级)。

---

## §4 月度顾问→总裁 1:1 排期

**时间:** W10 W4 (~2026-06-26 周五)
**时长:** 60 min
**主持:** 老郭
**参与:** 老雷 (GM 总裁) + 9 顾问全员

### 议程 (固定 4 块 + 本期专项)

| 顺序 | 议题 | 主讲 | 时长 |
|---|---|---|---|
| 1 | W9 W5 第 1 期意见 follow-up — P0/P1 条目执行状态汇报 | 老郭 | 15 min |
| 2 | Sprint-4 战略方向 — 赛道扩展 (Soccer 优先?) + ML pipeline 阶段规划 | 老钱 CPO | 15 min |
| 3 | 数据结构 IC 8/1 onboarding — 新 IC 衔接 D 部 ETL 与 A 部架构约束说明 | 老周 + 小余 | 10 min |
| 4 | 副总裁 7/1 入职 onboarding — 职权划分 + 与 GM/CPO/主管 协作接口 | 老雷 (GM 主导) | 10 min |
| 5 | AOB — 顾问自由发言 + 老雷 Q&A | 全员 | 10 min |

**前置动作:** 老郭 W10 W3 前收集各顾问 5 min 议题提案 → 合并议程 → 发给老雷 ack (72h 提前)。
**产出文档:** `docs/MEETINGS/2026-06-26-laoguo-advisor-monthly-1st.md`（老郭起草，小米 doc-curator 归档）。

---

## §5 ADR-029 流程 compliance 说明

本文档遵循 ADR-029 worktree push + PR flow (W9 W4 起强 enforce)。

老郭 Wave 88 执行 checklist:

- [x] 本文件落入 `docs/MEETINGS/` 路径，含 frontmatter (ADR-028 合规)
- [x] 文件名格式: `YYYY-MM-DD-<owner>-<topic>.md` (CONVENTIONS-naming.md 合规)
- [ ] git commit: `docs(advisor): W9 W5 第 1 期意见箱 aggregate (老郭 Wave 88)`
- [ ] git fetch origin + merge origin/main --no-edit
- [ ] git push origin worktree-agent-a69908d0ce0254ab8
- [ ] gh pr create (ADR-029 §3.1 Step 7 模板)
- [ ] 回汇 GM: commit hash + PR URL + ctest 状态

**ctest 说明:** 本 wave 无代码改动 (doc only)，ctest 不适用；PR body 标注 `ctest: N/A (doc only wave)`。

---

**最后更新:** 2026-05-29 by 老郭 (#46, F 协调人, Wave 88)
