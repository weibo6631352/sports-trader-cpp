# GM 推进指令 — 2026 全年功能落地 + Paper 持续盈利

- **Owner:** 老雷 (GM, professional-manager E-045)
- **Last review:** 2026-05-29
- **Status:** ACTIVE v2 — 7 主管/CPO/F协调 承诺已回收并合并 (§8); GM 裁决见 §9; GM-PAPER-G 阈值已据各单元背书重订 (§3)
- **触发:** 老板 verbatim 2026-05-29 — "开始推进项目吧, 协调各个部门, 确保今年功能都能落地, 虚拟交易的模式下能够持续盈利。"
- **抄送:** 老钱 (CPO) / 老郭 (F协调) / 5 主管 (老周/老韩/小梁/小余/老胡) / 小林 (HR)
- **配套:** [W9 W5 全量状态 audit](../SPRINTS/laohu-w9-w5-full-project-status-audit-v1.md) (老胡) / [盈利 KR Spec v1](laoqian-w8-w5-profitability-kr-v1.md) (老钱)

---

## §1 老板原话拆解 (GM 解析)

> "开始推进项目吧, 协调各个部门, 确保今年功能都能落地, 虚拟交易的模式下能够持续盈利。"

| 关键词 | GM 解析 | 量化锚点 |
|---|---|---|
| **"今年功能都能落地"** | 2026 年内端到端管线全功能跑通 (数据→信号→RM→signer→执行→paper runtime→PnL 看板), 非半成品 | M1 MVP feature-complete ≥ 95%, PnL 看板 8/8 |
| **"虚拟交易模式"** | **Paper mode** (非实盘) — 红线 R-11: paper 不得污染真账本 | paper runtime 在线 + VirtualMatcher 撮合 |
| **"持续盈利"** | 非一次性, **连续** 净 PnL > 0 (扣 fee+slippage+spread); 统计显著非噪声 | 见 §3 GM-PAPER-G 门禁 |

**GM 核心判断 — 这是一次"时间线压缩"指令:**

老钱 CPO 盈利 KR spec 里 paper 2 周稳盈 (G3) 在 ~T+22 周、M4.5 paper gate 官方排期 **2027-05**。
老板要求"今年"(2026) 实现 paper **持续**盈利 → **把 paper 持续盈利从 2027-05 拉进 2026 年内**。
这是本指令与现有计划的最大 delta, 全公司资源须围绕这条线重排。

---

## §2 GM 目标 (O1-O4, 2026 年内)

| ID | 目标 | KR (可测) | 截止 |
|---|---|---|---|
| **O1 管线贯通** | 端到端 paper 管线无断点 | ABI v0.5 全链 merge + RM v0.5 + audit replay + REST 接真 state + paper runtime 启动 | 2026-09-30 |
| **O2 盈利可证** | PnL 看板 + paper 撮合可量化盈亏 | PnL 看板 8/8; VirtualMatcher Mode A 上线; 14 日 paper 净 PnL 可出报表 | 2026-10-31 |
| **O3 持续盈利** | paper 模式连续盈利达门禁 | **GM-PAPER-G** (见 §3) 通过 | 2026-12-31 |
| **O4 零浪费** | Idle 率从 ~70% 降到 ≤ 20% | 每个 active persona W10 起有明确 ticket; 周报 idle 名单清零或给合理 standby 理由 | 2026-06 月内持续 |

**O4 是老板上一条批评 ("很多部门都没安排活就很浪费") 的直接整改, 与本次推进同步 enforce。**

---

## §3 GM-PAPER-G 门禁 ("持续盈利"的数字定义, 待 CPO/老韩/小梁 背书)

GM 在老钱 G3/G5 基础上, 为"今年 paper 持续盈利"定义专属门禁 (paper 版, 不碰实盘)。
**v2: 已据老韩 (RM) / 老钱 (CPO) / 小梁 (Sharpe) / 小余 (数据) 背书重订, GM 裁决见 §9。**

| 条件 | v2 阈值 (重订) | 背书来源 |
|---|---|---|
| 连续窗口 | **14 日软验证 (前置 gate) → 通过后 30 日正式窗口** | 老钱 ramp (避免直接烧 30 日空跑) |
| 净 PnL | 30 日累计 **> 0** (必扣 fee+slippage+**spread cost**) | 老钱 (spread 必扣, 体育盘口 spread 宽) |
| 样本量 | **n_trades ≥ 100 AND bootstrap Sharpe CI 下界 > 0** (5000 次重采样) | 小梁 (30 日日历天不等于统计显著, 看 n_trades) |
| 稳定性 | 正收益日占比 **≥ 52%** AND 无单日亏损 **> 权益 3%** | 老钱 (55%→52%) + **老韩 RM 主权 (5%→3%, 见 §9 裁决)** |
| 统计显著 | OOS Sharpe ≥ 0.5 (30 日窗口) AND t-test p < 0.10 | 小梁 + 老钱 ack |
| 分盘口核算 | **pregame Moneyline 单独分桶 gate, inplay 不计入** | 小梁 + 老钱 + 老彭 (inplay 净 edge 50-65% 概率为负) |
| 风控零失效 | RM 拒单链路零绕过 (拒单率 **8%-20%**) + paper 零污染真账本 (R-11) | 老韩 RM 主权 (拒单率 ≤10%→[8%,20%]) |
| 数据 attestation | 30 日窗口须附**数据完整性 attestation** (无 look-ahead, R-20 4ts 单调) | 小余 (D 主管签字前置) |

> v2 门禁 = 上述 8 条全满足。老韩 (RM 零失效) / 老余 (数据 attestation) / 小梁 (Sharpe) 任一不签字 = 门禁不通过。
> 联决路径: 老钱 v2 起草 → 老韩+小梁 2026-05-31 背书 → 老雷+老钱 2026-06-02 主管周同步联决 → 入 OKR SSOT。

---

## §4 部门 objective 派发 (GM → 主管, 主管自行拆 IC)

> 派单层级 ADR-005: GM 给主管"业务目标+截止+约束", 主管拆 IC。GM 不替主管派 IC。
> 模型: 主管/CPO/F协调 = Opus 4.8 (ADR-009 v3); IC = Sonnet 4.6。

| 单元 | 主管 | GM 下达的 objective | 截止 | 重点约束 |
|---|---|---|---|---|
| **A 系统工程** | 老周 | O1 管线贯通主力: ABI v0.5 收尾 + REST 接真 state + paper runtime 基座 (Frankfurt server + CI 干净); 6 名 Idle IC 全部补活 | 2026-09-30 | R-12 event loop 红线; 关键路径不得 delay |
| **B 风控合规** | 老韩 | RM v0.5 整合 (关键路径首环) + GM-PAPER-G 风控背书 + paper R-11 隔离验证 | RM: W10 W1; 背书: 48h | RM 强制门禁不可绕过; paper 零污染真账本 |
| **C 量化研究** | 小梁 | **盈利引擎主力**: alpha v2 落地 + backtest framework 实施 + FillRateModel + 信号能在 paper 产生正 net edge; 4 名 Idle IC 全部补活 (老板点名最严重 idle 区) | 信号可回测: 2026-08-31 | net edge 须扣 fee (C2≥6¢); pregame 先行 inplay stretch |
| **D 数据基础** | 小余 | paper runtime 数据供给: ETL 跨源 enforce + stats validation framework + ML data pipeline; 2 名 Idle IC 补活 | 2026-09-30 | 4 时间戳契约 R-20; schema 变更通知下游 |
| **E 产品保障** | 老胡 | **PnL 看板 8/8 (0/8 重灾区)** + 验收 spec v2 + chaos/replay test + 前端 UI + dogfood; 3 名无依赖 Idle IC 补活; 继续主持 W10 多人讨论会 | 看板: 2026-10-31 | 前端依赖 REST W10 W3; dogfood 依赖 paper runtime |
| **F 顾问团** | 老郭 (协调) | 5 名 Idle 顾问 (老张/老何/小邓/老徐/小白) 各认领独立 deliverable (老板 "顾问们别闲着"); security audit 预审 + AI/LLM pipeline gap + ML 衔接 | 各 deliverable: 2026-06 月内起 | 顾问 = advise + review, 不抢 IC 活 |
| **产品方向** | 老钱 (CPO) | GM-PAPER-G 背书 + 2026 盘口 scope 拍板 (Moneyline 主 + Soccer 3-way/Tennis 是否 stretch) + 盈利 KR v2 联决 | 背书: 48h; scope: W10 W1 | MVP scope 纪律, 不盲目铺盘口 |

---

## §5 2026 压缩时间线 (paper 持续盈利拉进年内)

```
2026-06 ── O4 idle 清零 + 各单元 W10 backlog 启动 (本指令 + 多人讨论会)
2026-07 ── ABI v0.5 全链 merge + RM v0.5 + audit replay; C 单元 alpha/backtest 推进
2026-08 ── REST 接真 state + 信号可回测出 net edge 数字; PnL 看板骨架
2026-09 ── ★ paper runtime 启动 (O1 done); VirtualMatcher Mode A
2026-10 ── PnL 看板 8/8 (O2 done); paper 14 日首轮跑通出报表
2026-11 ── M1 MVP feature-complete; GM-PAPER-G 30 日窗口起跑
2026-12 ── ★ GM-PAPER-G 通过 → 今年 paper 持续盈利达成 (O3 done)
```

**关键路径 (任一环 delay 全线顺延):**
`ABI v0.5 → RM v0.5 (老韩) → audit replay (老唐) → REST 接真 (小卢) → paper runtime (老吴 Frankfurt + 老高 CI) → 信号接入 (C单元) → 30日 paper → GM-PAPER-G`

**对比原计划:** paper gate 原排 2027-05, 本指令拉到 2026-09 启动 / 2026-12 持续盈利门禁 → **提前约 5 个月**。可行性风险见 §6。

---

## §6 风险与 GM 立场

| 风险 | 描述 | GM 立场 |
|---|---|---|
| **时间线压缩过激** | paper 提前 5 月, 关键路径已满 | 接受激进目标, 但 §3 门禁数字守底线; 若 9 月 paper 启动 miss, 11 月全体争议会重排 |
| **inplay net edge 可能为负** | 老彭警报 fee/edge=1.2-2x | 采纳 CPO 立场: pregame 先行, inplay 作 stretch, 不赌负 edge |
| **本金假设** | G5 $10K/月依赖 $100K 本金; paper 模式无真本金 | paper 阶段用模拟本金跑通逻辑, 真本金规划 GM+老板另议, 不阻塞 paper 持续盈利验证 |
| **盘口 scope 膨胀** | "所有功能落地" vs MVP Moneyline only | 2026 = 管线全功能 + Moneyline paper 持续盈利为硬目标; 其他盘口为 stretch, 老钱 W10 W1 拍 scope |
| **Idle 反弹** | 补活后若依赖未就绪又 idle | 主管周报 idle 名单 enforce; standby 须写明前置依赖 + 解锁 ETA |

---

## §7 落地动作

- [x] GM 推进指令立 (本文件)
- [x] 7 objective 派发 5 主管 + CPO + F协调, 承诺已回收 (§8)
- [x] 各单元承诺合并 → 本指令升 v2 (§8 backlog + §9 GM 裁决)
- [x] GM-PAPER-G 阈值据背书重订 (§3 v2) — 老钱/老韩/小梁/小余 已背书
- [ ] GM-PAPER-G v2 联决: 老钱 v2 起草 → 老韩+小梁 5-31 背书确认 → 老雷+老钱 6-02 联决入 OKR SSOT
- [ ] 老胡 W10 多人讨论会把本指令作为头号议程 (议题 0)
- [ ] W10 接口契约 6 项签字 (§8.2)
- [ ] O4 idle 整改纳入老胡周报每周 enforce (W10≤40%→W12≤25%→6月底≤20%)
- [ ] 9-30 checkpoint 设闹钟 (paper runtime 启动 + net edge 正数 双条件)

---

## §8 部门承诺合并 (v2, 7 主管/CPO/F协调 回收)

### 8.1 各单元 W10+ backlog (Idle IC 全部补活 — O4)

| 单元 | 补活 IC + 核心 ticket | 关键截止 |
|---|---|---|
| **A 老周** | 老陈 serialize_into 出站 (8-08) / 小赵 simdjson 收口 (8-15) / 小马 hot path latency 守卫 (8-15) / 小肖 paper 基座骨架 (9-12) / 小颜 paper 调度状态机 (9-19) / 小卢×3 REST 接真+e2e (8-22~9-30); 小郑 W11 standby; 小卢×7 pool standby | paper 基座 **9-30** |
| **B 老韩** | 老沈 RM v0.5 实施 (W10W1) / 老唐 audit replay + R-11 verify (W10W2) / 老黄 合规 standby (锚 G4 前) | RM v0.5 **W10W1** |
| **C 小梁** | 小程 P0-02 spec+backtest spec (W10W2-3) / 小蒋 backtest framework cpp (W11W2) / 小袁 FillRateModel+VirtualMatcher (W11) / 老彭 odds quality 周报+inplay 复核 (W10起) | 信号可回测 net edge **8-31** |
| **D 小余** | 小段 ETL 跨源 enforce (7-31) / 小冯 实时 feed+reconnect (8-15) / 小董 stats validation framework (8-31)+数据 attestation (10-15) / 小田#24 ML data pipeline (9-15) | 数据供给 **9-15~9-30** |
| **E 老胡** | 小颖 验收 spec v2 (W10W3) / 小杜 PRD 扩展 (W10W4) / 小宋 chaos+replay framework (W10W3) / 小苏 前端 PnL 看板 (9~10-31) / 小尤 UX (W10W4) / 小宫 dogfood checklist (W10W4) | PnL 看板 8/8 **10-31** |
| **F 老郭** | 老张 C++20 ABI 兼容审查 / 老何 AI/LLM gap PoC / 小邓 ML+PositionManager 衔接 gap / 老徐 工具栈 v2 / 小白 security 预审 | 各 deliverable **6 月内** |

**Idle 整改:** R-IDLE-C/E/F 三条 P0/P1 risk 全部给出关闭路径; 老胡周报 §6 每周 enforce idle 率 (W10≤40% → W12≤25% → 6 月底≤20%)。

### 8.2 关键路径接口契约 (W10 多人讨论会签字)

| 契约 | 双方 | 内容 |
|---|---|---|
| RM v0.5 字段冻结 | A↔B | OrderIntent 出入参 hard 冻结, 让 A-NET/A-SER 提前并行 (老周缩串行尾巴建议) |
| VirtualMatcher/FillRateModel interface | A↔C | A 给 matcher stub, C 填 fill 语义+slippage 注入点 |
| DRAIN StateMachine 复用边界 | A↔B | 老沈 DRAIN SM 复用到 paper 调度 (A-PAPER-02) |
| REST 契约 | A↔E | paper runtime 暴露给前端/dogfood 的 9 endpoint |
| ONNX hook schema | D↔小邓 | feature 列名/dtype/分区键 + Parquet 读取契约 (D-ML-05 硬前置) |
| feature 清单 point-in-time | D↔C | alpha v2 字段 + 时间对齐, 防 look-ahead (回测=实盘同源红线) |

### 8.3 一致结论 (跨单元共识)

1. **盘口 scope (老钱 CPO 拍板):** 2026 = 管线全功能 + **Moneyline only** paper 持续盈利。Soccer 3-way 不纳入 2026 (工程 1.6x); Tennis 列 stretch (GM-PAPER-G 通过后年内有余量再验)。
2. **inplay 不进 12 月 gate:** 全单元一致 (小梁估 50-65% 概率净 edge 为负) → 只认 pregame Moneyline, inplay 作 G4 后 stretch 研究。
3. **O3 持续盈利 owner 厘清 (老周 back-talk):** O1 paper 基座 = A 单元交"空管道"(9-30); **O3 管道里跑出正 PnL = C 单元 net edge + 全链, 非 A 单方可 commit**。GM v2 据此分 owner。

---

## §9 GM 裁决 (老雷)

| # | 议题 | 冲突 | GM 裁决 |
|---|---|---|---|
| 1 | 单日亏损上限 | 老韩 RM 主权要 **3%** (撞 kill switch) vs 老钱要保留 5% | **采老韩 3%**。风控红线主权 (CLAUDE.md §6 RM 单独叫停权), 5% 与 -5% 硬 kill 自相矛盾, 3% 留 2pp 缓冲。老钱无异议则不进协商会。 |
| 2 | 正收益日占比 | 老钱 55%→52% (统计) vs 老韩 55% ok | **采 52%**。小梁 "hit 54% 推不出日级 55%" 数学支持; 52% 仍 >50% 噪声线。 |
| 3 | n_trades 门 | 小梁 要加 n_trades≥100 + bootstrap CI | **采纳**。"数字说话"铁律, 30 日日历天不等于统计显著。 |
| 4 | 数据 attestation | 小余 要作通过必要附件 | **采纳**。无 attestation 的盈利数字 = 可能 look-ahead 假盈利, 小余有签字权。 |
| 5 | 拒单率区间 | 老韩 ≤10%→[8%,20%] | **采老韩**。RM 主权, ≤10% 逼策略边界试探。 |
| 6 | O3 owner | 老周: 持续盈利非 A 单方 commit | **厘清**: O1 owner=A (基座 9-30); O3 owner=C 信号 net edge + 全链协同; GM 兜底协调。 |
| 7 | 老黄合规激活 | 老韩: paper 阶段不激活, 锚 G4 前 | **采纳**。写成 G4 dependency, 避免无效占用 (与 O4 矛盾)。 |
| 8 | 9 月底 checkpoint | 老胡 PM 建议 | **设立**: 9-30 若 paper runtime 未启动 OR net edge 未出正数 → 触发全体争议会重排 O3, 不硬撞 12 月。 |
| 9 | ABI/R-11/R-12/R-20/门禁不放水 | 老郭 架构否决权 4 条不可妥协 | **GM 背书全部 4 条**。压时间线不压红线; ABI 校验未过不得启 paper runtime。 |

**GM 总结:** 7 单元承诺均可执行, Idle 全部补活。最大风险 = 关键路径串行无 buffer + net edge 真值未知。对冲 = RM 字段先冻结让 A 并行 + 9 月底 checkpoint + pregame-only 不赌负 edge。本指令 v2 作为老胡 W10 多人讨论会头号议程, 会后 W10 plan v2 final 对齐 O1-O4。

---

## §10 执行 log (Wave 1-2, 2026-05-29)

### 10.1 Wave 1 — 6 单元 spec 产出 (全部落 docs/RESEARCH/)

| 单元/IC | 交付 | 关键发现 |
|---|---|---|
| 老沈 (B) | RM v0.5 字段冻结 spec | OrderIntent 字段已冻结 (A 可并行); 查出代码缺口 (见 §10.3) |
| 小程 (C) | alpha v2 + backtest spec | de-vig 代码已存在吻合; 8-31 先验基准 57% hit/+1.8% net/Sharpe 0.7/CI下界+0.3 |
| 老陈 (A) | serialize_into 出站设计 | 出站链路全 stub, glaze 未引入; 可立即独立开工 |
| 小颖 (E) | 验收 spec v2 | 38 条改 7 条; GM-PAPER-G → 8 gate + 四方否决权 |
| 小宋 (E) | chaos+replay framework | 现有 R-11/R-20/chain 测试已跑通; paper 启动前安全网 gate 定义 |
| 小董 (D) | stats validation + attestation | ⚠️ 30 日窗统计功效仅 12-18%, 需 bootstrap CI + 双窗判定补偿 |
| 小白 (F) | security 预审 9 gaps | ⚠️ 3 CRITICAL (私钥, 见 §10.2) |

### 10.2 Wave 2 — 红线 + 数据前置

**C-2 私钥红线 → 处理方式修正 (⚠️ 老板 2026-05-29 指令 "不要删我的私钥", GM 作废"删 key"动作):**
- 老孙确认: signer 代码从不读 `WALLET_PRIVATE_KEY`, paper 用进程内随机 mock keypair, 四层隔离 (build-time 宏/构造期/SecureBuffer/账本路由) 全 PASS
- **❌ 作废原"删 .env key"方案** (老板明令不删私钥)。本地 `.env` 的 `WALLET_PRIVATE_KEY` 原样保留, 未来也不删
- ✅ **改用部署洁净 (deployment hygiene) 隔离, 不靠删 key**: paper 既然代码层用 mock keypair, 部署 Frankfurt paper server 时**本就不需要把真私钥拷过去** — 即"不投放"而非"删除"。真私钥只留在需要它的地方 (老板本地 / 未来 live 节点), paper 部署清单显式排除它
- 注: Frankfurt server 尚未采购, 当前无任何线上 paper 部署, **现在没有任何 key 需要处理**, 只是把"paper 部署不带真 key"写进 deploy SOP
- C-3 CI grep 守卫 (老高接线, 防私钥误入 log/audit); C-1 真私钥来源 (HSM/KMS/env) 走 ADR, pre-G4 ≥3 月前决议 (老沈主笔+老韩+老郭)
- 老孙红线: C-1 ADR 决议前禁替换 live stub

**Goalserve 历史回填 → 老板 2026-05-29 决议: $500/月付费源不批, 数据源以后再议, 功能优先:**
- 老板 verbatim: "不准, 数据源以后再考虑换。先这样继续推进, 功能做起来, 数据源以后想换再换。"
- **❌ The Odds API $500/月 不批** (外部付费源一律暂缓)
- ✅ **战略转向: 功能/管线建设 (O1/O2) 与数据依赖的盈利证明 (O3 net edge) 解耦**
  - **管线全速建**: RM/REST/paper runtime/VirtualMatcher/PnL 看板/chaos-replay 等所有功能不等数据, 用免费源 (Goalserve free feed 实时录 + Polymarket CLOB 实时) 即可跑通端到端
  - **盈利证明降级为 best-effort**: 8-31 net edge 数字改用"当前可得免费数据" — Goalserve API 历史探测 (path A, 6-05 测) + CSV Soccer sanity + 从现在起实时录制累积; 不为赶 8-31 花钱
  - net edge 数字日期/盘口覆盖可能顺延或先 Soccer-only, **但不阻塞 O1/O2 功能落地** (老板核心诉求 = 功能做起来)
- **数据源更换**: 留作未来选项 (The Odds API / 其他付费源 / 自建抓取), 老板想换时再评估, 现在不投入

### 10.3 RM v0.5 代码缺口 (老沈查出, Wave 3 实现中)

commit d97e952 vs 老韩 spec delta: TS_V2 窗口校验缺失 / enum 编号对不上 spec (INVALID_BYTES32_FORMAT 14 vs 16) / DD 软熔断 (-3%) 仅有硬 kill / fee estimate 未实现 / AuditRecord v1.4 三字段未透传。→ 派老沈 Wave 3 实现 (关键路径首环, 不依赖 OQ-14/W10)。

### 10.4 新增风险 (入 registry)

| 风险 ID | 描述 | 级别 | Owner |
|---|---|---|---|
| R-DATA-ZERO | ~~历史零起点~~ **关闭** → ADR-037: 我们有已订阅 Goalserve(付费) + Polymarket 订单簿, 信息充足; 自估 fair-value, 不缺数据。误判已纠正 | **关闭** | 老雷 (ADR-037) |
| R-CLOUD-DEFER | 暂不上云 (白名单审核 + 不利开发调试) → paper runtime 本地跑, Frankfurt 采购移出关键路径 | 关闭(转决策) | 老雷 (ADR-037 §2.1b) |
| R-STAT-POWER | 30 日窗统计功效 12-18%, 12 月 gate 可能拿不出显著结论 | P1 | 小董 + 小梁 |
| R-PRIVKEY-C1 | live 真私钥来源未设计 (paper 不影响, pre-G4 必关) | P1 | 老沈 + 老韩 + 老郭 |

---

*老雷 (GM), 2026-05-29 — v1 指令 + v2 部门承诺合并 + GM 裁决 + §10 Wave 1-2 执行 log。老板 "推进项目 + paper 持续盈利" 指令落地中。*
