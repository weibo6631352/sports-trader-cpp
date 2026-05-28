# 测试框架 C++ 接口骨架 v1 (Sprint-2 W2)

- Owner: 小宋 (test-replay-engineer)
- Date: 2026-05-28
- 关联设计: `docs/RESEARCH/xiaosong-test-framework-v0.2.md`
- 关联 RM: `docs/RESEARCH/laohan-riskmanager-design-v0.3.md`
- 关联 ADR: R-12 (`gm-redline-websocket-non-blocking.md`) / R-20 (`gm-redline-data-source-timestamping.md`)
- 状态: skeleton 锁版, W3 落代码

---

## 1. tests/ 目录 + CMakeLists.txt 骨架

```
tests/
├── CMakeLists.txt
├── helpers/                  # OBJECT lib, 4 target 共用
│   ├── virtual_clock / replay_driver / fault_provider
│   ├── mock_{clob,wss}_server / mock_chain_rpc
│   ├── risk_intent_factory / null_sut / wal_reader_adapter        # 接老王 WAL
│   └── {golden_log,single_flight,pit}_assertion.{h,cc}            # R-12 S-2 / R-20
├── unit/{risk,exec,infra,data,strategy,numerical}/
├── sim/{risk_sim,exec_sim,feature_sim,boundary_sim,r12_sim}/
├── replay/{golden,edge,paper,shadow,incident}/
├── chaos/{network,clock,api,process,r12_chaos}/playbooks/*.yaml
├── fuzz/{poly_wss_decoder,goalserve_parser,risk_intent_validator}/
├── perf/{risk_evaluate_bench,signer_sign_bench,slippage_bench}/
└── fixtures/{polymarket,goalserve,chain,intent_corpus,golden_pnl,_schema}/
```

```cmake
# tests/CMakeLists.txt (摘要)
add_library(stcpp_test_helpers OBJECT helpers/*.cc)
target_link_libraries(stcpp_test_helpers PUBLIC GTest::gmock_main stcpp_audit_wal_replay)
add_executable(stcpp_test_unit    $<TARGET_OBJECTS:stcpp_test_helpers> unit/...)
add_executable(stcpp_test_sim     $<TARGET_OBJECTS:stcpp_test_helpers> sim/...)
add_executable(stcpp_test_replay  $<TARGET_OBJECTS:stcpp_test_helpers> replay/...)
add_executable(stcpp_test_chaos   $<TARGET_OBJECTS:stcpp_test_helpers> chaos/...)
# nm 黑名单 (S2-028 hard block), 见 tools/ci/nm_blacklist.sh
add_test(NAME nm_blacklist_sim    COMMAND nm_blacklist.sh stcpp_test_sim    'polygon_rpc_real|chain_submit_real')
add_test(NAME nm_blacklist_replay COMMAND nm_blacklist.sh stcpp_test_replay 'polygon_rpc_real|chain_submit_real|sleep_for')
```

---

## 2. gtest fixtures (核心 3 个)

### 2.1 `RiskManagerFixture` — 21 enum 全覆盖共享 base

```cpp
// tests/helpers/risk_manager_fixture.h
namespace stcpp::test {
class RiskManagerFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    clock_ = std::make_shared<VirtualClock>(1'700'000'000'000'000'000LL);
    cfg_   = default_config();   // §7 老韩 v0.3 默认参数
    rm_    = std::make_unique<risk::RiskManager>(cfg_, clock_);
    rm_->force_state_for_test(risk::RiskState::RUNNING);
  }
  static risk::RiskManagerConfig default_config();
  void expect_reject  (const risk::OrderIntent&, risk::RejectReason);
  void expect_approve (const risk::OrderIntent&);
  void expect_deferred(const risk::OrderIntent&);
  std::shared_ptr<VirtualClock>      clock_;
  risk::RiskManagerConfig            cfg_;
  std::unique_ptr<risk::RiskManager> rm_;
};

#define EXPECT_REJECT_CODE(intent, code) \
  do { auto d = rm_->evaluate(intent); \
       EXPECT_EQ(d.decision, risk::RiskDecision::REJECTED); \
       EXPECT_EQ(d.reject_code, risk::RejectReason::code); \
       EXPECT_FALSE(d.audit_id.empty()); } while (0)
}  // namespace stcpp::test
```

### 2.2 `SimHarnessFixture` — sim 层共享 (3 mock server + paper-mode SUT)

```cpp
class SimHarnessFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    clock_     = std::make_shared<VirtualClock>(0);
    clob_srv_  = std::make_unique<MockClobServer>(0);  clob_srv_->start();   // OS 分端口
    wss_srv_   = std::make_unique<MockWssServer>(0);   wss_srv_->start();
    chain_rpc_ = std::make_unique<MockChainRpc>(0);    chain_rpc_->start();
    sut_       = std::make_unique<SystemUnderTest>(SutConfig{
      .clob_url=clob_srv_->url(), .wss_url=wss_srv_->url(), .rpc_url=chain_rpc_->url(),
      .clock=clock_, .mode=exec::ExecutionMode::Paper});                      // sim 默认 paper
  }
  void TearDown() override { sut_.reset(); clob_srv_->stop(); wss_srv_->stop(); chain_rpc_->stop(); }
  std::shared_ptr<VirtualClock>    clock_;
  std::unique_ptr<MockClobServer>  clob_srv_;
  std::unique_ptr<MockWssServer>   wss_srv_;
  std::unique_ptr<MockChainRpc>    chain_rpc_;
  std::unique_ptr<SystemUnderTest> sut_;
};
```

### 2.3 `R12SimFixture` — R-12 4 场景专用

```cpp
class R12SimFixture : public SimHarnessFixture {
 protected:
  void inject_rest_delay(std::chrono::milliseconds d) { clob_srv_->set_response_delay(d); } // S-1
  std::vector<int64_t>& wss_tick_latencies_ns()       { return sut_->wss_tick_latencies_ns(); }
  size_t mock_clob_request_count(std::string_view p) const { return clob_srv_->recv_count(p); } // S-2/S-3
  static int64_t p99_ns(std::vector<int64_t> xs);
};
```

---

## 3. Mock harness 接口 (3 server, 真 socket)

```cpp
// tests/helpers/mock_clob_server.h  (cpp-httplib)
class MockClobServer {
 public:
  MockClobServer(uint16_t port); void start(); void stop(); std::string url() const;
  void load_fixture(std::string_view path, std::string_view file);
  void set_response_delay(std::chrono::milliseconds d);       // R-12 S-1
  size_t recv_count(std::string_view path) const;             // R-12 S-2/S-3
  void inject_status(std::string_view path, int code, int n); // 429 / 5xx
};
// tests/helpers/mock_wss_server.h  (websocketpp)
class MockWssServer { void publish(...); void disconnect_all(); void slow_down(...); void corrupt_next_frame(size_t n); };
// tests/helpers/mock_chain_rpc.h  (JSON-RPC)
class MockChainRpc  { void set_nonce(...); void inject_nonce_conflict(...); void set_gas_gwei(uint64_t); void simulate_reorg(int blocks); };
```

---

## 4. ReplayDriver header

```cpp
// tests/helpers/replay_driver.h
namespace stcpp::test {
enum class ReplaySpeed : uint8_t { OneX, TenX, HundredX, Inf };
struct ReplayConfig {
  ReplaySpeed speed = ReplaySpeed::Inf;
  bool strict_pit = true;         // R-20 4 ts 不等式断言
  bool virtual_clock = true;      // 替换 SteadyClock
};
class ReplayDriver {
 public:
  explicit ReplayDriver(ReplayConfig);
  Status load(std::span<const FixturePath>);          // 多源合流
  Status load_wal(const WalReaderAdapter&);           // 老王 WAL
  Status load_empty();                                 // smoke
  void   inject_synthetic(SyntheticEvent);
  void   set_speed(ReplaySpeed);
  Status seek_to(int64_t monotonic_ns);
  Status run(SystemUnderTest&);
  Status run_until(int64_t monotonic_ns, SystemUnderTest&);
  void   attach_assertion(std::unique_ptr<ReplayAssertion>);
  void   attach_fault_provider(std::unique_ptr<FaultProvider>);
  const VirtualClock& virtual_clock() const;
  int64_t start_ns() const;
};
class ReplayAssertion {
 public:
  virtual Status on_decision(const risk::RiskDecision&)             = 0;
  virtual Status on_fill(const exec::Fill&)                          = 0;
  virtual Status on_state_change(risk::RiskState, risk::RiskState)   = 0;
  virtual Status finalize()                                          = 0;
  virtual ~ReplayAssertion() = default;
};
}  // namespace stcpp::test
```

---

## 5. ChaosFaultProvider header (13 类 + R-12 4 场景)

```cpp
// tests/helpers/fault_provider.h
class FaultProvider {
 public:
  virtual void inject_at(int64_t monotonic_ns) = 0;       // ReplayDriver tick 时回调
  virtual void clear() = 0;
  virtual std::string_view name() const = 0;
  virtual ~FaultProvider() = default;
};

// v0.1 §3.1 13 类 (全继承 FaultProvider, 各自 inject_at 实现):
//   NetworkJitter / WssDisconnect / RateLimit / Http5xx / ClockSkew / TscDrift /
//   NonceConflict / LedgerSyncStall / SignerCrash / BadConfigReload /
//   GoalserveSchemaDrift / WssLag / PacketLoss
// v0.2 R-12 4 场景专用:
class RestSlowResponseProvider   : public FaultProvider {/* S-1, set_response_delay */};
class SingleFlightStressProvider : public FaultProvider {/* S-2, 1k 并发 caller */};
class WssBurstDisconnectProvider : public FaultProvider {/* S-3, 5 hot market 同断 */};
class VCpu0BurstLoadProvider     : public FaultProvider {/* S-4, 4 conn × 1k msg/s × 60s */};
```

---

## 6. Replay 断言库

```cpp
// golden_log_assertion.h: 流式 diff golden log; 严格 decision/reject_code/approved_size_usdc/state;
//                        容忍 recorded_at_ns drift / audit_id ULID prefix regex
class GoldenLogAssertion    : public ReplayAssertion { /* impl */ };

// single_flight_assertion.h (R-12 S-2): EXPECT srv.recv_count(path) <= expected_max
class SingleFlightAssertion : public ReplayAssertion {
 public: SingleFlightAssertion(MockClobServer&, std::string path, size_t max=1); };

// pit_assertion.h (R-20 4 ts 不等式 + feature_snapshot_id 必填)
class PitAssertion : public ReplayAssertion {
  Status on_decision(const risk::RiskDecision& d) override {
    ASSERT_LE(d.intent.event_ts,       d.intent.data_source_ts);
    ASSERT_LE(d.intent.data_source_ts, d.intent.ingestion_ts);
    ASSERT_LE(d.intent.ingestion_ts,   d.intent.as_of_ts);
    ASSERT_LE(d.intent.as_of_ts,       clock_now_utc_ns());
    ASSERT_FALSE(d.intent.feature_snapshot_id.empty());      // ML-R8 / R-20 §7
    return Status::OK();
  }
};
```

---

## 7. WAL adapter (接老王 WAL framework) — 待会签 @老王

```cpp
// tests/helpers/wal_reader_adapter.h
class WalReaderAdapter {
 public:
  // 同一 driver 同时消费 risk_audit / paper_audit / shadow_audit 三份 (R-11 物理隔离)
  WalReaderAdapter(WalPath risk_audit, WalPath paper_audit, WalPath shadow_audit);

  // 流式 next, 按 ULID 时间戳合流 (跨三份 WAL global order)
  std::optional<WalEvent> next();

  // BLAKE3 hash chain 校验 (老唐 schema v1.2)
  Status verify_chain() const;
};
```

@老王: 你 WAL framework v0.1 reader 接口确定后, 我把 `WalReaderAdapter` 切到正式 decoder. 三份 WAL 物理隔离我帮你抓 R-11 违例 (任一 audit_id 出现在错的文件 → fail).

---

## 8. Chaos playbook YAML schema (R-12 S-1 范例)

```yaml
# tests/chaos/playbooks/r12_s1_rest_slow_wss_unblock.yaml
name: r12_s1_rest_slow_wss_unblock
linked_redline: R-12
fixtures: [polymarket/wss/nba-2026-05-15-celtics-heat-q4.msgpack]
load_profile: { wss_msg_rate: 1000/s, duration: 60s }
injection:
  - { type: RestSlowResponseProvider, at: T+10s, delay_ms: 5000 }
expectations:
  - histogram(sut.wss_tick_latency_ns).p99   <= 50000      # 50us
  - histogram(sut.wss_tick_latency_ns).p99_9 <= 100000
  - counter(sut.wss_in_ring_market_hot.push) >= 45000
  - assert(sut.vcpu0_kernel_time_ratio < 0.2)              # 非阻塞
```
R-12 S-1~S-4 各一份 yaml. chaos runner 解析 yaml → FaultProvider + ReplayAssertion.

---

## 9. R-20 PIT CI 脚本骨架 (W3 落)

```
tools/ci/
├── r20_pit_grep.sh                      # 7 项 grep, v0.2 §4.2
├── check_signal_struct_has_feature_snapshot_id.py
├── check_signer_has_pit_assert.py
├── risk_enum_coverage.py                # 21 enum × 3 层
├── nm_blacklist.sh                      # CMake target 物理隔离校验
└── coverage_diff.py                     # PR comment
```

---

## 10. 第一批必跑 case (Sprint-2 W3)

| ID | 文件 | 期望 |
|---|---|---|
| T-01 | replay/framework_smoke_test.cc                       | empty stream → 0 events |
| T-02 | unit/risk/exceed_per_order_cap_test.cc (v0.1 §11.1)  | EXCEED_PER_ORDER_CAP |
| T-03 | unit/risk/strategy_decayed_test.cc (RM v0.3 §16)     | STRATEGY_DECAYED + DEFERRED |
| T-04 | unit/numerical/slippage_model_test.cc (小肖 7 case)  | 7/7 过 |
| T-05 | sim/r12_sim/s1_rest_slow_wss_unblock_test.cc         | p99 < 50us |
| T-06 | sim/r12_sim/s2_single_flight_test.cc                 | recv_count == 1 |
| T-07 | replay/paper/paper_mode_no_chain_rpc_test.cc (§18.6.1)| 0 chain RPC |
| T-08 | replay/shadow/shadow_wal_isolation_test.cc (§19)     | 三 WAL 不交叉 |

---

**END skeleton v1.** 接口 spec 锁版, W3 落代码 + 联调老王 WAL + 接老练 CI.

— 小宋, Sprint-2 W2
