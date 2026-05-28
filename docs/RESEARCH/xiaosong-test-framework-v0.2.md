# 测试 + Replay + Chaos 框架 v0.2 (Sprint-2 W2)

- Owner: 小宋 (test-replay-engineer)
- Date: 2026-05-28 (Sprint-2 W2 落骨架)
- 验收人: 老周 (架构) + 老韩 (RM) + 老雷 (R-20 红线)
- 关联:
  - v0.1: `docs/RESEARCH/xiaosong-test-replay-framework-v0.1.md`
  - RM v0.3: `docs/RESEARCH/laohan-riskmanager-design-v0.3.md` (21 reject enum)
  - 架构 v0.4 §17/§18/§19: `docs/RESEARCH/laozhou-architecture-v0.4.md`
  - paper engine: `docs/RESEARCH/xiaojiang-paper-engine-skeleton-v1.md`
  - SlippageModel: `docs/RESEARCH/xiaoxiao-slippage-model-lib-v1.md`
  - ADR R-12 (WebSocket 不阻塞): `docs/ADR/2026-05-28-gm-redline-websocket-non-blocking.md`
  - ADR R-20 (PIT 时间戳): `docs/ADR/2026-05-28-gm-redline-data-source-timestamping.md`
  - sprint-02 关联 ticket: S2-004 / S2-005 / S2-020 / S2-028
- Sprint-2 W2 交付: v0.2 设计稿 + C++ 接口骨架 + CI grep 5 项 + 21 enum 覆盖矩阵

---

## 0. v0.1 → v0.2 变更摘要 (一段话)

v0.1 立的三层架构 (Unit/Sim/Replay) + gtest + 13 enum 全覆盖 + 覆盖率门禁 全部沿用. v0.2 增量:

| # | 域 | v0.1 立场 | v0.2 新立场 | 触发 |
|---|---|---|---|---|
| 1 | RM enum 覆盖矩阵 | 13 enum × (unit/sim/replay) | **21 enum × (unit/sim/replay+chaos)** | 老韩 v0.3 §3.10 (14 + 5 小肖 + 2 v0.3) |
| 2 | R-12 测试场景 | 通用 chaos network 类 | **R-12 专项 4 场景 (S-1~S-4)**, 量化 50us / single-flight / batch / 4-5 conn burst | 老雷 ADR R-12 + 老周 v0.4 §17.1.1 |
| 3 | R-20 PIT CI | 仅 v0.1 §9.7 一句 "时间不准 ban" | **CI grep job 7 项** (UTC/localtime/int64/snapshot_id + 2 老李 HMAC 教训) | 老雷 ADR R-20 §7 + 老李 HMAC bug 教训 |
| 4 | 物理隔离 | tests/{unit,sim,replay,chaos} 目录 | **CMake target 物理隔离 + nm 双向 grep** (Live/Paper signer 不交叉) | 小蒋 paper engine §1 + 老周 §18 |
| 5 | paper / shadow 测试 | v0.1 未涉及 | **paper-mode 专项 5 case + shadow 4 case** (R-11 WAL 三份隔离) | 老周 §18.6 + §19.6 派单给我 |
| 6 | SAFE_MODE enum | v0.1 §4.2 待会签 | **`STRATEGY_DECAYED` 独立 enum** (RM v0.3 §16), SAFE_MODE 仍复用 STATE_DRAIN | 老韩 v0.3 §16 OQ-D13 |
| 7 | CI 矩阵时间预算 | PR < 5min / nightly < 1h | **PR ≤ 8min / post-merge ≤ 30min / nightly ≤ 3h / weekly ≤ 14h** | sprint-02 任务要求 |

**未变**: gtest (击败 Catch2 v0.1 §9.1 数字理由有效) / VirtualClock / MessagePack framed JSONL / 覆盖率门禁 90% line + 85% branch / golden assertion / 红线演练月度.

---

## 1. 三层测试架构 + CMake target 物理隔离 (v0.2 强化)

### 1.1 目录布局 (v0.1 §1.3 沿用 + 物理隔离层)

```
tests/
├── CMakeLists.txt                  # 顶层, 加 4 个 add_subdirectory + nm-grep test
├── unit/                            # gtest, target: stcpp_test_unit
│   ├── risk/                       # RM 21 enum 全覆盖 (§3)
│   ├── exec/{signer,nonce,fill,ledger}/
│   ├── infra/{ipc,log,clock,net}/
│   ├── data/{ingest,book,normalize,heartbeat}/
│   ├── strategy/{pricing,signal}/
│   └── numerical/                  # 小肖 SlippageModel 7 case
├── sim/                             # target: stcpp_test_sim
│   ├── risk_sim/                   # 21 enum 状态机交互
│   ├── exec_sim/                   # paper/live signer 切换 (小蒋 §2 R-7)
│   ├── feature_sim/
│   ├── boundary_sim/               # L1-L5 依赖图禁边
│   └── r12_sim/                    # R-12 专项 4 场景 (§4)
├── replay/                          # target: stcpp_test_replay
│   ├── golden/                     # 黄金路径
│   ├── edge/                       # STALE 5 档 / fill rate / slippage
│   ├── paper/                      # 小蒋 paper engine 5 case (老周 §18.6)
│   ├── shadow/                     # 小邓 ML shadow 4 case (老周 §19.6)
│   └── incident/                   # 事故复现
├── chaos/                           # target: stcpp_test_chaos
│   ├── network/{wss_disconnect, rest_429, rest_5xx, packet_loss}/
│   ├── clock/{ntp_skew, tsc_drift}/
│   ├── api/{nonce_conflict, ledger_stall, audit_disk_full, gas_spike}/
│   ├── process/{signer_crash, recorder_full, config_bad_reload}/
│   └── r12_chaos/                  # R-12 S-1~S-4 chaos 套件
├── fuzz/{poly_wss_decoder, goalserve_parser, risk_intent_validator}/
├── perf/{risk_evaluate_bench, signer_sign_bench, slippage_bench}/
├── fixtures/                        # §2 fixture 体系
└── helpers/                         # mock harness + virtual clock + intent factory
```

### 1.2 CMake target 物理隔离 (v0.2 新, 联签小蒋 §1)

| target | link 范围 | nm 黑名单 (CI 拒) |
|---|---|---|
| `stcpp_test_unit` | 所有 src/ + gmock | 无 (允许 link 全部, 但禁连真 socket / 真 RPC) |
| `stcpp_test_sim` | mock_clob_server + mock_wss_server + mock_chain_rpc | `polygon_rpc_real`, `chain_submit_real` |
| `stcpp_test_replay` | replay_driver + virtual_clock | 同 sim + `std::this_thread::sleep_for` (静态扫) |
| `stcpp_test_chaos` | replay_driver + fault_provider | 同 replay |
| `stcpp_signer_paper` (跨集成) | paper_signer + virtual_matcher + slippage | `eip712_real`, `chain_rpc_submit`, `eth_signTransaction` |
| `stcpp_signer_live` | real_signer + nonce_mgr | (无 ban, 但 secret 限定到 SecureBuffer) |

**红线**: PR 通过 = `nm $TARGET | grep -E '<blacklist>'` 全 0. 老练 S2-028 CI hard block 已派.

---

## 2. RM 21 reject enum 全覆盖矩阵 (v0.2 强化)

### 2.1 21 enum × 覆盖 (老韩 v0.3 §3.10 完整列表)

| # | reject_code | 触发规则 | unit | sim | replay/chaos | owner |
|---|---|---|---|---|---|---|
| 1 | `STATE_HALTED` | R0 | u01 | s01 | chaos/network/wss_long_disconnect | 我 + 老韩 |
| 2 | `STATE_DRAIN` | R0 | u02 | s02 | replay/edge/drain_mode_close_only | 我 + 老韩 |
| 3 | `DUPLICATE_INTENT` | R1 (idempotency hit) | u03 | s03 | replay/edge/strategy_restart_retry | 我 + 老韩 |
| 4 | `STALE_DATA` | R2 (5 档 × WSS+Goalserve+对账) | u04a-e | s04 | chaos/network/goalserve_30s_stall | 我 + 小袁 |
| 5 | `INVALID_INTENT` | R3 + book_snapshot_ts_ns=0/<now-60s + NaN | u05a-d | s05 | fuzz/risk_intent_validator | 我 + 小肖 |
| 6 | `EXCEED_PER_ORDER_CAP` | R4 | u06 | s06 | replay/edge/big_intent_soft_cap | 我 + 老韩 |
| 7 | `EXCEED_MARKET_EXPOSURE` | R5 | u07 | s07 | replay/edge/exposure_climb | 我 + 老韩 |
| 8 | `DAILY_LOSS_HALT` | R6 | u08 | s08 | replay/incident/daily_loss_trigger | 我 + 老韩 |
| 9 | `CONSEC_LOSS_HALT` | R7 | u09 | s09 | replay/edge/n_consec_losses | 我 + 老韩 |
| 10 | `EDGE_CI_NEGATIVE` | R8 | u10 | s10 | replay/edge/ci_lower_zero | 我 + 老韩 |
| 11 | `INSUFFICIENT_BANKROLL` | R9 | u11 | s11 | replay/edge/drawdown_then_intent | 我 + 老韩 |
| 12 | `MARKET_TYPE_NOT_ENABLED` | R3 子项 | u12 | s12 | replay/edge/totals_intent_during_mvp | 我 + 老韩 |
| 13 | `INTERNAL_ERROR` | 全局兜底 | u13 (EXPECT_DEATH) | s13 | chaos/api/audit_disk_full | 我 + 老韩 |
| 14 | `AET_SIGN_FAILED` | signer B5 (D-11) | u14 | s14 (mock SignerError) | chaos/process/signer_b5_reject | 我 + 老孙 |
| 15 | `LOW_FILL_RATE` | R10 (小肖 §1) fill_rate < 0.50 | u15 (复用小肖 case#6) | s15 | replay/edge/fill_rate_floor | 我 + 小肖 |
| 16 | `EXCESSIVE_SLIPPAGE` | R10 slippage_ticks > 3 | u16 | s16 | replay/edge/slippage_3tick | 我 + 小肖 |
| 17 | `EDGE_NEGATED_BY_SLIPPAGE` | R10 net edge ≤ 0 | u17 | s17 | replay/edge/edge_negated_post_slip | 我 + 小肖 |
| 18 | `EXCEED_BOOK_DEPTH` | R10 rho > 3.0 | u18 (复用小肖 case#3) | s18 | replay/edge/rho_max_3 | 我 + 小肖 |
| 19 | `INVALID_INTENT` (v0.3 强化, book_ts) | R3+ts 检测 | u19 (复用小肖 case#7a-e) | s19 | fuzz extension | 我 + 小肖 |
| 20 | `STRATEGY_DECAYED` | R12 Bayesian BLACK | u20 (file watcher 4 子 case) | s20 (file unavailable → DEFERRED) | chaos/api/decay_file_corrupt | 我 + 小董 |
| 21 | (储备位) | 新增 enum 必走 ADR + 同 PR 加 3 case | — | — | — | — |

(#5 与 #19 同 enum, #19 是其强化覆盖. CI 视为同 enum 至少 3 case 全覆盖即可.)

### 2.2 CI 拦截脚本 `tools/ci/risk_enum_coverage.py` (v0.2 升级)

```
扫 src/risk/RejectReason.h enum
扫 tests/unit/risk/    grep EXPECT_REJECT_CODE(X)
扫 tests/sim/risk_sim/ grep EXPECT_REJECT_CODE(X)
扫 tests/replay/ tests/chaos/ grep EXPECT_REJECT_CODE(X)
每个 enum 必须三层都命中 (unit + sim + (replay OR chaos)).
新增 enum 同 PR 必带 3 case (unit + sim + replay/chaos), 否则 exit 1.
报告: tests/build/coverage_matrix.md (Markdown 表, PR comment 自动贴)
```

**新增**: AET_SIGN_FAILED 不走 evaluate, sim 用 gmock 在 signer 出口注入 SignerError, RM audit emit 校验.

---

## 3. R-12 测试 4 场景 spec (v0.2 新, 老雷 ADR §5 派单 + 老周 §17.1.1)

R-12 红线: "WebSocket event loop 线程禁任何同步 REST / 阻塞 IO / 锁 > 100us." 4 场景全跑 `tests/sim/r12_sim/` + `tests/chaos/r12_chaos/`.

### 3.1 S-1: REST 5s 慢响应, WSS event loop tick 保持 < 50us

```
背景: T4 bg_rest_worker_pool 故意注入 5s sleep (mock REST server delay).
SUT: T0a wss_poly_market_hot_reactor (vCPU0).
注入: 在 mock REST server 加 ResponseDelayProvider(5s).
负载: 同时 Polymarket market WSS 推送 1k msg/s.
断言:
  EXPECT 每条 msg processing latency p99 < 50us  (老雷 R-12 红线 + 老陈 派单)
  EXPECT 5s 内 wss_in_ring_market_hot push count ≥ 4500 (无阻塞)
  EXPECT T4 worker pool thread 处于 5s sleep 但 vCPU0 reactor 无任何 syscall 阻塞 (strace 验)
工具: google-benchmark + perf record + ftrace
```

### 3.2 S-2: 1000 strategy 同时要同一 market data → single-flight 1 个 REST

```
背景: 老李 api-call-optimization-v1.1 + 老雷 R-12 §3 "single-flight".
SUT: T3 strategy_engine + T4 bg_rest_worker_pool single-flight cache.
注入: 同时 1000 个并发 request fetch_market_book(market_id="0xabc").
断言:
  EXPECT mock_clob_server.recv_count("/book?id=0xabc") == 1  (折叠了 999 个)
  EXPECT 所有 1000 caller 收到同一份 BookSnapshot (deep_equal)
  EXPECT cache hit ratio ≥ 99.9% 在 burst 后续 100ms 内
工具: gtest + mock_clob_server counter + 自研 SingleFlightAssertion
```

### 3.3 S-3: WSS 断线 + 5 markets, REST 兜底 1 个 batched 调用

```
背景: 老雷 R-12 §3 "WebSocket 优先, REST 兜底, bulk endpoint 优先" + 老李 v3 /books 池.
SUT: T0a + T4 + book_builder.
注入: WssDisconnectProvider 模拟所有 5 个 hot market 同时断流, 但 T0c user channel 保持.
断言:
  EXPECT mock_clob_server.recv_count("/books") == 1  (5 个 market_id 合并到 1 个 batched POST)
  EXPECT mock_clob_server.recv_count("/book?id=...") == 0  (绝不发 5 个单独的)
  EXPECT 重连完成时 (≤ 30s watchdog), book_builder 已有这 5 个 market 的 fresh snapshot
  EXPECT 期间 T0c user channel 故障域独立, 不受影响 (D-05 红线)
工具: chaos/r12_chaos/wss_burst_disconnect_batched_recovery.yaml + ReplayDriver + mock_clob_server
```

### 3.4 S-4: vCPU0 4-5 connection burst 不阻塞 (老周 §17.1.1 OQ-R12-7)

```
背景: 老周 v0.4 §17.1.1 收口 4-5 conn (T0a market_hot + T0b market_cold 1-2 + T0c user + T1 polygon).
SUT: vCPU0 单 asio reactor, 4-5 coroutine.
注入: 4 conn 同时 burst 1k msg/s 持续 60s (msg = Polymarket book L1 update, ~200 bytes).
断言:
  EXPECT 每条 msg processing latency p50 ≤ 20us / p99 ≤ 50us / p99.9 ≤ 100us  (S2-011 D-07 红线 拍板值)
  EXPECT vCPU0 user time / kernel time ratio ≥ 80% (kernel time = 阻塞 syscall, 不该高)
  EXPECT 4 个 ring (market_hot/cold/user/polygon) 互不串流 (D-05 + §17.1.2 不变量 1)
  EXPECT 任一 ring 满 drop 时其他 ring 仍正常推进 (§17.1.2 不变量 2)
工具: perf record on vCPU0 + ring counter + ftrace tracepoint events
失败处置: 老钱 §5.2 战略升级 (MVP 单 sport NBA only)
```

**通用 R-12 静态扫 (老练 S2-028)**:
- `git grep -nE 'co_await|sync_wait|future<.*>::get' src/infra/net/wss/` 必须 0 命中 (vCPU0 reactor 不许阻塞 wait)
- `nm stcpp_trader | grep -E 'http_get|http_post' | xargs -I{} addr2line {}` 不能命中 vCPU0 thread

---

## 4. R-20 PIT CI grep job (v0.2 新, 老雷 ADR §7 派单)

集成到 `.github/workflows/pr.yml` 的 `r20-pit-grep` job (≤ 30s). 任一命中 = PR reject.

### 4.1 拦截清单 7 项

| # | 拦截规则 | 命令 (示意) | 教训源 |
|---|---|---|---|
| 1 | `now()` 不带显式 UTC | `git grep -nE '\bnow\(\)' src/ \| grep -v 'utc_now\|UtcNow\|monotonic'` | 老雷 ADR R-20 §7 |
| 2 | `localtime()` / `mktime()` 出现 | `git grep -nE '\b(localtime\|mktime\|gmtime)\b' src/` | 老雷 R-20 §7 (本地时区毒素) |
| 3 | 时间相关 `int` (应 int64) | `git grep -nE '(int\|int32_t)\s+\w*(ts\|timestamp\|time_ms\|time_ns)\b' src/` | 老雷 R-20 §7 (防溢出 2038) |
| 4 | signal struct 缺 `feature_snapshot_id` | python AST 扫 `src/strategy/signal/*.h` 含 `struct.*Signal\|struct.*Candidate` 必含 `feature_snapshot_id` | 老雷 R-20 §7 + 小邓 ML-R8 |
| 5 | base64 `urlsafe_b64encode().rstrip(b"=")` (老李 HMAC bug 4) | `git grep -nE 'b64encode.*rstrip\(b"="\)\|urlsafe_b64encode.*rstrip' src/ tools/` | 老李 HMAC bug 4 教训 (padding 必留) |
| 6 | `param_type` 出现 (老李 HMAC bug 3) | `git grep -nE '\bparam_type\b' src/ tools/` | 老李 HMAC bug 3 教训 (字段名不一致) |
| 7 | 4 时间戳不等式断言缺失 | `git grep -nE 'OrderIntent\s+intent' src/exec/signer/ \| xargs grep -L 'assert.*event_ts.*<=.*data_source_ts'` | 老雷 R-20 §7 PIT 断言 |

### 4.2 工具实现 `tools/ci/r20_pit_grep.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail
RC=0
check() { local rule=$1; local pattern=$2; local scope=${3:-src/}
  if git grep -nE "$pattern" -- $scope > /tmp/r20_$rule.out; then
    echo "R-20 violation [$rule]:"; cat /tmp/r20_$rule.out; RC=1
  fi
}
check now_no_utc       'now\(\)'                src/  # 简化版, 真实加 -v utc 过滤
check localtime        '(localtime|mktime|gmtime)\b'  src/
check int_for_time     '(\bint\b|\bint32_t\b)\s+\w*(ts|timestamp|time_ms|time_ns)\b' src/
check b64_rstrip       'b64encode.*rstrip\(b"="\)|urlsafe_b64encode.*rstrip'  'src/ tools/'
check param_type       '\bparam_type\b'         'src/ tools/'
python3 tools/ci/check_signal_struct_has_feature_snapshot_id.py  || RC=1
python3 tools/ci/check_signer_has_pit_assert.py                  || RC=1
exit $RC
```

### 4.3 集成到 GitHub Actions

```yaml
# .github/workflows/pr.yml (片段)
jobs:
  r20-pit-grep:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: R-20 PIT 7 项 grep
        run: bash tools/ci/r20_pit_grep.sh
      - name: comment violations
        if: failure()
        run: gh pr comment ${{ github.event.pull_request.number }} -F /tmp/r20_*.out
```

PR 任一 grep 命中 = block. 老雷 ADR R-20 §4 "任何 PR 引入'无时间戳'的数据流 → 直接 reject" 落地.

---

## 5. 覆盖率门禁 (v0.1 §5 沿用 + v0.2 微调)

| 范围 | line | branch | 备注 |
|---|---|---|---|
| `src/risk/**` | ≥ 90% | ≥ 85% | 21 enum 全覆盖兜底 |
| `src/exec/signer/**` (live + paper + backtest) | ≥ 90% | ≥ 85% | paper 路径必跑 |
| `src/exec/nonce/**` | ≥ 90% | ≥ 85% | 老叶 nonce_mgr |
| `src/exec/recon/**` | ≥ 90% | ≥ 80% | 对账 |
| `src/numerical/slippage/**` | ≥ 95% | ≥ 90% | 小肖 7 case + bench |
| `src/infra/clock/**` | ≥ 85% | ≥ 80% | R-20 / R-12 高频 |
| `src/data/heartbeat/**` | ≥ 90% | ≥ 85% | STALE 5 档 |
| 整体 `src/` | ≥ 70% | ≥ 60% | MVP 起步 |

**不允许下降**: PR 改 risk/signer/nonce/slippage, 关联文件覆盖率不得低于阈值且不得回退. 工具 `tools/ci/coverage_diff.py` PR comment 自动贴.

---

## 6. CI 矩阵 (v0.2 时间预算)

| 阶段 | 触发 | 内容 | 时长 | 失败影响 |
|---|---|---|---|---|
| **PR pre-merge** | push | lint + clang-tidy + r20-pit-grep + unit + sim smoke + ASAN/UBSAN unit + nm-blacklist | **≤ 8min** | 拒 merge |
| **post-merge to main** | merge | 全套 unit + sim + replay smoke (5 golden) + ASAN/UBSAN/TSAN matrix + risk_enum_coverage + R-12 S-1 微负载 | **≤ 30min** | alert + 不 promote artifact |
| **nightly** | cron 03:00 UTC | replay full + chaos smoke + R-12 S-1~S-4 full + perf regression + coverage report | **≤ 3h** | alert |
| **weekly** | cron Sun 02:00 UTC | fuzz 12h + chaos full + 跨平台 build + R-20 完整 + ML shadow stat | **≤ 14h** | alert |
| **red-line drill** | 月度 | 绕过 RM 尝试 + 21 enum 全 sim + R-12 burst + 红线演练 | 半天 | 老韩 + 我负全责 |

---

## 7. 派单 / 待签 (v0.2)

| 我问谁 | 问题 | deadline |
|---|---|---|
| @老王 | WAL framework `ReplayWalReader` adapter 接口 (我消费 audit.wal / paper_audit.wal / shadow_audit.wal 三份) | Sprint-2 W2 末 |
| @老韩 | enum #21 储备位是否预留 / SAFE_MODE_PENDING_RECON 是否独立 enum | W2 末 (跟 v0.3 sign-off 一起) |
| @老周 | R-12 S-4 失败时降级 SOP (单 sport NBA only) 测试集是否独立 maintain | W3 |
| @老雷 | R-20 §7 "缺 feature_snapshot_id" CI 是否允许 `// COV-EXCLUDE` 例外 | W2 |
| @小蒋 | paper_audit.wal schema 锁定时间 (我接 ReplayDriver decoder) | S2-020 W3 |
| @小邓 | shadow_audit.wal schema 锁定 | Sprint-3 |
| @小肖 | SlippageModel 7 case 我 unit 复用还是 sim 重写一份 | W2 中 |
| @老练 | r20-pit-grep job 接入 .github/workflows/pr.yml 时间窗口 | W2 末 |

---

## 8. 完成 checklist (W2 设计) + W3 落代码

W2: v0.2 + cpp skeleton + 21 enum 矩阵 + R-12 4 场景 + R-20 grep 7 项 + CMake target + CI 预算 全闭环.
W3 落代码: tests/ 目录 skeleton + `tools/ci/r20_pit_grep.sh` + `risk_enum_coverage.py` + 老王 WAL adapter 联调 + 老练 CI 接入.

---

**END v0.2.** 等老韩 v0.3 sign-off + 老王 W2 末 WAL framework + 老练 W3 CI 接入 → Sprint-2 W3 bump v0.3 落代码.

— 小宋, Sprint-2 W2
