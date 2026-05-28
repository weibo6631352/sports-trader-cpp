# `include/stcpp/` — Header-only 公共接口

按 namespace 划分：

| 子目录 | namespace | 用途 | Owner |
|---|---|---|---|
| `infra/` | `stcpp::infra` | WAL / lock-free / 通信原语 / 配置 | 老王 / 小石 / 老周 |
| `risk/` | `stcpp::risk` | RiskGateway / RM v0.3 / 21 reject enum / STALE 5 档 | 老韩 |
| `signer/` | `stcpp::signer` | PaperSigner / LiveSigner / IPC schema (4 ts) | 老孙 |
| `execution/` | `stcpp::execution` | OrderRouter / PaperEngine / ExecutionMode | 小蒋 / 老周 |
| `strategy/` | `stcpp::strategy` | Signal engines / P0-01 Pinnacle no-vig | 小程 / 小梁 |
| `data/` | `stcpp::data` | ETL pipeline / Goalserve / Polymarket adapter | 小余 / 老李 / 小段 |
| `numerical/` | `stcpp::numerical` | SlippageModel / Kelly / SlippageMode {Linear/Sqrt/Clob} | 小肖 |
| `observability/` | `stcpp::observability` | Metrics / tracing / audit emit | 小郑 / 老唐 |
| `test_support/` | `stcpp::test` | Fixtures / mock harness / replay driver | 小宋 |

## 红线提醒（CLAUDE.md §8）

- **R-1**: 任何下单链路绕过 `RiskGateway::evaluate()` = P0
- **R-7**: ExecutionMode build-time + run-time 二选一，同进程不允许动态切换
- **R-11**: paper_* 字段严禁写入 position / pnl_ledger / nonce_ledger
- **R-12**: WebSocket event loop 线程任何同步 REST / 阻塞 IO / 锁 > 100us = P0
- **R-20**: 4 时间戳契约（event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts）

## C++ 版本

C++20（老周 v0.5 + 老何 v1 + GM W-1 锁定）。使用 `tl::expected` polyfill 等价 `std::expected`（C++23 全节点工具链稳定后切）。
