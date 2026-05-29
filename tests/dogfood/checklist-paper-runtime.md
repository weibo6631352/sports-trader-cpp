# Paper Runtime Dogfood 检查单 — 端到端 6 阶段

- **Owner:** 小宫 (dogfood-evaluator)
- **Last review:** 2026-05-29
- **对齐:** GM-PAPER-G 门禁 (G-00~G-08) + M1 acceptance spec v2 + ADR R-11/R-12/R-20
- **视角:** 真实操作员第一次上手 paper runtime 的完整操作流程

---

## 阶段 0 — 启动前预检 (Operator 视角)

操作员拿到 paper runtime binary，上手前确认环境。

| # | 检查点 | 预期结果 | 验收 | 关联 |
|---|---|---|---|---|
| 0-1 | `stcpp_trader --version` 输出 build_mode=paper | stdout 含 `mode: paper` | 能独立执行 | PR-1 同 binary |
| 0-2 | binary 无 Python 依赖 `ldd stcpp_trader \| grep -i python` | 输出为空 | CI 通过 | PR-8 |
| 0-3 | `nm stcpp_signer_paper \| grep -E "RealSigner\|chain_rpc"` | 无匹配 | CI 通过 | PR-7 |
| 0-4 | paper_config.toml 与 prod_config.toml 红线字段 hash 一致 | `stcpp_check_redline` exit 0 | 可观察 | PR-2 |
| 0-5 | `.env` 内 WALLET_PRIVATE_KEY 存在但 paper binary 代码不读取 | grep stcpp_signer_paper 源码无 getenv("WALLET_PRIVATE_KEY") | 安全审查 | §10.2 部署洁净 |

**体感:**
- 操作员能否 5 分钟内看懂 `--help` 并知道 `--mode=paper` 入口?
- 配置文件位置 `/etc/stcpp/paper_config.toml` 文档是否有明确说明?

---

## 阶段 1 — 数据接入 (Data Ingest)

paper runtime 连接真实数据源，验证数据流通。

| # | 检查点 | 预期结果 | 验收 | 关联 |
|---|---|---|---|---|
| 1-1 | Polymarket WSS 连接建立 | `GET /metrics` 中 `stcpp_wss_connected{source="polymarket"}` == 1 | 可观测 | M1-B01 |
| 1-2 | Goalserve inplay 连接建立 | `stcpp_wss_connected{source="goalserve"}` == 1 | 可观测 | M1-B02 |
| 1-3 | 数据 staleness 正常 | `stcpp_data_staleness_seconds{source="polymarket"}` < 5s | Prometheus | M1-B03 |
| 1-4 | 4 时间戳契约 (R-20) | `GET /data/latency/{market_id}` 返回 4 ts 各段 ≥ 0 且无倒挂 | /api/v1/ | R-20 |
| 1-5 | 没有 silent data drop | `stcpp_data_gap_total{source="goalserve"}` counter 无异常增长 (1 分钟内 ≤ 3) | Prometheus | R-20 |
| 1-6 | book snapshot 写入 L2 DATA | `GET /status` 返回 book_count > 0 | 观测 API | M1-B01 |
| 1-7 | WSS 断连后自动重连 | 手动 kill WSS mock，`stcpp_wss_reconnect_total` counter +1，连接恢复 | chaos | M1-G01 |

**体感:**
- 数据断流时 dashboard 是否有明显告警？
- 操作员能否从 `/stream/events` 实时看到数据状态变化？

---

## 阶段 2 — 信号与 RM (Signal + RiskManager)

信号触发，RM 正确评估，审计链路完整。

| # | 检查点 | 预期结果 | 验收 | 关联 |
|---|---|---|---|---|
| 2-1 | P0-01 信号触发后产生 OrderIntent v0.5 | intent 含 condition_id / token_id / outcome / side 四字段 | unit test | M1-C01 ABI-UPDATE |
| 2-2 | 100% OrderIntent 经 RiskManager | `signer.wal audit_id 数 == risk.wal allow 数` (1h 窗) | WAL audit | M1-A03 |
| 2-3 | RM 拒单率在 [8%, 20%] | `stcpp_rm_decision_total{decision="reject"}` / total ∈ [0.08, 0.20] | Prometheus | G-07 |
| 2-4 | STALE_DATA 拒单正确触发 | 注入 stale book → RM reject STALE_DATA | 集成测试 | RM v0.3 5-档 |
| 2-5 | RM HALTED 状态下 0 单通过 | set state=HALTED → all reject STATE_HALTED | 集成测试 | M1-G04 |
| 2-6 | audit_id 全链路非零且唯一 | `GET /trace/recent?n=50` 所有 intent_id 不重复，无全零 | 观测 API | M1-E01 |
| 2-7 | 4 ts 违例拒单 | data_source_ts < event_ts → INVALID_INTENT.TS_ORDER_VIOLATED | unit test | M1-D04 |
| 2-8 | token_id / condition_id 缺失拒单 | 空 token_id → MISSING_TOKEN_ID | unit test | M1-D04 ABI-UPDATE |
| 2-9 | RM 单次 evaluate p99 < 100us | 100 笔连续 evaluate 实测 | R-12 sim | M1-F04/R-12 |

**体感:**
- 操作员能否通过 `/risk/rejects?n=50` 快速看到最近拒单原因，不需要翻日志？
- 拒单 reason_code 是否足够清晰，一看就知道是什么问题？

---

## 阶段 3 — VirtualMatcher 与 PnL (Paper Fill + Ledger)

虚拟成交模拟，paper ledger 更新，PnL 计算。

| # | 检查点 | 预期结果 | 验收 | 关联 |
|---|---|---|---|---|
| 3-1 | paper fill 不写真账本 | `position.wal` 和 `nonce_ledger.wal` 无新写入 (1h 窗) | R-11 | M1-D02/D03 |
| 3-2 | paper_audit.wal 正常写入 | paper fill 后 paper_audit HighWatermark 增长 | WAL | R-11 |
| 3-3 | VirtualFill 4 ts 透传 | fill.event_ts_ns == book.event_ts_ns，4 ts 全链路单调 | 集成测试 | R-20 |
| 3-4 | VirtualFill slippage 公式合理 | `(fill_price - quote_price) / quote_price` < 30bps | M1-D05 | P3 |
| 3-5 | 模拟 fill rate Bernoulli | fill_total / order_total 在 [0.3, 0.9] (mock book 条件下) | 观测 API | M1-D05 |
| 3-6 | fee 正确扣除 | net_pnl = gross - fee (fee = size_pUSD_micro * 0.03 * price * (1-price)) | PnL 看板 | G-02 |
| 3-7 | paper ledger WAL CRC32C 完整 | 停止进程后 WAL replay 不报错，bankroll 重建一致 | crash 恢复 | §5.4 WAL fsync |
| 3-8 | size_pUSD_micro 单位正确 (非旧 size_usdc_micro) | VirtualFill.size 字段名 == size_pUSD_micro | ABI 检查 | M1-D05 ABI-UPDATE |

**体感:**
- 操作员能否从 `/api/v1/paper/ledger` 或等效 endpoint 实时看到虚拟持仓和 PnL？
- paper 模式下 PnL 数字是否有明确的"这是虚拟的"标识，不会误以为是真实盈亏？

---

## 阶段 4 — 观测 API 与 PnL 看板 (Observability)

6 类观测 endpoint 验收，PnL 看板能出数字。

| # | 检查点 | HTTP 请求 | 预期响应 | 关联 |
|---|---|---|---|---|
| 4-1 | 系统健康 | `GET /healthz` | HTTP 200, `ok: true`, `as_of_ts > 0` | 小郑 obs v1 §1 |
| 4-2 | 版本信息 | `GET /version` | `build_mode: "paper"` | 小郑 obs v1 |
| 4-3 | 系统状态 | `GET /status` | `state: RUNNING/DRAIN/HALTED`, `mode: paper` | 小郑 obs v1 |
| 4-4 | Prometheus metrics | `GET /metrics` | 含 `stcpp_uptime_seconds`, `stcpp_pnl_usd{mode="paper"}` | 小郑 obs v1 §1 |
| 4-5 | RM 拒单流 | `GET /risk/rejects?n=50` | JSON 数组，每条含 intent_id/reason_code/bankroll | 小郑 obs v1 §4 |
| 4-6 | 单笔 trace | `GET /trace/{intent_id}` | span 树: signal.emit→rm.evaluate→signer.sign→exec.match | 小郑 obs v1 §2 |
| 4-7 | 最近 trace 列表 | `GET /trace/recent?n=50` | 50 条 trace 摘要，intent_id 不重复 | 小郑 obs v1 §2 |
| 4-8 | GM-PAPER-G 实时仪表 | `GET /gate/paper` | window_days/n_trades/sharpe_30d/prelim_pass/confirm_pass | 小郑 obs v1 §3 |
| 4-9 | 事件流 WSS | `WS /stream/events` | 连接后收到 RM 拒单/STATE_TRANSITION 事件 | 小郑 obs v1 §4 |
| 4-10 | 日志查询 | `GET /logs?level=warn&limit=20` | JSON lines，无私钥/明文凭证字段 | 小郑 obs v1 §5 |
| 4-11 | 数据延迟分解 | `GET /data/latency/{market_id}` | 4 ts 各段延迟 > 0，无倒挂 | 小郑 obs v1 §6 |

**体感:**
- 操作员能否仅凭 `/gate/paper` 一个 endpoint 判断"现在离 GM-PAPER-G 通过还差多少"？
- `prelim_pass` 和 `confirm_pass` 两阶段是否清晰区分，不会造成误判？

---

## 阶段 5 — 持续运行与 GM-PAPER-G 门禁对齐

paper runtime 稳定运行，验证六维 gate 数据是否可出。

| # | 检查点 | 度量方式 | 通过标准 | Gate |
|---|---|---|---|---|
| 5-1 | 连续运行 14 日 (G-00 前置) | `as_of_ts - start_ts` / 86400 ≥ 14 | `GET /gate/paper` window_days ≥ 14 | G-00 |
| 5-2 | 在线率 ≥ 99.0% | `stcpp_uptime_seconds / total_seconds` | Prometheus 计算 | G-01 |
| 5-3 | 30 日净 PnL > 0 | `GET /gate/paper` net_pnl_usd | > 0 (扣 fee+slippage+spread) | G-02 |
| 5-4 | n_trades ≥ 100 | `GET /gate/paper` n_trades | ≥ 100，覆盖 ≥ 10 交易日 | G-03 |
| 5-5 | 正收益日占比 ≥ 52% | `GET /gate/paper` positive_day_ratio | ≥ 0.52 | G-04 |
| 5-6 | OOS Sharpe ≥ 0.5 | `GET /gate/paper` sharpe_30d | ≥ 0.5 | G-05 |
| 5-7 | pregame Moneyline 分桶 PnL > 0 | VirtualFill market_type 标签 + 分桶汇总 | pregame 分桶单独 > 0 | G-06 |
| 5-8 | RM 零绕过 + paper 零污染 | WAL audit + CI grep | 绕过 == 0 AND R-11 cross-write == 0 | G-07 |
| 5-9 | 数据 attestation 可出 | R-20 4ts 单调 + look-ahead 检测 + join 0 孤儿 | attestation checklist 全绿 | G-08 |

**体感:**
- `/gate/paper` 的 `prelim_pass` / `confirm_pass` 两阶段是否足够直观？
- 哪项 gate 先亮红灯时，操作员能否快速定位到具体原因？

---

## 阶段 6 — 边缘场景 (Chaos Dogfood)

真实使用中的意外情况。

| # | 场景 | 操作步骤 | 预期系统行为 | 体感指标 |
|---|---|---|---|---|
| 6-1 | WSS 断连 | kill Goalserve WSS mock | 自动重连，staleness 告警，RM SAFE_MODE (若超阈) | 恢复时间 < 30s |
| 6-2 | paper signer 子进程 crash | kill stcpp_signer_paper | trader 进入 SAFE_MODE + 重连，不崩溃 | SAFE_MODE 0 数据丢失 |
| 6-3 | 数据 staleness 超阈 | 停止 mock book feed 5 秒 | RM STALE_DATA 拒单，`/stream/events` 推送告警 | 恢复后自动 RUNNING |
| 6-4 | RM HALTED 状态 | `POST /debug/rm/set_state HALTED` | 全部新 intent reject STATE_HALTED，/status 反映 | 恢复需 operator 手动触发 |
| 6-5 | paper ledger WAL 模拟损坏 | truncate paper_ledger.wal | replay 报错，系统拒绝启动而非静默忽略 | 明确 error log，不静默 |
| 6-6 | 高频信号 burst (100 笔/s) | harness 发送 100 个 intent | RM p99 < 1ms，event loop 无阻塞 (R-12) | metrics 无异常 spike |
| 6-7 | 网络高延迟 (跨洋链路模拟) | tc netem add 200ms delay | 数据 staleness 增加，4 ts lag histogram 反映 | 系统不崩溃，有可观测数字 |

---

## 检查单使用说明

1. **新人上手验证:** 0-1 ~ 4-11 全部过 → 操作员可独立上手 paper runtime
2. **W11 上线准入:** 所有阶段 0-5 过 (6 为补充项) → 可进入 14 日 G-00 软验证窗口
3. **GM-PAPER-G 预热:** 阶段 5 全过 → 具备进入 30 日正式 gate 窗口条件
4. **评分说明 (体感，1-10):**
   - 10: 操作员 0 培训独立完成
   - 7-9: 看文档即可完成
   - 4-6: 需要内部协助
   - 1-3: 卡点多，需要研发在场

---

*小宫 (dogfood-evaluator, E 产品业务保障部), 2026-05-29*
*关联 W11 paper runtime 上线准入卡 — 本检查单完整过才发放准入*
