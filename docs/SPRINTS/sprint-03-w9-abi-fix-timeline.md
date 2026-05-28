---
owner: 老胡 (pm-project-manager, E-026)
last_review: 2026-05-29
sprint: Sprint-3
relates_to:
  - docs/SPRINTS/sprint-03-backlog.md (WAL-B01/B02/B03, STG-001~006)
  - docs/SPRINTS/sprint-02-w8-w5-progress.md (§3 M1 关键路径, §5 风险 registry)
  - docs/ADR/ (ADR-027 ABI enforce, ADR-028 WSS subscriber spec)
  - docs/INCIDENTS/gm-self-mistakes-log.md (GM 错 #22 ABI 漏)
---

# Sprint-3 W9 ABI 修复 Timeline

> GM 错 #22 (OrderIntent ABI 漏 token_id/outcome/Side::Sell) 修复全周排期。
> W9 = Sprint-3 第 1 周, 对应日历周 2026-06-29 (Mon) → 2026-07-03 (Fri).
> Owner: 老胡统筹, 各模块 IC 见分节.

---

## §1 背景与目标

**触发事件:** GM 错 #22 — OrderIntent ABI 漏洞 (~4 周未发现), 波及 SignerV52 ed25519 签名链路 + audit schema.

**W9 目标:**
1. ABI 修复 6 deliverable 全部 merge (老沈 / 老孙 / 小卢 / 老高 / 老郭 / 老李)
2. ctest 从 458 升至 465 (+7, 老沈主导)
3. ADR-028 WSS subscriber spec 立项 (老李)
4. W9 末 Risk Registry 更新: R-001 从"活跃"转"整改中"

---

## §2 W9 W1 — ABI Spec 落定 (已完成, 回顾)

**完成时间:** W9 W1 (Sprint Planning 周, 2026-06-29)

**参与人 (9 人):**

| 角色 | 产出 |
|---|---|
| 老韩 (B 主管) | ABI gap audit 确认, OrderIntent v0.5 spec 批准 |
| 老孙 (A IC) | SignerV52 v5.3 ABI 对齐方案确认 |
| 老高 (F 顾问) | abi_lock v1.7 CI 脚本方案确认 |
| 小卢 (A IC) | REST API skeleton endpoint list 确认 (6 endpoint) |
| 小林 (HR) | 数据结构 IC 招聘 JD W9 W1 发布确认 |
| 老吴 (A IC) | toolstack / 部署约束确认 |
| 老郭 (F 协调) | ADR-028 立项范围确认 |
| 老彭 (C IC) | alpha model 接口约束确认 |
| 老周 (A 主管) | ABI 对齐技术决策拍板 |

**Sprint Planning 议题 (§9 老胡):**
- STG-004 Stage-Gate framework 落地 (老板 verbatim 触发)
- ABI 修复排期优先于 PositionManager 实施 (老雷 + 老周 + 老韩 联决)
- WAL-B01/B02/B03 spec 确认 (老王 + 老周)
- 数据结构 IC 招聘 JD 发布 (小林)

---

## §3 W9 W2 — 6 deliverable 实施 (本周)

**日期:** 2026-06-30 (Tue) → 2026-07-01 (Wed)

### 3.1 老沈 — OrderIntent v0.5 C++ 实现

**Owner:** 老沈 (B IC)
**依赖:** W9 W1 ABI spec 落定
**内容:**
- `OrderIntent` struct 补全 `token_id` (string) + `outcome` (string) + `side` (Side::Buy | Side::Sell)
- `Side` enum 补 `Sell` variant
- RM 内部 OrderIntent validation 逻辑同步更新
- 单测: 5 新 cases (Buy/Sell 各路径 + boundary + null token_id guard)

**ctest 增量:** +7 (458 → 465 末目标 W9 W2 末贡献)
**验收:** cmake --build build && ctest 全过, ctest --output-on-failure 摘要附回汇

### 3.2 老孙 — SignerV52 v5.3 C++ ABI 对齐

**Owner:** 老孙 (A IC)
**依赖:** 老沈 OrderIntent v0.5 struct 定义 merge
**内容:**
- `signer_v52.cpp` / `signer_v52.hpp` 更新: 签名输入 payload 加入 `token_id` + `outcome`
- ed25519 签名链路 E2E 重测 (libsodium)
- 原 v5.2 单测全部保留, 新增 3 cases: token_id 为空拒签 / outcome 大小写归一 / Side::Sell 路径

**ctest 增量:** 计入 W9 W3 (老孙 +7 在 W3)
**验收:** cmake --build build && ctest 全过

### 3.3 小卢 — REST API Skeleton 6 endpoint

**Owner:** 小卢 (A IC)
**依赖:** 老周 W8 W5 后端 REST API spec v1 (laozhou-w8-debug-rest-api-spec-v1.md)
**内容:**
- cpp-httplib skeleton: 6 endpoint stub (POST /order, GET /positions, GET /markets, GET /orderbook, DELETE /order/:id, GET /health)
- 每个 endpoint: 空 JSON 200 response (业务逻辑 W9 W3 填充)
- R-12: HTTP handler 不做任何同步阻塞 IO / 锁 > 100us

**ctest 增量:** +5 (计入 W9 W3, endpoint smoke test)
**验收:** cmake --build build && ctest 全过; httpie 本地 curl smoke test 附回汇

### 3.4 老高 — abi_lock v1.7

**Owner:** 老高 (F 顾问, CI)
**依赖:** 老沈 OrderIntent v0.5 struct merge
**内容:**
- `scripts/abi_lock_v1.7.sh`: grep 检查 `token_id` / `outcome` / `Side::Sell` 在 OrderIntent 定义文件中存在
- CI 集成: CMake custom_target `abi_lock_check` (默认 ON)
- 失败时 error message: "ABI_LOCK v1.7: OrderIntent field missing — 参考 GM 错 #22 + ADR-027"

**验收:** `cmake --build build --target abi_lock_check` pass; 人工删除 token_id 后 CI 报错

### 3.5 老郭 — ADR-028 立项

**Owner:** 老郭 (F 协调人 + 架构评审)
**内容:**
- `docs/ADR/2026-07-W9-adr-028-wss-subscriber-spec.md`
- 覆盖: WSS event loop 约束 (R-12: 无同步 REST / 阻塞 IO / 锁 > 100us) + 订阅协议 + 断线重连策略
- 关联 ADR-027 (核心数据结构 SSOT) + R-12 红线

**验收:** ADR-028 文件落 docs/ADR/, 老周 first review ack

### 3.6 老李 — WSS Subscriber Spec

**Owner:** 老李 (A IC)
**依赖:** ADR-028 老郭立项 (范围已确认 W9 W1)
**内容:**
- `docs/RESEARCH/laoli-wss-subscriber-spec-v1.md`
- 覆盖: Polymarket Sports WSS 5 host 订阅协议 (R-33 四维扫描 — 含 `wss://sports-api/ws` 第 5 host)
- event 类型映射: orderbook_update / trade / market_status
- 接口草稿 (供 ADR-028 引用)

**验收:** R-33 四维扫描完成 (官方 portal / SDK 源码 / 实测 RTT / 同行 SSOT); docs 文件落位

---

## §4 W9 W3 — 联调

**日期:** 2026-07-02 (Thu)

**参与人:** 老沈 + 老孙 + 老唐 + 小卢

### 4.1 老沈 + 老孙 ABI 端到端联调

**内容:**
- OrderIntent v0.5 (老沈) → SignerV52 v5.3 (老孙) E2E: 构造 order → 签名 → 验签
- Side::Sell 路径专项测试
- 失败路径: token_id 为空 → signer 拒签 → RM 拦截

**验收判定:** 老周 + 老韩 双方 ack "联调 pass"

### 4.2 老唐 — audit schema v1.3 更新

**Owner:** 老唐 (B IC)
**依赖:** 老沈 OrderIntent v0.5 struct 定义 (W9 W2 merge)
**内容:**
- `audit_record.hpp` AuditRecord 加 `token_id` + `outcome` 字段
- BLAKE3 audit chain 对齐新字段
- 4 时间戳契约 (R-20) 不动

**ctest 增量:** +N (audit schema 新 cases, 数量由老唐确认)
**验收:** cmake --build build && ctest 全过

### 4.3 小卢 — 6 endpoint 联调

**Owner:** 小卢 (A IC)
**内容:**
- POST /order 接入 OrderIntent v0.5 struct (老沈)
- 请求解析 → RM check → signer 调用 stub (真实调用 W10)
- 4 时间戳 (R-20) 注入到 REST response header (ingestion_ts)

**ctest 增量:** +5 (endpoint smoke test, 含 Side::Sell POST /order)
**验收:** cmake --build build && ctest 全过; 老周 review pass

---

## §5 W9 W4 — ABI Lock Final + ADR-027/028 Enforce 上线

**日期:** 2026-07-03 (Fri)

**内容:**

| 项目 | Owner | 内容 |
|---|---|---|
| ABI lock final | 老高 | abi_lock v1.7 CI deploy 到 main CI pipeline; 老郭 架构 ack |
| ADR-027 enforce 确认 | 老郭 | ADR-027 §4 4 grep CI 全部绿 (worktree_commit_check / gm_merge_audit / ssot_check / abi_lock) |
| ADR-028 enforce 确认 | 老郭 | ADR-028 WSS R-12 lint rule 草案 (W10 正式 CI 集成) |
| W9 风险 review | 老胡 | R-001 状态转"整改中"; R-005 WAL-B01 进度确认; R-006 跨 worktree 写 main W9 次数归零确认 |

**W9 末 ctest 目标:**

| 时间点 | ctest 数量 | 增量来源 |
|---|---|---|
| W9 W1 (Sprint-3 基线) | 458 | W8 W5 末 |
| W9 W2 末 | 465 | 老沈 OrderIntent v0.5 +7 |
| W9 W3 末 | 480 | 老孙 SignerV52 +7 / 老唐 audit schema +N / 小卢 endpoint +5 |

---

## §6 W10 — 整合测试 + Chaos Test

**日期:** Sprint-3 第 2 周 (2026-07-06 → 2026-07-10)

**Owner:** 老周 (A 主管, 整合统筹)

**内容:**

| 测试类型 | Owner | 内容 |
|---|---|---|
| 整合测试 | 老王 + 老孙 + 老唐 | WAL-B02 Append + OrderIntent v0.5 E2E + audit chain |
| Chaos test (信号注入) | 老姜 + 老沈 | SIGTERM + ring 满 + fdatasync fail 路径覆盖 |
| RM regression | 老韩 | Side::Sell 拒单 / 通过路径全覆盖 |
| REST load test | 小卢 | 6 endpoint wrk 压测 (R-12 验证: handler < 100us) |
| WAL-B02 验收 | 老王 | 1000 笔 Append p99 < 5us (FsyncMode::GroupCommit 路径) |

**WAL-B01 W9 验收:** (sprint-03-backlog.md WAL-B01 依赖满足后触发)
- ADR-017 小石 SPSC framework 接口锁定
- 老姜 vCPU pin S2-011 latency budget 数据到位

---

## §7 W11 — Paper Runtime 启动 (Frankfurt Server)

**日期:** Sprint-3 第 3 周 (2026-07-13 → 2026-07-17)

**Owner:** 老周 (A 主管) + 老韩 (B 主管, 风控 final ack)

**前提 (全部满足才启动):**

| 前提条件 | 负责人 | 状态 |
|---|---|---|
| WAL-B01/B02 merge (W9/W10) | 老王 | 待 W9 ADR-017 就绪 |
| ABI lock final (W9 W4) | 老高 + 老郭 | 本 timeline 交付 |
| ADR-013 v2 服务器选址定案 | 老郭 + 老吴 | W9 W2 实测结论 → 老郭 评审 |
| Frankfurt server 部署 | 老吴 | 选址定案后 2 周内 |
| R-11 paper mode 隔离确认 | 老韩 + 老沈 | Paper mode 不污染真账本 |

**paper runtime 启动内容:**
- M4.5 首步: paper engine on Frankfurt server 24/7 运行
- Paper PnL tracking 启动 (M4.5 gate §1: 14 天 paper runtime 计时开始)
- 新 ctest cases: paper runtime smoke test (W11 补, 数量待定)

---

## §8 Milestone 进度追踪 (W9 维度)

| 里程碑 | W8 W5 末基线 | W9 目标 | 关键 gate |
|---|---|---|---|
| M1 MVP (2026-11) | 64% | 66% | ABI 修复 + REST skeleton 6 endpoint |
| M4.5 paper gate (2027-05) | 10% | 10% (不变) | paper runtime W11 启动前维持 |
| M5 live (2027-11) | 8% | 9% | ADR-028 WSS spec 落地 |

---

## §9 风险条目 (W9 维度)

| 风险 ID | 描述 | 级别 | W9 mitigate | Owner |
|---|---|---|---|---|
| R-001 | ABI 修复 6 deliverable 中任一 W9 W2 未完成 → W9 W3 联调 delay | P0 | 老周 每日 standup check; 阻塞升 老胡 协调 4h 内 | 老胡 (跟进) |
| R-002 | 数据结构 IC 8/1 入职 — FOM 4 人 approve 缺第 4 | P1 | 小林 JD W9 W1 发布; 招聘周期 4 周估算 | 小林 |
| R-005 | WAL-B01 依赖 ADR-017 小石 SPSC framework; 小石 W9 W2 前未就绪 → WAL-B01 delay → W11 paper runtime hold | P0 | 小石 ADR-017 接口 W9 W1 确认; 若 delay → 老周 W9 W2 升级 | 老王 (实施) + 老胡 (跟进) |
| R-006 | 跨 worktree 写 main 重犯 (W8 共 5 次) | P0 | 本 wave 派单 prompt 强 enforce; 老高 W9 W4 4 grep deploy | 老胡 (跟进) + 老高 (CI) |

---

## §10 W9 完成汇报标准 (老胡 review checklist)

老胡 W9 末确认以下 5 项全绿才关 W9:

- [ ] ctest: W9 W2 末 465+ / W9 W3 末 480+
- [ ] 6 deliverable merge PR: 老沈 / 老孙 / 小卢 / 老高 / 老郭 / 老李 各 1 PR
- [ ] 联调 ack: 老周 + 老韩 双 ack "W9 W3 联调 pass"
- [ ] R-001 状态: "活跃" → "整改中"
- [ ] 跨 worktree 写 main: W9 = 0 次

---

*老胡, 2026-05-29 (Wave 62, Sprint-3 W9 timeline 补缺)*
