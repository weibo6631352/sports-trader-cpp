---
owner: 老胡 (pm-project-manager, E-026)
last_review: 2026-05-29
sprint: Sprint-3
period: W10 (2026-07-06 Mon → 2026-07-10 Fri)
version: v2 (废弃 Wave 82 v1 — 老板批评 1 人定, ADR-031 §5 反例)
adr_cite:
  - ADR-027: N/A (本文为 PM plan doc, 无核心数据结构 ABI 改动)
  - ADR-029: worktree push + gh pr create (push 后不等 CI)
  - ADR-031: Sprint plan 4 必要条件 全满足
  - ADR-032: CI 本地优先, sub-agent push 后不等远端
status: Final (W10 W1 多人讨论会 ack, 2026-05-29)
supersedes: docs/SPRINTS/sprint-03-w10-plan.md (v1, Draft, 废弃)
---

# Sprint-3 W10 Plan v2

- **Owner:** 老胡 (pm-project-manager, E-026)
- **周期:** Sprint-3 W10 (2026-07-06 Mon → 2026-07-10 Fri)
- **撰写日:** 2026-05-29 (W9 W5 末, Wave 90)
- **v2 合法化依据:** W10 W1 多人讨论会 (8 人到会, ADR-031 §2 4 条件全满足)
- **v1 废弃声明:** sprint-03-w10-plan.md (Wave 82, 老胡 1 人定) 已降 Draft 废弃

---

## ADR-031 §2 4 必要条件 Verify

| 条件 | 满足 | 证据 |
|---|---|---|
| 1. 多人讨论 (≥ 6 人) | PASS | 总裁 + 5 主管 + 老郭 + 老钱 = 8 人, 纪要见 `docs/MEETINGS/2026-05-29-w10-w1-planning-meeting.md` |
| 2. 项目状态 audit | PASS | 老胡 Wave 84 `docs/SPRINTS/laohu-w9-w5-full-project-status-audit-v1.md` (Idle 70%, M1 66%) |
| 3. 市场调研 | PASS | 老李 Wave 85 CLOB V2 / 小段 Wave 86 Wimbledon / 老彭 Wave 87 NBA Finals — 3 份调研已输入 |
| 4. 每部门有 ticket | PASS | A 7 / B 3 / C 5 / D 5 / E 9 / F 9 / 总裁办 2 = 40 tickets (详见 §1-§8) |

---

## §1 W10 总体目标

W10 = Sprint-3 第 2 周 (最后一周). 4 层目标:

1. **P0: CLOB V2 升级** — 老孙 signer V2 spec → 老沈 transformer → 老唐 schema v1.4 链路打通; MVP 下单链路 V2 兼容
2. **整合层** — RM v0.5 + audit chain + REST 接真 state: 全链路 Signal → OrderIntent v0.5 → SignedOrder → AuditRecord 端到端通
3. **基础设施** — Frankfurt server 购买 + base image: W11 paper runtime 物理环境 W10 W3 就绪
4. **Idle 激活** — C/E/F 三个主管 W10 W1 内完成全部 ticket 派发; W10 W2 老胡 follow up 激活率

**W10 W4 paper runtime 启动 gate (老胡 W10 W4 Thu 评审):** §9 checklist 9 项全绿 → W11 paper runtime 启动。

**ctest 目标:**

| 时间点 | 目标 | 增量来源 |
|---|---|---|
| W10 W1 末 | 488 (维持) | V2 spec 阶段无新 ctest |
| W10 W2 末 | 502 | 老沈 integration test +6 / 老唐 replay +5 / 小冯 chaos +4 |
| W10 W3 末 | 520 | 小卢 REST 9 endpoint +9 / 老袁 FillRate +5 / 小梁 backtest +4 |
| W10 W4 末 | 530+ | 老韩 RM v0.5 +8 / 老高 CI framework / 各 IC 增量 |

---

## §2 A 单元 — 系统工程部 (老周主管, 7 tickets)

### W10-T1: 老孙 — signer V2 ABI spec (P0)

**Owner:** 老孙 (A IC, 老周统筹)
**Priority:** P0 (CLOB V2 升级阻塞 MVP)
**ETA:** W10 W1 Tue

**内容:**
- CLOB V2 breaking changes 梳理: fee_rate_bps 拆分 / maker_address EIP-55 checksum / heartbeat 20s
- SignerV52 v5.3 → V6 ABI spec: 签名字节串格式适配 V2 fee 结构
- ADR-033 草案输入 (老郭主审)

**验收:**
- V2 ABI spec doc 交付 (docs/RESEARCH/laosun-signerv6-abi-spec-v1.md)
- 老郭 + 老韩 ack
- ADR-029 new flow: push + gh pr create (push 后不等 CI)

---

### W10-T2: 老沈 — V2 OrderIntent → SignedOrder transformer 升级

**Owner:** 老沈 (B IC 兼 A/B 联动, 老韩统筹)
**Priority:** P0
**依赖:** W10-T1 老孙 V2 spec W10 W1 Tue
**ETA:** W10 W2

**内容:**
- OrderIntent struct V2 兼容: fee_rate_bps 新字段接入 + maker_address checksum
- transformer: OrderIntent v0.5 → SignedOrder V2 格式适配
- PositionLedger + DRAIN StateMachine integration test (W10 W2 目标): ctest +6

**验收:**
- cmake --build build && ctest 502+ PASS
- V2 transformer 单测 全覆盖
- ADR-029 flow: push + gh pr create

---

### W10-T3: 老唐 — audit schema v1.4 V2 字段 (timestamp/metadata)

**Owner:** 老唐 (B IC)
**Priority:** P0
**依赖:** W10-T1 老孙 V2 spec + W10-T2 老沈 transformer
**ETA:** W10 W2

**内容:**
- AuditRecord v1.4: V2 fee_rate_bps + metadata 字段入 audit schema
- R-20 4 ts chain (event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts) 在 V2 字段中验证
- audit chain replay verify tool: `tools/audit_replay_verify.cpp` (experiments/ 模式)
- ctest +5 (replay verify cases)

**验收:**
- replay verify 对已有 audit chain 全过
- R-20 违反时正确 reject + 报错
- ADR-029 flow: push + gh pr create

---

### W10-T4: 老高 — workflow v2 deploy + pre-push hook 全员 onboard

**Owner:** 老高 (F 顾问, 老郭协调)
**Priority:** P0 (ADR-032 W10 W1 实施)
**ETA:** W10 W1 Mon (本 wave 实施, Wave 89 已落 ADR-032 草案)

**内容 (ADR-032 策略 1+2+3):**
- `.git/hooks/pre-push` 安装: cmake build + ctest + clang-format + 5 Python grep (< 30s 本地全过)
- `.github/workflows/pr.yml` v2: 精简为 5 Linux-specific job (移除 17 Python grep job)
- 全员 onboard doc: `docs/META/laogao-w10-pre-push-onboard.md`
- worktree_pr_check.py Rule P1/P2 落地 (ADR-029 §9.1)

**验收:**
- pre-push hook 在 Mac + worktree 环境测试通过
- pr.yml v2 CI 时间 < 25s (从 30-60s 降至)
- 全员 onboard doc 发布

---

### W10-T5: 老吴 — AWS Frankfurt 实测 24h → ADR-013 v2 final → 购买

**Owner:** 老吴 (A IC, 老周统筹)
**Priority:** P1
**ETA:** W10 W1 账号确认; W10 W3 server 购买 + base image

**内容:**
- W10 W1: AWS 账号 EC2 权限确认 (无权限立刻升老周 → 老雷)
- W10 W2: Frankfurt EC2 c6i.2xlarge 购买 + 24h RTT 实测 (Polymarket CLOB + Goalserve Sofia)
- W10 W3: ADR-013 v2 final doc + base image (Ubuntu 24.04 + C++20 toolchain + libsodium + cpp-httplib)
- paper runtime base image: Dockerfile (base image only, app binary W11 部署)

**验收:**
- Frankfurt server SSH 连通
- RTT 实测数据在 ADR-013 v2 final 中 (Polymarket WSS < 30ms, Goalserve < 30ms)
- base image build 成功
- ADR-029 flow: push + gh pr create

---

### W10-T6: 老姜 — perf framework hot path latency W10 enforce

**Owner:** 老姜 (A IC, 老周统筹)
**Priority:** P1
**ETA:** W10 W2

**内容:**
- CLOBSubscriber event loop p99 latency 基准 (R-12: < 100us)
- REST handler p99 latency 基准 (R-12: < 100us)
- 报告: `docs/RESEARCH/laojian-w10-latency-enforce-v1.md`
- 违规: 立即升老周 + 老韩 (P0 阻断 W11 paper runtime)

**验收:**
- 两处 p99 < 100us 数字化输出
- 合规: 通过; 违规: P0 incident + 阻断 W11

---

### W10-T7: 老周 — Sprint-4 信号探索统筹 + Sprint-3 收尾

**Owner:** 老周 (A 主管)
**Priority:** P2
**ETA:** W10 W4

**内容:**
- A 单元 W10 收尾确认: T1-T6 全部 PR merged
- Sprint-4 方向输入 (系统工程侧): 多 sport 盘口 ABI 扩展优先级
- A 单元 W10 W4 汇报: ctest 增量 + Idle 人员 (小马/老陈/小赵/小郑/小肖/小颜/9 卢) W11 派活建议

---

## §3 B 单元 — 风控合规部 (老韩主管, 3 tickets)

### W10-T8: 老沈 — PositionLedger + DRAIN StateMachine integration test

**Owner:** 老沈 (B IC, 老韩统筹)
**Priority:** P1
**ETA:** W10 W2

**内容:**
- PositionLedger + DRAIN StateMachine 联测: 完整状态迁移路径覆盖
- ctest +6 (状态机迁移 + boundary 条件)
- 依赖: W10-T2 V2 transformer 完成后同批测试

**验收:**
- cmake --build build && ctest 全过 (+6 新 case)
- DRAIN StateMachine: IDLE → ACTIVE → DRAIN → CLOSED 全路径覆盖
- ADR-029 flow: push + gh pr create

---

### W10-T9: 老韩 — RM v0.5 整合 spec (V2 + R6.3 cap + per-outcome)

**Owner:** 老韩 (B 主管)
**Priority:** P0
**依赖:** W10-T1 老孙 V2 spec W10 W1 Tue
**ETA:** W10 W2 spec; W10 W3 实施

**内容:**
- RM v0.5 整合 spec: V2 fee 字段校验链 + R6.3 per-outcome cap + Side::Sell 22 reject case
- 与老沈 W10 W3 联合实施
- ctest 增量: +8 (RM v0.5 新路径 + Side::Sell 拒单/通过路径)

**验收:**
- spec doc: `docs/RESEARCH/laohan-rm-v05-spec-v1.md`
- RM 22 reject case 全 ctest pass
- ADR-029 flow: push + gh pr create

---

### W10-T10: 老黄 — 风控 chaos test framework

**Owner:** 老黄 (B IC)
**Priority:** P1
**ETA:** W10 W3

**内容:**
- 风控 chaos test: RM reject 路径在异常注入 (并发 + 网络分区) 下的行为验证
- framework 骨架: bash script + C++ signal injection helper
- 供 W11 paper runtime 启动前 chaos 验证使用

**验收:**
- 3 种风控异常注入脚本可运行
- ctest +3 (chaos case)
- ADR-029 flow: push + gh pr create

---

## §4 C 单元 — 量化研究部 (小梁主管, 5 tickets)

> **背景:** C 单元 W9 W2-W4 全 4 IC Idle — 老板批评重点区域。W10 全部激活。

### W10-T11: 小程 — P0-02 spec v0.2 + P0-03 跨平台套利 spec

**Owner:** 小程 (C IC, 小梁统筹)
**Priority:** P0
**ETA:** W10 W2

**内容:**
- P0-02 alpha v2: 6¢ 价差信号 + C2 流动性层 + 25% 死区 (W9 Idle 期间 backlog)
- P0-03 新 spec: Polymarket vs 竞品盘口跨平台套利信号初版

**验收:**
- spec doc: `docs/RESEARCH/xiaocheng-p002-spec-v02.md` + `docs/RESEARCH/xiaocheng-p003-crossplatform-spec-v01.md`
- 老钱 CPO + 老韩 ack (alpha 合规性)

---

### W10-T12: 小蒋 — backtest framework cpp 实施 (paper runtime 配套)

**Owner:** 小蒋 (C IC, 小梁统筹)
**Priority:** P1
**ETA:** W10 W2

**内容:**
- backtest framework cpp v0.3: 接 W8 skeleton, 实现 fill simulation (VirtualMatcher Mode A)
- paper runtime 配套: 同一 binary 支持 paper mode 运行 (R-11 paper 隔离)
- ctest +4

**验收:**
- cmake --build build && ctest (+4)
- 一个 mini backtest (P0-02 信号 + 5 场历史数据) 可跑通
- ADR-029 flow: push + gh pr create

---

### W10-T13: 小袁 — microstructure FillRateModel cpp 完整

**Owner:** 小袁 (C IC, 小梁统筹)
**Priority:** P1
**ETA:** W10 W3

**内容:**
- FillRateModel v0.2: v0.1 基础上补全 Mode A (VirtualMatcher 接入)
- OrderBook depth 参数化: top-5 level 加权成交率估算
- ctest +5

**验收:**
- FillRateModel 单测全过 ctest (+5)
- backtest framework (小蒋 T12) 可调用 FillRateModel
- ADR-029 flow: push + gh pr create

---

### W10-T14: 老彭 — P1-04 lineup-news-lag NBA Finals 信号 spec + alpha v2.1

**Owner:** 老彭 (C IC, 小梁统筹)
**Priority:** P1
**ETA:** W10 W3

**内容:**
- P1-04 spec: lineup-news-lag 信号定义 + ESPN/Rotowire 数据源延迟分析
- NBA Finals 历史实证: N=23 场, 平均滞后 4.2min, 高价值触发 5 次
- alpha v2.1: V2 切换日前后数据分段校准 (依赖老沈 V2 transformer 完成节点 ts)
- 夏普估计输入 (月第 3 周四策略评审材料)

**验收:**
- spec doc: `docs/RESEARCH/laopeng-p104-lineup-lag-spec-v01.md`
- alpha v2.1 在 V2 切换日后数据上可跑通
- 老钱 CPO ack

---

### W10-T15: 小梁 — Sprint-4 信号探索统筹 + Soccer 扩展 (老钱顾问)

**Owner:** 小梁 (C 主管)
**Priority:** P2
**ETA:** W10 W4

**内容:**
- C 单元 W10 收尾确认: T11-T14 全部交付
- Sprint-4 信号探索方向初版: Soccer 3-way / Tennis Moneyline / NBA props
- 老钱 CPO Soccer 优先级顾问意见 (W10-T34) 作为输入

---

## §5 D 单元 — 数据基础设施部 (小余主管, 5 tickets)

### W10-T16: 小段 — bm 字段全 sport audit + 跨源 mapping 精度

**Owner:** 小段 (D IC, 小余统筹)
**Priority:** P1
**ETA:** W10 W2

**内容:**
- bm (betting market) 字段在 Goalserve inplay feed 里所有 sport 的覆盖率 audit
- Polymarket token_id ↔ Goalserve match_id 跨源 mapping 精度: Wimbledon + NBA 专项
- 报告: `docs/RESEARCH/xiaoduan-bm-audit-w10-v1.md`

**验收:**
- 覆盖率数字化 (各 sport bm 字段 % 有效)
- Wimbledon mapping 精度 ≥ 95%
- ADR-029 flow: push + gh pr create

---

### W10-T17: 小冯 — inplay client cpp 实施 (HTTP 1s poll, gzip, SPSC ring)

**Owner:** 小冯 (D IC, 小余统筹)
**Priority:** P1
**依赖:** W9 PolymarketCLOBSubscriber (PR #6 merged)
**ETA:** W10 W2

**内容:**
- GoalserveInplayClient cpp: HTTP 1s poll (非 WSS), gzip 解压, SPSC ring buffer 推送
- R-12 enforce: event loop 无同步 REST / 锁 > 100us
- reconnect chaos test: 5 次断线注入全部 < 5s 重连
- ctest +4

**验收:**
- cmake --build build && ctest (+4)
- R-12 event loop 验证 pass (老姜 perf framework)
- ADR-029 flow: push + gh pr create

---

### W10-T18: 小余 — ETL 跨源索引 cpp + gameId fallback

**Owner:** 小余 (D 主管)
**Priority:** P1
**ETA:** W10 W3

**内容:**
- ETL 跨源索引 cpp: Polymarket token_id + Goalserve match_id + gameId fallback 逻辑
- 覆盖 Tennis (Wimbledon) + Basketball (NBA) + Soccer (扩展准备)
- ADR-027 cite: 如涉及核心 struct 改动需补 C1-C4

**验收:**
- ETL 跨源查询 benchmark: p99 < 1ms
- gameId fallback 逻辑 ctest +3
- ADR-029 flow: push + gh pr create

---

### W10-T19: 小董 — stats validation framework W10

**Owner:** 小董 (D IC, 小余统筹)
**Priority:** P1
**ETA:** W10 W3

**内容:**
- stats validation framework: 对 Goalserve inplay 统计字段 (score/possession/shots) 的一致性检查
- A/B test 框架骨架: 供 paper runtime W11 启动后双策略比较使用
- ctest +4

**验收:**
- validation framework 对 W9 历史 feed 数据可跑通
- A/B test 框架骨架文档化
- ADR-029 flow: push + gh pr create

---

### W10-T20: 小田 #24 — ML data pipeline W10

**Owner:** 小田 #24 (D IC, 小余统筹)
**Priority:** P2
**ETA:** W10 W4

**内容:**
- ML data pipeline W10: Parquet 分区 + 特征工程 pipeline 接 小邓 ML hook (W10-T35)
- 产出: Parquet 文件格式 + DuckDB query 验证

**验收:**
- W9 inplay 数据 Parquet 化完成
- DuckDB 跨表 join 查询 < 500ms
- 产出归档 (非生产, experiments/ 模式)

---

## §6 E 单元 — 产品业务保障部 (老胡主管, 9 tickets)

> **背景:** E 单元 W9 Idle 6/9 — 小颖/小杜/小宋 无明显依赖但未被派活, 老胡自检失职。W10 全部补活。

### W10-T21: 老胡 — PM 周报 + Sprint-3 收尾 + Sprint-4 prep

**Owner:** 老胡 (E 主管)
**Priority:** P1
**ETA:** 持续 (W10 W1 + W4 关键节点)

**内容:**
- W10 W1: 本 plan v2 push + 多人讨论会纪要 (本 wave 已完成)
- W10 W2: Idle 激活 follow up + risk registry 更新
- W10 W4: Sprint-3 收尾确认 + Sprint-4 kick-off 材料准备
- 周报 §6: Opus 使用次数监控 (ADR-009 v2, 目标 < 5% / sprint)

---

### W10-T22: 小颖 — 验收 spec v2 (M1 38 条 update, V2 升级 fee 重构影响)

**Owner:** 小颖 (E IC, 老胡统筹)
**Priority:** P0
**ETA:** W10 W2

**内容:**
- M1 38 条验收标准 v2: V2 升级对 fee 字段的影响 (接受 / 拒单 条件更新)
- 老钱 spec v2 (W10-T34 输入) 对齐
- 输出: `docs/SPRINTS/xiaoying-m1-acceptance-spec-v2.md`

**验收:**
- 38 条全部逐一 check: 受 V2 影响的条款标记 + 修订
- 老韩 ack (风控侧合规)
- 老钱 CPO ack (产品侧)

---

### W10-T23: 小杜 — PRD 信号扩展 (Soccer/Tennis V2 升级 backend)

**Owner:** 小杜 (E IC, 老胡统筹)
**Priority:** P1
**ETA:** W10 W3

**内容:**
- PRD 信号扩展: Soccer 3-way Moneyline + Tennis Moneyline 盘口规格
- V2 升级 backend 影响: 新 sport 盘口 fee 结构 + settlement 规则
- 老钱 CPO 顾问意见 (W10-T34) 作为输入

**验收:**
- PRD doc: `docs/SPRINTS/xiaodu-prd-signal-expansion-soccer-tennis-v1.md`
- 老钱 CPO ack

---

### W10-T24: 小宋 — chaos + replay test framework W10

**Owner:** 小宋 (E IC, 老胡统筹)
**Priority:** P0
**ETA:** W10 W3

**内容:**
- chaos test framework: SIGTERM + ring 满 + fdatasync fail 信号注入基础脚手架
- replay test: 给定历史 feed, 回放完整 Signal → OrderIntent → SignedOrder → AuditRecord 链路
- 供 W11 paper runtime 启动前 chaos 验证使用
- ctest +3 (chaos case)

**验收:**
- 3 种信号注入脚本可运行
- replay test 对 W9 历史数据跑通
- ADR-029 flow: push + gh pr create

---

### W10-T25: 小苏 — 前端 UI v1 启动 (M1-H PnL 看板 0/8 重灾区)

**Owner:** 小苏 (E IC, 老胡统筹)
**Priority:** P1
**依赖:** REST API 9 endpoint (小卢 W10 W3)
**ETA:** W10 W4 启动 (接 REST API)

**内容:**
- PnL 看板 UI v1: 接 REST API 9 endpoint (GET /positions + GET /orderbook + GET /markets + GET /pnl)
- M1-H 8 条 PnL 指标初版展示 (W10 W4 开始, W11 完成)
- 技术栈: 老钱 + 小尤 确认 (轻量 web, 不引入 React 全家桶 — 讨论结论: vanilla JS + cpp-httplib 静态文件)

**验收:**
- W10 W4: 前端 UI 框架启动, 3 个 endpoint 可显示
- W11 W2: M1-H 8 条全覆盖 (W11 ticket)

---

### W10-T26: 小尤 — UX dogfood spec + 体验评估

**Owner:** 小尤 (E IC, 老胡统筹)
**Priority:** P2
**ETA:** W10 W3

**内容:**
- UX 评估计划: paper runtime W11 dogfood 期间的用户体验指标
- dogfood spec: 观察维度 (PnL 看板可读性 / 告警可操作性 / 风控红线可追溯性)
- 输出: `docs/SPRINTS/xiaoyou-ux-dogfood-spec-v1.md`

---

### W10-T27: 小宫 — dogfood W11 paper 启动后真用

**Owner:** 小宫 (E IC, 老胡统筹)
**Priority:** P2
**依赖:** W11 paper runtime 启动
**ETA:** W11 W1 启动 (W10 做 checklist 准备)

**内容:**
- W10: dogfood checklist 准备 (观察哪些指标, 报告给谁, 多久报一次)
- W11: paper runtime 真实 dogfood 启动
- 输出 W10: `docs/SPRINTS/xiaogong-dogfood-w11-checklist-v1.md`

---

### W10-T28: 小米 — docs INDEX.md frontmatter 各 owner 派回执行

**Owner:** 小米 (E IC, 老胡统筹)
**Priority:** P2
**ETA:** W10 W2

**内容:**
- W9 W3 docs frontmatter audit 后续: 所有 owner 确认 frontmatter 格式合规
- INDEX.md 更新: W10 新增文档 (本 plan v2 + 会议纪要 + 各 spec) 入索引
- ADR-028 enforce: 新建 md 必有 frontmatter (CI `docs_frontmatter_check.py`)

**验收:**
- INDEX.md W10 新文档全入索引
- 无 frontmatter 缺失告警

---

### W10-T29: 小林 HR — 数据结构 IC + 副总裁 P-01 招聘进度 + onboard plan

**Owner:** 小林 (E IC, 老胡统筹)
**Priority:** P1
**ETA:** W10 W4

**内容:**
- 数据结构 IC: 面试进度跟进 (2 名候选人); 8/1 入职 onboard plan 起草
- 副总裁 P-01: 7/1 入职目标; 老雷 W10 W2 final interview 后 offer 决策
- 所有新入职: employee-registry.md 登记前置 (CLAUDE.md §7 规则 7)
- W10 W4: 双岗 onboard plan doc

**验收:**
- 副总裁 P-01: W10 W2 final interview done + W10 W3 offer 决策
- 数据结构 IC: W10 W4 招聘进度 ack
- onboard plan doc: `docs/HIRING/onboard-plan-vp-p01-datastructure-ic-w10.md`

---

## §7 F 单元 — 顾问团 (老郭协调, 9 tickets)

> **背景:** F 顾问 W9 W5 末 5/9 Idle (老何/老张/小邓/老徐/小白)。W10 全部激活。

### W10-T30: 老郭 — ADR 主审 + 跨源 schema ADR 新 + 顾问团周报

**Owner:** 老郭 (F 协调)
**Priority:** P0
**ETA:** W10 W1-W4 持续

**内容:**
- ADR-031 (Sprint plan 制度): W10 W1 正式 ACCEPTED
- ADR-033 (CLOB V2 Migration): W10 W1 草案 + 主审
- 跨源 schema ADR 新建 (ADR-034 候选): Polymarket token_id + Goalserve match_id SSOT
- 顾问团周报: W10 W4 汇总 9 顾问 W10 deliverable

---

### W10-T31: 老叶 — G3 KR 审 (5/31 deadline, P0)

**Owner:** 老叶 (F 顾问, 老郭协调)
**Priority:** P0
**ETA:** 5/31 (W9 W5 末截止, 本 wave 确认)

**内容:**
- G3 KR 审核: paper runtime 14 天无崩溃 + 风控红线 0 次触发 + 每日 PnL 报表可导出
- 与老钱 spec v2 对齐 (老钱 W10 W2 草案前)
- 输出: `docs/ADR/老叶-g3-kr-review-2026-05-31.md`

**验收:**
- 5/31 前 doc 交付
- 老郭 + 老钱 ack

---

### W10-T32: 老何 AI/LLM — AI 在 signal pipeline gap 分析

**Owner:** 老何 (F 顾问, 老郭协调)
**Priority:** P1
**ETA:** W10 W3

**内容:**
- Signal pipeline 当前 gap: lineup-news → alpha 信号链路哪些环节可以用 AI/LLM 加速
- PoC 建议: 哪个 gap 优先尝试 (不引入 Python 生产, 离线分析/ONNX 形式)
- 输出: `docs/RESEARCH/laohe-ai-signal-pipeline-gap-v1.md`

---

### W10-T33: 小白 security — security audit framework W11 paper 启动前

**Owner:** 小白 (F 顾问, 老郭协调)
**Priority:** P1
**ETA:** W10 W4 (W11 paper 启动前必关闭)

**内容:**
- G4 上线前 security gap 清单 (W11 paper 先行版): 私钥管理 / API key 存储 / 日志脱敏
- CLAUDE.md §8 红线 verify: 私钥明文落盘 / 日志 → 0 容忍审查
- 输出: `docs/RESEARCH/xiaobai-security-audit-w11-pre-launch-v1.md`

**验收:**
- gap 清单 doc 交付
- 红线 0 violation 确认 (或 violation 标红 + mitigation)

---

### W10-T34: 老钱 顾问 — Soccer sport 扩展优先级 spec (Sprint-4 prep)

**Owner:** 老钱 (CPO, F 列席)
**Priority:** P1
**ETA:** W10 W2

**内容:**
- Soccer 3-way Moneyline 盘口规格: Polymarket soccer_moneyline 合约结构
- Sprint-4 Soccer 信号优先级输入: 市场容量 / alpha 可行性 / 数据源覆盖
- 输入给: 小梁 T15 / 小杜 T23 / 小段 T16

---

### W10-T35: 小邓 ML — ML → PositionLedger/StateMachine 接口 brief

**Owner:** 小邓 (F 顾问, 老郭协调)
**Priority:** P1
**ETA:** W10 W3

**内容:**
- ML 模型输出 → PositionLedger 接入接口: ONNX 推理结果如何送入 C++ 决策链
- StateMachine 兼容性 brief: ML 信号 vs 规则信号的优先级冲突处理
- 输出: `docs/RESEARCH/xiaodeng-ml-positionledger-interface-brief-v1.md`

---

### W10-T36: 老张 — Rust 退场角色重定 doc (Sprint-4 重评)

**Owner:** 老张 (F 顾问, 老郭协调)
**Priority:** P2
**ETA:** W10 W4

**内容:**
- Rust 退场决议 (GM 2026-05-28 最终版) 后老张角色重定: C++20 架构兼容性独立审查顾问
- doc: 老张 W10 起承担哪些 C++ 架构 review 职责 (接替 Rust 顾问角色)
- 输出: `docs/RESEARCH/laoz hang-role-redef-w10-v1.md`

---

### W10-T37: 老徐 — 工具栈升级 DuckDB/Parquet W10 W4 评估

**Owner:** 老徐 (F 顾问, 老郭协调)
**Priority:** P2
**ETA:** W10 W4

**内容:**
- DuckDB v1.x vs 现有版本: ETL 查询性能对比 (配合小余 T18 + 小田 T20)
- Parquet: 分区策略升级建议 (按 date + sport + token_id 三级分区)
- 输出: `docs/RESEARCH/laoxu-toolstack-upgrade-duckdb-parquet-w10-v1.md`

---

### W10-T38: 老高 — CI 持续优化 (Wave 89 后续 monitoring W10 W2)

**Owner:** 老高 (F 顾问)
**Priority:** P1
**ETA:** W10 W2

**内容:**
- ADR-032 实施后监控: CI 时间 / iteration 次数 / sub-agent wall time 数据收集
- W10 W2 中期数据报告: 目标 CI 时间 < 25s + sub-agent iteration < 1.5 次/wave
- 红线 grep 漏跑检查: pre-push hook 覆盖率验证

---

## §8 总裁办公室

### W10-T39: 总裁 P-00 — PR review + ADR 重大决议 + 副总裁招聘 final

**Owner:** 老雷 (总裁 P-00)
**Priority:** P0
**ETA:** W10 持续

**内容:**
- W10 PR review: 所有 P0 PR (V2 ABI spec / RM v0.5 / Frankfurt) GM review + gh pr merge
- ADR-031 (Sprint plan 制度): W10 W1 ack
- ADR-033 (CLOB V2 Migration): W10 W1 草案后 GM 拍板
- 副总裁 P-01 final interview: W10 W2 前完成

---

### W10-T40: 副总裁 P-01 (7/1 入职) — onboard plan W10 W4 ready

**Owner:** 小林 (HR) + 老雷 (总裁)
**Priority:** P1
**ETA:** W10 W4

**内容:**
- 入职 onboard plan (技术 + 管理): CLAUDE.md 必读 + AGENT.md + ADR 速览 + 5 主管 1:1
- employee-registry.md 登记 (前置, CLAUDE.md §7 规则 7)
- persona file `.claude/agents/NN-*.md` (小林签字后建)

---

## §9 W11 paper runtime 启动 checklist (W10 W4 Thu gate)

老胡 W10 W4 Thu 评审以下 9 项 — 全绿才开放 W11 paper runtime 启动:

- [ ] CLOB V2 ABI 全链路通: signer V6 → OrderIntent v0.5 V2 → SignedOrder V2 → AuditRecord v1.4 (老孙 + 老沈 + 老唐)
- [ ] WSS subscriber 真接 CLOB + reconnect chaos pass (小冯)
- [ ] RM v0.5 22 reject case + R6.3 per-outcome cap 全 ctest pass (老韩 + 老沈)
- [ ] AWS Frankfurt server SSH 连通 + base image ready (老吴)
- [ ] audit chain replay verify pass (老唐)
- [ ] REST API 9 endpoint 接真 state + wrk p99 < 100us (小卢 + 老姜)
- [ ] chaos + replay test framework 3 种信号注入可运行 (老高 + 小宋)
- [ ] CI main 全绿 (GitHub Actions, 老高 pre-push hook + workflow v2 实施后)
- [ ] security audit gap 清单: 红线 0 violation (小白)

**任一未绿 → W11 paper runtime 不启动, 老胡立刻升老雷.**

---

## §10 W10 风险 Registry

| 风险 ID | 描述 | 级别 | mitigation | Owner |
|---|---|---|---|---|
| R-V2 | 老孙 V2 spec 复杂度 — W10 W2 deadline 是否够 | P0 | W10 W1 Fri 老孙+老韩对齐; 不够立刻升老周; 老周 → 老胡 4h 协调 | 老孙 + 老韩 + 老周 |
| R-FRANKFURT | AWS 账号权限/预算 delay → W11 paper runtime hold | P1 | 老吴 W10 W1 确认账号; 无权限立刻升老周 → 老雷 | 老吴 + 老周 |
| R-W11 | Wimbledon 7/11-7/13 窗口 vs paper runtime 延期 | P1 | W10 W4 checklist gate 硬截止; 任一项未绿 → 老胡升老雷 | 老胡 |
| R-IC | 数据结构 IC 8/1 招聘进度 miss | P1 | 小林 每周跟进; W10 W4 ack; delay → 升老雷 | 小林 |
| R-IDLE | Idle 激活后真有产出 (W10 W2 follow up) | P1 | W10 W2 老胡各主管 follow up; 激活率 < 60% 升老雷 | 老胡 |

---

## §11 ADR-029 + ADR-032 流程约束 (全 W10 执行)

1. 所有 PR 走 ADR-029: sub-agent 自己 fetch + merge + push + gh pr create; GM 仅 review + gh pr merge
2. ADR-032: push 后不等 CI; 本地 pre-push hook (老高 W10 W1 实施) 是主要裁判
3. PR body 必含: ADR-027 cite (或 N/A) + commit hash (7 位) + ctest 结果 + worktree branch
4. sub-agent 回汇必含: commit hash + PR URL + ctest 状态 (ADR-029 §3.1 Step 8)

---

## §12 会议安排

| 会议 | 时间 | 主持 | 参与 |
|---|---|---|---|
| W10 全体站会 | W10 W1 Mon | 老胡 | 全员 15min |
| 主管周同步 | W10 W1 Mon 上午 | 老雷 | 5 主管 + 老郭 + 老钱 + 小林 |
| 风控例会 | W10 W5 Fri | 老韩 | 老沈/老唐/小余 |
| W10 W4 paper runtime checklist 评审 | W10 W4 Thu (2026-07-10) | 老胡 | 老周/老韩/老吴/小冯/老高/老姜/小白 |

---

*老胡 (E-026, pm-project-manager), 2026-05-29 (Wave 90, Sprint-3 W10 plan v2)*
*ADR-031 §2 4 必要条件 全满足. v1 (Wave 82, Draft) 废弃.*
