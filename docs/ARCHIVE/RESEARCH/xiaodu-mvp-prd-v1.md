# MVP PRD v1

- Owner: 小杜 (product-manager)
- Last review: 2026-05-28
- 验收人: 老钱 (CPO) + 老雷 (GM)
- 关联文档:
  - `CLAUDE.md` (公司宪法)
  - `docs/OKR/2026-Q2-Q3-startup-season.md` (起步季 OKR)
  - `docs/RESEARCH/laoqian-mvp-scope-rejection-v1.md` (老钱 scope 拒绝清单, 本 PRD 的 scope 锚)
  - `docs/RESEARCH/xiaoliang-market-structure-v1.md` (小梁市场结构)
  - `docs/RESEARCH/laohan-riskmanager-design-v0.1.md` (老韩 RM 设计)
  - `docs/RESEARCH/laozhou-architecture-v0.1.md` (老周架构)
- 状态: v1 草稿, 待老钱 + 老雷会签
- 验收对接: 小颖 (qa)

---

## 0. 文档目的 (read me first)

本 PRD 回答**一个问题**: **"MVP 上线时它长什么样, 用户怎么用, 怎么算验收通过"**.

- 不重写老钱 scope (引用 + 整合).
- 不重写老韩 RM 规则 (引用 + 翻译成产品语言).
- 不重写老周架构 (引用 + 站在用户视角讲).
- 不重写小梁信号 (引用 + 转成"用户看到的产物").

本文是**产品视角的 SSOT**: 所有 agent 看完应当对"MVP 长什么样"有一致认知; 任何 scope 争议先比对本 PRD, 仍不清 → @老钱 走升级路径.

---

## 1. 产品定位

### 1.1 一句话

**sports-trader-cpp 是一个 C++ 自营量化交易系统, 在 Polymarket NBA/NFL/MLB Moneyline 盘口上, 以 1 套定向交易策略, 经强制风控门禁, 自动执行 pregame + inplay 关键节点的 taker 入场, 服务 4 人核心运维内部用户.**

### 1.2 五句话定位

1. **业务范围:** 仅 Polymarket, 仅 Moneyline, 仅 NBA + NFL + MLB; 不做 maker, 不做跨平台, 不接外部用户 (引用老钱 scope §2 + §3).
2. **核心价值:** 把"Polymarket 体育市场是不是正期望游戏"这个命题, 用 C++ 严谨工程 + 量化纪律, 在 24 周内**得到一次实盘验证**, 而非做大做全 (引用 CLAUDE.md §2 MVP 定义).
3. **用户模式:** Level-2 半自动 — 信号自动生成 + 自动下单, 但开收盘人工 arm/disarm, 风控触发后必须人工复位 (引用老钱 §3.8).
4. **技术形态:** CLI + Grafana + 邮件日报, 无 Web 前端, 无移动端, 4 人内部用户 (引用老钱 §2 + §3.4).
5. **成功标准:** T+24 周完成实盘首笔成交, 此后 72h 系统零崩溃 + 零风控失效 (引用 OKR KR-C-1/C-2/C-3, CLAUDE.md §2).

### 1.3 不是什么 (反向定位)

- ❌ 不是面向外部交易员的 SaaS 产品
- ❌ 不是覆盖全盘口的"做市平台"
- ❌ 不是 ML/DL 平台
- ❌ 不是高频做市机
- ❌ 不是跨平台体育 arb 引擎

(详见老钱 §3 + §4.4 永不做清单)

---

## 2. 目标用户

MVP 只有**内部 4 人**, 0 外部用户. 用户即开发者, 即运维, 即风控.

### 2.1 用户画像总览

| Persona | 真实身份 | 主要场景 | 工具 | 核心指标关心点 |
|---|---|---|---|---|
| **P1 内部操作员** | 老雷 (GM) | 每日 arm/disarm, 异常处置, 收盘复盘 | CLI + Grafana + 邮件 | 在线率 / PnL / 风控失效数 |
| **P2 量化研究员** | 小梁 + 小程 | 信号迭代, 回测, OOS 验证 | CLI + replay + 日志 | Sharpe / 胜率 / edge 准确性 |
| **P3 风控员** | 老韩 + 老沈 | 监控 RM 状态, 拒单理由, audit 复核 | CLI + audit log + Grafana | 拒单分布 / 状态转移 / 红线触发 |
| **P4 架构观察员** | 老周 + 老郭 | 系统稳定性, 延迟预算, 故障复盘 | Grafana + 日志 + core dump | p99 延迟 / 在线率 / OOM |

**注:** 老钱 (CPO) 不算"日常用户", 算"产品决策人". 他看月度 review.

### 2.2 各用户使用场景详述

#### P1 操作员 (老雷)

- **日频任务:**
  - 比赛日开盘前 30min arm 系统 (CLI 命令)
  - 监控 Grafana 主看板 (PnL / 在线率 / 拒单数)
  - 异常告警 (PagerDuty 等价) 后第一时间介入
  - 收盘后 disarm + 查看日终邮件
- **周频任务:**
  - 周一站会带 PnL 周报
  - 周五看风控周报 (老韩出)
- **决策权:** 红线触发后是否人工复位 (双人 ack 之一)
- **不做:** 不改信号参数, 不改风控阈值, 不改代码

#### P2 量化研究员 (小梁 + 小程)

- **日频任务:**
  - 看当日成交回放, 判断信号触发逻辑是否符合预期
  - audit log 抽样: 拒单理由是否合理
- **周频任务:**
  - 用 replay 跑 OOS 验证, 看 Sharpe drift
- **MVP 期间不做:** 不在线改信号参数 (老钱 §3.3 锁 M2 之后到 M4 前不改)
- **MVP 之后:** 信号 v2 + portfolio 优化

#### P3 风控员 (老韩 + 老沈)

- **日频任务:**
  - 看 RM 状态时间序列 (RUNNING/WARNING/HALTED 转移)
  - 拒单理由分布 + 异常码 (`INTERNAL_ERROR` 必查)
  - bankroll vs exposure 实时比对
- **应急任务:**
  - 红线触发后必须**人工复位** (`双人 ack`, 老韩 §3.6 + §4.3)
  - 影子模式期间 (M3-M4) 复盘拒单是否过严/过松
- **决策权:** 任何状态从 HALTED → RUNNING 必经其手

#### P4 架构观察员 (老周 + 老郭)

- **日频任务:**
  - 看延迟 p99 直方图 (老周 §11 预算)
  - 看模块崩溃 / OOM / coredump (老周 §9.3 fail-fast)
- **周频任务:**
  - 出在线率周报, 异常事件 RCA
- **决策权:** 架构红线一票否决 (CLAUDE.md §8)

---

## 3. 核心场景 (用户旅程地图)

围绕 **"一个比赛日"** 展开. 每个阶段标注主用户 + 系统状态 + 关键交付物.

### 3.1 全景旅程

```
T-1d                                                                              T+1d
  │                                                                                │
  ▼                                                                                ▼
[启动] → [配置] → [arm] → [pregame 监控] → [inplay 监控] → [干预 (可选)] → [disarm] → [日终复盘]
  │        │       │          │                │                │              │           │
  P4       P1+P3   P1         P1+P4            P1+P4            P1+P3          P1          P1+P2+P3
```

### 3.2 阶段详述

#### 阶段 1: 启动 (cold start)

- **主用户:** P4 (老周/老郭) 监督, P1 (老雷) 执行
- **触发:** 系统首次部署 / 进程崩溃后 systemd 拉起 (老周 §9.2)
- **系统行为:**
  - 加载 TOML config (老周 §7)
  - 启动期 self-check (RM § 8.4)
  - WAL 重放 → 重建 PositionLedger + NonceManager
  - 与链上 + Polymarket REST 对账
  - 对账不一致 → 进入 SAFE_MODE (只撤不开仓), 告警
  - 对账一致 → 等 first heartbeat → 状态 = `HALTED` (默认), 等人工 arm
- **用户感知:** CLI 输出启动 checklist, 全绿才可下一步
- **交付物:** `stcpp-trader status` CLI 命令显示 `READY (HALTED)`

#### 阶段 2: 配置 (config check)

- **主用户:** P1 + P3
- **触发:** 每个交易日开盘前
- **系统行为:**
  - 加载当日比赛白名单 (NBA/NFL/MLB 当日有效场次, 来自 Goalserve pregame)
  - 加载当日 bankroll snapshot
  - 加载当日 RM 参数 (硬上限 + 软上限, 老韩 §7)
- **用户行为:**
  - P1 执行 `stcpp-trader config show` 复查配置
  - P3 复查 RM 参数 (`PER_ORDER_CAP_SOFT`, `DAILY_LOSS_PCT`, `KELLY_FRACTION`)
- **交付物:** 配置摘要 markdown, 标记任何变更

#### 阶段 3: arm (人工开盘)

- **主用户:** P1 (老雷)
- **触发:** 比赛日开赛前 ≥ 30 min (老钱 §3.8 强制规定)
- **系统行为:**
  - 状态 `HALTED` → `RUNNING` (RM audit 一条 `STATE_TRANSITION`)
  - 解锁下单网关
  - WSS 订阅触发, 开始接受 OrderIntent
- **用户行为:**
  - P1 执行 `stcpp-trader arm --date YYYY-MM-DD --ack-checklist`
  - checklist 含: 数据源连通 / 钱包余额 / 链上 nonce / 风控参数 / 合规标签
- **交付物:** arm 成功回执 (含 audit_id) + Slack/邮件通知

#### 阶段 4: pregame 监控 (T-60min → tipoff)

- **主用户:** P1 + P4
- **系统行为:**
  - 信号 v1 (5.2 Pinnacle no-vig 回归) 触发 OrderIntent
  - RM 评估 → APPROVED → CLOB 下单
  - 成交回报 → PositionLedger 更新
- **用户行为:**
  - 看 Grafana 主看板: 实时 PnL / open positions / 拒单数
  - 任何告警进 Slack (老周 §6.4)
- **交付物:** 实时看板更新; 每笔成交立即落 audit + WAL

#### 阶段 5: inplay 监控 (开赛 → 比赛结束)

- **主用户:** P1 + P4
- **系统行为:**
  - 信号 v1 (5.1 Goalserve-Poly 时延 + 5.3 比分-价格失配) 在**关键节点窗**触发
  - 关键节点 = Q1 末 / 半场 / 大比分跳变后 5 min (老钱 §3.7)
  - 全程**不**跟盘 (跨洋延迟承受不起, 老钱 §3.7)
- **用户行为:**
  - 与阶段 4 同, 看 Grafana
  - 持仓自动平仓: inplay 触发的持仓 ≤ 30 min 强制 close (小梁 §8.1)
- **交付物:** 同阶段 4

#### 阶段 6: 干预 (可选, 应急)

- **主用户:** P1 + P3
- **触发条件 (老钱 §3.8):**
  1. 数据源缺失 > 30s (RM 自动转 WARNING/HALTED, 等人工复位)
  2. 钱包余额 / nonce / gas 异常
  3. PnL 偏离日内预算 ±20% (人工 review)
  4. 红线触发 (DAILY_LOSS / CONSEC_LOSS)
- **系统行为:**
  - 自动进入 HALTED, 拒所有新单
  - 告警发出 (Slack + 电话, 老韩 §6.4)
- **用户行为:**
  - P1 + P3 双人 ack 复位 (`stcpp-trader resume --reason ... --operator-1 ... --operator-2 ...`)
  - 必经 audit log 双签 (老韩 §4.3)
- **交付物:** 复位 audit 记录

#### 阶段 7: disarm (人工收盘)

- **主用户:** P1
- **触发:** 每日比赛日收盘后
- **系统行为:**
  - 状态 `RUNNING` → `DRAIN` (只平仓不开仓) → 全部平仓后 → `HALTED`
- **用户行为:**
  - P1 执行 `stcpp-trader disarm --date YYYY-MM-DD`
- **交付物:** disarm 回执 + 当日持仓清零确认

#### 阶段 8: 日终复盘

- **主用户:** P1 + P2 + P3
- **触发:** disarm 完成 30 min 内自动出
- **系统行为:**
  - 生成日终 markdown 报告:
    - 当日 PnL (realized + unrealized)
    - 成交清单 (intent → order → fill → settle)
    - 拒单分布 (按 reject_code)
    - 状态转移序列 (RUNNING/WARNING/HALTED)
    - 数据源 freshness 异常统计
    - 信号触发计数 + 命中率
  - 通过邮件发送 (无 Web 前端)
- **用户行为:**
  - P1 看 PnL + 在线率
  - P2 看信号命中
  - P3 看拒单 + 状态转移
- **交付物:** `reports/YYYY-MM-DD-daily.md` + 邮件

---

## 4. 功能列表 (P0/P1/P2)

**优先级定义:**
- **P0** = MVP 必含, T+24 周前完工, OKR 验收锚
- **P1** = MVP 后期 (M4-M5), 可裁但建议有
- **P2** = MVP 之后, 进 backlog (老钱 §4)

### 4.1 功能全表

| ID | 功能 | 优先级 | Owner | 引用 |
|---|---|---|---|---|
| F-01 | Polymarket WSS/REST 数据摄入 | **P0** | 老李 + 小余 | 老周 §2.2 / 老钱 §2 |
| F-02 | Goalserve inplay/livescore/pregame 摄入 | **P0** | 小董 + 小田#24 | 老周 §2.2 |
| F-03 | OrderBook L2 + 比赛状态实时维护 | **P0** | 小田#8 + 小董 | 老周 §2.2 |
| F-04 | FeaturePipeline (实盘 + 回测共用) | **P0** | 小梁 + 小余 | 老周 §2.2 / D-04 |
| F-05 | 信号 v1 (1 套, 含 Goalserve-Poly + Pinnacle 回归 + 比分失配) | **P0** | 小程 + 小梁 | 小梁 §5.1/5.2/5.3 / 老钱 §3.3 |
| F-06 | RiskManager v1 (10 条规则 + 状态机 + audit) | **P0** | 老韩 | 老韩 §3-§5 |
| F-07 | CLOB 下单 (Limit IOC + Market, taker only) | **P0** | 老李 + 小肖 | 老钱 §2 |
| F-08 | EIP-712 签名 + KMS 隔离 | **P0** | 老孙 | 老周 §2.5 / S1-005 |
| F-09 | NonceManager + GasOracle | **P0** | 老孙 + 老叶 | 老周 §2.5 |
| F-10 | PositionLedger + FillTracker + 链上对账 | **P0** | 小肖 + 老彭 + 老韩 | 老周 §2.5 |
| F-11 | CLI 运维工具 (arm/disarm/status/resume) | **P0** | 老陈 + 小颖 | 老钱 §2 |
| F-12 | Prometheus + Grafana 基础看板 | **P0** | 小郑 | 老钱 §2 / KR-E |
| F-13 | PagerDuty 等价告警 (Slack + 电话) | **P0** | 小郑 | 老韩 §6.4 |
| F-14 | Audit log 系统 (jsonl + sqlite 索引) | **P0** | 老韩 + 小郑 | 老韩 §5 |
| F-15 | WAL + 进程崩溃恢复 | **P0** | 老周 + 小肖 | 老周 §9 |
| F-16 | Heartbeat watchdog (30s 自动 halt) | **P0** | 小余 | D-06 / 老周 §2.2 |
| F-17 | 日终 PnL + 成交清单报告 (markdown + 邮件) | **P0** | 小颖 + 小苏 | 老钱 §2 |
| F-18 | TOML 配置 + 热加载 (软参数) | **P0** | 老陈 | 老周 §7 |
| F-19 | Replay 框架 (回测 + 实盘录制) | **P0** | 小段 + 小宋 | KR-E-4 / 老周 §2.2 |
| F-20 | 影子模式 (shadow mode, 2 周) | **P0** | 老韩 + 老雷 | 老韩 §9.3 |
| --- | --- | --- | --- | --- |
| F-21 | NBA + NFL + MLB 三联赛字段 mapping | **P0** | 小余 + 小田#24 | 老钱 §3.2 / KR-D-1 |
| F-22 | 跨洋链路就近部署 (单点, 无 HA) | **P0** | 老吴 | 老钱 §2 / 老周 §8 |
| F-23 | NTP 时钟同步 + 漂移监控 | **P0** | 老姜 | 老韩 §8.3 |
| F-24 | 单元测试覆盖率 > 70% (核心模块) | **P0** | 老周 + 小宋 | KR-A-2 |
| F-25 | RM 单测覆盖率 > 90% | **P0** | 老韩 + 小宋 | KR-B-2 |
| --- | --- | --- | --- | --- |
| F-26 | 自动重连 (WSS 断线 < 3s) | P1 | 老李 + 老陈 | KR-A-5 |
| F-27 | 信号 OOS 验证报告 (≥ 100 场样本) | P1 | 小程 + 小梁 | 小梁 §8.6 |
| F-28 | 集成测试框架 (端到端 replay) | P1 | 小宋 | KR-E-4 |
| F-29 | 异地 audit 备份 (S3 等价) | P1 | 老韩 + 小郑 | 老韩 §5.1 |
| F-30 | 影子模式拒单分析报告 | P1 | 老韩 + 小颖 | 老韩 §9.3 |
| F-31 | 红线演练剧本 (月度) | P1 | 老韩 + 小宫 (dogfood) | 老韩 §9.4 |
| --- | --- | --- | --- | --- |
| F-32 | Totals 盘口扩展 | P2 (backlog B-01) | TBD | 老钱 §4.1 |
| F-33 | Inplay 全程跟盘 | P2 (B-02) | TBD | 老钱 §4.1 |
| F-34 | 信号 v2 (第 2 套) | P2 (B-03) | TBD | 老钱 §4.1 |
| F-35 | Web dashboard (内部) | P2 (B-08) | TBD | 老钱 §4.2 |
| F-36 | Maker 策略 | P2 (B-06) | TBD | 老钱 §4.2 |
| F-37 | NHL / Soccer 扩展 | P2 (B-05/B-11) | TBD | 老钱 §4.2/4.3 |
| F-38 | HA 双活 / 跨区域部署 | P2 (B-14) | TBD | 老钱 §4.3 |
| F-39 | ML 信号原型 | P2 (B-12) | TBD | 老钱 §4.3 |
| F-40 | 多账户 / 多钱包 | P2 (B-07) | TBD | 老钱 §4.2 |

**P0 功能合计: 25 个** (F-01 → F-25).

### 4.2 P0 / P1 / P2 边界判定

如有歧义, 一律按老钱 §3 拒绝清单走. 任何"我顺便做 X"的 PR (老钱 §5.1 第 8 条) 默认拒.

---

## 5. P0 功能详述

每个 P0 功能格式: 输入 / 输出 / 边界 / 验收标准. 验收对接 @小颖.

### F-01 Polymarket WSS/REST 数据摄入

- **输入:** Polymarket gamma/clob/data REST endpoint + WSS subscription
- **输出:** 内部规范化 `MarketEvent` POD (老周 §2.2)
- **边界:**
  - 仅订阅 NBA/NFL/MLB Moneyline market_id (老钱 §3.1/3.2)
  - 不订阅 Totals / Spreads / Props (拒接)
  - WSS 断线 → 自动重连 + 退避 + 全量重订阅
- **验收 (小颖):**
  - [ ] 接入 ≥ 5 场 NBA + 3 场 NFL + 5 场 MLB 实测样本
  - [ ] 摄入到策略层 p99 < 20ms (老周 §11)
  - [ ] 24h 不丢条 (drop counter = 0)
  - [ ] WSS 断线重连成功率 100% (M1 实测)

### F-02 Goalserve 摄入

- **输入:** Goalserve inplay/livescore/pregame XML/JSON
- **输出:** `MatchState` 事件流 (比分 / 时钟 / 关键事件)
- **边界:** 仅 NBA/NFL/MLB; 字段 mapping 由小余 + 小冯定 (KR-D-1)
- **验收:**
  - [ ] 字段缺失率 < 0.1% (KR-D-5)
  - [ ] inplay 事件 push 延迟 p50 < 1s (小梁 5.1 假设依赖)
  - [ ] 与 Polymarket market_id 准确映射 (映射表入 sqlite)

### F-03 OrderBook L2 + MatchState 维护

- **输入:** F-01 + F-02 事件流
- **输出:** `RcuPtr<OrderBookSnapshot>` + `RcuPtr<MatchSnapshot>`
- **边界:**
  - 增量更新 + 周期 snapshot reconcile
  - 不存历史 book (历史走 replay 录制)
- **验收:**
  - [ ] book 增量与 REST snapshot 对账 100% 一致
  - [ ] 增量更新 p99 < 3ms (老周 §11)

### F-04 FeaturePipeline

- **输入:** OrderBook + MatchState + Pinnacle 历史赔率
- **输出:** `FeatureSnapshot` (≤ 20 维, 小梁手工特征)
- **边界:**
  - 实盘与回测**同一份代码** (老周 D-04, 红线)
  - 不上 feature store 数据库 (老钱 §3.4)
- **验收:**
  - [ ] 回测 + 实盘特征向量逐字段 diff = 0
  - [ ] feature 更新 p99 < 5ms

### F-05 信号 v1

- **输入:** FeatureSnapshot + MatchSnapshot
- **输出:** `OrderIntent` (老韩 §2.2 字段集)
- **边界:**
  - **1 套信号** (老钱 §3.3 硬约束)
  - 信号实现 = 小梁 5.1 + 5.2 + 5.3 三组件的**复合判断** (不算 3 个独立信号, 算 1 套合议)
  - M2 锁版本, M4 前不改公式 (老钱 §3.3)
- **验收:**
  - [ ] IS 回测 Sharpe > 1.0 (≥ 500 场样本, KR-C-4)
  - [ ] 胜率 > 53% (KR-C-4)
  - [ ] OOS 验证 ≥ 100 场, Sharpe 衰减 < 50%
  - [ ] 信号 → OrderIntent p99 < 80μs (老周 §11)

### F-06 RiskManager v1

- **输入:** OrderIntent (老韩 §2.2)
- **输出:** RiskDecision (`APPROVED | REJECTED | DEFERRED`, 老韩 §2.3)
- **边界:**
  - 10 条规则全实装 (老韩 §3, R0-R9)
  - 4 状态机 (`RUNNING/WARNING/HALTED/DRAIN`, 老韩 §4)
  - 双人 ack 才能从 HALTED 复位
  - 红线: 100% 下单门禁, 0 绕过 (D-02)
  - 决策 p99 < 200μs (老韩 G3)
- **验收:**
  - [ ] 单测覆盖率 > 90% (KR-B-2)
  - [ ] 绕过检测 CI 脚本拦截率 100% (老韩 §9.2)
  - [ ] 影子模式 2 周内拒单理由分布合理 (老韩 §9.3)
  - [ ] 1000 QPS 压测 p99 < 200μs (老韩 §9.2)
  - [ ] 任意拒单 30s 内可 grep 出 audit (G2)

### F-07 CLOB 下单

- **输入:** RM APPROVED 后的 Order (含 approved_size_usdc)
- **输出:** Polymarket 链上 tx + fill 回报
- **边界:**
  - 仅 Limit IOC + Market, taker only (老钱 §2)
  - 禁 maker / 挂单 (老钱 §3.3)
- **验收:**
  - [ ] 端到端首笔实盘成交 (KR-C-1)
  - [ ] 提单到 fill p99 < 跨洋 RTT + 100ms
  - [ ] client_order_id 与 RM audit_id 一对一可反查

### F-08 EIP-712 签名 + KMS 隔离

- **输入:** Order 数据
- **输出:** 已签名 tx
- **边界:**
  - 私钥**绝不**落进程 RAM (老周 C8 红线)
  - KMS 同区域部署, RTT < 1ms (老周 §10)
- **验收:**
  - [ ] 安全审计 (老沈 + 老黄): 私钥扫描 0 次命中
  - [ ] 签名延迟 p99 < 100μs (老周 §11)

### F-09 NonceManager + GasOracle

- **输入:** 链上 nonce + gas price
- **输出:** 每笔 tx 的 nonce + gas
- **边界:**
  - nonce 单调递增, 每次递增前 WAL fsync (老周 §9.4)
  - gas oracle 多 provider 故障切换 (老周 D8)
- **验收:**
  - [ ] 重启后 nonce 与链上对账 100% 一致
  - [ ] 1000 笔模拟无 nonce 冲突

### F-10 PositionLedger + 对账

- **输入:** Fill 回报 + 链上 RPC 查询
- **输出:** 实时持仓 + 每小时对账报告
- **边界:**
  - ledger 持久化 (WAL, 老周 §7.3)
  - 对账不一致 → SAFE_MODE (老周 §9.2)
- **验收:**
  - [ ] 单日对账差异 = 0
  - [ ] 重启后 ledger 重放正确 100%

### F-11 CLI 运维工具

- **输入:** 命令行
- **输出:** 系统状态 / 操作回执
- **核心命令:**
  - `stcpp-trader status` — 显示当前状态 (RUNNING/HALTED/DRAIN/WARNING)
  - `stcpp-trader arm --date ... --ack-checklist` — 开盘
  - `stcpp-trader disarm --date ...` — 收盘
  - `stcpp-trader resume --reason ... --operator-1 ... --operator-2 ...` — 双人 ack 复位
  - `stcpp-trader config show` — 查看配置
  - `stcpp-trader positions` — 当前持仓
  - `stcpp-trader audit --intent-id ...` — 查 audit
- **验收:**
  - [ ] 所有命令有 `--help` + 错误回显清晰
  - [ ] 任何写操作有 audit + 回执
  - [ ] arm/disarm 不可绕过 (即使 root 也不行, 强制走 CLI)

### F-12 Prometheus + Grafana 看板

- **输入:** 系统埋点 metric
- **输出:** 实时看板 + Prometheus pull endpoint
- **看板内容 (P0 必含):**
  - 在线率 (uptime)
  - 当日 PnL (realized + unrealized)
  - 各数据源 freshness (lag in ms)
  - RM 状态时间序列
  - 拒单数 + 拒单理由分布
  - p99 延迟 (各阶段, 对照老周 §11 预算)
  - bankroll vs exposure
- **验收:**
  - [ ] 看板加载 < 3s
  - [ ] metric 采集对热路径 p99 影响 < 5%
  - [ ] 关键指标采样率 1Hz, p99 < 5s 延迟

### F-13 告警

- **输入:** metric 阈值 + 状态变更事件
- **输出:** Slack + 电话告警
- **告警规则 (P0 必含):**
  - RM 状态 → HALTED: 立即电话 (老雷 + 老韩)
  - WARNING 持续 > 1min: Slack
  - 数据源 stale > 30s: Slack + 电话
  - 进程崩溃: 电话
  - PnL 偏离日预算 ±20%: Slack
- **验收:**
  - [ ] 告警端到端延迟 < 30s
  - [ ] 演练 (小宫 dogfood): 模拟每类告警 1 次, 全部命中

### F-14 Audit log 系统

- **输入:** RM evaluate / 状态变更 / 关键操作
- **输出:** jsonl 文件 + sqlite 索引
- **边界:** 老韩 §5 schema
- **验收:**
  - [ ] 每次 evaluate 必产 1 条 audit (不多不少)
  - [ ] audit 写入失败 → REJECT(INTERNAL_ERROR)
  - [ ] 24h 内 ≥ 1 万条 audit 检索 < 1s

### F-15 WAL + 崩溃恢复

- **输入:** 关键状态变更 (position / nonce / open orders)
- **输出:** WAL 文件 + 启动期重放
- **边界:** 老周 §7.3 + §9.2
- **验收:**
  - [ ] 模拟崩溃 + 重启, ledger / nonce / open orders 全恢复, 与对账一致
  - [ ] WAL fsync 频率不破坏 p99 < 500μs

### F-16 Heartbeat watchdog

- **输入:** 各数据源 heartbeat
- **输出:** stale 信号 → RM 状态转移
- **边界:**
  - 30s 阈值 → WARNING + DEFERRED
  - 60s 阈值 → HALTED + REJECT
  - 红线 D-06 编入二进制, 不可热改
- **验收:**
  - [ ] 注入 30s 数据停摆, 系统在 30±2s 内自动 WARNING
  - [ ] 60s 停摆自动 HALTED

### F-17 日终报告

- **输入:** 当日 audit + fill + state transition
- **输出:** markdown 报告 + 邮件
- **报告必含:**
  - 当日 PnL
  - 成交清单 (按时间序)
  - 拒单分布
  - 状态转移记录
  - 信号触发计数
  - 异常事件列表
- **验收:**
  - [ ] disarm 完成 30 min 内自动出
  - [ ] 邮件抵达成功率 100%
  - [ ] 报告 markdown 渲染清晰 (老胡 + 小米 复核)

### F-18 TOML 配置 + 热加载

- **输入:** `config/runtime.toml`
- **输出:** RCU swap 后的 ConfigSnapshot
- **边界:**
  - 红线参数 (敞口上限 / 30s 阈值) 编入二进制, 不能热改 (老周 D9)
  - 软参数 (KELLY_FRACTION / spread) 可热改, 但只能朝保守方向 (老韩 §3.3)
- **验收:**
  - [ ] 启动期 self-check 拒绝 HARD 与 SOFT 冲突的 config
  - [ ] 热改单调性: 试图调高 ceiling 必被拒 + audit + 告警

### F-19 Replay 框架

- **输入:** 录制的事件流
- **输出:** 完整回测结果 + 与实盘 audit 序列 diff
- **边界:**
  - 与实盘共用 FeaturePipeline (D-04 红线)
  - CLI 跑, 不做 UI (老钱 §3.4)
- **验收:**
  - [ ] 同一事件流 + 同一 config, replay 与实盘 audit 序列**完全一致** (确定性)
  - [ ] 跑 500 场样本 < 10 min

### F-20 影子模式 (2 周)

- **输入:** 实时信号 + RM 决策
- **输出:** audit 序列 (不下单)
- **边界:** 接入实盘前必跑, 老韩 §9.3
- **验收:**
  - [ ] 2 周内拒单率分布报告 (老韩 + 小颖)
  - [ ] 任何 `INTERNAL_ERROR` = 0
  - [ ] 老雷 + 老韩双签后才解锁实盘

### F-21 NBA/NFL/MLB 字段 mapping

- **输入:** Goalserve + Polymarket 原始字段
- **输出:** 内部 canonical id + outcome 映射表
- **边界:** 仅 3 联赛, 不接其他 (老钱 §3.2)
- **验收:**
  - [ ] mapping 表 sqlite 化, ≥ 100 场实测对账无错
  - [ ] 字段超出范围一律 unmapped + 日志告警

### F-22 跨洋单点部署

- **输入:** 部署节点物理规格
- **输出:** 主进程 + 旁路进程 + KMS
- **边界:**
  - 单点, 无 HA (老钱 §2)
  - HA 是 backlog B-14
- **验收:**
  - [ ] 部署文档完整, 老吴 + 老郭签
  - [ ] 72h 连续在线无崩溃 (KR-C-2 锚)

### F-23 NTP 时钟同步

- **输入:** NTP server
- **输出:** 系统时钟 + 漂移监控
- **边界:** 老韩 §8.3, 老姜负责实测
- **验收:**
  - [ ] 漂移监控 metric 入 Grafana
  - [ ] 单日最大漂移 < 50ms

### F-24 单测覆盖 > 70%

- **范围:** 核心模块 (L1-L5 除三方接口适配层)
- **验收:**
  - [ ] CI 卡 70% (KR-A-2)
  - [ ] 覆盖率周报 (老周)

### F-25 RM 单测 > 90%

- **范围:** RM 所有规则 + 状态机
- **验收:**
  - [ ] CI 卡 90% (KR-B-2)
  - [ ] 边界 case 全覆盖 (老韩 §9.1)

---

## 6. 用户故事 (User Story 格式)

格式: **作为 [角色], 我要 [行为], 以便 [目标]**.

### 6.1 P1 操作员 (老雷)

- **US-01:** 作为操作员, 我要在每个比赛日开盘前 30 min 执行一条 CLI 命令完成 arm + checklist 核对, 以便系统在合规可控状态下开始交易.
- **US-02:** 作为操作员, 我要在 Grafana 主看板一眼看到当日 PnL / 在线率 / 拒单数 / 数据源 freshness, 以便不点开任何细节就能判断系统是否健康.
- **US-03:** 作为操作员, 我要在 RM 触发 HALTED 时立即收到电话 + Slack, 以便 5 min 内介入决策.
- **US-04:** 作为操作员, 我要能与风控员双人 ack 复位 HALTED 状态, 以便在确认无系统性风险后继续交易.
- **US-05:** 作为操作员, 我要在每个比赛日收盘 30 min 内收到日终 markdown 报告邮件, 以便快速复盘.
- **US-06:** 作为操作员, 我**不要**有任何 Web 前端可登录的交易页面 (避免误操作 + 攻击面), MVP 阶段所有写操作只走 CLI.
- **US-07:** 作为操作员, 我要在系统进程崩溃时, 重启后能看到对账状态 (SAFE_MODE / READY) 才决定是否解锁, 以便不在状态不明时盲目下单.

### 6.2 P2 量化研究员 (小梁 + 小程)

- **US-08:** 作为量化研究员, 我要在 replay 框架里用历史事件流重放, 得到与实盘**完全一致**的 audit 序列, 以便信号迭代有可复现基线.
- **US-09:** 作为量化研究员, 我要能查询任意一笔 fill 对应的 OrderIntent + RM decision + 输入 features, 以便复盘信号触发链路.
- **US-10:** 作为量化研究员, 我要在 M2 之后到 M4 之前**不**改信号公式 (老钱 §3.3 锁版), 以便回测 Sharpe 落地有稳定基线.
- **US-11:** 作为量化研究员, 我要在 OOS 验证 Sharpe 衰减 > 50% 时立刻告警, 以便提前发现模型风险.

### 6.3 P3 风控员 (老韩 + 老沈)

- **US-12:** 作为风控员, 我要能在 30s 内根据 intent_id grep 出对应 audit 记录 (老韩 G2), 以便任何拒单都可解释.
- **US-13:** 作为风控员, 我要看到 RM 拒单理由分布按 `reject_code` 聚合, 以便判断阈值是否过严/过松.
- **US-14:** 作为风控员, 我要在影子模式 (M3-M4) 2 周内累积足够 audit 数据 (建议 ≥ 1000 次 evaluate), 以便实盘前给老雷出验收报告.
- **US-15:** 作为风控员, 我要**绝对**禁止任何代码路径绕过 RM 直接调签名 (CI 静态扫描兜底), 以便守住公司一票否决红线.
- **US-16:** 作为风控员, 我要在 PnL 偏离日预算 ±20% 时收到 Slack, 以便提前介入 review.

### 6.4 P4 架构观察员 (老周 + 老郭)

- **US-17:** 作为架构观察员, 我要看到各模块 p99 延迟实时直方图对照老周 §11 预算, 以便发现性能漂移.
- **US-18:** 作为架构观察员, 我要在任意热路径线程出非预期错误时整进程 fail-fast + core dump + alert (老周 §9.3), 以便 RCA 不留状态污染.
- **US-19:** 作为架构观察员, 我要禁止 L1 → 业务层 / L4 → L3 / L5 → L3 等反向依赖 (老周 §4 禁止边), 以便架构纪律可执行.

---

## 7. 非功能需求

### 7.1 性能

引用 **老周 §11 + 老韩 G3** 延迟预算:

| 路径 | p99 上限 | Owner |
|---|---|---|
| 摄入 → 策略可用 (FeatureSnapshot ready) | 20 ms | 老李 + 小余 |
| 信号 + 定价 | 80 μs | 小程 + 小梁 |
| Intent 聚合 + MPSC 入队 | 10 μs | 小梁 |
| RiskManager evaluate | 200 μs (硬约束, 老韩 G3) | 老韩 |
| Router + 签名 + CLOB 序列化 | 210 μs | 老李 + 老孙 + 小肖 |
| **信号 → 出网卡 (本地小计)** | **~ 500 μs (KR-A-3 锚)** | 老周 |

跨洋 RTT 不计入预算, 由 §10 + 老吴 S1-010 最小化.

**老姜 + 小石 (S1-011)** 给 lock-free 原语具体实现, MVP 验收前需有压测数据兜底.

### 7.2 安全

引用 **老沈威胁模型 v1** (Sprint-1 内交付, KR-B-8) + CLAUDE.md §8 红线:

- 私钥**绝不**明文落盘 / 出现在日志 (红线, 违 = P0)
- 签名走 KMS / HSM (S1-005 老孙)
- mTLS / TLS pinning, 所有外部 IO 经 infra/net (老周 §10)
- 私钥扫描 CI 工具 (老沈 S1-006 协同)
- KMS 调用 audit (老孙)

(老沈威胁模型详档在 Sprint-1 末交付, 本 PRD 引用版本号 v1.)

### 7.3 合规

引用 **老黄合规红线清单** (Sprint-1 内交付, KR-B-6) + CLAUDE.md §8:

- 仅 Polymarket, 不跨平台 (避美国博彩合规风险, 老黄确认)
- 不接受外部 LP 资金 (老钱 §3.6, 避证券化合规)
- 不做公开 SaaS (老钱 §4.4 永不做)
- UMA 仲裁挑战期内不开新仓 (小梁 §8.3)
- 协议费敏感性: 假设 fee 涨到 1% 仍有 ≥ 3 个信号盈利 (小梁 §8.5)
- 任何合规变更 → 老黄 24h 内 review

### 7.4 可观测

引用 **小郑监控标准** (S1-018 落地):

- Prometheus pull endpoint
- Grafana 主看板 (F-12)
- 告警 (F-13)
- Audit log (F-14)
- 关键 metric (建议 P0 必埋):
  - `stcpp_intent_total{strategy_tag, decision}`
  - `stcpp_latency_us{stage, quantile}`
  - `stcpp_data_freshness_ms{source}`
  - `stcpp_rm_state{state}`
  - `stcpp_pnl_usdc{type}`
  - `stcpp_position_usdc{market_id}`
  - `stcpp_bankroll_usdc`
- 埋点对热路径 p99 影响 < 5% (小郑 OQ-10)

**疑问 @小郑:** 监控标准 v1 是否已出? 如无, MVP 锚定本 PRD 这版指标, 你后续补.

### 7.5 可用性 (Availability)

- 在线率 ≥ 99% (MVP 期, 北极星是 99.9%)
- 72h 连续无崩溃 (KR-C-2, MVP 验收硬指标)
- WSS 断线重连 < 3s (KR-A-5)
- 进程崩溃 → systemd 重启 → 对账完成前**禁止下单** (SAFE_MODE)

### 7.6 可追溯

- 所有 RM 拒单 / 关键决策 / API 变更全部 audit log (CLAUDE.md §7 第 6 条)
- audit 保留 ≥ 90 天 (MVP), 异地备份 P1 (F-29)

---

## 8. 验收标准 (MVP 上线 = 满足)

**MVP 验收公式 = OKR KR-C-1..C-4 全过 + 本 PRD §5 P0 功能全过 + 影子模式签字.**

### 8.1 量化指标 (硬验收)

| 指标 | 阈值 | 来源 | 验收人 |
|---|---|---|---|
| 实盘首笔成交 | T+24 周 (2026-11-12) 前 | KR-C-1 / 老钱 §1 | 老雷 |
| 系统连续在线 | 首笔成交后 ≥ 72h, 0 崩溃 | KR-C-2 / 老钱 §1 | 老周 + 老雷 |
| 风控失效事件 | = 0 起 | KR-C-3 | 老韩 |
| 回测 Sharpe | > 1.0 (样本 ≥ 500 场) | KR-C-4 | 小梁 + 老钱 |
| 信号胜率 | > 53% | KR-C-4 | 小梁 |
| OOS Sharpe 衰减 | < 50% | 小梁 §8.6 | 小梁 |
| 热路径 p99 | < 500 μs (信号→下单指令) | KR-A-3 | 老周 |
| 摄入 p99 | < 20 ms | 老周 §11 | 老周 |
| RM 决策 p99 | < 200 μs | 老韩 G3 | 老韩 |
| WSS 重连 | < 3 s | KR-A-5 | 老周 |
| 数据管道可用率 | > 99.5% | KR-D-3 | 小余 |
| 字段缺失率 | < 0.1% | KR-D-5 | 小余 |
| RM 单测覆盖 | > 90% | KR-B-2 | 老韩 + 小宋 |
| 核心模块单测 | > 70% | KR-A-2 | 老周 + 小宋 |

### 8.2 风控指标 (硬验收, 任何一项 = 0)

| 指标 | 阈值 | 验收人 |
|---|---|---|
| 绕过 RM 下单 | 0 | 老韩 + 老郭 |
| 私钥明文出现 (日志/落盘) | 0 | 老沈 + 老黄 |
| 回测与实盘数据处理逻辑不一致 | 0 | 老周 + 小梁 |
| 数据 schema 静默变更 | 0 | 小余 |
| 跳过架构评审上重大变更 | 0 | 老郭 |

### 8.3 产品体验验收 (软验收, 老钱主观)

| 项 | 要求 |
|---|---|
| CLI 命令一致性 | 所有命令风格统一, --help 全, 错误回显清晰 |
| Grafana 看板 | 加载 < 3s, 一眼看健康度 |
| 日终报告 | 邮件准时 (disarm 后 30min 内), markdown 清晰 |
| 告警噪音 | 误报 < 5% (影子模式实测) |

### 8.4 影子模式签字 (实盘前置门)

引用老韩 §9.3:

- **2 周影子** 期间累积:
  - ≥ 1000 次 RM evaluate
  - 拒单分布合理 (无 `INTERNAL_ERROR`)
  - 状态机所有转移路径至少触发 1 次
- 老韩 + 老雷 + 小梁 + 老钱 **四方双签**, 才解锁实盘.

### 8.5 验收对接 (小颖)

小颖 (qa) 负责把本 §5 + §8 转成可执行测试用例清单. 时间线:
- T+18 周: 测试用例清单 v1 提交 (覆盖 25 个 P0)
- T+20 周: 集成测试跑通 (F-28 P1 提前到此)
- T+22 周: 影子模式启动
- T+24 周: 实盘首笔

---

## 9. 不做 (Out of Scope)

**全量引用老钱 scope 拒绝清单 v1 (`docs/RESEARCH/laoqian-mvp-scope-rejection-v1.md`)**, 此处不重写, 只列**最易混淆的 12 条**, 任何挑战走老钱 §5 升级路径:

### 9.1 盘口

- ❌ Totals / Spreads / Period / Half / Series / Prop / Outright (老钱 §3.1)
- ❌ Parlays (老钱 §4.4 永不做)
- ❌ Alt lines

### 9.2 体育

- ❌ NHL / Soccer / Tennis / UFC / 大学体育 / F1 / Golf / eSports / 其他 (老钱 §3.2)
- ❌ Politics / Crypto / 文化等 Polymarket 非体育市场

### 9.3 策略

- ❌ 信号数量 > 1 (老钱 §3.3)
- ❌ Maker / 做市 / 报价 / 挂单
- ❌ Arbitrage / 跨市场对冲 / 套保
- ❌ ML / DL / 在线学习
- ❌ 在 M2-M4 期间在线改信号公式

### 9.4 功能

- ❌ Web 前端 / 移动端 / 公开 API (老钱 §3.4)
- ❌ 多账户 / 多钱包
- ❌ 回测 UI / 特征仓库 / A/B 实验平台
- ❌ 跨平台 (DraftKings / Pinnacle / Kalshi 等)
- ❌ 配置热更新 UI / 自愈 / Chaos engineering

### 9.5 用户

- ❌ 外部 LP / 投资人 / 公开 SaaS / 白标客户 (老钱 §3.6, 永不做)

### 9.6 时间窗

- ❌ Inplay 全程跟盘 (只做关键节点窗, 老钱 §3.7)
- ❌ 赛前 T-24h 之前长尾市场
- ❌ NBA Summer League / preseason

### 9.7 自动化等级

- ❌ L3 全自动 (MVP 锁 L2, 老钱 §3.8)
- ❌ L4 自适应 (永不在 MVP)

**任何不在本 §9 / 老钱 §3 明确允许范围内的 PR, 默认 = scope creep, 走老钱 §5.2 升级路径.**

---

## 10. 风险 + 缓解

### 10.1 风险登记表 (按"影响 × 概率"排序)

| # | 风险 | 概率 | 影响 | 主要 Owner | 缓解措施 | 引用 |
|---|---|---|---|---|---|---|
| R-01 | **跨洋链路抖动导致信号 5.1 时延优势消失** | 高 | 高 | 老吴 + 老姜 + 小梁 | 实测 Δ vs D 决定 5.1 是否成立; 不成立则信号主权重移到 5.2 + 5.3 | 小梁 §5.1 / OQ Q4 |
| R-02 | **OOS Sharpe 衰减 > 50%, 实盘验收不过** | 中 | 致命 | 小梁 + 小程 | 严格 ≥ 100 场 OOS 窗; 分数 Kelly 1/4 起步; 信号触发置信门 | 小梁 §8.6 / KR-C-4 |
| R-03 | **Polymarket 流动性薄 → fill < approved_size** | 高 | 中 | 小肖 + 老韩 | v0.2 引入 slippage model; MVP 单笔上限 ≤ $1K 控滑点 | 老韩 R-1 / 小梁 §3.4 |
| R-04 | **跨洋 stale 频发, RM 频繁 DEFERRED 错过机会** | 高 | 中 | 老韩 + 老姜 | stale 阈值实测后微调; 监控 DEFERRED 占比 | 老韩 R-2 |
| R-05 | **私钥泄漏 / KMS 故障** | 低 | 致命 | 老孙 + 老沈 | KMS 独立部署 + mTLS; 私钥扫描 CI; 灾备演练 | CLAUDE.md §8 / S1-005 |
| R-06 | **scope creep 漏进 MVP** | 中 | 中 | 老胡 + 老钱 | 双周 retro anti-creep 复盘; 升级路径明文化 | 老钱 §5 |
| R-07 | **24 周时间表延误** | 中 | 高 | 老胡 + 老雷 | 里程碑延误率 < 20% (KR-E-3); M1-M5 节点 review | OKR §E |
| R-08 | **进程崩溃后对账不一致进 SAFE_MODE 长时间** | 中 | 中 | 老周 + 小肖 | WAL fsync 严格; 启动重放 + 多源对账; 演练月度 | 老周 §9.2 |
| R-09 | **Polymarket 协议变更 (API/费率)** | 中 | 中 | 老李 + 老胡 | API 变更响应 < 2h (KR-D-6); 协议费敏感性测试 | 小梁 §8.5 |
| R-10 | **影子模式拒单率过高/过低, 实盘前推迟** | 中 | 中 | 老韩 + 小颖 | 影子模式预留 2 周; 拒单分析报告 (F-30) | 老韩 §9.3 |
| R-11 | **NTP 漂移超预期 → audit 时间戳不一致** | 低 | 中 | 老姜 | 漂移监控 metric; 漂移 > 50ms 告警 | 老韩 §8.3 / OQ-2 |
| R-12 | **招聘老冀/小秦延误 → 关键 owner 缺位** | 中 | 中 | 小林 + 老雷 | Q2 招聘 SOP v1 (6/13); JD 响应 < 48h | KR-E-7/8/9 |
| R-13 | **回测与实盘特征管道双套实现 (D-04 红线被违)** | 低 | 致命 | 老周 + 小梁 | CI 静态扫描共用代码; 架构评审兜底 | CLAUDE.md §8 / 老周 D-04 |
| R-14 | **绕过 RiskManager 下单 (D-02 红线被违)** | 低 | 致命 | 老韩 + 老郭 | CI grep 签名调用必前序 RM; 月度红线演练 | CLAUDE.md §8 / 老韩 §9.4 |
| R-15 | **Polymarket 单 market 操纵 → 信号触发被钓鱼** | 中 | 中 | 小程 + 老韩 | 只在 24h 平均 depth > $50K 的 market 上交易; UMA 挑战期内不开新仓 | 小梁 §8.3 |

### 10.2 最大风险 (单选)

**R-02: OOS Sharpe 衰减 > 50%, 实盘验收不过 KR-C-4 阈值, 整个 MVP 失败.**

理由: 这是**唯一可能让 24 周努力归零**的风险. 跨洋 / 流动性 / 安全 风险都可以工程缓解, 但模型不 work = 公司商业假设错误, 没有 plan B. 缓解优先级最高, 由小梁 + 小程在 M2 (T+10 周) 前给出 IS + OOS 双 Sharpe 报告. 若 OOS Sharpe < 0.8, 必须立即触发战略复盘 (老雷 + 老钱 + 小梁 三方), 不可硬扛到 M5.

### 10.3 风险监控节奏

- 每周风控例会 (周五, 老韩主持) 过一遍 R-01 → R-15
- 每月架构评审 (老郭) 过 R-13 / R-14 红线
- 每月策略评审 (小梁) 过 R-02 / R-03 / R-09
- 任意红线 (R-13 / R-14) 触发 → 立即 P0 incident + post-mortem

---

## 附录 A: 与 OKR 对照

| OKR KR | 本 PRD 覆盖项 |
|---|---|
| KR-C-1 (MVP 实盘) | §1.1 + §8.1 实盘首笔 |
| KR-C-2 (72h 无崩溃) | §8.1 + F-22 + F-15 |
| KR-C-3 (风控失效 = 0) | §8.2 + F-06 + F-14 |
| KR-C-4 (Sharpe > 1.0) | §8.1 + F-05 + F-27 |
| KR-A-1..A-6 | F-22 + F-01-F-03 + §7.1 |
| KR-B-1..B-8 | F-06 + F-14 + §7.2 + §7.3 |
| KR-C-1..C-6 (Quant) | F-04 + F-05 + F-19 + F-27 |
| KR-D-1..D-6 | F-01 + F-02 + F-21 |
| KR-E-1..E-9 | §3 旅程 + §8.5 (小颖) + F-28 |

## 附录 B: 与 Sprint-1 ticket 对齐

| Sprint-1 Ticket | 在本 PRD 的体现 |
|---|---|
| S1-001 架构 v0.1 | §7.1 性能 + F-22 + §10 R-13 |
| S1-002 CLOB API 实测 | F-01 + F-07 |
| S1-003 Goalserve 实测 | F-02 |
| S1-004 RiskManager 设计 | F-06 + §8.2 |
| S1-005 私钥 KMS | F-08 + §7.2 |
| S1-006 合规红线 | §7.3 |
| S1-007 市场结构 | §1 + §10 |
| S1-008 MVP scope | §9 全章 |
| S1-009 Polygon RPC | F-09 |
| S1-010 跨洋部署 | F-22 |
| S1-011 lock-free | §7.1 |
| S1-012 行业分析 | §10 R-09 |
| S1-017 信号假设 | F-05 + §10 R-02 |
| S1-018 监控 | F-12 + F-13 + §7.4 |
| S1-023 dogfood 剧本 | §3 旅程 + F-13 演练 |

## 附录 C: 签收

| 角色 | Owner | 状态 |
|---|---|---|
| GM (验收人) | 老雷 | ⬜ pending |
| CPO (验收人 + scope 仲裁) | 老钱 | ⬜ pending |
| Architect | 老周 | ⬜ pending |
| Risk | 老韩 | ⬜ pending |
| Quant | 小梁 | ⬜ pending |
| QA (验收对接) | 小颖 | ⬜ pending |
| PM (进度对齐) | 老胡 | ⬜ pending |
| Doc curator | 小米 | ⬜ pending |

## 附录 D: 修订记录

| 版本 | 日期 | 修订 | 人 |
|---|---|---|---|
| v1.0 | 2026-05-28 | 初版, 整合老钱 / 小梁 / 老韩 / 老周 输入, 锁 P0 = 25 个功能 | 小杜 |

---

**END v1.** 等老钱 + 老雷会签后转 v1.1; M2 前根据 S1-002/S1-003/S1-007/S1-017 实测数据回灌 §10 风险量化 + §7.1 延迟实测.

— 小杜, 2026-05-28
