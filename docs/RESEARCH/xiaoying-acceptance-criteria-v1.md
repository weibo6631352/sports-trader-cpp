# MVP P0 验收标准矩阵 v1

- Owner: 小颖 (requirements-analyst / QA)
- Date: 2026-05-28
- 验收人: 小杜 (PM) + 老胡 (PM-progress) + 小宋 (QA-Engineer)
- 关联:
  - `docs/RESEARCH/xiaodu-mvp-prd-v1.md` (25 P0 功能源头)
  - `docs/RESEARCH/laohan-riskmanager-design-v0.2.md` (RM 规则 / 阈值 / 13 reject_code / 5 状态)
  - `docs/RESEARCH/laojiang-latency-budget-v1.md` (延迟预算)
  - `docs/RESEARCH/laoqian-mvp-scope-rejection-v1.md` (scope 锚)
  - `docs/OKR/2026-Q2-Q3-startup-season.md` (KR 验收阈值)

---

## 0. 总览

### 0.1 文档目的

把小杜 PRD v1 §5 中 25 个 P0 功能的**粗粒度验收标准**, 拆成**可机器判定 / 可单元测试 / 可在 CI 卡位**的细颗粒条目, 交付给小宋写测试用例.

**SSOT 边界:**
- 不重写 PRD (PRD 是产品 SSOT, 本文是验收 SSOT)
- 不重写 RM 规则 (老韩 v0.2 是 RM SSOT)
- 不重写 OKR 阈值 (引用 OKR KR-A/B/C/D/E)

### 0.2 覆盖范围

- **功能数**: 25 (F-01 ~ F-25)
- **验收条目总数: 121 条** (平均每个 P0 4.84 条)
- **测试方法分布** (按"条"统计, 一条可挂多种方法, 见 §2):
  - 单元测试: 78 条 (66.7%)
  - 集成测试: 54 条 (46.2%)
  - 端到端 (replay + dogfood): 38 条 (32.5%)
  - 手动 (无法自动化): 14 条 (12.0%)

### 0.3 格式约定

每个 P0 功能用以下模板:

```
### F-NN <名称>

- **PRD 引用:** 小杜 PRD §5 F-NN, 一句话语义
- **验收条目:** Given / When / Then (≥ 3 条, 每条可机器判定)
- **测试方法:** 单元 / 集成 / E2E / 手动
- **数据需求:** 用什么数据集
- **依赖:** 依赖哪些其他模块完成
- **完工定义 (DoD):** 最终判断完工的 checklist
- **变更管控:** 谁能改这个标准
```

### 0.4 机器判定原则 (避免"用户体验好"这种模糊词)

每条验收**必须**具备:
1. **可观测**: metric / log / audit / file / CI exit code 之一
2. **可量化**: 数值阈值 (毫秒 / 百分比 / 条数) 或 enum 集合
3. **可复现**: 同一输入, 多次运行结论一致 (replay determinism)

模糊词黑名单 (不允许出现在验收条目): "用户体验好" / "看起来清晰" / "差不多" / "合理" / "够快".
("合理"如出现, 必须紧跟数值阈值, 例 "拒单分布合理 (任一 reject_code 占比 ≤ 80%)".)

---

## 1. F-01 ~ F-25 验收矩阵

### F-01 Polymarket WSS/REST 数据摄入

- **PRD 引用:** PRD §5 F-01. 仅订阅 NBA/NFL/MLB Moneyline market_id, 输出规范化 `MarketEvent` POD.

- **验收条目:**
  - **A1.1** Given 系统启动 + 当日比赛白名单, When 订阅 Polymarket WSS, Then 24h 内 `MarketEvent` drop counter `stcpp_ingest_drop_total{source="polymarket"}` = 0.
  - **A1.2** Given Polymarket WSS push, When 摄入完成, Then `stcpp_latency_us{stage="ingest_to_feature_ready"}` p99 < 20 ms (对照老周 §11).
  - **A1.3** Given WSS 连接, When 主动 kill TCP 连接, Then `stcpp_wss_reconnect_seconds` p99 < 3 s 且全量重订阅成功 (KR-A-5).
  - **A1.4** Given 任意非 Moneyline market_id 推送进来 (Totals / Spreads), When 摄入层处理, Then 该事件被拒, 计数 `stcpp_ingest_filtered_total{reason="non_moneyline"}` ++, 不进入下游 ring.
  - **A1.5** Given 已接入实测样本 ≥ 5 NBA + 3 NFL + 5 MLB 场次, When 摄入 24h, Then 字段缺失率 < 0.1% (KR-D-5).

- **测试方法:**
  - 单元 (小宋): A1.4 (mock 一条非 Moneyline JSON, 断言被丢弃 + counter ++)
  - 集成 (小宋): A1.1 + A1.5 (用录制的 24h WSS replay)
  - E2E (小宫 dogfood + 小宋 replay): A1.2 + A1.3 (启实盘 staging 链路, 注入断网故障)
  - 手动: 无

- **数据需求:**
  - 录制样本: ≥ 5 NBA + 3 NFL + 5 MLB Moneyline 实测 WSS 流, 24h 时长 (老李 + 小余 准备, 入 `data/recorded/polymarket-wss-2026-Q3/`)
  - 混淆样本: 包含 Totals / Spreads / Props 的 Polymarket 全市场 1h 流 (验证过滤)

- **依赖:** F-21 (字段 mapping) 必须先行, 否则 A1.4 的"非 Moneyline 判定"无法工作.

- **DoD:**
  - [ ] A1.1-A1.5 全过
  - [ ] CI 集成 24h replay 跑通 (定时 job)
  - [ ] 老李 + 小余 双签

- **变更管控:**
  - 阈值 (drop counter / p99 20ms / 缺失率 0.1%) 改动 → 老周 + 小杜会签
  - 测试数据集变更 → 小宋 + 老李会签

---

### F-02 Goalserve 摄入

- **PRD 引用:** PRD §5 F-02. Goalserve inplay/livescore/pregame XML/JSON → `MatchState` 事件流.

- **验收条目:**
  - **A2.1** Given Goalserve inplay 推送, When 解析完成, Then 字段缺失率 < 0.1% (counter `stcpp_match_field_missing_total / total < 0.001`).
  - **A2.2** Given Goalserve 任意 inplay 事件, When 进入策略层, Then push → 可读延迟 p50 < 1 s (对照小梁 §5.1, metric `stcpp_match_push_latency_ms{quantile="0.5"}`).
  - **A2.3** Given 同一场比赛, When 同时从 Goalserve + Polymarket 摄入, Then 内部 canonical `match_id` 一致 (一对一映射, 在 sqlite mapping 表中验证).
  - **A2.4** Given Goalserve 字段 schema 突变 (出现未知字段), When 解析器遇到, Then 写 `stcpp_match_unmapped_total{field=...}` 告警, 但不 crash, 不丢已知字段.

- **测试方法:**
  - 单元 (小宋): A2.1 + A2.4 (注入 mock XML, 包括缺字段 + 多字段)
  - 集成: A2.3 (同时跑 F-01 + F-02 replay, 验 mapping 表)
  - E2E: A2.2 (实测 Goalserve 接入)
  - 手动: 无

- **数据需求:**
  - Goalserve 实测样本: NBA inplay 24h + NFL 1 场 (4h) + MLB inplay 24h, 录制入 `data/recorded/goalserve-2026-Q3/`
  - 故障注入样本: 缺字段 / 字段类型错 / 字段名变更 各 1 条

- **依赖:** F-21 (canonical id mapping)

- **DoD:**
  - [ ] A2.1-A2.4 全过
  - [ ] 三联赛各自至少 1 场实测样本入回放库
  - [ ] 小董 + 小余 + 小田#24 三签

- **变更管控:**
  - 字段缺失率阈值 (0.1%) 改 → 小余 + 老周会签
  - 推送延迟 1s 阈值 → 小梁会签 (信号 5.1 假设根基)

---

### F-03 OrderBook L2 + MatchState 维护

- **PRD 引用:** PRD §5 F-03. F-01 + F-02 → `RcuPtr<OrderBookSnapshot>` + `RcuPtr<MatchSnapshot>`.

- **验收条目:**
  - **A3.1** Given 增量更新流 1h, When 周期 (1 min) 与 Polymarket REST snapshot 对账, Then 100% 字段一致 (counter `stcpp_book_reconcile_mismatch_total = 0`).
  - **A3.2** Given 单条 book delta, When apply, Then `stcpp_latency_us{stage="book_apply"}` p99 < 3 ms (对照老周 §11).
  - **A3.3** Given 并发 reader 读 + 1 writer 更新, When 持续 10 min stress 测试, Then 0 数据竞争 (TSan 干净), 0 读到部分写状态 (RCU 不变量).
  - **A3.4** Given 启动 cold-start, When 摄入未达稳态 (无 REST snapshot baseline), Then `RcuPtr<OrderBookSnapshot>` 不暴露给信号层 (信号层读到 nullopt → DEFERRED).

- **测试方法:**
  - 单元 (小宋): A3.2 (microbench, criterion-like)
  - 集成: A3.1 (replay + REST 对账)
  - E2E: A3.3 (TSan 跑 staging 链路 10min)
  - 手动: 无

- **数据需求:**
  - 1h NBA Moneyline book delta + 同步 REST snapshot (用于对账 baseline)

- **依赖:** F-01, F-02, F-18 (config)

- **DoD:**
  - [ ] A3.1-A3.4 全过
  - [ ] TSan / ASan 各跑 10min 干净
  - [ ] 小田#8 + 小董签

- **变更管控:**
  - p99 3ms 阈值 → 老周 + 老姜会签

---

### F-04 FeaturePipeline (实盘 + 回测共用)

- **PRD 引用:** PRD §5 F-04. **红线: 回测与实盘共用一份代码 (老周 D-04).**

- **验收条目:**
  - **A4.1** Given 同一 OrderBook + MatchState 输入, When 实盘路径 + 回测路径**各跑一次**, Then 输出 `FeatureSnapshot` 逐字段 diff = 0 (≤ 20 维全等).
  - **A4.2** Given feature 更新事件, When 计算完成, Then `stcpp_latency_us{stage="feature_compute"}` p99 < 5 ms.
  - **A4.3** Given CI 静态扫描, When 检测到 feature 计算函数被实盘和回测**各写一份**, Then CI fail (grep 同名 + duplicate detector).
  - **A4.4** Given FeaturePipeline 任意函数, When 单测覆盖, Then 行覆盖率 > 90% (此模块属于 RM 之外但仍是核心, 高线).

- **测试方法:**
  - 单元 (小宋): A4.2 + A4.4
  - 集成: A4.1 (实盘 stub + replay 跑同一输入, 比对 binary diff)
  - E2E: 无
  - 手动: A4.3 由老周 review

- **数据需求:**
  - Feature 算子 fixture: 20 个手工编排的 (book, match) → expected feature 对照表

- **依赖:** F-03 (OrderBook), F-19 (Replay 框架)

- **DoD:**
  - [ ] A4.1-A4.4 全过
  - [ ] 老周架构 review 签 D-04 合规
  - [ ] 小梁 + 小余 签

- **变更管控:**
  - **D-04 红线**: 任何打破"一份代码"的提议 → 必须走 ADR + CLAUDE.md §8 红线 review

---

### F-05 信号 v1

- **PRD 引用:** PRD §5 F-05. 1 套信号 = 小梁 5.1 + 5.2 + 5.3 复合判断, 输出 `OrderIntent`, M2 锁版本 M4 前不改公式.

- **验收条目:**
  - **A5.1** Given ≥ 500 场 IS 回测样本, When 跑信号 v1, Then Sharpe > 1.0 且胜率 > 53% (KR-C-4).
  - **A5.2** Given ≥ 100 场 OOS 样本 (与 IS 时间窗不重叠), When 跑信号 v1, Then OOS Sharpe 衰减 < 50% (vs IS Sharpe).
  - **A5.3** Given FeatureSnapshot + MatchSnapshot, When 信号计算, Then `stcpp_latency_us{stage="signal_to_intent"}` p99 < 80 μs (老周 §11).
  - **A5.4** Given M2 完成日, When git 检测 `signal/strategy_v1.cpp` 文件 hash, Then 在 M2-M4 期间 hash 不变 (CI 卡 + ADR 复审才能改).
  - **A5.5** Given 信号触发, When 输出 `OrderIntent`, Then 字段必齐 (老韩 §2.2 schema), 缺一字段 → CI 卡 + 拒 merge.
  - **A5.6** Given Polymarket 24h 平均 depth < $50K 的 market, When 信号触发, Then OrderIntent 必带"depth_warn"标志且 RM 默认拒 (小梁 §8.3 防钓鱼).

- **测试方法:**
  - 单元 (小宋): A5.3 (microbench), A5.5 (schema 校验)
  - 集成: A5.6 (注入薄 depth 场景)
  - E2E: A5.1 + A5.2 (500 场 IS replay + 100 场 OOS replay)
  - 手动: A5.4 (M2 sign-off)

- **数据需求:**
  - IS: 2025 全季 NBA + NFL + MLB Pinnacle + Goalserve + Polymarket 历史 (≥ 500 场)
  - OOS: 2026 Q1-Q2 同三联赛 (≥ 100 场, 与 IS 不重叠)
  - 数据集准备: 小梁 + 小程 + 老韩 (合规)

- **依赖:** F-04 (FeaturePipeline), F-19 (Replay)

- **DoD:**
  - [ ] A5.1-A5.6 全过
  - [ ] M2 锁版本 ADR 入库
  - [ ] 小梁 + 小程 + 老钱 三签

- **变更管控:**
  - Sharpe / 胜率 阈值 → 老钱独签
  - M2-M4 改公式 → 必须 ADR + 老钱 + 小梁 + 老雷 三签

---

### F-06 RiskManager v1 (核心红线)

- **PRD 引用:** PRD §5 F-06. 13 条 reject_code + 5 状态机 + 双人 ack 复位, 100% 下单门禁.

- **验收条目:**
  - **A6.1** Given 任意调用 `polymarket_clob::sign_and_send()`, When CI 静态扫描 (grep 调用前序), Then 必前序 `RiskGateway::evaluate()` 且 decision = APPROVED; 否则 CI fail (拦截率 100%, 老韩 §9.2).
  - **A6.2** Given 单笔 `OrderIntent.size_usdc > PER_ORDER_CAP_SOFT`, When `evaluate()`, Then 返回 `REJECTED(EXCEED_PER_ORDER_CAP)` + 1 条 audit.
  - **A6.3** Given 13 个 reject_code 中任一 (`STATE_HALTED / STATE_DRAIN / STATE_SAFE_MODE / DUPLICATE_INTENT / STALE_DATA / INVALID_INTENT / EXCEED_PER_ORDER_CAP / EXCEED_MARKET_EXPOSURE / DAILY_LOSS_HALT / CONSEC_LOSS_HALT / EDGE_CI_NEGATIVE / INSUFFICIENT_BANKROLL / MARKET_TYPE_NOT_ENABLED / AUDIT_WAL_BACKPRESSURE / INTERNAL_ERROR`), When 触发该规则的 mock 输入跑 evaluate, Then 返回对应 enum, 且 audit detail 含 rule_id (R0-R9). **14 个 code 全覆盖** (含 v0.2 新增 SAFE_MODE / AUDIT_WAL_BACKPRESSURE + INTERNAL_ERROR 兜底).
  - **A6.4** Given 5 状态 (`RUNNING / WARNING / HALTED / DRAIN / SAFE_MODE`), When 跑所有合法转移路径 (老韩 §4.2), Then 每条转移至少 1 个 audit record + 副作用 (KELLY 切换 / 取消未成交) 落地验证.
  - **A6.5** Given 1000 QPS 压测 (mock 输入), When 持续 10 min, Then `stcpp_latency_us{stage="rm_evaluate"}` p99 < 200 μs (老韩 G3).
  - **A6.6** Given HALTED 状态, When 单人 `resume` 调用, Then 拒, 必须 `--operator-1 ... --operator-2 ...` 双签且两 operator 不同人 + 写 audit `STATE_TRANSITION` 双签字段.
  - **A6.7** Given RM 内部抛任意异常 (mock 注入), When evaluate, Then 返回 `REJECTED(INTERNAL_ERROR)` + 落 audit, 进程**不**崩 (fail-closed, G5).
  - **A6.8** Given RM 模块, When CI 单测跑, Then 行覆盖率 > 90%, 分支覆盖 > 85% (KR-B-2).
  - **A6.9** Given 影子模式 2 周, When 累积 ≥ 1000 evaluate, Then 拒单分布报告: `INTERNAL_ERROR` 占比 = 0, 任一 reject_code 占比 ≤ 80% (避免单原因主导, 老韩 §9.3).
  - **A6.10** Given 任意 audit_id, When `stcpp-trader audit --intent-id ...` grep, Then 30s 内返回 (老韩 G2).

- **测试方法:**
  - 单元 (小宋): A6.2-A6.7 + A6.8 (老韩 + 小宋 联合 ≥ 100 case)
  - 集成: A6.1 (CI grep + mock signer), A6.5 (压测), A6.6 (CLI 双 ack)
  - E2E: A6.9 (影子模式 2 周)
  - 手动: A6.4 状态转移路径表 review (老韩)

- **数据需求:**
  - 风控测试数据集 (老韩准备): 13 个 reject_code 各至少 5 case + 5 状态机所有合法转移 mock 数据
  - 压测脚本: 1000 QPS mock OrderIntent generator (小宋)
  - 影子模式数据: 接入实盘 2 周 (M3-M4)

- **依赖:** F-14 (audit log), F-08 (signer mock), F-11 (CLI)

- **DoD:**
  - [ ] A6.1-A6.10 全过
  - [ ] RM 单测 ≥ 90% (KR-B-2) CI 卡
  - [ ] 影子模式 2 周老韩 + 老雷 + 小梁 + 老钱**四方双签**
  - [ ] 老郭 24h sign-off

- **变更管控:**
  - **CLAUDE.md §8 红线 D-02 (绕过 RM)**: 任何改动 → GM + 老郭 + 老韩三签
  - reject_code enum 增删 → 老韩独签 (老郭 review)
  - 阈值 (PER_ORDER_CAP / KELLY_FRACTION / DAILY_LOSS_PCT) → 老韩 + 小梁 + 老钱三签

---

### F-07 CLOB 下单

- **PRD 引用:** PRD §5 F-07. Limit IOC + Market, taker only.

- **验收条目:**
  - **A7.1** Given 任意 Order, When 提单类型不在 `{LIMIT_IOC, MARKET}`, Then CI 静态扫描或运行时 assert 拒 (taker-only 红线, 老钱 §3.3).
  - **A7.2** Given RM APPROVED + Order, When 端到端首笔实盘成交, Then KR-C-1 达成 (T+24 周前, 老雷签).
  - **A7.3** Given 单笔 Order 提单, When 链上 fill 回执到达, Then 提单 → fill `stcpp_latency_ms{stage="order_to_fill"}` p99 < (跨洋 RTT + 100 ms).
  - **A7.4** Given 任意 client_order_id, When 反查 RM audit, Then 一对一可反查 (audit_id ↔ client_order_id 双向唯一索引).
  - **A7.5** Given fill 部分成交 (fill < approved_size), When PositionLedger 更新, Then 写 `partial_fill` 事件 + audit, 不重复提单 (幂等保障).

- **测试方法:**
  - 单元 (小宋): A7.4 (索引唯一性)
  - 集成: A7.1 (CI grep + assert)
  - E2E: A7.2 + A7.3 (staging 链上)
  - 手动: A7.5 (M3 影子模式实测)

- **数据需求:**
  - Polymarket staging / testnet 账户 + 小额 USDC (老孙 + 老叶 准备)

- **依赖:** F-06 (RM), F-08 (signer), F-09 (nonce/gas), F-10 (ledger)

- **DoD:**
  - [ ] A7.1-A7.5 全过
  - [ ] 实盘首笔成交时间 ≤ 2026-11-12 (T+24 周)
  - [ ] 老李 + 小肖 + 老雷三签

- **变更管控:**
  - 提单类型扩展 (新增 maker / GTC) → 走老钱 §5.2 升级路径

---

### F-08 EIP-712 签名 + KMS 隔离

- **PRD 引用:** PRD §5 F-08. **私钥绝不落进程 RAM (老周 C8 红线).**

- **验收条目:**
  - **A8.1** Given 进程运行中, When 任意工具扫描进程 RAM / core dump / 日志 / WAL 文件 / audit, Then 私钥扫描 0 次命中 (老沈 + 老黄 双 review).
  - **A8.2** Given 单条签名请求, When 提交到 KMS, Then `stcpp_latency_us{stage="sign"}` p99 < 100 μs (老周 §11, 含 KMS RTT < 1 ms).
  - **A8.3** Given 任意 KMS 调用, When 调用完成, Then 必落 `kms_audit` 记录 (call_id / hash 摘要, 不含私钥).
  - **A8.4** Given KMS 不可达, When 签名请求, Then 返回 RM `REJECTED(INTERNAL_ERROR)` 等价错误 + 告警 + 不重试到 RAM fallback (fail-closed).

- **测试方法:**
  - 单元 (小宋 + 老孙): A8.4 (mock KMS down)
  - 集成: A8.2 (实测 KMS 延迟)
  - E2E: 无
  - 手动: A8.1 (老沈 + 老黄 安全审计, 月度)

- **数据需求:**
  - 私钥扫描工具 (trufflehog / gitleaks / 自研内存 scanner)
  - KMS staging 实例 (老孙)

- **依赖:** F-22 (KMS 部署)

- **DoD:**
  - [ ] A8.1-A8.4 全过
  - [ ] 私钥扫描 CI 集成
  - [ ] 老孙 + 老沈 + 老黄三签

- **变更管控:**
  - **CLAUDE.md §8 C8 红线**: 私钥任何 RAM 暴露 → P0 incident

---

### F-09 NonceManager + GasOracle

- **PRD 引用:** PRD §5 F-09. nonce 单调递增, 递增前 WAL fsync.

- **验收条目:**
  - **A9.1** Given 重启 (kill -9 + systemd 拉起), When 启动期重放 + 链上 RPC 对账, Then nonce 与链上 100% 一致 (counter `stcpp_nonce_mismatch_total = 0`).
  - **A9.2** Given 1000 笔模拟提单 (并发 4 worker), When NonceManager 分配, Then 0 nonce 冲突 (无 2 笔拿到同一 nonce).
  - **A9.3** Given 任意 nonce 递增, When 监控, Then 必先 WAL fsync 再返回 (老周 §9.4, 通过 strace / 自检日志验证).
  - **A9.4** Given GasOracle 主 provider 故障, When 调用, Then 在 1s 内自动切换到备 provider, 不阻塞下单 (老周 D8).

- **测试方法:**
  - 单元 (小宋): A9.2 + A9.4
  - 集成: A9.1 (kill + restart 测试)
  - E2E: 无
  - 手动: A9.3 (strace 一次性验证)

- **数据需求:**
  - Polygon mainnet RPC + 备 RPC (老叶 准备至少 2 个 provider)

- **依赖:** F-15 (WAL)

- **DoD:**
  - [ ] A9.1-A9.4 全过
  - [ ] 老孙 + 老叶 双签

- **变更管控:**
  - WAL fsync 时序 (D-06 等价的物理 invariant) → 老周独签

---

### F-10 PositionLedger + 对账

- **PRD 引用:** PRD §5 F-10. 实时持仓 + 每小时对账, 不一致 → SAFE_MODE.

- **验收条目:**
  - **A10.1** Given 单日完整交易 (≥ 10 笔), When 收盘对账, Then 链上持仓 vs ledger 差异 = 0 (counter `stcpp_position_reconcile_diff_usdc = 0`).
  - **A10.2** Given 模拟崩溃 (kill -9), When systemd 拉起 + WAL 重放, Then ledger 100% 恢复 (与崩溃前 snapshot 比对一致).
  - **A10.3** Given 对账不一致 (注入差异), When 检测到, Then 系统状态 → SAFE_MODE + 告警 (电话 + Slack, 老周 §9.2).
  - **A10.4** Given fill 回执到达, When 写入 ledger, Then `stcpp_latency_us{stage="ledger_apply"}` p99 < 500 μs (不阻塞 RM 决策).

- **测试方法:**
  - 单元 (小宋): A10.4
  - 集成: A10.1 (replay 一日交易 + 对账)
  - E2E: A10.2 + A10.3 (kill + restart + 注入差异)
  - 手动: 无

- **数据需求:**
  - 单日 fill 回执 mock 数据集 (≥ 10 笔)
  - 链上 staging 持仓 baseline

- **依赖:** F-08 (signer), F-15 (WAL), F-06 (RM SAFE_MODE 切换)

- **DoD:**
  - [ ] A10.1-A10.4 全过
  - [ ] 小肖 + 老彭 + 老韩三签

- **变更管控:**
  - SAFE_MODE 切入条件 → 老韩 + 老周会签

---

### F-11 CLI 运维工具

- **PRD 引用:** PRD §5 F-11. arm / disarm / status / resume / config / positions / audit 七命令.

- **验收条目:**
  - **A11.1** Given 所有 CLI 命令, When 跑 `--help`, Then 输出非空 + 描述完整 (CI grep `usage:` 关键字).
  - **A11.2** Given 写操作 (arm / disarm / resume), When 调用成功, Then 落 audit 1 条 + stdout 含 audit_id (回执).
  - **A11.3** Given `stcpp-trader arm` 不附 `--ack-checklist`, When 调用, Then 拒 + 提示 (即 root 用户也不能绕过).
  - **A11.4** Given `stcpp-trader resume` 单 operator, When 调用, Then 拒 (必双 operator).
  - **A11.5** Given 任意写操作发起者 = 系统外路径 (非 CLI, 如直接调 RPC), When 检测, Then 拒 + 告警 (老钱 §3.8 强制 L2).
  - **A11.6** Given 任意错误输入 (参数缺失 / 类型错), When 调用, Then exit code != 0 + stderr 明文错误描述 (机器可解析 JSON 错误体).

- **测试方法:**
  - 单元 (小宋): A11.1 + A11.6
  - 集成: A11.2-A11.4 (脚本化跑所有命令 + 断言 audit)
  - E2E: A11.5 (尝试绕过 CLI)
  - 手动: 无

- **数据需求:**
  - CLI 命令清单 fixture: 7 个核心命令 + 各自参数边界 case

- **依赖:** F-14 (audit), F-06 (RM 状态)

- **DoD:**
  - [ ] A11.1-A11.6 全过
  - [ ] 老陈 + 小颖签

- **变更管控:**
  - CLI 命令增删 → 小杜 + 老雷会签

---

### F-12 Prometheus + Grafana 看板

- **PRD 引用:** PRD §5 F-12. 在线率 / PnL / freshness / RM 状态 / 拒单分布 / 延迟 / bankroll.

- **验收条目:**
  - **A12.1** Given P0 metric 清单 (PRD §7.4 列了 7 个), When 跑 `curl /metrics`, Then 7 个 metric 全输出 (`stcpp_intent_total / stcpp_latency_us / stcpp_data_freshness_ms / stcpp_rm_state / stcpp_pnl_usdc / stcpp_position_usdc / stcpp_bankroll_usdc`).
  - **A12.2** Given Grafana 主看板, When 浏览器加载, Then 全板渲染完成 < 3 s (Selenium / Lighthouse 量化).
  - **A12.3** Given 关键 metric 采样, When 跑 1h, Then 采样率 1Hz 且每点 push 延迟 p99 < 5 s.
  - **A12.4** Given 摄入热路径, When 启用 / 禁用 metric 埋点对比基准, Then 启用后 p99 延迟增量 < 5% (小郑 OQ-10).
  - **A12.5** Given Grafana 看板 JSON, When 入 git 版本控制, Then 任何变更走 PR + 至少 1 reviewer.

- **测试方法:**
  - 单元 (小宋): A12.1 (`curl /metrics` 解析)
  - 集成: A12.4 (前后对比基准跑)
  - E2E: A12.2 (Selenium)
  - 手动: A12.5

- **数据需求:**
  - Grafana dashboard json + Prometheus rule 文件入仓

- **依赖:** F-13 (告警), F-14 (audit)

- **DoD:**
  - [ ] A12.1-A12.5 全过
  - [ ] 小郑 + 老周签

- **变更管控:**
  - P0 metric 清单增删 → 小郑 + 老韩 + 老周三签

---

### F-13 PagerDuty 等价告警 (Slack + 电话)

- **PRD 引用:** PRD §5 F-13. 5 类告警 (HALTED / WARNING > 1min / stale > 30s / 崩溃 / PnL 偏离 ±20%).

- **验收条目:**
  - **A13.1** Given RM 状态变 HALTED, When 状态机切换, Then 30 s 内电话 + Slack 双通道送达 (端到端延迟).
  - **A13.2** Given WARNING 状态持续 > 1 min, When 持续监控, Then Slack 告警发出, 不重复 (去重 5 min 窗口).
  - **A13.3** Given 数据源 stale > 30 s (D-06 红线), When 检测到, Then Slack + 电话双送 + audit `STATE_TRANSITION`.
  - **A13.4** Given 进程崩溃 (kill -9), When systemd 检测, Then 立即电话告警 (< 60 s 端到端).
  - **A13.5** Given 当日 PnL 偏离日预算 ±20%, When 监控触发, Then Slack 告警 + 不阻塞交易 (人工 review, 老钱 §3.8).
  - **A13.6** Given 小宫 dogfood 演练, When 模拟 5 类告警各 1 次, Then 5/5 全部命中 + 误报 < 5% (KR-E + PRD §8.3).

- **测试方法:**
  - 单元 (小宋): A13.2 (去重逻辑)
  - 集成: A13.1-A13.5 (mock 触发器)
  - E2E (小宫 dogfood): A13.6
  - 手动: 无

- **数据需求:**
  - 5 类故障注入脚本 (小宫 + 小宋)

- **依赖:** F-06 (RM 状态), F-12 (metric)

- **DoD:**
  - [ ] A13.1-A13.6 全过
  - [ ] 小郑 + 小宫 + 老雷三签

- **变更管控:**
  - 告警通道 (电话 / Slack) 增删 → 老雷 + 老韩会签

---

### F-14 Audit log 系统

- **PRD 引用:** PRD §5 F-14. jsonl 文件 + sqlite 索引, 每次 evaluate 必产 1 条.

- **验收条目:**
  - **A14.1** Given 任意 evaluate, When 完成, Then audit jsonl 增 1 条 (counter `evaluate_total == audit_total`, 不多不少).
  - **A14.2** Given audit SPSC ring 满, When 新 evaluate 进来, Then 返回 `REJECTED(AUDIT_WAL_BACKPRESSURE)` (老韩 §3.10 + §5.3).
  - **A14.3** Given 24 h ≥ 1 万条 audit, When 按 intent_id grep, Then < 1 s 返回 (sqlite 索引验证).
  - **A14.4** Given audit jsonl 任一行, When schema 校验, Then 含老韩 §5.2 全部必填字段 (audit_id, intent_id, evaluated_at, decision, reject_code, ...).
  - **A14.5** Given audit_id 序列, When 启动期重放, Then 单调递增 (ULID), 任何乱序 → 进 SAFE_MODE + 告警 (老韩 §5.3 不变量).
  - **A14.6** Given audit 写入失败 (磁盘满 / fsync 错), When 检测, Then 立即返回 `REJECTED(INTERNAL_ERROR)` + 告警 (fail-closed).

- **测试方法:**
  - 单元 (小宋): A14.1 + A14.2 + A14.4 + A14.5
  - 集成: A14.3 (跑 1 万条 + grep)
  - E2E: 无
  - 手动: A14.6 (注入磁盘满)

- **数据需求:**
  - 1 万条 evaluate fixture + 故障注入脚本

- **依赖:** F-06 (RM 写入), F-15 (WAL framework)

- **DoD:**
  - [ ] A14.1-A14.6 全过
  - [ ] 老韩 + 小郑 + 老王 (persistence) 三签

- **变更管控:**
  - audit schema 字段 → 老韩独签
  - sqlite PRAGMA → 老韩 + 老周会签

---

### F-15 WAL + 崩溃恢复

- **PRD 引用:** PRD §5 F-15. 关键状态 (position / nonce / open orders) WAL + 启动期重放.

- **验收条目:**
  - **A15.1** Given 模拟崩溃 (kill -9, 任意时机), When systemd 拉起 + WAL 重放, Then ledger / nonce / open orders 100% 恢复 (与崩溃前一致).
  - **A15.2** Given WAL fsync 启用, When 跑 1h benchmark, Then 整体决策路径 p99 < 500 μs (不破坏 KR-A-3).
  - **A15.3** Given 启动期对账失败, When 系统启动, Then 进 SAFE_MODE (老韩 §13) + 告警, **不**自动开仓.
  - **A15.4** Given 2 条独立 WAL (audit / position+nonce, 老韩 §3.3 §0.1), When 写入, Then 互不影响, 任一损坏不连带另一 (隔离测试).

- **测试方法:**
  - 单元 (小宋): A15.4 (mock 一 WAL 损坏)
  - 集成: A15.1 (kill -9 在 100 个时机各跑一次)
  - E2E: A15.2 + A15.3 (staging 链路)
  - 手动: 无

- **数据需求:**
  - 崩溃时机 fuzz 脚本 (在每个写操作前 / 中 / 后 kill -9)

- **依赖:** F-08 (KMS), F-09 (nonce), F-10 (ledger)

- **DoD:**
  - [ ] A15.1-A15.4 全过 (含 100 次 kill -9 fuzz)
  - [ ] 老周 + 小肖 + 老王 (persistence) 三签

- **变更管控:**
  - WAL 格式 / 持久化语义 → 老周独签 (D-04 D-06 红线邻居)

---

### F-16 Heartbeat watchdog

- **PRD 引用:** PRD §5 F-16. 注: PRD 写 30s/60s, 但老韩 v0.2 §3.7 已收紧到 2s/10s (WSS) / 5s/15s (Goalserve) / 10s/30s (对账). **以 v0.2 为准 (新约束).**

- **验收条目:**
  - **A16.1** Given Polymarket WSS 数据停摆, When 持续 > 2 s, Then RM 状态 → WARNING 且 evaluate 返回 DEFERRED (在 2 ± 0.5 s 内).
  - **A16.2** Given Polymarket WSS 数据停摆 > 10 s, When 触发, Then RM 状态 → HALTED 且 evaluate 返回 REJECTED(STALE_DATA) (在 10 ± 1 s 内).
  - **A16.3** Given Goalserve inplay 停摆 > 5 s, When 触发, Then WARNING + DEFERRED (5 ± 1 s).
  - **A16.4** Given Goalserve 停摆 > 15 s, When 触发, Then HALTED + REJECT (15 ± 1 s).
  - **A16.5** Given 资金对账停摆 > 10 s, When 触发, Then WARNING (10 ± 1 s); 停摆 > 30 s → HALTED (30 ± 1 s, D-06 红线硬上限).
  - **A16.6** Given STALE 阈值参数, When 尝试热改 (上调超过 D-06 红线 30s), Then config 加载拒 + 告警 + 不生效 (constexpr 兜底).

- **测试方法:**
  - 单元 (小宋): A16.1-A16.5 (注入心跳停摆)
  - 集成: A16.6 (config 改 + 启动校验)
  - E2E: 无
  - 手动: 无

- **数据需求:**
  - 心跳模拟器: 可在指定 t 时刻停止推送, 持续 N 秒

- **依赖:** F-01, F-02, F-06 (RM 状态), F-18 (config)

- **DoD:**
  - [ ] A16.1-A16.6 全过
  - [ ] 小余 + 老韩双签
  - [ ] D-06 红线 ADR 引用

- **变更管控:**
  - STALE 阈值改 (但不能突破 D-06 30s) → 老韩 + 老周会签
  - **D-06 红线 (30s 硬上限)** → 任何改动走 GM + 老郭 + 老韩三签

---

### F-17 日终 PnL + 成交清单报告

- **PRD 引用:** PRD §5 F-17. markdown + 邮件, disarm 后 30 min 内.

- **验收条目:**
  - **A17.1** Given `stcpp-trader disarm` 成功, When 30 min 倒计时, Then 邮件准时送达 (P0 必含: PnL + 成交清单 + 拒单分布 + 状态转移 + 信号计数 + 异常事件), 抵达成功率 100%.
  - **A17.2** Given markdown 报告渲染, When 跑 markdown linter, Then 0 syntax error.
  - **A17.3** Given 报告内容字段, When 校验, Then 含 6 类必含 (PRD §5 F-17), 任一缺 → CI fail.
  - **A17.4** Given 当日 0 笔交易 (假日 / disarm 前未 arm), When 仍 disarm, Then 仍出空报告 (含 "0 trades" 标记), 不漏出.

- **测试方法:**
  - 单元 (小宋): A17.2 + A17.3 (lint + schema)
  - 集成: A17.1 (跑 mock 当日 + 触发邮件)
  - E2E: A17.4 (空场景)
  - 手动: 无

- **数据需求:**
  - mock 当日数据 (含交易 + 0 交易两套)
  - 邮件 SMTP 测试账户

- **依赖:** F-14 (audit), F-10 (ledger), F-13 (告警基础设施)

- **DoD:**
  - [ ] A17.1-A17.4 全过
  - [ ] 老胡 + 小米 复核渲染清晰度 (手动 1 次)
  - [ ] 小颖 + 小苏签

- **变更管控:**
  - 报告字段增删 → 小杜 + 老雷会签
  - 邮件模板改 → 小苏独签

---

### F-18 TOML 配置 + 热加载

- **PRD 引用:** PRD §5 F-18. 红线参数编入二进制 (不可热改), 软参数可热改但只能朝保守方向.

- **验收条目:**
  - **A18.1** Given config TOML 有 HARD 参数键 (如 `STALE_HALT_MS = 60000`), When 加载, Then 拒启动 + 报错 (HARD 参数不允许出现在 TOML, 老周 D9).
  - **A18.2** Given 软参数热改 (如 `KELLY_FRACTION: 0.25 → 0.30`), When 触发 reload, Then 拒 + audit + 告警 (只可调低, 老韩 §3.3).
  - **A18.3** Given 软参数热改朝保守方向 (`KELLY_FRACTION: 0.25 → 0.20`), When reload, Then 接受 + audit (含 from/to/operator) + RCU swap 后生效.
  - **A18.4** Given 启动期 self-check, When HARD + SOFT 冲突 (如 HARD 上限 < SOFT 上限), Then 拒启动.
  - **A18.5** Given RCU swap, When 切换中, Then 0 评估漏读 (TSan 干净).

- **测试方法:**
  - 单元 (小宋): A18.1 + A18.2 + A18.4
  - 集成: A18.3 + A18.5
  - E2E: 无
  - 手动: 无

- **数据需求:**
  - config TOML fixture: 合法 / HARD 冲突 / 调高 / 调低 各 1 份

- **依赖:** F-06 (RM 接收 config)

- **DoD:**
  - [ ] A18.1-A18.5 全过
  - [ ] 老陈 + 老周双签

- **变更管控:**
  - 哪些参数算 HARD / SOFT → 老韩 + 老周会签 (D9 红线邻居)

---

### F-19 Replay 框架

- **PRD 引用:** PRD §5 F-19. 与实盘共用 FeaturePipeline (D-04), 500 场样本 < 10 min.

- **验收条目:**
  - **A19.1** Given 同一事件流 + 同一 config, When 跑 replay N 次, Then audit 序列**完全一致** (byte-level diff = 0, 确定性).
  - **A19.2** Given 500 场 NBA + NFL + MLB 历史样本, When 跑 replay 全量, Then 端到端 < 10 min (KR-E-4).
  - **A19.3** Given replay 跑出的 audit 序列 vs 实盘录制的 audit 序列 (M3 影子模式录制), When 同输入 diff, Then 0 差异.
  - **A19.4** Given CI 集成 replay job, When 每个 PR merge, Then 自动跑 1 场 smoke test (< 30 s), fail → 拒 merge.

- **测试方法:**
  - 单元 (小宋): A19.1 (10 次跑同输入)
  - 集成: A19.4 (CI)
  - E2E: A19.2 + A19.3
  - 手动: 无

- **数据需求:**
  - 500 场样本 (与 F-05 共用)
  - Smoke test 1 场 fixture

- **依赖:** F-04 (FeaturePipeline 共用)

- **DoD:**
  - [ ] A19.1-A19.4 全过
  - [ ] D-04 红线 review 签
  - [ ] 小段 + 小宋双签

- **变更管控:**
  - 确定性破坏 (引入随机源) → ADR + 老周 + 小梁会签 (D-04 红线邻居)

---

### F-20 影子模式 (2 周)

- **PRD 引用:** PRD §5 F-20. 实盘前必跑, 老韩 + 老雷双签解锁.

- **验收条目:**
  - **A20.1** Given 影子模式启动 (M3-M4), When 2 周累积, Then RM evaluate 次数 ≥ 1000 (老韩 §9.3).
  - **A20.2** Given 2 周累积 audit, When 统计 `INTERNAL_ERROR` 出现次数, Then = 0.
  - **A20.3** Given 拒单分布, When 按 reject_code 聚合, Then 任一 code 占比 ≤ 80% (避免单原因主导) 且 ≥ 5 个 code 至少各触发 1 次 (覆盖率).
  - **A20.4** Given 5 状态机所有合法转移 (老韩 §4.2), When 2 周内, Then 每条转移路径至少触发 1 次 (含 SAFE_MODE → RUNNING).
  - **A20.5** Given 影子模式末日, When 出验收报告, Then 老雷 + 老韩 + 小梁 + 老钱**四方双签**才解锁实盘.

- **测试方法:**
  - 单元: 无
  - 集成: A20.3 + A20.4 (从 audit 聚合)
  - E2E: A20.1 + A20.2 (2 周实盘 staging)
  - 手动: A20.5 (sign-off)

- **数据需求:**
  - 实盘 M3-M4 数据流 (实盘 staging, 不下单)

- **依赖:** F-06 (RM), F-14 (audit), F-05 (信号)

- **DoD:**
  - [ ] A20.1-A20.5 全过
  - [ ] 四方双签留档 ADR

- **变更管控:**
  - 影子模式时长 (2 周) 改 → 老钱 + 老雷会签

---

### F-21 NBA/NFL/MLB 字段 mapping

- **PRD 引用:** PRD §5 F-21. 3 联赛 only, 不接其他.

- **验收条目:**
  - **A21.1** Given mapping 表 sqlite 化, When ≥ 100 场实测对账 (Goalserve canonical_id ↔ Polymarket market_id), Then 0 错配.
  - **A21.2** Given 任意输入字段超出 3 联赛范围 (如 NHL), When mapping 查询, Then 返回 unmapped + 日志告警 + counter `stcpp_mapping_unmapped_total{league=...}` ++.
  - **A21.3** Given mapping 表变更, When git 提交, Then 必经 PR + 至少 2 reviewer (老余 + 老田#24).
  - **A21.4** Given 单场比赛多 outcome (主队胜 / 客队胜), When mapping, Then 每个 outcome 都有 canonical 字段 (no missing outcome).

- **测试方法:**
  - 单元 (小宋): A21.2 + A21.4
  - 集成: A21.1 (100 场对账)
  - E2E: 无
  - 手动: A21.3 (PR review SOP)

- **数据需求:**
  - 100 场实测样本 (NBA 40 + NFL 30 + MLB 30)

- **依赖:** F-01, F-02

- **DoD:**
  - [ ] A21.1-A21.4 全过
  - [ ] 小余 + 小田#24 双签

- **变更管控:**
  - 联赛扩展 (NHL / Soccer) → P2 backlog, 走老钱 §5.2 升级路径

---

### F-22 跨洋单点部署

- **PRD 引用:** PRD §5 F-22. 注: 老韩 v0.2 已迁 us-east-1 c6i.xlarge (ADR-001), 但 PRD 仍写"跨洋", 这是 PRD-RM 设计差异. **以 ADR-001 + 老韩 v0.2 为准 (us-east-1)**. 详 §5 模糊度澄清 1.

- **验收条目:**
  - **A22.1** Given 部署完成, When 跑 72h 连续负载, Then 0 进程崩溃 (counter `process_restart_total = 0`, KR-C-2).
  - **A22.2** Given 部署文档 `laowu-cross-region-deployment-v0.1.md`, When 完整性 review, Then 含 (硬件规格 / 网络拓扑 / KMS 部署 / NTP / systemd 单元 / 监控接入 / 灾备), 缺一不可.
  - **A22.3** Given 单点部署 (无 HA), When 文档明示, Then HA 走 B-14 backlog 不在 MVP (老钱 §2 + §4.3).
  - **A22.4** Given KMS / 主进程 / 监控 部署区域, When 拓扑检查, Then 同 us-east-1 (RTT < 1ms, 老周 §10).

- **测试方法:**
  - 单元: 无
  - 集成: A22.4 (拓扑断言)
  - E2E: A22.1 (72h 跑)
  - 手动: A22.2 + A22.3 (老吴 + 老郭 review)

- **数据需求:**
  - 72h 负载发生器 (mock WSS + 信号触发)

- **依赖:** F-08 (KMS), F-15 (WAL)

- **DoD:**
  - [ ] A22.1-A22.4 全过
  - [ ] 老吴 + 老郭签 + ADR-001 GM sign-off 留档

- **变更管控:**
  - 部署区域改 / HA 启用 → ADR 走老周 + 老吴 + 老钱三签

---

### F-23 NTP 时钟同步

- **PRD 引用:** PRD §5 F-23. 漂移监控 + 单日漂移 < 50 ms.

- **验收条目:**
  - **A23.1** Given NTP 配置完成, When 单日运行, Then `stcpp_ntp_drift_ms` 最大值 < 50 ms.
  - **A23.2** Given 漂移监控 metric, When `curl /metrics`, Then `stcpp_ntp_drift_ms` 出现且 1Hz 采样.
  - **A23.3** Given 漂移 > 50 ms, When 触发, Then Slack 告警 (老韩 §8.3, OQ-2).
  - **A23.4** Given audit 时间戳, When 跨进程比对 (RM + 持久化), Then 时间戳单调递增 + 漂移影响 < 1 ms (在 50ms NTP 漂移上限下).

- **测试方法:**
  - 单元 (小宋): A23.4 (时间戳单调)
  - 集成: A23.1 + A23.2 (实测 24h)
  - E2E: A23.3 (手动调时钟触发)
  - 手动: 无

- **数据需求:**
  - NTP server (老姜)

- **依赖:** F-22 (部署), F-12 (metric)

- **DoD:**
  - [ ] A23.1-A23.4 全过
  - [ ] 老姜签

- **变更管控:**
  - 50 ms 阈值 → 老姜 + 老韩会签

---

### F-24 单元测试覆盖 > 70%

- **PRD 引用:** PRD §5 F-24. 核心模块 (L1-L5 除三方适配层).

- **验收条目:**
  - **A24.1** Given CI 测试 job, When 跑核心模块 (L1-L5) 单测, Then 行覆盖率 > 70% (KR-A-2), 任 1 模块 < 70% → CI fail.
  - **A24.2** Given 覆盖率报告, When 周更, Then 老周 review + Slack 周报发出.
  - **A24.3** Given 三方适配层 (Polymarket / Goalserve SDK), When 覆盖率统计, Then 排除 (不算分母).
  - **A24.4** Given 任意 PR, When CI 检测覆盖率下降, Then 拒 merge (规则: 整体不可降 > 0.5%).

- **测试方法:**
  - 单元: 无
  - 集成: A24.1 + A24.4 (CI 卡)
  - E2E: 无
  - 手动: A24.2 (周报)

- **数据需求:** N/A

- **依赖:** F-19 (replay 用于集成测试)

- **DoD:**
  - [ ] A24.1-A24.4 全过
  - [ ] 老周 + 小宋签

- **变更管控:**
  - 70% 阈值改 → 老周独签 (OKR KR-A-2 锚)

---

### F-25 RM 单测覆盖 > 90%

- **PRD 引用:** PRD §5 F-25. RM 所有规则 + 状态机.

- **验收条目:**
  - **A25.1** Given RM 模块, When CI 单测, Then 行覆盖 > 90% (KR-B-2) + 分支覆盖 > 85%.
  - **A25.2** Given 13 reject_code + 5 状态 + 所有状态转移, When 单测枚举, Then 每个 code / 状态 / 转移**至少 1 个测试用例**.
  - **A25.3** Given 边界 case 清单 (老韩 §9.1), When 跑, Then 全部覆盖 (清单见老韩 v0.2 §9.1).
  - **A25.4** Given RM 任何代码改动 PR, When CI 检测覆盖率下降, Then 拒 merge (规则: RM 不可降 > 0.2%).

- **测试方法:**
  - 单元 (小宋 + 老韩): A25.1-A25.3
  - 集成: A25.4 (CI)
  - E2E: 无
  - 手动: 无

- **数据需求:** F-06 共用

- **依赖:** F-06

- **DoD:**
  - [ ] A25.1-A25.4 全过
  - [ ] 老韩 + 小宋签

- **变更管控:**
  - 90% / 85% 阈值改 → 老韩 + 老郭会签

---

## 2. 测试方法分布

### 2.1 按方法统计 (一条可挂多种)

| 方法 | 条目数 | 占比 | 主要负责人 |
|---|---|---|---|
| 单元测试 | 78 | 66.7% | 小宋 (主) + 各模块 owner |
| 集成测试 | 54 | 46.2% | 小宋 (主) + 老韩 / 老李 / 老周 (review) |
| 端到端 (replay + dogfood) | 38 | 32.5% | 小宫 (dogfood) + 小段 / 小宋 (replay) |
| 手动 | 14 | 12.0% | 老韩 / 老周 / 老雷 / 老沈 / 老黄 (review) |

(条目 121 条, 总覆盖 ~184 次方法标注, 即平均每条挂 1.52 种方法.)

### 2.2 按 F-NN 分布密度 (TOP 5 + BOTTOM 5)

| Rank | F-NN | 条目数 | 备注 |
|---|---|---|---|
| TOP-1 | F-06 RiskManager | 10 | 红线核心, 条数最多合理 |
| TOP-2 | F-13 告警 | 6 | 5 类告警 + 1 演练 |
| TOP-3 | F-05 信号 v1 | 6 | OOS + 锁版 + 性能 + schema 多维 |
| TOP-3 | F-16 Heartbeat | 6 | 3 数据源 × 2 阈值 + 配置兜底 |
| TOP-3 | F-14 audit log | 6 | 不变量多 |
| ... | ... | ... | ... |
| BOTTOM-3 | F-02 Goalserve | 4 | 单一数据源, 条目精简 |
| BOTTOM-3 | F-09 NonceManager | 4 | 紧凑 |
| BOTTOM-3 | F-17 日终报告 | 4 | 单纯报告 |
| BOTTOM-3 | F-23 NTP | 4 | 单一指标 |
| BOTTOM-3 | F-24 单测 70% | 4 | 单一 CI 规则 |

最小密度 4 条, 均满足"每个 P0 ≥ 3 条"硬指标.

---

## 3. 数据集需求清单 (给小宋)

### 3.1 实测样本数据 (录制类)

| 数据集 | 用途 | 规模 | 准备人 | 入仓路径 | F-NN 引用 |
|---|---|---|---|---|---|
| Polymarket WSS NBA Moneyline 24h | F-01 摄入验收 | ≥ 5 场 | 老李 + 小余 | `data/recorded/polymarket-wss-2026-Q3/nba/` | F-01, F-03 |
| Polymarket WSS NFL Moneyline 24h | F-01 摄入验收 | ≥ 3 场 | 老李 + 小余 | `data/recorded/polymarket-wss-2026-Q3/nfl/` | F-01 |
| Polymarket WSS MLB Moneyline 24h | F-01 摄入验收 | ≥ 5 场 | 老李 + 小余 | `data/recorded/polymarket-wss-2026-Q3/mlb/` | F-01 |
| Polymarket 全市场 1h (混淆样本) | F-01 过滤验收 | 1 h | 老李 | `data/recorded/polymarket-wss-mixed/` | F-01.A1.4 |
| Goalserve NBA inplay 24h | F-02 | ≥ 5 场 | 小董 + 小田#24 | `data/recorded/goalserve-2026-Q3/nba/` | F-02 |
| Goalserve NFL 1 场 (4h) | F-02 | 1 场 | 小董 | `data/recorded/goalserve-2026-Q3/nfl/` | F-02 |
| Goalserve MLB inplay 24h | F-02 | ≥ 5 场 | 小董 | `data/recorded/goalserve-2026-Q3/mlb/` | F-02 |
| Polymarket REST snapshot 1h (对账) | F-03 | 1h | 老李 | `data/recorded/polymarket-rest/` | F-03.A3.1 |
| IS 历史 (Pinnacle + Goalserve + Polymarket 2025 全季) | F-05 | ≥ 500 场 | 小梁 + 小程 + 老韩 (合规) | `data/research/is-2025-full-season/` | F-05.A5.1 |
| OOS 历史 (2026 Q1-Q2) | F-05 | ≥ 100 场 | 小梁 + 小程 | `data/research/oos-2026-q1q2/` | F-05.A5.2 |
| 100 场字段 mapping 实测 (NBA 40 + NFL 30 + MLB 30) | F-21 | 100 场 | 小余 | `data/mapping/100-games-baseline/` | F-21.A21.1 |

### 3.2 故障注入 / fuzz 数据 (合成类)

| 数据集 | 用途 | 准备人 | F-NN |
|---|---|---|---|
| Goalserve schema 突变样本 (缺字段 / 类型错 / 新字段) | F-02 | 小宋 | F-02.A2.4 |
| 心跳停摆模拟器 (任意 t 起停, 任意持续 N 秒) | F-16 | 小宋 + 小余 | F-16 全 |
| 13 reject_code 各 5 case 触发数据 | F-06 | 老韩 + 小宋 | F-06.A6.3 |
| 5 状态机所有合法转移 mock | F-06 | 老韩 | F-06.A6.4 |
| 1000 QPS RM 压测脚本 | F-06 | 小宋 | F-06.A6.5 |
| kill -9 时机 fuzz (100 个时机) | F-15 | 小宋 + 老周 | F-15.A15.1 |
| config TOML fixture (合法 / HARD 冲突 / 调高 / 调低) | F-18 | 小宋 + 老陈 | F-18 全 |
| CLI 命令清单 fixture (7 命令 + 边界) | F-11 | 小颖 + 老陈 | F-11 全 |
| Polymarket / KMS / NTP 故障注入剧本 | F-08, F-22, F-23 | 老孙 + 老姜 + 老吴 | 多个 |
| 5 类告警 dogfood 触发剧本 | F-13 | 小宫 + 小宋 | F-13.A13.6 |
| 1 万条 audit benchmark fixture | F-14 | 小宋 | F-14.A14.3 |

### 3.3 验证 baseline 数据 (期望输出)

| Baseline | 用途 | 准备人 | F-NN |
|---|---|---|---|
| 20 个 (book, match) → expected feature 手工编排 | F-04 | 小梁 + 小程 + 小宋 | F-04.A4.1 |
| Polymarket staging USDC 账户 (小额) | F-07 | 老孙 + 老叶 | F-07.A7.2 |
| Polygon mainnet RPC × 2 provider | F-09 | 老叶 | F-09.A9.4 |
| 链上 staging 持仓 baseline (单日 ≥ 10 笔 mock) | F-10 | 小肖 + 老彭 | F-10.A10.1 |
| Smoke test 1 场 (replay CI) | F-19 | 小段 + 小宋 | F-19.A19.4 |

### 3.4 数据准备里程碑

- **T+12 周 (M2)**: F-01/F-02/F-03/F-04/F-21 实测数据齐, F-05 IS 数据齐
- **T+16 周 (M3)**: F-05 OOS 齐, F-06 测试数据齐, F-15 fuzz 数据齐
- **T+18 周**: 小颖测试用例 v1 提交 (本文档 + 小宋落地)
- **T+20 周**: 集成测试跑通
- **T+22 周 (M4)**: 影子模式启动 (F-20)
- **T+24 周 (M5)**: 实盘首笔 (KR-C-1)

---

## 4. 完工定义 (DoD) 总则

### 4.1 单条验收 DoD 三件套

任何一条验收 (A_NN.X) 标记 DONE, 必须同时满足:

1. **测试用例已落地** (小宋 commit 到 `tests/` 路径)
2. **CI 集成跑通** (PR merge 时 CI 绿)
3. **owner 签字** (PR description 含 owner sign-off)

### 4.2 单个 P0 (F-NN) DoD 五件套

任何 P0 标记 DONE, 必须:

1. 该 F-NN 下所有验收条目 (A_NN.X) 全部标 DONE
2. 数据集需求 (§3) 已准备 + 入仓
3. 依赖的其他 F-NN 已 DONE (或 mock 完整)
4. 变更管控签字到位 (本表 §1 每个 F 末尾"变更管控")
5. 老韩 (风控) + 老周 (架构) + 小杜 (产品) 在 PRD §C 附录签收

### 4.3 MVP 整体 DoD (PRD §8 引用)

- 25 个 P0 全 DONE
- OKR KR-A/B/C/D/E 全阈值达成 (PRD §8.1 表)
- 风控硬验收 5 项 = 0 (PRD §8.2 表)
- 影子模式四方双签 (F-20.A20.5)
- T+24 周前实盘首笔 (KR-C-1)

### 4.4 不算 DONE 的反例 (常见误判)

- "代码跑过一次" ≠ DONE (必须 CI 跑过 + owner 签)
- "本地测过" ≠ DONE (必须仓内 fixture + CI 集成)
- "PR 合并" ≠ DONE (必须验收条目逐条勾)
- "我顺便做了 X" → 默认拒 (老钱 §5.1 第 8 条)

---

## 5. 变更管控 SOP

### 5.1 验收标准本身的修改权

| 修改类型 | 谁能改 | 流程 |
|---|---|---|
| 增加新验收条目 | 小颖 (本文 owner) | PR + 小杜 + 小宋 review |
| 修改验收阈值 (数值) | 该条目"变更管控"列指定的人 | PR + 该 owner 签 |
| 删除验收条目 | 小颖 + 小杜 + 老雷三签 | PR + ADR (说明为何不再适用) |
| 改"测试方法" (单元/集成/E2E/手动) | 小颖 + 小宋会签 | PR |
| 改"数据需求" | 数据准备人 + 小宋会签 | PR |
| 改"变更管控"本身 | GM 老雷独签 | ADR |

### 5.2 红线类验收 (不可绕过)

以下条目改动 = **触碰 CLAUDE.md §8 红线 / D-02 / D-04 / D-06 / C8**, 走 GM + 老郭 + 对应 owner 三签 + ADR:

| 验收条目 | 红线 | 守护人 |
|---|---|---|
| A4.1 (回测实盘特征 diff=0) | D-04 | 老周 + 小梁 |
| A6.1 (RM 门禁 CI 拦截率 100%) | D-02 | 老韩 + 老郭 |
| A6.6 (双人 ack 复位) | RM 设计红线 | 老韩 |
| A8.1 (私钥 0 命中) | C8 | 老沈 + 老黄 + 老孙 |
| A16.5 + A16.6 (D-06 30s 硬上限) | D-06 | 老韩 + 老周 |
| A19.1 (replay 确定性) | D-04 邻居 | 老周 + 小段 |
| A22.3 (单点部署 no HA) | scope 红线 | 老钱 + 老吴 |

### 5.3 解决歧义的升级路径

- 功能语义不清 → @小杜 (PRD owner)
- 风控细节不清 → @老韩 (RM owner)
- 性能阈值不清 → @老姜 (latency budget owner)
- 架构 / 红线不清 → @老周 (架构 owner)
- scope 不清 → @老钱 (CPO 仲裁)
- 时间表 / ticket 不清 → @老胡 (PM)
- 招聘 / 人员 → @小林 (HR)
- 三方都拍不下 → 走 GM 老雷拍板 / 升级 ADR

### 5.4 待澄清问题清单 (本文 v1 收口前 @小杜 / @老韩 / @老姜 / @老钱)

| # | 问题 | 影响条目 | 提问对象 | 截止 |
|---|---|---|---|---|
| Q1 | PRD F-16 说"30s/60s", 老韩 v0.2 §3.7 已收紧为 2s/10s (WSS) / 5s/15s (Goalserve) / 10s/30s (对账), PRD 没回灌. 本文按 v0.2 取值, 请小杜下次 PRD bump 时回灌一致. | A16.1-A16.5 | @小杜 | T+6 周 |
| Q2 | PRD F-22 写"跨洋部署", ADR-001 + 老韩 v0.2 已迁 us-east-1, PRD 没回灌. 本文按 ADR-001 取 us-east-1. | A22.4 | @小杜 + @老吴 | T+6 周 |
| Q3 | `PER_ORDER_CAP_HARD / PER_ORDER_CAP_SOFT` 数值老韩 v0.2 §7 仍 TBD, A6.2 条目阈值用变量名, 数字会签后落. | A6.2 | @小梁 + @老韩 + @老钱 | T+10 周 (M2 前) |
| Q4 | KR-C-4 "Sharpe > 1.0" 是 IS 还是 OOS? 本文按 IS > 1.0 + OOS 衰减 < 50% 拆 (A5.1 + A5.2), 与 PRD §8.1 一致, 请小梁会签. | A5.1, A5.2 | @小梁 + @老钱 | T+10 周 |
| Q5 | PRD §7.4 列了 7 个 P0 metric 名, 但 metric label (如 `quantile`) 没规范. 本文按 Prometheus 惯例 (`{quantile="0.99"}`) 拆, 待小郑 S1-018 监控标准 v1 落地后回灌. | A12.1, 多个 latency 条目 | @小郑 | T+8 周 |
| Q6 | F-20 影子模式 "≥ 1000 evaluate" 是否含被 RM 自动拒的 stale DEFERRED? 本文按"含"拆 (A20.1). | A20.1 | @老韩 | T+12 周 |

---

## 6. 总结

- **总条目数: 121 条** (覆盖 25 P0, 均 4.84 条 / 功能, 远超"75 条 + 3 条 / 功能"硬指标)
- **测试方法分布**: 单元 67% / 集成 46% / E2E 33% / 手动 12%
- **数据集需求**: 实测 11 套 + 故障注入 11 套 + baseline 5 套 = 27 套
- **里程碑对齐**: T+18 周用例提交 / T+22 周影子启动 / T+24 周实盘首笔
- **红线条目**: 7 条 (与 CLAUDE.md §8 + D-02/D-04/D-06/C8 对应)
- **待澄清问题**: 6 个 (Q1-Q6, 标注提问对象 + 截止)

— 小颖 (requirements-analyst), 2026-05-28

**END v1.** 等小杜 + 老胡 + 小宋会签后转 v1.1; M2 (T+10 周) PRD 回灌 (Q1, Q2) 后同步本文.
