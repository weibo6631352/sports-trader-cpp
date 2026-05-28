# W5 末架构 Challenge 投票会 — PM 老胡视角

- owner: 老胡 (pm-project-manager, E-026)
- last_review: 2026-06-01
- 会议性质: Wave 27 架构 challenge 投票会 (老郭主持, 老胡 weigh-in)
- ADR 编号: ADR-009 v2 = Sonnet
- 原则: 发现问题 + 协商优化, 不向不合理架构妥协 (老板原话)
- 约束: 主管帽 cpp = 0 守住 (输出 100% 文档/投票/派单)

---

## §1 5 议题投票 (PM 视角)

PM 不做最终架构拍板 (归老郭 + GM). 我的职责: milestone 倒推 + sprint plan 影响 + 跨主管协商协调.

### 议题 1 — paper/live 共享 binary (R-2)

**PM 投票: 支持 B 选项 (CMake flag 物理隔离, 现有 R-7 做法)**

**理由:**
- W5 Wave 24 已有 R-7 enforce: `paper/paper_pm_client.hpp` vs `live/live_pm_client.hpp` 物理隔离, 312/312 测试跑通, 架构实证在先
- 若选 A (完全共享 binary): 小宋 integration test framework (1132 行, 14 case) 重写成本 = W6 前 2 周烧掉, 直接影响 M1 7/9 评审
- 若选 C/D (完全独立 binary): Sprint backlog 50+ ticket 中涉及 paper engine 的 W4-03/W5-05 等 10+ ticket 需全部重排, M4.5 (2027-05) 节点可能右移 1 个月
- **PM 红线**: 任何选项导致 S2-020/W3-02/W3-04 (paper engine 联调主线) 工期变化 > 2 周, 必须走变更流程并重评 M1

### 议题 2 — WSS 拓扑

**PM 投票: 支持 B 选项 (单 WSS + SPSC ring buffer, W6 小石 W5-03bis 已接)**

**理由:**
- 小冯 PM WSS subscriber 1044 行 (commit 3ab5dfb) + 小石 SPSC ring buffer (W5-03bis Agreed) 已成事实 backbone
- 多 WSS 拓扑 (选 C/D): ASK-A-3 Goalserve 4ts 字段 + ASK-A-1 RiskGateway ABI 稳定性两个 open ASK 尚未 ack, 切换拓扑 = 两个 open ASK 基础上叠加新变量, P0 风险
- 跨洋链路 (议题 3) 延迟预算: 老姜 latency-budget-v1 单 WSS p99 < 200ms 约束已写入 KR-A-4, 多 WSS 需要重测 RTT 预算
- **PM 预警**: W6 起 50+ ticket, WSS 拓扑变更若超 3 天 = S2-023 小冯 cold storage + S2-022 小段 Goalserve ETL 连带阻塞

### 议题 3 — 跨洋部署

**PM 投票: 支持 A 选项 (us-east-1 单 region, ADR 现有方向)**

**理由:**
- W2-02 老吴 AWS us-east-1 c6i.large x2 已 Agreed, 6/24 截止; 变更 region = W2-02 重开 + 老陈 network bench v1 (`laochen-network-bench-v1.md`) 重测
- 老沈 v2 (单 vendor 双 region, ADR-003 §5) 已经是简化后结果: 撤跨 vendor/跨境硬约束省约 3 周. 再改 region = 省下来的 3 周部分吃掉
- **PM 风险登记**: 若选 B (多 region 主备): HC-10 SRE/devops 尚未立 JD, 当前小吴兼任, W5 一 (6/1) 协商会 0 次 = 跨洋 SRE 能力未经协商验证; 招聘 timeline: JD 6/06 → 发布 6/15 → 候选 7/1 → 入职 9/1, 9/1 前多 region 运维无人
- **sprint plan 影响 (选 B)**: W5-02 (老陈+老吴跨洋实测 7/10) 结果未出前, 无法定 region 拓扑; 若强行选 B 则 S2-002 生命周期 v1.1 (老周 W3 末) 需加主备切换场景, 工期 +3 天

### 议题 4 — ML 时机

**PM 投票: 支持 C 选项 (M4.5 gate 后 shadow 上线, 现有 ML-R1 路径)**

**理由:**
- M4.5 到 M5 milestone 倒推 (小颖 acceptance v1 121 条): M4.5 定义 "paper trading 跑通 7 hard gate" (小董 gate_evaluator.hpp), ML 上线必须在 gate 通过后
- 若选 A (M1 前 ML 上线): 小邓 ML shadow signal v0.1 (W5-09bis 依赖 data-contract v1.1 小余 W4-09 Compromised 状态) — 依赖链断裂风险, 直接影响 M1 7/9 评审
- 若选 D (M5 后 ML): ML-R1~R8 (老周 v0.4 §19 已写入架构) 作废 = 老周 v0.5 §21 重写 R-20 时间戳 enforcement 的 ML 相关部分, C-2 整改工期延
- **M1 到 M4.5 倒推**: 2026-11-12 (M5) 倒推 M4.5 预计 2027-05 (北极星目标 T+36 月 §2); 中间 6 个月 M5→M4.5+ 是否够: **这是 PM smell §2 的核心议题**

### 议题 5 — vCPU pin

**PM 投票: 支持 B 选项 (vCPU 0-6 七核映射, ADR-003 D1 §15.6 方案)**

**理由:**
- S2-011 (老姜+老李+老周 vCPU0 4-5 conn 压测, 6/22 截止) 是 P0 关键路径; 压测结果出来前拍 vCPU pin 方案 = 盲目投票. 我投 B 是有条件支持: 压测 p99 < 50us 达标则 B 成立
- ASK-F-1 (老郭 → 老周 R-12 vCPU 0-6 终评) 当前 TBD 状态 (6/1 EOD 回填), 老郭架构评审结论尚未到位
- 若选 C (动态调度): R-12 "WSS event loop 禁同步 IO/锁 > 100us" 红线在动态调度下更难 CI 静态扫 (老练 W4-12 grep `co_await` 会漏 scheduler yield)
- **sprint plan 影响 (选 D / 完全不 pin)**: S2-011 压测数据作废, 老周 v0.5 §15.6 重写 + 老练 CI 扫描规则 W5-07 重来, W5 7/10 deadline 紧

---

## §2 PM Smell (红旗预警)

以下是我作为 PM 发现的、值得全员注意的结构性问题。不是结论、不拍决策，是观察。

**Smell 1: M1 到 M4.5 6 个月够吗?**

OKR M1 = 7/9 (T+6 周), M4.5 (paper trading 7 gate 通过) 对应 OKR T+18 周约 10/01. 从 M1 到 M4.5 只有 12 周 (3 个月). 但中间要完成: 信号 v1 + 回测 (M2 T+10) + RM+CLOB 下单端到端 testnet (M3 T+14) + Kelly 调参 + paper engine 联调 W2 (R-21 闸 3). 5 件大事压 12 周, **每件事无任何 buffer**. 若 ML shadow 时机选错 (议题 4), 在 M2-M3 窗口插入 ML 联调, buffer = 负数.

PM 建议: 6/25 M1 评审时同步评估 M4.5 节点是否需要右移 2 周 (10/01 → 10/15).

**Smell 2: W6 硬约束 100% 是否过快**

W5 试点 100% 覆盖率超目标 (达标 70% 目标 + 30pp), 但: 协商会 0 次, 13 跨主管 ASK 6/1 EOD 才开始回填. W5 试点的 "100% 覆盖" 是文档覆盖, 不是流程闭环覆盖. W6 一 (7/13) 硬约束上线时, 协商会只跑过 1 次 (6/02 第一次). 50+ ticket 在 0 次协商会 baseline 下全走硬约束, 若 IC 卡在主管 SLA 24h ack 等待 = ticket 积压.

PM 建议: W6 一前 6/02 协商会必须产出 ≥ 1 条跨主管 ASK 真实 ack 记录作 baseline, 否则 W6 硬约束时无参照.

**Smell 3: 6/02 协商会 framework 未验证就转 W6 硬约束**

本次 5 议题 (paper/live binary + WSS + 跨洋 + ML + vCPU) 都是跨主管 (A/B/C/D/E) 协商议题. ADR-005 §6 协商会机制首次运行是 6/02. 但 W6 派单 50+ ticket 里至少 15+ ticket 涉及跨主管接口 (ASK-A-1/A-2/A-3 + ASK-B-1/B-2 + ASK-C-1/C-2 + ASK-D-2 = 8 open ASK). 协商会未验证就全量派单 = 阻塞风险链式传导.

**Smell 4: 议题 3 跨洋部署与 SRE 招聘耦合**

当前 SRE/devops 岗 (HC-10) 未立 JD. 老吴兼任跨洋 devops. 跨洋架构选型 (议题 3) 的实施复杂度直接决定是否需要提前开 HC-10. 若选多 region 主备: 老吴兼任不够 → HC-10 JD 应与 HC-04/05/06/07/08 同批 6/06 起草. 当前 §7 决议 D-09 HC 清单里没有 HC-10. **这是招聘路径上的盲点.**

**Smell 5: 议题 4 ML 时机与小颖 acceptance v1 121 条的绑定关系未明确**

小颖 acceptance-criteria-v1 121 条里 ML 相关条目 (F-19 ML shadow) 绑定在 M4.5 gate 通过后. 若议题 4 选 A (M1 前 ML 上线), 则 acceptance 标准需要重写, 小颖 v2 重来 = 验收体系动荡. 验收体系动荡 = 小宋 integration test framework 的测试用例目标移动. 这是级联影响链.

**Smell 6: 5 议题中议题 2 (WSS) 和议题 1 (binary 隔离) 耦合**

WSS subscriber (小冯) 在 paper_pm_client 下走 R-7 物理隔离. 若议题 1 改 binary 方案, 议题 2 的 WSS 代码复用路径同步变化. 两个议题不能独立投票, 需要联决.

---

## §3 5 议题优先排序 (PM 处理顺序)

| 优先级 | 议题 | 理由 |
|---|---|---|
| P0 | 议题 1 paper/live binary | W6 sprint plan 50+ ticket 直接依赖; 工期影响最大 |
| P0 | 议题 3 跨洋部署 | W2-02 老吴 6/24 截止在即; SRE 招聘 HC-10 窗口决策 |
| P1 | 议题 5 vCPU pin | S2-011 压测 6/22 截止 = 数据先于决策; 先等实测 |
| P1 | 议题 2 WSS 拓扑 | 与议题 1 耦合, 议题 1 决后联决 |
| P2 | 议题 4 ML 时机 | M4.5 → M5 timeline 影响, 但 M4.5 在 10/01 还有空间; M1 评审 7/9 前再确认 |

---

## §4 不耻下问 (PM 需要答案的跨域问题)

我作为 PM 对以下问题没有足够领域知识, 公开求助:

**Q1 → 老郭 (架构)**: 议题 1 若保持 R-7 CMake flag 隔离, live binary 何时需要真正独立编译 (M3? M5?)? 这个时间点决定 sprint backlog 是否需要在 M3 前插入 binary split ticket.

**Q2 → 老韩 (风控)**: 议题 5 vCPU pin — 若 S2-011 压测 p99 > 50us 不达标, B 选项降级到 C (老钱 §5.2 MVP NBA only), RM p99 ≤ 200us 约束是否还守得住? 守不住 = R-24 触发.

**Q3 → 老陈 (网络)**: 议题 3 跨洋 — us-east-1 到 Polymarket API 实测 RTT 是否已有数据 (`laochen-network-bench-v1.md`)? 若 p99 > 500ms 则多 region 方案根本无意义 (跨洋就是瓶颈). 我需要这个数字才能在 sprint plan 里给跨洋部署的 ticket 定工期.

**Q4 → 小梁 (量化)**: 议题 4 ML shadow — M4.5 7 hard gate (小董 gate_evaluator) 里有没有专门的 ML shadow accuracy gate? 如果没有, ML 上线时机本质上是无 gate 的, 这是 smell 1 里 6 个月 buffer 问题的子集.

**Q5 → 小林 (HR)**: Smell 4 里提到 HC-10 SRE 岗. 现有 HC 清单 D-09 是 HC-04/05/06/07/08 五岗. 是否需要在 6/06 JD 起草批次里加 HC-10? 跨洋选 A 则老吴兼任够, 选 B 则不够. 6/06 前老郭拍 ADR-009 结论后, 小林 48h 内决定是否补 HC-10.

---

## §5 W6 Sprint Plan 受 5 议题影响的派单 Update

W6 派单约 50+ ticket. 5 议题结论未出前, 以下 ticket 处于条件锁定状态 (不派单直到该议题 ack):

| ticket 编号 | 涉及议题 | 条件 | 锁定 owner |
|---|---|---|---|
| W6-A-binary-split | 议题 1 | 选 B (保 R-7 flag) = 不开新 ticket; 选 A/C/D = 新开拆分 ticket | 老周 |
| W6-A-wss-topology | 议题 2 | 选 B (单 WSS) = 不动; 选 C/D = 新开 SPSC 多路 ticket | 老周 + 小冯 |
| W6-A-crossregion | 议题 3 | 选 A (单 region) = 不动 W2-02; 选 B = 重开 SRE 票 + HC-10 | 老吴 + 小林 |
| W6-C-ml-shadow | 议题 4 | 选 C (M4.5 后) = 小邓 ML shadow 原计划不动; 选 A = 插入 W6 联调 ticket | 小邓 + 小梁 |
| W6-A-vcpu-bench | 议题 5 | S2-011 压测 6/22 结果出后决定; 不达标走 MVP NBA only ticket | 老姜 + 老周 |

**已确定不受 5 议题影响的 W6 主线 (可立即派单):**
- ADR-006 cpp-httplib 落地 (老周 FetchContent)
- ADR-007 VirtualMatcher Mode A 切换 (小袁 W6 末)
- ADR-008 Goalserve de-vig 建模 (小梁 + 老彭)
- R-20 4 ts 整改 C-2 (老周 v0.5 + 老孙 v5.1 + 老韩 v0.3.1 + 老唐 v1.1), 全部 6/26 截止
- HC-04/05/06/07/08 JD 起草 (小林, 6/06 EOW)
- 协商会 6/02 第 1 次 (老胡主持, ASK-A-1 ~ ASK-D-2 真实跑一轮)

**W6 Sprint Plan 更新 deadline:** 老郭 ADR-009 v2 结论出后 24h 内, 老胡出 W6 backlog v2, 公示周报 §6.

---

**主持记录:** 老胡 (PM, E-026)
**归档:** 小米 (E-040, doc-curator)
**声明:** 本文为 PM weigh-in 文档. 架构最终结论归老郭 ack + GM 拍板. 投票理由均从 milestone / sprint plan / 风险登记 视角出发, 无架构技术主张.
**最后更新:** 2026-06-01 by 老胡
