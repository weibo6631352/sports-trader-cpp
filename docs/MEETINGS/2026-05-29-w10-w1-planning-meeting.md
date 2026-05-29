---
owner: 老胡 (pm-project-manager, E-026)
last_review: 2026-05-29
type: meeting-minutes
meeting: W10 W1 Sprint-3 Planning Meeting v2
date: 2026-05-29 (W9 W5 末, 为 W10 W1 Mon 提前备案)
participants:
  - P-00 总裁 (老雷)
  - 老钱 CPO
  - 老周 (A 主管)
  - 老韩 (B 主管)
  - 小梁 (C 主管)
  - 小余 (D 主管)
  - 老胡 (E 主管, 主持)
  - 老郭 (F 协调, ADR-031 主审)
adr_cite:
  - ADR-031: Sprint plan 4 必要条件 全满足 (多人 + 状态 audit + 市场调研 + 每部门 ticket)
  - ADR-029: 本文走 worktree push + gh pr create
  - ADR-032: CI 本地优先, push 后不等远端
status: Final (W10 W1 多人讨论会 决议)
---

# W10 W1 多人讨论会纪要 — Sprint-3 W10 Plan v2

- **主持:** 老胡 (E 主管, PM)
- **日期:** 2026-05-29 (W9 W5 末, W10 W1 Mon 正式启用)
- **ADR-031 §2 4 必要条件 verify:**
  - 条件 1 (多人讨论): 总裁 + 5 主管 + 老郭 + 老钱 = **8 人到会**, 满足 ≥ 6 人硬门槛
  - 条件 2 (状态 audit): 老胡 Wave 84 audit doc 已输入 (Idle 70%, M1 66%, W9 W5 末各部门全状态)
  - 条件 3 (市场调研): 老李 Wave 85 (CLOB V2 P0) + 小段 Wave 86 (Wimbledon 窗口) + 老彭 Wave 87 (NBA Finals 信号) 三份调研已输入
  - 条件 4 (每部门 ticket): A/B/C/D/E/F + 总裁办 全覆盖 (详见 §8 决议 + sprint-03-w10-plan-v2.md)

---

## §1 会议背景与整改声明

**触发:** 老板 2026-05-29 verbatim — 老胡 Wave 82 W10 plan v1 1 人定, C/E/F 多部门无活, 违反 ADR-031 §2 4 条件。Wave 82 PR #7 降 Draft 废弃。

**整改:** 本次多人讨论会是 W10 plan v2 合法化前置。8 人到会, 5 个 input wave 已完成 (Wave 84-88), 本纪要 + sprint-03-w10-plan-v2.md 为 v2 正式决议文件。

**老胡开场:** W10 plan v1 1 人定的根本错误在于: 没有让各主管在 plan 形成前发声。今天的会议目的是 8 个人把 W10 每一个 ticket 过一遍, 确认方向正确、资源到位、没有漏派活的部门。

---

## §2 议题 1: CLOB V2 升级 P0 — 阻塞 MVP 根因

**主讲:** 老李 (输入 Wave 85 调研结论)

**老李汇报要点:**

Polymarket CLOB API 已升级 V2 (2026 年中)。主要变化:
- 订单创建接口 body schema 变更 (fee_rate_bps 字段拆分 + metadata 新增)
- 签名算法: maker_address 格式强制 checksum (EIP-55)
- WSS protocol: heartbeat 频率改为 20s (原 30s), 超时 60s (原 90s)
- CLOB V2 endpoint: `https://clob.polymarket.com` 不变, 但 `/order` POST body 有 breaking change

**阻塞分析 (老沈 + 老唐 联合发言):**

- 老孙 signer V2 ABI 对齐 → 老沈 OrderIntent transformer → 老唐 audit schema v1.4 = 3 节点串行依赖链
- 现有 codebase: signer V52 v5.3 是按 V1 spec 设计; V2 fee_rate_bps 结构变化导致签名字节串格式错误 → 所有 /order 请求 400 rejected
- W10 W1 是 P0 硬截止: 不解决 V2, W11 paper runtime 启动后所有下单全部失败

**老韩 (B 主管) 发言:** 风控层的 OrderIntent struct 已按 V1 设计, V2 升级需要同步修改 RM validate 路径里的 fee 字段 schema check。预计 RM 侧改动 3 个函数, 工作量不大, 但必须在老孙 V2 ABI spec 出来后才能改。

**老周 (A 主管) 发言:** 老孙 V2 spec 是 A 单元 W10 W1 P0 任务, 我这边已经和老孙确认, W10 W1 Tue 交付 spec, W1 Fri 前 PR 合并。老沈 W10 W2 可以用新 spec 启动 transformer 升级。老高 W10 W1 同步实施 ADR-032 pre-push hook, 确保 V2 相关 PR 本地验证全过再 push。

**老郭 (F 协调) 发言 — ADR 需求:**

V2 升级涉及跨源 schema 变更, 需要新立一个 ADR (我暂定 ADR-033: CLOB V2 ABI Migration Plan)。这个 ADR 需要: 1) 明确 breaking change 清单; 2) 每个 struct 改动的 ADR-027 C1-C4 cite 补全; 3) 迁移时间窗口。我 W10 W1 主审, 请老孙草案一出来同步给我。

**决议 (老胡记录):**

| 行动 | Owner | ETA |
|---|---|---|
| signer V2 ABI spec | 老孙 (A IC, 老周统筹) | W10 W1 Tue |
| V2 OrderIntent transformer 升级 | 老沈 (B IC, 老韩统筹) | W10 W2 |
| audit schema v1.4 V2 字段 | 老唐 (B IC) | W10 W2 |
| RM v0.5 整合 spec (V2 + R6.3 cap) | 老韩 (B 主管) | W10 W2 spec; W10 W3 实施 |
| ADR-033 CLOB V2 Migration | 老郭 (F 协调, 主审) | W10 W1 草案 |
| workflow v2 + pre-push hook 全员 | 老高 (F 顾问) | W10 W1 Mon |

**风险标记 (老胡):** R-V2: 老孙 spec 复杂度高 — W10 W2 deadline 是否够, W10 W1 Fri 老孙 + 老韩 对齐一次, 不够立刻升老周协调。

---

## §3 议题 2: ABI 修复主线收尾 (老沈 OrderIntent integration + RM v0.5 整合)

**主讲:** 老韩 + 老沈

**老沈汇报:** PositionLedger + DRAIN StateMachine W9 W4 PR #3 已合并。W10 W2 integration test 目标: PositionLedger + DRAIN StateMachine 联测 (完整状态迁移路径覆盖), ctest 增量 +6。

**老韩:** RM v0.5 整合 spec 在 W10 W2 给出 (依赖 V2 spec 先行)。spec 内容: V2 fee 字段校验链 + R6.3 per-outcome cap + Side::Sell 22 reject case。老沈 W10 W3 实施。W10 W4 整合测试全过。

**老胡确认:** 依赖顺序 — V2 spec (W10 W1) → RM v0.5 spec (W10 W2) → RM v0.5 实施 (W10 W3) → 整合测试 (W10 W4)。任何节点 delay > 2 天升老胡协调。

---

## §4 议题 3: paper runtime W11 启动地 Frankfurt

**主讲:** 老吴

**老吴汇报:** AWS Frankfurt (eu-central-1) RTT 实测结论 (Wave 85 / PR #5):
- Polymarket CLOB WSS from Frankfurt: ~18ms
- Goalserve Sofia from Frankfurt: ~12ms
- 两者均在 ADR-013 v2 阈值内 (< 30ms 可接受)

**待决:** Frankfurt 服务器购买需 AWS 账号 EC2 权限。老吴 W10 W1 先确认账号权限; 无权限立刻升老周 → 老雷 (预算审批)。

**老周:** 老吴 W10 W1 确认账号权限, 购买 EC2 c6i.2xlarge (暂定), 24h 实测后出 ADR-013 v2 final 结论, W10 W3 前 server 购买完成。

**决议:** 老吴 W10 W3 目标 = Frankfurt server SSH 连通 + base image build 成功。

---

## §5 议题 4: W11 启动期 Wimbledon 7/11-7/13 窗口

**主讲:** 小段 (输入 Wave 86 Goalserve 调研结论)

**小段汇报:**

Wimbledon 2026 决赛周: 7/11 (Fri) 女单决赛 + 7/12 (Sat) 男单决赛 + 7/13 (Sun) 混双决赛。

Goalserve inplay feed Wimbledon 覆盖:
- `/tennis/inplay` feed 已验证含 Wimbledon match_id, latency ~800ms (Sofia → Frankfurt)
- bm 字段 (开赔提供商): 6 家, Polymarket 盘口覆盖 Wimbledon 约 20% (主要男单/女单冠亚)

**paper runtime 窗口价值:** W11 paper runtime 启动 (目标 2026-07-07 Mon) 如期, 可捕获 Wimbledon 决赛周 3 天真实数据跑 paper。这是 M4.5 14 天 paper 倒计时的高价值窗口。

**风险 (老胡):** R-W11: 若 W10 任何节点 delay → W11 paper runtime 延期 → Wimbledon 窗口错失。依赖链: W10 W1 V2 spec → W10 W3 server 就绪 → W10 W4 CI green → W11 paper runtime 启动。

**小梁 (C 主管) 发言:** 我们 C 单元的 backtest framework (小蒋 W10) 需要在 paper runtime 启动前就绪, 才能在 Wimbledon 期间做离线回测验证。小蒋 W10 W2 交付 backtest cpp 框架骨架。

**决议:** Wimbledon 窗口作为 W11 paper runtime 启动的 hard deadline 输入。老胡 W10 W4 paper runtime 启动 checklist 评审是 7/11 前的最后 gate。

---

## §6 议题 5: P1-04 lineup-news-lag NBA Finals 信号 (老彭)

**主讲:** 老彭 (输入 Wave 87 信号分析)

**老彭汇报要点:**

P1-04 信号: 重要球员伤病/上阵名单 (lineup news) 相对市场赔率更新的滞后窗口。

NBA Finals 2026 历史实证:
- ESPN / Rotowire lineup news 发布 → Polymarket 赔率更新平均滞后 4.2 分钟 (样本 N=23 场)
- 5 次高价值触发 (delta > 8 tick): 平均 CLOB 可成交窗口 2.1 分钟
- V2 升级日 (CLOB V2 生效日) 前后数据需分段处理 (V2 切换导致 fee 结构变化影响 tick delta 计算)

**与 V2 升级联动:** 老彭 alpha v2.1 需要在 V2 切换日后重新校准。请老沈 V2 transformer 完成后给老彭一个 V2 数据可用的时间戳节点。

**老钱 CPO 发言:** P1-04 作为 MVP 第一个信号候选很合适 — lineup-news-lag 是人工可解释、延迟明确的 alpha。我建议老彭 W10 出 spec 时把 alpha v2.1 的夏普估计也带上, 下次策略评审 (月第 3 周四) 作为输入。

**决议:** 老彭 W10 票号 W10-T14: P1-04 lineup-news-lag NBA Finals 信号 spec + alpha v2.1 (V2 切换日数据)。

---

## §7 议题 6: G3 KR 老叶 5/31 审 + 老钱 spec v2

**主讲:** 老郭

**老郭汇报:** Wave 88 顾问意见箱第一期汇总已出 (老郭 Wave 88)。G3 (M4.5 paper 14 天 gate) 的关键验收标准 (KR) 老叶负责在 5/31 前审核完毕。

**老叶 (standby → W10 W1 激活):** 我确认 5/31 deadline。G3 KR 包含: paper runtime 14 天无崩溃 + 风控红线 0 次触发 + 每日 PnL 报表可导出。我会结合老钱 spec v2 里的产品验收口径审核是否对齐。

**老钱 CPO:** spec v2 的核心变化是把 CLOB V2 升级对产品功能的影响梳理进去 — 主要是 fee 透明度 UI 展示和 order status webhook 格式变化。W10 W2 前给出 spec v2 草案, 请老郭在 G3 KR 审前同步看一下。

**决议:** 老叶 5/31 G3 KR 审是 P0 (直接影响 paper runtime 启动条件定义)。老郭联动老钱 spec v2 在 5/31 前对齐。

---

## §8 议题 7: Idle 70% 部门激活 (C 全 / E 6 / F 5)

**主讲:** 老胡 (基于 Wave 84 audit 结论)

**老胡汇报:**

W9 W5 末 Idle 率 70% (40/57 人 Idle 或 Standby)。严重区域:

- **C 单元 4/5 Idle**: 小程/小蒋/小袁/老彭 — W9 无活
- **E 单元 6/9 Idle**: 小颖/小杜/小宋/小苏/小尤/小宫 — 其中 3 人 (小苏/小尤/小宫) 有合理前置依赖, 但小颖/小杜/小宋 无明显依赖
- **F 顾问 5/9 Idle**: 老何/老张/小邓/老徐/小白

**各主管自我陈述:**

小梁 (C 主管): C 单元 W9 全 Idle 是我的失职。ABI 修复期间我没有主动拆 W9 信号探索任务填充 C 单元。W10 我已经有 5 个 ticket 候选 (小程/小蒋/小袁/老彭各 1 个, 我自己 1 个统筹)。这次多人讨论会后我立刻派下去。

老胡 (E 主管): E 单元小颖/小杜/小宋 W9 无活也是我自己没主动填充。W10 这三人有清晰的 ticket: 小颖验收 spec v2, 小杜 PRD 信号扩展, 小宋 chaos + replay test framework。小苏等 REST API W10 W3 就绪后启动前端 UI, 小尤/小宫 W11 前做 UX 评估 + dogfood checklist。

老郭 (F 协调): F 单元顾问意见箱 W9 W1 启动但 W9 W5 末个人 deliverable 几乎 0。这不合理。W10 我给每个有能力的顾问各分配 1 个 ticket: 老何 AI/LLM gap 分析, 小邓 ML-PositionLedger 接口 brief, 老徐 工具栈升级评估, 小白 security audit framework, 老张 Rust 退场角色重定。老叶 5/31 G3 KR 审。

**总裁 (老雷) 总结发言:** 激活 Idle 部门不只是"给活干", 是确保每个人的输出对 critical path 有实质贡献。C 单元的信号研究要能在 W11 paper runtime 启动后立刻接入测试; E 单元的验收 spec/chaos test 要成为 W11 gate 检查项; F 顾问的 security audit 要在 G4 上线前完成。不是为了活而活。

**决议:** W10 全员 Idle 率目标从 70% 降至 ≤ 30% (17 Active → ≥ 40 Active)。W10 W2 老胡 follow up 各主管激活进度。

---

## §9 议题 8: 招聘 — 数据结构 IC 8/1 + 副总裁 P-01 7/1

**主讲:** 小林 (HR)

**小林汇报:**

- **数据结构 IC**: JD 2026-07 W9 W1 发布; 目前进入面试阶段 (2 名候选人); 预计 8/1 入职; 入职前 employee-registry.md 登记前置 (CLAUDE.md §7 规则 7)
- **副总裁 P-01**: JD 2026-07 W9 W1 同步发布; 1 名强候选人; 总裁 P-00 final interview 安排中; 7/1 入职目标

**老雷 (总裁):** 副总裁 P-01 final interview 我 W10 W2 前完成。8/1 数据结构 IC 入职后对接老周 (A 主管) onboarding。小林 W10 W4 给出双岗 onboarding plan。

**决议:** 小林 W10-T29; 副总裁 P-01 onboard plan W10 W4 ready (W10-T40)。

---

## §10 ADR-031 §2 条件 4 全部门 ticket 验证

| 部门 | ticket 数量 | 最低要求 | 状态 |
|---|---|---|---|
| A 系统工程 | 7 (T1-T7) | ≥ 1 | PASS |
| B 风控合规 | 3 (T8-T10) | ≥ 1 | PASS |
| C 量化研究 | 5 (T11-T15) | ≥ 1 | PASS |
| D 数据基础 | 5 (T16-T20) | ≥ 1 | PASS |
| E 产品业务 | 9 (T21-T29) | ≥ 1 | PASS |
| F 顾问团 | 9 (T30-T38) | ≥ 1 | PASS |
| 总裁办 P-00/P-01 | 2 (T39-T40) | ≥ 1 | PASS |

**ADR-031 §2 条件 4 全 PASS. W10 plan v2 有效。**

---

## §11 风险决议 (5 条)

| 风险 ID | 描述 | 级别 | 决议 | Owner |
|---|---|---|---|---|
| R-V2 | 老孙 V2 spec 复杂度 — W10 W2 deadline 是否够 | P0 | W10 W1 Fri 老孙+老韩对齐; 不够立刻升老周协调 | 老孙 + 老韩 + 老周 |
| R-FRANKFURT | AWS Frankfurt 实测后果 (Goalserve Sofia RTT 已验证, 主要风险是 AWS 账号权限/预算) | P1 | 老吴 W10 W1 确认账号权限; 无权限立刻升老周 → 老雷 | 老吴 + 老周 |
| R-W11 | Wimbledon 7/11-7/13 窗口 vs paper runtime 延期风险 | P1 | W10 W4 checklist gate 硬截止; 任一项未绿 → 老胡升老雷 | 老胡 |
| R-IC | 数据结构 IC 8/1 入职招聘进度 | P1 | 小林 每周跟进; W10 W4 ack | 小林 |
| R-IDLE | Idle 部门激活后真有产出 | P1 | W10 W2 老胡 follow up 各主管; 激活率 < 60% 升老雷 | 老胡 |

---

## §12 决议汇总

1. **CLOB V2 P0** — 老孙 W10 W1 Tue spec; 老沈 W10 W2 transformer; 老唐 W10 W2 schema v1.4; 老郭 W10 W1 ADR-033 草案
2. **ABI 收尾** — 老沈 W10 W2 integration test; 老韩 W10 W2 spec / W10 W3 实施
3. **Frankfurt** — 老吴 W10 W1 账号确认, W10 W3 server 就绪
4. **Wimbledon** — W11 paper runtime 7/7 启动是 hard deadline; W10 W4 checklist gate
5. **P1-04 信号** — 老彭 W10 spec + alpha v2.1
6. **G3 KR** — 老叶 5/31 审; 老钱 spec v2 W10 W2 草案
7. **Idle 激活** — C/E/F 三个主管 W10 W1 派完所有 ticket; W10 W2 老胡 follow up
8. **招聘** — 副总裁 W10 W2 final interview; 数据结构 IC 8/1 onboard plan W10 W4
9. **ADR-032** — 老高 W10 W1 Mon 实施 pre-push hook + workflow v2; push 后不等 CI

---

## §13 下次会议

- **W10 W4 paper runtime checklist 评审:** W10 W4 Thu (2026-07-10), 主持 老胡, 参与 老周/老韩/老吴/小冯/老高/老姜
- **W10 W5 风控例会:** W10 W5 Fri (2026-07-11), 主持 老韩
- **月末全体 Review:** 月末 (2026-07-31), 主持 老雷

---

**主持人:** 老胡 (E-026, pm-project-manager)
**记录:** 老胡
**抄送:** 全员 (通过 docs/ SSOT)

**ADR-029 + ADR-032 commit 约束:** 本文 push 后立刻汇报, 不等 CI.
