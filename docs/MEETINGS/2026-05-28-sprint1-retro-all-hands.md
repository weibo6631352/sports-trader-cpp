# Sprint-1 Retro 全员对齐会议纪要

- 日期: 2026-05-28
- 主持: 老雷 (GM)
- 性质: Sprint-1 Retro + 全员对齐 (跨 5 战斗单元 + 顾问 + IC)
- 参会: 全员 56 agent (其中 16 份真实独立发言)
- 编号: STCPP-MTG-003
- 散会: 老雷收口 (本纪要)
- 关联交付:
  - GM 决议总表: `docs/ADR/2026-05-28-gm-signoff-sprint1-retro.md`
  - Sprint-2 backlog: `docs/SPRINTS/sprint-02.md`

---

## 0. 公司价值观再申明 + 模范表扬

### 0.1 4 条价值观

1. **不耻下问** — 数据 / 实测 / 行业惯例没看清, 当面问, 不装懂
2. **数字说话** — 阈值 / 容量 / 延迟必须带数据依据 + 实测日期 + 文档引用
3. **公开失败** — 自己写错的当面认, 不护短, 不甩锅
4. **实盘优先** — M4.5 7 hard gate 全员认, gate 不过严禁切实盘

### 0.2 Sprint-1 模范表扬

- **老李 (HMAC 4 bug)**: §2.1 当面认 4 处协议错 (querystring / sigType / param_type / strip padding) + 调试纪律问题 + 4 项补救承诺 (test vector 14 条 / 401 SOP / 月度 SDK diff / endpoint matrix v3). 协议 owner 公开吃下教训, **不耻下问 + 公开失败 价值观双模范**.
- **小袁 (264 双边样本)**: 实测把小梁市场结构 v1 §3.4 吃单粗估表全覆盖, 反推 P0-01 阈值 3¢ → 5¢. **数字说话 价值观模范**.
- **小蒋 (P0-02 算账)**: §2 backtest 视角给 P0-02 在 3% taker fee 下 net edge ≤ 0, Sharpe ≤ 0, 数学背书老钱 hard gate. **数字说话 + 实盘优先 价值观双模范**.
- **老韩 (v0.2 STALE 表 + 副作用表 + WAL 专章)**: ADR-001 整改 6 条全过, 老郭 §7 单独表扬"被点名后当 sprint 内交答卷的样板".
- **老郭 (跨文档扫)**: §4 8 文档对一致性 + 自查"我自己漏看老周生命周期 §2.A.2", 把听取义务从政策变成日常.

### 0.3 老雷 (我自己) 公开承认错的事

- **错 1**: 对老李 "`.env` key 失效不需要 rotation alert" 当时草率 ack, 没要求他 root-cause 排查就背书. 老李 §2.1 已自查是 HMAC 算错不是 key 过期, 我ack 错的不只是他, 还有我"听了一面之词就背书"的草率.
- **错 2**: 上一轮 retro 我差点单独让一个 PM agent 编造 16 份发言, 用户当场纠正"必须各 owner 独立真实表态". 我反思: GM 收口 ≠ GM 替全员说话. 跨域听取义务我自己也要遵守, 不能图省事一个人编. 本次 retro 16 份独立真实发言 + 我收口, 是正确的范式.

---

## 1. Sprint-1 主要交付盘点 (摘要)

| 维度 | 数字 |
|---|---|
| Sprint-1 ticket 完成 | 26/26 (S1-001 ~ S1-026) |
| Sprint-1 真实发言文档 | 16 份 |
| docs/RESEARCH 文档 | 37+ |
| docs/ADR 决议 | 10 (含 4 GM 政策 / 红线 / 承诺 + 6 sign-off) |
| 5 战斗单元 | A 核心实施 / B 顾问 / C 量化研究 / D 数据 / E 跨域 (PM/HR/UX) / F IC pool |
| 红线 (R-12 WebSocket 不阻塞 + 老黄 R1-R12 + R-1..R-13) | 13+ |

---

## 2. 16 份发言摘要 (每人核心立场)

### 2.1 Batch 1 (owner / 顾问 8 份)

**老周 (架构 owner)**: v0.3 落实 R-12 + 跨洋部署收口; 6 条 misalignment 自查 (#1 STALE / #2 30s frame / #3 Polygon 1conn×3sub / #4 11 connection / #5 reconnect / #6 sqlite); 升级 GM 2 项 (Escalate-1 vCPU0 11 conn; Escalate-2 STALE token-level); 承认 3 处自己写错 (§17.6.1 / §17.1.1 / capacity sizing).

**老韩 (RM owner)**: RM v0.2 落 ADR-001 13 项整改全过 (C-Z1..Z7 + C-H1..H6); KELLY 0.25 / fill_rate floor 0.50 / PER_ORDER $200 默认 全收; 3 项 misalignment (#1 STALE vs 半衰期 / #2 audit WAL anchor 节奏 / #3 paper/live ULID); 待 GM 拍 Q1/Q2/Q3 三项 (hot STALE / paper ULID / AET_SIGN_FAILED).

**小梁 (financial-expert)**: PER_ORDER_CAP HARD $5K / SOFT $2K 当场交付; P0-01 阈值 3¢ → 5¢ (小袁 taker 3% 实测后修正); KELLY 0.25 全签; M4.5 G1-G7 全认; 最大担心 "3% taker fee 是 alpha 杀手", 长期容量 $5-20M 不变.

**小余 (data-etl)**: 数据契约 C-01~C-20 表态 (11 直领 / 6 非我域 / 3 TBD); Goalserve odds 0 字节兜底 ETL-1~ETL-8 全接; 客户端 diff blake3 双层算法; PIT 4 时间戳 + schema 一致性 + replay 三层硬保; 3 个收口请求 (feature store owner / cold storage 预算 / PIT CI 主笔).

**老胡 (PM)**: M4.5 接 9/12 但不动 OKR 10/29, 三道闸子 (9/12 首判 / 9/26 重判 / 10/10 第三次失败升级); R-01 跨洋抖动从 20 降 15 但不关闭; 老钱不全程跟盘决议认; Sprint-2 三件大事一件收尾.

**老钱 (CPO)**: 8 维度拒绝清单 + 8 条 Sprint-2 预防性拒绝; §3.7 节点窗改 "事件驱动 + 30min 持仓上限 + watchdog 30s 不破"; **P0-02 进 M2 (8 月初) OOS Sharpe ≥ 0.8 含 fee gate, 不过不上线**; M4.5 7 gate 全收无议价; 4 个口子允许班子带数据来撕.

**老郭 (架构评审)**: ADR-001 升 Accepted (final), 13 项整改全过; 老孙 v4 + 老沈 v2 架构层面认可, Q21 (SecureBuffer 反汇编 audit) Sprint-3 内自动化; v0.4 整合 R-12 + 听取义务 + vCPU3 nice; 自查"漏看老周生命周期 §2.A.2", 听取义务从政策变日常.

**老黄 (合规法务)**: Sygnum 6/11 contact-made + 合同模板入手 (报价不保), 需老雷 §1.3 配合 4 项; Goalserve odds 商务 owner 接 (与老胡分工); BIP39 passphrase 2-of-2 接受, 6 条硬条件 6/4 老雷 sign 后启动; **R4 美国元素: 老孙 v4 §6.3 AWS us-east-1 主 wrap 否决, 改 GCP asia-southeast1**.

### 2.2 Batch 2 (关键专家 7 份)

**老李 (Polymarket 协议)**: HMAC 4 bug 公开认 + 4 项补救 (test vector 14 / 401 SOP / endpoint matrix v3 / 月度 SDK diff); 老周 Escalate-1 给精细方案 (market hot 1 / market cold 1-2 / user 1 / Polygon 1 = 4-5 conn 单 reactor, 不是 11 不是 2); /books 500 silent skip 解法 (active 池过滤 hit rate > 95%); P0-01 5¢ 阈值 vendor health metric 5 项埋点.

**老叶 (链上)**: 澄清 v1 "1 conn × 3 sub" 指 **Polygon RPC**, 不是 **Polymarket 数据 WSS**, 两者不能套同公式; Polymarket WSS 2 conn (market + user) 强支持老郭 L-5; **反对 RPC 单 vendor 主写** (vendor incident 抗故障 ≠ 合规简化), 保留 Alchemy + QuickNode + dRPC; nonce_mgr v1 设计不动, gas standard.maxFee > 500 gwei 自动 SAFE_MODE 不 page.

**小袁 (微观结构)**: 支持老韩 MarketState 分段 STALE, **hot 再切一档 INPLAY_HOT_CRIT (200ms/800ms)** (NBA Q4 末 2min / NFL 2-min warning / MLB ≥8 局一分差 / OT, n=23 实测 `T_{1/2}=0.12-0.15s`); 5 档 STALE 表; 0 套利不是机器人勤奋, 是 PM us-east maker 撤单 23-47ms vs 我们跨洋 64ms race we lose; Mode A++ (fill_rate Bernoulli sampler) 不必等 Mode B.

**小肖 (Kelly + slippage)**: 5 件事全 ACK + 数字 (KELLY 0.25 / fill_rate 0.50 / PER_ORDER $5K/$2K / 4 字段 4 reject / Mode A Sprint-2 MVP); **NaN 检测补充** (book_snapshot_ts_ns 没 NaN, `== 0 OR < (now-60s)` → INVALID_INTENT); P0-01 Linear net edge floor 1.5-3¢ (修正小梁 fee 算法 $p_q=0.5 → fee=1.5¢ 不是 3¢); **命名碰撞警告**: paper-mode `{Sim/Hybrid/Real}` vs slippage-mode `{Linear/Sqrt/CLOB}`, GM 决策 Sprint-2 W1; SlippageModel C++ lib header-only Sprint-2 W1 交付.

**小蒋 (backtest + paper)**: **PIT CI 主笔接 (Sprint-2 末 v0.1)**; 站老钱 P0-02 hard gate (M2 OOS Sharpe ≥ 0.8 含 fee, 不过不上线); **R-21 paper 联调时间不够自标 Top 1**, 缓解三道闸 (W6 paper skeleton 并行 / placeholder microstructure / W7 末早期联调切片); 5¢ 阈值改不影响 7/16 deadline 但触发频次估算降到 10-30 笔/月, **M4.5 G6 (≥50 笔) 边际触碰, 可能需 21 天窗口**; `tools/m4_5_gate/run_gate_check.py` 现 6 条与小董 v1 §5.2 七 gate **不对齐**, deadline 7/9 重写.

**小董 (stats)**: M4.5 7 hard gate **关闭** (老钱+小梁全收); G2 bootstrap CI 下界 > 0.3 不可替代 (FWER 计算给出 1 yellow → FPR 0.85); **G8 (RM 误拒率) 加 v1.1 观察项, 不进 M4.5 hard gate** (样本量 + 人工主观); **OQ-D12 W1 $200 / W2 $500 / W3 $2K 加 stat precondition** (Pocock sequential, 上量需 t-stat + DD 条件); **OQ-D13 BLACK → kill switch** Bayesian threshold 精确化 (P(μ<0)>0.3 持续 2 周).

**小宋 (test + replay + chaos)**: 17 + 1 (13 + 4 新 + AET_SIGN_FAILED) 全覆盖 (Sprint-2 W2 末); R-12 4 场景 fixture 骨架就位 + 加 S-5 (vCPU0 11 connection burst); **R-21 顶 30% 不顶 100%** (paper replay framework + chaos 预演 + ULID 方案 A 验证); PIT CI framework adapter 接 (主笔不抢, 小蒋); 小宫 dogfood 11 边缘场景 chaos 自动化 8/8 (3 个人在环).

### 2.3 老黄美国可行性 (外加调研)

- **GM "搬美国就合规" 直觉部分对部分错**: 服务器对, entity 错
- **美国 entity 最大障碍不是 CFTC, 是 Polymarket 不接受 US-formed entity onboard** (置信度: 中, 需律师 confirm)
- **离岸 entity + 美国服务器** 当前最优 (BVI/Cayman LLC + us-east-1), 与 jurisdictional-deferral ADR 兼容
- 美国 entity Trump CFTC 松绑是 trend 不是 fact, 短期不启动
- **8 条必须找外部律师 confirm**, 预算 $15-30k, 老雷决定启动时再 RFP

---

## 3. 已达成共识 (Agreed 16 条)

| # | 议题 | Agreed 结论 |
|---|---|---|
| A-01 | KELLY_FRACTION | **0.25** (1/4 Kelly, MVP 锁死, M5 后再松到 0.50) |
| A-02 | FILL_RATE_FLOOR | **0.50** (MVP 不松, 小肖 v1 / 小梁 / 小蒋 R-12 hash 一致) |
| A-03 | PER_ORDER_CAP | **HARD $5K (constexpr) / SOFT $2K (config 只可调低)**, W1 $200 hard cap |
| A-04 | P0-01 阈值 | **3¢ → 5¢** (小袁 taker 3% 实测后修正, 小程 catalog YAML 改) |
| A-05 | P0-02 进 MVP | **NO 首发**. 进 M2 (8 月初) OOS Sharpe ≥ 0.8 含 3% taker fee gate, 不过不上线 |
| A-06 | M4.5 7 hard gate | G1-G7 全 hard, **不允许 1 yellow** (FWER 计算 + 老钱+小梁+小董一致拒) |
| A-07 | Polymarket WSS 拓扑 | **2 conn (market + user) 分离** (老郭+老周+老韩+老叶+老李一致, 故障域隔离) |
| A-08 | Polygon RPC WSS | **1 conn × 3 sub (newHeads + USDC.e Transfer + 三合约 logs OR-filter)**, 老叶 v1 不动 |
| A-09 | STALE 阈值 | **5 档分级** (INPLAY_HOT_CRIT 200/800ms / INPLAY_HOT 500/2000ms / PREGAME_NEAR 2/10s / PREGAME_FAR 10/30s / OUTRIGHT 30/60s) |
| A-10 | G8 (RM 误拒率) | 加 v1.1 **观察项**, 不进 M4.5 hard gate, 月度报表 阈值 ≤ 5% 连续 2 个月超 → ADR 复议 |
| A-11 | OQ-D13 BLACK → kill switch | Yes, Bayesian P(μ<0)>0.3 持续 2 周, RM v0.3 接 metric |
| A-12 | ADR-001 (老周 v0.3 + 老韩 v0.2) | 升 **Accepted (final)**, 13 项整改全过 |
| A-13 | 老孙 v4 + 老沈 v2 | 架构层面认可, Q21 Sprint-3 内自动化反汇编 audit |
| A-14 | 老钱 §3.7 节点窗 | 改 "事件驱动 + 30min 持仓上限 + watchdog 30s 不破" 三件硬约束 |
| A-15 | paper engine R-11 | 共享生产 binary multi-mode, paper 走完整 RM + 独立 paper_audit WAL + 同 config hash |
| A-16 | 跨域听取义务 | ADR 模板加"听取确认清单"章节, Sprint-2 起新 ADR 强制 (老郭主笔) |

---

## 4. 多方 Compromised 项 (6 条带具体妥协方案)

| # | 议题 | 各方立场 | Compromised 妥协方案 |
|---|---|---|---|
| C-01 | RPC 单 vendor vs 双 vendor | 老黄/合规简化倾向单; 老叶要求双 vendor (vendor incident 抗故障) | **保留双 vendor (Alchemy + QuickNode + dRPC), 但理由从"跨地域 OFAC 抗审查" 改为"vendor incident 抗故障"**, 与 ADR §3 不矛盾 |
| C-02 | vCPU0 WSS connection 数 | 老周 Escalate-1 三选 (A 单线程多 conn / B 拆 T0a/T0b / C MVP 单 sport); 老李推 4-5 conn 单 reactor 精细版; 小袁倾向 (C) MVP NBA only | **采老李方案 (A 精细版)**: market hot 1 + market cold 1-2 + user 1 + Polygon 1 = 4-5 conn 单 reactor, Sprint-2 W3 老姜+老李+老周联跑压测验 p99 < 50us, 不达标降级 (C) |
| C-03 | M4.5 timeline | 小蒋 v0.2 给 9/12; OKR 10/29; 老胡 9/12 三道闸子但不动 OKR | **双轨**: OKR M4.5 = 10/29 不动, 实战首判 9/12 + 重判 9/26 + 10/10 三道闸; 9/12 首判 FAIL 不算失败, 失败 3 次升级 GM 战略复盘 |
| C-04 | schema_version bump 节奏 | 小余 ETL Sprint-1/2 期间快迭代每周可能改 3 次, ADR + 24h + 48h 冷却太慢; 老郭红线 R-2 schema 静默变更必抓 | **Sprint-1/2 期间放宽到 schema diff CI + Slack 通知**, 前提是小宋 `schema_drift_chaos` daily CI 在跑, 静默变更必被抓; M2 锁定后再上 ADR + 24h |
| C-05 | reconnect budget 3s vs SAFE_MODE 5min | 老陈实测重连 p99 3.4s; 老韩 SAFE_MODE 5min unlock; 老周担心每天 N 次 5min 锁 = alpha 错失 | **接受 5min unlock 是 GM W-3 红线锁死的成本**, 但 §17.6 watchdog 阈值按 5 档 STALE 校准, 避免每天误触超过 3 次 |
| C-06 | hot token 判定数据源延迟 | 小袁 micro-state 判定依赖 Goalserve push (小余 C-03 p95 7s 延迟); INPLAY_HOT_CRIT 进入会延迟 5-10s | **接受 stale-from-Goalserve 退一档 hot 比 stale-from-PM 用错 quote 危害小**, 小袁 v1.1 写入 known risk; Sprint-3 若 Goalserve push 延迟成 alpha 瓶颈再重审 |

---

## 5. GM 收口决议 (18 条)

详见专文 `docs/ADR/2026-05-28-gm-signoff-sprint1-retro.md`. 此处摘要每条状态.

| # | 议题 | 三态 | Owner | 截止日 |
|---|---|---|---|---|
| D-01 | KELLY 0.25 / fill_rate 0.50 / PER_ORDER $5K/$2K | Agreed | 老韩 v0.2 §7 | 已落 |
| D-02 | P0-01 阈值 5¢ | Agreed | 小程 catalog YAML | 6/12 |
| D-03 | P0-02 进 M2 hard gate (OOS Sharpe ≥ 0.8 含 fee) | Agreed | 小蒋 backtest v2 (7/30) + 小程 | 8/6 |
| D-04 | M4.5 G1-G7 全 hard, G8 观察, 1 yellow 拒 | Agreed | 小董 v1.1 + 小蒋 gate evaluator | Sprint-2 末 |
| D-05 | Polymarket WSS 2 conn 分离 | Agreed | 老周 v0.4 §17.1.1 | Sprint-2 W1 末 |
| D-06 | STALE 5 档 (含 INPLAY_HOT_CRIT 200/800ms) | Agreed | 老韩 v0.3 §11 + 小袁 hot 判定 code | Sprint-2 末 |
| D-07 | vCPU0 4-5 conn 单 reactor (老李精细方案) | Compromised | 老姜 + 老李 + 老周 联跑压测 | Sprint-2 W3 |
| D-08 | RPC 保留双 vendor (Alchemy+QuickNode+dRPC) | Compromised | 老叶 v1.1 | Sprint-2 W1 |
| D-09 | M4.5 双轨 (OKR 10/29 + 实战 9/12 三闸) | Compromised | 老胡甘特 | 已落 |
| D-10 | RM 命名统一 (paper-mode `{Sim/Hybrid/Real}` vs slippage-mode `{Linear/Sqrt/CLOB}`) | Agreed | 小肖 + 小袁 + 小蒋 + GM 联签 | Sprint-2 W1 |
| D-11 | AET_SIGN_FAILED 单列 enum | Agreed | 老唐 schema v1.1 + 老沈 B5 mock | Sprint-2 W2 |
| D-12 | paper/live ULID 命名空间方案 A (独立 generator + mode 字段) | Agreed | 小蒋 paper engine + 小宋 验证 | Sprint-2 W2 |
| D-13 | 老孙 v4 §6.3 AWS us-east-1 主 wrap 改 GCP asia-southeast1 | Agreed | 老孙 v4.1 patch | 6/4 |
| D-14 | BIP39 passphrase 2-of-2 (老黄 + 老雷) 6 条硬条件 | Agreed | 老雷 6/4 书面 sign | 6/4 |
| D-15 | 美国 entity 短期不启动, 维持 jurisdictional-deferral | Escalated → GM 待外部律师 confirm | 老黄 RFP 起草 + 老雷决定启动 | 老雷决策 (不锁日期) |
| D-16 | PIT CI 主笔 = 小蒋 (Sprint-2 末 v0.1) | Agreed | 小蒋 + 小邓 review + 小余 + 小郑 实施 + 小宋 framework | Sprint-2 末 |
| D-17 | HMAC test vector 14 条 + 401 SOP + 月度 SDK diff | Agreed (新红线 R-17) | 老李 endpoint matrix v3 + 老练 CI | Sprint-2 W2 |
| D-18 | ADR 模板加"听取确认清单"章节 | Agreed | 老郭主笔 _TEMPLATE.md | Sprint-1 末 (6/4) |

**Escalated → GM 待外部 confirm 1 条 (D-15)**:
- 美国 entity 启动决策需外部 US fintech 律所 + IRS crypto 顾问双 confirm, 老黄 §10 8 条问题清单已起草, GM (老雷) 拍板启动后再 RFP. **本会议不锁日期**, 维持 jurisdictional-deferral. 不耻下问: 这条 GM 自己拿不定, 标"GM 待外部律师 confirm", 不假装全知.

---

## 6. Sprint-2 backlog (28 项)

详见专文 `docs/SPRINTS/sprint-02.md`. 此处仅摘 5 战斗单元分组数:

| 战斗单元 | ticket 数 | 关键 owner |
|---|---|---|
| A 核心实施 | 11 | 老周 / 老韩 / 老李 / 老叶 / 老姜 / 老吴 / 老孙 / 老王 |
| B 顾问 | 3 | 老郭 (架构评审) / 老沈 (security) / 老黄 (合规) |
| C 量化研究 | 6 | 小梁 / 小程 / 小袁 / 小肖 / 小董 / 小蒋 |
| D 数据 | 4 | 小余 / 小段 / 小冯 / 小邓 |
| E 跨域 | 4 | 老胡 (PM) / 小林 (HR) / 小米 (doc) / 老练 (CI) |
| 总计 | **28** | (≥ 25 满足约束) |

---

## 7. 红线增补 (6 条新红线)

(关联老黄 R1-R12 + 老韩 R-1..R-13, 编号续 R-14 起)

| # | 红线 | 来源发言 | Owner |
|---|---|---|---|
| R-14 | paper Mode A 套 fill_rate ~ Bernoulli(0.50-0.65) sampler, 不允许 fill_rate=1.0 (与 live 系统性偏差 Sharpe 0.1-0.3) | 小袁 §3.2 | 小蒋 paper engine v0.3 |
| R-15 | quote_age_ms > 100 && market_state ∈ {INPLAY_HOT, INPLAY_HOT_CRIT} → size *= 0.5 (microprice 失效兜底) | 小袁 §2.3 | 老韩 RM v0.3 |
| R-16 | 命名碰撞统一: paper-mode `{Sim/Hybrid/Real}` vs slippage-mode `{Linear/Sqrt/CLOB}`, 不许字母 A/B/C 跨域复用 | 小肖 §4 命名碰撞警告 | GM 联签 (Sprint-2 W1) |
| R-17 | HMAC test vector 14 条 (cursor / query / negRisk / proxy / EOA / browser_wallet) 强制 CI, 任一行复现失败 = pipeline 红 | 老李 §2.2 | 老李 endpoint matrix v3 + 老练 CI |
| R-18 | PIT CI 主笔 = 小蒋, 任何 PR 改训练 join 必扫 future-leak row = 0 (R-3 镜像红线) | 小余 §8.3 + 小蒋 §1 | 小蒋 + 老练 |
| R-19 | 阈值参数必须标 "数据依据 = 谁的实测 + 文档引用 + 实测日期", 拍脑袋值禁止进 v1.0 | 老韩 §7.1 自我反思 | 全员 + 老郭 ADR 模板 |

---

## 8. 老雷自我警示

1. **草率 ack**: 我对老李 "`.env` key 失效"草率背书, 没要求 root-cause. 反思: GM 不背书 vendor health 之外的诊断结论, 永远要求 owner 给出 root-cause 后才 ack. Sprint-2 起所有 vendor / api / cred 类问题 ack 前先问"你 base string / body / SDK diff 三件看了吗".
2. **单 PM 编造 retro 险些发生**: 上一轮我差点让一个 PM agent 编造 16 份发言. 用户当场纠正"各 owner 独立真实表态". 反思: GM 收口 ≠ GM 替全员说话, 跨域听取义务 GM 自己也要遵守. Sprint-2 起任何 retro / 评审 / 决议要求 owner 独立表态, GM 只做收口 + 拍板 + 公开承认错的事.
3. **D-15 美国 entity 不假装全知**: 老黄美国可行性 v1 指出 8 条必须律师 confirm, 我标"Escalated → GM 待外部 confirm", 不在本会议拍. 这是 GM 该做的"不耻下问".

---

## 9. 散会 + Sprint-2 启动条件

### 9.1 散会签字

- 主持: 老雷 (GM) — 签
- 五战斗单元 owner 拟代签 (各自发言已落):
  - A: 老周 (架构) / 老韩 (RM) / 老李 (协议) / 老叶 (链上)
  - B: 老郭 (评审) / 老黄 (合规) / 老沈 (security)
  - C: 小梁 / 小程 / 小袁 / 小肖 / 小董 / 小蒋
  - D: 小余 / 小段 / 小冯
  - E: 老胡 (PM) / 小林 (HR) / 小米 (doc)

### 9.2 Sprint-2 启动条件 (6/13 Planning 之前必须完成)

| # | 启动条件 | Owner | 截止 |
|---|---|---|---|
| 1 | GM 决议总表 (本会议 §5) 各 owner ack | 全员 | 6/4 |
| 2 | 老郭 ADR 模板 + 听取确认清单 章节 | 老郭 | 6/4 |
| 3 | 老雷 BIP39 passphrase 6 条 sign | 老雷 + 老黄 | 6/4 |
| 4 | 老孙 v4.1 patch (AWS us-east-1 → GCP SG) | 老孙 | 6/4 |
| 5 | 老吴 `127.0.0.1:7890` 代理产权确认 | 老吴 | 6/4 |
| 6 | 老雷 Sygnum 主体身份决定 + UBO 同意书 | 老雷 | 6/11 |
| 7 | Sprint-2 backlog ack (本会议 §6) | 老胡 + 各 owner | 6/12 |
| 8 | Sprint-2 Planning 会议 (9:00) | 老胡 主持 | 6/13 |

### 9.3 Sprint-2 周期

- 启动: 2026-06-13 (Mon)
- Mid-Sprint Check: 2026-06-18 (Wed)
- 结束 + Retro: 2026-06-26 (Fri 16:00)

---

**END.**

— 老雷 (GM), 2026-05-28 散会
