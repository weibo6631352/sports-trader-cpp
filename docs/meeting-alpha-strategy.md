# Meeting α 战略决议

> 日期: 2026-05-28 · 主持: 主 agent · 参会: CPO / 首席架构师 / Rust 顾问 / Polymarket 协议 / 体育市场 / 博彩 / 金融 / 量化×3 / 需求 / 数据 ETL / 数据 Stats / 数据仓库 / Goalserve 调研
> 上游引用: `sports-tail-trader/CLAUDE.md` (§0 部署 / §3 硬约束 / §7.1 后端统一 / §15 策略 / §17 数据真相), `AGENT.md` §3/§6, `goalserve/full_package_feed.txt` (1487 行), R39 PoC

---

## A. 项目愿景

**CPO 主张**: 北极星指标分两段。
- **P1 (3 个月)**: paper-mode 跑 $112 → $1000, 验证 C++ 链路 RSS leak < 50 MB/h (R39 PoC 实测 ~0; 对比 Python 8 GB/h 见 CLAUDE.md §14 R16-B 噪声教训), p99 决策延迟 < 5 ms (Python 现状 30-80 ms), 24/7 不重启 ≥ 30 天.
- **P2 (6-9 个月)**: 实盘 $1000 → $10,000, 加多账户隔离 (3 个独立钱包 / 独立 bankroll / 独立 OrderExecutor).

**业务边界**: v1 **仅 Polymarket 体育** (复刻 Python 现 8 sport: soccer/basket/tennis/volleyball/amfootball/esports/hockey/baseball + livescore 覆盖 cricket/handball/rugby/boxing/mma 等 9 项, 见 CLAUDE.md §9). v2 候选: outright (世界杯冠军等) + political (选举类). **明确拒绝 crypto / 股票**——非体育需另一套真概率源, 团队组成不适配.

**反对意见**:
- **金融专家** 反对 "$112 → $1000": "样本太小, 统计不显著. p99 决策延迟达标但实际盈利可能纯运气." 建议加 Sharpe > 1.5 / max DD < 30% / 至少 200 笔成交三项硬指标.
- **博彩专家** 反对仅复刻现 sport: "Goalserve 还有 cricket / golf / boxing / F1 / motogp 5 类已订阅未接, 错过 edge 窗口 (CLAUDE.md §16 已点名 cricket/handball/rugby/boxing/mma/golf/horse_racing/f1/motogp 未接入)."

**共识**:
1. P1 加金融三指标 (Sharpe / DD / N≥200)
2. v1 范围 = Python 现已接 8 sport, 但 **v1.1 (P1 内部)** 主动补 cricket + boxing + mma (Goalserve livescore getfeed 已覆盖, Python 也未利用)
3. v2 outright / political 留作 Meeting γ 议题, v1 不绑死

---

## B. 历史复盘共识 (5 条经验)

1. **Python 内存模型不是代码 bug 而是 cpython arena fragmentation** (R39 PoC 同负载 C++ ~0 leak, Rust ~108 MB/h). 任何 GC 语言长跑都有此风险, C++ 是物理解决.
2. **不要看错地方** (R33-R39): 8 GB/h 不是 cycle / closure / asyncio task leak, 是 dict / str 大量 alloc 后 arena 不归还 OS. 改 retention / GC 频率全无效.
3. **真因可能违反直觉**: R16-B 实测 `pipeline/decision/worker.py` tick_reprice_skip 占 backend.log 66% 噪声 (CLAUDE.md §7 引用) — 日志本身就是 GC 压力源.
4. **实测优先**: 任何"应该会快"的论点必须有 benchmark 否则不算数. PoC + perf 测试是 hard requirement.
5. **CLAUDE.md §0 部署约束不变**: 中国→代理→美国 RTT 150-400ms, 带宽小. C++ 改写只省内存和 CPU, **不省网络**. WS 仍是核心, 任何 REST 周期轮询仍是 §17.2 已禁止的反模式.

---

## C. 重写 vs 渐进决议

**架构师立场 (主)**: 100% C++20 ground-up. 理由:
- R39 PoC 单一数据点已证 RSS 行为
- C++ 一体化 (asio + simdjson + websocketpp + libpqxx) 工具链统一, 单一 build / 单一 ASan / 单一 perf 剖析
- Rust 引入会带 cbindgen / `unsafe extern "C"` 边界, 不见得比纯 C++ 安全

**Rust 顾问立场**: C++ 主体 + Rust 局部, 至少 3 处:
- **TLS (rustls)**: OpenSSL 1.1 EOL, BoringSSL build 复杂, rustls 内存安全 + zero-config
- **加密签名 (ethers-rs / k256)**: Polymarket EIP-712 签名 (CLOB-client v2 走 secp256k1) C++ 生态只有 libsecp256k1 + 手写, Rust 有 audit 过的库
- **WS 解析层 (tungstenite)**: websocketpp 已 unmaintained, Boost.Beast 是 OK 但 ws+gzip 帧解析有过历史 CVE

**反对意见**:
- **首席架构师** 反对引 Rust: "FFI 边界本身是状态机 bug 滋生地 (Python ctypes 早教训过). 加密签名用 libsecp256k1 + 1 个手写 EIP-712 helper 200 行可控."
- **C++ 高频系统工程师** 支持 Rust 局部: "TLS 层确实不该自己写, rustls 是工业级答案."

**共识 (最终决议)**: **C++ 主 + Rust 极少量局部 (仅 2 处)**:
| 模块 | 语言 | 理由 |
|---|---|---|
| 核心运行时 / WS / decider / executor / risk / DB / API / 前端 | **C++20** | R39 PoC 已验 |
| TLS (HTTPS + WSS 共用 transport) | **Rust (rustls via cxx bridge)** | 安全 + 维护 |
| EIP-712 签名 + secp256k1 | **Rust (ethers-core + k256)** | audit 过 |

**落地动作 (v0.1)**:
1. 主 build 系统: CMake + Conan2 (C++ 包) + Cargo workspace (Rust 包) + cxx-rs 桥接
2. Rust 部分编译为静态 lib 链入 C++ 主二进制, **不引入运行时 Rust runtime / 多线程模型**, 所有 async 仍在 C++ asio 侧
3. Meeting β 由架构师 + Rust 顾问出 `docs/rust-boundary.md`, 列死 FFI 函数签名 + 内存所有权 + panic = abort 策略

---

## D. 业务范围 v1 / v2

### v1 复刻清单 (3 个月内必达, 对齐 CLAUDE.md §15 策略, §5 落点)

| 模块 | 来源 | 范围 |
|---|---|---|
| 数据入口 | Python `pipeline/ingest/` | Polymarket WS (market + user) + gamma `/events?live=true` + data-api positions/fills |
| 决策器 | Python `workflow/quant_decider.py + quant_signal.py` | Kelly + estimate_signal 真概率 (goalserve devig + math_prob) + 盘口兜底 (microprice/mid/best_bid) |
| 风控 | Python `domain/risk.py` | bankroll / per-position cap / 暂停 / 拒绝原因可审计 (§10) |
| 执行 | Python `infra/polymarket/order_executor.py` + `pipeline/execution/order_gateway.py` | 唯一下单入口 / 唯一签名入口 (§3) |
| 直播源 | Python `infra/sports/goalserve_*` | inplay 8 sport GZIP + livescore 9 项 + pregame odds (默认关) |
| 持久化 | Python audit_events + PostgreSQL | 仅审计 (§17.1) |
| Operator API | Python `api/routes/` | runtime / positions / fills / candidates / decision-context |
| 前端 | Python `frontend/` | 操盘 UI (React + TS) |

### v1 扩展 (P1 内)

- **博彩专家强推 / 共识通过**: Goalserve cricket + boxing + mma livescore getfeed 接入 (Python 未利用; CLAUDE.md §16 已点名)
- **金融专家共识通过**: Kelly sizing 加 confidence interval (用 goalserve 多 bookmaker devig 标准差作为 σ; Kelly fraction = mean - k·σ)
- **博彩专家强推**: line movement tracking — goalserve odds ts 增量拉取 (full_package_feed.txt:32-38) 做 sharp money detection. 当 odds 5 分钟内 > 3% drift 且 Polymarket 价格未跟上 → 触发 edge candidate

### v2 候选 (Meeting γ 再议)

- outright (世界杯 / 联赛冠军, 长期持仓)
- political (选举类, 共享 Polymarket 协议但无 Goalserve 数据源, 需新真概率源)
- VaR-based 整账户风控 (替代 per-position cap)
- 多账户隔离 (3 钱包并行)

### v1 明确拒绝 (反对意见)

- **量化研究-信号** 反对加 ML 模型: "v1 只用 devig + microprice, 不引 ML training pipeline. 历史 8 GB leak 教训告诉我们: 先把基础稳了再加复杂度."
- **CPO** 反对 v1 做 backtest 框架: "v1 paper-mode 就是 backtest 的活体版本, 不再单独造 backtest 工具. Meeting β 再议."

---

## E. 数据基础设施

### 现状 (CLAUDE.md §17.1)
- PostgreSQL 仅 audit, 启动不预热, 运行时不读
- 所有运行时状态在内存 store (GammaMarketSnapshotStore / AccountStateStore / market WS store)

### Goalserve 调研专家提案

`full_package_feed.txt` 1487 行通读后, **未接入** 的 endpoint:
1. **cricket / handball / rugby / boxing / mma livescore** (CLAUDE.md §9 已列, Python 框架准备好但未实战)
2. **NFL play-by-play** (`football/nfl-playbyplay-scores`, 行 395) — drive/play 级状态, 比 score 更细
3. **NBA play-by-play** (行 571) — possession 级
4. **MLB play-by-play** (`baseball/mlb-playbyplay`, 行 308) — pitch 级, NRFI / 单局 prop 必需
5. **Soccer commentaries** (200+ 联赛 ID, 行 151-204) — 进球/红牌事件
6. **Injuries feeds** (soccer/NFL/NBA, 行 232/428) — pregame edge

**共识接入优先级**:
- **v1 必接**: MLB play-by-play (NRFI 策略已上线, 现仅靠 inplay 比分粗粒度, CLAUDE.md "MLB Under总分门禁随局数缩放" 备忘已暗示需要)
- **v1.1**: NBA / NFL play-by-play (分节市场用)
- **v2**: commentaries / injuries / 跨账户 (留 Meeting γ)

### 存储栈决议

**数据仓库专家提案**: 加 TimescaleDB (audit_events 已有 ts, hypertable 自动分片)
**数据 ETL 专家提案**: 加 ClickHouse 做 odds line movement 历史回放
**数据 Stats 专家提案**: DuckDB embedded, 0 运维, 直接读 Parquet

**反对意见**:
- **首席架构师** 强烈反对加任何新 OLAP / 时序 DB 进 v1: "CLAUDE.md §17.1 死规定 DB 不参与运行时. 加 ClickHouse / Timescale 增运维负担且诱惑业务回读. v1 先证 C++ 主链路稳."
- **CPO** 附议: "v1 的目标是 $112→$1000, 不是建数据平台. ETL 写 Parquet 落盘审计够了."

**v1 数据栈最终决议**:

| 用途 | 选型 | 边界 |
|---|---|---|
| 运行时状态 | C++ 内存 store (per-condition lock-free hashmap) | 唯一真相 |
| 审计 (现状保持) | PostgreSQL (audit_events 表) | 仅写入 / operator 查询读 |
| Odds 历史 (新增) | **Parquet 文件 (按天分区)** | ETL worker 每分钟 flush; 0 在线读, 离线分析用 DuckDB ad-hoc |
| 回放 / backtest | v1 不做, v2 议 | — |

**ETL worker**: 单进程内独立 thread (asio strand), 不分布式. CLAUDE.md §0 已说明部署是单 box + 代理, 引分布式只会增带宽消耗.

---

## 未解决议题 (移交 Meeting β / γ)

1. **Meeting β (架构层)**: Rust ↔ C++ FFI 边界细化 — 由架构师 + Rust 顾问出 `docs/rust-boundary.md`, 列死 cxx-bridge 函数签名 / 内存所有权 / panic 策略 / build pipeline
2. **Meeting β (数据层)**: MLB play-by-play parser + NRFI 策略适配 — 由 Goalserve 调研专家 + 量化-信号 出 PoC, 验证 pitch 级数据是否能让 NRFI edge 提升 ≥ 20%
3. **Meeting γ (产品层)**: v2 outright + political 是否纳入 — 由 CPO + Polymarket 协议专家评估市场 liquidity + 真概率源可获得性, 决定是否值得为 outright 重设决策链路 (长期持仓 vs v1 短期 inplay 完全不同形态)

---

## 出席决议确认

- 全员同意 v1 范围 + C++/Rust 边界 + 数据栈
- 反对意见已明确记录, 未否决决议
- 下一步: 主 agent 在 Meeting β 前准备 v0.1 目录骨架 + CMake + Conan2 + Cargo workspace scaffolding
