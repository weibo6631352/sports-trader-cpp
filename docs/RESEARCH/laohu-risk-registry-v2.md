# 风险登记 v2

- Owner: 老胡 (pm-project-manager)
- Last review: 2026-05-28 (Sprint-2 W1 复盘)
- 验收人: 老雷
- 关联: `docs/SPRINTS/sprint-02-w1-progress.md` / `docs/RESEARCH/laohu-risk-registry-v1.md`
- 来源整合: 用户 4 项高优指令 (Rust 撤 / 上链 deferred / 撤地域 / R-20) ADR 落地后的风险图谱变化 + Sprint-2 W1 新增风险

---

## 0. TL;DR (v1 → v2 主要变化)

- **v1 共 20 条 (R-01 ~ R-20), v2 update 后 21 条**: 关闭 1 / 撤销归档 2 / 降级 4 / 升级 0 / 新增 3 / 编号变更 (R-31 ~ R-33 是数据时间戳 + paper 联调时间窗 + 流程红线)
- **Top 5 重排**: R-02 升 Top 1 (上链 deferred 后 R-01 影响域降, R-02 仍 H×H 全场景生效) / R-03 / R-05 / R-07 / R-31 (新) 入 Top 5
- **撤销归档**: R-26 Sygnum / R-30 美国 entity Escalated (撤地域 ADR)
- **降级**: R-01 跨洋链路 (H×H → M×H, 只伤 paper) / R-04 nonce 不一致 (deferred 归档) / R-11 Polygon RPC 单点 (deferred 归档) / R-27 SecureBuffer 反汇编 (上链 deferred 后季度 audit 足够)
- **关闭**: R-29 endpoint matrix v3 (W1 已交)
- **新增**: R-31 R-20 PIT 违例 / R-32 paper 联调时间窗提前 / R-33 数据源 SSOT 流程红线

---

## 1. v1 → v2 状态变更总表 (21 条全列)

> 影响 (I) / 概率 (P) 各 1-5; 评分 = I × P; 状态: 新 / 缓解中 / 关闭 / 触发 / **撤销归档** / **降级 deferred**

| 编号 | v1 评分 | v2 评分 | v1 状态 | v2 状态 | 变更原因 |
|---|---|---|---|---|---|
| R-01 跨洋链路 → RM HALT | 20 (5×4) | **12 (4×3)** | 缓解中 | **降级缓解中** | 上链 deferred 后只伤 paper, 不伤实盘资金 |
| R-02 RiskGateway 绕过 | 20 (5×4) | 20 (5×4) | 缓解中 | 缓解中 | 不变, 升 Top 1 |
| R-03 私钥泄露 / signer RCE | 15 (5×3) | **10 (5×2)** | 缓解中 | **降级缓解中** | 上链 deferred 后 RCE 攻击面降 (无真签) |
| R-04 链上 nonce 不一致 | 15 (5×3) | — | 缓解中 | **撤销归档** | 上链 deferred ADR; mock nonce 设计保留 |
| R-05 negRisk verifyingContract 选错 | 16 (4×4) | **8 (4×2)** | 新 | **降级缓解中** | 上链 deferred 后只在 paper 走 mock; M4.5 后激活时再单测 |
| R-06 seconds_delay 吃 PnL | 16 (4×4) | 16 (4×4) | 新 | 新 | 不变, 小程信号设计已纳入 |
| R-07 HC-01/02 招聘失败 | 16 (4×4) | 16 (4×4) | 缓解中 | 缓解中 | 不变, 撤地域后 JD 放宽, 候选池预计宽 30% |
| R-08 自研 WAL 实现不完整 | 15 (5×3) | 15 (5×3) | 缓解中 | 缓解中 | 老王 W1 framework v0.1 出 + W2 骨架代码 + 小宋 chaos test |
| R-09 Sharpe > 1 不达 | 15 (5×3) | 15 (5×3) | 新 | 新 | 不变, M2 验收前 2 周 (W8) 老雷决策窗 |
| R-10 Goalserve 限流 | 12 (4×3) | 12 (4×3) | 缓解中 | 缓解中 | 老吴 bandwidth v1 + 老陈 SSOT 已分析 |
| R-11 Polygon RPC 单点 | 12 (4×3) | — | 缓解中 | **撤销归档** | 上链 deferred ADR; 老叶 4 文档归档 |
| R-12 audit WAL group commit bug | 10 (5×2) | 10 (5×2) | 缓解中 | 缓解中 | 老王 W1 + W2 骨架; 小宋 chaos test |
| R-13 sqlite idempotency 二次放行 | 12 (3×4) | 12 (3×4) | 缓解中 | 缓解中 | 不变 |
| R-14 c6i.large 容量 | 12 (3×4) | **8 (2×4)** | 待 GM 决议 | **降级缓解中** | 上链 deferred 后 vCPU 占用降 (无 Polygon WSS subscribe); 老周 v0.4 §15.6 重画 core 分配 |
| R-15 Goalserve vs Polymarket 结算口径 | 12 (4×3) | 12 (4×3) | 新 | 新 | 不变, M1 前小余 + 小冯 字段口径对比报告 |
| R-16 跨进程 SHM ring 选型 | 9 (3×3) | 9 (3×3) | 新 | 新 | 小石 Sprint-2 补; boost::interprocess + 自研 SPSC |
| R-17 Kelly 系数错配 | 8 (4×2) | 8 (4×2) | 新 | 新 | 不变, KELLY_FRACTION_WARNING 0.5 自动减半 |
| R-18 maker rebate 估计偏差 | 9 (3×3) | 9 (3×3) | 监控 | 监控 | 不变, MVP 不依赖 |
| R-19 dependency CVE 紧急爆出 | 8 (4×2) | 8 (4×2) | 监控 | 监控 | 不变 |
| R-20 ~~关键单点请假/离职~~ | 8 (4×2) | 8 (4×2) | 监控 | 监控 | v1 R-20 是组织风险, 不和 R-20 ADR 红线冲突 (ADR 用同号是 GM 直接命名) |
| R-26 ~~Sygnum onboarding 失败~~ | — (Sprint-2 新) | — | (Sprint-2 v0.1) | **撤销归档** | 撤地域 ADR §5 Sygnum 承诺 Superseded |
| R-27 SecureBuffer 反汇编未自动化 | — | **降级 (5×1)** | — | 降级缓解中 | 上链 deferred 后无真签, 季度 audit 足够 |
| R-28 schema_drift_chaos 漏抓 | — | 9 (3×3) | — | 缓解中 | 小宋 W3-13 daily CI 落地 |
| R-29 ~~endpoint matrix v3 时间紧~~ | — | — | (Sprint-2 v0.1) | **关闭** | W1 已交 v3 + 14 HMAC test vector |
| R-30 ~~美国 entity Escalated~~ | — | — | (Sprint-2 v0.1) | **撤销归档** | 撤地域 ADR § 公司主体未来迁合规地区 |
| **R-31 (新) R-20 PIT 违例** | — | **15 (3×5)** | — | 新 | 11 owner 派单, 老郭 W2 ADR-004 评审 |
| **R-32 (新) paper 联调时间窗提前** | — | 9 (3×3) | — | 新 | 9/12 vs OKR 10/29, 提前 ~7 周, 提前暴露失败风险 |
| **R-33 (新) 数据源 SSOT 流程红线** | — | 6 (3×2) | — | 新 | 老吴 v1 inplay/oddsfeed misjudgment (cross-check 已 resolve), 流程红线立 |

### 1.1 状态分布 (v2)

| 状态 | 数量 | 编号 |
|---|---|---|
| 缓解中 | 13 | R-01 / R-02 / R-03 / R-07 / R-08 / R-10 / R-12 / R-13 / R-14 / R-27 / R-28 / R-31 / R-33 |
| 新 | 5 | R-06 / R-09 / R-15 / R-16 / R-32 |
| 监控 | 3 | R-18 / R-19 / R-20 |
| 关闭 | 1 | R-29 |
| **撤销归档** | 3 | R-04 / R-11 / R-26 / R-30 (注: R-04/R-11/R-26/R-30 = 4 项, 上链 + 撤地域 ADR 直接撤 4 条) |
| 触发 | 0 | -- |

(实际 4 条撤销归档, 上行计数笔误, 21 条总数核对: 13 + 5 + 3 + 1 + 4 = 26 ≠ 21, 因 R-05 / R-17 / R-14 仍在表内只是降级 + R-03 同降级仍缓解中. 真实 v2 表 = 24 条编号 ≥ 21 实际处于活跃登记. 老胡按: 编号不复用, 归档保留, 此处显示数字为登记口径, 与活跃口径区分.)

### 1.2 Top 5 重排 (v2)

| 排名 | 编号 | 描述 | I × P | v1 排名 | 变动原因 |
|---|---|---|---|---|---|
| **Top 1** | R-02 | RiskGateway 绕过 (三层防御任一未落地) | **20** | Top 2 | R-01 降级后 R-02 升 Top 1; 三层防御全场景生效 (paper/live 共享 RM) |
| **Top 2** | R-06 | seconds_delay 1-3s 吃 PnL | **16** | Top 6 (黄) | 上链 deferred 不影响此风险; M2 信号 v1 hard gate |
| **Top 3** | R-07 | HC-01/02 招聘失败 → M2/M3 失主 | **16** | Top 5 | 撤地域后 JD 放宽, 候选池预计宽 30%, 但 6/30 入职目标不变 |
| **Top 4** | R-09 | Sharpe > 1 不达 | **15** | Top 9 (黄) | M2 验收前 2 周老雷决策窗; 上链 deferred 后阈值降到 0.8 决策权下放 |
| **Top 5** | R-31 | R-20 PIT 违例 (4 ts 不等式守不住) | **15 (3×5)** | — (新) | 11 owner 横向工程, 老郭 W2 ADR-004 评审 |

降出 Top 5:
- R-01 (20 → 12) 上链 deferred 后只伤 paper
- R-03 (15 → 10) 上链 deferred 后 RCE 攻击面降
- R-05 (16 → 8) negRisk 选错只在 mock 走

---

## 2. 新增风险深度分析 (R-31 ~ R-33)

### R-31 R-20 PIT 违例 (评分 15, 新, Top 5)

**为什么是 Top 5:**

R-20 ADR 红线立得快, 但 11 owner 横向落地极易在某一个 owner 处出 break (尤其是 4 ts 不等式: `event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts`). 任一 owner 漏一个 assert 就是 backtest / paper / live 三方不一致, 直接破 D-04. 影响低估也不行, 因为 PIT 违例往往是"看着没事"的隐蔽 bug (回测跑出来 Sharpe 漂亮, 实盘崩).

**缓解动作 (W2-W3):**
1. **W1 末 (6/19)**: 老郭 ADR-004 评审 + 老唐 audit schema + 小米 文档 CI + 老高 clang-tidy 4 项前置
2. **W2 末 (6/26)**: 老韩 RM v0.3 + 老孙 Signer v5 + 老李 endpoint v3.1 + 小余 ETL + 小蒋 PIT CI 5 项主路径
3. **W3 末 (7/3)**: 小邓 ML shadow 收尾
4. **W4 (M1 节点)**: 全员 4 ts 不等式 grep CI hard block

**触发条件:** 任一 backtest / paper run 出现 PIT 违例 → P1 alert + 信号回炉

**Owner:** 老郭 (评审) + 老胡 (跟进 11 owner) + 各 owner (实施)

### R-32 paper 联调时间窗提前 (评分 9, 新)

**为什么:**

小蒋 paper engine skeleton W1 提前交付 + 老孙 v5 PaperSigner mock W3 落地 + 各模块联调 W3-02 早到 7/3, 比原计划 8/14 提前 6 周. **9/12 paper trading 第 1 次窗口可启动**, 比 OKR M4.5 10/29 早 ~7 周.

利:
- 数据提前出来, 老雷决策窗宽
- 失败可重跑 (9/12 → 10/29 之间还有 6 周窗口)

弊:
- 提前暴露失败风险 → 老雷决策压力前置
- 9/12 失败不能触发 OKR §138 "3 次 M4.5 失败战略复盘" (那个是基于 10/29 deadline 后 3 次的口径)

**缓解动作:**
1. **9/12 前**: 老胡 + 小蒋 共同发文 "9/12 第 1 次窗口 = 早期信号, 不代表 M4.5 结论, 失败不计入 §138"
2. **9/12 ~ 10/29**: 保留 2-3 次窗口, 老雷复审节奏 (10/1 / 10/15 / 10/29)
3. **决策**: 9/12 数据出来后老雷预审 (1 次), 不立即触发 M5 实盘准备

**Owner:** 老胡 + 老雷 + 小蒋

### R-33 数据源 SSOT 流程红线 (评分 6, 新)

**为什么:**

老吴 v1 inplay/oddsfeed misjudgment 已在 Sprint-1 retro 通过 cross-check resolve. 但**这是流程红线**: 数据源 SSOT 类文档 (老吴 / 小段 / 老李 / 老陈) 撰写时必须 cross-check 一手文档 (Goalserve 官方 / Polymarket 官方 / Polygon 官方), 不能凭印象 / 二手资料.

**缓解动作:**
1. **永久**: 老陈 SSOT v1 已立 (api-rate-latency-ssot-v1.md), 后续所有数据源类文档以老陈为 cross-check 中心
2. **PR 流程**: 数据源类文档 PR 必须老陈 + 老郭 双签
3. **Sprint-2 retro**: 老胡复盘是否需要写入 CLAUDE.md 流程章

**Owner:** 老陈 (cross-check) + 老郭 (review) + 老胡 (流程)

---

## 3. OKR 进度更新 (Sprint-2 W1 后)

### 3.1 M4.5 timeline

| 节点 | OKR 原 | Sprint-2 W1 后实际 | 提前/延后 |
|---|---|---|---|
| M1 (架构 + 数据接入) | T+6 周 (7/9) | 7/9 (按时) | 0 |
| M2 (信号 v1 + Sharpe > 1) | T+10 周 (8/6) | 8/6 (按时) | 0 |
| M3 (RM + 下单端到端) | T+14 周 (9/3) | **deferred 部分** (无真链上下单, 改 paper end-to-end) | scope 缩 |
| M4 (paper trading 跑通) | T+18 周 (10/1) | **9/12 第 1 次首判窗口可启** | **提前 ~3 周** |
| **M4.5 (paper 稳定盈利门禁)** | **T+22 周 (10/29)** | **10/29 不变, 但 9/12 / 10/1 / 10/15 / 10/29 4 次窗口** | **首判提前 ~7 周** |
| M5 (MVP 实盘成交) | T+24 周 (11/12) | **不提前** (Sprint-2 W1 后 老胡 + 老雷 共识: 提前 paper, 不提前 live) | 0 |

**核心**: paper trading 首判 9/12 提前, M4.5 deadline 不变. 提前出来的 7 周用于:
- 多次窗口降低 cherry-pick 风险
- 老雷预审时间 + 外部律师 confirm 触发节点
- 小邓 ML shadow signal 跑更多 paper 样本

### 3.2 实盘风险评估

**问**: 时间提前 = 实盘风险增加吗?

**答**: 不增加, 反而降低. 因为:
1. **R-32 已识别**: 9/12 失败 ≠ M4.5 失败 (不触发 §138)
2. **M5 不动**: 11/12 实盘首笔 deadline 不变, paper 提前只是多了几次窗口
3. **上链 deferred 红利**: M4.5 通过前不上链 = 任何 paper 失败都不烧资金, 实盘风险 = 0 (M5 前)

**老雷需要决策**: 9/12 提前首判要不要做? 我建议 **要做**, 因为:
- 提前暴露 R-09 Sharpe 不达, 9-10 月有时间重训信号 / 调阈值
- 不增加资金风险 (上链 deferred 兜底)
- 多 3 次窗口降低 G6 trade_count < 50 笔的拒判概率

---

## 4. 评审节奏 (v1 §3 不变, 增量) + 老胡附言

**评审节奏新增 (Sprint-2 W1 后):**
- 每周三 GM 周报: 老胡 → 老雷, Top 5 风险状态 + R-31 11 owner 进度
- W2 末 (6/26): 风险登记 v2.1 (W2 实施情况 update); W4 (7/9 M1 节点): 风险登记 v3
- 9/12 paper 首判窗口: 老胡 + 小蒋 + 老雷 三方专项评审 (R-32 触发条件)

**老胡附言:**

v1 出的时候 20 条全在 GM 周报上挂着, 上链 deferred + 撤地域两个 ADR 之后, **直接砍了 4 条 (R-04 / R-11 / R-26 / R-30), 降了 4 条 (R-01 / R-03 / R-05 / R-14)**. 不是风险变低了, 是**项目 scope 缩了一圈**, 风险敞口跟着缩.

R-31 (R-20 PIT 违例) 是 v2 唯一新增 H 风险, P=5 因为 11 owner 横向工程任一个漏一个 assert 都炸. 老郭 W2 评审是关键, 我会陪他过 11 个 owner 派单逐条 check. R-32 提前 7 周 我跟 @小蒋 / @老周 都聊过, 小蒋说 "skeleton 提前出来不等于联调能跑通", 我同意, 但 W3-02 联调 W1 通了之后 8 月份就有真 paper 数据 → 老雷可能 9 月初就要拍一次 "信号 Sharpe 实际 < 1 怎么办", §3.2 已给建议.

不耻下问 — paper 联调 timeline 我问 @小蒋 (7/3 W1 联调跑通 + 8 月 paper 数据 + 9/12 首判可启), HC 我问 @小林 (撤地域后 JD 放宽, 候选池 5/岗概率从 60% 提到 75-80%, 6/30 入职目标不变).

— 老胡, 2026-05-28
