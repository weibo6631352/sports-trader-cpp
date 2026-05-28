# 量化研究部主管就职宣言 v1

- **Owner:** 小梁 (E-018, financial-expert) — C 单元 Manager
- **Date:** 2026-05-28
- **Status:** v1 RFC, W4 EOW (2026-06-06) deadline
- **关联:**
  - `docs/ADR/2026-05-28-department-manager-mandate.md` (ADR-005, 5 主管 6 条职责)
  - `docs/MEETINGS/2026-05-28-gm-business-needs-must-haves.md` (GM M-01~M-10)
  - `docs/MEETINGS/2026-05-28-sprint1-retro-all-hands.md` §3 A-01~A-16 共识 + §5 D-01~D-18 决议
  - `docs/RESEARCH/xiaoliang-market-structure-v1.md` (我自己 12 信号机制) / `xiaocheng-signal-catalog-v1.md` (小程 catalog) / `xiaojiang-paper-engine-skeleton-v1.md` (小蒋 paper skeleton) / `xiaoyuan-microstructure-v1.md` (小袁 264 双边样本) / `laopeng-betting-industry-analysis-v1.md` (老彭行业)
  - `docs/HIRING/employee-registry.md` (E-019 ~ E-021, E-030, E-051 待入职)
- **触发:** GM 错 #8 (越级派单), ADR-005 立 5 主管制. 本宣言交付 5 主管 + 1 协调人之一.

---

## §1 单元成员清单 (5 人, 工号)

| 工号 | persona | name (file) | 单元 | 职位 | 当前状态 |
|---|---|---|---|---|---|
| **E-018** | **小梁 (我)** | financial-expert (#18) | **C** | **Manager** | Active (本宣言) |
| E-019 | 小程 | quant-signal-research (#19) | C | Senior IC | Active (信号 catalog + spec) |
| E-020 | 小蒋 | quant-backtest (#20) | C | Senior IC | Active (paper engine + 回测) |
| E-021 | 小袁 | quant-microstructure (#21) | C | Senior IC | Active (微观结构 + fill_rate) |
| E-030 | 老彭 | betting-industry-expert (#30) | C | IC (Advisor 性质) | Active (行业 prior + Pinnacle/Goalserve odds) |
| **E-051** | **小吕 (待入)** | quant-engineer (#51) | C | IC | **Pending → 8/1 (HC-03, HR 提前自 Q3)** |

**说明:**
- 4 IC + 我 = 5 人, 加 HC-03 小吕 8/1 入职后 = 6 人
- 老彭 IC 角色是"行业顾问 + 数据源", 不写代码 (写 CSV 解析交叉单元给小卢/小段)
- 我 ack HC-03 提前到 8/1 (HR 小林提案), 详见 §8

---

## §2 我作为主管要做的 vs 不做的

### 2.1 do (8 项)

1. **接 GM 业务目标 → 拆任务 → 派 IC.** GM 给 "M2 OOS Sharpe ≥ 0.8 含 fee gate" / "P0-01 落地 5¢ 阈值", 我自己拆成 spec + 回测 + 实测 + signal C++ stub 派 4 个 IC, 不让 GM 拆.
2. **审 IC 产出 first review.** signal spec / paper engine 设计 / 微观结构数字 / Pinnacle CSV schema 我先看 24h 内给 ack 或 retry, 通过后再过老郭 (架构) / 老韩 (RM) / 老钱 (CPO).
3. **守"金融数学 + 阈值"主权.** Kelly 0.25 / fill_rate_floor 0.50 / PER_ORDER $5K/$2K / P0-01 5¢ 阈值 / M4.5 G1-G7 等数学结论, IC 提报由我背书后入 RM v0.3.
4. **跨单元接口协商.** signal_iface schema (老周 A), RM SIGNAL_CI_TOO_LOW (老韩 B), Pinnacle CSV / Goalserve odds schema (小余 D), backtest 数据准备 (老胡 E), paper R-2 binary 共享 (老郭 F) — 24h ack, 48h 不下升老雷.
5. **单元 IC weekly 1:1 + 月度 KPI.** 4 IC + 老彭 每周 30min 1:1 (我主问 blocker + 数字进展), 月末交 KPI 矩阵给小林联签.
6. **守"统计稳健性 + 因果"红线.** 任何 IC 上线信号必须过 PIT (R-18, 主笔小蒋) + bootstrap CI > 0.3 (G2) + 含 fee net edge > 0 (G3-G4). 拍脑袋数字 (R-19) 我不签.
7. **培养 + 答疑 + 跨单元学习.** "不耻下问"我自己先做, 4 IC 跨 A/B/D 找答案我帮搭桥 (例 小蒋 ↔ 老王 WAL, 小袁 ↔ 老李 WSS, 小程 ↔ 老周 signal_iface).
8. **轮值 GM 助理 (8 月).** ADR-005 §4.3, 第 1 轮 6/老周 → 7/老韩 → **8/我** → 9/小余 → 10/老胡. 8 月我 dial-in 老雷, 学 GM 视角.

### 2.2 don't (5 项)

1. **不写代码** (signal C++ / paper engine C++ / 回测 binary). 小程 + 小蒋 + 小卢 IC pool 写, 我评审.
2. **不替老钱拍 P0/P1 上线顺序** (CPO 决策权). 我提报数学背书, 老钱 + 老雷拍.
3. **不替老韩拍 RM 红线阈值最终落点** (老韩 enforce). 我给数学 + CI, 老韩入 RM v0.x.
4. **不替老周拍架构 / 接口物理拆分** (老周架构主权). 我只提"我要什么", 怎么拆 layer 是老周的活.
5. **不动态切上线 / 不私签 ADR.** signal 上线 / 阈值改 / fraction 变, 必走"小梁数学 + 老韩 RM + 老钱 CPO" 联签, 我单方不能签.

---

## §3 IC 工作量评估 (0-10)

### 3.1 当前载荷 (HR pulse + 我观察)

| IC | 工号 | 当前主任务 (W3-W4) | 载荷 0-10 | HR 颜色 | 关键风险 |
|---|---|---|---|---|---|
| 小程 | E-019 | 信号 catalog v1 (12 信号 spec) + P0-01 spec v0.1 (拒接 C++ stub, 边界正确) | **7/10** | 黄 | spec v0.1 → v1.0 还要 1 轮 (W5), 与小蒋回测对齐 |
| 小蒋 | E-020 | paper engine main v0.1 **1233 行 + 27 测试** (W4-03 已交) + PIT CI 主笔 (D-16) + R-21 backtest 上线压力 | **9/10** | **红** | M2 gate 7/30 + PIT CI Sprint-2 末双 deadline, 我 W5 给他降载 |
| 小袁 | E-021 | microstructure C++ lib v0.1 (Wave 20 在飞) + 5 因子 + 8 sport profile + 20 测试 (264 双边样本基础) | 8/10 | 黄 | C++ lib + fill_rate model 双线, W5 VirtualMatcher 切 option A 灰度联动 |
| 老彭 | E-030 | 行业分析 v1 (398 行已交) + Pinnacle CSV 数据源选型 (W5+ 接入) | **5/10** | 绿 | 当前低载, W5 起接 Pinnacle CSV 主线 |
| 小吕 (E-051) | E-051 | **未入职 (8/1 计划)** | — | — | HC-03, HR 提案提前 |

### 3.2 4 IC + 1 待入工作量具体拆 (本宣言核心)

#### 3.2.1 小程 (E-019) — W3-W4 ✓ + W5 微调

**W3-W4 已交:**
- 信号 catalog v1 12 信号 (P0×2 + P1×4 + P2×6), 表 + ID + hit rate + edge/trade + 容量
- P0-01 pregame 6h novig revert spec v0.1 (5 因子触发 + Kelly fractional + slippage gate)
- **拒接 P0-01 C++ stub** (Wave 19, GM 错 #4 sub-agent 自纠首次), 边界正确

**W5 派单:**
- P0-01 spec v0.1 → v1.0 (5¢ 阈值 vendor health metric 接老李 v3 endpoint matrix)
- 5 触发条件边界 review (小卢 Wave 19 已落 1082 行 + 25 测试) — 小程 review 我背书
- 配合小蒋 PIT CI scaffolding 给 P0-01 训练 join 写 future-leak 测试用例 (R-18)

**预计 W5 载荷:** 6/10 (降一档, 让出带宽给老彭 Pinnacle CSV 配合)

#### 3.2.2 小蒋 (E-020) — W4 ✓ + W5-W6 降载

**W4 已交:**
- paper engine main v0.1 **1233 行 + 27 测试** (PaperSigner + VirtualMatcher Mode A++ Bernoulli + R-7 双调 abort + stcpp_paper E2E)
- W4-07 老孙 Polygon RPC mock 三 stub 内联覆盖 (合并入 W4-03)
- **R-21 backtest 上线压力闸 2 闭环** (M4.5 G6 ≥50 笔倒推: W6 paper skeleton 并行 / placeholder microstructure / W7 末早期联调切片)

**W5 派单:**
- VirtualMatcher 切 option A 灰度 (5 触发条件接 microstructure C++ lib): **W5 小蒋 + 小袁 联跑** (3-5 d, 不上 option B/C)
- PIT CI v0.1 主笔起步 (D-16, Sprint-2 末 deadline) — W5 schema + framework adapter, W6 join 测试

**W5 KPI 关怀 (HR 红色):**
- 我 W5 周一 1:1 必跑 (上周已跑), 跟他确认 PIT CI scaffolding 老练 + 小宋 framework 已就位 (D-16 列双 owner)
- 如 PIT CI 占用 > 60% 周时间, 申请 HC-03 小吕 8/1 入职后 P0-02 信号 spec 起草交小吕, 给小蒋让出 M2 OOS 回测 + R-21 主线
- 红色 → 黄色目标: W6 EOW

**预计 W5 载荷:** 8/10 (略降, W6 EOW 目标 7/10)

#### 3.2.3 小袁 (E-021) — Wave 20 在飞 + W5 联跑 VirtualMatcher

**Wave 20 在飞:**
- microstructure C++ lib v0.1 (5 因子: spread / L1_ask / depth_±2tick / cross_vig / microprice)
- fill_rate model v0.1 (Mode A++ Bernoulli 0.50-0.65, 配 R-14)
- 8 sport profile (NBA / NFL / MLB / Tennis / Soccer / EPL / ChampionsLeague / SerieA 各自 spread_med / L1_ask_med / depth)
- 20 测试 (含 hot/cold 分桶 INPLAY_HOT_CRIT 200/800ms = R-15 兜底)

**W5 派单:**
- VirtualMatcher option A 灰度联跑 (与小蒋, W5 EOW): microstructure factor → fill_rate sampler 接 PaperSigner
- 5 触发条件边界 review 协助小程 (小卢 P0-01 1082 行 use microstructure factor 三个: spread / depth / microprice)
- W5 末提报 hot token 数据源延迟 (Goalserve push p95 7s = C-06 折中, 是否升 INPLAY_HOT_CRIT 用 PM-only)

**预计 W5 载荷:** 8/10 (持平, microstructure C++ lib 收尾 + VirtualMatcher 联跑双线)

#### 3.2.4 老彭 (E-030) — W5+ Pinnacle CSV 主线接入

**W3-W4 已交:**
- 行业分析 v1 398 行 (Pinnacle / Bet365 / 亚菠 / Asian / power rating / key number 全覆盖)
- 6 信号 (S1 novig revert / S2 lineup news lag / S3 nfl key number / S4 lead taker / S5 favorite overpay / S6 series lead overprice) 整合入小程 catalog
- 5/10 低载 (绿色)

**W5+ 派单 (主线):**
- Pinnacle CSV 数据源选型 (老彭 + 小余) — 选型已基本明确 (donbest.com / sportsdata.io / SBR feed 三选), W5 末决议
- Pinnacle no-vig fair line 历史数据回填 (2 年, NBA + NFL + MLB + Tennis + Soccer + EPL) — 与小余 D-04 历史回填合并
- W6+ no-vig 算法 (Shin / Power method / Multiplicative) 选型给小蒋回测 (P0-01 ground truth)

**预计 W5 载荷:** 7/10 (升一档, 主线接活)

#### 3.2.5 HC-03 小吕 (E-051) — 8/1 入职 (我 ack 提前)

- **HR 提案 (小林):** 原 Q3 入职, 提前到 8/1, 理由 M4.5 倒推 W6 P0-01 回测必出, 单元 4 IC 已饱和, HC-03 不提前 = 8 月 W6-W7 paper 联调 + 回测压力全压小蒋小程 (双红)
- **我 ack:** 同意提前 8/1
- **入职后 W7 派单 (8/1-8/15):**
  - P0-02 信号 spec 起草 (D-03, M2 8/6 gate, 含 3% taker fee net edge > 0) — M4.5 ≤ 3 信号 CPO 决议, P0-02 是第 2 个
  - PIT CI 给小蒋让出主笔 (小吕分担 join schema + future-leak 测试)
- **评委需 ack (HR registry §110):** 小梁 + 老韩 (待 HR 派两位)

---

## §4 W5 派单 backlog (我自己拆, 不让 GM 拆)

### 4.1 W5 涉及 C 单元任务 (5 件)

| Ticket | 标题 | Owner (C) | Co-Owner | 截止 | 红线 / 约束 |
|---|---|---|---|---|---|
| **W5-C-01** | P0-01 spec v0.1 → v1.0 + 5 触发条件边界 review | 小程 | 小卢 (代码) | W5 EOW | A-04 (5¢) / R-19 (拍脑袋禁) |
| **W5-C-02** | VirtualMatcher 切 option A 灰度联跑 | 小袁 + 小蒋 | 老周 (signal_iface) | W5 EOW | A-15 (binary 共享) / R-7 (mode 双调 abort) / R-14 (Bernoulli 0.50-0.65) |
| **W5-C-03** | Pinnacle CSV 数据源选型 + 历史回填启动 | 老彭 + 小余 (D) | 小蒋 (P0-01 ground truth) | W5 EOW | R-20 (4 时间戳) / D-04 (2 年回填) |
| **W5-C-04** | PIT CI v0.1 主笔起步 (schema + framework adapter) | 小蒋 | 小宋 (framework) / 老练 (CI) | Sprint-2 末 (6/26) | D-16 / R-18 (future-leak row=0) |
| **W5-C-05** | microstructure C++ lib v0.1 收尾 + fill_rate model | 小袁 | 小肖 (slippage) | W5 EOW | R-14 / R-15 (size *= 0.5 兜底) |

### 4.2 HC-03 小吕入职后 8/1 backlog (预派, 我 ack)

| Ticket | 标题 | Owner (C) | 截止 | 约束 |
|---|---|---|---|---|
| **HC-03-W7-01** | P0-02 信号 spec 起草 (score-price-mismatch, M2 8/6 gate) | 小吕 (新) + 小程 mentor | 8/15 | A-05 (M2 OOS Sharpe ≥ 0.8 含 3% taker fee gate) / M4.5 ≤ 3 信号 CPO 决议 |
| **HC-03-W7-02** | PIT CI v0.2 协作 (join schema + future-leak 测试) | 小吕 + 小蒋 | 8/22 | R-18 / D-16 |

### 4.3 5 触发条件边界 (小卢 W4 已落, 小程 review)

小卢 (E-035-XX, IC pool A 单元借调) W4 Wave 19 已落 **P0-01 1082 行 + 25 测试** (5 触发条件 + Kelly fractional 公式 + slippage gate). W5 小程 review (我背书).

---

## §5 跨单元接口需求

### 5.1 与老周 (A 系统工程部) — signal_iface schema 锁

- **我要的:** signal_iface.h 接口 (Signal 输出 SignalDecision { side, price, size, conf_interval, signal_id, trace_id, signal_age_ns, *4 时间戳 R-20 }) 锁死 W5 EOW, P0-01 + P0-02 + 未来 12 信号都按此 schema
- **要老周做的:** Wave 20 老周架构 v0.6 已派 (本 wave 派出尚未回汇), 我需要 v0.6 spec 里 §signal layer 部分 review ack W5 mid
- **24h ack 期望:** 是

### 5.2 与老韩 (B 风控合规部) — RM SIGNAL_CI_TOO_LOW 接 small_signal output

- **我要的:** RM v0.3 evaluate() 接 SIGNAL_CI_TOO_LOW (G2 bootstrap CI 下界 < 0.3 → reject), 我提报数学 (小董 v1.1), 老韩入 RM v0.3
- **要老韩做的:** RM v0.3 W5 接 SIGNAL_CI_TOO_LOW + R-15 size *= 0.5 兜底 (quote_age > 100ms 且 hot)
- **24h ack 期望:** 是

### 5.3 与小余 (D 数据基础设施部) — Pinnacle CSV / Goalserve odds schema

- **我要的:** Pinnacle CSV schema (W5 老彭 + 小余 选型) + Goalserve odds 0 字节兜底 (D-04 / ETL-1~ETL-8)
- **要小余做的:** Pinnacle CSV 历史回填 (2 年, 与 D-04 合并) + 4 时间戳 R-20 (pinnacle_ts / ingestion_ts / as_of_ts) 全字段标注
- **48h ack 期望:** 是 (W5 EOW 选型决议)

### 5.4 与老胡 (E 产品业务保障部) — backtest 回测 W6 数据准备

- **我要的:** W6 paper engine 联调 + 回测训练数据 (Pinnacle 2 年 + Polymarket 历史 fills + Goalserve 历史 push) 就位, M4.5 G1-G7 7/30 backtest v2 准备
- **要老胡做的:** Sprint-2 M2 backlog 排期 confirm (D-03 8/6 OOS Sharpe ≥ 0.8 gate), 跨单元数据准备甘特对齐
- **24h ack 期望:** 是

### 5.5 与老郭 (F 顾问团协调人) — paper R-2 红线 + backtest binary 共享

- **我要的:** A-15 (paper engine R-11 共享生产 binary, paper 走完整 RM + 独立 paper_audit WAL + 同 config hash) 老郭架构层 final ack
- **要老郭做的:** Wave 20 老郭 ADR-004 (本 wave 派出尚未回汇), 我需要 ADR-004 里 paper binary 共享部分 W5 review ack
- **24h ack 期望:** 是

---

## §6 主管 KPI 自评 (基于 ADR-005 §2.2 6 条职责)

| 职责 | 自评 | 证据 / 说明 |
|---|---|---|
| 1. 接 GM 目标拆任务 → 派 IC | **A-** | W3-W4: 我把 12 信号 + P0-01 spec + microstructure 已分拆派 4 IC, GM 没拆. W5 §4 backlog 5 件本宣言我自己定 |
| 2. 单元内排队 + 优先级 | A- | 4 IC 当前主任务清晰, 红线只小蒋一个 (HC-03 提前缓解), 老彭 W5 升档主线接活 |
| 3. review + 质量门禁 | **B+** | W4 我审了小程 spec v0.1 拒接 C++ stub (背书正确), 但 W3 小袁 264 样本 + P0-01 5¢ 修正我跑了 24h 才 ack (慢), 改进点 |
| 4. 跨单元接口对接 | A- | §5 5 接口主管/owner 全清 24h ack 期望, W5 必须落 5/5 |
| 5. KPI + 1:1 + 培养 | B | 4 IC 1:1 我跑了 3/4 (老彭跳过, 因载荷绿), W5 起 5/5 全跑 (含老彭 Pinnacle CSV 接入前 1:1) |
| 6. 不亲力亲为 | **A** | 我 W3-W4 没写代码, 只出 spec / 数字 / 评审, ADR-005 §2.3 红线守住. 例外: 0 次 (没碰过 hotfix < 2h) |

**整体自评 A-** (1+2+4+6 强项, 3+5 待补).

---

## §7 W5 GM 派单约定

### 7.1 GM 错 #8 教训永久 enforcement (ADR-005 §3.3 5 题自检)

GM 派 wave 前 5 题自检, 任一 yes 拒派:

1. 一面之词背书?
2. 单 agent 替全员说话?
3. 让 agent 看老项目 / 撤销方案?
4. 派单 prompt 越 persona "拒绝任务" 边界?
5. **是否越主管直接派 IC?**

### 7.2 C 单元 W5 GM 派单期望

- **GM 不指定"谁做什么"**, 只给业务目标 (例: "W5 落 P0-01 v1.0 + paper engine option A 灰度 + Pinnacle CSV 选型") + 截止 + 约束
- **GM 直派我** (主管本人, ADR-005 §3.2 例外), 我 24h ack, 48h 内拆出 IC 任务 (§4 backlog 模板)
- **GM 紧急 P0 例外** (RM HALTED / 安全事件 / 用户原话 < 2h 响应) 可越主管, 我事后补 audit
- **GM 跨多单元统筹例外** (老胡 PM 周报 / 老郭 架构评审 / 老高 PR review) 不算越级

### 7.3 我作为主管的 24h ack 硬约束

- GM 派单 24h 内 ack (受/拒/澄清)
- 48h 内拆出 IC 任务 + IC 24h 内反馈可行性
- 超时升老雷 (R-42 IC 等主管兜底)

---

## §8 HR 扩招建议

### 8.1 HC-03 小吕 quant-engineer 提前 8/1 (我正式 ack)

- **HR 小林提案理由:**
  - 评估我 8/10 黄 (单元红绿 1:3:1)
  - M4.5 倒推 W6 P0-01 回测必出, 单元 4 IC 已饱和 (小蒋红 / 小袁黄 / 小程黄 / 老彭绿)
  - 不提前 = 8 月 W6-W7 paper 联调 + 回测压力全压小蒋小程 (双红)
  - 原 Q3 入职 → 提前到 8/1 (提前约 1 月)
- **我正式 ack:** **同意**
- **我的额外条件:**
  1. JD 强调"P0-02 spec 起草 + PIT CI 协作", **不是** P0-01 复制粘贴 (P0-01 已小程 spec + 小卢代码 + 小蒋回测三方接力)
  2. 入职后 W7 第一 sprint 派 P0-02 spec, 小程 mentor (mentor 关系 8/1-9/1 一个月)
  3. 9/1 后小吕独立, 小程回 P0-03 / P1 信号 spec 起草
- **评委 ack 待 HR 派 (我 + 老韩):** 我现 ack 担任评委, 老韩待 HR 派 (HR §110 registry §51 列两位评委待派)

### 8.2 是否还要 P2 quant-engineer-2 (M4.5 后)?

- **答: 暂不要.**
- **理由:**
  - HC-03 小吕入职后 5 IC + 我 = 6 人, 单元规模相当 A 单元 (老周 +14 IC + 10 IC pool 借调) 的 1/3, 但 C 不需要 A 那么大 (我们出数学 + spec, 写代码是 A + IC pool)
  - M4.5 7 hard gate 闯过后 (Q4 末) 真正瓶颈是"信号容量 + 多 sport 扩展", 那时再看是否要 quant-engineer-2 (Q1 2027 评估)
  - 当前不申请扩 HC, 避免 GM 错 #6 (HR registry 制度缺失补救后) 再叠"扩招冲动"
- **触发条件 (未来评估 HC-04):**
  - M4.5 7 gate 全过, 单元 5 IC 持续 > 8/10 载荷 4 周
  - P1 信号 (4 条) 全部上线但 PnL attribution 仍未拆清 (alpha decay 检测瓶颈)
  - 多 sport 扩展 (NBA + NFL + MLB → + Tennis / Soccer / EPL) 单元 IC 全饱和
- **HR 同步:** 本宣言入 registry, HC-04 暂不进 pending 池, 评估窗 2026 Q4 末 (M4.5 后)

---

## §9 不变量 + 风险

### 9.1 不变量

- 我作为主管不写代码 (例外 §2.1 第 6 "守红线" 不算写代码; 架构原型 / 紧急 hotfix < 2h 暂未触发)
- 我不替老钱拍 P0/P1 上线顺序 (CPO 主权), 不替老韩拍 RM 阈值落点 (RM 主权), 不替老周拍接口物理拆分 (架构主权)
- 4 IC + 老彭 + 小吕 (8/1 后) 任务必经我 first review, 我超时升老雷

### 9.2 风险

- **R-Liang-1 (主管装睡):** 我接 GM 派单后实际默默自己做, 没拆给 IC. → ADR-005 R-41 兜底, 老胡周报追踪派单覆盖率
- **R-Liang-2 (IC 等我):** 我审 spec 慢 (R-2 §6 KPI 自评 B+ 已识别), W5 起 24h ack 硬约束
- **R-Liang-3 (小蒋红色持续):** HC-03 8/1 不入职 / M2 gate 7/30 不过 / PIT CI Sprint-2 末双 deadline 重叠. → 8/1 HC-03 入职是 hard 缓解, 若 HC-03 8/1 不能入职 → 我升老雷 § "M2 gate 顺延 7 天" 或 "PIT CI 主笔切小宋" 二选

---

## §10 落地动作 (W4 EOW 6/6 deadline)

- [x] 本宣言 v1 (300 行内 ≤ 300, 当前 ≤ 320 含 frontmatter, 满足 ADR-005 §5)
- [ ] 4 IC 周一 1:1 + 老彭 周一 1:1 (新增, W5 起 5/5 全跑)
- [ ] W5 §4 5 ticket 派单 (GM ack 后 24h 内 IC 反馈)
- [ ] HC-03 评委 (我 + 老韩) HR 派完, JD 起草 6/15 deadline
- [ ] 跨单元 §5 5 接口 ack 全收 (W5 EOW)
- [ ] 老胡 KPI 矩阵 + HR 小林 联签 (月末)

---

**最后更新:** 2026-05-28 by 小梁 (E-018, 量化研究部 Manager)
**Next review:** Sprint-2 末 (6/26) 与小蒋 PIT CI 主笔节点同步
