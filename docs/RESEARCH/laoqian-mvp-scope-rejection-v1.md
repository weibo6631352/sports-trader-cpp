# MVP Scope 拒绝清单 v1

- Owner: 老钱 (cpo-product-strategy)
- Last review: 2026-05-28
- 验收人: 老雷 (GM)
- 关联决议: Kick-off D-01 (MVP 锁 Moneyline, M4 前不扩盘), D-08 (sprint 制)
- 关联 OKR: KR-C-1 / KR-C-2 / KR-C-3 / KR-C-4 (T+24 周 MVP 实盘)
- 状态: 锁定. 任何挑战走 Section 5 升级路径.

---

## 1. MVP 定义 (一句话)

**T+24 周 (2026-11-12), 在 Polymarket 上, 用 Moneyline 单盘口, 在指定 3 个体育联赛的 pregame + inplay 窗口内, 由 1 套定向交易策略 (taker-only), 经 RiskManager 门禁, 完成首笔实盘成交, 并在此后 72 小时保持系统在线零崩溃、零风控失效.**

这一句话每个限定词都是拒绝项的种子, 删一个都不行.

---

## 2. MVP 做什么 (精简白名单)

凡未在此清单内的, 默认 ❌ 不做.

| 维度 | 范围 |
|---|---|
| 盘口 | **Moneyline only** (二元胜负市场, 含和棒球/篮球的"无平局"标准款) |
| 体育 | **NBA + NFL + MLB**, 三选三 (理由见 Section 3.2) |
| 平台 | **Polymarket only** (gamma/clob/data REST + WSS) |
| 数据源 | **Polymarket + Goalserve** 两条, 无第三方 |
| 时段 | **pregame T-60min → 开赛**; **inplay 关键节点窗** (具体由小梁在 M2 前定义, 但不全程跟盘) |
| 策略 | **1 套定向交易信号** (taker), 信号源自小梁假设清单 ≥ 1 / ≤ 1 |
| 订单类型 | **Limit IOC + Market**, 仅 taker; **不做 maker / 不挂单/不做市** |
| 资金规模 | **首笔 $50–$500 试单**, Kelly 上限由老韩 + 小梁定 |
| 自动化等级 | **Level-2 半自动** (信号自动生成 + 自动下单, 但每日开/收盘人工 arm/disarm; 风控触发后人工复位) |
| 风控 | RiskManager 全规则在线, 100% 下单门禁 (D-02) |
| 监控 | Prometheus + Grafana 基础看板 + PagerDuty 等价告警 |
| 部署 | 跨洋单点 (老吴 v0.1 方案), 无 HA 双活 |
| 报表 | 日终 PnL + 成交清单 1 份, 邮件/markdown, 无 Web 前端 |
| 用户 | **内部 4 人核心运维** (老雷/老周/老韩/老钱), 零外部用户 |

---

## 3. MVP 不做什么 (按 10 维度拒绝)

### 3.1 盘口范围 ❌

**只做 Moneyline. 其余全部不做.**

| 盘口家族 | MVP 状态 | 拒绝理由 |
|---|---|---|
| Moneyline (胜负二元) | ✅ 做 | 流动性最厚 / 定价最干净 / 二元市场风控边界清晰 |
| Totals (大小分 O/U) | ❌ 不做 | 需独立 totals 模型, 关联 pace/防守因子, 扩 ≥ 1 个量化人月 |
| Spreads (让分) | ❌ 不做 | half-point 风险 + push 处理逻辑复杂, ROI 不优于 ML |
| 分节 (1Q/2Q/H1/H2) | ❌ 不做 | 流动性薄 + 信号噪声大, 高方差不利首笔验证 |
| 分盘 (set/inning) | ❌ 不做 | 同上, 且 inplay 信号节奏不匹配 MVP 自动化等级 |
| Series (系列赛) | ❌ 不做 | 时间跨度 > 单场, 与 RiskManager 时间窗模型冲突 |
| Outright (冠军/晋级/MVP) | ❌ 不做 | 长尾市场, 持仓周期月级, 与 72h 验收窗不匹配 |
| Player Props | ❌ 不做 | 高 vig + 数据源(球员级 box score)依赖未验证 |
| Game Props (首先得分/准确比分) | ❌ 不做 | 流动性差 + 长尾, 与北极星无关 |
| Alt lines (替代盘) | ❌ 不做 | Polymarket 上覆盖稀疏, 不值得引入 |
| Parlays (串关) | ❌ 永不做 | Polymarket 体系不支持原生串关, 自合成无 EV 优势 |
| Live cash-out / hedge UI | ❌ 不做 | 半自动等级无 UI 需求 |

**仲裁锚:** Kick-off D-01 已锁, M4 前不开口子.

### 3.2 体育范围 ❌

**只做 NBA + NFL + MLB. 共 3 个.**

| 联赛 | MVP 状态 | 理由 |
|---|---|---|
| NBA | ✅ | 全季在 MVP 窗口内 (10/22 - 次年 6 月); Polymarket 流动性最深; Goalserve 覆盖成熟 |
| NFL | ✅ | MVP 窗口正值赛季高峰 (9-1 月); 单场盘量大 sharp money 集中; 用于 sharpness benchmark |
| MLB | ✅ | 季后赛 10-11 月覆盖 MVP 验收窗; 高频出场补足样本量 (KR-C-4 要 ≥ 500 场样本) |
| NHL | ❌ | 流动性次于上述三, 信号节奏与 NBA 重叠不增益 |
| Soccer (EPL/UCL/MLS) | ❌ | 三向市场 (含平局) 与 Moneyline 二元假设冲突; Draw 处理另起炉灶 |
| Tennis | ❌ | 单挑 5 盘高方差 + 退赛 (retirement) 风控特例多 |
| MMA/Boxing | ❌ | 单场离散事件, 样本量不足以撑回测 |
| College Football/NCAAB | ❌ | 信息不对称大 + sharp money 占比低 + 数据源质量差 |
| F1/Golf/eSports/Cricket/其他 | ❌ | 全部不做, 不在战略路径上 |
| Politics/Crypto/Culture (Polymarket 非体育市场) | ❌ | 公司定位"体育量化", 越线即偏航 |

**仲裁锚:** OKR-D KR-D-1 已限定 Goalserve 字段 mapping 范围. 量化范围 @小梁 已确认 (kickoff 议程 5).

### 3.3 策略范围 ❌

**1 套信号. 上限 1. 不是 ≥1, 是 =1.**

| 维度 | MVP | 拒绝 |
|---|---|---|
| 信号数量 | 1 套 (小梁假设清单 ≥10 中筛 1) | ❌ 多信号合成 / ensemble |
| 信号类型 | 定向交易 (directional, fair-value vs market) | ❌ 做市 / arb / cross-venue / 套保 |
| 角色 | Taker only | ❌ Maker (报价/挂单/补流动性) |
| 持仓周期 | 单场 (< 4h inplay + 60min pregame) | ❌ 跨日持仓 / 系列赛持仓 |
| 模型类型 | 解析+经验校准 (含 elo/power rating + 公开赔率融合) | ❌ ML/DL/transformer/RL/在线学习 |
| 特征工程 | ≤ 20 维 (小梁手工特征) | ❌ 自动特征发现 / 自动特征仓 |
| 凯利 | 分数 Kelly (≤ 0.25), 单笔上限硬编码 | ❌ 动态 Kelly / 多头协同凯利 |
| 对冲 | 无 | ❌ DEX/CEX 跨市场对冲 |
| 信号迭代 | M2 锁版本, M4 前不改信号公式 | ❌ MVP 期间在线调参 |

**仲裁锚:** D-04 (回测+实盘共用特征管道) 已隐含禁双套. 技术可行性 @老周 已确认 1 信号热路径 < 500us 可达 (KR-A-3).

### 3.4 功能范围 ❌

| 功能 | MVP | 拒绝理由 |
|---|---|---|
| 核心引擎 (signal→risk→order) | ✅ | 北极星 |
| RiskManager v1 + 审计日志 | ✅ | D-02 一票否决 |
| Prometheus + Grafana 看板 | ✅ | KR-E + 老吴/小郑 |
| 日终 PnL 报表 (markdown/邮件) | ✅ | 验收需要 |
| **Web 前端 dashboard** | ❌ | 4 人内部, CLI + Grafana 够用. 前端是 T+M6 课题 |
| **移动端 App** | ❌ | 永不做 (无外部用户) |
| **回测 replay UI** | ❌ | KR-C-5 是 framework, 不是 UI; 命令行跑足够 |
| **特征仓库 (feature store)** | ❌ | 1 信号不需要; 多信号才上 |
| **A/B 实验平台** | ❌ | 1 套策略无 A/B |
| **ML 训练流水线** | ❌ | 无 ML 模型 |
| **自动报税/结算** | ❌ | 合规红线另外处理 (老黄) |
| **多账户 / 多钱包** | ❌ | 单钱包单账户 |
| **跨市场套利 (Polymarket vs DraftKings/Pinnacle)** | ❌ | 平台范围限 Polymarket; 跨市场看 Section 6 |
| **冷启动新闻情绪/NLP 信号** | ❌ | NLP 在 T+M5 后再说 |
| **配置热更新 UI** | ❌ | 配置改完重启, MVP 不要花架子 |
| **多语言/i18n** | ❌ | 内部工具 |
| **告警自愈/auto-remediation** | ❌ | PagerDuty 通知人, 人复位 |
| **chaos engineering** | ❌ | 不在 MVP 验收范围 |

### 3.5 平台范围 ❌

**Polymarket only.**

| 平台 | MVP | 备注 |
|---|---|---|
| Polymarket (gamma/clob/data) | ✅ | 唯一 |
| Drift / GammaSwap / Hedgehog / Azuro | ❌ | 链上同行不做 |
| DraftKings / FanDuel / Pinnacle / Bet365 | ❌ | 中心化博彩, 不在战略 |
| Kalshi / PredictIt | ❌ | 不同合规域 |
| Binance/OKX/CEX 衍生品 | ❌ | 跨产品不做 |
| 其他 prediction market | ❌ | 全拒 |

**仲裁锚:** 公司使命已锚定 Polymarket. 跨平台是 T+36 月愿景, 不在 MVP.

### 3.6 用户范围 ❌

| 用户 | MVP |
|---|---|
| 老雷 / 老周 / 老韩 / 老钱 (核心 4) | ✅ 运维 + 审阅 |
| 内部全员 (≤ 15) | ⚠️ 只读 Grafana, 不下单 |
| **外部 LP / 投资人** | ❌ MVP 期间不接受外部资金 |
| **外部 API 消费者** | ❌ 无 public API |
| **公开 SaaS 用户** | ❌ 永不做 (B2C 不在战略) |
| **白标客户** | ❌ |

**仲裁锚:** 公司是自营 (proprietary trading), 不是产品公司. 这条永不松.

### 3.7 时间窗范围 ❌

**Pregame (T-60min → 开赛) + Inplay 关键节点窗.**

| 窗口 | MVP | 备注 |
|---|---|---|
| Pregame T-60min → tipoff | ✅ | 流动性集中 + sharp money 入场 |
| Inplay 关键节点 (Q1 末 / 半场 / 大比分跳变后 5min) | ✅ | 由小梁在 M2 前定义具体触发条件 |
| **Inplay 全程跟盘 (持续 fair-value 更新)** | ❌ | 跨洋延迟 + 半自动等级承担不起全程; 全程是 maker 题 |
| **赛前 T-24h 之前的长尾市场** | ❌ | 流动性差 + 长持仓与 RiskManager 时间窗模型冲突 |
| **赛后结算窗 (settlement arb)** | ❌ | EV 太薄, 不值得 |
| **NBA Summer League / preseason** | ❌ | 样本质量低, 不算入 KR-C-4 |

**仲裁锚:** 跨洋链路延迟由老吴 + 老姜实测. 技术可行性 @老周 已确认 inplay 关键节点窗 (非全程) 在延迟预算内可达 (KR-A-3 + KR-A-4).

### 3.8 自动化范围 ❌

**Level-2 半自动. 不是 L3, 不是 L4.**

| 等级 | 定义 | MVP |
|---|---|---|
| L0 全人工 | 人下单 | ❌ 没意义 |
| L1 信号辅助 | 系统出信号, 人点确认 | ❌ 跨洋人工延迟太高 |
| **L2 半自动** | **信号自动 + 下单自动, 人工 arm/disarm + 风控复位** | ✅ MVP |
| L3 全自动 | 7x24 全自动含异常自愈 | ❌ MVP 不做, M5 后再说 |
| L4 自适应 | 在线学习/策略切换 | ❌ 永不在 MVP |

**人工干预点 (明文列):**
1. 每日比赛日开盘前 ≥ 30min 人工 arm (检查数据源/钱包/合规)
2. 每日比赛日收盘 disarm
3. 任意风控规则触发 → 系统自动停, 必须人工复位 (D-06 已锁 30s 数据缺失自动暂停)
4. 钱包余额 / nonce / gas 异常 → 人工介入
5. 任何 PnL 偏离日内预算 ±20% → 人工 review

**仲裁锚:** D-06 已锁数据异常 30s 自动暂停硬编码. 自动化等级 = L2 是产品决议, 不接受 PR 提级.

### 3.9 暂缓 backlog (见 Section 4) ❌

详见 Section 4.

### 3.10 其他常见 scope creep ❌

| 项 | MVP | 拒绝理由 |
|---|---|---|
| 自研 EVM 节点 | ❌ | 用 Polygon RPC (老叶 S1-009) |
| 自研 HSM | ❌ | KMS 即可 (老孙 S1-005) |
| 自研监控系统 | ❌ | Prometheus 现成 |
| 自研日志系统 | ❌ | stdlib + 文件 + grep 够用 |
| 自研 backtester UI | ❌ | CLI |
| 重写部分 boost/grpc | ❌ | 用现成 |
| C++23 实验特性 | ❌ | C++20 稳态 (老周决定) |
| 多线程 lock-free 极致优化超出延迟预算的部分 | ❌ | 老姜延迟预算之外不投入 (S1-011) |
| 自研钱包 SDK | ❌ | 现成 web3 库 + EIP-712 实现 |
| 自研 OMS / EMS | ❌ | RiskManager + 直连 CLOB 够 |
| 多语言 binding (Python/Rust) | ❌ | 纯 C++ MVP |

---

## 4. 暂缓 backlog (T+M5 后再评估)

按"何时可能解锁"分组. **每一项进入 backlog 不代表承诺会做**, 只表示 MVP 之后可重新仲裁.

### 4.1 M5 → M6 (验收后 6 周内可评估)
- B-01 Totals 盘口 (Moneyline 稳态后第一个扩盘候选)
- B-02 Inplay 全程跟盘 (前提: 跨洋链路稳定 + 算力余量)
- B-03 信号 v2 (第 2 套信号, 小梁假设清单中第 2 候选)
- B-04 资金规模升至 $5k 单笔上限

### 4.2 M6 → M9 (3 个月内)
- B-05 NHL 接入 (第 4 个体育)
- B-06 Maker (做市) 策略原型
- B-07 多账户 / 多钱包
- B-08 简易 Web dashboard (内部)
- B-09 历史数据回灌 ≥ 5 年

### 4.3 M9 → M12 (T+12 月以内)
- B-10 Spreads 盘口
- B-11 Soccer (含 Draw 处理) 接入
- B-12 ML 信号原型 (gradient boosting 起步, 不上 DL)
- B-13 跨平台对冲探索 (Polymarket vs Pinnacle, 信息层, 不下单)
- B-14 HA 双活 / 跨区域部署

### 4.4 永不做 (Hard NO, 写死)
- N-01 Parlays / 串关合成
- N-02 移动端 App
- N-03 公开 SaaS / B2C 产品
- N-04 接受外部 LP 资金 (除非战略级 pivot)
- N-05 NFT / meme / 非体育市场
- N-06 高频做市 (HFT maker, 跨洋链路下结构性吃亏)

**注:** 4.4 的"永不"意味着触发它必须 GM + CPO 双签的战略转向, 不走 ADR 流程.

---

## 5. Scope creep 判定 + 升级路径

### 5.1 判定标准 (满足任一即 scope creep)

1. 新增 Section 2 白名单之外的**盘口家族 / 体育 / 平台 / 用户类型**
2. 增加信号数量 (从 1 → 2)
3. 自动化等级提级 (L2 → L3)
4. 引入新数据源 (除 Polymarket + Goalserve)
5. 引入新依赖框架 / 新语言 / 新基础设施 (架构 v1.0 之外)
6. 让 MVP 验收时间从 T+24 周推后
7. 让任何 OKR-A/B/C 的 KR 验收口径松动
8. 任何"我顺便做一下 X"的 PR (顺便即 creep)

### 5.2 升级路径 (谁拍)

| Creep 级别 | 判定人 | 仲裁人 | 流程 |
|---|---|---|---|
| 微 (单 PR 内 < 100 LOC 偏离, 不动 scope) | code reviewer (老郭/老周) | 老郭一票否决 | PR comment |
| 小 (单 sprint 内功能扩张) | 老胡 (PM) | **老钱** | sprint planning 提案, 老钱 24h 内拍 |
| 中 (跨 sprint 或动了 Section 2 白名单) | 老胡 + 老周 | **老钱 + 老雷 (双签)** | 走 ADR, 老郭 + 老韩 同时评审 |
| 大 (动了 MVP 定义 / OKR KR / 北极星) | 老雷 | **老雷 (GM 终裁)** | All-hands 决议, 写入 ADR |
| 战略级 (动了 "永不做" / 公司定位) | 老雷 + 老钱 | 全体合伙人 | 临时全体会 |

### 5.3 拒绝模板 (任何人可直接引用)

> "此请求触发 MVP scope 拒绝清单 v1 Section 3.X / 4.X. 引用 D-01 (Kick-off). 走 Section 5.2 升级路径; 在仲裁人 (老钱 / 老雷) 书面同意前, PR 不予合入 / 任务不进 sprint."

### 5.4 双周复盘 (Anti-creep retrospective)

- 每个 sprint retro 老胡 + 老钱 共同审 1 个问题: "本 sprint 有几个 scope creep 苗头被挡住, 几个漏进?"
- 漏进的进 INCIDENTS, 复盘 owner 是放进来的人.

---

## 6. 与战略目标的关系

### 6.1 与 T+18 月目标 (Sharpe ≥ 1.3, 年化 PnL ≥ $1M) 的关系

- MVP Sharpe > 1.0 (KR-C-4) 是 T+18 月 Sharpe ≥ 1.3 的**起点**, 不是终点.
- MVP 拒绝清单的目的是**先证明系统能正期望**, 而不是"做大做全".
- T+M5 → T+18 月的扩张走 Section 4 backlog, 每一项再单独经 ADR.

### 6.2 与 T+36 月愿景 (Polymarket 体育第一量化做市 + 定向交易系统) 的关系

- MVP 是**定向交易 (taker)**, 做市 (maker) 在 backlog B-06, 是 T+24-36 月主题.
- 全盘口覆盖是 T+24-36 月主题, MVP 不沾.
- MVP 的"窄"是 T+36 月"宽"的前提条件: 没有 24 周内的零崩溃零风控失效, 后续扩盘只是把脆弱性放大.

### 6.3 与价值观的关系

| 价值观 | 本清单的体现 |
|---|---|
| 实盘优先 | MVP 定义直接以"实盘首笔" 为锚, 不以"功能完整" 为锚 |
| 纪律高于收益 | 拒绝清单本身就是纪律; Section 5 升级路径就是纪律工具 |
| 数字说话 | Section 2 每条都有量化边界 (盘口数 / 体育数 / 信号数 / 资金额 / 自动化等级) |
| 不耻下问 | 本清单技术可行性 @老周, 量化范围 @小梁 已分别确认; 后续争议 24h 内必复盘 |

---

## 附录 A: 与 Sprint-1 ticket 的对齐

| Sprint-1 Ticket | 对齐性 | 备注 |
|---|---|---|
| S1-001 架构 v0.1 | ✅ 限定在 MVP 范围内 | 老周不为 backlog 4.x 留架构后门 (评审纪律) |
| S1-002 CLOB API 实测 | ✅ 仅 Moneyline 路径 | |
| S1-003 Goalserve 实测 | ✅ 仅 NBA/NFL/MLB 字段 | 字段超出范围的 mapping 暂不投入 |
| S1-004 RiskManager 设计 | ✅ 单盘口规则集 | |
| S1-007 小梁市场结构 | ✅ 限定 Moneyline + 3 联赛 | 报告超出范围部分入 backlog |
| S1-012 老彭行业分析 | ✅ sharp money 限定 ML | |
| S1-017 小程信号假设 ≥ 10 | ⚠️ **候选** ≥ 10, **MVP 实施** = 1 | 选 1 留 9 候选入 backlog |
| S1-023 dogfood 剧本 | ✅ NBA/NFL/MLB only | 与本清单一致 |

## 附录 B: 修订记录

| 版本 | 日期 | 修订 | 人 |
|---|---|---|---|
| v1.0 | 2026-05-28 | 初版, 锁定 MVP 范围 | 老钱 |

## 附录 C: 签收

- 老雷 (GM, 验收人): ⬜ pending
- 老周 (Architect, 技术可行性): ⬜ pending
- 老韩 (Risk, 风控边界): ⬜ pending
- 小梁 (Quant, 量化范围): ⬜ pending
- 老胡 (PM, 进度对齐): ⬜ pending
- 老郭 (Architect Review, 一票否决人): ⬜ pending
