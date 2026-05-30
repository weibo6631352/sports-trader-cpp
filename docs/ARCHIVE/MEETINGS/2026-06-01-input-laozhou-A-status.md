# 6/01 主管周同步 input — 老周 A 系统工程部 status v1

- **Owner:** 老周 (E-001, A 系统工程部主管)
- **Date:** 2026-05-28 (W5 末; 6/01 周同步前置 input)
- **Last review:** 2026-05-28
- **触发:** GM 老雷 6/01 主管周同步 (ADR-005 主管 mandate 试点 W5 → 正式 W6)
- **权威输入:** mandate v1 §4 W5 24 子任务 + commit `3ab5dfb` (W5 Wave 24) + mandate v1 §5 跨主管 5 ASK
- **完成汇报锚:** A 单元 W5 末 status + 2 v0.1 first review PASS + 5 跨主管 ASK 进度 + W6 lib 选型决议入口

---

## §1 A 单元 W5 末 status

| Ticket | IC | 状态 | 备注 |
|---|---|---|---|
| W5-A-01 polymarket-client cpp | 老李 | ✓ commit `3ab5dfb` | 1050 行 / 14 接口 / 28 测试 / R-7 物理隔离 |
| W5-A-02 PM WSS subscriber | 小冯 | ✓ commit `3ab5dfb` | 1044 行 / 8 sub topic / 8 测试 / R-12 enforce |
| W5-A-03 SPSC ring 5 capacity | 小石 | TBD W5 末启动 | 老李 + 小冯 已留 ISpscEventSink 接口 |
| W5-A-04 e2e smoke | 老李 + 老吴 | TBD W5 末 | 小宋 integration framework v0.1 已落 (1132 行, 14 测试 ✓), 待真数据接 |
| W5-A-05 Prometheus 12 metric | 小郑 | TBD | HC-05 入职前小郑独扛 |
| W5-A-06 Docker compose dev stack | 老吴 | TBD | M1 后启动 |
| W5-A-07 signer mock interface | 老孙 | 主管自加 / TBD | 小蒋 paper engine 已 mock signer; M5+ 老孙 v4 真接 |
| W5-A-08 LiveSection 5 enum | 小卢 | ✓ W4 提前落 (25/25, W4 Wave 19) | A/D 跨主管协商小田归属仍 pending |

**W5 末 A 单元 cpp 净增 (来自 commit `3ab5dfb`):** 老李 1050 行 + 小冯 1044 行 = **2094 行 cpp + 36 unit tests** (PolymarketClient 28 + PM WSS 8), 全 312/312 ctest ✓.

**我作为主管 W5 cpp lines = 0 ✓** (mandate §2.2 §6 不亲力亲为红线守住).

---

## §2 first review 结果 (我作为 A 主管 first review, 上 PR 前)

### §2.1 老李 PolymarketClient v0.1

- ✓ 14 接口契约 (IPolymarketClient) 与架构 v0.6 §17 vCPU0 拓扑一致
- ✓ paper/live CMake 物理隔离 (R-7 if-else 二选一 link 互斥)
- ✓ live stub 14 接口全 Unknown (M5+ 老孙 signer 接 byte-equal v3 §D 14 vector)
- ✓ OrderAck.audit_wal_kind 硬填 PaperAudit (R-11)
- ✓ TimestampQuad + DataSourceTsSource 4 值 (R-20 UPSTREAM_PAYLOAD 优先)
- ✓ HMAC bug #3 反模式拦截 (sigType=2 paper 直接 Rejected)
- ✓ 28 unit tests + 零 -Wno- 抑制 (老高 v1.1 review 通过)
- ⏸ F-02 SubmitOrder 接 VirtualMatcher (小蒋 W5 末联调, A↔C 跨, 不阻 PR)
- ⏸ F-14 接公开 endpoint 真数据 (小冯 W5 末, A 单元内, 不阻 PR)
- **A 主管 ack:** PASS, 上 PR

### §2.2 小冯 PM WSS subscriber v0.1

- ✓ IWssTransport 抽象 (MockWssTransport 单测注入)
- ✓ 8 sub topic + exp backoff (1→2→4→...→30s) + 10s heartbeat + 30s pong timeout
- ✓ back-pressure SPSC TryPush 满 drop + metric (R-12 §17.1.1 不阻塞)
- ✓ 4 ts UPSTREAM_PAYLOAD 优先 (R-20)
- ✓ 第 5 host `wss://sports-api.polymarket.com/ws` (R-33, 公开免鉴权, paper 严禁 /ws/user)
- ⏸ BoostBeastTransport 真接 io_context (W6 启动, **我倾向 boost.beast**, 决议见 §4)
- ⏸ ISpscEventSink 接 rigtorp::SPSCQueue (小石 W5 末)
- **A 主管 ack:** PASS, 上 PR; lib 选型我 W6 周一 EOD 拍

### §2.3 first review 累计

W5 末我 first review 通过 2 个 v0.1 (老李 + 小冯) = mandate §6 review KPI 计入 2/期望 ≥ 12 (月末).

---

## §3 跨主管 5 ASK 进度 (mandate §5, 6/01 EOD deadline)

| # | 协商对象 | 接口 | 进度 (W5 末) |
|---|---|---|---|
| 1 | 老韩 (B) | RiskGateway::evaluate() ABI lock + RiskDecision struct | ✓ ABI 0 改动 (老沈 ADR-004 patch + BUG-W5-001 patch 全守住, RiskDecision struct + 21 RejectCode 枚举值未动, commit `3ab5dfb`) |
| 2 | 小梁 (C) | SignalOutput struct schema (signal_id / intent / confidence / 4ts / feature_snapshot_id) | 待小梁 6/01 input ack |
| 3 | 小余 (D) | Goalserve client struct + 4ts 字段位置 | ✓ 已对齐 (小段 v3 4ts 字段 + 小冯 PM WSS 4ts UPSTREAM_PAYLOAD 与 PM client 同 layout) |
| 4 | 小余 (D) | 小田归属 (兼 A + D) | 待 6/02 EOD 老雷拍板 (mandate §5 #4) |
| 5 | 老胡 (E) | integration test fixture | ✓ 小宋 v0.1 已交 (1132 行 / 14 测试, M1 13 验收 ✓), e2e p99 3.9us 远低 50ms 预算 |

**当前 ack 数:** 3/5 ✓ + 2 pending (小梁 SignalOutput + 老雷小田归属).

**顾问团 2 ASK:** 老郭 R-12 vCPU 分配仲裁 (待 6/01 EOD) + 老高 PR review v1.1 (✓ 已落 commit `3ab5dfb`, 5 grep + 4 clang-tidy).

---

## §4 W6 启动决议 (我作为主管自己拍 + 上呈待 GM ack)

### §4.1 我自己拍 (主管内权限)

1. **A 单元 6 月轮值 GM 助理:** 我接 (ADR-005 §4.3 默认 6 月首轮)
2. **主管层 W5 试点 → W6 正式:** 100% 派单走主管 (W5 已达, W6 硬约束保持)
3. **小冯 W6 lib 选型:** 我倾向 **boost.beast** (header-only / asio 同生态 / R-12 协程友好), 周一 EOD 给小冯定稿; 备选 cpp-websocketpp 仅 W6 fallback
4. **小石 ISpscEventSink rigtorp:** W5 末接, W6 周一 first review

### §4.2 待 GM 6/01 ack (老雷拍板, 我不替拍)

- ADR-006 候选 HTTP client: **我倾向 cpp-httplib** (header-only + 编译快 + 老郭同向), 备选 cpr; GM 6/01 点选
- ADR-007 候选 VirtualMatcher 切 Mode A 灰度: **我倾向 W5 末再切** (M1 评审 7/9 前留 buffer), GM 6/01 拍
- ~~ADR-008 Pinnacle 路径~~: 已撤, 改 Goalserve de-vig (GM 错 #9 ack 收口)
- HC-04 wal-storage-engineer / HC-05 observability-2 JD: W4 EOW 起草, 我 + 老吴 + 小林联签, 待老雷 6/01 批 JD timeline

### §4.3 顾问团仲裁待 6/01 EOD (不归我)

- 老郭 R-12 vCPU0-6 分配终评 (mandate §5 #6)
- 老郭 W5 candidate ADR 3 个 forward GM 点选 (HTTP / VirtualMatcher / 撤 Pinnacle 后只剩 2 个)

---

## §5 主管 KPI 自评 update (mandate §6 baseline → W5 末)

| 职责 | W5 baseline | W5 末实际 | 证据 |
|---|---|---|---|
| 1. 拆任务 | 5/5 | **5/5** | W5 24 子任务全派 ✓ (commit `3ab5dfb` 5 模块 + 2 文档全 spec by 主管) |
| 2. 排队 | 4/5 | **4/5** | A 单元 W5 backlog 优先级 ack ✓; W6/W7 v2 6/1 周一出 |
| 3. review | N/A | **5/5** | 2 v0.1 first review PASS (老李 + 小冯), 月末预期 ≥ 12 PR review |
| 4. 跨单元协商 | 4/5 | **4/5** | 5 ASK 中 3 ack ✓ + 2 pending (6/01 EOD 验证) |
| 5. 1:1 + KPI | 3/5 | **3/5** | W5 启动期 1:1 老李 + 小冯 周一各 15min ✓; 14 IC 完整轮 6/30 出 baseline |
| 6. 不亲力亲为 | 5/5 | **5/5** | W5 cpp lines = 0 ✓ (与老韩并列), 0 hotfix 触发 |

**红线自检 (mandate §6):** W5 末 cpp 行数 = 0 (远低 200 行红线), 跨主管 ASK 48h 未 ack 数 = 0 (6/01 EOD 前两 pending 在 24h 窗口内), IC 1:1 跳过 = 0 (W5 启动期仅 2 IC 入轮, 不触发 > 2 周跳过 HR 红牌).

---

## §6 风险登记 update (mandate §10)

| ID | W5 末状态 |
|---|---|
| A-R-01 主管装睡 | 绿 (W5 派单覆盖 100% > 80% 红线) |
| A-R-02 IC 等主管 | 绿 (无 blocker > 24h, 老李 + 小冯 ⏸ 项均跨单元 / 跨 W5-W6 依赖, 非我延迟) |
| A-R-03 跨主管 48h 不下 | 黄 (2 pending, 6/01 EOD 验证; 6/02 EOD 仍未 ack 升老雷) |
| A-R-04 GM 越级派单 | 绿 (W5 GM 1 次紧急 P0 派老沈 BUG-W5-001 走 ADR-005 §3.2 例外, 合法) |
| A-R-05 HC-04/05 未及时扩编 | 黄 (老雷 5/28 批 4 P1 HC 含 E-052/053/054/055, HC-04/05 JD W4 EOW 起草中) |
| A-R-06 小田归属仲裁不下 | 黄 (6/02 EOD 老雷拍板) |

---

## §7 完成汇报

1. **A 单元 W5 末 status:** 2 v0.1 (老李 PolymarketClient + 小冯 PM WSS) 上 PR, 6 ticket TBD W5 末 / M1 后, cpp 净增 2094 行 / 测试 36 个全 PASS (commit `3ab5dfb`)
2. **2 v0.1 first review PASS:** 老李 + 小冯 ack 上 PR; 老李 F-02 / F-14 + 小冯 BoostBeast / SPSC 4 ⏸ 项跟踪 W5-W6 衔接
3. **5 跨主管 ASK 进度:** 3 ack ✓ (老韩 ABI / 小余 Goalserve 4ts / 老胡 fixture) + 2 pending (小梁 SignalOutput / 小余 + 老雷 小田归属), 6/01 EOD 验证
4. **W6 启动决议:** 我接 6 月轮值 GM 助理 + 主管层 W5 试点 → W6 正式 + 小冯 lib 选型周一 EOD 倾向 boost.beast + 小石 SPSC W5 末
5. **待 GM 6/01 ack:** ADR-006 HTTP client (倾向 cpp-httplib) + ADR-007 VirtualMatcher 切时机 (倾向 W5 末) + HC-04/05 JD timeline; 我只表倾向, 老雷拍板

---

**最后更新:** 2026-05-28 by 老周 (A 系统工程部主管)
**下次 review:** 6/01 主管周同步会上老雷质询 + 6/02 EOD 跨主管 2 pending ASK 是否 ack
