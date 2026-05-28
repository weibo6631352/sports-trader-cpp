# GM Sign-off Sprint-1 Retro 决议总表

- 决议人: 老雷 (GM)
- 日期: 2026-05-28
- 编号: STCPP-ADR-011
- 性质: GM 收口决议 (含 Agreed / Compromised / Escalated 三态)
- 参引: 16 份真实独立发言 (`docs/MEETINGS/sprint1-retro/`)
- 主会议纪要: `docs/MEETINGS/2026-05-28-sprint1-retro-all-hands.md`

---

## 0. 状态定义

| 状态 | 含义 |
|---|---|
| **Agreed** | 各方一致, GM 拍板生效, 无议价空间 |
| **Compromised** | 各方有分歧, GM 给出中间方案, owner 落地 |
| **Escalated → GM 待外部 confirm** | GM 自己拿不定, 需外部 confirm (律师 / 实测数据), 不假装全知 |

---

## 1. GM 决议 18 条

### D-01 KELLY 0.25 / FILL_RATE 0.50 / PER_ORDER $5K/$2K

- **议题**: MVP 阶段 RM 参数表三档
- **各方立场**:
  - 小梁 (Batch 1 §2.3+§2.4): 1/4 Kelly 是 MacLean-Thorp-Ziemba 2010 Pareto 前沿, 不议价; HARD $5K (constexpr) + SOFT $2K (config 只可调低)
  - 小肖 (Batch 2 §1): fill_rate floor 0.50 + KELLY 0.25 乘积不重复保守 ($f^*_{adj} = f^* × E[fill\_rate]$ 已内化)
  - 老韩 (Batch 1 §2 + §7): 全收, v0.2 §7 已落
  - 老钱 (Batch 1 §3): W1 $200 hard cap 落 RM 软参数, 改值走 §5.2 中级升级
- **老雷决议**: **Agreed**
- **Owner**: 老韩 v0.2 §7 (已落) + RM C++ 实现 Sprint-2 W3
- **截止日**: 已落 (Sprint-2 实现 6/26)
- **关联**: `docs/ADR/2026-05-28-gm-signoff-rm-v0.2.md`

### D-02 P0-01 阈值 3¢ → 5¢

- **议题**: P0-01 (Pinnacle no-vig revert) 触发阈值修正
- **各方立场**:
  - 小袁 (Batch 2 §2.1): 264 双边样本实测 taker 3% + maker 0% + 0.75% rebate
  - 小梁 (Batch 1 §2.2): 重新算账 5¢ - 3% fee - 0¢ 滑点 = 2¢ 净 edge
  - 小肖 (Batch 2 §3): Linear 模型重算 $p_q=0.5 → fee=1.5¢ 不是 3¢, net edge floor 1.5-3¢
- **老雷决议**: **Agreed**
- **Owner**: 小程 catalog YAML `0.03 → 0.05`
- **截止日**: 6/12 (Sprint-2 W1)
- **关联**: 小程 P0-01 §3.1 + 小蒋 backtest v0.2

### D-03 P0-02 进 M2 hard gate (OOS Sharpe ≥ 0.8 含 fee, 不过不上线)

- **议题**: P0-02 (score-price-mismatch) 是否进 MVP 首发
- **各方立场**:
  - 老钱 (Batch 1 §2): P0-02 数学上 borderline (1.5-2¢ - 3¢ fee = 负 edge), 不进首发
  - 小梁 (Batch 1 §2.2): P0-02 在 5¢ 阈值下边际 edge 死, 同意 hard gate
  - 小蒋 (Batch 2 §2): backtest 算账 P0-02 含 fee Sharpe ≤ 0, **数学上死路一条**; 站老钱 hard gate
  - 小程 (跨域引用): P0-02 OQ-13 战略决议
- **老雷决议**: **Agreed**. P0-01 单信号 MVP, P0-02 进 M2 (8 月初) OOS Sharpe ≥ 0.8 含 3% taker fee gate, 不过不上线
- **Owner**: 小蒋 backtest v2 报告 (7/30 或 8/6) + 小程 catalog
- **截止日**: 8/6 (M2 deadline)
- **关联**: 老钱拒绝清单 v1.1 + 小杜 PRD v1.1 改 §5 F-05

### D-04 M4.5 G1-G7 全 hard, G8 观察, 1 yellow 拒

- **议题**: M4.5 实盘闸门 gate 严格度
- **各方立场**:
  - 小董 (Batch 2 §1+§2+§3): 7 hard 不松, G2 bootstrap CI 下界 > 0.3 不可替代, 1 yellow FWER 计算 0.85 不能要; G8 (RM 误拒率) 加 v1.1 观察项不进 hard
  - 老钱 (Batch 1 §3): 全收无议价
  - 小梁 (Batch 1 §2.6): 全认, G7 (shadow > random-entry) 强认
  - 老韩 (Batch 1 misalignment #5): RM 误拒率 KPI 接, 但同意"观察项不进 hard"
  - 小蒋 (Batch 2 §5): 7 gate 全 hard, G6 触发频次 5¢ 后会触碰, 允许 G6 trade_count 不足 → 窗口拉长 (不算 yellow)
- **老雷决议**: **Agreed**. G1-G7 hard, G8 观察, 1 yellow 拒, G6 不够允许窗口拉长 (5¢ 阈值后可能用 21 天)
- **Owner**: 小董 v1.1 (6/19) + 小蒋 `tools/m4_5_gate/run_gate_check.py` v2 (7/9)
- **截止日**: 小董 6/19, 小蒋 7/9 (Sprint-2 W6 在 Sprint-3 内, 此条跨 sprint)
- **关联**: 老钱 OKR M4.5 段 + `docs/RESEARCH/xiaodong-stats-validation-framework-v1.md`

### D-05 Polymarket WSS 2 conn (market + user) 分离

- **议题**: Polymarket WSS 单 conn 还是双 conn
- **各方立场**:
  - 老郭 (Batch 1 §4 L-5): 倾向 2 conn (故障域隔离永远是 RM 第一原则)
  - 老韩 (Batch 1 听取确认): 偏好 2 conn 同因
  - 老叶 (Batch 2 §1.3): 强支持 2 conn, market 挂可降级 REST, user 挂必 SAFE_MODE, 两个故障语义不同
  - 老李 (Batch 2 §4): 不反对 2 conn, 但精细化为 market_hot 1 + market_cold 1-2 + user 1 = 3-4 conn
  - 老周 v0.3 §17.1.1: 描述模糊 (1 conn 还是 2 conn 未明示, 跟生命周期 v1 §2.A.2 微冲突)
- **老雷决议**: **Agreed**. Polymarket WSS market + user 物理分离 2 conn, market 内部可再按 hot/cold 拆分 (老李精细方案, 详见 D-07)
- **Owner**: 老周 v0.4 §17.1.1 明示
- **截止日**: Sprint-2 W1 末 (6/19)

### D-06 STALE 5 档 (含 INPLAY_HOT_CRIT)

- **议题**: STALE 阈值 connection-level vs token-level + hot/cold 分段
- **各方立场**:
  - 老周 (Batch 1 misalignment #1): connection-level 误判, 推 per-token freshness
  - 老韩 (Batch 1 §3): 4 档 MarketState 分段 (INPLAY_HOT 500/2000ms / PREGAME_NEAR 2/10s / PREGAME_FAR 10/30s / OUTRIGHT 30/60s)
  - 小袁 (Batch 2 §1): **再加一档 INPLAY_HOT_CRIT (200ms/800ms)** 给 NBA Q4 末 / NFL 2-min / MLB ≥8 局一分差 / OT (n=23 实测 `T_{1/2}=0.12-0.15s`)
  - 小宋 (Batch 2 §1): 4 段 transition fixture 等老韩 v0.3 + 小袁判定函数
- **老雷决议**: **Agreed** 5 档 (含 INPLAY_HOT_CRIT)
- **Owner**: 老韩 RM v0.3 §11 + 小袁 hot 判定 code-level (Sprint-2 中给 RM v0.3)
- **截止日**: Sprint-2 末 (6/26)
- **关联**: 小袁 §1 5 档表 + 老韩 §3 misalignment #1

### D-07 vCPU0 4-5 conn 单 reactor (老李精细方案)

- **议题**: vCPU0 11 connection burst 与 R-12 红线冲突
- **各方立场**:
  - 老周 Escalate-1 (Batch 1 §4): 三选 (A 单线程多 conn / B 拆 T0a/T0b / C MVP 单 sport)
  - 老李 (Batch 2 §3.2): 推 (A) 精细版 — market_hot 1 + market_cold 1-2 + user 1 + Polygon 1 = 4-5 conn 单 reactor
  - 小袁 (Batch 2 §4.2): 倾向 (C) MVP NBA only, 11 conn 压不住 microprice 死
  - 老叶 (Batch 2 §1.4): 11 conn burst 时 newHeads p99 > 500ms 抢死 vCPU0, **必须拆**, 加 metric `polygon_wss_newheads_latency_ms`
  - 老姜 / 老周 未实测
- **老雷决议**: **Compromised**. 采老李 (A) 精细方案 (4-5 conn 单 reactor), Sprint-2 W3 老姜 + 老李 + 老周联跑压测验 p99 < 50us; 不达标降级 (C) MVP NBA only
- **Owner**: 老姜 + 老李 + 老周 联跑压测; 老周 v0.4 §17.1.1 落
- **截止日**: Sprint-2 W3 (6/22)

### D-08 RPC 保留双 vendor (Alchemy + QuickNode + dRPC)

- **议题**: RPC 选型与合规简化 ADR 张力
- **各方立场**:
  - 老黄 (Batch 1 §4): R4 美国元素简化只针对 KMS / 部署, 不针对 RPC
  - 老叶 (Batch 2 §2): **反对 RPC 单 vendor 主写**, 三条理由 (vendor incident 高概率 / stuck nonce 复盘靠备 vendor / WSS 双订阅 by blockNumber 去重已设计)
- **老雷决议**: **Compromised**. 保留双 vendor (Alchemy + QuickNode + dRPC), 但理由从"跨地域 OFAC 抗审查"改为"vendor incident 抗故障", 与合规简化 ADR §3 不矛盾
- **Owner**: 老叶 v1.1 调整理由文字
- **截止日**: Sprint-2 W1 (6/19)
- **关联**: 老叶 Y-1 GM 拍板

### D-09 M4.5 双轨 (OKR 10/29 + 实战 9/12 三闸)

- **议题**: M4.5 timeline 提前到 9/12 vs OKR 10/29
- **各方立场**:
  - 小蒋 (Batch 2 §3): C++ ground-up 节省 6 周, paper D1 8/29, M4.5 gate 首判 9/12
  - 老胡 (Batch 1 §1): 9/12 三道闸子, 不动 OKR 10/29
  - 老钱 (Batch 1 §3): 失败 3 次升级战略复盘
- **老雷决议**: **Compromised**. OKR M4.5 = 10/29 不动 (上层信号稳); 实战首判 9/12 + 重判 9/26 + 第三次 10/10 + 失败 3 次升级 GM (下层节奏快). 9/12 首判 FAIL 不算失败, 是 paper 设计本意
- **Owner**: 老胡甘特 §1.1 + GM 周报 W2/W3 状态色
- **截止日**: 已落
- **关联**: `docs/RESEARCH/laohu-master-gantt-v1.md`

### D-10 命名碰撞统一

- **议题**: paper-mode 与 slippage-mode 字母 A/B/C 跨域复用
- **各方立场**:
  - 小肖 (Batch 2 §4 + 命名碰撞警告): paper-mode `{Sim/Hybrid/Real}` vs slippage-mode `{Linear/Sqrt/CLOB}`, 不再用字母
  - 小钱 §5.1 P1.5 paper-trade Mode A/B 是 fill 模拟精度, **不是**小肖 slippage Mode
  - 小袁 (Batch 2 §3): Mode A++ (fill_rate Bernoulli) 暂用, 等 GM 命名拍板后改
- **老雷决议**: **Agreed**. paper-mode = `{Sim, Hybrid, Real}`, slippage-mode = `{Linear, Sqrt, CLOB}`, 字母 A/B/C 跨域复用禁止
- **Owner**: 小肖 + 小袁 + 小蒋 + GM 联签 (Sprint-2 W1 决议文档)
- **截止日**: Sprint-2 W1 (6/19)
- **关联**: 新红线 R-16

### D-11 AET_SIGN_FAILED 单列 enum

- **议题**: signer B5 REJECT 而 RM APPROVED 时 audit 闭环
- **各方立场**:
  - 老韩 (Batch 1 misalignment #4): 倾向单列, 父 audit = ORDER_DECISION.audit_id
  - 老唐 OQ-2 (跨域引用): 未决, 单列 vs 复用 ORDER_PLACED 空 tx_hash
  - 老叶 (Batch 2 §6 收口): 同支持 AET_SIGN_FAILED 单列 (一致性)
- **老雷决议**: **Agreed** 单列 enum. ORDER_PLACED + 空 tx_hash 语义混乱, 不接受
- **Owner**: 老唐 schema v1.1 + 老沈 B5 mock signer + 小宋 audit replay fixture
- **截止日**: Sprint-2 W2 (6/26)

### D-12 paper/live ULID 命名空间方案 A

- **议题**: paper / live ULID generator 是否共享
- **各方立场**:
  - 老韩 (Batch 1 misalignment #3): 方案 A (独立 generator + payload mode 字段) vs 方案 B (物理隔离子进程)
  - 老韩倾向 A (实现简单, 取证字段明确)
  - 小宋 (Batch 2 §3 + §6.1): 同进程并发 ULID 生成方案 A 验证
- **老雷决议**: **Agreed** 方案 A. 独立 generator + payload `mode: enum {Live, Paper, Shadow}` 冗余标识
- **Owner**: 小蒋 paper engine v0.3 + 小宋验证
- **截止日**: Sprint-2 W2 (6/26)

### D-13 老孙 v4 §6.3 AWS us-east-1 主 wrap 否决, 改 GCP asia-southeast1

- **议题**: KMS 主 wrap vendor 选择
- **各方立场**:
  - 老孙 v4 §6.3: AWS us-east-1 主 (过渡方案)
  - 老黄 (Batch 1 §4.2): 否决, R4 红线"任何环节带美国元素全部 block"包括启动期 unwrap 凭据
  - 老沈 v1: GCP asia-southeast1 主 / Azure switzerlandnorth 备 / AWS ap-northeast-1 紧急 / YubiHSM 2 离线兜底
- **老雷决议**: **Agreed**. 老孙 v4 §6.3 三 vendor 表改 GCP SG 主 (老沈 v1 §3.1), AWS us-east-1 删除
- **Owner**: 老孙 v4.1 patch
- **截止日**: 6/4 (Sprint-2 启动条件)
- **关联**: 老黄 §4 美国元素 + 老沈 v1 §3.1 + ADR Sygnum 2027-02-26

### D-14 BIP39 passphrase 2-of-2 (老黄 + 老雷) 6 条硬条件

- **议题**: Shamir 3-of-5 + 2-of-2 passphrase 双层防御
- **各方立场**:
  - 老黄 (Batch 1 §3): 接受 passphrase 角色, 6 条硬条件 6/4 老雷 sign 后启动
  - 老沈 v1 N3: 建议升级 Shamir 3-of-5 + 2-of-2 passphrase
  - 老孙 v4 §8.6: 预留位置
- **老雷决议**: **Agreed**. 老黄 §3.3 6 条硬条件全签:
  1. passphrase 生成双人离线见证 (老黄 + 老雷 air-gap)
  2. passphrase 不进数字介质
  3. passphrase 与 5 分片永不在同一物理位置
  4. passphrase 季度 (90 天) 轮换
  5. 紧急事件下老黄可单方销毁副本
  6. charter 文件明记老黄持 passphrase 不构成项目资金法律所有权
- **Owner**: 老雷 6/4 书面 sign + 外部律师 charter 起草 (6/11) + air-gap 仪式 (Sprint-2 启动前 6/26)
- **截止日**: 6/4 (Sprint-2 启动条件)

### D-15 美国 entity 短期不启动, 维持 jurisdictional-deferral

- **议题**: 美国 entity 注册可行性
- **各方立场**:
  - 老黄美国可行性 v1: 8 条必须律师 confirm, 个人偏好 BVI/Cayman LLC + 美国服务器
  - GM 原话: "服务器搬美国就合规"直觉部分对部分错 (服务器对, entity 错)
- **老雷决议**: **Escalated → GM 待外部律师 confirm**. 短期 (0-30 天) 不启动美国 entity 注册, 维持 jurisdictional-deferral. 8 条问题清单老黄已起草, 老雷决定启动时再 RFP ($15-30k 预算). **本会议不锁日期** — 不耻下问, GM 拿不定不假装全知
- **Owner**: 老黄 RFP 起草 + 老雷决策启动
- **截止日**: 不锁 (老雷决策)
- **关联**: `docs/RESEARCH/laohuang-us-entity-feasibility-v1.md` + `docs/ADR/2026-05-28-gm-policy-jurisdictional-deferral.md`

### D-16 PIT CI 主笔 = 小蒋

- **议题**: PIT correctness CI 主笔
- **各方立场**:
  - 小余 (Batch 1 §7.3+§8): 建议小蒋主笔, 数据小余供给, 小邓 review
  - 小蒋 (Batch 2 §1): 接 owner, Sprint-2 末 v0.1 设计稿, M3 跑通
  - 小宋 (Batch 2 §4): 主笔不抢 (小蒋对), 接 framework adapter
- **老雷决议**: **Agreed**. 小蒋主笔 + 小邓 review + 小余 + 小郑实施 + 小宋 framework
- **Owner**: 小蒋 (`xiaojiang-pit-ci-v0.1.md`)
- **截止日**: Sprint-2 末 (6/26)
- **关联**: 新红线 R-18

### D-17 HMAC test vector + 401 SOP + 月度 SDK diff

- **议题**: 老李 HMAC 4 bug 补救
- **各方立场**:
  - 老李 (Batch 2 §2): HMAC 4 bug 公开认 + 4 项补救承诺
  - GM 老雷 (本会议 §0.3 自我警示): 我之前 ack "key 失效"草率
- **老雷决议**: **Agreed** (新红线 R-17). endpoint matrix v3 含 14 条 HMAC test vector (curl + base string + signature 三件套), 任一行复现失败 = CI 红
- **Owner**: 老李 endpoint matrix v3 + 老练 CI 接 + 小米归档 401 SOP
- **截止日**: Sprint-2 W2 (6/19, test vector 给老孙 W1 6/13)

### D-18 ADR 模板加"听取确认清单"章节

- **议题**: 跨域听取义务从政策变日常
- **各方立场**:
  - 老郭 (Batch 1 §3.2 + §5 #1): 主笔 ADR 模板 + 自查"漏看老周生命周期 §2.A.2"
  - GM 跨域听取政策 (`docs/ADR/2026-05-28-gm-policy-cross-domain-listening.md`)
  - 小宋 §6.1: 配 test fixture (每个 reviewer 必须签字才合并)
- **老雷决议**: **Agreed**. 老郭主笔 _TEMPLATE.md, Sprint-2 起新 ADR 强制带"听取确认清单". 不签字也要明示 ("我没意见" = 赞同, "我没看过" = 不合格)
- **Owner**: 老郭 + 小宋 fixture
- **截止日**: Sprint-1 末 (6/4)
- **关联**: 跨域听取政策 ADR

---

## 2. 决议状态汇总

| 状态 | 条数 |
|---|---|
| Agreed | **13** (D-01/02/03/04/05/06/10/11/12/13/14/16/17/18) |
| Compromised | **4** (D-07/08/09 + 主纪要 §4 6 条 Compromised) |
| Escalated → GM 待外部 confirm | **1** (D-15) |
| **总计** | **18** |

---

## 3. 关联文档

- 主会议纪要: `docs/MEETINGS/2026-05-28-sprint1-retro-all-hands.md`
- Sprint-2 backlog: `docs/SPRINTS/sprint-02.md`
- 16 份真实发言: `docs/MEETINGS/sprint1-retro/*.md`
- ADR-001 (老周 v0.3 + 老韩 v0.2): `docs/ADR/2026-05-28-arch-and-rm-v0.1-review.md` (升 Accepted final)
- RM v0.2 sign-off: `docs/ADR/2026-05-28-gm-signoff-rm-v0.2.md`
- paper trade sign-off: `docs/ADR/2026-05-28-gm-signoff-paper-trade.md`
- Sygnum 承诺: `docs/ADR/2026-05-28-gm-commitment-sygnum-deadline.md`
- 跨域听取政策: `docs/ADR/2026-05-28-gm-policy-cross-domain-listening.md`
- WebSocket 不阻塞红线: `docs/ADR/2026-05-28-gm-redline-websocket-non-blocking.md`
- 美国可行性: `docs/RESEARCH/laohuang-us-entity-feasibility-v1.md`

---

**会签**:
- 老雷 (GM, 主拍): 签
- 老周 (架构 v0.3 owner, D-05/D-07/D-18): ____
- 老韩 (RM v0.2 owner, D-01/D-06/D-11/D-12): ____
- 老李 (协议 owner, D-07/D-17): ____
- 老叶 (链上 owner, D-08): ____
- 老郭 (架构评审, D-18): ____
- 老黄 (合规, D-13/D-14/D-15): ____
- 老孙 (signer, D-13): ____
- 老胡 (PM, D-09): ____
- 小梁 (financial-expert, D-01/D-02/D-03/D-04): ____
- 小程 (信号, D-02/D-03): ____
- 小袁 (微观, D-06): ____
- 小肖 (Kelly+slippage, D-01/D-10): ____
- 小董 (stats, D-04): ____
- 小蒋 (backtest+paper, D-03/D-09/D-12/D-16): ____
- 小余 (数据, D-16): ____
- 小宋 (test, D-11/D-12/D-16/D-18): ____
- 老钱 (CPO, D-01/D-03/D-04): ____

— 老雷 (GM), 2026-05-28
