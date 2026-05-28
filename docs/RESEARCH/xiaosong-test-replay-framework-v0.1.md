# 测试 + Replay 框架 v0.1

- Owner: 小宋 (test-replay-engineer)
- Last review: 2026-05-28
- 验收人: 老周 (chief-architect) + 老韩 (risk-engineer)
- 关联 ticket: S1-014 (本文) / S1-001 (架构) / S1-002 (Polymarket API) / S1-004 (RM) / S1-016 (威胁模型) / S1-018 (observability)
- 关联依赖: 小宫 (dogfood 剧本, 待出) / 小余 (历史数据 schema) / 小段 (Goalserve samples) / 老李 (Polymarket samples) / 老吴 (CI/CD 部署流水线)
- 状态: DRAFT, 待会签
- Sprint-1 末交付: 框架 v0.1 + 第一批 fixture + RM 13 拒单 enum 全覆盖测试骨架

---

## 0. TL;DR (老雷 + 老周 + 老韩看这一段)

- **三层架构:** Unit (gtest) → Integration / Sim (gtest + 自研 harness) → E2E / Replay (自研 ReplayDriver).
- **Unit 框架选 gtest, 不选 Catch2.** 数字理由见 §9.1.
- **Replay 输入格式: MessagePack framed JSONL** (v0.1 双格式并存, v0.2 砍 JSONL). 与小余 EventRecorder 输出对齐 (待 @小余 会签).
- **Chaos 框架内嵌于 Replay:** 同一 EventStream, 注入 fault provider (网络 / 时钟 / API 限流) → 黑盒看 RM/exec 行为.
- **RM 13 拒单 enum 强制全覆盖 + CI 拦截:** 每个 reject_code 必须至少 1 个 unit test + 1 个 sim test. 未覆盖的 enum 项 CI 拒 merge.
- **覆盖率门禁:** 核心 (`risk/`, `exec/signer`, `exec/nonce`) ≥ 90% line + 85% branch; 整体 ≥ 70% line. PR < 阈值拒 merge.
- **CI 矩阵:** PR fast (< 5min unit + lint + sanitizer) → nightly long (replay 30min + chaos 1h + 压测 1h) → weekly fuzz (12h ASAN/UBSAN/MSAN).
- **绕过检测:** signer 入口必须前序经 RM, 静态扫描 + AST grep + 运行期 instrumentation 三重拦截 (与老沈 TB-B + 老韩 §6 配套).
- **第一个该跑的测试 case:** `risk::EvaluatePolicy/EXCEED_PER_ORDER_CAP_basic` — 单笔超 hard cap 必拒, 验 RM 红线最硬一条规则不通过任何"软放行". 见 §11.

---

## 1. 三层测试架构

### 1.1 分层定义 + 跑频

| 层 | 范围 | 框架 | 单次跑时长 | 跑频 | 失败影响 |
|---|---|---|---|---|---|
| **Unit** | 单个 class / free function | gtest + gmock | 每个 case < 50ms, 全套 < 5min | PR 必跑 | 拒 merge |
| **Integration / Sim** | 2-5 个模块组合 + mock 外部 IO | gtest + 自研 harness (`SimHarness`) | 单个 case < 5s, 全套 < 15min | PR 必跑 (smoke 子集) + nightly full | smoke 拒 merge / nightly 报警 |
| **E2E / Replay** | 全链路 + 历史事件流回放 | 自研 `ReplayDriver` + `ReplayAssertion` | 单次 5-30min | nightly | 报警 + 阻断下一次 release |
| **Chaos** | E2E 子类型, 加 fault injection | `ReplayDriver` + `FaultProvider` | 单次 30min - 2h | nightly + 红线演练 (月度) | 报警 + 阻断 release |
| **Fuzz** | 协议解析 / RM 输入 | libFuzzer + ASAN | 12h+ | weekly | 报警 |
| **Perf regression** | 热路径 p99 | google-benchmark + 内部 harness | 30min | nightly | 退步 > 10% 报警 |

### 1.2 测试金字塔比例 (目标)

| 层 | case 数 (目标) | 行覆盖贡献 | 跑时占比 |
|---|---|---|---|
| Unit | 80% | 60% | 10% |
| Sim | 15% | 25% | 25% |
| Replay/Chaos | 4% | 12% | 50% |
| Fuzz | 1% | 3% | 15% |

**反 pattern (拒绝):** 把所有逻辑塞进 E2E "因为方便". E2E 跑慢且 flaky, 出错难定位. 每发现一个 E2E only 的 case, 必须开 unit/sim 子 case 补 (跟踪 ticket).

### 1.3 测试目录布局 (与老周架构附录 A 对齐)

```
tests/
  unit/                   # gtest, 镜像 src/ 目录结构
    infra/{ipc,log,clock,...}
    data/{ingest,book,feature,...}
    strategy/{pricing,signal,...}
    risk/                 # 老韩的内部 + 我的 enum 全覆盖
    exec/{signer,nonce,fill,...}
  sim/                    # SimHarness 驱动
    risk_sim/             # RM 13 拒单 enum × 状态机 × sizing
    exec_sim/             # nonce 治理 / 签名链路 / fill 回灌
    feature_sim/          # FeaturePipeline 黄金路径
    boundary_sim/         # L1-L5 边界契约 (依赖图禁边检测)
  replay/                 # ReplayDriver 驱动
    golden/               # 黄金路径 case (历史成交)
    edge/                 # 边缘 case (限流 / 断流 / 仲裁延迟)
    incident/             # 事故复现 (与 docs/INCIDENTS/ 对应)
  chaos/                  # 含 FaultProvider 注入剧本
    network/              # WSS 断, REST 429
    clock/                # NTP 漂移, RDTSC 失校
    api/                  # gamma 502, clob 5xx, RPC 切换
    process/              # signer 子进程 crash, recorder 满
  fuzz/                   # libFuzzer 入口
    poly_wss_decoder/
    goalserve_parser/
    risk_intent_validator/
  perf/                   # google-benchmark
    risk_evaluate_bench/
    book_builder_bench/
    feature_pipeline_bench/
  fixtures/               # 测试数据集 (§7)
    polymarket/{gamma,clob,wss,data}/
    goalserve/{inplay,livescore,pregame}/
    chain/{rpc-mock,nonce}/
    intent_corpus/        # OrderIntent 黄金 + 边缘
    golden_pnl/           # 已知输出基线
  helpers/                # 通用 mock / matcher / time travel
    mock_clob_server.{h,cc}
    mock_wss_server.{h,cc}
    mock_chain_rpc.{h,cc}
    virtual_clock.{h,cc}
    risk_intent_factory.{h,cc}
```

每个目录配 `OWNERS`. 改 `risk/` 下 PR 自动 cc 老韩, 改 `fixtures/polymarket/` 自动 cc 老李, 改 `fixtures/goalserve/` 自动 cc 小段.

---

## 2. Replay 框架设计

### 2.1 设计目标

| # | 目标 | 度量 |
|---|---|---|
| R1 | 确定性: 同一 fixture + 同一 binary + 同一 config → 同一 decision 序列 | 100% replay 复现 (允许 1 个 tolerance: 时间戳 wall-clock, 用 virtual clock 替换) |
| R2 | 与 prod EventRecorder 同 schema | 录的回放, 回的能录 (`Recorder ↔ Replay` 闭环) |
| R3 | 时间轴可控: 加速 / 减速 / 暂停 / 跳到时间点 | 加速比 1x / 10x / 100x / max (CPU-bound) |
| R4 | 与 RM audit 对账 | replay 一遍产同款 audit_id 序列 (除 ULID 时间戳部分) |
| R5 | 用于事故复现 | INCIDENTS 文档每个 P0/P1 事故必产对应 replay case (老唐配套) |

### 2.2 输入格式

**主格式: MessagePack framed** (二进制, 跨洋带宽 / 磁盘友好). 兼容格式: JSONL (调试 + diff 友好).

```
[4 bytes: frame_len BE] [1 byte: frame_type] [N bytes: msgpack payload]

frame_type 枚举:
  0x01 EVENT_POLY_WSS        # 来自 ws-subscriptions-clob market/user 流
  0x02 EVENT_POLY_REST_SNAP  # gamma/clob/data 快照
  0x03 EVENT_GOALSERVE       # inplay/livescore/pregame
  0x04 EVENT_CHAIN_RPC       # Polygon RPC 响应 (含 fill, nonce, balance)
  0x05 EVENT_INTENT          # 策略层产 OrderIntent (RM 输入)
  0x06 EVENT_DECISION        # RM 输出
  0x07 EVENT_FILL            # 成交回报 (落 ledger)
  0x08 EVENT_STATE           # RM 状态变更
  0x09 EVENT_CONFIG_RELOAD   # 配置热加载
  0x0A EVENT_HEARTBEAT       # 源心跳 (用于 stale 触发)
  0x0B EVENT_CHAOS_INJECT    # chaos 标记 (注入开始/结束)
  0x0C EVENT_OPS_ACK         # 人工 ack (HALTED 恢复)
  0x10 META_RECORDING_OPEN   # 文件头
  0x1F META_RECORDING_CLOSE  # 文件尾
```

**Common envelope (所有 payload 必含):**

```
recorded_at_ns   : i64   wall clock (NTP-sync, 用于跨源对齐)
monotonic_ns     : i64   单调时钟 (用于延迟测量, virtual clock 基准)
source_id        : str   "poly_wss_market" / "goalserve_inplay" / ...
seq             : u64    源内递增, 用于检测丢包
schema_version  : str    "v0.1"
```

**Schema 治理:**
- Schema 文件: `tests/fixtures/_schema/replay_v0.1.msgpack-schema` (待 @小余 + 我会签).
- 每个 `frame_type` 一份 schema, 字段加只许追加, 改语义必须 bump `schema_version`.
- ReplayDriver 加载时校验 `schema_version`, 不匹配拒绝跑.

**与小余 EventRecorder 对齐 (待会签):**
- 我会用她在 `data/replay/EventRecorder` 的输出作为输入, 不重复定义.
- @小余: 你的 schema 准备好之后请丢一份样本到 `tests/fixtures/_samples/`, 我对齐写 ReplayDriver decoder.

### 2.3 ReplayDriver 设计

```
class ReplayDriver {
public:
    ReplayDriver(ReplayConfig);

    // 装载多源 (按 monotonic_ns 排序合流)
    Status load(std::span<const FixturePath>);

    // 时间轴控制
    void   set_speed(double multiplier);       // 1.0 / 10.0 / inf
    void   pause();
    void   resume();
    Status seek_to(int64_t monotonic_ns);

    // 跑
    Status run(SystemUnderTest& sut);
    Status run_until(int64_t monotonic_ns, SystemUnderTest& sut);

    // 断言挂载
    void   attach_assertion(std::unique_ptr<ReplayAssertion>);
    void   attach_fault_provider(std::unique_ptr<FaultProvider>);   // chaos 复用同一驱动
};
```

**核心机制:**

1. **虚拟时钟 (VirtualClock):** `infra/clock/SteadyClock` 在测试态被替换为 `VirtualClock`, 时间只由 ReplayDriver 推进. 业务代码读 `Clock::now()` 拿到的是当前 event 的 `monotonic_ns`. 不允许任何模块 `sleep(real time)` (CI 静态扫描禁 `std::this_thread::sleep_for` 在 src/ 出现, 只许 tests/helpers 用).
2. **事件分发:** 按 `monotonic_ns` 全局有序合流, 推到 SUT 的对应 ingress (mock WSS server / mock REST / mock RPC).
3. **加速比:** `1x` 严格按 ns 推进 (跑 30min 历史要 30min wall); `10x` 推 10 倍速 (跑 3min); `inf` (max) CPU-bound 直接推, 不睡, 测确定性最常用.
4. **快进:** `seek_to` 用 snapshot + 增量 replay, snapshot 由 EventRecorder 周期产 (依赖小余 v0.2).
5. **倒带:** v0.1 不支持任意倒带, 只支持 `restart() + run_until(t)`. 真正倒带 v0.2 (依赖 RCU snapshot 回滚, 与小石讨论).

### 2.4 基线对比 (Golden 模式)

```
class ReplayAssertion {
public:
    virtual Status on_decision(const RiskDecision&) = 0;
    virtual Status on_fill(const Fill&) = 0;
    virtual Status on_state_change(State from, State to) = 0;
    virtual Status finalize() = 0;
};

class GoldenLogAssertion : public ReplayAssertion {
    // 加载 golden log, 流式 diff 当前 decision 序列
    // 容忍字段: recorded_at_ns (allow drift) / audit_id (regex 校验 ULID prefix)
    // 严格字段: decision / reject_code / approved_size_usdc / state
};
```

**Golden 生成:**
- 录: prod 跑一周, EventRecorder 落盘 + RM audit log 落盘.
- 提: 用工具脱敏 (脱钱包 / API key / PII), 抽样, 入 `tests/fixtures/golden_pnl/`.
- 验: 修代码后 replay, GoldenLogAssertion 通过 = 行为不变. 不通过 = 主动 review (是 bug 还是 intentional)? intentional 就更新 golden + 写 RFC.

### 2.5 用例

| 用例 | 输入 | 期望 |
|---|---|---|
| 回测复现 | 历史 7d 事件流 | PnL / 决策序列 / 拒单率 = baseline (容忍 ε) |
| 事故复现 | INCIDENT-XXX 当时事件流 (T-5min 起) | 当时 RM 决策序列重现, 验改的 fix 改了它 |
| 新功能行为基线 | 主线 golden + 新 PR build | 行为变更必须显式 (PR 描述里列) |
| 跨配置对比 | 同 fixture + config_A vs config_B | A/B 测试 sizing / kelly fraction / cap |

---

## 3. Chaos Test 框架

### 3.1 注入点 (FaultProvider)

Chaos 不是单独的进程, 是 ReplayDriver 加 `FaultProvider`. 同一 fixture 跑两遍 (有/无 fault), 行为差 = chaos signal.

| 类别 | FaultProvider | 注入点 | 期望行为 |
|---|---|---|---|
| **网络抖动** | `NetworkJitterProvider` | `infra/net/TlsSession` 中间层 | RTT +Δ → RM 不慌 / 报价不发到过时盘口 |
| **WSS 断流** | `WssDisconnectProvider` | `infra/net/WssClient` | 30s 内自动重连 + 全量重订阅 (老李 §X) / heartbeat 触发 STALE → WARNING |
| **API 限流** | `RateLimitProvider` | mock REST server 返回 429 | client backoff + 不打死 / 不误判为 server down |
| **API 5xx** | `Http5xxProvider` | mock REST server 返回 502/503/504 | 退避 + 切 fallback endpoint (老叶 D8 multi-provider) |
| **时钟漂移** | `ClockSkewProvider` | `infra/clock/NtpMonitor` | 漂移 > 阈值 → NtpMonitor 告警 → audit 用 monotonic 不受影响 |
| **RDTSC 失校** | `TscDriftProvider` | `infra/clock/SteadyClock` | latency 测量退化, 但不污染逻辑 |
| **Nonce 冲突** | `NonceConflictProvider` | mock chain RPC | NonceManager 检测 + recover (老孙 + 老叶 联合) |
| **资金对账失联** | `LedgerSyncStallProvider` | mock data-api | > 30s → STALE_DATA → WARNING/HALTED |
| **signer 子进程 crash** | `SignerCrashProvider` | TB-B IPC | trader 不 down, recon 进入 SAFE_MODE |
| **配置热加载非法 diff** | `BadConfigReloadProvider` | inotify 触发坏 TOML | watcher 拒绝 + alert + 沿用旧 config |
| **Goalserve 字段缺失** | `GoalserveSchemaDriftProvider` | 修改 fixture payload 字段 | normalize 报错 + drop + counter, 不传染 |
| **Polymarket WSS lag** | `WssLagProvider` | 延迟推送 + 乱序 seq | book builder 检测 seq gap + 触发 REST 全量补 |
| **跨洋链路丢包** | `PacketLossProvider` | `infra/net/TcpSocket` | TCP 重传不阻塞 hot path, 业务级 timeout 触发 |

### 3.2 与小宫 dogfood 剧本对接

小宫剧本 (`xiaogong-dogfood-playbook-v1.md`, 待出) 给的是"人在用的时候看到什么", 我把每个 dogfood scenario 转成一个 chaos `playbook.yaml`:

```yaml
# tests/chaos/playbooks/wss_disconnect_during_burst.yaml
name: wss_disconnect_during_burst
description: 比赛白热化(进球前2min)WSS突断15s
linked_dogfood: xiaogong-scenario-S-014
fixtures:
  - polymarket/wss/nba-2026-05-15-celtics-heat-q4.msgpack
  - goalserve/inplay/nba-2026-05-15-celtics-heat-q4.msgpack
injection:
  - type: WssDisconnectProvider
    at: T+90s
    duration: 15s
expectations:
  - rm_state_at(T+95s) IN (RUNNING, WARNING)
  - rm_state_at(T+115s) == WARNING       # stale > 30s 阈值
  - no_orders_during(T+90s, T+105s)      # halt 后不下单
  - reconnect_before(T+135s)
  - audit_contains(reject_code=STALE_DATA, count>=1)
```

@小宫: v0.1 我先 stub 10 个剧本, 你 v0.1 出来后我对齐补到 N 个.

### 3.3 Chaos 跑频

| 套件 | 跑频 | 跑时 |
|---|---|---|
| **chaos smoke (10 个核心剧本)** | nightly | ≤ 30min |
| **chaos full (全部剧本)** | weekly | ≤ 4h |
| **红线演练 (月度, 与老韩 §9.4)** | 月度 | 半天, 含手工 ack |

### 3.4 期望行为 (cross-reference)

| Chaos 类别 | 风控期望 | 关联文档 |
|---|---|---|
| WSS / Goalserve 断流 > 30s | `STATE_HALTED` 或 `DEFERRED(STALE_DATA)` | 老韩 §3.7, 老周 D6 |
| 资金对账失联 > 30s | `WARNING`, 持续到 60s → `HALTED` | 老韩 §6.3 |
| signer 子进程 crash | trader 不下单, SAFE_MODE | 老沈 TB-B, 老周 §9.2 |
| 配置非法热改 | watcher reject + audit + 沿用旧 config | 老周 §7.2, 老韩 §8.5 |
| 跨洋链路丢包 | TCP retransmit 透明 / 业务 timeout | 老吴 S1-010 |

---

## 4. 风控测试 (RM 13 拒单 enum 全覆盖)

### 4.1 拒单 enum 强制覆盖矩阵 (与老韩 §3.10 对齐)

每个 `RejectReason` 必须满足:
- 至少 1 个 **unit test** (规则单测, mock 其他)
- 至少 1 个 **sim test** (含状态机交互)
- 至少 1 个 **replay/chaos case** (端到端可触发)
- audit log 字段 `reject_code` 精确匹配

| # | reject_code | 触发规则 | unit owner | sim owner | replay/chaos owner |
|---|---|---|---|---|---|
| 1 | `STATE_HALTED` | R0 | 我 + 老韩 | 我 | chaos/network/wss_long_disconnect |
| 2 | `STATE_DRAIN` | R0 | 我 + 老韩 | 我 | replay/edge/drain_mode_close_only |
| 3 | `DUPLICATE_INTENT` | R1 (idempotency hit) | 我 + 老韩 | 我 | replay/edge/strategy_restart_retry |
| 4 | `STALE_DATA` | R2 | 我 + 老韩 | 我 | chaos/network/goalserve_30s_stall |
| 5 | `INVALID_INTENT` | R3 (price ∉ (0,1), size ≤ 0, 必填空) | 我 + 老韩 | 我 | fuzz/risk_intent_validator |
| 6 | `EXCEED_PER_ORDER_CAP` | R4 | 我 + 老韩 | 我 | replay/edge/big_intent_soft_cap |
| 7 | `EXCEED_MARKET_EXPOSURE` | R5 | 我 + 老韩 | 我 | replay/edge/exposure_climb |
| 8 | `DAILY_LOSS_HALT` | R6 | 我 + 老韩 | 我 | replay/incident/daily_loss_trigger |
| 9 | `CONSEC_LOSS_HALT` | R7 | 我 + 老韩 | 我 | replay/edge/n_consec_losses |
| 10 | `EDGE_CI_NEGATIVE` (待会签 @小梁 是否单列) | R8 | 我 + 老韩 | 我 | replay/edge/ci_lower_zero |
| 11 | `INSUFFICIENT_BANKROLL` | R9 | 我 + 老韩 | 我 | replay/edge/drawdown_then_intent |
| 12 | `MARKET_TYPE_NOT_ENABLED` | R3 子项 | 我 + 老韩 | 我 | replay/edge/totals_intent_during_mvp |
| 13 | `INTERNAL_ERROR` | 全局兜底 (fail-closed) | 我 + 老韩 | 我 (mock 内部异常) | chaos/api/audit_disk_full |

**CI 拦截脚本:**
- `tools/ci/risk_enum_coverage.py`:
  1. 扫 `src/risk/RejectReason.h` 全部 enum 项.
  2. 扫 `tests/unit/risk/` + `tests/sim/risk_sim/` + `tests/replay/` 中 `EXPECT_REJECT_CODE(X)` 调用.
  3. 任何 enum 项无对应 case → exit 1, CI 拒 merge.
  4. 新增 enum 必须同 PR 加 3 个 case 才能进.

### 4.2 SAFE_MODE 启动/退出测试 (与老周 §9.2 对账)

SAFE_MODE = 重启后对账未完成期间的"只撤不开"状态. 不是 RM 的状态 (RM 是 RUNNING/WARNING/HALTED/DRAIN), 而是整体系统的运行模式.

| case | 输入 | 期望 |
|---|---|---|
| sim/safe_mode_enter_on_crash_restart | WAL 重放完 + chain nonce 拉到 + ledger 对账中 (mock 拖 5s) | `SAFE_MODE = true`, RM 拒所有开仓 intent (`STATE_DRAIN` 或新增 `SAFE_MODE`?待会签) |
| sim/safe_mode_exit_on_recon_ok | 对账一致 + WSS first heartbeat 到 | `SAFE_MODE = false`, RM RUNNING |
| sim/safe_mode_stuck_on_recon_diff | 对账不一致 | `SAFE_MODE` 留住, alert, 等人工 ack |
| chaos/safe_mode_during_fill_in_flight | 重启时刚好有 in-flight fill 回灌 | nonce / ledger 不污染, fill 落到对账完成后 |

**疑问 @老韩 + @老周:** SAFE_MODE 在 RM 拒单时是新增 `SAFE_MODE_PENDING_RECON` enum, 还是复用 `STATE_DRAIN`? 我倾向**新增**, 语义更清楚 (DRAIN 是显式人工, SAFE_MODE 是启动期自动). 待会签.

### 4.3 绕过 RM 检测 (与老韩 §9.4 红线演练对账)

**红线: 任何下单链路必经 RiskManager::evaluate. 绕过 = P0.**

三重拦截:

#### 4.3.1 静态扫描 (CI 拒 merge)

`tools/ci/risk_bypass_scan.py`:
- AST 级扫描 (libclang Python binding), 不靠正则.
- 规则:
  1. `exec/signer/Eip712Signer::sign()` 的所有 caller 必须前序在同函数或父函数有 `RiskManager::evaluate()` 调用, 且其 `RiskDecision::decision == APPROVED` 分支才进 sign.
  2. `exec/clob/ClobOrderClient::submit_order()` 的所有 caller 必须传入由 RM 产出的 `audit_id`.
  3. `risk/manager/RiskManager` 的实例化必须 singleton (启动期注入), 任何 `new RiskManager` / `make_unique<RiskManager>` 在非 `main()` / `Bootstrap::init` 调用点出现 → reject.
  4. 任何 PR 改 `src/risk/` 或 `src/exec/signer/` 必须 cc 老韩 + 老沈, missing review 则 CI block.

#### 4.3.2 运行期 instrumentation (debug build)

`risk/manager/RiskGateway` 在 debug build 内部维护一个 `recent_audit_ids` LRU (size 1024). `exec/signer/Eip712Signer::sign()` 接收 `audit_id`, 必须命中 LRU; 不命中 → `assert(false) + abort + core dump`. 生产 build 同样校验但走 fail-closed (拒签 + RM 强制 HALT + 报警).

#### 4.3.3 链路 audit 对账 (nightly job)

`tools/recon/sign_audit_recon.py`:
- 拉 RM audit log (`reject_code IS NULL` 即 APPROVED) 与 signer log 对账.
- 任意 signer record 找不到对应 RM audit → P0 alert + INCIDENT 自动开.

### 4.4 RM 测试 fixture 设计

| Fixture 集 | 内容 | 来源 |
|---|---|---|
| `intent_corpus/golden/` | 100 个合法 OrderIntent (覆盖 sizing / edge / market 多场景) | 我 + 小梁 |
| `intent_corpus/boundary/` | 边界值 (price = 0.001 / 0.999, size = 5 USDC 最小, edge_low = 1bps) | 我 |
| `intent_corpus/malformed/` | 13 类 INVALID_INTENT 触发样本 | 我 + 老韩 |
| `intent_corpus/duplicate/` | 同 idempotency_key 复 N 份 | 我 |
| `bankroll_trajectory/` | bankroll 变动曲线 (用于 daily/consec loss 触发) | 我 + 小梁 |
| `positions_state/` | positions ledger 初始状态 (各种敞口) | 我 |
| `config_snapshots/` | TOML config 套餐 (默认 / 缩水 / 非法) | 我 + 老陈 |

---

## 5. 覆盖率门禁

### 5.1 工具

- `gcovr` (lcov 后端) 出报告, `kcov` 备选.
- LLVM `source-based coverage` (`-fprofile-instr-generate -fcoverage-mapping`) 是主用 (clang), gcc 走 gcov fallback.
- CI 上传到 `coverage/<commit>/index.html` (S3 静态), PR comment 自动贴差异.

### 5.2 阈值

| 范围 | line | branch | 备注 |
|---|---|---|---|
| `src/risk/**` | **≥ 90%** | **≥ 85%** | 红线模块 |
| `src/exec/signer/**` | **≥ 90%** | **≥ 85%** | 私钥 + 签名 |
| `src/exec/nonce/**` | **≥ 90%** | **≥ 85%** | nonce 治理 |
| `src/exec/recon/**` | **≥ 90%** | **≥ 80%** | 对账 |
| `src/infra/clock/**` | **≥ 85%** | **≥ 80%** | 时钟 (chaos 高频用) |
| `src/data/heartbeat/**` | **≥ 90%** | **≥ 85%** | 触发 halt |
| `src/data/normalize/**` | **≥ 85%** | **≥ 75%** | 外部 → 内部 id 翻译 |
| 整体 (`src/`) | **≥ 70%** | **≥ 60%** | MVP 目标 |
| 整体 v0.2 目标 (M+6) | ≥ 80% | ≥ 70% | post-MVP 提 |

### 5.3 PR 门禁规则

1. PR 改了 `src/risk/**` 或 `src/exec/signer/**`, 关联文件覆盖率不得低于阈值, **不允许覆盖率下降** (覆盖率回退也 block).
2. PR 改了别处, 整体不得低于 70%. 允许局部小幅波动 (新代码无测), 但触发"测试待补"ticket 自动开.
3. 新增 RejectReason enum 必须同 PR 加测试 (§4.1 强制).
4. 任何 `// COV-EXCLUDE` 注释必须有 reviewer 二次 sign-off, 且记账 (年度审计).

### 5.4 反 gaming 措施

覆盖率不是 KPI 主指标. 怕同事写"调用一次就完"的伪测试. 拒绝 pattern:
- 单纯调用不 assert → CI 静态分析 (gtest 不允许全空 body, 至少一个 `EXPECT_*` 或 `ASSERT_*`).
- 测 happy path 不测拒单 → RM 强制 13 enum 覆盖 (§4.1) 兜底.
- Mock 过度, 测了 mock 自己 → review 时人工抓, 季度 sample 复查.

---

## 6. CI/CD 集成 (与老吴对接)

### 6.1 矩阵

| 阶段 | 触发 | 跑什么 | 时长目标 | 失败影响 |
|---|---|---|---|---|
| **PR pre-merge** | push to PR branch | lint + format + unit + sim smoke + clang-tidy + ASAN unit | ≤ 8min | 拒 merge |
| **post-merge to main** | merge | 全套 unit + 全套 sim + replay smoke (5 个 golden) + ASAN/UBSAN/TSAN matrix | ≤ 30min | 拒 deploy, alert |
| **nightly** | cron 03:00 UTC | replay full + chaos smoke + perf regression + coverage report | ≤ 3h | alert + dashboard 红 |
| **weekly** | cron Sun 02:00 UTC | fuzz 12h + chaos full + 跨平台 build matrix | ≤ 14h | alert |
| **red-line drill (月度)** | 手动 + cron Mon 第一天 | 完整红线演练 (绕过尝试 + RM 全 enum sim) | 半天 | 不通过 = 老韩 + 我负全责 |

### 6.2 GitHub Actions 布局

```
.github/workflows/
  pr.yml                   # PR pre-merge (matrix: clang17, gcc13; sanitizer: ASAN, UBSAN)
  post-merge.yml           # main 推送
  nightly.yml              # cron
  weekly-fuzz.yml          # cron
  red-line-drill.yml       # workflow_dispatch + cron
  deploy.yml               # 与老吴 S1-010 对接 (本 framework 不动)
```

### 6.3 与老吴 deploy 流水线对接 (待会签 @老吴)

- 我跑 test, 老吴跑 deploy.
- 接口: nightly 通过 → 在 release artifact 上贴 `tests_passed=true` annotation, 老吴的 deploy 流水线只 promote `tests_passed=true` 的 artifact.
- 不通过的不 promote, 不存在"先上线再补测试".
- 长跑 (chaos full / fuzz 12h) 不阻塞 deploy, 但失败必产 INCIDENT 自动开.

### 6.4 sanitizer 矩阵

| sanitizer | 跑频 | 范围 | 备注 |
|---|---|---|---|
| ASAN | PR + nightly | unit + sim | 必跑 |
| UBSAN | PR + nightly | unit + sim | 必跑, 含 implicit-int-conversion |
| TSAN | nightly | sim + replay | 主线 lock-free 关键 |
| MSAN | weekly | unit (clang only) | uninitialized 读 |
| LSAN | nightly | unit | 内存泄漏 |
| libFuzzer + ASAN | weekly | parser / decoder | 12h |

**为什么不全 PR 跑:** TSAN/MSAN 慢 (~5-10x), PR 跑超时. Nightly + weekly 兜底, PR 上 ASAN + UBSAN 够抓 80% 问题.

### 6.5 perf regression

`google-benchmark` + 内部 `PerfBaseline` harness:
- 每个热路径模块一个 benchmark suite (`risk_evaluate_bench`, `book_builder_bench`, `feature_pipeline_bench`, `signer_sign_bench`).
- 输出 p50/p99/max + 标准差.
- baseline 存在 `tests/perf/baseline/<commit>.json`, nightly 跟 main HEAD baseline 比.
- p99 退步 > 10% (与老姜延迟预算 §11 关联) → alert.
- 退步 > 30% → 自动 INCIDENT.

---

## 7. 测试数据集体系

### 7.1 数据源分层

| 层 | 内容 | 大小 | 来源 | 脱敏 |
|---|---|---|---|---|
| **L0 单元 fixture** | 单条 JSON / msgpack payload | < 1KB 每条 | 我手写 + 老李 / 小段 现成样本 | 无敏感 |
| **L1 sim fixture** | 短时间窗 (1-5min) 事件流 | < 1MB 每个 | EventRecorder 录的小段 | 钱包脱敏 |
| **L2 replay fixture** | 完整赛事 (2-4h) 事件流 | 50-200MB 每个 | EventRecorder 录的全场 | 全脱敏 |
| **L3 incident fixture** | 事故前后窗 (T-5min 起) | 10-50MB 每个 | 事故时刻录的 | 完整脱敏 + 老唐 sign-off |

### 7.2 脱敏规则 (与老沈 + 老黄会签)

| 字段 | 脱敏方式 |
|---|---|
| 钱包地址 (signer EOA / funder) | 替换成 deterministic 假地址 (`sha256(real)[:40]`) |
| API key / secret / passphrase | 删除 |
| PII (用户名 / 邮箱 / IP) | 不应该出现 (Polymarket 数据公开), 但兜底脱 |
| 真实 PnL 数字 | 保留 (盘后已公开) |
| 钱包余额 | 同地址做映射后保留比例 |

工具: `tools/fixtures/desensitize.py`, 跑前必过. 任何 PR 把未脱敏数据加到 `fixtures/` → CI 静态扫描拒 (扫钱包正则 / api key 正则).

### 7.3 与小余 (EventRecorder) + 小段 (Goalserve samples) + 老李 (Polymarket samples) 对接

- @小余: 你 EventRecorder 输出格式定下后, 我把当前 stub 的 fixture loader 切到正式 decoder. 我建议 schema 共享 ownership (`tests/fixtures/_schema/` 由我俩共同 own).
- @小段: `docs/RESEARCH/data/xiaoduan-goalserve-samples/` 现有的 19 个 JSON/XML 我会全部纳入 fixture (inplay/livescore/pregame 三类), 命名规范 `goalserve/<sport>/<feed>/<datetime>-<scenario>.{msgpack,json,xml}`. 你后续 sample 请按这套命名落到 `tests/fixtures/goalserve/`.
- @老李: `docs/RESEARCH/data/laochen-network-bench-polymarket-*.csv` 是网络层的, 我要的是 payload sample. 麻烦你在 S1-002 落地时, 每个端点丢 5 个真实 response 样本到 `tests/fixtures/polymarket/<api>/<endpoint>/`. 我配套写 schema 校验 fixture.

### 7.4 黄金路径 + 边缘 case 清单 (v0.1 起步 30 个)

| ID | 类型 | 场景 | 输入 fixture | 期望 |
|---|---|---|---|---|
| G-01 | golden | NBA Moneyline 单笔下单 + fill | poly + goalserve + chain | RM APPROVED + sign + fill 回灌 |
| G-02 | golden | 同赛事 5 笔 intent 不冲突 | ... | 5/5 APPROVED, exposure 增 |
| G-03 | golden | strategy restart 后幂等 | dup intent | 第 2 笔 DUPLICATE_INTENT |
| E-01 | edge | size 触发 PER_ORDER_CAP | big intent | EXCEED_PER_ORDER_CAP |
| E-02 | edge | 单市场敞口爬升触上限 | 连续 N 笔 | 第 N+1 笔 EXCEED_MARKET_EXPOSURE |
| E-03 | edge | 日内亏 3% | bankroll 跌穿 | DAILY_LOSS_HALT |
| E-04 | edge | 连 5 笔亏 | sequential losses | CONSEC_LOSS_HALT |
| E-05 | edge | edge CI 下界 ≤ 0 | low confidence intent | EDGE_CI_NEGATIVE 或 size=0 |
| E-06 | edge | Goalserve 30s 无更新 | inplay halt | STALE_DATA |
| E-07 | edge | Polymarket WSS 60s 无更新 | wss halt | STATE_HALTED |
| E-08 | edge | bankroll 不足 (含 5% safety buffer) | drawdown | INSUFFICIENT_BANKROLL |
| E-09 | edge | TOTALS intent (MVP 不开) | wrong type | MARKET_TYPE_NOT_ENABLED |
| E-10 | edge | invalid price 0 / 1 | malformed | INVALID_INTENT |
| E-11 | edge | DRAIN 模式下开仓 | state=DRAIN | STATE_DRAIN |
| E-12 | edge | HALTED 模式下任何单 | state=HALTED | STATE_HALTED |
| E-13 | edge | audit log fsync 失败 | inject disk-full | INTERNAL_ERROR |
| C-01 | chaos | WSS 断 15s 比赛 q4 | playbook S-014 | WARNING → 不下单 → 重连 |
| C-02 | chaos | clob REST 429 限流 | repeated reqs | backoff + 不死锁 |
| C-03 | chaos | nonce 冲突 | parallel sign | NonceManager 拒第二笔 |
| C-04 | chaos | signer 子进程 crash | TB-B 断 | trader 不 down, SAFE_MODE |
| C-05 | chaos | 配置非法热改 (调高 cap) | bad TOML | reload reject + alert |
| C-06 | chaos | 跨洋链路 5% 丢包 | netem drop | TCP retransmit, 业务无感 |
| C-07 | chaos | NTP 漂移 +5s | clock skew | NtpMonitor alert, audit 用 monotonic |
| C-08 | chaos | data-api 30s 停更 | ledger sync stall | WARNING → HALTED |
| C-09 | chaos | Goalserve XML schema drift | inject extra field | normalize 报错 + drop + counter |
| C-10 | chaos | Polygon RPC 切 provider | primary 503 | 透明切 fallback (老叶 D8) |
| I-01 | incident | (待第一起 P1 事故后补) | TBD | replay 出当时决策 |
| I-02 | incident | (同上) | TBD | TBD |
| I-03 | incident | (同上) | TBD | TBD |
| F-01 | fuzz | poly_wss_decoder | corpus + libFuzzer | 0 crash 0 ASAN |

---

## 8. 与小宫 dogfood 协同

小宫 (`dogfood-tester`) 是"假用户", 他跑全流程发现"用着不顺手"或"看到奇怪日志". 我把他的发现转测试:

| 小宫 finding | 我做的事 |
|---|---|
| "CLI 撤单慢" | sim/exec_cancel_latency_bench |
| "日志看不出来为什么拒单" | unit/risk_audit_reject_detail_format |
| "重启后 ack 流程坑" | sim/safe_mode_human_ack_flow |
| "图表上 stale 状态看不出" | (转给小郑 dashboard, 我加 metric assertion) |
| "WSS 重连后 book 不一致 1s" | replay/edge/wss_reconnect_book_consistency |

**协同节奏:**
- 小宫每跑一轮 dogfood → 写一份 "用户视角 finding 清单" (`docs/RESEARCH/xiaogong-dogfood-findings-w<N>.md`).
- 我每周扫一遍, 把 reproducible 的转 test case + 关联 ticket.
- 小宫的 chaos 剧本 (§3.2 已述) 由他主写 / 我落 yaml.

---

## 9. 工具选型

### 9.1 Unit framework: **gtest vs Catch2** (选 gtest)

| 维度 | gtest | Catch2 | 我选 gtest 的理由 |
|---|---|---|---|
| 编译时间 (1000 case) | ~ 18s | ~ 65s | Catch2 header-only 重模板, 大项目编译墙 |
| death test (signal abort) | 原生支持 (`EXPECT_DEATH`) | 弱, 要自己包 | RM `INTERNAL_ERROR` fail-closed 测要测 abort, gtest 是事实标准 |
| gmock 集成 | 同家, 无缝 | 第三方 trompeloeil, 接口不齐 | 我重度用 mock (CLOB / WSS / RPC), gmock 更顺 |
| parameterized test | `TEST_P` + `INSTANTIATE_TEST_SUITE_P` 成熟 | `TEMPLATE_TEST_CASE` / `GENERATE` | 13 RejectReason enum 用 param test 一把过, gtest 写法更直接 |
| death + ASAN 兼容性 | 好 | 偶尔卡 | 跨 sanitizer matrix 跑 gtest 更稳 |
| C++ 生态 (大厂) | Google / Meta / LLVM / Folly / Tensorflow 全用 | 中小项目 | 招聘 + 上手成本低 |
| 数字: Polymarket-cpp / Polygon-cpp / Folly / Abseil 用啥 | gtest | — | 行业事实标准 |

**反方:** "Catch2 写起来漂亮" → 是, 但我们是大型多人项目, 编译时间 + 工具链稳定性 + mock 生态优先. PR 跑 8min, Catch2 编译就吃掉 3min, 不划算.

**决定:** gtest (v1.14+) + gmock + abseil status compat. 启动期就锁, 别中途换.

@老何 @老周 评审.

### 9.2 Benchmark: google-benchmark

无悬念, 业内事实标准, 与 gtest 共存.

### 9.3 Fuzz: libFuzzer + AFL++ 备用

- libFuzzer (clang) 主用, 与 ASAN/UBSAN 协同好.
- AFL++ 用于覆盖率难推的场景 (e.g. Goalserve XML 解析).

### 9.4 Mock 工具

- **gmock** 用于内部接口 (RM / signer / RPC client).
- **mock 外部服务** 用真 socket server, 不 mock socket 层 (理由: 跨洋 TLS / WSS frame 都是 net IO, mock 到 socket 上面易漏边界 bug).
  - `tests/helpers/mock_clob_server` — 用 cpp-httplib + websocketpp 拉真 server, fixture 驱动响应.
  - `tests/helpers/mock_wss_server` — 同上.
  - `tests/helpers/mock_chain_rpc` — JSON-RPC server.

### 9.5 Sanitizer

- ASAN + UBSAN: PR 必跑.
- TSAN: nightly (lock-free 关键, 与小石 / 老姜 联合).
- MSAN: weekly (uninitialized 读).
- LSAN: nightly.

### 9.6 Coverage

- LLVM source-based coverage (主), gcov (备).
- `gcovr` 出 html, `llvm-cov export` 出 JSON 给 dashboard.

### 9.7 lint / static analysis

- `clang-format` (PR 自动改).
- `clang-tidy` (PR 必跑, 选 cert + bugprone + concurrency + performance 子集).
- `cppcheck` (nightly 补盲).
- `include-what-you-use` (weekly).
- 自研 `risk_bypass_scan.py` (§4.3.1).
- 自研 `risk_enum_coverage.py` (§4.1).

### 9.8 时间旅行

- `VirtualClock` 自研 (替换 `infra/clock/SteadyClock` 在测试态, 见 §2.3).
- 静态扫描禁 `std::this_thread::sleep_for` / `std::chrono::system_clock::now` 在 `src/` 出现 (除 `infra/clock/`), 只许走 `Clock::now()`.

---

## 10. 开放问题

| # | 议题 | 待定 | 负责跟进 |
|---|---|---|---|
| TQ-1 | EventRecorder schema 锁版 | 待小余 v0.1 schema | @小余 + 我 |
| TQ-2 | SAFE_MODE 是新 reject_code 还是复用 STATE_DRAIN | 待 §4.2 会签 | @老韩 + @老周 |
| TQ-3 | EDGE_CI_NEGATIVE 是单列 enum 还是 size=0 软放行 | 老韩 Q2 同源 | @小梁 + @老韩 |
| TQ-4 | 覆盖率工具主用 LLVM source-based 还是 gcov | 倾向 LLVM (clang 主编译器), gcov 备 | @老何 (cpp-advisor) |
| TQ-5 | mock 外部服务用真 socket 还是 mock 层 | §9.4 倾向真 socket, 但 PR 跑可能慢 | @老吴 (CI infra) |
| TQ-6 | replay 严格 1x 模式是否必要 (考虑跨洋链路本身就有抖动) | v0.1 提供, v0.2 评估去掉 | 我 |
| TQ-7 | 跨平台 (linux x86_64 only? 还是 macOS dev) | nightly matrix 是否含 macOS | @老吴 |
| TQ-8 | fuzz 12h 是否够 (Polymarket 协议字段多) | v0.1 12h, v0.2 评估 24h+ | 我 |
| TQ-9 | golden log 脱敏与合规 (老黄红线) | 数据出测试环境前必过老黄 sign-off | @老黄 |
| TQ-10 | perf baseline 存储 (S3 / git LFS / artifact) | 倾向 S3, git LFS 备 | @老吴 |
| TQ-11 | chaos playbook DSL 是 yaml 还是 cpp DSL | v0.1 yaml (易读), v0.2 评估 | 我 + @小宫 |
| TQ-12 | 红线演练月度强度 vs Sprint 节奏 | 月度 + Sprint 末加跑一次 | @老韩 + @老雷 |

---

## 11. 第一个该跑的测试 case

为了让本框架"今天就能跑起来" (即使 RM 还没实现, 也能定接口), 我提交**第一个该写的 case** + **第一个该跑的 case**.

### 11.1 第一个该写的 case (TDD 风格, 红 → 绿 → 重构)

**case 名:** `risk::RiskManagerEvaluate/exceed_per_order_cap_hard_basic`
**意图:** 验 RM 红线最硬一条规则: 单笔超 `PER_ORDER_CAP_HARD` (编译期 constexpr, 运行时不可改) 必拒, 任何"软放行"路径都不应该让它通过.

**为什么选它:**
1. 它是老韩 §3.3 / G1 / G6 三处红线的交集.
2. 它不依赖任何外部 IO (mock RM 自己的 config / ledger 就够).
3. 它能立刻暴露设计漏洞: 如果 RM 实现里 `PER_ORDER_CAP_HARD` 走了 config 路径而不是 constexpr, 测试一跑就挂.
4. 它是 §4.1 表 #6 `EXCEED_PER_ORDER_CAP` 的 unit 入口.

**伪代码 (gtest 风格):**

```cpp
// tests/unit/risk/risk_manager_exceed_per_order_cap_test.cc

#include "risk/risk_manager.h"
#include "tests/helpers/risk_intent_factory.h"
#include "tests/helpers/virtual_clock.h"
#include <gtest/gtest.h>

namespace stcpp::risk::test {

class RiskManagerExceedPerOrderCapTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 注入 VirtualClock, 不依赖 wall time
        clock_ = std::make_shared<VirtualClock>(/*epoch_ns=*/1'700'000'000'000'000'000LL);

        // 拿编译期 hard cap, 不允许测试 override 它 (这是红线)
        hard_cap_usdc_ = RiskManager::kPerOrderCapHardUsdc;

        // soft cap = hard cap (worst case: 没收紧 config 的情况)
        RiskManagerConfig cfg;
        cfg.per_order_cap_soft_usdc = hard_cap_usdc_;
        cfg.bankroll_usdc           = 100'000;  // 充足
        cfg.kelly_fraction          = 0.25;
        cfg.market_exposure_pct     = 0.02;
        cfg.daily_loss_pct          = 0.03;
        cfg.consec_loss_n           = 5;
        cfg.stale_threshold_ms      = 30'000;
        cfg.stale_halt_ms           = 60'000;
        cfg.safety_buffer_pct       = 0.05;
        cfg.idempotency_ttl_hours   = 24;

        rm_ = std::make_unique<RiskManager>(cfg, clock_);
        rm_->force_state_for_test(RiskState::RUNNING);
    }

    std::shared_ptr<VirtualClock> clock_;
    int64_t                        hard_cap_usdc_;
    std::unique_ptr<RiskManager>   rm_;
};

TEST_F(RiskManagerExceedPerOrderCapTest, request_just_above_hard_cap_must_reject) {
    // 构造请求量 = HARD + 1 USDC, 一切其他字段合法
    auto intent = IntentFactory::valid_moneyline()
                      .with_size_usdc(hard_cap_usdc_ + 1)
                      .with_edge_bps(50)
                      .with_edge_ci_low_bps(20)
                      .build();

    auto d = rm_->evaluate(intent);

    EXPECT_EQ(d.decision,    RiskDecision::REJECTED);
    EXPECT_EQ(d.reject_code, RejectReason::EXCEED_PER_ORDER_CAP);
    EXPECT_EQ(d.approved_size_usdc, 0);
    EXPECT_FALSE(d.audit_id.empty());  // 即使拒也必产 audit
    EXPECT_EQ(d.state_snapshot, RiskState::RUNNING);
}

TEST_F(RiskManagerExceedPerOrderCapTest, request_exactly_hard_cap_after_kelly_must_still_clamp) {
    // 请求恰好等于 HARD, 但 Kelly sizing 后理论 size < HARD
    // 验"approved_size 必 <= min(intent.size, kelly, HARD)" — 即使有 edge 也不能超
    auto intent = IntentFactory::valid_moneyline()
                      .with_size_usdc(hard_cap_usdc_)
                      .with_edge_bps(500)            // 大 edge
                      .with_edge_ci_low_bps(400)     // 大 CI 下界
                      .with_price(0.5)
                      .build();

    auto d = rm_->evaluate(intent);

    // 不应该被拒 (恰好 = HARD), 但 approved_size 由 Kelly 收敛
    EXPECT_EQ(d.decision, RiskDecision::APPROVED);
    EXPECT_LE(d.approved_size_usdc, hard_cap_usdc_);
}

TEST_F(RiskManagerExceedPerOrderCapTest, soft_cap_cannot_be_set_above_hard_at_construction) {
    // 启动期校验: soft > hard 必须构造失败 (老韩 §8.4 启动 self-check)
    RiskManagerConfig cfg;
    cfg.per_order_cap_soft_usdc = hard_cap_usdc_ + 1;
    cfg.bankroll_usdc           = 100'000;
    // ... 其他字段
    EXPECT_DEATH(
        { RiskManager rm(cfg, clock_); },
        "per_order_cap_soft_usdc.*exceeds.*hard"
    );
}

TEST_F(RiskManagerExceedPerOrderCapTest, runtime_attempt_to_raise_soft_cap_must_reject_reload) {
    // 热 reload 调高 soft cap → reject + 沿用旧 (老韩 §3.3 + §8.5)
    auto new_cfg          = rm_->current_config();
    new_cfg.per_order_cap_soft_usdc = hard_cap_usdc_;  // 调到 HARD (前面 SetUp 已经设到 HARD)
    auto reload_status1   = rm_->reload_config(new_cfg);
    EXPECT_TRUE(reload_status1.ok());

    new_cfg.per_order_cap_soft_usdc = hard_cap_usdc_ + 100;  // 试图调高
    auto reload_status2   = rm_->reload_config(new_cfg);
    EXPECT_FALSE(reload_status2.ok());
    EXPECT_EQ(reload_status2.code(), StatusCode::kInvalidConfig);
}

}  // namespace stcpp::risk::test
```

**该 case 揭示的 RM 接口约束 (给老韩):**

1. `RiskManager::kPerOrderCapHardUsdc` 必须是 `constexpr` 公开常量, 测试能直接读.
2. `RiskManager` 构造接收 `clock_` 参数 (依赖注入), 不能内部 hard-code `system_clock`.
3. `force_state_for_test()` 是 friend / test-only API, 用 `#ifdef STCPP_TESTING` 隔离.
4. `RiskManagerConfig` 启动期 self-check, soft > hard → 进程 abort (老韩 §8.4).
5. `reload_config()` 返回 `Status`, 不静默 swap.
6. `RejectReason::EXCEED_PER_ORDER_CAP` 必须存在.
7. `RiskDecision` 必含 `audit_id` (即使拒).
8. `IntentFactory` 是测试工具, 提供 builder pattern 构造合法 OrderIntent (避免每个 case 重写 30 行 setup).

### 11.2 第一个该跑的 case (CI 上线前的 smoke)

**case 名:** `framework::smoke/replay_driver_decode_empty_stream`
**意图:** 验 ReplayDriver 能装载、解码、产 0 个事件后正常退出. 不依赖任何业务模块, 是框架自检.

**为什么:** 框架自身有 bug, 后面所有 test 都不可信. 这个 case 一过, 才开始铺 unit case.

**伪代码:**

```cpp
// tests/replay/framework_smoke_test.cc

#include "tests/helpers/replay_driver.h"
#include "tests/helpers/null_sut.h"
#include <gtest/gtest.h>

namespace stcpp::test {

TEST(ReplayDriverSmokeTest, empty_stream_drives_to_completion) {
    ReplayConfig cfg;
    cfg.speed = ReplaySpeed::Inf;

    ReplayDriver driver(cfg);
    ASSERT_TRUE(driver.load_empty()).ok();    // 内置 empty fixture

    NullSystemUnderTest sut;
    auto status = driver.run(sut);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(sut.events_seen(), 0);
    EXPECT_EQ(driver.virtual_clock().now_ns(), driver.start_ns());
}

TEST(ReplayDriverSmokeTest, single_event_dispatched_in_order) {
    ReplayConfig cfg;
    cfg.speed = ReplaySpeed::Inf;

    ReplayDriver driver(cfg);
    driver.inject_synthetic(SyntheticEvent::poly_wss_book_snapshot(
        /*monotonic_ns=*/100,
        /*market_id=*/"0xabc",
        /*bid=*/0.52, /*ask=*/0.53
    ));

    RecordingSystemUnderTest sut;
    auto status = driver.run(sut);

    EXPECT_TRUE(status.ok());
    ASSERT_EQ(sut.events_seen(), 1);
    EXPECT_EQ(sut.first_event().monotonic_ns, 100);
}

}  // namespace stcpp::test
```

这两个 case 加上 §4.1 的 RM enum 矩阵, 是 Sprint-1 末必交付的最小集合.

---

## 附录 A — 与 ticket 对应

- S1-014 (本文): 测试 + Replay 框架 v0.1.
- S1-001 (老周架构): 我落到 §1.3 目录结构 + §2.3 VirtualClock 替换 `infra/clock`.
- S1-002 (老李 Polymarket): fixture (§7.3) + mock_clob_server / mock_wss_server 配套.
- S1-003 (小段 Goalserve): fixture (§7.3) + mock_goalserve_server.
- S1-004 (老韩 RM): §4 全部 + §11 第一个 case.
- S1-005 / S1-016 (老孙 + 老沈 私钥 + 威胁): §4.3 绕过检测 + TB-B chaos (§3.1).
- S1-009 (老叶 RPC): §3.1 FaultProvider + mock_chain_rpc.
- S1-010 (老吴 部署): §6.3 CI/CD 对接.
- S1-011 (小石 + 老姜 lock-free + latency): §6.5 perf baseline 引用老姜 §11 预算.
- S1-018 (小郑 observability): chaos 期望含 metric assertion (§3.2 yaml), 与小郑 dashboard 关联.
- S1-019 (待出, 协同规范): §4.3.1 PR 流程对接.

---

## 附录 B — 已知风险

| # | 风险 | 缓解 |
|---|---|---|
| TR-1 | EventRecorder schema 没定, 我先用 stub | v0.1 stub 跑通框架, schema 锁定后 1 周内切真 decoder |
| TR-2 | mock 外部服务用真 socket, PR 跑变慢 | 单测仍用 in-process mock; 真 socket 留 sim/chaos, PR 跑 sim smoke 子集 |
| TR-3 | 跨洋链路真实抖动不可完全模拟 | netem 注入 + 真实跨洋节点录的 fixture 双管 |
| TR-4 | 覆盖率 KPI 化导致写垃圾测试 | §5.4 反 gaming + 季度人工 sample 复查 |
| TR-5 | chaos 失败难定位 (黑盒) | 每个 chaos case 必含: 注入 timeline + 期望 timeline + RM audit 对账, 失败时三者 diff 输出 |
| TR-6 | replay 与生产代码绑死, refactor 痛 | replay 走 RCU snapshot API, 不耦合内部数据结构; SUT 抽象层吸收 refactor |

---

## 附录 C — Sprint-1 末必交付的最小集合

| # | 交付物 | 形态 | 验收 |
|---|---|---|---|
| 1 | 本框架文档 v0.1 | 本文 | 老周 + 老韩 sign-off |
| 2 | 目录骨架 `tests/{unit,sim,replay,chaos,fixtures,helpers}/` | git skeleton + README | 老周 review |
| 3 | gtest + gmock + google-benchmark 接入 (`CMakeLists.txt`) | 编译通过 | 老何 review |
| 4 | `VirtualClock` + `ReplayDriver` skeleton | 跑通 §11.2 两个 smoke case | 老周 review |
| 5 | `risk_enum_coverage.py` + `risk_bypass_scan.py` (空跑) | CI 接通 | 老吴 + 老韩 review |
| 6 | §11.1 RM 第一个 case 骨架 (即使 RM 还没实现也能 compile + skip) | gtest 跑 PENDING | 老韩 review |
| 7 | fixture 命名规范 + 脱敏脚本 `desensitize.py` | 跑通 dry-run | 老沈 + 老黄 review |
| 8 | GitHub Actions `pr.yml` + `nightly.yml` 初版 | PR 触发跑过 | 老吴 review |
| 9 | 13 拒单 enum 覆盖矩阵 (§4.1) 初版 stub (全 PENDING) | CI 报告 13/13 PENDING | 老韩 review |

---

**END v0.1.** 等小余 schema / 老韩 RM 实现 / 老吴 CI 资源到位, 我再 bump v0.2.

— 小宋 (test-replay-engineer), 2026-05-28
