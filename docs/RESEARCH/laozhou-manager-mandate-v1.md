# 主管就职宣言 v1 — 老周 (A 系统工程部)

- **Owner:** 老周 (E-001, cpp-chief-architect)
- **Date:** 2026-05-28
- **Last review:** 2026-05-28
- **触发:** ADR-005 部门主管 mandate (用户原话 "各部门应该评一个主管, 理论上主管尽可能的统筹部门的人员, 而不是事事亲力亲为")
- **权威输入:**
  - `docs/ADR/2026-05-28-department-manager-mandate.md` (ADR-005 我的 mandate)
  - `docs/HIRING/employee-registry.md` (A 单元 15 人 + 自己)
  - `docs/HIRING/hr-pulse-check-2026-05-28.md` (小林对老周 9/10 红, A 单元单点)
  - `docs/MEETINGS/2026-05-28-gm-business-needs-must-haves.md` (GM M-01 ~ M-10)
  - `docs/RESEARCH/laoqian-business-capabilities-v1.md` (CPO 16 BC, A 单元 primary 7 个)
  - `docs/RESEARCH/xiaodu-prd-v2-user-journey.md` (PRD v2, 4 role)
  - `docs/RESEARCH/xiaoying-acceptance-spec-v1.md` (M1 38 acceptance)
  - `docs/SPRINTS/sprint-02.md` §1.6.4 + §1.6.4-bis (W5 9 + 9 派单)
  - `docs/RESEARCH/laozhou-architecture-v0.6-e2e.md` (端到端架构 v0.6, 我 owner)
- **完成汇报锚:** 主管就职 + 15 IC 工作量 + W5 派单拆 24 子任务 + 跨主管协商 5 接口 + 2 HC 申请

---

## §1 A 单元成员清单 (15 人, 工号 + persona file)

> 与 `employee-registry.md` 14-43 行对齐. 含 IC pool 10 个独立行.

| 工号 | Persona | name (file) | 职位 | 当前状态 |
|---|---|---|---|---|
| E-001 | 老周 (我) | `01-cpp-chief-architect.md` | **Manager** (主管) | Active, 架构主权 v0.1-v0.6 |
| E-002 | 小马 | `02-cpp-hot-path-engineer.md` | Senior IC | Active, 热路径核心 |
| E-003 | 老陈 | `03-cpp-network-engineer.md` | Senior IC | Active, 网络层 |
| E-004 | 小赵 | `04-cpp-serialization-engineer.md` | Senior IC | Active, 序列化 (simdjson) |
| E-005 | 老王 | `05-cpp-persistence-engineer.md` | Senior IC | Active, WAL 框架 |
| E-006 | 老孙 | `06-crypto-signing-expert.md` | Senior IC | Active, signer (撤 Rust 后 C++ 重写) |
| E-007 | 老李 | `07-polymarket-protocol-expert.md` | Senior IC | Active, PM 协议 + endpoint matrix |
| E-008 | 小田 | `08-sports-market-expert.md` | Senior IC | Active (兼 D 数据仓库) |
| E-010 | 老吴 | `10-linux-sre-devops.md` | Senior IC | Active, SRE + 工具栈 |
| E-011 | 小郑 | `11-observability-engineer.md` | Senior IC | Active, observability |
| E-039 | 老姜 | `39-performance-engineer.md` | Senior IC | Active, latency budget |
| E-041 | 小石 | `41-data-structures-expert.md` | Senior IC | Active, SPSC / RCU |
| E-042 | 小肖 | `42-senior-algorithm-engineer-a.md` | Senior IC | Active, SlippageModel |
| E-043 | 小颜 | `43-senior-algorithm-engineer-b.md` | Senior IC | Active |
| E-035-01..10 | 小卢-01..10 | `35-senior-cpp-ic-pool.md` | IC (pool) | Active × 10 |

**单元总人数:** 15 (含我自己) + 10 IC pool = **25 编制**, A 单元是公司**最重单元** (CPO 16 BC 中 primary 7 个).

---

## §2 我作为主管要做的 vs 不做的

### §2.1 Do 8 项 (主管的本职)

1. **接 GM 业务目标拆任务:** GM 下达 "业务目标 + 截止 + 约束", 我自己拆成具体技术任务派给 14 个 IC + 10 小卢. GM 不指定 "谁做什么". 24h 内 ack, 48h 内拆出 IC 任务.
2. **单元 backlog 排队 + 优先级 (W5 / W6 / W7):** A 单元 backlog 我定, 周期 P0/P1/P2 + W5/W6/W7 时间窗双轴排序.
3. **每个 IC 产出 first review:** IC 交付 → 我先 review (架构 / 接口契约 / R-12 / R-11 / R-20 三红线), 通过后送 老高 (代码质量) / 老郭 (架构) / GM. 不通过我退回 + 给具体修改方向.
4. **跨单元接口对接:** 与老韩 (B) / 小梁 (C) / 小余 (D) / 老胡 (E) 协商, 协商不下走 ADR-005 §4.1 升级老雷 (48h 不下).
5. **单元内 1:1 + KPI:** 14 个 IC + 10 小卢, weekly 1:1 (15 min/人), 月度 KPI 评估, 与小林 (HR) 联动.
6. **架构主权 (v0.1 ~ v0.6 我 owner):** 端到端架构 v0.6 我 owner, 重大变更 (vCPU 分配 / SPSC 拓扑 / WAL 物理隔离) 我拍, 顾问团评审通过后入 ADR.
7. **紧急 hotfix < 2h 例外可写代码:** 仅限 P0 hotfix (如 RM HALTED 死锁 / WAL 物理隔离破坏), 写完归档 IC 接手 long-term fix.
8. **月度轮值 GM 助理:** 我 6 月第 1 轮 (ADR-005 §4.3), dial-in 老雷学习 GM 视角.

### §2.2 Don't 5 项 (主管不做的)

1. **不写正常 sprint 代码:** 例外仅 §2.1.6 架构原型 (proof-of-concept ≤ 200 行) + §2.1.7 紧急 hotfix < 2h. 任何 W5/W6/W7 正常 ticket 我**派给 IC**, 不自己写.
2. **不替老雷拍战略:** 战略归 GM + CPO. 我可上呈技术约束 (如 "M1 真 settle 不现实"), 但拍战略 (M1/M4.5/M5 时间线) 归 GM.
3. **不一票否决其他单元产出:** 除非该单元产出触动 A 单元红线 (R-11 / R-12 / R-20 / vCPU 分配 / WAL 物理隔离), 触动我立刻升老雷 / 老郭, 不私下否决.
4. **不替 IC 单方面承诺其他 IC 工作量:** 跨 IC 排队我单元内可 (例: 老李 + 小郑 联调), 跨主管协商 (例: 老李 ↔ 小段) 必走主管对接.
5. **不被 GM 拉去做 IC 工作:** 我接业务目标, 不接技术任务. 如果 GM 试图越级派单到 A 单元 IC, 我拒接 (按 ADR-005 §3.3 5 题自检 #5 enforcement).

---

## §3 单元内 IC 当前工作量评估 (0-10 + 风险信号)

> 基础: 小林 HR pulse (老周 9/10 红). 这里我作为主管下钻到 14 IC + 10 小卢的细粒度. 评分基于 W4 末实际负载 + W5 派单后预计.

| 工号 | Persona | 工作量 (0-10) | 风险信号 | HR 建议 |
|---|---|---|---|---|
| E-001 | 老周 (我) | **9/10 红** | 主管单点, 14 IC 协调 + 架构 v0.7 + 主管对接 5 接口 | 6/1 老周 1:1 (HR-W4-07), 7/15 老冀到岗前过渡 |
| E-002 | 小马 | 6/10 黄 | 热路径核心 (BookBuilder RCU + SignalEngine), W5 SignalQueue 接入 | 工作量稳定, 维持 |
| E-003 | 老陈 | 7/10 黄+ | 跨洋链路实测 (W5-02) + WSS reconnect + R-12 enforce 评审 | W5 与老吴 / 小段 协作, 维持 |
| E-004 | 小赵 | 4/10 绿 | simdjson 整合 + 4ts stamp 字段对齐, W5 低载 | 工作量充足空间, 可接小卢 IC pool 借调 |
| E-005 | 老王 | **8/10 黄+** | 4 wal kind (paper_audit / risk_audit / paper_position / paper_mldata) 一人扛 group commit + fsync 线程池 | **HC-04 申请: wal-storage-engineer 扩编 1 人** (见 §8) |
| E-006 | 老孙 | **8/10 黄+** | 撤 Rust 后 signer v4 C++ 重写 (libsecp256k1 + OpenSSL + libsodium + libfido2 + SecureBuffer 三层), W5 PaperSigner mock 优先 | M5 live signer deferred, W5 仅 paper stub, 工作量缓解到 7/10 |
| E-007 | 老李 | 7/10 黄+ | polymarket-client C++ (W5-01bis) + endpoint matrix v3 + 14 HMAC test vector + active 池过滤 | W5 高峰, W6 缓 |
| E-008 | 小田 | **8/10 黄+** | 兼 A (sports-market) + D (data-warehouse) 双单元, W5 LiveSection 5 enum 分类 | 我与小余主管协商 6/1 前明确小田主单元归属 (跨主管协商 §5) |
| E-010 | 老吴 | 6/10 黄 | SRE + Docker compose dev stack (W5-08bis) + 工具栈 + 跨洋实测协作 | 维持 |
| E-011 | 小郑 | **8/10 黄+** | 76 metrics 一人扛 (W5-05bis Prometheus 12 指标 + W6/W7 64 metrics 扩展) + 4 stack (Prometheus + Grafana + Loki + Tempo) | **HC-05 申请: observability-2 扩编 1 人** (见 §8) |
| E-039 | 老姜 | 6/10 黄 | latency budget + vCPU0 4-5 conn burst 压测 (W5-03) + benchmark | 维持 |
| E-041 | 小石 | 5/10 绿 | SPSC ring 5 capacity (W5-03bis) + 数据结构选型 | 维持, 可接小卢借调 |
| E-042 | 小肖 | 5/10 绿 | SlippageModel C++ lib header-only + 7 case 单测 (W3 已交), W5 paper engine 接入 | 维持 |
| E-043 | 小颜 | 4/10 绿 | 算法工程 B, W5 无独立 ticket | 可接 W5 backlog (见 §4 子任务分配) |
| E-035-01..10 | 小卢-01..10 | 3/10 绿 (IC pool) | 灵活借调, 当前已埋 W2 HC-01 初面降级位 (老陈分担), 其余 9 人未激活 | IC pool 是弹性缓冲, W5 我激活 3 人接 backlog (见 §4) |

**A 单元单点红榜 (我作为主管的关切):**

1. **老周 (我) 9/10 红** — 主管单点, 6/1 与小林 1:1 + 7/15 老冀到岗后转移 PaperSigner / nonce_mgr / lifecycle 协调任务
2. **老王 8/10 黄+** — 4 wal kind 单点, HC-04 必招
3. **小郑 8/10 黄+** — 76 metrics + 4 stack 单点, HC-05 必招
4. **老孙 8/10 黄+** — signer v4 重写, M5 live deferred 后缓解
5. **小田 8/10 黄+** — 兼 A + D, 跨主管协商主单元归属

---

## §4 A 单元 W5 派单 backlog v1 (主管自己拆, 不让 GM 拆)

> 老胡 sprint-02.md §1.6.4 + §1.6.4-bis 给的是 "M1 评审主线 + M1 代码补齐主线" 高阶视角. 我作为主管把高阶 ticket **拆成 IC 层子任务 + 估期 + 依赖 + 优先级**.

### §4.1 W5 派单拆解表 (24 子任务)

| 子 Ticket | 父 Ticket | 子任务 | IC | 估期 | 依赖 | 优先级 |
|---|---|---|---|---|---|---|
| W5-01-A | W5-01bis | polymarket-client header (REST + WSS interface) | 老李 | 1.5d | 老李 PM 需求 v1 已交 | P0 |
| W5-01-B | W5-01bis | REST GET /books active 池过滤 实现 | 老李 | 1d | W5-01-A | P0 |
| W5-01-C | W5-01bis | 14 HMAC test vector 落 ctest | 老李 (主) + 老孙 (review) | 1d | endpoint matrix v3 | P0 |
| W5-01-D | W5-01bis | 4ts ingestion_ts stamp 字段 | 老李 (主) + 小赵 (struct align) | 0.5d | W5-01-A | P0 |
| W5-02-A | W5-02 | 跨洋实测脚本 (us-east-1 → PM REST + WSS + Goalserve + Polygon edge) | 老陈 (主) + 老吴 (infra) | 1.5d | 老吴 .env 跨洋节点 | P0 |
| W5-02-B | W5-02 | p50/p95/p99/p99.9/max 五档统计 | 老陈 (主) + 小段 (D 跨) | 1d | W5-02-A | P0 |
| W5-02-C | W5-02 | 实测结果交老郭重定 RM 阈值 (R-01 触发评估) | 老陈 → 老郭 | 0.5d | W5-02-B | P0 |
| W5-02bis-A | W5-02bis | PM WSS sports channel subscriber (libwebsockets) | 小冯 (D 主) + 老李 (review) | 1d | W5-01-A | P0 |
| W5-02bis-B | W5-02bis | exp backoff reconnect + bounded queue + drop policy | 小冯 (D 主) + 老陈 (network review) | 1d | W5-02bis-A | P0 |
| W5-03-A | W5-03 | vCPU0 4-5 conn burst 压测 fixture | 老姜 (主) + 老李 (PM client) | 1d | W5-01-A | P0 |
| W5-03-B | W5-03 | p99 < 50us 验证 + 不达标 fallback 设计 (NBA only) | 老姜 → GM 老雷 | 0.5d | W5-03-A | P0 |
| W5-03bis-A | W5-03bis | rigtorp SPSCQueue FetchContent + CMake 接入 | 小石 (主) | 0.5d | v0.6 §17 拓扑 | P0 |
| W5-03bis-B | W5-03bis | 5 ring capacity 落代码 (MarketEvent 65536 / SignalOutput 8192 / RiskDecision 4096 / VirtualFill MPMC / MLSignal 4096) | 小石 (主) + 小马 (热路径 review) | 1.5d | W5-03bis-A | P0 |
| W5-03bis-C | W5-03bis | back-pressure counter + drop policy (vcpu0_enqueue_drop_total) | 小石 (主) + 小郑 (metric 接入) | 0.5d | W5-03bis-B | P0 |
| W5-04bis-A | W5-04bis | `build/scripts/e2e_smoke.sh` 编写 (paper engine + PM client + Goalserve + WSS + SPSC + audit WAL 全链路启动) | 老李 + 老吴 (并跨) | 1d | W5-01 / W5-02bis / W5-03bis 全 ✓ | P0 |
| W5-04bis-B | W5-04bis | 60s 跑通 + fail-fast + 退出码标准化 | 老李 + 老吴 | 0.5d | W5-04bis-A | P0 |
| W5-05bis-A | W5-05bis | prometheus-cpp FetchContent + CMake 接入 | 小郑 (主) | 0.5d | 独立 | P0 |
| W5-05bis-B | W5-05bis | 12 paper-engine 指标定义 + exporter | 小郑 (主) + 老韩 (B 单元 review 4 指标) | 1.5d | W5-05bis-A | P0 |
| W5-05bis-C | W5-05bis | Grafana dashboard JSON (12 指标可视化) | 小郑 (主) + 小苏 (E 跨, UI mockup 配合) | 1d | W5-05bis-B | P1 |
| W5-08bis-A | W5-08bis | `docker-compose.dev.yml` 编写 (paper engine + prometheus + grafana) | 老吴 (主) | 1d | W5-05bis-B | P1 |
| W5-08bis-B | W5-08bis | `docker compose up` 60s 全起验证 | 老吴 (主) + 老练 (CI) | 0.5d | W5-08bis-A | P1 |
| W5-A-extra-01 | (主管自加) | signer v4 PaperSigner mock interface 收口 (M5 live deferred) | 老孙 (主) + 老郭 (架构 review) | 1d | v0.6 §3.1 OrderIntent struct | P0 |
| W5-A-extra-02 | (主管自加) | LiveSection 5 enum 分类落代码 (PREGAME/Q1/Q2/H_TIME/Q3/Q4/OT) | 小田 (主, A 边) + 小宋 (E 测试) | 1d | 小田 sports-market v1 已交 | P0 |
| W5-A-extra-03 | (主管自加) | 小颜 W5 接 IC pool 借调任务 (协助小石 SPSC + 小郑 metrics) | 小颜 (主) | 2d | 灵活 | P1 |
| W5-A-extra-04 | (主管自加) | 小卢-01 / 02 / 03 激活: 接 W5 backlog 溢出 (老王 wal helper + 老陈 reconnect 单测) | 小卢-01/02/03 | 3d (并行) | W5 派单缺口 | P2 |

### §4.2 主管视角 W5 关键路径 (critical path)

```
W5-03bis-A (SPSC FetchContent) → W5-03bis-B (5 ring capacity)
                                                ↓
W5-01-A (PM client header) ────────────→ W5-04bis-A (e2e_smoke.sh)
                                                ↓
W5-02bis-A (PM WSS subscriber) ────────→ W5-04bis-B (60s 跑通)
                                                ↓
                                          M1 评审 7/9 (Thu)
```

**关键路径长度:** 4.5d (W5-01-A 1.5d + W5-04bis-A 1d + W5-04bis-B 0.5d + buffer 1.5d). W5 5 工作日刚够, **零 slack**, IC 任何 1d 延期触发 W5 末延期.

### §4.3 W6 / W7 backlog 预排 (主管前瞻)

- **W6:** Sprint-3 启动. 主线: signer v4 完整 C++ 实现 (老孙) + RM v0.4 接 STRATEGY_DECAYED (老韩跨单元) + 跨洋链路 redundancy (老陈 + 老吴) + ML feature snapshot 落地 (小颜 + 小邓 跨 F).
- **W7:** M4.5 gate 评估器 v0.1 落代码 (小董 跨 C) + WAL 4 流物理隔离 chaos test (老王 + 小宋 跨 E) + observability 完整 64 metrics (小郑 + HC-05 候选).

W6 / W7 完整拆解我下周一 (W5 启动后 24h 内) 出 v2, 现在仅占位.

---

## §5 跨单元接口需求 (与其他主管协商, 24h ack)

> ADR-005 §4.1: 主管间协商 24h ack, 48h 不下升老雷.

| # | 协商对象 | 接口 | A 单元需求 | 24h ack 期望 | 升级路径 |
|---|---|---|---|---|---|
| 1 | **老韩 (B)** | RiskGateway::evaluate() ABI stable | A 单元 PaperSigner / VirtualMatcher 接 evaluate(), 需要 RiskDecision struct lock (字段 + 顺序 + reject_code enum 21 种) 在 W5-A-extra-01 之前. **W5 周一 ack** | 6/1 EOD | 老郭仲裁 |
| 2 | **小梁 (C)** | SignalOutput struct schema | A 单元 SignalEngine (vCPU1) 出 SignalOutput 进 SPSC, 字段锁 (signal_id / intent / confidence / 4ts / feature_snapshot_id). v0.6 §3.1 已 draft, 需小梁 W5 周一 ack | 6/1 EOD | 老郭仲裁 |
| 3 | **小余 (D)** | Goalserve client struct + 4ts 字段位置 | A 单元 Ingest Reactor 接 Goalserve client (小段 W5 deliverable), 4ts 字段位置 (event_ts / data_source_ts / ingestion_ts / as_of_ts) 必须与 PM client 对齐 一致 | 6/1 EOD | 老郭仲裁 |
| 4 | **小余 (D)** | 小田归属仲裁 (兼 A + D) | 小田 8/10 黄+, 兼任不可持续. W5 起明确小田主单元归属 (A 倾向于 sports-market 主, data-warehouse 兼; 小余可能反对). | 6/2 EOD | 老雷拍板 |
| 5 | **老胡 (E)** | integration test fixture | A 单元 W5-04bis e2e_smoke.sh 需老胡 (E) 单元小宋 fixture 准备 (paper_audit.wal verify + hash chain + 4ts 单调). 小宋 W5-07bis 已派, 但接口需对齐 | 6/1 EOD | 老雷协调 |

**额外 (顾问团):**

| # | 协商对象 | 接口 | 期望 |
|---|---|---|---|
| 6 | **老郭 (F 协调)** | R-12 vCPU 分配仲裁 | v0.6 §2 vCPU0-6 分配, 老郭终评 6/1 EOD. 如果老郭判 vCPU6 reserved 不合理, 我接 patch | 6/1 EOD |
| 7 | **老高 (F)** | PR review v1.1 (W5-09bis 老高 deliverable) | A 单元 W5 24 子任务 PR 走老高 v1.1 review pipeline (R-20 grep + R-11 grep + persona 边界 grep) | 7/8 (Tue) |

**协商不下兜底:** 任何 1 个接口 48h 内未 ack → 升级老雷 (ADR-005 §6 决策机制).

---

## §6 主管 KPI 自评 (基于 ADR-005 §2.2 6 职责)

> 第一周自评是 baseline, 月末小林 + 老雷复评.

| 职责 | 自评 (1-5) | 证据 |
|---|---|---|
| 1. 拆任务 | **5/5** | W5 9 项父 ticket 拆出 24 子任务, 父→子 mapping 表 §4.1 |
| 2. 排队 | **4/5** | A 单元 W5 backlog 优先级 ack, W6/W7 仅 backlog 预排 (v2 下周出) |
| 3. review | **N/A** (W5 启动前) | 月末凭 first review 数 (期望 W5 末 ≥ 12 PR review) |
| 4. 跨单元协商 | **4/5** | 跨主管对接 5 接口 + 顾问团 2 接口 (§5), 24h ack 待 6/1 EOD 验证 |
| 5. 1:1 + KPI | **3/5** | 14 IC + 10 小卢 weekly 1:1 (15 min/人) 6/1 启动, baseline 月评 6/30 |
| 6. 不亲力亲为 | **5/5** | W5 写代码次数期望 = 0 (除 §4.1 W5-A-extra-01 架构原型 PaperSigner mock interface, 我可能写 ≤ 200 行 spec, 不写实现) |

**主管 KPI 红线 (我对自己的约束):**

- W5 末写代码 > 200 行 (除 hotfix) → 自评降 1 级, HR 介入
- 跨主管 5 接口 48h 仍未 ack → 升老雷, 自评降 1 级
- 14 IC 任一 1:1 跳过 > 2 周 → HR 红牌

---

## §7 W5 起 GM 派单约定 (与老雷的契约)

> ADR-005 §3.1 + §3.3 落地到 A 单元.

### §7.1 GM 派单流程 (3 层)

```
GM 老雷 (业务目标 + 截止 + 约束)
    ↓ (24h 内我 ack)
老周 (拆任务 + 排队 + 选 IC + 设质量门禁)  ← 我现在的位置
    ↓ (48h 内 IC 任务出)
A 单元 IC (具体技术任务)
    ↓
老周 first review → 老高 / 老郭 / GM ack
```

### §7.2 GM 不再直接派单到 A 单元 IC

**用户原话 enforcement (ADR-005):** "各部门应该评一个主管, 理论上主管尽可能的统筹部门的人员, 而不是事事亲力亲为."

**如果 GM 试图越级派单到 A 单元 IC**, 我**拒接**:

- 我作为主管发邮件 (或 wave 内 channel) "GM 此次派单越过主管层, 按 ADR-005 §3.3 5 题自检 #5 我有义务拒接. 请把业务目标交我, 我 48h 内拆出 IC 任务."
- 升级走 ADR-005 §3.2 例外 (顾问团 / 紧急 P0 / 主管本人 / 跨多单元统筹)

### §7.3 例外 (GM 可直接派的场景)

- 紧急 P0 (RM HALTED / 安全事件 / 用户原话指令 < 2h 响应)
- GM 给我本人派 (不算越级)
- 跨多单元统筹 (老胡 PM 周报 / 老郭 架构评审 / 老高 PR review v1.1) — 这些本身就是跨单元

---

## §8 给 HR 小林的扩招建议 (本次主管申请)

> 主管申请, 老雷最终批. 我不替老雷拍, 仅上呈建议.

### §8.1 HC-04 wal-storage-engineer

- **背景:** 老王 (E-005) 8/10 黄+, 4 wal kind (paper_audit / risk_audit / paper_position / paper_mldata) 一人扛, group commit + fsync 线程池 + WAL 4 流物理隔离 chaos test (W7 主线) 全压老王
- **JD 方向:** Senior C++ engineer, 专精 fsync / mmap / 文件系统 / WAL / fault-tolerance, 协助老王分担 paper_mldata.wal + paper_position.wal 两 stream
- **目标入职:** 8/15 (Q3 开局)
- **JD 起草:** 我 + 老吴 + 小林联签, W5 起草

### §8.2 HC-05 observability-2

- **背景:** 小郑 (E-011) 8/10 黄+, 76 metrics (W5 12 + W6 32 + W7 32) + 4 stack (Prometheus / Grafana / Loki / Tempo) 一人扛
- **JD 方向:** Mid-senior observability engineer, 专精 Prometheus exporter + Grafana dashboard + Loki tail + Tempo trace, 协助小郑分担 64 metrics 实现 + Grafana 完整看板 (BC-09 性能监控 + BC-15 M4.5 看板的 UI 侧)
- **目标入职:** 8/15 (Q3 开局)
- **JD 起草:** 我 + 老吴 + 小林联签, W5 起草

### §8.3 与小林 HR pulse 对齐

- 小林 pulse 已识别 "老周 9/10 红" (A 单元单点), HC-01 老冀 7/15 兜底 → 我 ack
- 小林 pulse 未列 HC-04 / HC-05, 我作为主管补充 (主管下钻细粒度发现单点)
- 小林 6/1 老周 1:1 (HR-W4-07) 我接, 议程加 HC-04 / HC-05 JD 联签时间表

---

## §9 完成汇报

> 与 ADR-005 §5 主管就职宣言要求对齐.

1. **主管就职:** 老周 (E-001) A 系统工程部主管, ADR-005 2026-05-28 立, 架构主权 + 单元统筹双 hat
2. **15 IC 工作量评估:** 5 黄+ (老王 / 老孙 / 老李 / 小田 / 小郑) + 4 黄 (老陈 / 老吴 / 小马 / 老姜) + 5 绿 (小赵 / 小肖 / 小颜 / 小石 / IC pool), 我自己 9/10 红 (单点)
3. **W5 派单拆 24 子任务:** §4.1 表, 父 ticket 9 项 (老胡 §1.6.4-bis 8 项 + 主管自加 4 项) → 子任务 24 项, 关键路径 4.5d, 零 slack
4. **跨主管协商 5 接口:** 老韩 (RiskGateway ABI) / 小梁 (SignalOutput schema) / 小余 (Goalserve client + 小田归属) / 老胡 (integration test fixture) + 顾问团 2 (老郭 vCPU 分配 / 老高 PR review v1.1)
5. **2 HC 申请上呈小林 + 老雷:** HC-04 wal-storage-engineer (老王分担) + HC-05 observability-2 (小郑分担), 目标入职 8/15

---

## §10 风险登记

| ID | 风险 | 触发条件 | 缓解 |
|---|---|---|---|
| A-R-01 | 主管装睡 (ADR-005 R-41) | W5 末派单覆盖率 < 80% | 老胡周报追踪, 我月末自评接受 HR 评分 |
| A-R-02 | IC 等主管 (ADR-005 R-42) | 任何 IC blocker > 24h 我未响应 | 我手机 + slack push + 老胡周报 escalate 老雷 |
| A-R-03 | 跨主管 48h 不下 | §5 5 接口任一 48h 内未 ack | 升老雷 (ADR-005 §6) |
| A-R-04 | GM 越级派单 | GM 试图直派 A 单元 IC | 我按 §7.2 拒接, 升 ADR-005 §3.3 例外 |
| A-R-05 | 老王 / 小郑 单点未及时扩编 | HC-04 / HC-05 8/15 未入职 | 老雷批 → 小林 JD 6/15 起草 → 7/1 发布 → 8/15 入职; 兜底 IC pool 借调 (小卢-04 / 05 接) |
| A-R-06 | 小田归属仲裁不下 | 6/2 EOD 老雷未拍 | 我接受先 A 单元 sports-market 主, D 单元 data-warehouse 兼 50%/50%, 月末复审 |

---

**最后更新:** 2026-05-28 by 老周 (A 系统工程部主管)
**下次 review:** Sprint-2 retro 7/10 (Fri), 验证 §4 W5 派单 24 子任务交付率 + §5 跨主管 5 接口 ack 率
