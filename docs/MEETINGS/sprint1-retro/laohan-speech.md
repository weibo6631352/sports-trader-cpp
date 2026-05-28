# Sprint-1 Retro — 老韩发言 (RM / 风控合规)

- Speaker: 老韩 (risk-engineer)
- Date: 2026-05-28
- 场合: GM 老雷主持的 Sprint-1 Retro
- 约束: 听取义务 + 双向收口 (`docs/ADR/2026-05-28-gm-policy-cross-domain-listening.md`)

---

## 1. 我交付什么 (Sprint-1 老韩产出)

| # | 产出 | 文件 | 状态 |
|---|---|---|---|
| 1 | RM v0.1 设计 (10 规则 / 状态机 / audit 草) | `docs/RESEARCH/laohan-riskmanager-design-v0.1.md` | 老郭 Conditional Accept, 6 项整改 C-H1..C-H6 |
| 2 | RM v0.2 修订 (吸 ADR-001 + 小肖 slippage + 老唐 schema + 双 WAL) | `docs/RESEARCH/laohan-riskmanager-design-v0.2.md` | DONE, 设计已闭环, 实现 Sprint-2 |
| 3 | GM 拍板表 ack (KELLY 0.25 / DAILY 3% / PER_ORDER $200 / 双人 ack 老雷+老沈) | `docs/ADR/2026-05-28-gm-signoff-rm-v0.2.md` | ack |
| 4 | Paper R-11 落位 (paper 走完整 RM / paper_audit 独立 WAL) | `docs/ADR/2026-05-28-gm-signoff-paper-trade.md` | ack |

**承认没交付**:
- 实现代码 (`src/risk/`) 一行没写, 全部停在设计层. Sprint-2 必须出代码.
- §11 STALE 实测校准还差老陈 S1-021 us-east-1 → Polymarket WSS / Goalserve / Polygon p99.9 RTT 数据.
- §12 audit WAL group commit 的 fail-closed 反压注入测试场景没给小宋写完整 fixture.

---

## 2. 听取确认清单 (每文档三态: 我接 / 我有保留 / 我驳)

| 文档 | 我接 (Agreed) | 我有保留 (Concern) | 我驳 (Reject) |
|---|---|---|---|
| 老周 `architecture-v0.3` (R-12 红线 / vCPU 收口) | §17.3.1 下单走 inline (vCPU2), audit WAL fsync 走 vCPU3 T8 跨核, 我接 — 同步路径只 SPSC ring append, fsync 跨核不在 RM 6us 预算 | §15.6 vCPU3 8 线程, audit_fsync (T8) 与 exec_fsync (T9) **共享 vCPU3**, 突发 500 ops/s 高 QPS 时 fsync 抖动会不会影响 metric_exporter / config_watcher 抢核? 我担心 fsync 偶发 1ms 抖动 期间 metric 数据点丢失 → 监控盲区 | — |
| 小肖 `kelly-slippage-model-v1` | §0 fail-closed (NaN/Inf REJECT INTERNAL_ERROR) / §1.4 Kelly 用 $p_f$ 不用 $p_q$ / §3.1 Linear 模型 MVP / §4.1 fill-rate-adjusted Kelly 公式. **`FILL_RATE_FLOOR=0.50` 我接受**, 与我 RM v0.1 §10.3 R-1 一致 | §4.2 RM `OrderIntent` 增 4 字段 (`book_depth_l1_usdc` / `book_snapshot_ts_ns` / `tick_size` / `expected_fill_price_hint`) — 全员上游 (老程 信号 / 小肖) 必填我没异议, 但 Sprint-1 RM v0.2 §3.10 我已经把 4 个 reject 加进 enum, 实现在 Sprint-2. 字段缺失或 NaN = `REJECT(INVALID_INTENT)`, 不允许默认值兜底 | — |
| 老唐 `audit-schema-v1` (BLAKE3 链式 hash + Polygon Merkle anchor + 12 event 类型) | §2 envelope 字段 / §4.1 BLAKE3 prev/payload/current 三 hash / §4.4 防篡改红线 (append-only / chattr +a / S3 Object Lock) / §7 同步 6us 预算 — 全接, **替代我 RM v0.1 §5.2 schema** | §4.3 Merkle anchor 上 Polygon L2 每小时一次, 月成本 $0.7 我接受, 但**OQ-1 (单列 `AET_ANCHOR_PUBLISHED` event vs 复用)**: 我倾向**单列**, 因为 anchor 是合规闭环动作不是业务事件, 混进 RECON_DRIFT 会污染 query Q6 "对账漂移历史". 留 GM 拍 | — |
| 老王 `wal-framework-v0.1` (三 WAL 隔离 / GroupCommit vs PerRecord / RPO≤1ms) | §1.2 R1-R9 全部红线 / §3.1 同步 ≤6us / §4.1 三 WAL 物理分离 (risk_audit / position / paper_audit) / §5.1 启动 SAFE_MODE 默认 / §6 5 种 crash 场景. **我 RM v0.2 §12 派单已接** | §4.1 position WAL 走 **PerRecord** 模式 (每条 fsync 100us..1ms 同步), 这意味着 nonce 递增 / position 写入是 1ms 级阻塞. RM evaluate 不写 position WAL (写在 fill 回来时), 不在我 200us 预算; 但**老叶 nonce_manager 要写, 这块跨进程 signer → trader 同步链路是否 1ms 够?** 留 OQ 给老叶+老姜 | — |
| 小袁 `microstructure-v1` (quote half-life hot 0.21s / cold 120s+ / κ_depth=1.0) | §3.5 给我 `T_HALFLIFE_QUOTE_MS=30000` (cold) + `T_HALFLIFE_QUOTE_HOT_MS=500` (hot) 双参数 / §3.4 `KAPPA_DEPTH_OUTRIGHT=3.0` outright 子参数 / §4.2 `T_half_ms(MarketState)` 分段函数. 我 RM v0.2 §7 参数表会接 | **R-2 复杂化**: hot token 临场 ±10min T_{1/2}=0.21s, 我 v0.2 的 `STALE_THRESHOLD_MS=2s WARNING / 10s HALT` 对 hot token 仍**太宽** (10s = 47 个半衰期, quote 早死透). 见 §3 misalignment #2 | — |
| 小董 `stats-validation-framework-v1` (M4.5 7 hard gate) | §5 M4.5 机器判定 (Sharpe + PnL + 风控失效=0 + 在线率 + DD) 我接, **风控失效=0 是我 G1 红线的 KR 化**, 同义 | §5 给我的口径 "RM REJECT 触发率 < X%" 我希望补一项 "RM 误拒率 (人工复盘判定本应放行) 月度 ≤ 2%", 否则 RM 过严会被 M4.5 反推松绑, 这是我不想踩的坑. 留 OQ 给小董 | — |
| 老孙 `key-management-v4-cpp` (C++ signer / libsecp256k1 / SecureBuffer) | §1 进程拓扑 (UDS HMAC + SO_PEERCRED) / signer 二次校验 (B5) / SignRequest 含完整 typed data. **我 RM audit emit 在 signer 之前, 不冲突** | signer 收到 SignRequest 后做 receiver/amount 白名单校验, 这是**第二道 RM** 在 signer 内. 我支持, 但**若 signer REJECT 而 RM APPROVED, audit 怎么闭环?** 需要 signer 回写一条 `AET_SIGN_FAILED` (老唐 OQ-2). 我倾向**单列**, 不复用 ORDER_PLACED + 空 tx_hash | — |
| 老沈 `key-management-coreview-v1` (8 Blocker) | B1 (SO_PEERCRED+cgroup+binary hash) / B5 (signer 二次校验 typed data + 白名单) / B6 (mlock + MADV_DONTDUMP + PR_SET_DUMPABLE=0 + explicit_bzero 四件套) 全接. **B5 是 receiver 白名单的来源, 与老叶 §1 21 个白名单地址直连** | B3 (审批人 WebAuthn / YubiKey) 与 GM 拍板的 "老雷+老沈 互为 backup 单 ack" 有张力: 24/7 单 ack 怎么 enforce 硬件 token? 我接 WebAuthn 但希望 §7.2 SOP 写"backup ack 也必须有硬件 token, 不允许密码兜底" | — |
| 老黄 `compliance-redline-v1` (12 红线) | R1-R12 全部, 尤其 R8 (私钥永不入 audit) / R12 (AI 自主提币 双 ack) — 与老唐 schema §2.4.8 一致, RM audit emit KEY_ROTATION 只留元数据 | R5 (OFAC 关联) 走老唐 ORDER_FILLED.counterparty_address 离线 Chainalysis 扫, 这是**事后扫**不是 RM 前置拦. 我不能在 evaluate 路径上同步查 OFAC (跨洋查询 100ms+ 杀死 200us 预算), 但 RM 应该接 OFAC 黑名单 in-memory bloom filter, 命中即 REJECT. 留 Sprint-2 OQ | — |
| 老叶 `receiver-whitelist-v1` (21 个 Polygon 合约) | §1 21 个白名单全收 (CTF Exchange V2 / NegRisk V2 / Conditional Tokens / USDC.e / pUSD / proxy wallet factory). RM 在 evaluate 阶段不查链上, signer B5 命中白名单后才签 | §1.1 v1 (CTFExchange V1) 与 v2 共存 6 个月 (2026-12-31 下线), RM v0.2 §3 R0 (基础校验) 现在不区分 v1/v2, 但 audit ORDER_PLACED.tx_hash 必须能事后区分 (走 receiver_addr enum). 我接, 但需要老叶给一份**v1→v2 流量迁移监控** 指标 | — |
| ADR-001 (老郭 13 整改) | C-H1 STALE 2s/5s/10s + 10s/15s/30s / C-H2 audit WAL group commit / C-H3 EDGE_CI_NEGATIVE 单列 / C-H4 sqlite NORMAL+WAL / C-H5 副作用表 / C-H6 RM event loop fill 高优. **全接, v0.2 已落** | — | — |
| GM RM v0.2 sign-off (KELLY 0.25 / DAILY 3% / PER_ORDER $200 默认 / 双人 ack / 影子 2 周) | 全接 | PER_ORDER_CAP $200 是 hardcoded default 等小梁 6/4 给值, 这 6 天窗口期 RM 实现已经在跑 (Sprint-2 周一启动), 我用 `consteval` 把 $200 编入 binary 防绕过, 等小梁数值出再 hot reload | — |
| GM paper-trade sign-off (R-11 paper 走完整 RM) | D-PT1 paper 走全 RiskGateway + 全 audit / D-PT3 paper_audit 独立 WAL / R-11 paper_* 严禁污染 position+pnl+nonce. **全接** | — | — |

---

## 3. 跨域 misalignment (我观察到的)

### #1 [WSS STALE 阈值 vs quote half-life hot token]

- 老郭 ADR-001 §3.2 定 `STALE_THRESHOLD_MS=2000 (WARNING) / STALE_HALT_MS=10000 (HALT)` for Polymarket WSS
- 小袁 `microstructure-v1` §3.5 实测 hot token (临场 ±10min) `T_{1/2}=0.21s`, mean inter-arrival 0.30s
- **冲突**: 2s WARNING = 9.5 个半衰期 = quote 99.86% 已失效; 10s HALT = 47 半衰期, quote 早就死透
- **我 v0.1 R-2 老郭驳回 30s/60s 改 2s/10s, 现在小袁实测说 2s 仍然太宽** 给 hot token. 这不是老郭错, 是 hot/cold 二态他没分; 不是我错, 是我 v0.1 没读小袁 (那时小袁还没出 v1)
- **建议 GM 拍**: RM v0.3 引入 `MarketState` 分段 STALE 阈值 (老韩 + 小袁 联签):
  - `INPLAY_HOT` (临场 ±10min): `STALE_WARNING=500ms / STALE_HALT=2000ms` (~10 半衰期内 HALT)
  - `PREGAME_NEAR` (临场 5-60min): `STALE_WARNING=2000ms / STALE_HALT=10000ms` (老郭原值, 保留)
  - `PREGAME_FAR` (临场 > 1h): `STALE_WARNING=10000ms / STALE_HALT=30000ms` (cold token 容忍)
  - `OUTRIGHT`: `STALE_WARNING=30000ms / STALE_HALT=60000ms` (`T_{1/2}>120s`)
- **D-06 仍然是 HALT 硬上限 30s**, 但 hot token 提前到 2s HALT 不违反 D-06 (只能更严)

### #2 [audit WAL fsync 跨核 vs Polygon Merkle anchor 节奏]

- 老唐 §4.3 Merkle anchor 每小时上 Polygon L2 一次
- 老王 §4.1 audit WAL segment_max_bytes=256MiB, 1h rotation_period
- **冲突**: 如果 audit QPS 突发 500 ops/s 持续, 256MiB segment 大约 4-6h 才满; 但 anchor 是 1h 节奏. 那 1h 内未 roll 的 segment 怎么算 Merkle root? 老唐 §4.3 写"上一小时 WAL records 的 Merkle root", 暗示**跨 segment** 算 root, 不依赖 segment 边界
- **派单**: 老唐 + 老王 联签确认 Merkle root 计算是"按 records 时间窗"还是"按 segment 文件". 我倾向**按时间窗** (实现稍复杂但语义清晰), 不能让 anchor 频率被 segment rotation 牵着走

### #3 [paper_audit WAL 与 live audit_id 命名空间]

- 老王 §4.1 paper_audit 独立 ring + 独立 fd + 独立 bg fsync 线程 ✓
- 老唐 schema §2.1 `audit_id = ULID (16B)`, **全局唯一**
- **冲突**: paper 与 live 是同一进程不同 mode (GM D-PT2 build-time / run-time 二选一启动, 单进程只跑一个 mode), 还是 paper 子进程独立? 如果同进程 paper run-time 切, paper_audit ULID 和 live audit ULID 共享 generator → ULID 时间戳部分可能交错 → 日后审计取证时无法仅凭 ULID 区分 paper/live
- **派单**: 小蒋 (paper engine owner) + 老唐 + 我, 三方确认:
  - 方案 A: paper / live ULID 各跑独立 generator, payload 里加 `mode: enum {Live, Paper, Shadow}` 字段冗余标识
  - 方案 B: 物理隔离子进程, ULID 共享时序 OK
- 我倾向 A (实现简单, 取证字段明确), 留 GM 拍

### #4 [signer B5 REJECT vs RM APPROVED — audit 闭环]

- 老沈 B5 / 老孙 v4 §3.1: signer 收 SignRequest 二次校验 typed_data + receiver 白名单, REJECT 则不签
- RM evaluate 已经 emit `ORDER_DECISION` (APPROVED) audit
- **冲突**: RM 说放行, signer 说拒, 中间有 audit 缺口. 老唐 OQ-2 (单列 `AET_SIGN_FAILED` vs 复用 ORDER_PLACED + 空 tx) 未决
- **派单**: 我倾向**单列 `AET_SIGN_FAILED`**, 父 audit = ORDER_DECISION.audit_id, 含 signer reject reason + typed_data hash. enum 多一项不算膨胀, 比 ORDER_PLACED 空 tx 语义清晰百倍

### #5 [小董 M4.5 风控失效=0 vs RM 误拒率]

- 小董 §5 M4.5 gate "风控失效 = 0", 与我 G1 (0 起绕过事故) 同义
- **缺口**: 没定义"RM 误拒率" (本应放行被 REJECT). M4.5 通过后若 RM 过严, GM 会有压力松绑参数 → 触发我 §7 "只可调严不可调松" 原则冲突
- **派单**: 小董 v1.1 加 KPI "RM 误拒率 月度 ≤ 2%", 误拒判定走小宋人工抽样 replay (audit `stcpp-audit replay <intent_id>` 给等价 mock 入参, 老韩 + 小梁 双签判定)

---

## 4. 待 GM 拍板争议

### Q1: hot token STALE 是否进 RM v0.3

如上 misalignment #1. 我请 GM 拍:
- (a) RM v0.3 引入 `MarketState` 分段, hot 用 500ms/2000ms (我倾向)
- (b) MVP 全局 2s/10s 不分段, M5 再优化

**我的立场**: (a). 如果一个 inplay NBA 关键节点 quote 死了 5s, 我们还在按 5s 前 quote 下单, 这是 §0 红线 "fail-closed" 立场的违反.

### Q2: paper / live ULID 命名空间

如上 misalignment #3. 我倾向方案 A (独立 generator + payload mode 字段). 留 GM 拍.

### Q3: AET_SIGN_FAILED 单列 vs 复用

如上 misalignment #4. 我倾向单列, 留老唐 + GM 拍.

---

## 5. Sprint-2 承诺 (3-5 项, 可度量)

| # | 承诺 | 度量 | 截止 |
|---|---|---|---|
| S2-RM-1 | RiskGateway::evaluate() C++ 实现 (R0-R9 + R-new-A/B), p99 ≤ 200us 含 audit emit | google benchmark; 老姜 perf review | Sprint-2 中 |
| S2-RM-2 | audit WAL 接老王 framework (GroupCommit, batch=64 OR 1ms), fail-closed 反压测试通过 (小宋 3 场景) | 注入 fsync hang / disk full / ring full, evaluate 全部 REJECT(INTERNAL_ERROR) | Sprint-2 末 |
| S2-RM-3 | slippage 模型 v1 §3.1 Linear 接入 RM (小肖代码 + 我 wiring), 单测 7 case 全过 | Case 5 (slippage 抵消 edge → size=0 → REJECT EDGE_NEGATED_BY_SLIPPAGE) 必过 | Sprint-2 中 |
| S2-RM-4 | RM v0.3 设计稿: hot/cold 分段 STALE + signer feedback loop (AET_SIGN_FAILED) | doc draft + 老郭 review | Sprint-2 末 |
| S2-RM-5 | M4.5 RM 误拒率 KPI 定义 + audit replay 工具 (`stcpp-audit replay`) MVP | 与小宋 / 小董 联签 | Sprint-2 末 |

**不在 Sprint-2 (留 Sprint-3+)**:
- 多策略 Kelly (correlated bets) — 小肖 v0.3 后再做
- OFAC bloom filter in-memory — 等老黄 / 老叶给数据源
- Merkle anchor 上 Polygon — 老叶 + 老唐 owner, 我配合 audit emit

---

## 6. 双向收口

### 6.1 给我派单 (我接的)

- 老王: §1.2 R3 fail-closed enforcement 在 RM evaluate 端 — 我接, S2-RM-2
- 老唐: §6.1 RM v0.2 §5 schema 升级到 audit-schema-v1 §2.4.1 — 我接, v0.2 已落
- 老郭: C-H1..C-H6 — 我接, v0.2 已落
- 小肖: §4.2 OrderIntent 增 4 字段 + RejectReason 增 4 — 我接, v0.2 §3.10 已落
- 老沈: B5 signer 二次校验后, RM 接 `AET_SIGN_FAILED` audit — 我接, S2-RM-4 设计

### 6.2 我派出 (等回执)

- 小袁: §3.5 给我的 hot/cold 二态参数, 我 v0.3 接, 但需要你给"hot token 判定" code-level 实现 (过去 60s WSS event_rate > 1/s OR market.gameStartTime ±10min) — 老韩派, 截止 Sprint-2 中
- 老唐: OQ-2 (AET_SIGN_FAILED 单列) 我倾向单列, 请你 v1.1 拍板
- 老叶: v1→v2 receiver 迁移监控 (audit ORDER_PLACED.tx_hash 按 receiver enum 统计), Sprint-2 出
- 小蒋: paper / live ULID 命名空间, 请走 GM 决策 (我倾向方案 A)
- 小董: v1.1 加 "RM 误拒率 ≤ 2%" KPI, 我配合定义 replay 抽样规则
- 老姜: §11.3 audit WAL 同步 6us 在我 200us 预算内是 3%, 请你 S1-011 v0.4 把 RM evaluate 阶段 (含 audit emit) 显式列一行, 别埋在"剩余 194us"里
- GM 老雷: Q1 (hot STALE), Q2 (paper ULID), Q3 (AET_SIGN_FAILED) 三项拍板

---

## 7. 我承认做错的

1. **v0.1 STALE 30s/60s 是不读老吴部署文档的直接后果**. 老郭 §8 附言批我"跨文档读不全", 我 ack. v0.2 改 2s/10s 也是用老郭的数; 现在小袁实测出来连 2s 都太宽 (hot token). 这说明我 v0.1 不仅没读老吴, 也没等小袁 — 设计时凭直觉拍阈值是错的, 凭"上游会校准"也是错的. **Sprint-2 起所有阈值参数必须在文档里标注"数据依据 = 谁的实测 + 文档引用 + 实测日期", 没数据依据的拍脑袋值禁止进 v1.0**.
2. **v0.1 audit fsync N=10 是教科书答案, 不是工程答案**. 老郭 §3.3 group commit + WAL 的方案我应该自己想到 (Oracle/MySQL 1990 年代就有). 这块是我 trading-system 工程经验不够厚的硬伤, 不是"事后认错", 是"事前应该 ask 老郭". 流程上, 我承诺 Sprint-2 起每个新设计先与老郭 30min 同步 (异步发我 outline + 他给 ok/不 ok), 再正式起草.
3. **v0.1 §3.2 把 EDGE_CI_NEGATIVE 留成开放问题**, 老郭 F-6 直接拍单列, 这种"小决策不拍"是延迟病, 我应该自己拍 (单列, 显然). v0.2 已修. **Sprint-2 起设计文档不允许出现"待会签" 的小决策, 只许出现"待会签的战略决策" (例如 PER_ORDER_CAP 数值需小梁市场容量评估), 工程决策必须我自己拍**.
4. **paper R-11 我没 anticipate**. 老高 §2.6 提了 paper 走不走 RM, 我 v0.1 没接, GM 直接拍板才写进 v0.2. 这种"上游已经在问的问题我没接"是听取义务违规, ack. Sprint-2 起每天 30min 扫一遍同 Sprint 别人的开放问题列表 (老米 doc-curator 可帮我做 watcher).

---

**END.** 本发言要旨已落, 等 GM 拍 Q1/Q2/Q3 三项即可推进 Sprint-2 实施.

— 老韩, 2026-05-28
