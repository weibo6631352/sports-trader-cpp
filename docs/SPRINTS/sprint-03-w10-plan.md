---
owner: 老胡 (pm-project-manager, E-026)
last_review: 2026-05-29
sprint: Sprint-3
period: W10 (2026-07-06 Mon → 2026-07-10 Fri)
relates_to:
  - docs/SPRINTS/sprint-03-w9-w4-progress.md
  - docs/SPRINTS/sprint-03-backlog.md (WAL-B02, STG-006)
  - docs/SPRINTS/sprint-03-w9-abi-fix-timeline.md (§6 W10 整合测试)
  - docs/ADR/2026-06-W4-adr-029-worktree-push-pr-flow.md
---

# Sprint-3 W10 Plan

- **Owner:** 老胡 (pm-project-manager, E-026)
- **周期:** Sprint-3 W10 (2026-07-06 Mon → 2026-07-10 Fri)
- **撰写日:** 2026-05-29 (Wave 82)
- **目标:** Sprint-3 收尾 + W11 paper runtime 启动 checklist W10 W4 final ready
- **关联上一份:** `docs/SPRINTS/sprint-03-w9-w4-progress.md`

---

## §1 W10 总体目标

W10 = Sprint-3 第 2 周. 目标三层:

1. **整合层** — RM v0.5 + audit chain + REST 接真 state: ABI 全链路从 Signal → OrderIntent → SignedOrder → Audit 端到端通
2. **基础设施层** — Frankfurt server 购买 + paper runtime base image: W11 paper runtime 物理环境就绪
3. **质量层** — CI main 干净 + chaos test framework: W11 上线前质量门槛达标

**W10 W4 收关标准 (老胡 review checklist):** §7 checklist 7 项全绿 → W11 paper runtime 启动.

---

## §2 W10 Backlog

### §2.1 老韩 — RM v0.5 整合 (W10 W1)

**Owner:** 老韩 (B 主管, 统筹) + 老沈 (B IC, 实施)
**依赖:** 老沈 PositionLedger W9 merged (已满足, PR #3)
**内容:**
- OrderIntent v0.5 接入 RM 校验链: token_id + outcome 必填 validate
- R6.3 per-outcome cap: 单 outcome 持仓上限 enforce (老韩 W8 spec 已有)
- RM reject 路径 22 case 覆盖 (含 Side::Sell 拒单 / 通过路径)
- PositionLedger 读取接入 RM cap check

**ctest 增量:** +8 (RM v0.5 新路径)
**验收:**
- cmake --build build && ctest 全过 (含 RM v0.5 新 8 case)
- 老韩 RM regression: Side::Sell 拒单 / 通过路径全覆盖 ack
- ADR-029 new flow: push + gh pr create

### §2.2 老唐 — audit chain replay verify (W10 W2)

**Owner:** 老唐 (B IC)
**依赖:** 老沈 OrderIntent v0.5 (W9 W3 merged, PR #3)
**内容:**
- audit chain replay: 给定 token_id + outcome 入参, 重放 BLAKE3 chain 验证完整性
- AuditRecord v1.3 replay 路径: event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts (R-20 4 ts chain verify)
- replay tool: `tools/audit_replay_verify.cpp` 单文件小程序 (experiments/ 模式)

**ctest 增量:** +5 (replay verify cases)
**验收:**
- replay verify 对已有 audit chain (W9 test fixture) 全过
- R-20 4 ts 违反时正确 reject + 报错
- ADR-029 new flow: push + gh pr create

### §2.3 小卢 — REST API 9 endpoint 接真 state (W10 W3)

**Owner:** 小卢 (A IC, 系统工程部, 老周统筹)
**依赖:** 老韩 RM v0.5 W10 W1 merged + 老唐 audit chain W10 W2 merged
**内容:**
- 6 endpoint (W9 skeleton) 升级接真 state:
  - POST /order: OrderIntent v0.5 → RM check → signer stub → AuditRecord 写入
  - GET /positions: PositionLedger 真实读取
  - GET /orderbook: PolymarketCLOBSubscriber 当前快照
  - GET /markets: gamma REST 数据 (实测 RTT 可接受则接真, 否则 stub)
  - DELETE /order/:id: RM 撤单路径
  - GET /health: SystemState + PositionLedger 状态
- 新增 3 endpoint (W10 补全):
  - GET /audit/:token_id: audit chain 查询
  - GET /risk/caps: RM R6.3 per-outcome cap 当前值
  - POST /paper/order: paper mode 专用 (R-11 paper 隔离, 不写真账本)
- R-12: HTTP handler 全部无同步阻塞 IO / 锁 > 100us

**ctest 增量:** +9 (9 endpoint integration test)
**验收:**
- cmake --build build && ctest 全过
- wrk 压测: 9 endpoint p99 handler latency < 100us (R-12)
- ADR-029 new flow: push + gh pr create

### §2.4 小冯 — CLOB subscriber integration test + reconnect chaos (W10 W2)

**Owner:** 小冯 (D IC, 数据基础设施部, 小余统筹)
**依赖:** 小冯 PolymarketCLOBSubscriber W9 merged (PR #6)
**内容:**
- reconnect chaos test: WSS 连接中断注入, 验证自动重连 < 5s
- integration test: 真接 Polymarket CLOB (sandbox / testnet, 若可用)
- R-12 enforce: event loop 无同步 REST / 阻塞 IO / 锁 > 100us (老姜 latency perf framework 配合)
- ADR-028 WSS subscriber spec cite 入 PR body

**ctest 增量:** +4 (reconnect chaos + integration 4 case)
**验收:**
- reconnect chaos: 5 次断线注入全部 < 5s 重连
- R-12 event loop 验证 pass (老姜 perf framework)
- ADR-029 new flow: push + gh pr create

### §2.5 老吴 — Frankfurt server 购买 + paper runtime base image (W10 W3)

**Owner:** 老吴 (A IC, 系统工程部, 老周统筹)
**依赖:** AWS 3-region RTT 实测 (W9 PR #5 merged, Frankfurt 选址确认)
**内容:**
- AWS Frankfurt (eu-central-1) server 购买: EC2 instance type 按 ADR-013 v2 规格
- paper runtime base image: Ubuntu + C++20 toolchain + libsodium + cpp-httplib + libomp
- Dockerfile (base image only, 不含 app binary — app binary W11 部署)
- server 网络配置: Polymarket CLOB WSS + Goalserve inplay RTT 验证 (从 Frankfurt)

**验收:**
- Frankfurt server 可达, SSH 连通
- base image build 成功
- Polymarket WSS RTT from Frankfurt < ADR-013 v2 阈值
- ADR-029 new flow: push + gh pr create (Dockerfile + runbook)

### §2.6 老高 — CI main 干净 + chaos test framework (W10 W4)

**Owner:** 老高 (F 顾问, CI)
**依赖:** Wave 81 后 CI 状态 (老高 Wave 81 预期 CI 绿化完成)
**内容:**
- CI main 干净验证: cmake --build build && ctest 488+ PASS on CI (GitHub Actions)
- worktree_pr_check.py Rule P1/P2 落地 (ADR-029 §9.1, W9 W5 承诺)
- chaos test framework: SIGTERM + ring 满 + fdatasync fail 信号注入基础脚手架
  - 供 W11 paper runtime 启动前 chaos 验证使用
  - 实现: bash script + C++ signal injection helper

**验收:**
- CI green on main (GitHub Actions badge 绿)
- worktree_pr_check.py Rule P1 + P2 CI enforce
- chaos test framework: 3 种信号注入脚本可运行

### §2.7 老姜 — hot path latency perf framework W10 enforce (W10 W2)

**Owner:** 老姜 (A IC, 系统工程部, 老周统筹)
**内容:**
- hot path latency perf framework: 基于已有 latency budget (S2-011 vCPU pin 数据)
- W10 enforce: 对 PolymarketCLOBSubscriber event loop + REST handler 两处做基准测试
- 结论: 合规 (< 100us) / 违规 (报 P0) — 输出数字
- 报告: `docs/RESEARCH/laojian-w10-latency-enforce-v1.md`

**验收:**
- CLOBSubscriber event loop: p99 < 100us (R-12)
- REST handler p99: < 100us (R-12)
- 违规路径: 报 P0 告警 + 阻断 W11 paper runtime 启动

### §2.8 数据结构 IC 8/1 入职 onboarding (小林 W10 W4 ack)

**Owner:** 小林 (HR) + 老周 (A 主管, onboarding 技术导师)
**内容:**
- HR 注册前置: employee-registry.md 登记 (工号 + 入职日 + 单元 + 状态)
- persona file: `.claude/agents/NN-*.md` (小林签字后建)
- onboarding checklist: CLAUDE.md 必读 + AGENT.md + ADR-024/ADR-029 新流程
- W10 W4 ack: 小林确认登记完成, 老周确认技术 onboarding ready

**依赖:** 小林 JD W9 W1 发布 → 招聘周期 ~4 周 → 8/1 入职目标
**验收:** employee-registry.md 有新 IC 登记 + 小林 W10 W4 ack 回汇

---

## §3 W10 依赖链

```
W10 W1: 老韩 RM v0.5 整合
           ↓
W10 W2: 老唐 audit chain replay verify
        小冯 CLOB reconnect chaos (并行)
        老姜 latency perf enforce (并行)
           ↓
W10 W3: 小卢 REST API 9 endpoint 接真 state (依赖 W1 + W2)
        老吴 Frankfurt server 购买 + base image (并行)
           ↓
W10 W4: 老高 CI 干净 + chaos test framework (依赖 W1/W2/W3 全绿)
        小林 IC onboarding ack (并行)
```

---

## §4 ctest 目标

| 时间点 | ctest 目标 | 增量来源 |
|---|---|---|
| W10 W1 末 | 496 | 老韩/老沈 RM v0.5 +8 |
| W10 W2 末 | 510 | 老唐 replay +5 / 小冯 chaos +4 / 老姜 perf +3 |
| W10 W3 末 | 519 | 小卢 REST 9 endpoint +9 |
| W10 W4 末 | 519+ | 老高 CI 框架 (不新增 ctest, 重点是 CI green) |

---

## §5 W10 风险 Registry

| 风险 ID | 描述 | 级别 | mitigation | Owner |
|---|---|---|---|---|
| R-W10-CHAIN | 小卢 REST W10 W3 依赖 RM v0.5 + audit chain W10 W2 全就绪 — 任一延迟 → REST 推 W11 | P1 | 老韩/老唐 W10 W1/W2 硬截止; 延迟即刻升老胡 4h 协调 | 老胡 |
| R-W10-FRANKFURT | Frankfurt server AWS 账号权限 / 预算审批 delay → W11 paper runtime hold | P1 | 老吴 W10 W1 前确认账号权限; 无权限即刻升老周 → 老雷 | 老吴 + 老周 |
| R-W10-CI | Wave 81 后 CI 仍有遗留失败 → W10 W4 CI 干净目标 miss | P1 | 老高 Wave 81 CI fix 后汇报; W10 W4 CI green 是 paper runtime 前置 gate | 老高 |
| R-W10-LATENCY | 老姜 latency enforce 发现 CLOBSubscriber / REST handler > 100us → R-12 红线 → W11 hold | P0 | 老姜 W10 W2 前给初测结果; 违规立即升老周 + 老韩 | 老姜 + 老周 |
| R-W10-IC | 数据结构 IC 招聘 delay → 8/1 入职 miss | P1 | 小林 每周跟进; W10 W4 ack 是目标 | 小林 |

---

## §6 ADR-029 流程约束 (全 W10 执行)

W10 全部 PR 走 ADR-029 新流程:

1. sub-agent 自己 fetch + merge origin/main
2. sub-agent 自己 push + gh pr create
3. GM 仅 review + gh pr merge

**PR body 必含:**
- ADR-027 cite (或 N/A)
- commit hash (7 位)
- ctest 结果
- worktree branch

**例外:** CI 热修 (老高 chaos test framework) 如无 code change → ADR-024 fast merge.

---

## §7 W11 paper runtime 启动 checklist (W10 W4 final ready gate)

**老胡 W10 W4 确认以下 7 项全绿才开放 W11 paper runtime 启动:**

- [ ] ABI v0.5 全链路通: Signal → OrderIntent v0.5 → SignedOrder → AuditRecord (老韩 + 老孙 + 老唐)
- [ ] WSS subscriber 真接 CLOB + reconnect chaos pass (小冯)
- [ ] RM 22 reject case + R6.3 per-outcome cap 全 ctest pass (老韩 + 老沈)
- [ ] AWS Frankfurt server 部署 + base image ready (老吴)
- [ ] audit chain replay verify pass (老唐)
- [ ] REST API 9 endpoint 接真 state + wrk p99 < 100us (小卢 + 老姜)
- [ ] chaos test framework: 3 种信号注入脚本可运行 (老高)
- [ ] CI main 全绿 (GitHub Actions, 老高)

**任一未绿 → W11 paper runtime 不启动, 老胡升级老雷.**

---

## §8 会议安排

| 会议 | 时间 | 主持 | 参与 |
|---|---|---|---|
| W10 全体站会 | W10 W1 (Mon) | 老胡 | 全员 15min |
| 风控例会 | W10 W5 (Fri) | 老韩 | 老沈/老唐/小余 |
| W10 W4 paper runtime checklist 评审 | W10 W4 (Thu) | 老胡 | 老周/老韩/老吴/小冯/老高/老姜 |

---

*老胡, 2026-05-29 (Wave 82, Sprint-3 W10 plan)*
