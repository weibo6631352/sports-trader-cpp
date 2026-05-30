# W5 末架构 Challenge 投票 — 小余 (D 数据基础设施部)

- Owner: 小余 (E-022, D 数据基础设施部主管)
- Date: 2026-05-28 (W5 末, Wave 27 架构 challenge 投票会)
- last_review: 2026-05-28
- ADR ref: ADR-009 v2 (Sonnet)
- 视角: 数据 ETL + schema + ML pipeline + 跨洋数据源约束
- 主管 KPI 守住: W5 末 cpp = 0 (mandate v1 §2.2 don't #1, 5.5 → 7 已 close)

---

## §1 五议题投票

### 议题 1 — paper/live 共享 binary (R-2)

**投票: A 共享**

理由 (数据视角):

- 老周 actual 架构图 (commit `0a9c374`) 已证明 R-7 物理隔离 3 模块 (signer_paper / execution_paper / polymarket_paper) 用 CMake build-time flag 隔离, **binary 可以共享而数据路径不共享**.
- 数据接入层 (小段 Goalserve client / 小冯 PM WSS) 本身是 mode-agnostic — 它们只负责 ingest + 4 ts 携带, 不感知 paper/live. 分叉 binary 会导致 Goalserve schema 需维护两份代码路径, 给 D 单元带来双倍 schema 维护成本.
- CLAUDE.md 红线 "回测与实盘用不同数据处理逻辑 → 策略不允许上线" 直接排除 B 分叉. 共享 binary + build-time flag 是唯一不违此红线的方案.
- 小邓 ML hook (shadow_audit WAL) 已按共享 binary 设计 — `STCPP_EXEC_MODE ∈ {paper, backtest}` 时编译 `stcpp_ml` lib, live 不接; 分叉会打碎这个 CMake 模型.

**反对 B/C 的数据理由**: 分叉 binary 在历史回填场景下要跑两份 binary, Parquet 分区 schema 对齐成本加倍; C 中间方案语义模糊, 老周 v0.4 D5 "stale-from-Goalserve 退档不污染 R-11" 已证明 mode 标记在同一 binary 内可控.

---

### 议题 2 — WSS 拓扑

**投票: B 4-5×2 (4-5 连接 × 2 主题组)**

理由 (数据视角):

- 小冯 PM WSS subscriber v0.1 (commit `3ab5dfb`) 已实现 8 sub topic (market / game / outcomes / book / price_change / last_trade_price / tick_size_change / system) + 第 5 host `wss://sports-api.polymarket.com/ws`. 实际是 sports channel 归一个连接, core channel 归另一个连接, **天然 2 组**.
- 老陈 SSOT v1: Polymarket WSS 单连接 500 token 上限, 重连 ≤ 3 次/min. 8 topic 一个连接 = token 用量高, 分 4-5 连接 × 2 组分散故障域 (老周 v0.4 D4 §17.1.2 ring 拆 4 路 = 故障域硬隔离).
- A 1×8: 单点故障, 老陈 bench 测到 WSS 首条延迟 p99 ~3400ms, 断线重建代价大; 所有 8 topic 共享一条 tcp 连接在 back-pressure 下 head-of-line 阻塞.
- C 2×4 与 B 差异不大, 但 sports 相关 topic (game / outcomes / price_change) 与核心订单 topic (book / last_trade_price) 混在同一连接有 correlated failure 风险.
- D 1×3×3 或 E 其他方案未有实测数据支撑.

---

### 议题 3 — 跨洋部署

**投票: C — M4.5 前单点 (us-east-1), M5+ 多点**

理由 (数据视角):

- 老吴 v1 实测: Goalserve 三域名均为 Phoenix 美西 / NJ 美东机房; 从 us-east-1 直连 Goalserve ~5-15ms, 比中国大陆 + 代理的 1787-2839ms p50 快 200 倍. **Goalserve 是单源, 主节点就近唯一能保证数据时效**.
- 老吴 跨洋部署 v0.1: 多点 active/active 双写面临 nonce 竞争 (老孙 nonce_mgr 设计明确只有 primary 出 OrderIntent), 不适合 M4.5 前.
- M4.5 前 MVP 阶段: 单 Goalserve 源 + 单 Polymarket WSS 接入, 数据依赖链条简单, 单点已够.
- M5+ 多点的核心驱动力是: (a) Goalserve 未来可能出多区域镜像 (目前仅 Phoenix/NJ 单机房, 见 inplay subscription 未开通约束); (b) Polymarket 若扩区; (c) 业务量达到单节点带宽上限.

**数据约束告警**: Goalserve `inplay.goalserve.com` 当前 HTTPS 443 不可达 (老吴 v1 §3.2), oddsfeed 未开通, **单 source 风险已存在**, 但这是订阅问题不是部署架构问题. 见 §2 data smell.

---

### 议题 4 — ML 时机

**投票: B — W6 shadow 启动**

理由 (数据视角):

- 小邓 ML pipeline v0.1 已落地 32 feature POD + 4 stage join + WAL sink (mldata.wal), W5 小田 Parquet 分区 v0.1 (W6-D-04) 即将接上. **数据资产已就绪**, W6 shadow 启动有物质基础.
- ML hook 走 WAL (WalKind::ShadowAudit, 现状) 与直接 Parquet 不是对立的 — 当前架构: WAL → 异步消费 → Parquet (小邓 v0.1 pipeline §1). WAL 层提供回放和 RPO ≤ 1ms, Parquet 提供 OLAP 探索. **不应该跳过 WAL 直写 Parquet** (详见 §2 data smell).
- C (M2 8/6 shadow) 太早 — paper engine W5 才刚落代码 (1233 行 + 27 测试), 训练样本量不足 (小邓 ML-R6 要求 shadow ≥ 500 触发次数, M2 不可能达到).
- A (M4.5 后) 太晚 — 老雷决议 "paper trading 跑起来后每天产出对齐数据是金矿, 等 M5 = 浪费 4 个月".

**D 单元接口承诺** (W6-D-04 小田 Parquet 分区 P0): W6 末 Parquet schema 落地, 小邓 W7 LightGBM baseline 可以启动. shadow inference C++ 接口 (W8+) 需老周 ack lib.

---

### 议题 5 — vCPU pin

**投票: A — 7 vCPU (按老周 v0.4 D1 §15.6 已 Agreed)**

理由 (数据视角):

- WAL framework v0.2 (老王) 已分配: risk_audit bg core 7, shadow_audit bg core 6, position bg core 7 (合并 GroupCommit). 4 WAL 种类各需 1 bg fsync 线程, 至少需要 core 4-7 共 4 个专用核.
- D 单元 ETL pipeline 数据路径 (Goalserve poller + PM WSS ingest) 在接入层 (SPSC 前), **不在热路径上**, 不需要独立 pin core; 走 OS scheduler 可以接受 (老周 v0.4 D1 nice 三档已覆盖).
- B 2-3 vCPU: 会让 WAL bg fsync 与 ETL ingest 抢 core, 给 Goalserve REST poller (c=4-6 并发, 老吴 实测) 带来调度抖动.
- C M4.5 前 single: 单核跑不了当前已有的 319 测试 + WAL 4 种 bg 线程. 已经是 M2+ 阶段了.

---

## §2 Data Smell — 我自己抛的异常点

**DS-01: 4 WAL kind 够不够? 建议拆到 5 WAL**

现有 4 WAL: risk_audit / position / paper_audit / shadow_audit.
问题: Goalserve raw ingest 数据 (小段 client 拿到的原始 XML/JSON) 目前没有自己的 WAL — 它直接走 SPSC ring 进 Signal Engine, 没有持久化层. 如果 ring 满 silent drop (R-12 语义), **原始 ingest 数据就永久丢失**, 回填和 debug 无法重放.
建议: 新增 `WalKind::IngestRaw` (第 5 WAL), 专门存 Goalserve / PM WSS 原始帧 (best-effort, silent drop on ring full 可接受, 但要能 replay). 影响: 需老王 WAL v0.3 + 老郭 ADR 评审.

**DS-02: Goalserve 单 source vendor lock-in 风险未量化**

inplay.goalserve.com HTTPS 443 不可达 (老吴 v1 §3.2, subscription 未开通), oddsfeed 未开通. **当前实际上只有 www.goalserve.com 主 REST 可用** = 完全单 source 单域名. 如果 Goalserve 服务端故障 (老吴 v1 测到 p99 13763ms, 晚高峰未实测), 整个信号链路 (P0-01 PinnacleNoVig 改 Goalserve de-vig, ADR-008) 断掉. 建议在 W6 确认 inplay subscription 开通时间前, 老周/老雷 评估第二 odds source (备选) 的引入时机.

**DS-03: R-20 4 ts 在 Parquet 训练数据回放时是否需要第 5 个 ts?**

4 ts 契约: event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts.
ML 训练场景额外需要 `label_ts` (UMA settle 时间) — 这已经是第 5 个时间戳. 小邓 v0.1 §4.3 "Settle ts 链特殊" 里 `as_of_ts = label 写入时`, 但 `label_ts` 与 `as_of_ts` 语义不同: 一个是标签产生时间, 一个是 WAL 写入时间. PIT join 用的是哪个? 建议小邓 + 小余 在 W6-D-04 Parquet schema 里明确 `label_ts` 作为第 5 字段 (非 R-20 红线字段, 但 ML pipeline 必须).

**DS-04: ML hook WAL → Parquet 中间层丢失问题**

小邓 pipeline: WAL bytes → Parquet (W5 小田). 但 WAL segment 是 256 MiB/segment (老王 v0.2 §3), 按 1 KiB record = 每段 256K 条. Parquet 转换是 cron? 实时流? 如果是 cron 1h 一次, W4-12 期间的 record 在 crash recovery 后是否能从 WAL 重放完整? WAL replay (RTO ≤ 30s for 1M records) 是有保障的, 但 cron 间隔内挂掉 Parquet 会缺 window. 建议明确 WAL → Parquet ETL 触发机制 (time-based / size-based / manual) 和补偿策略.

**DS-05: gzip ratio anomaly 检测缺 CI 集成**

老吴 v1 §8.2 已定义 `gzip_ratio_anomaly` 告警 (XML endpoint gzip ratio > 50% → 服务端未开 gzip). 但这个告警只在 Prometheus alertmanager 层, **没有 ETL pipeline CI 集成**. 如果 Goalserve 悄悄关掉 gzip (schema 静默变更, CLAUDE.md 红线), 小段 Goalserve client 会收到膨胀 3 倍的 raw body, SPSC ring 被撑满. 建议 D-W6 加 ETL 层 gzip ratio 自检 (小段 / 小冯 接 `Accept-Encoding` response header 检查).

---

## §3 议题优先级排序 (数据影响面降序)

| 优先级 | 议题 | 理由 |
|---|---|---|
| 1 | 议题 3 跨洋部署 | Goalserve 单 source 约束 + inplay subscription 未开通 = 部署拓扑错误会让 D 单元数据接入全断 |
| 2 | 议题 4 ML 时机 | Parquet pipeline (W6-D-04) 是 D 单元 P0 critical path, ML shadow 时机直接决定小田 / 小邓 W6-W7 工作量 |
| 3 | 议题 2 WSS 拓扑 | PM WSS 8 topic 接法影响小冯 W6 真接 (W6-D-10) + 4 ts 字段位置 enforce |
| 4 | 议题 1 paper/live 共享 binary | 已有 R-7 CMake 隔离实现, 数据层影响已知, 风险最低 |
| 5 | 议题 5 vCPU pin | 影响 WAL bg core 分配, 但 D 单元 ETL 路径不在热路径上, 对我影响最小 |

---

## §4 不耻下问

| # | 问题 | 问谁 | 紧急度 |
|---|---|---|---|
| Q1 | DS-01 第 5 WAL (IngestRaw) 可行性? 老王 v0.2 说 4 类, 加到 5 需要 ADR 评审吗? | 老王 + 老郭 | W6 before D-W6-04 |
| Q2 | Goalserve inplay subscription 开通预期时间? 这直接决定 DS-02 风险等级 | 老雪 (vendor) / 老雷 GM | P0 |
| Q3 | DS-03 label_ts 是否加入 Parquet schema 作第 5 字段? ML-R 红线是否需要为此更新? | 小邓 + 小田 | W6 D-W6-04 之前必须定 |
| Q4 | DS-04 WAL → Parquet ETL 触发机制拍板 (time-based cron / size-trigger / both)? 我需要知道才能设计 W6-D-04 补偿策略 | 小邓 + 老王 | W6 初必须 ack |
| Q5 | 议题 4 投 B (W6 shadow), 但老周/老梁 (C 量化) 若投 C (M2), 会拖 D-W6-04 Parquet schema 的截止时间吗? 若拖, 我需要 24h 内升老雷 | 老周 + 小梁 | 投票后 24h |

---

**约束声明**: 本 vote 不替小田 / 小段 / 小冯 / 小董说话 — 他们各自有 IC 独立立场, D 主管只代表 D 单元的数据/schema 视角. 议题 1-5 投票是我个人主管立场, 非一票否决.

**最后更新**: 2026-05-28 by 小余 (D 主管, W5 末 Wave 27 投票会)
**下次更新**: 投票会后 24h, 结论入 ADR-009 v2
