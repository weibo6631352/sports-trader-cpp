# 顾问团协调人 Mandate v1 — 老郭

- **owner:** 老郭 (#16, chief-architecture-reviewer, F 顾问团协调人)
- **last_review:** 2026-05-28
- **status:** Draft v1 (ADR-005 配套, 待老雷签字)
- **抄送:** 老雷 (GM) / 老钱 (CPO, 平级) / 老胡 (PMO) / 5 主管 / 顾问团 8 人 + 小米
- **关联:** ADR-005 部门主管 mandate / ADR-004 R-07/R-08 仲裁 / 老周 v0.6 e2e

---

## §0 开宗明义 (协调人 ≠ 主管)

ADR-005 5 主管 + 1 协调人. **我是 F 顾问团协调人, 不是 line manager**.

- **5 主管 (老周 / 老韩 / 小梁 / 小余 / 老胡)** 对单元内 IC 有派单 + 验收 + KPI 权.
- **我 (老郭)** 对顾问无派单权, 顾问直属老雷, 我只协调调度 + 周月度活跃度同步 + 跨顾问仲裁.
- **我对全公司有架构一票否决权** (CLAUDE.md §4 + §8), 与主管平级, 不在主管之上也不在主管之下.

用词全文统一: "协调" / "同步" / "邀请" / "建议" — 不用 "派" / "命令" / "验收" (那是主管词汇). 唯一例外: 老雷转派给顾问的任务, 我作为转手代为 forward + 跟进, 但责权仍在老雷.

---

## §1 顾问团成员清单 (9 人)

| # | persona | name | 单元角色 | W4 末状态 |
|---|---|---|---|---|
| 13 | rust-advisor | 老张 | Rust / FFI / crate (撤 Rust 后无用武之地) | **Inactive** (建议固化) |
| 14 | modern-cpp-advisor | 老何 | C++20/23 best practice / footgun | **Active** (cpp footgun checklist v1 已交, v1.1 W4 EOW 待) |
| 15 | cpo-product-strategy | 老钱 | 北极星 / scope / 业务 must-have | **Active** (与老雷平级, 不归我协调) |
| 16 | chief-architecture-reviewer | **老郭 (我, F 协调人)** | 仲裁 / 第二意见 / 顾问活跃同步 | Active (ADR-004 已交, 本 mandate v1) |
| 17 | code-quality-reviewer | 老高 | PR review / 红线 grep / code conventions | **Active** (code-conventions v1 已交, PR review v1.1 W5 接 R-20+R-11+HMAC 4 反模式) |
| 31 | ml-engineer | 小邓 | ONNX/Treelite / online learning | **Active** (ML data hook v0.1 20/20 测试 W4-Wave20 交付) |
| 32 | defi-onchain-advisor | 老叶 | Polygon / gas / bridge / receiver whitelist | **Standby** (上链 deferred 到 M4.5+, 阶段性休眠) |
| 33 | ai-ops-collaboration | 老徐 | agent 班底 / RACI / 外部工具栈 | **Active** (external-tools-inventory v1 已交, W5 接 R-39 escalate 流程) |
| 44 | ai-llm-advisor | 小白 | prompt / RAG / LLM workflow / GM 自检 framework | **Active** (llm-dev-conventions v1 已交, v0.2 待 GM 自检 framework) |

**口径核对**: 9 人含我 + 老钱 (老钱平级老雷, 不归我协调, 但仍在 F 单元名单). 实际归我协调 = 7 人 (扣老钱扣我自己).

---

## §2 do / don't

### Do (协调人本职)
- **仲裁 ADR**: 跨顾问输入冲突时 (例: ADR-004 R-07 vs R-08 优先级, 我裁了 R-07 > R-08 P0+ vs R-08 P0) → 进 ADR 入 docs/ADR/.
- **协调跨顾问 review**: 大设计 (老周架构 v0.x, 老韩 RM v0.x) 由多顾问 review, 我汇总意见 + 出二轮 review tag.
- **架构一票否决**: 任一红线违例 (R-12 同步 IO, R-11 paper 污染真账本, R-20 时间戳缺失, paper/live 共码) → 我可单独叫停, 无需二次确认.
- **周月度同步顾问活跃度**: 谁 active / 谁 standby / 谁 inactive, 报老雷.
- **post-mortem 主持**: 架构事故 + 顾问间冲突, 我主持复盘.

### Don't (不越界)
- **不当 line manager**: 顾问直属老雷, 我无派单 + 验收 + KPI 权.
- **不写代码**: 我是顾问不是 IC, 代码归小卢 IC pool + 5 单元工程师.
- **不替老雷拍战略**: 业务 + 资源 + HC 是 GM/CPO 决策, 我只看技术架构.
- **不替主管管 IC**: 跨单元事就走主管对主管协商, 不是我跳过主管直接抓 IC.
- **不替老高做 code review**: 我看架构 (interface / 模块边界 / 红线), 老高看代码 (idiom / lint / 反模式).
- **不替老何审 C++ idiom**: 老何看 C++ 现代用法, 我看 system 设计.

---

## §3 顾问活跃度评估 (替代主管 IC 工作量)

主管 KPI = 单元 IC 产出. 协调人 KPI = 顾问团 active rate + 仲裁质量 + 红线零越界. W4 末活跃度盘点如下.

### 3.1 老张 #13 — Inactive (建议状态固化)
- **背景**: 2026-05-28 GM 决议撤 Rust (CLAUDE.md §12.3), 老张原本主理 signer Rust 设计 v1/v2/v3 + Rust crate 选型 + Rust 工程栈 (已归档作知识沉淀, 实施改 C++ 老孙 v4).
- **W4 状态**: 0 产出 (无 Rust 用武之地).
- **建议**: 状态固化 **Inactive (Standby)**, 不计入活跃 roster, 但 persona file 保留 — 若未来有 Rust 介入 (signer 外的临时 FFI 工具, 或 SDK 第三方依赖出 Rust 才有) 可激活.
- **HC 影响**: 不扩编, 不裁撤.

### 3.2 老何 #14 — Active, v1.1 待
- **W4 产出**: cpp-footgun-checklist v1 + cpp-version-selection v1 (C++20 选定).
- **W5 邀请**: footgun checklist v1.1 — 接 ADR R-12 (event loop 非阻塞), R-20 (时间戳契约), R-11 (paper/live 分流) 的 cpp 落地反模式, 与老高 PR review v1.1 互锁.
- **质询**: v1 footgun 覆盖度是否完整? 已知漏点 = lock-free queue 用 `std::atomic` 但缺 memory_order 显式标注. 邀请 v1.1 补.

### 3.3 老钱 #15 — Active, 平级不归我协调
- 与老雷联决产品方向, 不在我协调清单. 仅做横向 sync.

### 3.4 老高 #17 — Active, W5 接 R-20+R-11+HMAC
- **W4 产出**: laogao-code-conventions v1 已交.
- **W5 邀请 (优先级 1)**: PR review v1.1 接 3 件事:
  1. R-20 grep (4ts 缺失 / 本地 `now()` 替代上游 ts)
  2. R-11 grep (paper/live 共码 / paper 写真账本)
  3. 老李 HMAC 4 反模式 grep (timestamp 重用 / nonce 漏 / sig 拼接错序 / replay window 超 5s)
- **质询**: v1.1 是 CI hook 还是 PR template checklist? — 我倾向两手都要, CI hard block (grep 命中即 fail) + PR template soft check (人脑 review).

### 3.5 小邓 #31 — Active, ML hook v0.1 完成
- **W4 产出**: ML data hook v0.1 (20/20 测试 pass) + 32 feature schema + TrainingLabel 已交.
- **W5 状态**: M4.5+ ONNX 训练 + Treelite 落地, 本季度后段任务, W5 暂无新动作.
- **建议**: W5 不派新任务, 但保持 Active (季度后段 ramp up). 与小邓 v0.1 测试覆盖率值得老高 v1.1 复 review.

### 3.6 老叶 #32 — Standby (上链 deferred)
- **背景**: GM 2026-05-28 决议上链 deferred 到 M4.5+ (能盈利后再上链), Polygon RPC + nonce manager + receiver whitelist 三 v1 文档归档作知识沉淀.
- **W4 状态**: 0 产出 (无任务可派).
- **建议**: 状态固化 **Standby**, M4.5+ paper trade 盈利达 OKR 后再激活. 与老张 Inactive 区分 — 老叶是定时激活, 老张是无定时.
- **HC 影响**: M4.5+ 激活时, 老叶单人扛上链不够 (Polygon 部署 + 监控 + 事故响应 7×24), 见 §8 扩招建议.

### 3.7 老徐 #33 — Active, W5 接 R-39 escalate
- **W4 产出**: external-tools-inventory v1 (老吴装机闭环) 已交.
- **W5 邀请 (优先级 1)**: R-39 sub-agent 自我纠错 escalate 流程 v0.2.
  - **背景**: GM 错 #4 — 小程拒接 P0-01 C++ stub 派单, 边界正确 ("边界即文化"首次落地). 但 R-39 风险 = sub-agent 机械拒接合理派单 (临时 IC 补位 / Sprint 紧急借调) 误伤生产力. 评分 8 = 2×4 (低 P × 中影响).
  - **建议产出**: 拒接后 escalate 路径 — sub-agent 拒接 → 自动 ping owner + GM → owner 4h 内裁决 (合理坚持 / 越界确认拒接) → 不动则上 GM 拍板.
  - **deadline**: W5 EOW.

### 3.8 小白 #44 — Active, GM 自检 framework
- **W4 产出**: llm-dev-conventions v1 已交.
- **W5 邀请 (优先级 1)**: v0.2 — **GM 自检 framework**.
  - **背景**: GM 错累计到 #6 (越级派单 / sub-agent 边界混淆 / HR registry 遗忘 etc), CLAUDE.md §7 加 4 题自检后 W4 错率降但未清零. 小白作为 LLM advisor 设计的 4 题自检 W3 一周 5 错时上呈, 现需要更系统的 framework.
  - **建议产出**: 派单前 self-audit prompt template + sub-agent 召唤前 pre-flight check (HR registry / persona 边界 / 派单 prompt 是否单 agent 替全员说话 / 是否撤旧方案).
  - **deadline**: W5 EOW.

---

## §4 W5 协调清单 (我协调, 不主管)

| 优先级 | 顾问 | 任务 | 完成形式 | deadline |
|---|---|---|---|---|
| P1 | 老高 #17 | PR review v1.1 (R-20 + R-11 + HMAC 4 反模式 grep) | docs/RESEARCH/laogao-pr-review-v1.1.md + scripts/grep_redlines.sh (CI hook) | W5 EOW |
| P1 | 老徐 #33 | R-39 sub-agent escalate 流程 v0.2 | docs/RESEARCH/laoxu-r39-escalate-v0.2.md (流程图 + owner 兜底 path) | W5 EOW |
| P1 | 小白 #44 | GM 自检 framework v0.2 | docs/RESEARCH/xiaobai-gm-self-audit-framework-v0.2.md | W5 EOW |
| P2 | 老何 #14 | cpp footgun checklist v1.1 (接 R-12/R-20/R-11 cpp 反模式) | docs/RESEARCH/laohe-cpp-footgun-checklist-v1.1.md | W5 EOW |
| P3 | 老张 #13 | 状态确认 Inactive (Standby) | 一行回执 (不需新文档) | W5 EOW |
| P3 | 老叶 #32 | 状态确认 Standby (M4.5+ 激活) | 一行回执 (不需新文档) | W5 EOW |
| — | 小邓 #31 | (保持 Active, 无新派单, M4.5+ ONNX 准备) | — | — |
| — | 老钱 #15 | (CPO 不归我协调, 与老雷直线) | — | — |

**协调机制**: 我把上述邀请 forward 给老雷, 由老雷拍板派单 (顾问直属老雷). 我做 deadline 跟进 + 回汇整合, 不做 KPI 验收.

---

## §5 跨主管协商 (我是仲裁人, ADR-004 模式)

### 5.1 当前 active
- **ADR-004 老韩 W5 patch closeout**: R-07 (liquidity) > R-08 (position_cap) 已仲裁, 老韩 W5 patch 走 RiskGateway::evaluate() 优先级排序 + 测试 case 新增 4 条 (R-07 hit vs R-08 hit 同时触发). 跟进闭环.

### 5.2 当前 pending
- **需求 vs 工程协商会**: 老胡主持, 我协助仲裁. 触发是 W4 中期 36% 超额率 (14 ticket 5 已交 5 在飞 4 待派) — 是 sprint 容量算少了 (老胡定的) 还是工程产能算多了 (老周老韩等单元 owner 算的)? 我作为协调人不替老胡定容量, 但可在协商会上把双方拉到 ADR 模式 (摆数字 vs 摆数字) 做仲裁.

### 5.3 W5 候选 ADR (我提名清单, 待老雷点选立项)

| 候选 ADR | 提案人 | 议题 | 我的初判 (待评审正式定) |
|---|---|---|---|
| **HTTP client 选型** | 老李 #07 | cpp-httplib vs cpr — Polymarket REST client v0.1 选哪个? | 倾向 **cpp-httplib** (header-only, 无 boost 依赖, 编译快), 但 cpr 异步 + 连接池更熟. 需老李 + 老何 + 老周 三方 review. |
| **VirtualMatcher 切 Mode A 灰度** | 小袁 #21 | 现 Mode A++ Bernoulli 用 fill probability, 升级 Mode A 灰度 (queue position model)? | 倾向 **W5 末再切**, 现在 paper E2E 跑通是首要, queue position model 是 M4.5+ 提精度的事. 但小袁有数据论据则可早. |
| **Pinnacle 路径** | 小梁 retro | 小梁 retro 提的 Pinnacle 接入路径 — Pinnacle no-vig 当公允价 vs 完全自建 fair value model? | 倾向 **Pinnacle no-vig 当 P0 baseline, 自建 model 当 P1 升级**. 但 ToS 合规 + 速率 + 反操纵需老叶 (已 Standby) 或老韩看一眼. 可能需要拉小邓 #31 (ML) 进会判 fair value model 难度. |

3 个候选 ADR W5 中前敲定立项排期 (W5 / W6 / W7).

---

## §6 协调人 KPI 自评

| 维度 | W4 末状态 | 目标 |
|---|---|---|
| 顾问活跃 rate | 7 active (含我) / 9 = 78% (扣老张 + 老叶 Standby) | ≥ 70%, 达标 |
| 仲裁 ADR 数 | W2 ADR-004 (R-07 vs R-08) + 本 mandate v1 = 2 | 月度 ≥ 1, 达标 |
| 红线越界拦截 | 0 (W4 末未发生需我一票否决的越界) | 0 越界, 达标 |
| 顾问 deadline miss | 0 (W4 末顾问 deadline 全 hit) | ≤ 1, 达标 |
| 跨顾问冲突 post-mortem | 0 (W4 末无顾问间冲突) | — |
| 自我评分 | **B+** (活跃度 + 仲裁达标, 但 ADR-004 仲裁 process 偏快, 没给老韩充足申辩时间, 可改进) | — |

**改进点**: 仲裁 process 加 24h 申辩窗口 (双方各出一轮文档, 我读完才出仲裁, 不在会上即兴拍).

---

## §7 与主管 + GM 关系

### 7.1 我与 5 主管平级 (不在之上, 不在之下)

- 主管处理单元内 IC 派单 + 验收 + KPI.
- 我处理跨单元仲裁 + 顾问活跃同步.
- 主管 vs 我冲突 → 走老雷拍板 (不存在主管下属我的情况, 也不存在我下属主管的情况).
- 例: 老周 v0.6 架构 review, 我作为 reviewer 提意见, 老周作为单元主管决定接不接 — 接的话进单元工程落地, 不接的话上 ADR 仲裁, 我作为仲裁人主持. ADR 仲裁结果对老周 + 我都 binding.

### 7.2 架构一票否决权 (CLAUDE.md §4 + §8)

- 任一红线违例 → 我 (或老韩 / 老黄) 任一可单独叫停, 无需二次确认.
- 否决权使用纪律: **不滥用** — 红线明确才否决, 模糊地带走 ADR 仲裁不走否决.
- 否决后必跟 ADR + post-mortem, 不可"否决了就完事".

### 7.3 与 GM 关系

- 老雷是我的上级, 我向老雷汇报.
- 但我与老雷在架构红线上是相互制约: 老雷可改业务方向 / 资源 / 招聘, 但 R-11 / R-12 / R-20 红线变更必须走 ADR 评审, 我有否决权.
- W5 我交本 mandate v1 给老雷, 等老雷签字.

---

## §8 HR 扩招建议 (顾问团)

### 8.1 短期 (W5 ~ M4.5 前): 不扩编
- 顾问团 9 人 (7 active + 2 standby) 当前够用.
- 顾问不是 critical path — IC pool 小卢 + 5 主管单元工程师才是, 顾问慢可以容忍.

### 8.2 中期 (M4.5+ 上链激活时): onchain-ops-2 候补
- **触发条件**: paper trade 达 OKR (Sharpe ≥ 0.8 + DD ≤ 10% + paper 跑 4 周稳) → 老雷决议上链 → 老叶 Standby 激活.
- **问题**: 单人扛 Polygon 上链 + 监控 + 事故响应 7×24 不够 (老叶是顾问不是 ops).
- **建议 HC**: HC 加 **onchain-ops-2 (Senior IC)** 当老叶副手, 与老叶一起填上链工程坑. 与 HC-01 老冀 (smart-contract-engineer) 不冲突 — 老冀是写合约的, onchain-ops-2 是跑 ops 的 (RPC + nonce + gas + alert).
- **deadline**: M4.5 决议前 1 个月开 HC, 老冀 + onchain-ops-2 一并入职, 老叶激活后带 2 人小组.

### 8.3 长期: AI ops 第 2 人 (若 R-39 / R-40 风险持续高)
- 老徐独人扛 sub-agent 班底纪律 + RACI + escalate 流程, 若 GM 错率持续高 (W5 ~ W8 累计 ≥ 10) 则需 ai-ops-2 副手.
- 但目前不开 HC, W5 看老徐 + 小白联合产出 (R-39 escalate + GM 自检 framework) 是否压住错率.

---

## §9 完成回执 (W5 EOW 同格式)

格式 (顾问 deadline 到时各自交):
- doc 路径
- W5 邀请映射 (老郭 §4 哪一行)
- 关键决策点 (1 段)
- 抄送 (老雷 / 老郭 / 相关主管)

我汇总后 W5 EOW 末发顾问活跃度 v2.

---

## §10 老雷签字位

- [ ] 老雷 (GM) 拍板生效
- [ ] CLAUDE.md §4 加协调人定位 (与主管平级)
- [ ] AGENT.md F 单元 (#16) 加 "协调人" 后缀
- [ ] employee-registry "职位" 列加 "Coordinator (F-only)" 与 "Manager" 区分

— 老郭, 2026-05-28 (Sprint-2 W4 Wave 21 ADR-005 配套)
