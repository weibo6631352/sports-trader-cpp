# 业务能力清单 v1 — 老钱 (CPO)

> **Owner:** 老钱 (CPO, 与老雷平级, 产品方向决策权)
> **建立:** 2026-05-28 (Wave 22)
> **触发:** 用户原话 "更重要的是你需要什么, 而不是看他有什么。 接口可以他来定, 需求可是一定要你提的。"
> **权威输入:** `docs/MEETINGS/2026-05-28-gm-business-needs-must-haves.md` (GM 老雷 M-01..M-10) + `docs/OKR/2026-Q2-Q3-startup-season.md` (起步季 OKR)
> **禁忌遵守:** 本文档未读 include/src/ 任何代码, 未读 Polymarket/Goalserve 官方文档, 未读 docs/RESEARCH/ 其他报告. 纯战略倒推.
> **下游:** 小杜 (PM) 写 PRD user journey, 小颖 (需求分析师) 写 acceptance spec.

---

## Part 0: 倒推链条

```
公司 mission (赚 PnL, 可预期正期望游戏)
    ↓
北极星 T+36 月 ($5M / Sharpe 1.5 / DD 15% / 99.9% uptime)
    ↓
MVP M1 (T+6 月 Moneyline 实盘 + 第一笔成交 + 0 风控失效 + 72h 无崩溃)
    ↓
GM must-have M-01..M-10 (老板视角不可砍)
    ↓
业务能力清单 (本文档, 16 个 BC)
    ↓
PM PRD user journey (小杜) + 需求分析师 acceptance spec (小颖)
    ↓
工程实现 (老周 / 老韩 / 小梁 / 小余 / 老胡 五战斗单元)
```

**正向需求 = "我们要什么"**, 不是 "后端有什么". 接口让工程定, 需求 CPO 拍.

---

## Part 1: 业务能力清单 (BC-01 ~ BC-16)

> 格式: 目的 / 输入 / 输出 / 优先级 (P0 / P1 / P2) / Stage (M1 / M4.5 / M5).
> P0 = MVP 不可砍, P1 = M4.5 解锁前必须, P2 = M5 实盘前必须.

### BC-01 价格发现能力 (Price Discovery)

- **目的:** 知道每个 PM market 的"真实价格" (fair value), 不被 PM mid 牵着鼻子走
- **输入:** 外部锐利锚 (Pinnacle / 其他锐利 sportsbook) + PM 微观结构 (orderbook depth / spread / impact)
- **输出:** 每个 condition_id 的 fair_value + 我们的 edge (fair_value − PM_mid)
- **优先级:** P0
- **Stage:** M1
- **依据:** mission 核心 ("可预期正期望游戏" 必须先有 fair value 信念)

### BC-02 信号生成能力 (Signal Generation)

- **目的:** 当 fair_value 与 PM mid 偏离 > 阈值时, 触发 trade idea
- **输入:** BC-01 输出 + signal config (Kelly fraction / 偏离阈值 / 信号开关)
- **输出:** trade intent (buy/sell, condition_id, size, side, signal_id, audit_id)
- **优先级:** P0
- **Stage:** M1 (首发 P0-01 Pinnacle no-vig anchor)
- **未来扩展:** P0-02..12 (12+ 信号), 每个独立可暂停 / 启用 (M-08)
- **决策权:** 增删 signal 必须老钱 + 小梁联签

### BC-03 风控决策能力 (Risk Decision)

- **目的:** 每个 trade intent 必须过风控网关 (RM), 不允许任何下单链路绕过 (CLAUDE.md 红线)
- **输入:** trade intent + 当前 portfolio state + RM config (Kelly / exposure / DD 阈值)
- **输出:** allow / reject + 21 种 reject reason + RM state (NORMAL / SAFE_MODE / HALTED / DRAIN)
- **优先级:** P0 (红线)
- **Stage:** M1
- **决策权:** RM 阈值调整必须 老韩 + 老郭 联签, GM 单方面不能改 (M-08)
- **手动触发:** HALTED / SAFE_MODE / DRAIN 必须能手动按钮 (M-05)

### BC-04 订单执行能力 (Order Execution)

- **目的:** 把 RM 允许的 intent 转成 PM order, 上链或上 paper engine
- **输入:** RM allowed intent
- **输出:**
  - paper 模式: 虚拟成交 (不真上链, 不污染真账本) — CLAUDE.md R-11 红线
  - live 模式: 签名 EIP-712 → 上 Polygon → PM CLOB 撮合
- **优先级:** P0 (paper) / P1 (live, M4.5 后才解锁)
- **Stage:** M1 (paper) / M5 (live)

### BC-05 持仓管理能力 (Position Management)

- **目的:** 任何时刻清楚知道当前手上有什么 (paper / live 都要)
- **输入:** 成交事件流 (fill / partial / cancel / settle)
- **输出:**
  - 每个 condition 当前持仓 (qty, avg_price, side)
  - 总 exposure_pct vs 红线 (默认 2%)
  - unrealized_pnl + realized_pnl (mark-to-market)
- **优先级:** P0
- **Stage:** M1

### BC-06 PnL 核算能力 (PnL Accounting)

- **目的:** 老板每天能看清今天赚了还是亏了 (M-01)
- **输入:** BC-05 持仓 + 历史成交
- **输出:**
  - 今日 PnL 一个大数字 (绿 / 红)
  - 7d / 30d / Sprint / 季度趋势
  - 按 signal / sport / market type / maker_taker 分拆 (M-06)
- **优先级:** P0 (今日 PnL 单数字) / P1 (分拆)
- **Stage:** M1 (P0 部分) / M4.5 (P1 分拆)

### BC-07 结算对账能力 (Settlement Reconciliation)

- **目的:** 比赛结束后, 我们预期结果 vs PM 实际 settle 结果对账, 任何偏差立刻 alert (fraud / bug 信号)
- **输入:** 比赛 final score + PM settle event + 我们持仓
- **输出:** 对账 OK / 对账偏差 (差额 + 原因猜测 + alert)
- **优先级:** P1
- **Stage:** M4.5 (paper 阶段先 simulate settle, M5 上链后真对账)
- **CPO 决策:** 偏差 > $100 必须 GM + 操盘手双签确认, 不允许"静默吃掉"

### BC-08 审计回放能力 (Audit Replay)

- **目的:** 任何一笔决策, 给定 audit_id, 必须能拉出完整链路 (M-03)
- **输入:** audit_id
- **输出:** 完整链路 PIT snapshot:
  - 触发 signal + signal config 当时状态
  - signal 看到的 PM book + Pinnacle quote
  - RM 决策 (allow / reject + reason)
  - signer + matcher 事件
  - settle 结果
- **优先级:** P0
- **Stage:** M1
- **技术红线:** BLAKE3 hash chain 不可篡改 (M-02)
- **业务意义:** ML 训练数据基础 + 事故追责基础 + 策略迭代基础, **三合一**

### BC-09 性能监控能力 (Performance Monitoring)

- **目的:** 老板看系统健康度 (M-07)
- **输入:** WSS 连接 / 决策 latency / WAL fsync / vCPU 利用率
- **输出:**
  - WSS 状态 (连 / 断 / 重连倒计时)
  - 决策 latency p99 (端到端 / 各阶段)
  - WAL fsync 频率 + 积压
  - vCPU 利用率 (7 核分配)
- **优先级:** P1
- **Stage:** M1 (基础指标) / M4.5 (完整看板)

### BC-10 异常处理能力 (Exception Handling)

- **目的:** 任何模块挂掉, 系统不能"裸奔". 默认 fail-safe (M-09)
- **输入:** 模块异常事件 (signer / matcher / RM / WSS 任一)
- **输出:**
  - 自动 SAFE_MODE (不下新单, 允许平仓)
  - 重要决策 escalate 操盘手 (strategy decay / 大额 reject 流)
  - 7×24h 平时不打扰, 异常立刻打扰 (M-09)
- **优先级:** P0
- **Stage:** M1

### BC-11 策略迭代能力 (Strategy Iteration)

- **目的:** paper 跑起来产生 ML 训练金矿, 让策略持续优化
- **输入:** 完整 audit chain (BC-08) + paper PnL 历史
- **输出:**
  - ML 数据钩子 (paper → 训练集导出)
  - alpha decay 检测 (STRATEGY_DECAYED 状态)
  - backtest / paper / live 共享 binary (CLAUDE.md R-2 红线, 不允许分叉)
- **优先级:** P2
- **Stage:** M4.5 (paper 数据积累) / M5+ (ML 离线训练, ONNX 导出后 C++ 推理)
- **CPO 决策:** M4.5 前 ML 不进生产 (N-03), 起步纯 if-else + 人工调参

### BC-12 紧急止损能力 (Emergency Kill Switch)

- **目的:** GM 一键 halt (M-05) + 自动 strategy decay 检测
- **输入:**
  - GM 手动按钮 (二次确认)
  - 自动检测信号 (Bayesian P(μ<0|data) > 0.3 持续 2 周)
  - DD > 8% 强 halt (M4.5 gate G5)
- **输出:**
  - RM HALTED 状态
  - 已 open position 处理策略 (CPO 决策, 见 Part 5)
  - 解锁需要 RM 三签 (老韩 + 老郭 + 老雷)
- **优先级:** P0
- **Stage:** M1
- **CPO 决策:** halt 后已 open position **默认等到期 + 操盘手可选手动平**, 不自动平 (避免 fire sale 进一步亏损). M4.5 后根据 paper 数据复审.

### BC-13 数据接入可靠性 (Data Ingestion Reliability)

- **目的:** 数据是公司命脉, 跨洋链路高延迟 + 带宽紧, 必须把数据接稳
- **输入:** Polymarket gamma/clob/data REST + WSS + Goalserve inplay/livescore/pregame
- **输出:**
  - 4 时间戳契约 (event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts) — CLAUDE.md R-20 红线
  - 断线自动重连 < 3s (KR-A-5)
  - 数据 schema 变更检测 + 通知下游 (CLAUDE.md 红线)
- **优先级:** P0
- **Stage:** M1

### BC-14 投资人 / 第三方审计能力 (Investor Reporting)

- **目的:** 业绩可独立第三方审计 (M-10)
- **输入:** BC-06 PnL + BC-08 audit chain
- **输出:**
  - 一键导出: 季度 PnL + Sharpe + DD + 笔数 + 净收益
  - PDF / CSV (不需 fancy UI, N-01)
  - 内部数据不出公司, 但**业绩数字 + audit hash** 可给审计方核验
- **优先级:** P2
- **Stage:** M5 (实盘后才有意义)
- **CPO 决策:** 我们自己操盘, 不做 SaaS, 不接外部 LP, 但**未来如果接 friend money 必须先有这个能力** (前置准备)

### BC-15 M4.5 解锁状态可视化 (M4.5 Gate Readiness)

- **目的:** GM 一眼看公司离实盘还差多远 (M-04)
- **输入:** 7 个 hard gate 实时状态 (G1 PnL / G2 Sharpe / G3 Risk / G4 Uptime / G5 DD / G6 Trades / G7 Shadow)
- **输出:**
  - 每个 gate 当前值 + 阈值 + PASS/FAIL
  - 哪个 gate 卡住 + 卡多久 + 还差多少
  - 累计窗口 + 末窗双判定状态 (OKR §M4.5 注)
- **优先级:** P0
- **Stage:** M4.5 (这能力本身就是 M4.5 gatekeeper)
- **CPO 决策:** 任一 gate fail = 不解锁实盘, **无豁免** (与 OKR OQ-D11 GM 决议一致)

### BC-16 配置变更可追溯 (Config Change Audit)

- **目的:** Kelly fraction / RM 阈值 / signal 开关任何调整可追溯, 防止"半夜偷偷改参数甩锅"
- **输入:** GM / 操盘手 config 调整请求
- **输出:**
  - 谁 (operator) / 什么时间 / 改了什么 / 为什么 (reason 必填)
  - RM 阈值调整必须双签 (老韩 + 老郭) audit 留痕
  - 历史 config 可复原 (回滚)
- **优先级:** P1
- **Stage:** M1 (基础 audit) / M4.5 (完整双签流程)
- **CPO 决策:** 这是合规底线 — 未来如果出事故 (亏大钱), 审计能复原"那一刻 config 是什么", 否则连追责都做不到

---

## Part 2: 业务能力 vs 班底单元 mapping

| BC | Primary 单元 | 协作单元 | Stage |
|---|---|---|---|
| BC-01 价格发现 | C 量化研究 | A 系统 (数据接入) + D 数据基建 | M1 |
| BC-02 信号生成 | C 量化研究 | A 系统 (代码实现) | M1 |
| BC-03 风控决策 | B 风控合规 | A 系统 (RM 实现) + F 顾问 (架构评审) | M1 |
| BC-04 订单执行 | A 系统工程 | B 风控 (网关) + F 顾问 (老叶 onchain) | M1 paper / M5 live |
| BC-05 持仓管理 | A 系统工程 | C 量化 (PnL 算法) | M1 |
| BC-06 PnL 核算 | C 量化研究 | A 系统 (落库) + D 数据 (历史) | M1 / M4.5 |
| BC-07 结算对账 | A 系统工程 | B 风控 (异常处理) + D 数据 | M4.5 |
| BC-08 审计回放 | A 系统工程 | B 风控 (audit chain) + D 数据 (查询) | M1 |
| BC-09 性能监控 | A 系统工程 (小郑 observability) | E 产品 (UI) | M1 / M4.5 |
| BC-10 异常处理 | A 系统工程 | B 风控 (SAFE_MODE 决策) | M1 |
| BC-11 策略迭代 | C 量化研究 | F 顾问 (小邓 ML) + D 数据 | M4.5 / M5+ |
| BC-12 紧急止损 | B 风控合规 | A 系统 + E 产品 (按钮 UI) | M1 |
| BC-13 数据接入 | D 数据基建 | A 系统 (网络层) | M1 |
| BC-14 投资人报表 | C 量化研究 | E 产品 (导出 UI) + B 风控 (audit) | M5 |
| BC-15 M4.5 看板 | E 产品业务保障 | C 量化 (gate 算法) + D 数据 | M4.5 |
| BC-16 配置审计 | B 风控合规 | A 系统 (config store) | M1 / M4.5 |

**单元工作量初估 (CPO 视角, 仅指示性, PM 老胡细化):**

- A 系统工程部: primary 7 个 BC (BC-04/05/07/08/09/10) — **最重**
- B 风控合规部: primary 3 个 BC (BC-03/12/16)
- C 量化研究部: primary 5 个 BC (BC-01/02/06/11/14)
- D 数据基础设施部: primary 1 个 BC (BC-13)
- E 产品业务保障部: primary 1 个 BC (BC-15)
- F 顾问团: 全程协作 + 评审, 无 primary

---

## Part 3: 业务流程图 (3 个核心 use case)

### Use case 1: "GM 每天早上看公司" (M-01 + M-06)

**用户:** 老雷 (GM) — 我们老板

**触发条件:** GM 主动登陆系统 (每天早上 / 关键决策前)

**步骤:**

1. 老雷登陆系统 (operator console)
2. 主屏显示一个大数字: **今日 paper PnL = $X** (绿 / 红)
3. 趋势区: 7d / 30d / Sprint / 季度 曲线
4. 分拆区: 哪个 signal 贡献多少 PnL (M-06)
5. 顶部 banner: 是否有 RM HALTED / SAFE_MODE 警告 (无 = 绿条静默, 有 = 红条爆闪)
6. M4.5 gate readiness 一行: "7/7 PASS" 或 "3/7 PASS, G2 Sharpe 卡 7 天"
7. 老雷决定: 一切正常 → 关 dashboard. 异常 → drill-down 进 use case 3

**关键能力:** BC-06 (PnL) + BC-09 (健康度) + BC-15 (M4.5 看板) + BC-12 (halt 警告)

**CPO 验收标准:** GM 30 秒内看清"今天怎么样", 不需要登服务器 grep log (M-01 原话).

---

### Use case 2: "operator 监控 paper engine 7×24h" (M-09)

**用户:** 操盘手 (Operator) — 未来全职岗位, M1 阶段由 GM 兼任

**触发条件:** 系统自动 push (operator 不主动盯屏)

**步骤:**

1. 24h 监控屏挂在墙上 / 后台静默运行
2. **平淡运行不打扰** — 平时只显示心跳 + 关键指标 (PnL / position count / latency p99)
3. **异常立刻打扰** — 触发条件:
   - RM HALTED / SAFE_MODE 进入
   - WSS 断 > 5s
   - 大额 reject 流 (> 10 笔/min)
   - DD 触及 5% / 8% 警戒线
   - strategy decay alarm (Bayesian BLACK)
4. 打扰方式: 邮件 + 浏览器 push + 声音 alarm
5. operator 决断:
   - 误报 → 静默 + 写入 false_positive 集
   - 真问题 → 进 use case 3 drill-down + 决定 manual halt / 调参 / 平仓
6. 重大决策 escalate GM (大额 reject 流 / DD > 5%)

**关键能力:** BC-03 (RM 状态) + BC-09 (监控) + BC-10 (异常处理) + BC-12 (kill switch)

**CPO 验收标准:** operator 不在岗 (晚上睡觉 / 周末出门), 系统该 halt 自己 halt, 不裸奔.

---

### Use case 3: "操盘手复盘单笔异常" (M-03 + M-04)

**用户:** 操盘手 + 量化研究 (小梁单元) + 风控 (老韩单元)

**触发条件:** Use case 2 弹 alert, 或 GM 看到 dashboard 异常 drill-down

**步骤:**

1. 看到 RM reject 流异常 (比如 14:32 那笔大额 reject) 或 PnL 大跌
2. Click in 单笔 audit_id
3. 系统拉出完整 audit chain (BC-08):
   - 触发的 signal (是 P0-01 Pinnacle no-vig 吗?)
   - signal 当时看到的 PM book snapshot (PIT)
   - signal 当时看到的 Pinnacle quote (PIT)
   - RM 决策 + reason code (21 种之一)
   - 如果通过 RM: signer / matcher 事件
   - 如果上链: settle 结果
4. 操盘手分析:
   - 是 signal bug (fair value 算错了)? → 反馈 C 量化研究 (小梁 / 小程)
   - 是 RM 误判 (过严)? → 反馈 B 风控 (老韩)
   - 是数据 bug (Pinnacle 没刷新)? → 反馈 D 数据基建 (小余 / 老李)
   - 是市场异常 (PM 真的乱了)? → 记录到 market_anomaly 集
5. 反馈进入下个 sprint backlog

**关键能力:** BC-08 (audit replay) + BC-09 (drill-down UI) + BC-16 (config 当时状态)

**CPO 验收标准:** 任意一笔 audit_id, 5 分钟内能拉出完整 chain. 1 周内异常笔数收敛 (策略迭代有效).

---

## Part 4: M1 / M4.5 / M5 各 stage 业务能力清单

| Stage | 时间 | 必备 BC | 不需要 BC | 备注 |
|---|---|---|---|---|
| **M1 (T+6 月)** | 2026-11-12 | BC-01, 02, 03, 04 (paper), 05, 06 (单数字), 08, 09 (基础), 10, 12, 13, 16 (基础) | BC-07 (paper 不真 settle), BC-11 (ML deferred), BC-14 (无实盘无业绩), BC-15 (M4.5 才有意义) | 12 个 BC, 全 P0 |
| **M4.5 (T+22 周)** | 2027-05 | M1 全部 + BC-07 (simulate settle) + BC-15 (7 gate 全过) + BC-06 完整分拆 + BC-09 完整看板 + BC-16 完整双签 | BC-11 (ML 离线训练 OK, 推理还不上生产), BC-14 (无实盘 PnL 不出报表) | 14 个 BC, paper 7 hard gate 全过 |
| **M5 (T+24 周)** | 2027-11 | M4.5 全部 + BC-04 live (上链) + BC-07 真对账 + BC-11 部分 (alpha decay 检测) + BC-14 投资人报表 | - | 16 个 BC 全, 实盘运行 |

**CPO 关键观察:**

- M1 不需要 BC-07 / BC-11 / BC-14 / BC-15 — **不要在 MVP 里塞这 4 个, 反人类的过度工程**
- M4.5 加 4 个 BC, 重心在 "证明 paper 能稳定盈利"
- M5 才上链, BC-04 live + BC-07 真对账, **小步快跑** (OQ-D12 单笔 $200 起)

---

## Part 5: CPO 6 项产品方向决策 (单方拍板)

> 老钱跟老雷平级, 产品方向决策权归 CPO. 以下 6 项, 拍板, 不接受讨论 (除非新数据推翻).

### 决策 1: 不做 X (must-not, 与 GM N-01..N-05 一致并扩展)

- **不做 SaaS** — 我们自己操盘自己的钱, 不卖给别人
- **不做多用户** — operator / GM / analyst 3 个 role 足够, 不做 RBAC 矩阵
- **不做 fancy UI** — 黑底白字 + 大字号, 不要拖拽 / 不要主题 / 不要 mobile
- **不做 < 1ms 高频** — 跨洋 200ms RTT 是物理现实, 端到端 50ms 已远超需求
- **不做跨平台** — 只 Polymarket, 只体育, 只 Polygon. M5 前任何"扩 X 平台" 提案拒绝
- **不做自动调参** — M4.5 前 ML 不进生产, 起步 if-else + 人工调参

### 决策 2: 做 X (must, 与 GM 一致)

- **操盘自己的钱** — 这是定位, 不动摇
- **第三方可审计业绩** — BC-14, M5 必须有
- **一键 halt** — BC-12, M1 必须有
- **paper 与 live 共享 binary** — CLAUDE.md R-2 红线, 不允许分叉 (这是 BC-11 基础)

### 决策 3: 优先级 — 风控 > 收益

- 即使少赚也不能踩 RM 红线
- M4.5 gate fail = 不解锁实盘, 无豁免
- DD > 8% 强 halt, RM 三签解锁
- **CPO 心法:** "不踩坑活到下个 sprint", 比 "这笔赚 $100" 重要 100 倍

### 决策 4: halt 后已 open position 处理 (M-05 GM 留给 CPO 拍)

- **默认: 等到期 + 操盘手可选手动平**
- **不自动平** — 避免 fire sale 把小亏变大亏
- M4.5 后根据 paper 实战数据复审, 可能改成 "5% 阈值内手动 / 超 5% 自动平"
- **理由:** halt 触发往往是市场异常, 此时市场流动性差, 自动平 = 自伤. 让 operator 看着办.

### 决策 5: ML 推理 M5 前不进生产

- **M4.5 前:** ML 完全不存在 (起步 if-else)
- **M4.5 后:** ML 可离线训练 + 数据探索 (小邓 #31), 但**推理仍在 C++ + ONNX**, Python 不进生产 (CLAUDE.md §10 语言纪律)
- **M5 后:** alpha decay 检测可用 ML (Bayesian), 但参数调整仍是双签 (老韩 + 老郭)
- **理由:** ML 黑盒 + 不可解释 + 易过拟合, 在我们这个 sample size (起步 < 500 trade) 不适合做主决策

### 决策 6: 信号扩展节奏 (M-08 配方变更归 CPO)

- **M1:** 仅 P0-01 (Pinnacle no-vig anchor) 上线 — **单一信号, 易复盘**
- **M4.5:** 至多 3 个信号上线 (P0-01 + 2 个) — 必须 backtest Sharpe > 1.0 + paper 2 周 PnL > 0
- **M5+:** 信号数量受 capital scale 制约, 单信号最大 capital share 50% (避免 concentration)
- **签批流程:** 新 signal 上线必须老钱 + 小梁联签, 老韩 一票否决权 (风控视角)

---

## Part 6: 给 PM (小杜) / 需求分析师 (小颖) 的指令

### 给 小杜 (PM #36)

**任务:** 写 `docs/RESEARCH/xiaodu-prd-v2.md` (≤ 600 行)

**输入:**

1. 本文档 16 个 BC + 6 项 CPO 决策
2. GM `docs/MEETINGS/2026-05-28-gm-business-needs-must-haves.md` M-01..M-10
3. CLAUDE.md + AGENT.md

**输出: 4 个 user role 各自 user journey + use case**

- **Operator** (操盘手, 未来全职岗位, M1 由 GM 兼任) — 7×24h 监控 + drill-down + halt 决断
- **Analyst** (量化研究 + 风控研究) — 复盘 + 信号调试 + RM 阈值评估
- **GM** (老雷) — 每天 30 秒看公司 + 季度复盘 + 紧急 halt
- **未来 Trader** (M5+, 全职操盘手交接) — Operator 的超集, 加 capital allocation 权限

**禁忌 (与本文档一致):**

- 不准读 include/src/
- 不准读 Polymarket / Goalserve 官方文档
- 不准读 docs/RESEARCH/ 其他报告 (除本文档)
- 每个 use case 必须 "用户想做什么" 开头, 不是 "系统能做什么" 开头
- 任一 use case 与 BC 对不上, **以 use case 为准, 让工程改实现** (GM 原话)

### 给 小颖 (需求分析师 #25)

**任务:** 写 `docs/RESEARCH/xiaoying-acceptance-spec-v1.md` (≤ 500 行)

**输入:**

1. 本文档 16 个 BC + Stage 清单 (Part 4)
2. GM must-have M-01..M-10
3. OKR 起步季 KR + M4.5 7 hard gate

**输出: M1 / M4.5 / M5 各 stage 验收标准**

每个 acceptance criteria 必须:

- **Testable** — 能写 test case
- **Measurable** — 有数字阈值
- **Owner 明确** — 谁验收 (GM / 操盘手 / 第三方审计)

**示例格式:**

```
AC-M1-001 [BC-06 PnL 核算]
GIVEN paper engine 已运行 ≥ 1 个交易日
WHEN GM 登陆 operator console
THEN 主屏显示 "今日 paper PnL = $X" 大字号, 颜色随符号 (绿/红)
AND 加载延迟 ≤ 2s
AND 数据 freshness ≤ 5min
Owner: GM 老雷 + 操盘手
```

**禁忌 (与本文档一致):** 不准看代码 / 不准看 endpoint, 验收标准从需求出发不是从实现出发.

---

## Part 7: 风险 & 未解决问题 (CPO 视角)

- **R-1 第三方锚源依赖:** BC-01 严重依赖 Pinnacle, 限赔/关账/API 变更影响整链路. CPO 决: Sprint-2 调研 backup 锚源 (老彭/小段接), M4.5 前 ≥ 2 个独立锚
- **R-2 跨洋链路单点:** WSS 断 > 3s 影响决策. CPO 决: M1 接受单链路, M4.5 前 redundancy (主备机房/多 VPS)
- **R-3 sim2real gap:** paper 无法 100% 模拟真撮合. CPO 决: OQ-D12 单笔 $200 起, 第一周 sim2real gap 测量, 偏差 > 5% 退回 paper
- **R-4 ML 主决策门槛:** M5 后 alpha decay 可用 ML, 但主决策何时放? CPO 暂定 M+12 (实盘 12 个月后) 再评估
- **R-5 信号 IP / 复制风险:** P0-01 Pinnacle no-vig 公开易复制. CPO 决: 不防御, 靠执行速度 + 风控严格度差异化
- **R-6 监管不确定性:** 沿用 GM 2026-05-28 决议, M1-M5 不纠缠, 未来迁合规地区一次性处理

---

## Part 8: 完成汇报

**v1 业务能力清单 16 个 BC** (BC-01 ~ BC-16)
**3 个核心 use case** (GM 日看 + Operator 7×24 + 操盘手复盘)
**M1 / M4.5 / M5 stage 清单** (M1=12 个 BC, M4.5=14 个 BC, M5=16 个 BC)
**CPO 6 项产品方向决策** (must-not / must / 优先级 / halt 处理 / ML 节奏 / 信号扩展)
**禁忌严格遵守** — 未读 include/src/, 未读 Polymarket/Goalserve 官方, 未读 docs/RESEARCH/ 其他报告

**下一步:**

1. 老雷 GM 审阅本文档 + 反馈
2. 小杜 PM 接手写 prd-v2.md (4 user role × user journey)
3. 小颖 需求分析师 接手写 acceptance-spec-v1.md (M1/M4.5/M5 验收)
4. Wave 23 GM 整合 3 文档 + 与现有代码做 gap 分析

---

**最后更新:** 2026-05-28 by 老钱 (CPO)
**下次 review:** Wave 23 GM 整合后, 或 M1 节点 (2026-11-12) 回看
