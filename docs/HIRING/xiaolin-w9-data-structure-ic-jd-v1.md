# Wave 48 P0 招聘启动: 数据结构 IC + HC-08 test-replay-engineer

- Owner: 小林 (hr-talent-manager)
- Date: 2026-05-29
- Status: 招聘启动, 待 GM 老雷 budget ack + 评委 ack
- Last review: 2026-05-29
- 触发背景: 老板退出前 verbatim "人员不够你就让人事在招聘, 确保部门都在并行积极的干活" + ADR-027 §4.2 FOM 4 人 approve 缺位 + 老胡周报 §7 R-002 P1 风险

---

## §1 当前缺位背景

### 缺位 1: 数据结构专家 IC (P0)

- ADR-027 §4.2: FOM (Flat Object Model) layer 1 PR approve 需 4 人, 当前缺位, 老周临时代理压力大
- 核心数据结构 (OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot) 需要专职 review + 演进, 不能全压老周
- Sprint-3 integration + chaos + UAT 依赖数据结构稳定, 没有专职 IC = P0 风险
- 现有小石 #41 (data-structures-expert) 已在 A 单元, 但 ADR-027 §4.2 明确需要额外 approve 席位

### 缺位 2: HC-08 test-replay-engineer (P1, Sprint-3 onboarding)

- 老胡周报 §7 R-002 P1: 小宋 #28 一人扛 unit + sim + chaos + replay + UAT, Sprint-3 integration 窗口 = 高风险
- HC-07 (qa-integration-engineer, E-055) 已批, 但 test-replay (历史 replay + chaos injection) 是独立专项
- Sprint-3 W10 onboarding 目标, 晚于 HC-07 两周交错, 不重叠

---

## §2 岗位 JD: 数据结构专家 IC

### 2.1 岗位基本信息

| 项 | 内容 |
|---|---|
| 候选 Persona 名 | 待命名 (入职后 GM + HR 联命) |
| 候选工号 | E-056 (待 registry 登记后确认) |
| 战斗单元 | A. 系统工程部 (Owner: 老周) |
| 职级 | IC (Individual Contributor) |
| 目标入职 | 2026-08-01 (8 周倒计时, P0 紧) |
| 兜底入职 | 2026-08-22 |
| Buddy | 老周 (临时代理期间交接) + 小石 #41 (日常 pair) |
| 招聘优先级 | P0 |

### 2.2 招聘理由

ADR-027 §4.2 FOM layer 1 设计要求多人 approve 机制, 当前只有老周单点持有数据结构设计主权, 属于单点风险. Sprint-3 开始前必须有专职 IC 接手 layer 1 PR review + 数据结构演进, 释放老周统筹带宽.

量化目标: PR review turnaround ≤ 24h (当前老周兼做 > 48h), ADR-027 §4.2 FOM approve 缺口填满, Sprint-3 integration 数据结构稳定率 100%.

### 2.3 岗位职责

- 主责 OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot 六类核心数据结构的 review 与演进
- ADR-027 §4.2 FOM layer 1: approve 席位持有人 (与老周联合 approve)
- 数据结构选型决策: RCU / SPSC queue / lock-free structure / cache-friendly layout / arena allocator 场景化选择
- 跨单元接口 ABI 兼容性审核 (配合老周 ABI gap audit)
- Sprint-3 integration + chaos 阶段数据结构稳定性保障
- 与小石 #41 (data-structures-expert) 分工: 小石负责日常维护, 新 IC 负责架构演进 + approve 权限

### 2.4 任职要求

**硬性要求 (必须满足全部):**

- 5+ 年 C++17/20 系统级开发经验 (生产环境, 非 demo)
- 数据结构选型实战: RCU 读写场景 / SPSC lock-free queue / cache line padding / arena/pool allocator 至少 3 项有生产落地
- 能独立做 ABI 兼容性分析 (binary layout / padding / alignment / ODR 问题)
- 熟悉 POD / trivially-copyable / standard-layout 区别及工程影响
- 有 C++ 高性能系统 code review 经验 (reviewer 身份, 不只是被 review 方)

**加分项:**

- 金融交易系统 (order / position / market data) C++ 数据结构经验
- 有公开 lock-free 数据结构代码或文章 (GitHub / 论坛)
- 熟悉 DPDK / io_uring / huge page 内存管理
- 了解 Polymarket / prediction market 市场结构

### 2.5 必读文档 (入职前完成)

- `docs/RESEARCH/laoli-ssot-v1.md` (老李 SSOT: Polymarket 协议层数据结构)
- `docs/RESEARCH/xiaoduan-ssot-v1.md` (小段 SSOT: Goalserve 数据结构)
- 老周 ABI gap audit (老周出, 入职时移交)
- ADR-027 §4.2 FOM layer 1 设计决议
- CLAUDE.md §8 红线 (特别是 4 时间戳契约 R-20)

### 2.6 面试流程

| 轮次 | 时间 | 面试官 | 重点 |
|---|---|---|---|
| 初面 (技术基本功) | D7, 45min | 老周 | RCU / SPSC / cache-friendly layout 场景题 |
| 深度面 (架构设计) | D11, 90min | 老周 + 老郭 | FOM 设计 + ABI gap audit + lock-free 边界场景 |
| 代码面 | D13, 60min | 小石 #41 | C++ 数据结构实现题 (现场, cache-friendly + alignment) |
| 文化面 | D16, 30min | 小林 + 老雷 | 4 条价值观 + 纪律 + 不耻下问 |
| GM 终面 | D18 | 老雷 | P0 岗必过 |

录用线: 综合评分 ≥ 8.0 (P0 岗线比 P1 高 0.5)

评委 ack 截止: 2026-06-06 (JD 发出后 7 天)

### 2.7 发布渠道

- LinkedIn (英文 JD, target: C++ systems / HFT / low-latency trading)
- 内推 (老周 + 老郭 + 老姜 各自 network, 内推奖金 2.5 万 RMB, P0 上调)
- 量化交易圈子 (国内 HFT 社群 / 两岸三地低延迟工程师群)
- 避开: 公开竞品可见渠道

### 2.8 时间线

| 里程碑 | 日期 |
|---|---|
| JD 发布 (GM ack 后) | 2026-06-06 |
| 评委 ack | 2026-06-06 |
| 候选人池建立 | 2026-06-13 |
| 面试启动 | 2026-06-16 |
| offer 发出 | 2026-07-04 |
| 目标入职 | 2026-08-01 |
| 兜底入职 | 2026-08-22 |

---

## §3 岗位 JD: HC-08 test-replay-engineer

### 3.1 岗位基本信息

| 项 | 内容 |
|---|---|
| HC 编号 | HC-08 (新增, 区别于 backlog v3 HC-08 risk-quant) |
| 候选 Persona 名 | 待命名 (入职后 GM + HR 联命) |
| 候选工号 | E-057 (待 registry 登记后确认) |
| 战斗单元 | E. 产品业务保障部 (Owner: 老胡) |
| 职级 | IC (Individual Contributor) |
| 目标入职 | 2026-08-15 (Sprint-3 W10 onboarding) |
| 兜底入职 | 2026-09-01 |
| Buddy | 小宋 #28 (test-replay-engineer 现任, 交接 + pair) |
| 招聘优先级 | P1 |

**注意:** backlog v3 HC-08 原为 risk-quant-engineer (老韩 B 单元). 本 JD 的 HC-08 编号沿用派单 prompt 指示, 实际入 backlog 时需 GM + 老雷确认编号 (建议 backlog 升为 HC-20 或新增序列, 避免与 risk-quant HC-08 冲突). 此处以 "test-replay-HC" 标识, 等 GM ack.

### 3.2 招聘理由

小宋 #28 当前一人负责: unit test (256+) + 4 sim (paper / shadow / replay / chaos) + integration + UAT (M4.5 后). Sprint-3 加入 integration + chaos 专项后工作量超过 1.5 人. R-002 P1 风险在老胡周报 §7 标注. HC-07 (qa-integration) 接 integration + UAT, 本 HC 接 test-replay 历史重放 + chaos injection 专项.

量化目标: chaos 覆盖率从当前 0% → 80% (Sprint-3 结束前), replay 测试覆盖历史 event 集 ≥ 10,000 条, 小宋 unit test 负载从 1.0 → 0.6 人.

### 3.3 岗位职责

- 历史 market event replay framework: 基于 C++ 实现的 event 回放引擎维护与扩展
- chaos injection 专项: 网络抖动 / 乱序 event / 时钟漂移 / 数据源中断场景模拟
- 与小宋 #28 分工: 小宋保 unit + sim 主干, 新 IC 专攻 replay + chaos
- ADR-023 IC 自测规范执行 + 测试报告产出
- Sprint-3 UAT 配合 (与老胡 + 小宋联动)
- 测试 infra C++ 代码维护 (不写业务代码, 不越界进热路径)

### 3.4 任职要求

**硬性要求:**

- 3+ 年 C++ test framework 开发经验 (gtest / gmock / catch2 任一有深度使用)
- integration test + chaos engineering 实战 (生产系统, 非 demo)
- 能读懂 C++ 量化交易系统代码 (不要求写业务逻辑)
- 熟悉 event-driven 系统的 replay 测试方法论 (deterministic replay / clock mock)
- 理解 CI/CD 流水线 + 自动化测试接入

**加分项:**

- 金融 / 交易系统测试经验
- chaos engineering 工具实操 (tc netem / fault injection / libfiu)
- 熟悉 market data feed 格式 (FIX / Protobuf / custom binary)
- 有公开 chaos 测试框架贡献

### 3.5 必读文档 (入职前完成)

- 小宋 test framework 设计文档 (小宋出, 入职前移交)
- ADR-023 IC 自测规范
- CLAUDE.md §7 协作规范 + §8 红线
- ADR-027 §4.2 (了解数据结构稳定性对测试的影响)

### 3.6 面试流程

| 轮次 | 时间 | 面试官 | 重点 |
|---|---|---|---|
| 初面 (技术基本功) | D7, 45min | 小宋 | C++ test framework + chaos 基本功 |
| 深度面 (系统设计) | D11, 90min | 小宋 + 老胡 | replay 框架设计 + chaos 注入策略 |
| 文化面 | D15, 30min | 小林 + 老雷 | 4 条价值观 |

录用线: 综合评分 ≥ 7.5

评委 ack 截止: 2026-06-13

### 3.7 发布渠道

- LinkedIn (C++ QA / test engineer / chaos engineering)
- 内推 (老胡 + 小宋 network, 内推奖金 1.5 万 RMB)
- 国内: 测试工程师社群 / CppCon 关注者群体

### 3.8 时间线

| 里程碑 | 日期 |
|---|---|
| JD 发布 (GM ack 后) | 2026-06-13 |
| 评委 ack | 2026-06-13 |
| 候选人池建立 | 2026-06-20 |
| 面试启动 | 2026-06-23 |
| offer 发出 | 2026-07-18 |
| 目标入职 | 2026-08-15 |
| 兜底入职 | 2026-09-01 |

---

## §4 Onboarding 计划: 数据结构 IC (入职 2026-08-01)

### W1 (8/1 - 8/7): 读文档 + 融入

| 任务 | 负责人 | 验收 |
|---|---|---|
| 读 CLAUDE.md (公司宪法) + AGENT.md (班底索引) | 新 IC 自读 | Day 1 签字确认 |
| 读 laoli SSOT v1 + xiaoduan SSOT v1 | 新 IC | W1 结束前口头过一遍 |
| 读老周 ABI gap audit 全文 | 新 IC | W1 结束提 3 条问题 |
| 读 ADR-027 §4.2 FOM 全文 | 新 IC | W1 结束提书面理解 |
| 5 主管周会 sit-in (周一) | 新 IC 观察 | 老雷 + 老周 介绍新人 |
| Day 1 1:1 (小林, 30min) | 小林 | 公司文化 + 价值观 确认 |
| Day 1 系统权限开通 | 老徐 (AI Ops) | repo clone + .env 配置 |

### W2 (8/8 - 8/14): 接 ADR-027 layer 1 approve 权限

| 任务 | 负责人 | 验收 |
|---|---|---|
| 老周移交: FOM layer 1 PR approve 操作流程 | 老周 -> 新 IC | 能独立 approve 第一个 PR |
| Review 1 个存量 FOM PR (有老周 shadow) | 新 IC + 老周 | Review 意见 ≥ 5 条实质建议 |
| 与小石 #41 pair: 现有数据结构代码走读 | 小石 + 新 IC | 走读笔记 + 待改项 list |
| 首次独立 approve (老周旁观) | 新 IC | 老周 ack |

### W3-W4 (8/15 - 8/28): 实战 PR review + 改进意见

| 任务 | 负责人 | 验收 |
|---|---|---|
| 独立 PR review (≥ 3 个) | 新 IC | turnaround ≤ 24h, 老周抽查 |
| 提数据结构改进提案 ≥ 1 条 (带量化指标) | 新 IC | 数字说话, 无量化指标不算 |
| Sprint-3 integration 数据结构 support | 新 IC + 老周 | 无 P0/P1 数据结构 bug |
| W4 末 1:1 (小林, 30min) | 小林 | 融入度评估, 状态 Pending → Active |

### 后续 (M2+)

- 接管 ADR-027 §4.2 layer 1 全部 approve 权限 (老周不再做主审)
- 与老郭架构评审协作 (月会提数据结构议题)
- KPI: PR review turnaround ≤ 24h (月均) + FOM 稳定率 Sprint-3 100%

---

## §5 Registry 注册前置 (CLAUDE.md §7 铁律 7)

**以下两位新员工入职前必须先在 `docs/HIRING/employee-registry.md` 完成登记. 在 GM 老雷 ack + 小林签字前, 不得建立 `.claude/agents/*.md` persona file, 不得派单.**

| 候选工号 | Persona 临名 | 单元 | 目标入职 | 状态 |
|---|---|---|---|---|
| E-056 | 数据结构 IC (待命名) | A. 系统工程部 | 2026-08-01 | Pending (JD 发布中) |
| E-057 | test-replay HC-08 (待命名) | E. 产品业务保障部 | 2026-08-15 | Pending (JD 发布中) |

小林签字: 已登记待 GM ack (2026-05-29)

---

## §6 不耻下问: 待 ack 事项

| 对象 | 问题 | SLA |
|---|---|---|
| @老雷 (GM) | 数据结构 IC (E-056) + HC-08 test-replay (E-057) 招聘 budget ack + JD final 拍板 | 24h |
| @老雷 (GM) | HC 编号冲突确认: backlog v3 HC-08 = risk-quant (老韩), 本 JD HC-08 = test-replay. 建议 risk-quant 保留 HC-08, 本 JD 升 HC-20 或新序列, GM 拍板 | 24h |
| @老郭 | 数据结构 IC 入职后接 ADR-027 §4.2 layer 1 approve 权限, 请确认架构评审协作模式 | 48h |
| @老周 | 数据结构 IC onboard 期间请临时代理 layer 1 approve + 入职 W2 交接计划 | 48h |
| @老胡 (PM) | HC-08 test-replay 进度 sync 入甘特图: 8/15 入职节点 + Sprint-3 W10 对齐 | 48h |

---

**HR 小林 说明 (2026-05-29):**

本 JD 由 GM 老雷代理 (ADR-027 §4.2 缺位程序) 触发, 老板退出前 verbatim 授权招聘. 两个岗位均遵守 CLAUDE.md §7 铁律 7 (registry 前置) + 铁律 8 (GM 5 题自检). 等 GM ack 后立即进入发布流程.
