# Totals 盘口接入 PRD v1

- **owner:** 小杜 (product-manager, E 产品业务保障部)
- **last_review:** 2026-05-29
- **status:** DRAFT — 待老钱 (CPO) + 老雷 (GM) 会签
- **验收对接:** 小颖 (requirements-analyst)
- **前置门 (hard gate):** Moneyline paper runtime 起跑 + 看板真值验证通过后才立项。本文喂 M2 规划，当前阶段**不启动**。
- **派单来源:** 老钱 CPO 全盘口路线指示 + 老胡主管派单

---

## 0. 文档目的

本文回答**一个问题**："Totals 盘口上线时长什么样、与 Moneyline 的共同点和差异点在哪里、怎么算验收通过"。

严格守 CPO 老钱盘口序：Moneyline（进行中）→ **Totals（本 PRD）** → Spreads → 分节 → 系列赛 → Prop → Outright。禁跳序，禁同期并行启动 Totals 与 Spreads 的工程实现。

本文不重写老钱 scope 拒绝清单、老韩 RM 设计、老周架构——引用即可。

---

## 1. 背景与动机

### 1.1 为什么 Totals 是第二顺位

老钱 CPO 全盘口序原则：流动性递减 + 复杂度递增，严格按序。Totals（大小盘）位列第二，理由：

| 维度 | Moneyline | Totals | 差异幅度 |
|---|---|---|---|
| 流动性 | 最高 | 次高（同场赛事通常有 Totals 深度） | 小 |
| outcome 数 | 2（球队 A / 球队 B） | 2（Over X.5 / Under X.5） | 相同 |
| 市场结构 | 二元 condition + 2 token | 二元 condition + 2 token | **完全相同** |
| 定价锚 | Moneyline 赔率 | 大小盘线 (line 点位) + 两侧赔率 | 增加 line 字段 |
| Goalserve 数据需求 | 比分 + 时钟 | 比分 + 时钟（已有） | 几乎无增量 |
| CLOB 下单路径 | token_id + side | token_id + side | **完全相同** |
| RM 规则 | R0-R9 Moneyline | R0-R9 **全部复用** + Totals 专属 edge 门限 | 边际成本低 |

结论：Totals 与 Moneyline **共享同一 per-token 二元市场结构**，工程管线复用率最高，是扩盘口最低摩擦的下一步。

### 1.2 Polymarket 中 Totals 的真实结构（一手 API 证明）

引用老李审计（`docs/RESEARCH/laoli-polymarket-market-structure-audit-v1.md` §3.4）实测确认：

```
Event（单场比赛，如 NBA: Clippers vs Magic）
  ├── Market[0]: Moneyline
  │     condition_id: 0x8945183c...
  │     outcomes: ["Clippers", "Magic"]
  │     clobTokenIds: ["91665...", "59154..."]
  │
  ├── Market[1]: Totals（Over/Under X.5）
  │     condition_id: 0x...（独立 condition_id，不同于 Moneyline）
  │     outcomes: ["Over 220.5", "Under 220.5"]
  │     clobTokenIds: ["<over_token_id>", "<under_token_id>"]
  │     neg_risk: false
  │     tick_size: 0.01
  │     min_order_size: 5
  │
  └── Market[2]: Spread...（本期不涉及）
```

**Totals 市场结构核心事实**（ADR-040 §2 per-token 体系完全适用）：

1. 一个 Totals condition = 2 个 token（Over token + Under token），与 Moneyline 完全对称。
2. 每个 token 有独立订单簿（per token_id），`GET /book?token_id=<over_token_id>` 和 `GET /book?token_id=<under_token_id>` 分别查询。
3. 互补镜像关系仍成立：`price_OVER + price_UNDER ≈ 1.00`（扣去 vig 后）。
4. outcome 字段格式为 `"Over 220.5"` / `"Under 220.5"`（含 line 点位值），不是 `"Yes"` / `"No"`。
5. `cross_spread = ask_OVER + ask_UNDER - 1.0`（ADR-040 §4.4 BinaryMarketBookView 已支持）。

---

## 2. 产品定位

### 2.1 一句话

**Totals 盘口接入是 Moneyline 管线的 outcome 扩展，通过复用现有二元市场管线（RiskManager / paper engine / PnL 归因 / per-token 订单簿），仅在 outcome 映射层增加 Over/Under 语义与 line 点位解析，零新增并行架构。**

### 2.2 不是什么

- 不是新的交易策略架构（复用 Moneyline 策略框架，仅扩信号输入特征）。
- 不是新的 RM 规则集（R0-R9 全部复用，仅扩 Totals 专属信号 edge 门限）。
- 不是新的数据摄入管线（Goalserve 比分/时钟数据已有，仅增 line 点位字段解析）。
- 不是并行架构（任何为 Totals 单独建数据流、单独建 RM 实例的方案一律拒绝）。
- 不是本期工作（前置门未过，本 PRD 喂 M2 规划）。

---

## 3. 复用点 vs 差异点（工程视角）

这是本 PRD 的核心产品判断，分两列清楚列出，工程团队按此执行。

### 3.1 完全复用点（Moneyline 管线零改动）

| 模块 | 复用内容 | 引用 |
|---|---|---|
| **市场结构层** | Event → condition_id → token_id × 2 的四层结构完全相同；ADR-040 TokenInfo / MarketInfo / BinaryMarketBookView / BookSnapshot per-token 全部适用 | ADR-040 §2/§4 |
| **CLOB 下单路径** | OrderIntent 字段（condition_id, token_id, side, size_usdc）完全相同；EIP-712 签名流程无变化；Signer IPC 无变化 | 老韩 OrderIntent v0.5 §2.2；老孙 SignerV62 |
| **RiskManager 主框架** | RiskGateway::evaluate() 接口签名不变；R0-R9 规则全部适用（持仓上限 / 日亏 / 连亏 / STALE 门控）；状态机 RUNNING/WARNING/HALTED/DRAIN 不变 | 老韩 RM v0.2 §2/§3/§4 |
| **WAL + audit 体系** | paper_audit.wal / risk_audit.wal schema 不变；audit_id 生成逻辑不变 | 老周架构 v0.4 §18.3 |
| **PnL 归因** | realized PnL 归因到 condition_id 粒度，聚合层不变；fill_tracker 不变 | MVP PRD v1 F-10 |
| **WebSocket 订阅** | Polymarket market channel 按 token_id 订阅（`assets_ids[]`），Totals token 直接加入热/冷分片订阅列表 | 老周架构 v0.4 §17.1.1 |
| **per-token 订单簿** | OrderBookAdapter key = token_id，增量更新逻辑不变；BinaryMarketBookView 双边展示不变 | ADR-040 §4.3/§4.4；小冯工程内证 |
| **STALE 分档机制** | 5 档 STALE（INPLAY_HOT_CRIT / INPLAY_HOT / PREGAME_NEAR / PREGAME_FAR / OUTRIGHT）判定逻辑不变 | 老周架构 v0.4 §17.6 |
| **4 时间戳契约** | event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts 红线不变（R-20） | CLAUDE.md §8 |
| **Paper engine** | IPaperSigner / IVirtualMatcher / IVirtualNonce 不变；paper_exec.wal 不变 | 老周架构 v0.4 §18 |
| **观测 API** | `/api/v1/book_pair/{condition_id}` / `/api/v1/market/{condition_id}` 接口已通用，Totals condition 直接查 | ADR-038 / ADR-040 §4.6 |

**结论：90%+ 管线零改动。Totals 是一次插值，不是一次重建。**

### 3.2 差异点（Totals 专属，工程需增量处理）

| 差异维度 | Moneyline 现状 | Totals 增量 | 影响面 |
|---|---|---|---|
| **outcome 名称格式** | `"Clippers"` / `"Magic"`（球队名）或 `"Yes"` / `"No"` | `"Over 220.5"` / `"Under 220.5"`（含浮点 line 点位） | outcome 展示层 + 日志 + 看板标签 |
| **Outcome enum 扩展** | `Outcome::Yes`(0) / `Outcome::No`(1)（OrderIntent v0.5 已预留 Over=5 / Under=6） | 启用 `Outcome::Over = 5` / `Outcome::Under = 6`（已预留，取消注释即可） | signal_iface.hpp（OrderIntent v0.5 §2.1 已预留，零 ABI break） |
| **line 点位字段** | 无（Moneyline 无 line） | Totals condition 的 `question` 字段含 line 点位（如 `"NBA: Lakers vs Celtics, Total Points Over/Under 225.5"`）；`outcomes[]` 字段内嵌 line（`"Over 225.5"`），需解析出浮点数 `line_value: double` | gamma 解析层（小余 / 小段 ETL）；FeatureSnapshot 需增 `totals_line` 字段 |
| **信号 edge 逻辑** | 基于 Moneyline 胜负概率 de-vig 与 Polymarket 价格偏差 | 基于 Goalserve Totals 赔率 de-vig（Over/Under 两侧）与 Polymarket Totals 价格偏差；line 点位需与 Goalserve line 对齐（可能存在 line 不一致问题，需处理） | 小程 / 小梁量化信号层（新策略组件，但调用框架复用） |
| **Goalserve 数据对齐** | 使用 Goalserve `moneyline` 节点赔率 | 使用 Goalserve `totals` 节点赔率（`<totals home="Over 225.5" away="Under 225.5" odds_home="1.87" odds_away="1.95"/>`）；line 需与 Polymarket outcome string 中的 line 做精度对齐（避免 225.5 vs 226 错配） | 小段 Goalserve adapter（已有 Totals 节点，需确认字段）；映射表扩展 |
| **RM 信号 edge 门限** | `MIN_EDGE_MONEYLINE`（老韩 RM v0.2 §3 R8） | 新增 `MIN_EDGE_TOTALS`（独立参数，Totals 市场 vig 结构不同，edge 门限需单独标定）；R0-R9 规则体不变 | 老韩 RM TOML config 增一参数；RM 代码不改规则 |
| **per-condition 持仓 cap** | `MAX_EXPOSURE_PER_CONDITION` 按 Moneyline market 统计 | Totals condition_id 独立计入 cap（与 Moneyline condition 同场但 condition_id 不同，天然隔离）；无需 grouping 逻辑 | 老韩 RM R-5/R-6 head-count 逻辑，天然兼容 |
| **看板/日报标签** | "Moneyline" 标签 | 增加 "Totals" 标签；日终报告信号触发统计增 Totals 分项 | 小颖 / 小苏 report 层；小郑 metrics label |
| **Polymarket 市场发现** | gamma poller 按 `tag_slug=sports` 全量拿，过滤 Moneyline condition | gamma poller 结果中已包含 Totals condition（`marketsSections.totals`），增加 Totals 白名单过滤逻辑 | 小段 / 小余 ETL 过滤层；T5 periodic_gamma_poller 输出增量 |

---

## 4. 用户故事

格式：**作为 [角色]，我要 [行为]，以便 [目标]**。

### 4.1 操作员（P1 Operator）

- **US-T01：** 作为操作员，我要在 Grafana 看板上通过"Totals"标签与"Moneyline"标签区分持仓和 PnL，以便一眼判断两类盘口各自的健康状态，而无需深入 audit 日志。
- **US-T02：** 作为操作员，我要在系统首次 arm Totals 盘口时，收到与 Moneyline arm 同格式的 checklist 回执（含 Totals condition 白名单数量 + WSS 订阅 token 数），以便确认配置正确无误。
- **US-T03：** 作为操作员，我要 Totals RM 触发 HALTED 时收到的电话和 Slack 告警格式与 Moneyline 完全一致（仅 market_type 字段不同），以便不因格式差异延误响应。

### 4.2 量化研究员（P2 Quant Analyst）

- **US-T04：** 作为量化研究员，我要能在 replay 框架中对同一场比赛的 Moneyline 和 Totals 事件流**同时**重放，以便对比两类盘口的信号触发时机和 edge 分布，而无需切换不同 replay 配置。
- **US-T05：** 作为量化研究员，我要在 FeatureSnapshot 中访问 `totals_line`（Totals line 点位浮点数）字段，以便构建基于 line 偏差的量化特征，而无需自己解析 outcome 字符串。
- **US-T06：** 作为量化研究员，我要能在 OOS 回测中按 `market_type=Totals` 切片，独立计算 Totals 信号的 Sharpe 和胜率，以便在 Totals 实盘前有独立的统计显著性依据。
- **US-T07：** 作为量化研究员，我要确认 Goalserve Totals 赔率中的 line 点位（如 `225.5`）与 Polymarket outcome 字符串（如 `"Over 225.5"`）的解析精度完全一致，以便不因 line 对齐误差导致虚假 edge 信号。

### 4.3 风控员（P3 Risk）

- **US-T08：** 作为风控员，我要在 audit log 中通过 `condition_id` 唯一标识 Totals 盘口，并能通过 `market_type=Totals` 字段过滤拒单分布，以便独立评估 Totals 的 RM 行为而无需解析 outcome 字符串。
- **US-T09：** 作为风控员，我要 Totals 盘口使用独立的 `MIN_EDGE_TOTALS` TOML 配置参数，以便在不影响 Moneyline 策略的前提下，单独调整 Totals 信号的 edge 门限（R8 规则复用，参数独立）。
- **US-T10：** 作为风控员，我要 Totals condition_id 的持仓 cap 独立统计（不与 Moneyline 共用同场比赛的 cap 额度），以便精确控制两类盘口的总敞口。

### 4.4 架构观察员（P4 Architect）

- **US-T11：** 作为架构观察员，我要确认 Totals 接入**不新增任何并行数据流**（不新建独立 WSS 连接、不新建独立 RM 实例、不新建独立 WAL 文件），以便架构复杂度不随盘口数量线性增长。
- **US-T12：** 作为架构观察员，我要 Totals token_id 的 WSS 订阅直接加入现有 market_hot / market_cold ring 分片，而不是新开 ring，以便 vCPU0 的 4-5 conn 拓扑不被破坏（老周架构 v0.4 §17.1.1）。

---

## 5. 功能列表（Totals 增量，基于 Moneyline 管线）

P0 = Totals 实盘必含；P1 = Totals 接入后期；N/A = 复用 Moneyline，无需列。

| ID | 功能 | 优先级 | Owner（建议） | 依赖 |
|---|---|---|---|---|
| TF-01 | gamma poller 增加 Totals condition 过滤与白名单管理 | P0 | 小段 / 小余 | T5 periodic_gamma_poller |
| TF-02 | outcome 字段解析：从 `"Over 225.5"` 提取 `line_value: double` + `side: Over/Under` | P0 | 小段 | gamma adapter |
| TF-03 | Goalserve totals 节点赔率摄入（line 点位精度对齐） | P0 | 小段 | goalserve_adapter |
| TF-04 | FeatureSnapshot 增加 `totals_line` 字段（不破坏 Moneyline feature 向量） | P0 | 小余 / 小梁 | feature_store_contract |
| TF-05 | OrderIntent v0.5 启用 `Outcome::Over = 5` / `Outcome::Under = 6`（已预留，取消注释） | P0 | 老韩 | signal_iface.hpp |
| TF-06 | RM TOML config 增加 `MIN_EDGE_TOTALS` 参数；R0-R9 规则引用该参数 | P0 | 老韩 | RM v0.2 TOML |
| TF-07 | Totals 信号 v1（基于 Goalserve totals de-vig vs Polymarket Totals 价格偏差） | P0 | 小程 / 小梁 | 小梁量化框架 |
| TF-08 | Grafana 看板增 `market_type=Totals` 标签分项（PnL / 拒单 / freshness） | P0 | 小郑 | metrics label |
| TF-09 | 日终报告信号触发统计增 Totals 分项 | P0 | 小颖 / 小苏 | report 层 |
| TF-10 | Totals OOS 验证（≥ 100 场样本，独立 Sharpe gate） | P0（实盘前） | 小程 / 小梁 | replay 框架 |
| TF-11 | line 对齐单测（Goalserve line vs Polymarket outcome string，精度 1e-4） | P0（CI） | 小宋 | 测试框架 |
| TF-12 | Totals 影子模式 2 周（≥ 500 次 evaluate，验收后才解锁实盘） | P0（实盘前） | 老韩 + 小颖 | 影子模式框架 |
| TF-13 | audit log 增加 `market_type` 字段（`"Moneyline"` / `"Totals"`） | P0 | 老韩 / 老唐 | audit schema |
| TF-14 | Totals condition 映射表（Goalserve match_id + Totals line → condition_id）扩展 | P1 | 小余 | 映射表 sqlite |
| TF-15 | Totals 拒单分析报告（影子模式后，参照 F-30 Moneyline 格式） | P1 | 老韩 + 小颖 | — |

**不新增功能（复用 Moneyline，工程无需额外工作）：**

| 功能 | 复用来源 | 备注 |
|---|---|---|
| per-token 订单簿更新 | OrderBookAdapter（key=token_id）| Over/Under token 直接插入，无改 |
| WSS 订阅管理 | T0a/T0b market_hot/cold reactor | Totals token_id 加入 `assets_ids[]` 即可 |
| CLOB 下单 | condition_id + token_id + side + size_usdc | 完全兼容 |
| EIP-712 签名 / Signer | IPaperSigner / IRealSigner | 无变化 |
| WAL / audit 写入 | audit_fsync (T8) + exec_fsync (T9) | schema 增 market_type 字段（TF-13）|
| PnL 归因 | fill_tracker + position_ledger | condition_id 粒度天然隔离 |
| STALE 判定 | 5 档 classifier（T2 book_builder）| Totals token freshness 同规则 |
| 观测 API | `/api/v1/book_pair/{totals_condition_id}` | ADR-040 接口已通用 |
| 崩溃恢复 | WAL 重放 | 无变化 |

---

## 6. 验收标准

### 6.1 Totals 实盘前置门（全部 pass 才解锁实盘）

| # | 验收项 | 通过标准 | 验收人 |
|---|---|---|---|
| AC-T01 | **前置门：Moneyline paper runtime 已起跑且看板真值验证通过** | Moneyline paper gate（GM-PAPER-G v2 相关条款）全部 pass，老雷 + 老钱签字 | 老雷 + 老钱 |
| AC-T02 | **不新增并行架构** | 代码审计确认：零新增独立 WSS 连接；零新增独立 RM 实例；零新增独立 WAL 文件路径 | 老周 + 老郭 |
| AC-T03 | **Outcome enum 启用** | `Outcome::Over = 5` / `Outcome::Under = 6` 在 signal_iface.hpp 取消注释；CI 编译通过；RM 单测覆盖 Over/Under 分支 | 老韩 + 小宋 |
| AC-T04 | **line 字段解析精度** | `"Over 225.5"` → `line_value = 225.5`（精度 ≤ 1e-4）；Goalserve line 与 Polymarket line 对齐率 ≥ 99.5%（≥ 200 场样本） | 小段 + 小宋 |
| AC-T05 | **FeatureSnapshot 兼容性** | 增加 `totals_line` 字段后，Moneyline feature 向量 diff = 0（同一输入，回测与实盘 feature 一致，D-04 红线） | 小梁 + 小余 |
| AC-T06 | **Totals 信号 OOS 验证** | ≥ 100 场 OOS 样本；Totals Sharpe ≥ 0.5（独立计算，不与 Moneyline 合并）；胜率 ≥ 53% | 小梁 + 老钱 |
| AC-T07 | **RM 拒单 audit 可追溯** | 任意 Totals 拒单，30s 内通过 `condition_id` + `market_type=Totals` grep 出 audit 记录 | 老韩 |
| AC-T08 | **Totals 影子模式** | ≥ 2 周 / ≥ 500 次 evaluate；`INTERNAL_ERROR` = 0；Totals 拒单分布报告出炉 | 老韩 + 小颖 |
| AC-T09 | **看板标签分项** | Grafana 看板 `market_type=Totals` 过滤可用；PnL / 拒单 / freshness 三项独立展示 | 小郑 + 老雷目视验收 |
| AC-T10 | **Totals 持仓 cap 独立** | 同场比赛 Moneyline 满 cap 时，Totals 单独计入独立 cap，二者不互相占用 | 老韩 |
| AC-T11 | **端到端 paper fill 路径** | Totals Over/Under token paper 下单走通：OrderIntent → RM evaluate → IPaperSigner → paper_audit.wal 完整路径，且 mode=Paper | 老韩 + 小蒋 |
| AC-T12 | **日终报告 Totals 分项** | disarm 后 30min 内日终报告含 Totals 信号触发计数 + 成交清单（按 market_type 分项） | 小颖 + 老雷 |

### 6.2 风控硬验收（任意一项 = 0 违规即 block）

| 验收项 | 标准 |
|---|---|
| 绕过 RM 下单 Totals | 0（同 Moneyline 红线，CI 静态扫描） |
| Totals paper 污染 live ledger | 0（R-11 红线，paper_audit.wal 独立） |
| 回测与实盘 Totals feature 不一致 | 0（D-04 红线） |
| Totals audit schema 静默变更 | 0（CLAUDE.md §8 红线） |

---

## 7. 约束（产品硬约束）

### 7.1 不新建并行架构（最强约束）

任何为 Totals 单独建立以下内容的 PR，**一律驳回**，不走架构评审直接拒：

- 独立 WebSocket 连接或独立 ring buffer
- 独立 RM 实例或独立 RM 规则集（允许增 MIN_EDGE_TOTALS 参数，不允许新 RM 类）
- 独立 WAL 文件体系（paper_audit.wal / risk_audit.wal 统一，增 market_type 字段即可）
- 独立 PnL 归因管线（condition_id 粒度天然隔离）
- 独立 Goalserve 数据摄入线程（T6/T7 periodic poller 增字段解析，不新增线程）

### 7.2 禁跳序

Totals 工程实现**必须**在 Moneyline paper 成功起跑后才立项。在 Moneyline 看板真值验证通过前，本 PRD 仅用于 M2 规划，不进入 Sprint backlog，不派实现任务。

### 7.3 单信号约束

老钱 CPO 全盘口路线中，Totals 期的信号仍为**1 套**（Totals 专属信号 v1），不叠加 Moneyline 信号判断，不做 Moneyline + Totals 联合仓位。

### 7.4 盘口范围

Totals 接入范围：**NBA / NFL / MLB 三联赛的 Totals（大小盘）**，不扩展至 NHL / Soccer / 其他联赛。扩联赛在更后续盘口序中处理。

### 7.5 line 对齐优先

Goalserve Totals line 与 Polymarket Totals outcome 字符串 line 必须精度对齐（≤ 0.5 档差），否则信号层会产生系统性虚假 edge。line 对齐单测（TF-11）是 CI 门禁，不可绕过。

---

## 8. 前置门详述（M2 规划锚）

**Totals PRD 立项触发条件（双条件 AND 关系）：**

| 条件 | 判断口径 | 签字人 |
|---|---|---|
| Moneyline paper runtime 起跑 | IPaperSigner 路径完整跑通；paper_audit.wal 有真实 evaluate 记录；VirtualMatcher 填充逻辑验证正确 | 老周 + 小蒋 |
| 看板真值验证通过 | Grafana Moneyline 看板数据与 audit log 人工抽查 ≥ 20 笔 100% 一致；小颖验收报告出炉；老钱 + 老雷双签 | 老钱 + 老雷 + 小颖 |

两条件都满足 → 老胡召集需求-工程协商会 → 本 PRD 转 active → 进 Sprint backlog。

任一未满足 → 本 PRD 保持 M2 规划状态，不派单。

---

## 9. 风险登记

| # | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| R-T01 | **Goalserve Totals line 与 Polymarket line 系统性不对齐**（如半盘差 0.5） | 中 | 高（虚假 edge 信号，实盘亏损） | TF-11 line 对齐单测 CI 门禁；≥ 200 场样本 AC-T04 验收 |
| R-T02 | **Totals 市场流动性比预期薄**（深度 < $20K） | 中 | 中（信号触发后 fill 不足） | 开盘前检查 Totals depth；MIN_EDGE_TOTALS 设置保守初值 |
| R-T03 | **Totals vig 结构与 Moneyline 不同，MIN_EDGE_TOTALS 初值不合理** | 中 | 中（拒单率过高或 edge 估算偏差） | 影子模式 2 周标定（AC-T08）；小梁量化背书初值 |
| R-T04 | **Moneyline 前置门延误，Totals 立项被推迟** | 中 | 低（本期不在关键路径，M2 规划） | 前置门是硬约束，延迟是正确行为；不提前启动 |
| R-T05 | **outcome 字符串格式变更**（Polymarket 更新 outcome 命名规则） | 低 | 中（解析逻辑失效） | outcome 解析单测 CI 门禁；老李 API 监控响应 < 2h（KR-D-6） |
| R-T06 | **Totals 信号 OOS Sharpe < 0.5**，实盘前验收不过 | 中 | 中（Totals 实盘推迟） | 提前 OOS 验证（AC-T06）；推迟不是失败，是纪律 |

---

## 10. Roadmap 锚（M2 规划视角）

| 阶段 | 内容 | 触发条件 | 预计时间窗 |
|---|---|---|---|
| **M1（当前）** | Moneyline paper runtime 建设；本 PRD 仅规划 | — | Sprint-2 → Sprint-3 |
| **M1.5（前置门验证）** | Moneyline 看板真值验证；老钱 + 老雷双签 | Moneyline paper 跑通 | Sprint-3 末 / Sprint-4 初 |
| **M2（Totals 立项）** | 本 PRD 转 active；TF-01 → TF-13 进 Sprint backlog；派单老胡统筹 | M1.5 双签通过 | Sprint-4 启动 |
| **M2 建设** | TF-01~TF-12 工程实现；Totals 影子模式 2 周 | — | Sprint-4 → Sprint-5 |
| **M2 实盘** | Totals paper 首笔 → 看板验证 → 实盘解锁 | AC-T01~AC-T12 全过 | Sprint-5 末 |
| **M3（Spreads）** | 下一盘口，另出 PRD | Totals 实盘稳定 2 周 | Sprint-6+ |

---

## 附录 A：与老钱 CPO 全盘口路线的对照

| 老钱路线条目 | 本 PRD 对应约束 |
|---|---|
| Moneyline → Totals → Spreads 严格禁跳序 | §2.2 + §7.2 + §8 前置门 |
| Totals 最大化复用 Moneyline 管线 | §3.1 复用点全表（90%+ 零改） |
| 盘口差异只在 outcome 映射层 | §3.2 差异点（outcome enum + line 解析）|
| 不新建并行架构 | §7.1 最强约束 |
| RiskManager / paper engine / PnL 归因 / per-token 结构全复用 | §3.1 表逐项确认 |

---

## 附录 B：关联文档

| 文档 | 关联内容 |
|---|---|
| `docs/ADR/2026-05-29-adr-040-market-structure-per-token.md` | per-token 订单簿结构（Totals 直接适用） |
| `docs/RESEARCH/laoli-polymarket-market-structure-audit-v1.md` | Polymarket Totals 市场结构实测（§3.4） |
| `docs/RESEARCH/laohan-w9-orderintent-v05-spec-v1.md` | OrderIntent v0.5（Outcome::Over/Under 已预留）|
| `docs/RESEARCH/laohan-riskmanager-design-v0.2.md` | RM R0-R9 规则全集（Totals 全部复用）|
| `docs/RESEARCH/laozhou-architecture-v0.4.md` | 线程拓扑 + ring buffer（Totals token 直接插入现有分片）|
| `docs/RESEARCH/xiaodu-mvp-prd-v1.md` | MVP PRD v1（F-32 Totals backlog 原始条目）|
| `docs/OKR/laoqian-w8-w5-profitability-kr-v1.md` | 老钱 CPO 盈利 KR（Totals 不在 MVP scope，M2 规划锚）|
| `docs/RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md` | Goalserve totals 节点字段（TF-03 依赖）|

---

## 附录 C：签收

| 角色 | Owner | 状态 |
|---|---|---|
| CPO（scope 仲裁 + 盘口序确认） | 老钱 | 待签 |
| GM（最终拍板） | 老雷 | 待签 |
| 主管（PM milestone 主权） | 老胡 | 待签 |
| Architect（并行架构约束确认） | 老周 + 老郭 | 待签 |
| Risk（RM 复用确认 + MIN_EDGE_TOTALS 参数） | 老韩 | 待签 |
| Quant（信号 + OOS 验收） | 小梁 | 待签 |
| QA（验收对接） | 小颖 | 待签 |
| Doc curator | 小米 | 待签 |

---

**最后更新：** 2026-05-29 by 小杜 (product-manager, E 产品业务保障部)

— 小杜
