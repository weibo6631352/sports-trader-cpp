# 6/01 主管周同步 input — 量化研究部 (C) W5 末 status

- **Owner:** 小梁 (E-018, financial-expert, C 单元 Manager)
- **Date:** 2026-06-01 (W5 末 → W6 启动前一天)
- **Meeting:** 6/01 主管周同步 (老雷主持, 5 主管 + 老钱)
- **Status:** input v1
- **关联:**
  - `docs/RESEARCH/xiaoliang-manager-mandate-v1.md` (我就职宣言)
  - `docs/INCIDENTS/gm-self-mistakes-log.md` 错 #9 (Pinnacle 误判)
  - `docs/RESEARCH/xiaoduan-goalserve-official-doc-v3.md` §7 + §9 (Goalserve 单源够 fair value 锚源)
  - `docs/ADR/2026-05-28-department-manager-mandate.md` (ADR-005)

---

## §1 C 单元 W5 末 status (5 ticket)

| Ticket | IC | 状态 | 备注 |
|---|---|---|---|
| W5-C-01 P0-01 spec v0.1→v1.0 + 小卢 1082 行 review | 小程 | **TBD W5 末** | 小程 spec 主体 ✓, 小卢 W4 末 25/25 测试已过, 边界 review 我 24h 内背书 |
| W5-C-02 VirtualMatcher 切 option A 灰度 | 小袁 + 小蒋 | **TBD W5 末** | 老郭建议待 ADR-007 锁后再切, 与小蒋协商 W6 头切 (Mode A 不上 B/C) |
| W5-C-03 Pinnacle CSV 选型 + 2 年历史回填 | 老彭 + 小余 | **撤回 (GM 错 #9)** | 改 Goalserve de-vig 算法建模 (见 §3), W5 末归零, W6 起重新派 |
| W5-C-04 PIT CI v0.1 主笔起步 | 小蒋 + 小宋 + 老练 | **TBD W5 末** | 小宋 integration framework v0.1 已含 R-20 PIT 测试 scaffolding, 小蒋 schema 起草中 |
| W5-C-05 microstructure C++ lib 收尾 | 小袁 + 小肖 | **✓ W4 末** | 小袁 v0.1 落 fill_rate model + 20 测试, W6 切灰度联跑 |

**W5 末 C 单元产出汇总:** 主要是 P0-01 review 收口 + W5-C-03 撤回归零 + de-vig 算法建模启动 (W6 才落代码). 5 ticket 中 1 已交 (C-05), 3 W5 末收口 (C-01/02/04), 1 撤回重派 (C-03).

**单元载荷红绿色:** 小蒋红 (PIT CI 主笔 + R-21 双线), 小袁黄, 小程黄, 老彭绿 (W5 主线撤回后短期低载, W6 接 de-vig 校准升档). HC-03 小吕 8/1 入职预期不变.

---

## §2 GM 错 #9 ack — Pinnacle 撤回 + Goalserve de-vig 接 (我作为 C 主管负责)

### 2.1 ack

GM 错 #9 我 ack. Sprint-1 retro 我决议 "Pinnacle 路径 A/C 二选一" 是基于小段 v2.1 信息 (W3 早期 Pinnacle 缺失 + UK Racing 没 Pinnacle 的纠结). 小段 v3 (W3 末 `xiaoduan-goalserve-official-doc-v3.md` §7) 四维扫描后已反转结论:

- `inplay.goalserve.com/inplay-<sport>.gz` 含 bet365 单源 value_eu (~1 秒推, 端到端 ~1.6s)
- `www.goalserve.com/getfeed/<key>/getodds/<sport>?cat=<cat>_10` 含 8-9 家 bookmaker (10Bet / WilliamHill / bet365 / Marathon / Unibet / BetVictor / 1xBet / Betano)
- 不再需要单独接 Pinnacle / Betfair / SBR feed

我跨 wave 没读到 v3, 一面之词背书过时信息. 永久 enforcement 我自己接: **W5 起 C 单元每周一 1:1 第一问 "本周各 IC 引用的 SSOT 是否最新版", 不再凭记忆背 vN**.

### 2.2 撤回原决议

我撤回 Sprint-1 retro 提的 "路径 A 必须 W6 启动 (donbest.com / sportsdata.io / SBR feed 三选)". **真正的 W6 open 决议是: Goalserve 8-9 家 bookmaker 怎么算 fair value (de-vig 算法选型)**, 归我建模.

### 2.3 ADR-008 候选 (改): Goalserve fair value de-vig 算法选型 — 我 owner

---

## §3 ADR-008 候选: de-vig 算法 4 选 1

### 3.1 算法对比表

| 算法 | 输入 | 输出 | 优 | 缺 | 我倾向 |
|---|---|---|---|---|---|
| **A 多 book 中位数** | 8-9 家 implied prob 取 median | fair_prob = median(p_i) | 简单稳健, 抗异常 | retail 普遍 5-12% margin, median 仍含 vig | ❌ |
| **B 多 book 加权均值** | 按 liquidity / 历史准度加权 | fair_prob = Σ w_i × p_i | 理论合理 | 校准需 2 年历史数据 + 准度回测, 6 月 paper 起步前用不上 | ❌ |
| **C Shin de-vig** | 8-9 家 (over_p, under_p) → 解 Shin 模型 | fair_prob = Shin(z) | 理论最优 (有 insider trading 假设) | ~150 行算法, 黑盒, M4.5 G7 老韩/老唐 audit 难, 单元测试覆盖率难 | ⏸ M4.5 后升级评估 |
| **D multiplicative de-vig** | 单家 normalize: p_yes' = p_yes / (p_yes + p_no); 跨家取均值 | fair_prob = avg(p_yes' across books) | retail 友好, ~30 行, audit 易, 单元测试 100% 可覆盖 | 假设 vig 在 yes/no 比例对称, 比 Shin 简化但够用 | **✓ 选 D** |

### 3.2 我决定 (主管数学背书): ADR-008 选 D (multiplicative de-vig)

**理由 (3 条):**

1. **paper 起步阶段简单优先 + audit 易**: M2 (8/6) 之前 paper 跑通是 hard gate, de-vig 算法不能成为 PnL attribution 的"黑盒源头". multiplicative ~30 行, 老韩 / 老唐 / 老郭三家 audit 5 分钟看懂, Shin 150 行 + 解析解 + 数值优化, audit 成本高且 M4.5 G7 风险.
2. **retail 数据特性**: Goalserve 8-9 家全是 retail bookmaker (bet365 / 10Bet / WilliamHill / Marathon / Unibet / BetVictor / 1xBet / Betano), 平均 margin 5-12%. multiplicative 假设 "vig 在 yes/no 对称分摊" 对 retail bookmaker 经验上成立 (老彭行业 v1 §3.2 印证). Shin 的 insider trading 假设对 retail 不显著.
3. **老韩 §5 表态一致**: 老韩 W5 RM v0.3 表态偏好 "multiplicative" (G7 audit 友好), 我数学背书与老韩 enforcement 路径一致, 不需要再单独协商.

### 3.3 D 算法公式 (一段话讲清)

对每家 bookmaker i (i = 1..N, N = 8-9), 读取 yes/no implied prob (p_yes_i, p_no_i) 满足 p_yes_i + p_no_i = 1 + vig_i (vig_i ≈ 0.05-0.12 retail). multiplicative 单家去 vig:

  p_yes_i' = p_yes_i / (p_yes_i + p_no_i)
  p_no_i'  = p_no_i  / (p_yes_i + p_no_i)

跨家融合:

  fair_p_yes = (1/N) × Σ p_yes_i'    (等权均值, 后续可按 @ts 衰减加权改进)
  fair_p_no  = 1 − fair_p_yes

输出 SignalDecision.fair_value = fair_p_yes, 进 P0-01 5¢ 阈值判断 (|fair − poly_mid| > 0.05 触发).

### 3.4 W6 落地分工

| Owner | 任务 | 截止 |
|---|---|---|
| 小程 (E-019) | de-vig 算法 spec v0.1 (含 D 公式 + 边界 + 数值稳定性测试) | W6 EOW |
| 老彭 (E-030) | 8-9 家 bookmaker 历史 vig 实证 (Goalserve `getodds` 1 周采样, 算 margin 分布) | W6 EOW |
| 小蒋 (E-020) | de-vig output 接 P0-01 v1.0 fair_value 锚 (signal pipeline) | Sprint-2 早 |
| 小卢 (IC pool) | C++ 落代码 (~30 行 + 5 测试, 含数值边界 p=0/p=1/NaN/N=1) | Sprint-2 早 |

**我 first review:** 24h ack 硬约束, 老韩 G7 audit + 老郭架构层 ack 后入 RM v0.3.

### 3.5 待 GM 拍板

我只表 "我倾向 D + 数学背书", **ADR-008 final ack 归 GM**. 协商不下 (例如老钱 CPO 视角希望 Shin 提早上) → 升老雷或开全体争议会 (CLAUDE.md §6).

---

## §4 跨主管 ASK 进度 (我 mandate §5 的 5 ASK 收口)

| # | 对象 | 内容 | 状态 |
|---|---|---|---|
| 5.1 | 老周 (A) | signal_iface schema W5 EOW 锁 (含 R-20 4ts) | **✓** 老李 + 小冯 v0.1 ABI 与 signal_iface 兼容, 老周 v0.6 review ack |
| 5.2 | 老韩 (B) | RM SIGNAL_CI_TOO_LOW + R-15 size *= 0.5 兜底 | **✓** RM v0.3.1 已 enforce, G2 bootstrap CI < 0.3 拦截 + R-15 size 兜底入 |
| 5.3 | 小余 (D) | Pinnacle CSV → ~~撤回~~ 改 Goalserve 4ts 标注 + de-vig 数据源接 | **✓ (已对齐)** 小段 v3 + 小冯 PM WSS 4ts + ETL-9~16 落地, 小余 W6 接 de-vig 历史采样 |
| 5.4 | 老胡 (E) | M2 8/6 OOS Sharpe gate 数据准备 + backtest 训练数据甘特 | **待老胡 input** W6 paper 起跑产生数据后 6/15 mid-sprint 同步 |
| 5.5 | 老郭 (F) | ADR-004 paper R-2 binary 共享 final ack | **✓** R-2 红线已 enforce, ADR-004 v0.1 通过 |

**5/5 ASK 4 ✓ + 1 待**, ASK 跨单元落地 80%.

---

## §5 待 GM 拍板 (我 6/01 周同步上呈)

| # | 议题 | 我倾向 | 拍板人 | 紧急度 |
|---|---|---|---|---|
| 1 | **ADR-008 候选 (改): Goalserve multiplicative de-vig** | 选 D (multiplicative, ~30 行) | 老雷 (老韩 G7 audit + 老郭架构 ack 后) | **W6 启动前 (6/01)**, 不拍 W6 派单卡住 |
| 2 | HC-03 小吕 8/1 提前入职 ack | 我 W4 已 ack, 等小林 JD 起草 6/15 deadline | 老雷 + 小林 联决 | 中 (8/1 倒推 JD 起草 6/15) |
| 3 | 小蒋 9/10 红榜需 GM 关注 | 我 1:1 W5 启动 30min/周, HC-03 8/1 入职后 P0-02 分担 | 老雷 (HR pulse 观察) | 中 (PIT CI Sprint-2 末 deadline) |

---

## §6 W6 启动决议 (C 单元 W6 派单 5 ticket)

W6 启动假设 (GM 6/01 ack ADR-008 选 D):

| Ticket | 标题 | Owner (C) | Co-Owner | 截止 | 约束 |
|---|---|---|---|---|---|
| **W6-C-01** | ADR-008 multiplicative de-vig 落代码 + 算法 spec | 小程 (spec) + 老彭 (历史 vig 实证) + 小卢 (代码) | 老韩 (G7 audit) | W6 EOW | ~30 行 / 5 边界测试 / R-19 (拍脑袋禁) |
| **W6-C-02** | P0-01 v1.0 接 ADR-008 fair value 锚 (signal pipeline) | 小程 + 小蒋 | 老周 (signal_iface) | Sprint-2 早 | A-04 (5¢) / R-20 (4ts) |
| **W6-C-03** | PIT CI v0.1 落代码 (schema + framework adapter + future-leak 测试) | 小蒋 + 小宋 + 老练 | — | Sprint-2 末 6/26 | D-16 / R-18 (future-leak row=0) |
| **W6-C-04** | VirtualMatcher 切 Mode A 灰度 (5 触发条件接 microstructure) | 小袁 + 小蒋 | 老郭 (ADR-007 时机) | W6 mid | R-7 / R-14 (Bernoulli 0.50-0.65) / R-15 |
| **W6-C-05** | P0-02 信号 spec 起草 (M2 8/6 倒推, HC-03 小吕 8/1 入职后接 mentor) | 小程 (mentor) → 小吕 (8/1 后) | — | 8/15 (HC-03 入职后) | A-05 (3% taker fee net edge > 0) / M4.5 ≤ 3 信号 CPO |

**W6 EOW 关键里程碑:** ADR-008 落代码 + P0-01 接 fair_value 锚 + PIT CI scaffolding. 这三件成则 paper engine 7/30 M4.5 G6 倒推有底.

---

## §7 主管 KPI 自评 update (基于 mandate §6 6 条职责)

| 职责 | W4 自评 | W5 自评 | 证据 / 改进 |
|---|---|---|---|
| 1. 拆任务 → 派 IC | A- | **5/5** | W5 5 ticket 我自己拆派, GM 没拆 |
| 2. 排队 + 优先级 | A- | **4/5** | 小蒋红色降到黄色 W6 EOW 目标, 老彭 W5 撤回 → W6 升档 |
| 3. review + 质量门禁 | B+ | **4/5** | 小程 P0-01 spec review pending W5 末; W3 慢点已改进 (24h ack 硬约束) |
| 4. 跨单元接口 | A- | **5/5** | 5 ASK 中 4 已 ack (5.4 待老胡, 非我延), §5.1/5.2/5.3/5.5 全 ✓ |
| 5. KPI + 1:1 + 培养 | B | **4/5** | 5 IC 周一 1:1 已启动 (小蒋 30min 红色加码), 老彭 W5 跳过 → W6 接 de-vig 必跑 |
| 6. 不亲力亲为 | A | **5/5** | W5 C 单元 cpp 代码我 = 0 行, 守住主管不写代码红线 |

**W5 整体: A-** (W4 持平 A-). W6 起若 ADR-008 落代码 + paper 起跑顺利 → 目标 A.

**风险监控 (我 mandate §9.2 R-Liang-1~3):**

- R-Liang-1 (主管装睡): W5 我 5 ticket 全派出去, 没装睡, ✓
- R-Liang-2 (IC 等我审 spec): 小程 P0-01 review 我 24h 内必给, W5 末收口, 监控中
- R-Liang-3 (小蒋红色持续): HC-03 8/1 倒推依赖 HR 小林 JD 6/15 起草, 监控中. 若 HC-03 不能 8/1 入职 → W6 早升老雷, 备选 "M2 gate 顺延 7 天" 或 "PIT CI 主笔切小宋"

---

## §8 不耻下问 (跨单元求助 backlog)

- **@小段** (E-037, D 单元): de-vig 历史 vig 实证, 我请小段配合老彭 W6 起 7 天 Goalserve `getodds` 采样 (8-9 家 bookmaker × 1 周 × NBA/NFL/MLB/Tennis/Soccer/EPL 6 sport), 用于 D 算法的 vig 分布校准
- **@老郭** (F 顾问团): ADR-008 选 D 后老郭架构层 final ack, 是否需要在 signal layer 抽象 "de-vig provider" 接口便于未来 Shin 升级 (M4.5 后)
- **@老雷** (GM): ADR-008 6/01 ack, 不拍 W6-C-01 卡住; HC-03 小吕 JD 6/15 deadline 同步进度
- **@小林** (HR): HC-03 评委我 + 老韩待派进度, JD 起草是否 on track

---

## §9 完成汇报

**C 单元 W5 末 status + GM 错 #9 ack + ADR-008 撤 Pinnacle 改 Goalserve multiplicative de-vig (我倾向 D) + W6 5 ticket 派单待 GM 6/01 ADR-008 ack 后启动.**

主管 KPI W5 自评 A- (与 W4 持平), W6 ADR-008 落代码 + paper 起跑后再评.

---

**Last updated:** 2026-06-01 by 小梁 (E-018, 量化研究部 Manager)
**Next review:** Sprint-2 mid (6/15) 与 HC-03 小吕 JD 起草 deadline 同步
