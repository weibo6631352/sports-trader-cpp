# Chaos + Replay 框架 Spec v1 (E-03 / E-04)

- **Owner:** 小宋 (test-replay-engineer, E 产品业务保障部)
- **Last review:** 2026-05-29
- **验收人:** 老胡 (E 主管) / 老韩 (RM 主权) / 老周 (架构) / 老唐 (audit schema)
- **关联 ticket:** E-03 chaos test framework (W10W3) / E-04 replay test framework (W11W1)
- **配套上游:**
  - `xiaosong-test-replay-framework-v0.1.md` (W5 基线, 本文升级到 v1)
  - `laotang-audit-schema-v1.1.md` (audit replay chain + 4 ts PIT)
  - `laohan-riskmanager-design-v0.3.1.md` (RM 状态机 + 拒单 enum)
  - `laolei-2026-drive-directive-paper-profit-v1.md` §5 关键路径 / §3 R-11 paper 零污染
  - `tests/integration/test_fixture.hpp` (PaperE2EFixture, W5 实现)
  - `tests/integration/r11_paper_pollution_test.cpp` (R-11 现有 5 case)
  - `tests/integration/audit_chain_verify_test.cpp` (chain verify T1-T4)
- **状态:** DRAFT — 待老胡 + 老韩 sign-off

---

## §0 TL;DR

本文交付两套框架的完整 spec，作为 W11 paper runtime 安全网：

| 框架 | 核心任务 | paper runtime gate 作用 |
|---|---|---|
| **chaos test framework (E-03)** | 5 类故障注入矩阵 × paper mode 场景 | paper runtime 启动前必过 chaos smoke；持续盈利窗口内每晚跑 full |
| **replay test framework (E-04)** | audit chain 一致性 + R-11 账本零污染 + R-20 4 ts 单调校验 | 每次 paper runtime 版本升级强制跑 replay gate |

两套框架共用同一 `ReplayDriver` 内核（v0.1 已 spec，本文补全实现细节）；chaos 是在 replay event stream 上叠加 `FaultProvider`，不是独立进程。

---

## §1 Chaos Test Framework (E-03)

### 1.1 总体设计原则

- chaos 不是独立框架，是 `ReplayDriver` + `FaultProvider` 的组合运行模式
- 同一 fixture 跑两遍（有/无 fault），行为差异即 chaos signal
- 每个 chaos case 必须输出：注入 timeline + RM audit 序列 + 期望 vs 实际 diff
- 失败时 diff 必须可读（不接受"failed at assertion line 42"的黑盒输出）

### 1.2 故障注入矩阵 (5 类 × 场景 × 断言)

#### 1.2.1 WSS 断连 (WssDisconnectProvider)

| 属性 | 内容 |
|---|---|
| **注入点** | `MockWssStub::disconnect_all()` / 生产: `infra/net/WssClient` hook |
| **注入参数** | `at_ns` (注入时刻), `duration_ms` (断连持续), `reconnect_ok` (bool, 重连是否成功) |
| **paper 专属场景** | paper runtime 跑 Moneyline pregame 时 WSS 断 15s，验 VirtualMatcher 不被脏数据填充 |

| 场景 ID | 描述 | 注入参数 | 期望系统行为 |
|---|---|---|---|
| C-WSS-01 | 比赛进行中 WSS 断 15s | duration=15s, reconnect=true | RM → WARNING(STALE_DATA); 断连期间 0 新 intent 产生; 15s 内重连 + 全量重订阅 |
| C-WSS-02 | WSS 断 > 30s stale 阈值 | duration=35s, reconnect=true | RM → HALTED; 重连后恢复 RUNNING; audit 含 `STALE_DATA` reject_code ≥ 1 条 |
| C-WSS-03 | WSS 断 + 重连失败 3 次 | reconnect=false, retries=3 | RM 维持 HALTED; 不崩溃; alert log 出 reconnect_fail_count=3 |
| C-WSS-04 (paper) | paper runtime 启动阶段 WSS 断 | at=startup+2s | paper engine 不产生任何 VirtualFill; WAL 无 position 写入 |

**共同断言:**
```
ASSERT RM state IN (WARNING, HALTED) within 30s of disconnect
ASSERT no_orders_during(disconnect_start, disconnect_end)
ASSERT reconnect_before(disconnect_end + 30s) IF reconnect=true
ASSERT audit.reject_code == STALE_DATA IF duration > stale_threshold
ASSERT paper_audit.HighWatermark 不回退 (R-11: 断连不污染已写记录)
```

#### 1.2.2 消息乱序 (WssOutOfOrderProvider)

| 属性 | 内容 |
|---|---|
| **注入点** | `MockPmWss::push()` 之前插入乱序 shim，交换相邻 N 条消息的 seq |
| **注入参数** | `window_size` (乱序窗口), `shuffle_probability` (0.0-1.0), `at_ns` |

| 场景 ID | 描述 | 注入参数 | 期望系统行为 |
|---|---|---|---|
| C-OOO-01 | seq 跳 1 (单条丢失) | window=2, gap=1 | book builder 检测 seq gap → 触发 REST 全量补；补完前 book 不更新 |
| C-OOO-02 | 乱序窗口 5 条 | window=5, prob=0.5 | book builder 缓冲 + 重排；R-20 ts 不等式校验各条独立；不产生负时间戳 intent |
| C-OOO-03 (paper) | paper runtime 接收乱序 book 快照 | window=3 | VirtualMatcher 不接受 stale quote（quote_ts < last_fill_ts）；paper_audit 无乱序 decision |

**共同断言:**
```
ASSERT book.seq_gap_detected == true IF gap injected
ASSERT REST_fallback_triggered == true IF gap injected
ASSERT intent.event_ts <= intent.data_source_ts (R-20 invariant, per intent)
ASSERT no_negative_timestamp_in_any_intent
```

#### 1.2.3 延迟尖峰 (LatencySpikeProvider)

| 属性 | 内容 |
|---|---|
| **注入点** | `MockClobStub::set_response_delay_ms()` / `infra/net/TlsSession` 中间层 |
| **注入参数** | `spike_ms` (注入延迟), `spike_count` (持续多少次请求), `target` (REST/WSS/RPC) |
| **paper 专属** | 跨洋链路抖动模拟，验 paper engine 决策不因延迟而使用过期 book 数据 |

| 场景 ID | 描述 | 注入参数 | 期望系统行为 |
|---|---|---|---|
| C-LAT-01 | REST 5s 慢响应 | spike_ms=5000, target=REST | WSS event loop tick < 50us (R-12 §S-1)；单测已有 r12_sim/s1_rest_slow_wss_unblock_test |
| C-LAT-02 | REST 持续慢响应 10 次 | spike_ms=3000, count=10 | backoff 生效；不死锁；总等待 < 60s |
| C-LAT-03 | RPC 延迟尖峰 2s | spike_ms=2000, target=RPC | signer 超时处理；paper signer VirtualConfirm 返回超时 error；不崩 |
| C-LAT-04 (paper) | paper runtime 全链延迟叠加 | spike_ms=200, all targets | end-to-end latency p99 < 500ms；PaperSigner.Sign 不超时崩溃 |

**共同断言（继承 R-12）:**
```
ASSERT wss_event_loop_tick_p99_us < 50 (R-12 红线)
ASSERT no_blocking_call_in_wss_loop (CI static scan 配套)
ASSERT paper_signer_error != TIMEOUT (paper runtime: VirtualConfirm 宽松超时)
ASSERT no_order_with_stale_book (as_of_ts - book_snapshot_ts < staleness_threshold)
```

#### 1.2.4 REST 超时 (RestTimeoutProvider)

| 属性 | 内容 |
|---|---|
| **注入点** | mock REST server 返回 429 / 502 / 504 / 无响应（连接挂起） |
| **注入参数** | `status_code` (HTTP 状态), `path_filter` (正则匹配哪些 endpoint), `at_ns` |

| 场景 ID | 描述 | 注入参数 | 期望系统行为 |
|---|---|---|---|
| C-REST-01 | clob REST 429 限流 | status=429, all paths | exponential backoff；不打死；不误判为 server down；拒单率暂升但不 HALT |
| C-REST-02 | gamma 502 快照失败 | status=502, path=/markets | 切 fallback endpoint（老叶 D8 multi-provider）；book 降级到 WSS only |
| C-REST-03 | REST 完全超时（无响应） | status=timeout, timeout_ms=10000 | client 在 deadline 内主动断；不阻塞 WSS loop；alert log 出 rest_timeout_count |
| C-REST-04 (paper) | paper runtime 启动时 REST 快照超时 | status=timeout, path=/positions | paper engine 以空 position state 启动；不写 position WAL；R-11 保持 |

**共同断言:**
```
ASSERT http_client.retry_count <= max_retries (不无限重试)
ASSERT wss_event_loop_unblocked (独立线程验证)
ASSERT paper_position_wal.HighWatermark == 0 IF REST startup fails (R-11)
ASSERT audit_contains(reject_code=STALE_DATA) IF rest_failure > stale_threshold
```

#### 1.2.5 部分成交回灌 (PartialFillProvider)

| 属性 | 内容 |
|---|---|
| **注入点** | `VirtualMatcher::Match()` stub；注入分批 fill（多次部分成交替代一次全成交） |
| **注入参数** | `fill_ratio` (0.0-1.0, 首次成交比例), `fill_count` (分批次数), `slippage_bps` |
| **paper 专属** | VirtualMatcher Mode A 分批撮合，验 PnL 计算不重复计入、ledger 不超量 |

| 场景 ID | 描述 | 注入参数 | 期望系统行为 |
|---|---|---|---|
| C-FILL-01 | 50% 部分成交 | ratio=0.5, count=2 | position_ledger 累计 = intent.size (最终全成); 不超额; audit 2 条 ORDER_FILL |
| C-FILL-02 | 极小首批 5% 成交 | ratio=0.05, count=20 | 20 次 FILL audit; position 单调递增; 不中途触发 EXCEED_MARKET_EXPOSURE |
| C-FILL-03 | 部分成交 + 市场反转 | ratio=0.3 + price_move | 剩余 70% 以新价格撮合; slippage 计入 net PnL; GM-PAPER-G slippage cost 扣 |
| C-FILL-04 (paper) | VirtualMatcher 部分成交写 paper WAL | ratio=0.6, paper_mode=true | VirtualFill.audit_wal_kind == PaperAudit; position WAL = paper path; 不写 live position |

**共同断言:**
```
ASSERT sum(fill.size) == intent.approved_size (最终全成场景)
ASSERT fill.audit_wal_kind == PaperAudit (R-11, paper mode)
ASSERT position_ledger[market] == sum(fills) (不重复计入)
ASSERT net_pnl includes slippage_cost (GM-PAPER-G: 扣 fee+slippage+spread)
ASSERT no position write to live_position_wal (R-11)
```

### 1.3 Paper Runtime 专属 Chaos 场景汇总

以下场景专门针对 paper runtime (W11)，是 paper gate 前的**强制安全网**：

| 场景 ID | 类别 | 场景描述 | 通过条件 |
|---|---|---|---|
| C-PAPER-01 | WSS断连 | paper runtime 运行中 WSS 断 15s | VirtualMatcher 暂停；WAL 无 position 写入；重连后恢复 |
| C-PAPER-02 | 乱序 | paper engine 接收乱序 book，连续 5 笔 intent | 每笔 R-20 4 ts 单调；无 stale quote 进 VirtualMatcher |
| C-PAPER-03 | 延迟尖峰 | paper 全链 p99 延迟 > 200ms | paper_signer 不崩；VirtualConfirm 宽松超时不 HALT |
| C-PAPER-04 | REST 超时 | paper 启动时 positions REST 超时 | 以空 state 启动；R-11 保持（position WAL 空） |
| C-PAPER-05 | 部分成交 | VirtualMatcher 50% 部分成交 20 笔 | PnL 计算正确；无账本污染；GM-PAPER-G 报表可出 |
| C-PAPER-06 | 多故障组合 | WSS断 + REST慢 同时注入 | 不崩；HALTED 后人工 ack 恢复；audit chain 不断 |

### 1.4 Chaos Gate 判定规则

```
chaos_smoke_pass := ALL(C-PAPER-01 ~ C-PAPER-06 PASS)
                 AND ALL(C-WSS-01, C-REST-01, C-FILL-04 PASS)  // 核心 3 条

paper_runtime_gate := chaos_smoke_pass
                   AND replay_gate_pass  (见 §2.4)
                   AND r11_pollution_test_pass (现有 T1-T5)
```

---

## §2 Replay Test Framework (E-04)

### 2.1 设计目标 (升级 v0.1)

本节在 `xiaosong-test-replay-framework-v0.1.md` §2 基础上，补充 E-04 专项内容：

| 目标 | v0.1 | v1 新增 |
|---|---|---|
| R1 确定性回放 | 同 fixture → 同 decision 序列 | **同 audit chain hash 序列**（BLAKE3 chain head 可还原） |
| R4 audit 对账 | replay 产同款 audit_id 序列 | **老唐 v1.1 audit chain replay：prev_hash → current_hash 链式验证** |
| **R5 R-11 校验** | (新增) | replay 断言 paper mode 所有 WAL 写入落 paper 路径，不染 live |
| **R6 R-20 校验** | (新增) | replay 每条 event 断言 4 ts 单调不等式；违例计数 > 0 → FAIL |
| **R7 audit chain 一致性** | (新增) | replay 结束时 chain head 必与 golden chain head 一致（允许 audit_id ULID timestamp 部分漂移） |

### 2.2 Audit Chain Replay 一致性 (接老唐 W10W2)

老唐 `laotang-audit-schema-v1.1.md` 定义了 BLAKE3 chain (`prev_hash → ChainCombine(prev, payload_digest) → current_hash`)。replay 必须能完整重建这条链。

#### 2.2.1 链式重算协议

```
AuditChainReplayAssertion:

on_decision(ctx: RiskDecisionInput):
  seq = emitted_count + 1
  digest = BLAKE3_payload_hash(seq, ctx.event_type, ctx.decision_ts)
  expected_current = BLAKE3_chain_combine(mirror_prev, digest)
  mirror_prev = expected_current
  
  ASSERT emitter.last_hash() == expected_current
       "R-audit-chain: replay decision #{seq} chain head mismatch"

finalize():
  ASSERT mirror_prev == golden_chain_head
       "R-audit-chain: replay 结束 chain head 必与 golden 一致"
  ASSERT emitted_count == golden_record_count
       "R-audit-chain: replay record 数量必与 golden 一致"
```

**关键约束（继承老唐 v1.1 §4）：**
- BLAKE3 非 debug build，走真 BLAKE3（`BLAKE3_REAL=1`）
- ULID 时间戳部分允许漂移（VirtualClock 推进时刻与 wall clock 不同），但 ULID random 部分要求同 seed 可复现
- `payload.ingested_at_ns == header.ingestion_ts`（v1.1 双轨校验），replay 必须同时验两处

#### 2.2.2 Replay 断言：策略衰减事件 (AET_STRATEGY_DECAYED / AET_STRATEGY_UNLOCK)

老唐 v1.1 新增 14 → 15 AET（DECAYED/UNLOCK）。replay 必须能回放这两类事件：

```
on_strategy_decayed(payload: StrategyDecayedPayload):
  ASSERT payload.p_mu_negative >= 0.3 (BAYES_DECAY_P_THRESHOLD)
  ASSERT payload.sustained_days >= 14
  ASSERT payload.state_after == BLACK
  // 不在 replay 中重新调用三签（只验记录格式合法性）
  ASSERT payload.approver_cpo == "" OR payload.approver_gm == ""
       "DECAYED event 不需要三签，UNLOCK 才需要"

on_strategy_unlock(payload: StrategyUnlockPayload):
  ASSERT payload.approver_cpo != "" AND payload.approver_gm != "" AND payload.approver_financial != ""
       "R-unlock-3sig: UNLOCK 必须三签全非空"
  ASSERT payload.decay_audit_id links to prior AET_STRATEGY_DECAYED
       "因果链：UNLOCK 必须有对应 DECAYED audit_id"
  ASSERT payload.kelly_multiplier == 0.5
       "MONITORING 期 Kelly 0.5x"
```

### 2.3 R-11 校验：Paper Mode 账本零污染断言

在 v0.1 基础上，E-04 将 R-11 校验从 integration test 提升为 replay assertion，使其能在历史数据回放中持续验证。

#### 2.3.1 PaperLedgerIsolationAssertion

```cpp
// tests/replay/paper_r11_assertion.hpp

class PaperLedgerIsolationAssertion final : public ReplayAssertion {
public:
    // 在 replay 开始时注入 4 个 WalWriter 的 HighWatermark 基线
    void set_baselines(uint64_t paper_audit_base,
                       uint64_t risk_audit_base,
                       uint64_t position_base,
                       uint64_t shadow_audit_base);

    Status on_decision(const RiskDecision& d) override {
        if (d.mode == ExecutionMode::Paper) {
            ASSERT d.audit_wal_kind == WalKind::PaperAudit
                "R-11: paper decision must write to PaperAudit WAL";
        }
        return Status::Ok();
    }

    Status on_fill(const Fill& f) override {
        if (f.mode == ExecutionMode::Paper) {
            ASSERT f.audit_wal_kind == WalKind::PaperAudit
                "R-11: paper fill must write to PaperAudit WAL";
        }
        return Status::Ok();
    }

    Status finalize() override {
        // 回放结束时，3 条 live WAL 水位线必须未变
        ASSERT risk_audit_writer->HighWatermark() == risk_audit_base_
            "R-11: paper replay 不得写 risk_audit WAL";
        ASSERT position_writer->HighWatermark() == position_base_
            "R-11: paper replay 不得写 position WAL";
        ASSERT shadow_audit_writer->HighWatermark() == shadow_audit_base_
            "R-11: paper replay 不得写 shadow_audit WAL";
        // paper_audit 必须有新增记录
        ASSERT paper_audit_writer->HighWatermark() > paper_audit_base_
            "R-11: paper replay 必须有 PaperAudit 写入";
        return Status::Ok();
    }
};
```

#### 2.3.2 R-11 Replay 场景矩阵

| 场景 ID | 描述 | 输入 | 通过条件 |
|---|---|---|---|
| R-R11-01 | 50 笔 paper e2e 历史回放 | 50 条 event (含 approved + rejected) | paper_audit HWM > 0; risk_audit/position/shadow HWM == 初始值 |
| R-R11-02 | paper mode 切换检测 | 含 1 条 mode=live 误注入的 event | replay assertion 检出 R-11 violation; FAIL 且定位到事件序号 |
| R-R11-03 | paper runtime 全天日志回放 | 一日完整 paper audit WAL | 所有 WAL 写入在 paper 路径; GM-PAPER-G 报表字段全出 |
| R-R11-04 | SignResponse.audit_wal_kind 全程校验 | 含 signer 输出的 event | 每条 SignResponse.audit_wal_kind == PaperAudit (T2 升级为 replay) |

### 2.4 R-20 四时间戳单调校验

#### 2.4.1 TimestampMonotonicityAssertion

```cpp
// tests/replay/r20_ts_assertion.hpp

class TimestampMonotonicityAssertion final : public ReplayAssertion {
public:
    Status on_event(const ReplayEvent& ev) override {
        const auto& ts = ev.timestamps;
        
        // R-20 不等式: event_ts <= data_source_ts <= ingestion_ts <= as_of_ts
        if (ts.event_ts <= 0) {
            ++violation_count_;
            record_violation(ev.seq, "event_ts == 0 (禁用本地 now() 替代上游 ts)");
            return Status::PitViolation();
        }
        if (ts.data_source_ts < ts.event_ts) {
            ++violation_count_;
            record_violation(ev.seq, "data_source_ts < event_ts");
            return Status::PitViolation();
        }
        if (ts.ingestion_ts < ts.data_source_ts) {
            ++violation_count_;
            record_violation(ev.seq, "ingestion_ts < data_source_ts");
            return Status::PitViolation();
        }
        if (ts.as_of_ts < ts.ingestion_ts) {
            ++violation_count_;
            record_violation(ev.seq, "as_of_ts < ingestion_ts");
            return Status::PitViolation();
        }
        return Status::Ok();
    }

    Status finalize() override {
        if (violation_count_ > 0) {
            // 输出所有违例记录（seq + 违例类型）
            dump_violations();
            return Status::Fail("R-20: " + std::to_string(violation_count_) + " 条 4 ts 违例");
        }
        return Status::Ok();
    }
    
    // data_source_ts_source enum 月度审查（老唐 v1.1 §2.y）
    void check_ts_source_enum(const DataSourceTimestamp& dsts) {
        if (dsts.data_source_ts_source == DataSourceTimestamp::Source::INFERRED_FROM_INGESTION) {
            ++inferred_ts_count_;  // 不 FAIL，但月度 sweep 小冯用
        }
    }

private:
    uint64_t violation_count_{0};
    uint64_t inferred_ts_count_{0};
    std::vector<ViolationRecord> violations_;
};
```

#### 2.4.2 R-20 Replay 场景矩阵

| 场景 ID | 描述 | 期望 |
|---|---|---|
| R-R20-01 | 正常历史流（7 天 paper run） | violation_count == 0; inferred_ts_count 记录供月度 sweep |
| R-R20-02 | 注入 event_ts=0 的 event | 检出 violation；定位到 seq 号；replay FAIL |
| R-R20-03 | 注入 data_source_ts < event_ts | 检出；定位；FAIL |
| R-R20-04 | 注入 ingestion_ts < data_source_ts | 检出；定位；FAIL |
| R-R20-05 | 注入 as_of_ts < ingestion_ts | 检出；定位；FAIL |
| R-R20-06 | WAL header 4 ts 与 payload 4 ts 对比 | header.ingestion_ts == payload.ingested_at_ns（老唐 v1.1 双轨校验） |
| R-R20-07 (paper) | paper runtime 30 日窗口 attestation 校验 | 0 违例 + 小余 (D 主管) 签字前提：attestation 文件输出 |

### 2.5 Replay Gate 判定规则

```
replay_gate_pass :=
    audit_chain_assertion.finalize() == OK
    AND paper_r11_assertion.finalize() == OK
    AND r20_ts_assertion.finalize() == OK (violation_count == 0)
    AND golden_chain_head_match == true
    AND replay_record_count == golden_record_count
```

**GM-PAPER-G 配套要求（§3 R-11/R-20 零失效）：**
- `30 日窗口 replay gate` 是 GM-PAPER-G 数据 attestation 的技术支撑
- 小余 (D 主管) 签字前提：`r20_ts_assertion.finalized_violation_count() == 0`
- 老韩 (RM 主权) 签字前提：`paper_r11_assertion.finalized_live_wal_delta() == 0`

---

## §3 与 Paper Runtime 集成点

### 3.1 Paper Runtime 启动前 Gate（双层）

```
paper_runtime_启动条件:
  LAYER 1 (编译期/静态):
    - ExecutionMode::kCompiledMode == Paper (CMake STCPP_EXEC_MODE=paper)
    - AuditWalKindForBuild() == WalKind::PaperAudit
    - CI grep: r33_5host_paper.py PASS

  LAYER 2 (运行前 test gate):
    - chaos_smoke_pass (§1.4, 含 C-PAPER-01~06)
    - replay_gate_pass (§2.5, 含 R-R11-01~04 + R-R20-01~07)
    - r11_paper_pollution_test 现有 T1-T5 全 PASS
    - audit_chain_verify 现有 T1-T4 全 PASS
```

**强制顺序（关键路径衔接 §5 directive）：**
```
ABI v0.5 → RM v0.5 (老韩) → audit replay (老唐 W10W2) 
→ [E-04 replay gate] → REST 接真 state (小卢) 
→ [E-03 chaos smoke] → paper runtime 启动 (老吴 Frankfurt + 老高 CI)
→ [每日 chaos full nightly] → 30 日 paper run
→ [replay attestation] → GM-PAPER-G
```

### 3.2 Paper Runtime 运行中持续验证

| 频率 | 套件 | 内容 | 失败处理 |
|---|---|---|---|
| 每个 PR | chaos smoke (10 核心) + r11 T1-T5 | paper mode 基本安全性 | 拒 merge |
| 每晚 nightly | chaos full + replay 7d golden | 全故障矩阵 + audit chain 一致性 | alert + 阻断下一次 paper runtime 版本更新 |
| 每周 weekly | chaos full 延长版 (4h) + fuzz | libFuzzer 压 intent/book decoder | alert + INCIDENT 自动开 |
| 30 日窗口末 | replay attestation full run | R-20 violation_count==0 + chain head 一致 | 老韩/老余 不签字 → GM-PAPER-G 不通过 |

### 3.3 Paper Runtime 版本升级 Gate

每次 paper runtime 二进制更新前必须：
1. 以**新 binary** 完整回放**最近 7 天**的 paper audit WAL
2. `audit_chain_assertion` chain head 与旧 binary 跑出的 golden 一致
3. `paper_r11_assertion` 通过（不产生新的 live WAL 写入）
4. `r20_ts_assertion` violation_count == 0

---

## §4 tests/ 目录落点建议

### 4.1 新增目录（在现有结构基础上）

```
tests/
  chaos/                          # E-03 (新建)
    wss/
      c_wss_01_disconnect_15s.cpp
      c_wss_02_stale_halt.cpp
      c_wss_03_reconnect_fail.cpp
      c_wss_04_paper_startup.cpp
      CMakeLists.txt
    ooo/
      c_ooo_01_seq_gap.cpp
      c_ooo_02_window_shuffle.cpp
      c_ooo_03_paper_stale_quote.cpp
      CMakeLists.txt
    latency/
      c_lat_01_rest_5s.cpp         # 复用 r12_sim/s1 fixture
      c_lat_02_rest_sustained.cpp
      c_lat_03_rpc_spike.cpp
      c_lat_04_paper_e2e.cpp
      CMakeLists.txt
    rest_timeout/
      c_rest_01_429.cpp
      c_rest_02_502_gamma.cpp
      c_rest_03_timeout.cpp
      c_rest_04_paper_startup.cpp
      CMakeLists.txt
    partial_fill/
      c_fill_01_50pct.cpp
      c_fill_02_5pct_many.cpp
      c_fill_03_price_move.cpp
      c_fill_04_paper_wal.cpp
      CMakeLists.txt
    paper/                         # paper 专属组合场景
      c_paper_01_wss_during_run.cpp
      c_paper_02_ooo_5intents.cpp
      c_paper_03_latency_spike.cpp
      c_paper_04_rest_startup_timeout.cpp
      c_paper_05_partial_fill_20.cpp
      c_paper_06_multi_fault.cpp
      CMakeLists.txt
    playbooks/                     # chaos yaml 剧本 (v0.1 §3.2)
      c_paper_01_wss.yaml
      c_paper_06_multi_fault.yaml
      ...
    chaos_fixture.hpp              # 共享 FaultProvider + ChaosE2EFixture (继承 PaperE2EFixture)
    CMakeLists.txt

  replay/                          # E-04 (新建)
    r11/
      r_r11_01_50_paper_e2e_replay.cpp
      r_r11_02_live_mode_injection.cpp
      r_r11_03_full_day_replay.cpp
      r_r11_04_sign_resp_wal_kind.cpp
      CMakeLists.txt
    r20/
      r_r20_01_normal_7d.cpp
      r_r20_02_event_ts_zero.cpp
      r_r20_03_ds_lt_event.cpp
      r_r20_04_ingestion_lt_ds.cpp
      r_r20_05_asof_lt_ingestion.cpp
      r_r20_06_wal_header_payload.cpp
      r_r20_07_paper_30d_attestation.cpp
      CMakeLists.txt
    audit_chain/
      r_chain_01_20_records.cpp    # 升级自 audit_chain_verify_test.cpp T1
      r_chain_02_tamper.cpp        # 升级自 T2
      r_chain_03_strategy_decayed.cpp
      r_chain_04_strategy_unlock.cpp
      r_chain_05_golden_head_match.cpp
      CMakeLists.txt
    paper/
      paper_r11_assertion.hpp      # PaperLedgerIsolationAssertion
      r20_ts_assertion.hpp         # TimestampMonotonicityAssertion
      audit_chain_replay_assertion.hpp
      CMakeLists.txt
    golden/                        # 黄金路径 fixture (从 v0.1 §2.4)
      nba_moneyline_7d.msgpack     # 待小余 EventRecorder 就绪后填
    CMakeLists.txt
```

### 4.2 与现有结构衔接

| 现有文件/目录 | 关系 | 处理方式 |
|---|---|---|
| `tests/integration/test_fixture.hpp` (PaperE2EFixture) | chaos 新 `ChaosE2EFixture` 继承它 | 不改现有文件，子类扩展 |
| `tests/integration/r11_paper_pollution_test.cpp` (T1-T5) | E-04 R-11 场景是其 replay 版本 | 并行存在；replay 版增加 golden 对账 |
| `tests/integration/audit_chain_verify_test.cpp` (T1-T4) | E-04 audit_chain 场景是其 replay 版本 | T1/T2 直接迁移到 `replay/audit_chain/`；integration 版保留作 PR smoke |
| `tests/sim/r12_sim/` (S1-S4) | chaos latency 场景复用 S1 fixture | `c_lat_01_rest_5s.cpp` include `r12_sim_fixture.hpp` |
| `tests/ci_grep/r20_pit_chain.py` | E-04 R-20 静态检查配套 | 不改；replay assertion 是运行期补充 |
| `tests/ci_grep/r33_5host_paper.py` | paper runtime 编译期 gate | 不改；两者并列 |

### 4.3 CMake 集成

```cmake
# tests/CMakeLists.txt 追加（不改现有 target）

# E-03 chaos suite
add_subdirectory(chaos)

# E-04 replay suite
add_subdirectory(replay)

# CTest labels for gate separation
# chaos_smoke: C-PAPER-01~06 + C-WSS-01 + C-REST-01 + C-FILL-04
# chaos_full: 全部 chaos
# replay_gate: R-R11-01~04 + R-R20-01~07 + R-chain-01~05
# paper_runtime_gate: chaos_smoke + replay_gate (两者都 PASS 才放行)
```

---

## §5 关键设计约束

### 5.1 语言与工具

- 全部 C++20（CLAUDE.md 铁律：chaos/replay 框架是测试代码，不进生产，但仍用 C++20）
- gtest v1.14+ (v0.1 §9.1 决定)
- 禁止在 `src/` 出现 `std::this_thread::sleep_for` / `std::chrono::system_clock::now`（VirtualClock 替代）
- chaos yaml playbook 用 yaml（v0.1 §3.2 决定，v0.1 TQ-11）

### 5.2 确定性要求

- VirtualClock: 所有 chaos/replay case 使用 `VirtualClock` 替换 `infra/clock/SteadyClock`，时间仅由 `ReplayDriver` 推进
- VirtualMatcher seed 固定（`0xBEEFCAFEULL`，现有 `test_fixture.hpp` 已设）
- BLAKE3 replay 重算：`BLAKE3_REAL=1` build flag 下用真 BLAKE3；debug build 下 XOR stub（向后兼容）

### 5.3 R-12 严格遵守

chaos latency 场景必须持续测量 WSS event loop tick：
```
ASSERT wss_event_loop_tick_p99_us < 50 (R-12 §17.1.1 量化口径)
```
复用 `r12_sim_fixture.hpp` 的 `p99_ns()` 工具函数，不重复实现。

### 5.4 paper mode 防串

chaos/replay 两套框架运行时均须校验：
```
ASSERT kCompiledMode == ExecutionMode::Paper (R-7 build-time lock)
ASSERT AuditWalKindForBuild() == WalKind::PaperAudit (R-11 build-time)
```
这两条断言放入 `ChaosE2EFixture::SetUp()` 和 `ReplayDriver::Init()`，不依赖测试写者记住。

---

## §6 开放问题（需上游解锁）

| # | 议题 | 前置依赖 | 处理方式 |
|---|---|---|---|
| OQ-01 | `VirtualMatcher` 分批 fill API 还未实现（C-FILL 类依赖） | 小袁 (FillRateModel/VirtualMatcher, W11) | E-03 C-FILL 场景 stub 先跑；小袁就绪后切真实现 |
| OQ-02 | EventRecorder schema 未锁版（replay golden fixture 无法生产） | 小余 (EventRecorder v0.1) | E-04 replay golden 场景 stub；schema 到位后 1 周内切真 decoder |
| OQ-03 | audit chain replay golden head 无生产数据 | paper runtime 首次运行 | 首跑产出 golden chain head 后才能跑 `R-chain-05-golden_head_match` |
| OQ-04 | `data_source_ts_source` enum 月度 sweep 工具（小冯） | 小冯 实时 feed+reconnect (8-15) | R-R20-07 attestation 里只计数 inferred_ts_count；sweep 工具小冯负责 |
| OQ-05 | 三签 UNLOCK replay 验证需要真实 approver 字段 | 无（paper 阶段不触发真实 UNLOCK） | 构造合成 UNLOCK event 验格式；不验真实人工签名 |

---

## §7 验收清单（老胡 + 老韩 sign-off 用）

| # | 验收项 | 状态 |
|---|---|---|
| A1 | chaos suite CMakeLists 接入，`ctest -L chaos_smoke` 可跑 | 待实现 |
| A2 | C-PAPER-01~06 全 PASS（paper runtime 专属） | 待实现 |
| A3 | C-WSS-01/02/03 PASS（断连 + stale halt + 重连失败） | 待实现 |
| A4 | C-REST-01/02/03/04 PASS（限流 + 502 + 超时 + paper 启动） | 待实现 |
| A5 | C-FILL-04 PASS（VirtualMatcher paper WAL 路径，R-11 验证） | 待实现 |
| A6 | replay suite CMakeLists 接入，`ctest -L replay_gate` 可跑 | 待实现 |
| A7 | R-R11-01/02/03/04 PASS（paper 账本零污染） | 待实现 |
| A8 | R-R20-01~06 PASS（4 ts 单调，全场景） | 待实现 |
| A9 | R-chain-01/02/03/04 PASS（BLAKE3 chain + DECAYED/UNLOCK） | 待实现 |
| A10 | `paper_runtime_gate` label 集成进 CI nightly | 待老高 CI 接 |
| A11 | 本 spec 老韩 sign-off（RM 断言 + R-11 判据） | 待签 |
| A12 | 本 spec 老唐 sign-off（audit chain replay 协议） | 待签 |

---

*小宋 (test-replay-engineer), 2026-05-29 — E-03 chaos + E-04 replay framework spec v1。老胡 W10W3 验收。*
