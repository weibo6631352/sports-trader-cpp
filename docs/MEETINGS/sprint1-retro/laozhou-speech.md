# Sprint-1 Retro 发言 — 老周 (系统工程 owner)

- Date: 2026-05-28
- Owner: 老周 (cpp-chief-architect)
- Status: 提交 GM 老雷 + retro 全员

---

## 1. 我交付了什么

- `laozhou-architecture-v0.1.md` — 5 层分层 + 10 项技术决策 (D1-D10), 通过 ADR-001 Conditional Accept
- `laozhou-architecture-v0.2.md` — 落 ADR-001 整改 (双 WAL 物理分离 §7.3, RiskGateway link 阻断 §4, SAFE_MODE §14)
- `laozhou-architecture-v0.3.md` — 落 GM R-12 红线 (§17 WebSocket 不阻塞 + bulk endpoint + single-flight + 4 vCPU 收口)
- `laozhou-lifecycle-management-v1.md` — 数据 + 连接生命周期 (G1 保连接 / G2 防泄漏 / G3 防浪费 + 三层兜底)
- §6 ring buffer 拓扑 (wss_in_ring / book_to_strat_ring / bg_work_ring / bg_to_book_ring)
- §15 vCPU0..3 显式收口表 (vCPU0 = WebSocket only, REST 强制 vCPU3)

---

## 2. 我读了谁的文档 (听取确认清单)

- **老韩 RM v0.2** — Agreed (主要部分). §12 audit WAL group commit / SPSC 6us 同步预算 / §13 SAFE_MODE 三态 unlock 全部对齐我 v0.3 §17. §4.3 副作用表我同意 (C-H5). 我对 §3.7 STALE 阈值 2s/10s 有补充 (见 §3 #1).
- **老李 endpoint matrix v2** — Agreed (47 endpoint 实测 + 决策路径 5 endpoint 浓缩). 我 v0.3 §17.5 bulk 路由直接引用他这个矩阵. 对 v1 spec 3 处错 (HMAC path 含 query / asset_type / sigType) 我同意他纠正, 我 v0.3 不受影响 (我没固化错版).
- **老叶 RPC v1** — Agreed (1 WSS × 3 logs sub 多合约), 影响我架构 §17.2: Polygon 反应器 T1 只跑 1 个 connection × 3 sub, 不是 per-strategy. 我会在 v0.4 把 §17.1.1 T1 描述显式写成 "1 conn × 3 sub multi-address", 不是模糊"Polygon WebSocket reactor".
- **老陈 network-bench v1** — Agreed (跨洋数据). 880ms 是包含 TLS 握手的 starttransfer 中位数, 我已据此 §11.3 把外环 budget 设为 20ms 不含跨洋. 但他的"中国大陆出口"基线不是部署假设 (我们走 us-east-1), v0.3 仍以 ADR-001 §3.2 同区 < 10ms 假设为准.
- **小石 数据结构 v1** — Agreed. rigtorp SPSC 8ns / moodycamel MPSC / SoA price ladder / arena 全部进我 v0.3 §6. §6.4 风险 "数组 ladder 假设 tick 固定 0.01" 我确认: Polymarket 体育市场 tick 死定 0.01, 数组 ladder 可用; 出现 0.005 时 fallback B-tree (我会在 v0.4 §6 补这个 fallback 路径).
- **老姜 latency-budget v1** — Agreed (500us 内环 / 20ms 外环). 但他 §1 阶段 1-5 加起来 92us p99, 我 v0.3 §11.3 收口为 vCPU0 单 message < 50us, **差 42us 缺口我们已对齐** (合并 reactor + simdjson on-demand 只解必要字段). 老姜口头 ack 待他文字补.
- **老孙 signer v4 C++** — Agreed. libsecp256k1 + OpenSSL EVP_sha3_256 选型不冲突我架构. 我对他 §1.2 双 signer UDS 同意, 但要求 v0.4 起 UDS 必须走我 §17 的 worker pool 模式 (不是 hot path inline) — 见 §3 #3.
- **老李 + 小段 api-call-optimization v1** — Agreed (TTL L0-L3 四级). 我 v0.3 §17.5 路由直接引用他们矩阵. cache 接口对接由我 v0.4 起补设计.
- **老唐 audit-schema v1** — Agreed. 12 个 event 类型封闭 + 链式 hash + 7 年留存. audit_id ULID 16B 跟我 §17 / 老韩 §12 / 老王 WAL framework SEQ 三方对齐.
- **老王 WAL framework v0.1** — Agreed. 三 WAL (risk_audit / position / paper_audit) + group commit 64/1ms + R8 paper 不入 position 全部对齐我 §7.3.
- **老郭 ADR-001 评审** — Agreed (五项整改全在 v0.2 落地). 增强项 (CI 静态扫 / runtime trip-wire / 一票否决) 接受, 不抗.
- **GM R-12 WebSocket 不阻塞** — Agreed (Hard Redline). v0.3 §17 整章就是落它.
- **GM 跨域听取义务** — Agreed (Standing Policy). 本文就是落它.

---

## 3. 跨域 misalignment (我发现的)

### #1 STALE 阈值不一致 (老韩 v0.2 §3.7 vs 老陈 网络抖动 vs 小袁微观结构)

- 老韩 v0.2 §3.7 写 WSS WARNING=2s / HALT=10s, 假设 us-east-1 同区.
- 老陈 v1 §3 测出 WSS 消息 interval mean 1024-1781ms, **max 6-19s 静默窗**. 即"无消息 2s" 在跨洋链路上是常态, 不是异常.
- 即使 us-east-1, WSS server-side 心跳间隔不保证 < 2s (老李 v2 §4.2 实测 50 token 16s 内 1014 msg ≈ 63 msg/s, 但低活跃 token 数十秒静默).
- **结论**: 2s WARNING 在低活跃市场会爆 false-positive DEFERRED 雪崩. 我建议**改 token-level freshness**, 不是 connection-level:
  - WSS connection 心跳: 30s 无 frame (含 ping/pong) = WARNING, 60s = HALT (不是 2s)
  - per-token freshness: token 上一次有 book/price_change 已 30s = 该 token 单独 stale, 不影响别的 token 决策
- **态度**: Escalated 给老韩 + 老陈 + 老李三方对齐. 我建议 Sprint-2 联会 30 分钟收口. 这一条不松开, v0.4 不发.

### #2 v0.3 §17 我自己写"WSS 30s 无 message 触发兜底", 与老李 v2 §4.2 "229/500 token silently 跳过"冲突

- 我 §17.6.1 写 "WebSocket 30s 无任何 message → 标 reconnect". 但老李实测: 单 connection 500 token, 229 个**静默 token = server 端没 orderbook 跳过**, 不是断线.
- 即"30s 无 message"判 reconnect 是错的, 真正信号应该是"30s 无 **任何** frame 包括 ping" 才算 connection 死.
- **结论**: 我 v0.3 §17.6.1 措辞要改成 "30s 无任何 frame (含 ping/pong/heartbeat)". 静默 token 走 §17.6 per-token watchdog, 不触发 connection 重连.
- **态度**: Agreed (我承认我写错, v0.4 修, 不需要争议).

### #3 老叶 v1 §3.2 "1 WSS × 3 sub 多 address" 与我 v0.3 §17.1.1 T1 描述不一致

- 老叶: Polygon 1 个 WSS connection × 3 个 logs sub (Polymarket族 + USDC.e Transfer-to-funder + newHeads) 完全够覆盖.
- 我 v0.3 §17.1.1 T1 写 "Polygon WebSocket reactor (eth_subscribe nonce/gas)", 没明示"1 conn × 3 sub multi-address".
- 若按我目前写法, 实现者可能误开 per-strategy WSS connection, 浪费稀缺 socket.
- **态度**: Agreed (我描述模糊, v0.4 §17.1.1 补 "1 conn × 3 sub multi-address [CTF+NegRisk+Funder+ConditionalTokens; USDC.e Transfer-to-funder; newHeads]"). 老叶不需要改, 我改.

### #4 老李 v2 §9.2 #13: NBA 1404 token / MLB 3954 token > WSS 单连 500 上限, 但我 v0.3 §17.1.1 只画 1 个 Polymarket reactor

- 老李实测单 WSS connection 上限 ≥ 500 (1000+ 未压).
- MLB 3954 token / 500 = 8 个 WSS connection 才能全订. NBA + MLB 同时跑要 11 个 connection.
- 我 v0.3 §17.1.1 T0 是单线程单 reactor, 一个线程跑 11 个 WSS connection 用 asio coroutine 是可行的, 但 vCPU0 红线 (R-12 < 50us p99) 在 11 个 connection 并发解 message 时**未压测**.
- **态度**: Escalated 给老姜 + 老李 + 老陈. Sprint-2 必须压测: vCPU0 11 个 WSS connection 同时 burst 时 p99 还能否 < 50us. 若不行, 拆 T0 为 T0a/T0b 多 reactor 分担, 但这破坏 v0.3 §15.6 "vCPU0 = WSS only" 的整洁度. 这是真争议.

### #5 老陈 §10 "WebSocket reconnect budget 3s" 与老韩 v0.2 §13.5 "SAFE_MODE unlock 5min heartbeat OK" 时间尺度差 100x

- 老陈: 单次重连 p50 2s, p99 3.4s, 总 budget 3s.
- 老韩: SAFE_MODE 离开需 5min 持续 heartbeat OK.
- 两个不是同件事 (一个是物理重连, 一个是业务 unlock), 但若 SAFE_MODE 入口频繁触发 (例如 §17.6 stale watchdog 误判), 系统每天若有 N 次 5min 锁等于 alpha 错失.
- **态度**: Compromised. 我接受 5min unlock 是 GM W-3 红线锁死的成本 (老韩 §14.4 R-4). 但我要求 §17.6 watchdog 阈值校准 (见 #1) 避免每天误触超过 3 次.

### #6 老韩 §3.8 sqlite synchronous=NORMAL 与我 §7.3 "运行时不读 DB" 总则冲突边界模糊

- 老韩: sqlite for idempotency, PRAGMA synchronous=NORMAL + WAL mode + 60s checkpoint.
- 我 §7.3.1 P1 写 "DB 仅审计, 运行时不读 DB".
- 老韩的 sqlite **是运行时读 (evaluate 路径上 idempotency 查 seen_keys)**, 但他用 in-memory LRU 主, sqlite 是 cold fallback.
- 表面冲突, 实际不冲突: in-memory LRU 是"运行时主路径", sqlite 是"启动重放期 + LRU miss fallback". 但**这个边界没在任何文档明示**.
- **态度**: Agreed (不算 misalignment, 是文档缺章). 我 v0.4 §7.3 加一条 "sqlite idempotency 不算 DB-read, 因为只在 in-memory LRU miss 走" 明示. 老韩不需要动.

---

## 4. 待 GM 拍板争议

### Escalate-1: WSS connection 数 ceiling 与 vCPU0 单 reactor 是否冲突

(对应 §3 #4)

- 现状: 我 v0.3 §15.6 vCPU0 = "WSS event loop only", T0 单线程跑 Polymarket WSS.
- 问题: MLB + NBA + Soccer 全启用 = 11+ WSS connection 物理需要, 单线程跑 11 个 reactor 的 vCPU0 p99 < 50us 是个 **未验证假设**.
- 三个选项:
  - **(A)** 单线程多 connection (asio coroutine 复用), 接受 vCPU0 < 50us 可能压不住 (需老姜 Sprint-2 实测)
  - **(B)** vCPU0 拆 T0a/T0b 两 reactor, 每 reactor 跑 ≤ 6 connection. 破坏 vCPU0 整洁, 但 p99 有保障
  - **(C)** 只支持单 sport 启用 (MVP NBA only), 11 connection 不存在
- 我推荐 (C) MVP, (B) 长期. 等 GM 老雷拍板 MVP 范围 (产品 owner 小杜也参与).

### Escalate-2: STALE 阈值定义 (connection-level vs token-level)

(对应 §3 #1)

- 老韩 v0.2 §3.7 是 connection-level (WSS 整体 freshness), 但实际业务关心 per-token freshness.
- 改成 per-token 是大手术 (RM 状态机 / metric 维度全要改), 但不改就会爆 false-positive HALT.
- **建议 GM 拍**: per-token freshness 是否进 MVP. 若进, Sprint-2 老韩 v0.3 必须重做 §3.7. 若不进, MVP 接受 false-positive 雪崩风险, 但 KPI 上限拉低.

---

## 5. 我承诺 Sprint-2 做什么

1. **架构 v0.4** — 落实 §3 #1/#2/#3/#4/#6 五条文档补漏 + Escalate-1/-2 GM 拍板后落地. 截止: Sprint-2 W2.
2. **生命周期 v1.1** — 补 single-flight 异常路径 (leader fn() throw / promise broken / 内存泄漏边界), 补 audit WAL fsync 线程 hang 时的 vCPU3 主备切换. 截止: Sprint-2 W3.
3. **vCPU 压测协同** — 与老姜 + 老陈联跑 vCPU0 11 个 WSS connection burst 压测, 验证 p99 < 50us 可达性. 截止: Sprint-2 W4.
4. **cache 接口与老李对齐** — L0-L3 四级 cache 落到 C++ 接口 (`class TtlCache<K,V>` + 写后 bust 钩子 + WSS bust 路径), 给老李 + 小冯 review. 截止: Sprint-2 W3.
5. **R-12 PR-reject 规则 4 条交付老高** — 写成 grep regex + clang-tidy 检查项, 落入 CI. 截止: Sprint-2 W2.

---

## 6. 我对其他人的具体回应 (双向收口)

- **老韩**: 你 v0.2 §3.7 STALE 阈值我**不全同意** (见 §3 #1). Escalated 给 GM. 其余 §4/§12/§13 全 Agreed.
- **老李**: 你 v2 47 endpoint 矩阵我 Agreed, 决策路径 5 endpoint 直接进我 §17.5. v2 §9.2 #13 (NBA/MLB 单 sport 超 500 token) 我承认我 v0.3 没接住, Escalated 给 GM.
- **老叶**: 你 1 WSS × 3 sub 多 address 我 Agreed, 我 v0.3 §17.1.1 描述模糊我自己改 (Agreed). 你不需要动.
- **老陈**: 你跨洋数据我 Agreed, 但 v0.3 部署假设 us-east-1 不是中国大陆, RTT 不直接套你的数. 你 §10 reconnect budget 3s 我 Agreed 但与老韩 SAFE_MODE 5min 关系我已 Compromised 收口 (§3 #5).
- **小石**: 你 v1 全 Agreed. §9 未决项 #3 (tick 固定 0.01) 我**确认 Polymarket 体育市场固定, MVP 不开 0.005**. 你可以基于此实现.
- **老姜**: 你 v1 budget 全 Agreed. §1 阶段表 92us vs 我 §11.3 收口 50us 的 42us 缺口我已 Compromised (合并 reactor + simdjson on-demand). 但 vCPU0 11 connection 压测我求你 Sprint-2 优先做 (Escalate-1).
- **老孙**: 你 v4 C++ signer 我 Agreed. UDS 我额外要求 v0.4 起走 worker pool 不进 hot path inline — 这是我新加的约束, **请你 ack 或反驳, 不允许默认**.
- **老唐**: 你 audit-schema v1 我 Agreed. 12 event 类型封闭性我同意 (我 §17 SAFE_MODE 落 SAFE_MODE_ENTER/EXIT 进你 schema, 不另开 enum).
- **老王**: 你 WAL framework v0.1 我 Agreed. 三 WAL 物理分离 + R8 paper 不入 position 与我 §7.3 完全对齐.
- **老郭**: 你 ADR-001 评审五项整改我 Agreed 全部. 增强项 (CI 静态扫 / runtime trip-wire / 一票否决) 我接受, 不抗.
- **小段**: 你 Goalserve endpoint 矩阵 (我看了关联引用部分) 我 Agreed, T6 / T7 周期线程跑你的 bulk endpoint 已落 §17.1.3. 你不需要动.

---

## 7. 我承认我做错的

1. **v0.3 §17.6.1 "WSS 30s 无任何 message 触发 reconnect" 写错了** — 应该是 "30s 无任何 **frame 含 ping/pong**". 老李 v2 §4.2 实测 229/500 token 静默是常态, 我若按当前写法实现 → 每次启动都会误触 reconnect 雪崩. 这是我对 WSS server-side 行为理解不足. v0.4 修.

2. **v0.3 §17.1.1 T1 Polygon reactor 描述模糊** — 没明示 "1 conn × 3 sub multi-address", 留下了实现者开 per-strategy WSS 的口子. 老叶 v1 §3.2 实证后我才补的, 等于他读完我文档才发现我没写清楚. 跨域听取义务下我应该**先读老叶再写架构**, 顺序反了. 反思: Sprint-2 架构改动前**先把跨域依赖文档读完**再动键盘.

3. **v0.3 没接住"单 sport 4000 token > 500 WSS 上限"的物理事实** — 我 §17 假设单 reactor 单 connection 够用, 老李 v2 §9.2 #13 把这个数学拍我脸上. 我承认我没做 capacity sizing, 直接 11 connection 这个数都是老李推给我才意识到. Escalate-1 是我应该 Sprint-1 内部自查发现的, 不该拖到 retro.

---

**汇报**:

- **已完成**: ✓ (本发言文档)
- **misalignment 数量**: **6 条** (§3 #1-#6, 其中 #2/#3/#6 我自己改, #1/#4 Escalated, #5 Compromised)
- **我升级 GM 的争议**: **2 条** (Escalate-1 vCPU0 ceiling vs WSS connection 数; Escalate-2 STALE 阈值 connection vs token level)
- **Sprint-2 候选数**: **5 项** (§5 列表)

— 老周 (cpp-chief-architect), 2026-05-28
