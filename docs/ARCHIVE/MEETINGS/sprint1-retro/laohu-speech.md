# Sprint-1 Retro · 老胡发言

- Speaker: 老胡 (pm-project-manager)
- Date: 2026-05-28
- Sprint: S1 retro
- 受听: 老雷 + 老郭 + 老钱 + 5 单元 owner + 全员
- 关联:
  - `docs/RESEARCH/laohu-master-gantt-v1.md`
  - `docs/RESEARCH/laohu-risk-registry-v1.md`
  - `docs/OKR/2026-Q2-Q3-startup-season.md`
  - `docs/RESEARCH/xiaojiang-paper-trading-engine-v0.2-cpp.md`
  - `docs/RESEARCH/laoqian-mvp-scope-rejection-v1.md`

---

## 0. 一句话

我接受 paper D1 8/29 / M4.5 gate 9/12 / M5 实盘 11/12 这条新时间轴, 但 **6 周提前量我不会写进甘特, 我会写"M4.5 第一次 gate 9/12, 失败则 9/26 再判, 再失败则按 OKR 10/29 兜底", 三道闸子**. 老钱不全程跟盘的决议我认, 写进 PRD F-33 P2, 配套 dogfood 不演练. R-01 老吴 us-east-1 还成立, 因为实测数据没到. Sprint-2 我按"Z 整改 + 网络实测 + JD 发布"三件事排, 每件配 owner + 期望日期.

---

## 1. M4.5 timeline: 9/12 vs OKR 10/29 — 我的表态

**结论: 接, 但加缓冲, 不动 OKR M4.5 = 10/29 那一格.**

**小蒋 v0.2 给的时间轴 (xiaojiang-paper-trading-engine-v0.2-cpp.md §9.1):**

| 节点 | v0.1 | v0.2 (现) | OKR (锚) | 间距 |
|---|---|---|---|---|
| backtest 首报告 (P0-01) | 7/2 | 7/16 | -- | -- |
| paper D1 | 8/15 | **8/29** | -- | -- |
| **M4.5 gate 首判** | 8/29 | **9/12** | **10/29** | 早 6 周 7 天 |
| M5 实盘首笔 | -- | -- | 11/12 | (OKR 不变) |

**我为什么接 9/12:**

1. 全 C++ 单 binary (GM Wave 6 + 老高 R-11) 把 pyo3 wrapper 那 6 周省了, 这是真实工作量减少, 不是"乐观估计". 小蒋 §0.4 算账清楚.
2. 9/12 是 **gate 首判**, 不是"必须过". 7 gate 全 hard (G1-G7 + OQ-D11 无豁免), 首判 FAIL 大概率, 这是 paper 设计本意 (老雷 "未过 M4.5 严禁切实盘").
3. 9/12 提前到 OKR 10/29, 给我们留了 **3 次 gate 失败窗口** (9/12 / 9/26 / 10/10), 第 3 次再失败已经进 OQ-D11 §138 "升级老钱+老雷+小梁三方战略复盘是否撤回 MVP scope" 触发线.

**我的甘特怎么动:**

- 甘特 §1.1 W6 (8/8-8/21) 原本是 M2 节点; **新增** W7-W8 (8/22-9/4) "paper dry-run + D1 起跑" 单独列, paper engine 联调上 critical path
- 主 milestone 节点 **M4.5 不动**, 保留 10/29 作为 OKR 兜底
- **新增** "M4.5 候选首判 9/12 (信息性, 非交付)" 作为里程碑插针; 9/12 首判结果直接写进当周 GM 周报状态色 (PASS = 绿 / FAIL = 黄)
- 关键路径 §2.1 CP 重画: paper D1 8/29 上 CP, 因为它现在领先 M3 (9/3) 4 天, **paper engine 主进程实际成为 M3 RM + CLOB 端到端的下游验证场**

**我对小蒋的担忧 (不阻止, 但要标记):**

- 小蒋 §10.1 PR-9 (新, Top 1): "C++ VirtualMatcher 重写期间, paper engine 联调时间不够". 这条小蒋自标 "高". 我把它加进风险登记 **R-21** (Sprint-2 retro 入), I=4 P=4 = 16, 红色.
- 8/14 paper engine 联调 deadline 是 paper D1 8/29 的硬前置, 中间只有 2 周, 任何 1 周延误 = paper D1 推到 9/12 = M4.5 gate 推到 9/26. 这是**我每周 GM 周报必须露脸的事**.

**给老雷的话:** 9/12 是好事, 不是压力. 真正硬约束是 M5 = 11/12, 不动. M4.5 提前=我们多出 2 次 gate 重判机会, 这是我们能承受的失败次数. 失败 3 次必须升级你.

---

## 2. R-01 跨洋抖动: 老吴 us-east-1 后还成立吗?

**结论: 成立, 但**应该**降级**. 我从 I=5 P=4 (评分 20) 调到 I=5 P=3 (评分 15), 仍是 Top, 但不是绝对 Top 1.

**老郭 ADR 已经给的判断 (2026-05-28-arch-and-rm-v0.1-review.md §3.2):**

> "跨洋抖动" 在选址决定后已经基本消失. 老郭不接受"阈值这么小, 跨洋抖动会频繁触发" 的反方.

老郭把 RM STALE 阈值收紧到 WSS 2s/10s, 前提是 us-east-1 同区 RTT < 10ms.

**我为什么不直接关闭 R-01, 只是降级:**

1. **实测数据没到.** S1-021 网络实测 (老陈 + 老吴 + 小段) Sprint-2 末才出. 没有 p99.9 实测前, 老郭的判断是**架构推理**, 不是**实测背书**. 老郭自己 ADR §C-Z1 也明示 "S1-021 实测必须出 us-east-1 节点到 Polymarket WSS / Goalserve / Polygon 的 p50/p99/p99.9 数据, 实测 p99.9 > 2s 反推 WSS 阈值, 老郭重新定."
2. **跨域 listening 政策 (`2026-05-28-gm-policy-cross-domain-listening.md`).** 如果将来 GM 决议要做跨域监听 (用户高优 listening 范围扩到欧洲场次或亚洲场次), us-east-1 同区前提会被打破. 这是 R-01 不能完全关闭的另一个原因.
3. **WebSocket 非阻塞红线 (`2026-05-28-gm-redline-websocket-non-blocking.md`).** WSS 抖动 ≠ 链路 RTT. WSS 服务端推送间隔本身有抖动 (Polymarket 推送频率), 即便 us-east-1 同区, WSS 静默 > 2s 的可能性仍非零.

**降级后的缓解动作 (在风险登记 R-01 更新):**

- (不变) Sprint-2 末 S1-021 五档数字必出, 老郭基于实测重定阈值
- (不变) W6 M1 验收时 DEFERRED 频率 metric 必须 < 5%/h
- (新增) **DEFERRED → HALT 之间加二级阈值的设计选项**, 由老韩在 RM v0.2 评估: 短抖动 (< 5s) 标记 WARNING 但不 HALT, 累积 > 10s 才 HALT. 这是兜底, 老郭不一定接, 我作为 PM 把这个选项放进 backlog 给老韩
- (新增) **跨域 listening 政策的依赖**: 任何"扩 listening 域"的需求, RM 阈值必须重审, 这条挂进 ADR-001 §3.2

**触发条件 (不变):**

- 实测 p99.9 > 2s → 我立刻 page 老雷 + 老郭, R-01 重回 P=4 评分 20

---

## 3. 老钱 "不做全程跟盘" 决议: 我认?

**结论: 认. 全员闭嘴, 不让"我顺便做 X"的 PR 进 main.**

**老钱原话 (laoqian-mvp-scope-rejection-v1.md §4.1):**

> Inplay 全程跟盘 (持续 fair-value 更新) ❌ 跨洋延迟 + 半自动等级承担不起全程; 全程是 maker 题

**小杜 PRD 已经落地:**

- F-33 "Inplay 全程跟盘" 标 P2 (B-02)
- §820 "❌ Inplay 全程跟盘 (只做关键节点窗, 老钱 §3.7)"

**我的 PM 立场:**

1. **全程跟盘是 maker 题, 不是 taker 题.** MVP taker-only (老钱 §2 + xiaodu §3 订单类型 §32). taker 不需要全程报价, 只在策略触发时入场.
2. **跨洋延迟物理约束.** 老姜 §11 决策内环 500us 是机器, 但跨洋 RTT 200ms+ 是物理. 全程跟盘要求"每条 book 更新都重定价", 这对 taker 无意义, 对 maker 才有意义.
3. **半自动等级.** 操盘员不全程盯屏 (小尤 UX §0 也写了 8-12h 但不是 24h), 全程跟盘要全自动, 半自动不能背这个 SLA.

**配套动作 (我跟):**

- (PRD) F-33 P2 不入 Sprint-1 / Sprint-2 / Sprint-3 backlog. 任何 owner 在 ticket 里写"顺便加跟盘" → PM 拒
- (dogfood) 小宫 11 边缘场景 (xiaogong-dogfood-playbook-v1.md) 不演练"全程跟盘"分支, 演练"关键节点窗" + "节间/换边" + "lineup 公布" 三类 trigger
- (UX) 小尤 UX 框架 §2 决策延迟维度不评估"全程跟盘 latency", 只评估"trigger → 下单 latency"
- (BR-02) 小宫 backlog §11 也列了, "全程跟盘" 列在 v0.3+ 永久 backlog, 不删, 但不排期

**给老钱的话:** 我跟你站一队. 后面任何 owner 提"做了不亏"的 PR, 我先 reject, 再走老钱 §5.1 第 8 条复议. 你不用复述, 我知道你拒.

---

## 4. Sprint-2 我怎么排?

**Sprint-2 = W2 (6/13 - 6/26), 2 周, 26 个 ticket 上 Sprint-1 已经在跑, Sprint-2 围绕 3 件大事 + 1 件收尾.**

### 4.1 三件大事 (critical path)

| 优先级 | 事项 | Owner | 截止 | 验收 |
|---|---|---|---|---|
| P0 | ADR-001 整改 13 项落地 (C-Z1..C-Z7 + C-H1..C-H6) | 老周 + 老韩 | **6/26** | 老郭复审 0 hard block |
| P0 | S1-021 跨洋网络实测 (us-east-1 → Polymarket/Goalserve/Polygon) p50/p99/p99.9/max | 老陈 + 老吴 + 小段 | **6/26** | 五档数字报告, 老郭 + 老韩签收, RM 阈值校准 |
| P0 | HC-01 老冀 + HC-02 小秦 JD 发布 + 候选池建仓 | 小林 | **6/20 候选池 ≥ 5/岗** | 老胡 + 小林 周三 check-in |

### 4.2 一件收尾 (Sprint-1 残留)

| 优先级 | 事项 | Owner | 截止 |
|---|---|---|---|
| P1 | Sprint-1 各 owner v0.2 文档归档 (老周 / 老韩 / 小程 / 小蒋 v0.2 等) | 小米 + 各 owner | 6/26 |
| P1 | 老钱 MVP scope 拒绝清单 v1.1 (吸收 Sprint-1 新增"我顺便做 X"边界) | 老钱 + 老胡 | 6/26 |
| P1 | 风险登记 v2 (含 R-21 paper engine 联调时间不够, R-22 cross-domain listening) | 老胡 | 6/26 |

### 4.3 仪式表

| 仪式 | 日期 | 输出 |
|---|---|---|
| Sprint-2 Planning | 6/13 (周一) 9:00 | Sprint-2 backlog 文档 (`docs/SPRINTS/sprint-02.md`) |
| Mid-Sprint Check | 6/18 (周三) 14:00 | 进度通报 (重点: S1-021 实测进度) |
| Sprint-2 Retro | 6/26 (周五) 16:00 | retro 文档 + risk registry v2 |
| GM 周报 W2 | 6/15 (周一) 18:00 | 老雷 1 页纸 |
| GM 周报 W3 | 6/22 (周一) 18:00 | 同上 |

### 4.4 Sprint-2 GM 周报状态预期

- W2 (6/15): **绿**. 整改 + 实测 + 招聘三件都刚启动, 没出问题.
- W3 (6/22): **黄 (高概率)**. S1-021 实测要么没出, 要么数据让老郭重审阈值; HC 候选池 < 5/岗 大概率.
- W4 (6/26, Sprint-2 末): **黄 / 绿之间**. 看老郭复审 + 实测数据.
- 红线: 6/30 老冀 + 小秦未入职, 触发 R-07 + 启动甘特 §4.4 兜底 (老叶兼链上运维, 老周兼信号-执行, M3 顺延 2 周)

### 4.5 我对 owner 的两条 ask

1. **老韩**: RM v0.2 你按老郭 ADR 整改 C-H1..C-H6 出实现, 但**别等 S1-021 实测数据再开工**. 实测 6/26 出, 你 7/1 启动实现就晚了. 你按 ADR 给的"老郭新值"先编码 + 配 metric, Sprint-3 retro (7/10) 用实测数据校准. 这是甘特 §1.1 W3 数据接入 + RM 实现并行启动的逻辑.
2. **老周**: ADR 整改 13 项 6/26 deadline 是死线. C-Z7 (Gateway link 阻断) 是 CI hard block, 不到位整个 PR pipeline 走不动. 你跟老练 (testing-coach) 一起把 CMake link 阻断 + CI grep 扫描两件事 6/20 前 PR review. 我周三 check-in 看进度.

---

## 5. 老胡附言

- M4.5 9/12 这件事我反复想了, 不写进 OKR, 是因为**OKR 一旦动, 全员心理预期就垮一格**. OKR 10/29 不动, 但**实战目标 9/12 首判**, 这是"双轨" — 上层信号稳, 下层节奏快. 老雷你如果想统一签, 我可以把 OKR M4.5 注释 "首判 9/12, 兜底 10/29", 不动主时间.
- R-01 我从 20 降到 15, 但仍在 Top 5. 老郭说话我信, 但**实测数据没到, 我不签关闭单**. 这条不是"对老郭判断不信任", 是 PM 的留痕责任.
- 老钱不全程跟盘我认得彻底, 但要提醒一句: **小程 12 信号假设里有几条隐含"持续 fair-value"** (例如 sharp money detection 需要持续 book 比对). Sprint-2 我会让小程 + 小梁过一遍 12 条, 排查哪些信号 inplay 触发时**不需要全程跟盘也能算**, 哪些必须降级或挪后. 这是给老钱的二次背书.
- Sprint-2 三件大事一件收尾, 我**不再加 ticket**, 加就是占资源. 任何 Sprint-1 没干完的, 一律列 Sprint-2 backlog 残留段, 不进 critical path.
- 周报模板 (甘特 §5) 我下周开始用. 第一份 GM 周报 W2 6/15 老雷你能收到.

— 老胡, 2026-05-28
