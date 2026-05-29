# paper runtime 集成设计 v1

owner: 小肖 (numerical-algorithms, A 系统工程部)
last_review: 2026-05-29
cite:
  ADR-015 (vCPU 分工)
  ADR-011 (paper/live binary)
  ADR-012 (WSS 拓扑 1×8)
  ADR-017 (SPSC 5-ring)
  queue_capacities.hpp (65536/8192/4096/8192/65536)
  risk_gateway.hpp v0.6
  orderbook_snapshot_hub.hpp v0.1
  rm_debug_snapshot.hpp (老沈)
  virtual_matcher.hpp (小袁)
  position_ledger.hpp (老王/老蒋)
  sizing_calculator.hpp v0.1 (小袁)
  fair_value_estimator.hpp v0.1 (小肖)
  real_state_provider.hpp (小卢)
  paper.cpp (当前 stub loop)

红线:
  R-11  paper 不污染真账本 — VirtualFill.mode_tag==0 + PaperAudit WAL + paper_prefix 硬校验
  R-12  event loop vCPU0 同步路径 <= 100us; 零阻塞 IO; hub.Publish noexcept
  R-20  4 ts 全链路: event_ts <= data_source_ts <= ingestion_ts <= as_of_ts
        data_source_ts 来自 WssEvent payload timestamp (ms×1e6), 禁本地 now() 替代

---

## 1. 目标

让 paper engine 主循环从当前 stub 打印一行就退出，升级为:

- M1 (replay 路径): 用 replay/synthetic 数据驱动全链路 (WSS → signal → RM → 撮合 → 账本), 观测看板读到真实流动数据而非 demo 数据。
- M2 (真实 WSS): Frankfurt 机房 Boost.Beast WSS 接 Polymarket CLOB，M1 逻辑不变，数据源切换。
- M3 (72h 稳定): 连续 72h 无崩溃、R-11/R-12/R-20 零违规、看板持续刷新。

---

## 2. 架构全景

```
外部数据
  Polymarket CLOB WSS ─────────────────────────────────
  (Frankfurt M2+, M1 用 replay)                       |
                                                        v
  vCPU0 — WSS event loop (boost.beast io_context, R-12)
  ┌──────────────────────────────────────────────────────────────────────┐
  │  PolymarketCLOBSubscriber::OnMarketFrame(payload, recv_ts_ns)       │
  │    → OrderBookAdapter::OnEvent(ev, token_id, recv_ts_ns,            │
  │                                out_snap, out_book_row)              │
  │    → hub_.Publish(token_id, MakeFeatures(out_snap, ...))            │  ← 观测读端可见
  │    → MarketDataBus.try_push(WssEvent)          [SPSC 65536]        │
  └────────────────────────────────┬────────────────────────────────────┘
                                   | SPSC try_push (非阻塞, 满则 drop)
  vCPU1 — 信号引擎
  ┌──────────────────────────────────────────────────────────────────────┐
  │  MarketDataBus.pop(WssEvent)                                        │
  │    → FairValueEstimator::estimate(game_row, &book_row)  [小肖]     │
  │    → α 信号 ISignalEngine::tick(ctx)             [老彭]            │
  │    → SizingCalculator::compute(cfg, sizing_input) [小袁]           │
  │    → 组装 OrderIntent (4 ts 透传, timestamp_ms 填当前毫秒)          │
  │    → SignalQueue.try_push(OrderIntent)         [SPSC 8192]         │
  └────────────────────────────────┬────────────────────────────────────┘
                                   |
  vCPU2 — RiskGateway
  ┌──────────────────────────────────────────────────────────────────────┐
  │  SignalQueue.pop(OrderIntent)                                        │
  │    → RiskGateway::evaluate(intent) → RiskDecision                  │
  │        内部: rm_debug_snapshot.push_reject(row)  ← 观测读端可见    │
  │    → APPROVED → RiskQueue.try_push(intent, decision) [SPSC 4096]  │
  │    → REJECTED → AuditEmitter::emit (WALQueue → paper_audit.wal)    │
  └────────────────────────────────┬────────────────────────────────────┘
                                   |
  vCPU3 — PaperSigner / VirtualMatcher / PositionLedger
  ┌──────────────────────────────────────────────────────────────────────┐
  │  RiskQueue.pop(intent, decision)                                     │
  │    → PaperSigner::Sign(req)                                         │
  │    → VirtualMatcher::MatchWithBook(order_with_book)  [小袁 Mode A] │
  │        fill.mode_tag == 0 (paper, R-11 硬填)                        │
  │    → PositionLedger::apply_fill(fill)      ← 观测: positions/pnl  │
  │    → WALQueue.try_push → paper_audit.wal (R-11 PaperAudit)        │
  │    → ledger_snapshot_.publish(snapshot)    ← 观测读端可见          │
  └──────────────────────────────────────────────────────────────────────┘

观测线程 (debug_api 独立线程, R-12 只读)
  hub_.Read(token_id)            → RealStateProvider::book()
  rm_debug_snapshot.snapshot()   → RealStateProvider::risk_rejects()
  ledger_snapshot_.read()        → RealStateProvider::positions() / pnl()
  quote_snapshot_.read()         → RealStateProvider::quote_params()
```

---

## 3. 主循环 wiring

### 3.1 vCPU0 — WSS event loop (R-12 守卫线)

vCPU0 同步路径预算 <= 100us, 分解:

| 步骤 | 函数 | 预算 |
|------|------|------|
| JSON parse | simdjson (W10+, 当前 stub parser) | ~5us |
| OrderBookAdapter::OnEvent | 单档合成 + 4-ts 校验 | ~1us |
| hub_.Publish(token_id, features) | atomic release swap | ~300ns |
| MarketDataBus.try_push(ev) | rigtorp SPSC | ~50ns |
| 合计 | | ~7us << 100us |

关键接口:

```cpp
// vCPU0 OnMarketFrame 内 (新增 vs 当前 stub)
void OnMarketFrame(std::string_view payload, std::int64_t recv_ts_ns) {
    WssEvent ev;
    if (!ParseBook(payload, recv_ts_ns, ev) &&
        !ParsePriceChange(payload, recv_ts_ns, ev)) {
        return;  // drop; metric++
    }
    OrderBookSnapshot out_snap;
    FeatureStoreBookRow out_book_row;
    std::string_view token_id = ev.asset_id_sv();  // 从 ev 取

    if (adapter_.OnEvent(ev, token_id, recv_ts_ns, out_snap, out_book_row)) {
        // R-20: hub Publish — 4 ts 已在 OrderBookFeatures 内
        OrderBookFeatures feat = MakeOrderBookFeatures(out_snap, out_book_row, ev);
        hub_.Publish(token_id, feat);               // 观测层 Read() 可见

        // SPSC non-blocking push (R-12: 满则 drop + counter)
        if (!market_data_bus_.try_push(ev)) {
            ++mdb_drop_count_;
        }
    }
}
```

MakeOrderBookFeatures 需完成 OrderBookSnapshot + FeatureStoreBookRow → OrderBookFeatures 的字段映射, 遵循 orderbook_snapshot_hub.hpp 末尾映射说明。as_of_ts_ns 在此置 ingestion_ts_ns (vCPU0 publish 时刻; 观测消费方读时会覆盖为 now_ns())。

### 3.2 vCPU1 — 信号 + Sizing

MarketDataBus pop → 查 game_row (Goalserve 缓存, M1 用合成数据) → FairValueEstimator::estimate → α 信号 → SizingCalculator::compute → 组装 OrderIntent。

关键注意事项:

- as_of_ts_ns 在 vCPU1 组装 OrderIntent 时填写 (= 信号评估时刻), 不早于 ingestion_ts_ns。
- timestamp_ms (V2 EIP-712) = vCPU1 当前毫秒时间, 非 RM 内生成 (spec-10 enforce)。
- SizingCalculator 输出 suggested_notional → 填 OrderIntent.size_pUSD_micro。
- edge_ci_lower 由 FairValueEstimator + CI 区间计算 (small-sample bootstrap 或 normal approx)。

CI 估计口径 (本文件责任范围):
```
edge_ci_lower = (p_fair - p_ask) - z_alpha * sigma_estimate
sigma_estimate = sqrt(p_fair*(1-p_fair)/n_obs + measurement_uncertainty)
z_alpha = 1.645  (90% CI 单侧)
n_obs 从 game_row.sample_count 取; 初期 M1 用保守固定值 (n=30)
```

SignalQueue push 后, vCPU1 还需更新 quote_snapshot_ 双缓冲 (供观测 quote_params):
```cpp
// vCPU1 push 前
QuoteSnapshotData qs;
qs.fair_value       = fv_result.p_yes();
qs.market_mid       = feat.microprice;
qs.edge_bps         = sizing_out.snapshot.edge_bps;
qs.kelly_fraction   = sizing_out.kelly_fractional;
qs.suggested_notional = sizing_out.suggested_notional;
qs.as_of_ts_ns      = intent.as_of_ts_ns;
quote_hub_.Publish(condition_id, qs);  // 双缓冲, 观测只读
```

### 3.3 vCPU2 — RiskGateway

SignalQueue pop → evaluate → 若 REJECTED 则 rm_debug_snapshot.push_reject(row); 若 APPROVED 则 RiskQueue.try_push。

RmDebugSnapshot 连接:
```cpp
// main 启动期
RmDebugSnapshot rm_snap;
attach_rm_debug_snapshot(&rm_snap);  // 全局 atomic ptr 注入

// RealStateProvider 持 const RmDebugSnapshot* 读
RealStateProvider real_provider(hub_, &rm_snap, token_map, ExecMode::Paper);
```

RiskGateway.set_token_book_freshness_ms 需在 vCPU1 或 vCPU2 定期刷新 (来自 hub_.Read 的 ingestion_ts_ns 与当前时间差)。这是 R8.4 book staleness 检查的入参。

### 3.4 vCPU3 — Signer / Matcher / Ledger

RiskQueue pop → PaperSigner::Sign → 若 Sign OK → VirtualMatcher::MatchWithBook (传入 hub_.Read(token_id) 的最新 book snapshot 做 Mode A 撮合) → PositionLedger::apply_fill。

R-11 守护点:
- VirtualFill.mode_tag 必须为 0 (paper); PositionLedger.apply_fill 进入前 assert mode_tag == 0。
- WAL 路径前缀必须是 paper 前缀 ("/var/lib/stcpp/paper/position")。
- WAL kind = WalKind::PaperAudit (严禁 RiskAudit / Position 混用)。

apply_fill 完成后更新 ledger_snapshot_ 双缓冲:
```cpp
ApplyResult res = ledger_.apply_fill(fill);
if (res.status == ApplyStatus::Ok) {
    LedgerSnapshotData snap;
    snap.positions = ledger_.query_all_positions();  // 全仓快照
    snap.cum_pnl   = ledger_.circuit_breaker_state().bankroll_total;
    snap.as_of_ts_ns = fill.as_of_ts_ns;
    ledger_hub_.Publish(snap);  // 双缓冲; 观测 positions/pnl 端 Read
}
```

---

## 4. 快照喂给观测层的时机

下表按信号流方向列出每个观测 endpoint 的数据时机:

| 观测 endpoint | 快照发布时机 | 发布方 | 读端 (RealStateProvider) |
|---|---|---|---|
| /api/v1/book/{token_id} | vCPU0 adapter_.OnEvent 之后立即 hub_.Publish | vCPU0 | hub_.Read(token_id) → to_book_snapshot |
| /api/v1/risk/rejects | vCPU2 evaluate() 返回 REJECTED 后 push_reject | vCPU2 | rm_snap.snapshot() |
| /api/v1/quote/{condition_id} | vCPU1 SizingCalculator 完成后 quote_hub_.Publish | vCPU1 | quote_hub_.Read(condition_id) |
| /api/v1/positions | vCPU3 apply_fill Ok 后 ledger_hub_.Publish | vCPU3 | ledger_hub_.Read() |
| /api/v1/pnl/* | 同 positions (来自同一 LedgerSnapshot) | vCPU3 | ledger_hub_.Read() |
| /metrics | 各 vCPU 原子计数器; 观测线程直接 load(relaxed) | 各 vCPU | 直接原子读 |

R-12 保证: 所有观测读端均为原子 acquire-load 或 double-buffer value copy, 无持锁 > 100us, 无热路径反向依赖。

### 4.1 LedgerSnapshotHub (新增组件)

RealStateProvider.positions/pnl 当前委托 DemoStateProvider。为接真数据需新增 LedgerSnapshotHub:

```cpp
// include/stcpp/infra/wal/ledger_snapshot_hub.hpp (新增, 小石实施)
// 设计与 OrderBookSnapshotHub 完全对称:
//   - 单 writer (vCPU3) double-buffer atomic swap
//   - 多 reader (debug_api) acquire-load + value copy
//   - LedgerSnapshotData: positions vector + cum_pnl + as_of_ts_ns
struct LedgerSnapshotData {
    std::vector<HoldingView> positions;  // debug_api::HoldingView 直接用
    double cum_net_pnl{0.0};
    double realized_pnl{0.0};
    std::int64_t as_of_ts_ns{0};
    bool valid{false};
};
class LedgerSnapshotHub { /* 与 OrderBookSnapshotHub 完全对称 */ };
```

### 4.2 QuoteSnapshotHub (新增组件)

```cpp
// include/stcpp/pricing/quote_snapshot_hub.hpp (新增, 小肖/小石实施)
// 单 writer (vCPU1) double-buffer; 读端 quote_params(condition_id)
struct QuoteSnapshotData {
    double fair_value{0.0};
    double market_mid{0.0};
    double edge_bps{0.0};
    double kelly_fraction{0.0};
    double suggested_notional{0.0};
    double signal_strength{0.0};
    std::int64_t as_of_ts_ns{0};
    bool valid{false};
};
class QuoteSnapshotHub { /* 与 OrderBookSnapshotHub 完全对称 */ };
```

---

## 5. 新 paper.cpp 主函数骨架

当前 paper.cpp RunStubLoop() 是一次性打印后退出。集成后主循环结构:

```cpp
// src/stcpp/bin/paper.cpp — 集成后骨架 (不是完整实现)
int main() {
    // [1] 防多开 (现有, 不变)
    SingleInstanceLock s_lock{ExecutionMode::Paper};

    // [2] build-time mode 校验 (现有, 不变)
    ExecutionContext::Init(ExecutionMode::Paper);

    // [3] 共享对象构造 (main thread, 启动期)
    OrderBookSnapshotHub        hub{kDefaultMaxTokens};
    RmDebugSnapshot             rm_snap;
    attach_rm_debug_snapshot(&rm_snap);

    QuoteSnapshotHub            quote_hub;
    LedgerSnapshotHub           ledger_hub;

    RiskConfig                  rm_cfg;  // 来自 config 文件
    auto audit_emitter = std::make_shared<WalAuditEmitter>(
        WalConfig{ .kind=WalKind::PaperAudit,
                   .path_prefix="/var/lib/stcpp/paper/audit",
                   .fsync_mode=FsyncMode::GroupCommit });
    RiskGateway                 rm{rm_cfg, audit_emitter};
    rm.set_state(RmState::RUNNING);  // 从 SAFE_MODE 转 RUNNING

    BaselineFairValueModel      fv_model;
    FairValueEstimator          fv_estimator{fv_model};
    VirtualMatcher              matcher{0xBEEFCAFEULL};

    VirtualNonceProvider        nonce{0};
    VirtualGasEstimator         gas;
    VirtualConfirmWatcher       confirm{0xC0FFEED00DULL};
    PaperSigner                 psigner{&nonce, &gas, &confirm};

    PositionLedger              ledger{"/var/lib/stcpp/paper/position",
                                       /*init_bankroll=*/100'000LL * 1'000'000LL};
    ledger.restore_from_wal("/var/lib/stcpp/paper/");  // 崩溃恢复 (老韩 #3 P0)

    MarketTokenMap              token_map;   // 从 gamma REST 预加载 condition_id → tokens
    RealStateProvider           real_provider{hub, &rm_snap, token_map, ExecMode::Paper};
    HttpServer                  obs_server{8080, &real_provider};
    obs_server.start();  // 独立线程 (R-12: 观测不进 event loop)

    // [4] 5 SPSC queue
    SpscQueue<WssEvent,      MARKET_DATA_BUS_CAPACITY> market_data_bus;
    SpscQueue<OrderIntent,   SIGNAL_QUEUE_CAPACITY>    signal_q;
    SpscQueue<OrderIntent,   RISK_QUEUE_CAPACITY>      risk_q;
    MpmcQueue<VirtualFill,   FILL_QUEUE_CAPACITY>      fill_q;

    // [5] 启动 vCPU1/2/3 worker 线程 (非 event loop, std::jthread)
    std::jthread vcpu1{signal_worker,  std::ref(market_data_bus), std::ref(signal_q),
                        std::ref(fv_estimator), std::ref(quote_hub), std::ref(rm_cfg)};
    std::jthread vcpu2{risk_worker,    std::ref(signal_q), std::ref(risk_q),
                        std::ref(rm), std::ref(rm_snap)};
    std::jthread vcpu3{fill_worker,    std::ref(risk_q), std::ref(fill_q),
                        std::ref(psigner), std::ref(matcher), std::ref(hub),
                        std::ref(ledger), std::ref(ledger_hub)};

    // [6] vCPU0 — Boost.Beast io_context + PolymarketCLOBSubscriber (M2+)
    //     M1: ReplayDriver 替代 (见 §6)
    RunReplayDriver(market_data_bus, hub);   // M1 路径; M2 换 CLOBSubscriber

    obs_server.stop();
    detach_rm_debug_snapshot();
    return 0;
}
```

vCPU1 (signal_worker), vCPU2 (risk_worker), vCPU3 (fill_worker) 各自持续循环 pop → 处理 → push, 直到 stop_token 被触发。

---

## 6. 依赖与排期

### 6.1 依赖组件状态

| 组件 | Owner | 状态 | M1 替代方案 |
|---|---|---|---|
| PolymarketCLOBSubscriber (真实 WSS) | 小冯 / Frankfurt (老吴) | 待 Frankfurt M2+ | ReplayDriver (§6.2) |
| Boost.Beast WSS transport | 老周 W10+ | 待 W10+ | ReplayDriver |
| OrderBookAdapter v0.1 | 小冯 | 已落 | 直接用 |
| OrderBookSnapshotHub | 小冯 | 已落 | 直接用 |
| RmDebugSnapshot (attach 钩子) | 老沈 | 已落 | 直接用 |
| RiskGateway v0.6 | 老韩 | 已落 | 直接用 |
| VirtualMatcher (MatchWithBook) | 小袁 | 已落 | 直接用 |
| SizingCalculator v0.1 | 小袁 | 已落 | 直接用 |
| FairValueEstimator (baseline) | 小肖 | 已落 | 直接用 |
| PositionLedger + WAL | 老王/老蒋 | 已落 | 直接用 |
| PaperSigner | 小蒋/老孙 | 已落 | 直接用 |
| SPSC 5-ring (rigtorp) | 小石 | 已落 | 直接用 |
| LedgerSnapshotHub | 小石 (派单) | 未落 | M1 阻塞点 |
| QuoteSnapshotHub | 小肖/小石 | 未落 | M1 阻塞点 |
| RealStateProvider.positions/pnl 接真 | 小卢 | 待 LedgerSnapshotHub | DemoStateProvider 降级 |
| alpha 信号 (老彭) | 老彭 | ABI 待确认 | stub SignalOutput (edge=50bps) |
| Frankfurt 机房 WSS | 老吴 | M2 | ReplayDriver |

### 6.2 ReplayDriver — M1 不等 Frankfurt 先跑通全链路

M1 用 ReplayDriver 替代真实 WSS, 直接向 MarketDataBus 喂合成 WssEvent + 同步 hub_.Publish, 跳过 Boost.Beast transport 依赖:

```cpp
// src/stcpp/bin/replay_driver.hpp (新增, 小肖实施)
//
// 职责: 按固定频率生成 synthetic WssEvent, 模拟真实 book snapshot + price_change 序列。
//   - token_id、bid/ask 等参数从 config 文件读
//   - 4 ts 按规范填写: event_ts < data_source_ts < ingestion_ts <= as_of_ts
//   - sequence_no 单调递增 (模拟 WSS 序列号)
//   - 可注入历史 Parquet 文件做回放 (对接 小余/小董 feature store)
//
// 使用: M1 paper.cpp 的 vCPU0 位置调用 RunReplayDriver();
//       M2 替换为 PolymarketCLOBSubscriber (接口不变: 同写 MarketDataBus + hub_)

void RunReplayDriver(
    SpscQueue<WssEvent, MARKET_DATA_BUS_CAPACITY>& bus,
    OrderBookSnapshotHub& hub,
    std::stop_token stop);
```

ReplayDriver 可以在 Frankfurt 接通前先让整条链路跑起来, 确保信号→RM→撮合→账本→观测全路径可观测。

### 6.3 关键路径 (M1 解锁顺序)

```
Day 1: LedgerSnapshotHub 接口设计 (小肖拿草稿 → 小石实施)
        QuoteSnapshotHub 接口设计 (小肖拿草稿 → 小石实施)

Day 2: ReplayDriver 实现 (小肖 or 小田)
        paper.cpp RunStubLoop → 多线程主循环重构

Day 3: vCPU1 signal_worker 接 FairValueEstimator + SizingCalculator
        (先用 stub SignalOutput, 等老彭 alpha ABI 确认后再换)

Day 4: vCPU3 fill_worker 接 LedgerSnapshotHub; RealStateProvider.positions 接真
        观测看板 /api/v1/positions 从 demo 变真实流动

Day 5: 全链路端到端: replay 喂数据 → 看板见真实流动 (M1 里程碑 Done)

M2 (等 Frankfurt + 老周 Boost.Beast transport):
  vCPU0 切 PolymarketCLOBSubscriber; ReplayDriver 降级为 fallback/测试路径

M3 (72h 稳定):
  连续 72h 跑; P0 监控: rm_drop/mdb_drop/walq_overflow 全 0; R-11/R-12/R-20 无违规
```

---

## 7. R-11 / R-12 / R-20 守法细则

### R-11 (paper 不污染真账本)

- VirtualFill.mode_tag == 0 硬填; PositionLedger.apply_fill 入口 assert(fill.mode_tag == 0)。
- WAL path_prefix: 所有 paper WAL 路径以 "/var/lib/stcpp/paper/" 开头; WalWriter::Open 内 PathRootOf 校验不命中 → std::abort (禁绕过)。
- WAL kind: PaperAudit (WalKind::PaperAudit); 禁 RiskAudit (真账本 kind) 出现在 paper 路径。
- 观测层 debug_api 零写权限: RealStateProvider 只持 const 句柄; 无法调用任何写接口。

### R-12 (WSS event loop 无阻塞)

- vCPU0 同步路径: adapter_.OnEvent + hub_.Publish + SPSC try_push 全部 noexcept, 无 malloc (热路径 token 已预分配), 预算 ~7us << 100us。
- MarketDataBus 满 → try_push 返 false → 原子 mdb_drop_count.fetch_add(1)。不等待, 不阻塞。
- 观测 HttpServer 独立线程, 不进 vCPU0/1/2/3 event loop。

### R-20 (4 时间戳链)

全链路 4 ts 传递路径:

```
WSS frame.timestamp (ms) × 1e6
  → data_source_ts_ns (禁 now() 替代, PolymarketCLOBSubscriber R-33)
  → OrderBookAdapter::OnEvent (记录到 TokenState.last_data_source_ts_ns)
  → OrderBookFeatures.data_source_ts_ns (hub_.Publish)
  → WssEvent.ts.data_source_ts_ns (MarketDataBus)
  → OrderIntent.data_source_ts_ns (vCPU1 组装, 从 WssEvent 透传)
  → RiskDecision (evaluate 透传)
  → VirtualFill.data_source_ts_ns (VirtualMatcher 透传)
  → PositionRecord.data_source_ts_ns (apply_fill 透传)
  → WAL frame header.data_source_ts_ns (WalWriter Append)
```

as_of_ts_ns 各阶段填写规则:
- hub_.Publish 时: = ingestion_ts_ns (vCPU0 publish 时刻)
- 观测 to_book_snapshot 读时: = now_ns() (R-20 allowed, 消费方读取时刻)
- vCPU1 组装 OrderIntent 时: = 信号评估时刻 now_ns() (>= ingestion_ts_ns)
- apply_fill 时: = fill.as_of_ts_ns (透传, 不重置)

ts_chain_ok 校验点:
- OrderBookFeatures::ts_chain_ok() 在 hub_.Publish 前 assert (debug build)
- pit::AssertChain 在 WalWriter::Append 入口 (R-20 硬校验)
- RM evaluate check_invalid_intent_ step 2 (R-20 PIT 4 ts 校验)

---

## 8. 新增接口摘要 (ABI 锁定提议)

以下接口是 M1 集成所需新增或扩展, 需各 owner 确认:

### 8.1 MakeOrderBookFeatures (小冯扩展)

```cpp
// vCPU0 hot path; 在 orderbook_adapter.hpp 或独立 util 头 (小肖建议单独 util)
// 将 adapter 产出映射到 hub 要求的 POD
[[nodiscard]] OrderBookFeatures MakeOrderBookFeatures(
    const OrderBookSnapshot&    snap,
    const FeatureStoreBookRow&  book_row,
    const WssEvent&             ev) noexcept;
// 映射规则见 orderbook_snapshot_hub.hpp 末尾"映射说明"注释
// as_of_ts_ns = ingestion_ts_ns (hub publish 时刻; 观测读时覆盖为 now_ns())
```

### 8.2 LedgerSnapshotHub (小石新增)

接口与 OrderBookSnapshotHub 完全对称。Publish 由 vCPU3 在 apply_fill Ok 后调用, Read 由 RealStateProvider 在 positions/pnl endpoint 调用。详见 §4.1。

### 8.3 QuoteSnapshotHub (小石新增, 小肖 spec)

接口与 OrderBookSnapshotHub 完全对称。Publish 由 vCPU1 在 SizingCalculator 完成后调用。详见 §4.2。

### 8.4 ReplayDriver::RunReplayDriver (小肖/小田新增)

详见 §6.2。

### 8.5 RealStateProvider 扩展 (小卢)

在 LedgerSnapshotHub/QuoteSnapshotHub 落地后, 修改 RealStateProvider 构造入参:
```cpp
explicit RealStateProvider(
    const OrderBookSnapshotHub& hub,
    const RmDebugSnapshot*      snap,
    const LedgerSnapshotHub*    ledger_hub,   // 新增; nullptr → 回落 Demo
    const QuoteSnapshotHub*     quote_hub,    // 新增; nullptr → 回落 Demo
    MarketTokenMap              tokens,
    ExecMode                    m = ExecMode::Paper);
```
向后兼容: 两个新参数可为 nullptr → 对应 endpoint 回落 DemoStateProvider (优雅降级)。

---

## 9. 里程碑

### M1: replay 喂数据, 看板见真实流动 (目标: Day 5)

验收标准:
- paper binary 不退出, 持续循环。
- /api/v1/book/{token_id} 返回非 demo-fallback 的真实 hub 数据 (bid/ask 在 ReplayDriver 设定范围内)。
- /api/v1/risk/rejects 返回真实拒单 ring 数据 (RM 从 SAFE_MODE 转 RUNNING 后有数据产出)。
- /api/v1/positions, /api/v1/quote 返回 LedgerSnapshotHub/QuoteSnapshotHub 数据 (非 Demo)。
- R-11: WAL 落盘路径前缀全部 paper; VirtualFill.mode_tag == 0。
- R-20: hub.Publish 的 ts_chain_ok() 全部通过; WAL frame ts 单调链无违规。

### M2: 真实 WSS 接入 (依赖 Frankfurt + 老周 Boost.Beast, 预估 W10+)

验收标准:
- vCPU0 切换为 PolymarketCLOBSubscriber; ReplayDriver 降级为测试路径。
- /api/v1/book 返回真实 Polymarket CLOB 价格 (bid/ask 与 Polymarket 网页一致, 延迟 < 500ms)。
- 数据源标记从 "replay" 变 "polymarket"。

### M3: 72h 稳定 (依赖 M2)

验收标准:
- 连续运行 72h 无崩溃、无 OOM、无 WAL 失败。
- 监控告警全无: mdb_drop_total=0, walq_overflow_total=0, rm_reject_stale_total 合理 (< 1%)。
- R-11/R-12/R-20 全量 CI 测试通过。
- PositionLedger crash-recovery: 杀进程重启后 positions 从 WAL replay 正确恢复 (老韩 #3 P0)。

---

## 10. 数值稳定性 (小肖责任范围)

### 10.1 FairValueEstimator 在主循环的稳定性保证

- safe_sigmoid 防 exp overflow: |x| > 500 直接返极值, 无 NaN 传播。
- normalize2 Kahan sum: catastrophic cancellation 防护 (p_yes + p_no 量级接近时有效)。
- clamp_prob: 任何 p ∈ (kProbEps=1e-6, kProbMax=1-1e-6); 防 Kelly 分母 0。
- fail-closed: game_row 含 NaN → FairValueResult.valid=false → SizingCalculator 不进 Kelly → suggested_notional=0 → OrderIntent 不组装。

### 10.2 SizingCalculator 在主循环的稳定性保证

- is_valid_input_: fair_value/price/edge_ci_lower 任一非 finite 或越界 → valid=false 全 0 输出。
- net_ci_edge = edge_ci_lower - kSportsTakerFeeRate × p × (1-p): 与 RM check_signal_ 完全同源, 无漂移。
- 5-cap 链取最小值, CappedBy 记录原因: 无 NaN 传播风险 (cap 均为正整数 USDC 上限)。

### 10.3 CI 区间计算 (小肖实施)

edge_ci_lower 是 RM 和 Sizing 共同的 gating 门。在初始 M1 阶段用保守估算:

```
p_fair       来自 FairValueEstimator (baseline sigmoid 先验)
p_ask        = hub_.Read(token_id).best_ask()
raw_edge     = p_fair - p_ask
sigma_approx = sqrt(p_fair * (1 - p_fair) / n_effective)
              n_effective = 30 (M1 保守固定; M2+ 接 game_row.sample_count)
z_90         = 1.645
edge_ci_lower = raw_edge - z_90 * sigma_approx

// 数值边界保护
if (!std::isfinite(edge_ci_lower)) edge_ci_lower = -1.0;  // fail-closed
edge_ci_lower = std::clamp(edge_ci_lower, -1.0, 1.0);
```

---

## 11. 待确认 open items

| # | 问题 | 责任方 | 优先级 |
|---|---|---|---|
| OI-1 | 老彭 alpha ISignalEngine::tick 的 SignalContext.market_id 是 condition_id 还是 token_id? (RM 用 condition_id, 信号层用 market_id) | 老彭/老韩 | P0 (vCPU1 组装 OrderIntent 前) |
| OI-2 | LedgerSnapshotHub: positions vector 包含全部 market 还是 top-N? (全量在 vCPU3 热路径有 copy 开销) | 小石/小肖 | P1 |
| OI-3 | vCPU1 signal_worker 是否需要 Goalserve game_row 缓存? M1 若无 Goalserve 接入, FairValueEstimator 退化为纯 microprice 先验 (score_diff=0, time_frac=0) | 小肖/小余 | P1 (M1 可接受退化) |
| OI-4 | ReplayDriver 合成数据的 token_id 与 MarketTokenMap 的对应关系需要预约定 (debug_server --real 的 token_map 构建) | 小肖/小冯 | P1 |
| OI-5 | WAL path 在 macOS dev 环境是否 fallback 到 ./paper_wal/ (生产是 /var/lib/stcpp/paper/) | 老王/老吴 | P2 |

---

## 12. 不涉及范围 (本文件边界)

- Goalserve inplay 接入 (小余/小董): OI-3 等待
- 真实 PositionRecord WAL schema v1.4 (老唐): 接口已收口在 PositionLedger
- Boost.Beast WSS transport (老周): M2 依赖, M1 绕过
- Frankfurt 机房部署 (老吴): M2 依赖
- 老彭 alpha 信号实现: vCPU1 用 stub SignalOutput 先跑通; OI-1 确认后换真实
- 前端 dashboard UI: 小宫/小尤负责; 本设计只保证 API 喂真实数据
