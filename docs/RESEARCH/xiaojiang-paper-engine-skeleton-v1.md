# Paper Engine Skeleton v1 — Sprint-2 W1 落地

- Owner: 小蒋 (quant-backtest)
- Date: 2026-05-28
- Wave: Sprint-2 W1
- Parent: `xiaojiang-paper-trading-engine-v0.2-cpp.md`
- Hard refs: ADR `defer-onchain` (上链 deferred → 三 mock 必做) / ADR `signoff-paper-trade` (R-7/R-11) / `xiaoyuan-speech` R-14 (Mode A++) / `laowang-wal-framework-v0.1` §9 `WalWriter` / `xiaoxiao-kelly-slippage-model-v1` §5.2 `SlippageEstimate` / `xiaodong-stats-validation-framework-v1` §5.2 G1-G7
- 状态: skeleton v1 RFC, 等老王 + 老韩 + 小肖 + 小袁 + 小董 W2 会签

本文不是 v0.2 替代, 是 **W1 立桩**: 代码骨架 + mock 接口签名 + W6 gate 重写计划. 不在范围: VirtualMatcher 内部撮合 (v0.2 §4) / report (v0.2 §8) / 时间表 (v0.2 §9).

---

## 1. 目录结构 (CMake target 物理隔离, PR-7)

```
src/exec/signer/
├── signer_main.cc                       # paper / live 共享入口, mode 在 link 期决
├── common/{sign_request.h, execution_mode.h, signer_iface.h}
├── live/                                # ONLY linked into stcpp_signer_live (上链 deferred, 接口先立)
│   ├── live_signer.{h,cc}               # 老孙 v5 真 EIP-712 (Sprint-N+)
│   ├── real_nonce_provider.cc           # 老叶 nonce_mgr (deferred)
│   ├── real_gas_estimator.cc            # 老叶 RPC eth_gasPrice (deferred)
│   └── real_confirm_watcher.cc          # Polygon block subscribe (deferred)
├── paper/                               # ONLY linked into stcpp_signer_paper
│   ├── paper_signer.{h,cc}              # 共享老孙 SecureBuffer (PR-9)
│   ├── virtual_matcher.{h,cc}           # 小肖 SlippageModel + 小袁 microstructure
│   ├── virtual_nonce_provider.{h,cc}    # mock #1
│   ├── virtual_gas_estimator.{h,cc}     # mock #2
│   ├── virtual_confirm_watcher.{h,cc}   # mock #3
│   └── fill_rate_sampler.{h,cc}         # Mode A++ Bernoulli (R-14)
└── backtest/backtest_signer.{h,cc}      # in-process into stcpp_trader, deterministic seed

src/exec/ledger/
├── ledger_provider.h                    # 抽象 (v0.2 §5.2)
├── {live,paper,shadow,backtest}_ledger.cc   # paper 写 paper_audit.wal; shadow 写 shadow_audit.wal
```

**CMake gate**: 三 target — `stcpp_trader` (主进程, link `stcpp_backtest_signer` 仅 backtest 用), `stcpp_signer_live` (link real_signer + securebuffer), `stcpp_signer_paper` (link paper_signer + securebuffer + virtual_matcher + slippage). shadow 复用 paper binary + `--mode=shadow` flag. CI `nm` 双向 grep 隔离 (v0.2 §2.4 已 spec, PR-7).

---

## 2. ExecutionMode 切换契约 (R-7)

```cpp
// src/exec/signer/common/execution_mode.h
namespace stcpp::exec {

enum class ExecutionMode : uint8_t { Live, Paper, Shadow, Backtest };

// 启动期一次性决定, 全局 const, 同进程禁动态切换 (R-7).
class ExecutionContext {
public:
  static void Init(ExecutionMode m);                  // main() 第一行调, 调过两次 = abort
  static ExecutionMode mode() noexcept;               // 全局 read-only
private:
  inline static std::atomic<ExecutionMode> mode_;
  inline static std::atomic<bool>          inited_{false};
};

}  // namespace stcpp::exec
```

**红线**: `ExecutionContext::Init` 调用过两次 → `std::abort()`. CI fuzz test 覆盖.

`--mode=shadow` 与 `--mode=paper` 共享 PaperSigner binary, 但 trader 主进程内部:
- shadow → 真 RM evaluate + 真 router dry-run, 但 fill 走 VirtualMatcher
- shadow audit 走 `shadow_audit.wal` (第 4 类 WAL, 与 paper_audit 物理隔离, R-11)

---

## 3. 三 mock 接口 C++ class spec (上链 deferred 后必做)

所有 mock 接口**与未来 LiveSigner 完全对称**, 切换 live 时 PaperSigner → LiveSigner 一行 link 改, 不重做.

### 3.1 `INonceProvider` (mock #1, virtual_nonce)

```cpp
// src/exec/signer/common/signer_iface.h
namespace stcpp::exec::signer {

class INonceProvider {
public:
  virtual ~INonceProvider() = default;
  // 同步获取下一个 nonce. live 走 老叶 nonce_mgr (Redis + RPC observe);
  // paper 走 in-memory counter; backtest 走 deterministic seed.
  // 失败 (RPC 抖 / Redis 断) → tl::expected nullopt, 调用方 SAFE_MODE.
  virtual tl::expected<uint64_t, NonceError> next_nonce(const Address& signer) noexcept = 0;
  // 提交失败时回滚 (live mempool reject / paper unfill).
  virtual void rollback(const Address& signer, uint64_t nonce) noexcept = 0;
};

class VirtualNonceProvider final : public INonceProvider {
  std::atomic<uint64_t> counter_;
public:
  explicit VirtualNonceProvider(uint64_t start = 0) : counter_(start) {}
  tl::expected<uint64_t, NonceError> next_nonce(const Address&) noexcept override
  { return counter_.fetch_add(1, std::memory_order_relaxed); }
  void rollback(const Address&, uint64_t) noexcept override {}  // paper 不回滚
};
}  // namespace
```

### 3.2 `IGasEstimator` (mock #2, virtual_gas)

```cpp
struct GasEstimate {
  uint64_t gas_limit;        // wei
  uint64_t max_fee_per_gas;  // wei
  uint64_t max_priority;     // wei
  int64_t  estimated_at_ns;
};

class IGasEstimator {
public:
  virtual ~IGasEstimator() = default;
  virtual tl::expected<GasEstimate, GasError>
  estimate(const SignRequest& req) noexcept = 0;
};

class VirtualGasEstimator final : public IGasEstimator {
public:
  tl::expected<GasEstimate, GasError> estimate(const SignRequest&) noexcept override {
    return GasEstimate{ .gas_limit=250'000, .max_fee_per_gas=50e9, .max_priority=30e9,
                        .estimated_at_ns = stcpp::infra::clock::now_ns() };  // 不调 RPC
  }
};
```

paper 不真烧 gas, 但 audit 字段照写 (live / paper schema 一致, R-11 红线 + v0.2 P2).

### 3.3 `IConfirmWatcher` (mock #3, virtual_confirm)

```cpp
struct ConfirmEvent {
  ULID        audit_id;
  std::string tx_hash;             // paper: "paper_" + ulid_hex
  uint64_t    block_number;
  int64_t     confirmed_at_ns;
  uint8_t     n_confirmations;
};

class IConfirmWatcher {
public:
  virtual ~IConfirmWatcher() = default;
  // 注册一个待确认 tx; n_confirms 后 callback. live 走 polygon WS subscribe;
  // paper 走定时器模拟. callback 在 watcher 内部线程触发, 调用方需 thread-safe.
  virtual void watch(const ULID& audit_id,
                     const std::string& tx_hash,
                     uint8_t n_confirms,
                     std::function<void(const ConfirmEvent&)> cb) noexcept = 0;
};

class VirtualConfirmWatcher final : public IConfirmWatcher {
  // paper 模拟 confirm 时序: 每个 confirm 抽 truncated normal(mean=2.0s, σ=0.3s, [1.2s,4.0s])
  TimerWheel timer_;
  std::atomic<uint64_t> fake_block_counter_{42'000'000};
public:
  void watch(const ULID& aid, const std::string& tx, uint8_t n,
             std::function<void(const ConfirmEvent&)> cb) noexcept override;
};
```

**参数 (v1 起步, 等 live 数据 calibrate)**:
- block_time mean = 2.0s, σ = 0.3s, truncate [1.2s, 4.0s]
- n_confirms 默认 1 (Polymarket 设定; v2 走 2)
- jitter 用 `std::normal_distribution` + 固定 seed (回测可复现) / 时间 seed (paper 不可复现, 求贴近真实)

---

## 4. Mode A++ fill_rate Bernoulli 实施 (小袁 R-14)

paper Mode A 默认 fill_rate=1.0 → live 系统性偏差 0.1-0.3 Sharpe. 必须叠 Bernoulli sampler.

```cpp
// src/exec/signer/paper/fill_rate_sampler.h
namespace stcpp::exec::signer::paper {

struct FillRateConfig {
  // floor / cap: 小肖 v1 §4 FILL_RATE_FLOOR = 0.50; cap = 0.65 (小袁 v1 §3.5 hot 实测上限)
  double floor = 0.50;
  double cap   = 0.65;
  // 用小肖 SlippageEstimate.expected_fill_rate 做点估, 在 [floor, cap] 内做 Bernoulli;
  // 出区间夹紧 (低于 floor 直接拒 — 小肖红线; 高于 cap 截到 cap).
  uint64_t seed = 0;  // 0 = 时间 seed (paper); 固定值 = 回测可复现
};

class FillRateSampler {
public:
  explicit FillRateSampler(const FillRateConfig& cfg);

  // 输入小肖 SlippageEstimate, 输出 (filled? , effective_size_usdc).
  // expected_fill_rate < floor → 直接 not filled (小肖红线, REJECT 在 RM 层已发生, 这里防御);
  // 否则 p = clamp(expected_fill_rate, floor, cap), Bernoulli(p) 决定全成交 / 不成交.
  // size 不做部分: floor=0.5 = 全成交或全不成交 (小肖 §0 红线 "不允许先下 50% 试试").
  struct Result { bool filled; double effective_size_usdc; };
  Result sample(const slippage::SlippageEstimate& est, double order_size_usdc) noexcept;

private:
  std::mt19937_64 rng_;
};

}  // namespace
```

**调用点 (在 VirtualMatcher::simulate_taker_fill 内)**:
1. 拉 current_book snapshot (与 L2 一致, 实时)
2. 调小肖 `estimate_slippage(SlippageInput{...})` → `SlippageEstimate`
3. 若 `expected_fill_rate < floor` → return `Fill{UNFILLED, "FILL_RATE_FLOOR"}` (与 live RM 拒一致)
4. 否则 `FillRateSampler.sample()` → 决定 filled 与否
5. filled → `fill_price = expected_fill_price` (复用小肖 vwap), `fill_size = order_size`
6. unfilled → `Fill{UNFILLED, "BERNOULLI_LOST"}`

**与 prod 决策共享 90%+ 路径 (老高 R-11)**:
- L3 信号 / L4 RM evaluate / L5 router / audit / metrics — 完全同代码
- 仅 D-1 signer backend (PaperSigner ↔ LiveSigner)
- 仅 D-3 ledger provider (PaperLedger ↔ LiveLedger)
- 这两处加起来 < 200 行差异 (v0.2 §2.3 已锁)

---

## 5. shadow_audit.wal (paper_audit 第 4 类 WAL)

老王 v0.1 §4.1 原本三类 (`risk_audit` / `position` / `paper_audit`). **追加第 4 类 `shadow_audit`**, 路径 `/var/lib/stcpp/shadow/shadow_audit-*.wal`, FsyncMode = `GroupCommit`, owner = 小蒋, 与 paper_audit 物理隔离 (R-11: shadow 不进 paper 账本, paper 不进 prod). 用老王 `WalWriter<ShadowAuditRecord>::Open(cfg)` 接入, payload schema 与 paper_audit 完全一致 (含 audit_id / intent / SignRequest / Fill / virtual_nonce / virtual_gas / virtual_confirm_event), 仅 `mode` 字段区分 = `SHADOW`.

**问老王 (W1 内回执, Q-PE1/Q-PE2 见 §7)**.

---

## 6. M4.5 gate v2 脚本重写计划 (W6 deadline, 7/9)

### 6.1 v1 → v2 差异 (与小董 7 gate 对齐)

v0.2 §7 现 6 gate (G-A 到 G-F) → 重写为小董 v1 §5.2 的 7 hard gate.

- **G1** PnL > 0 **AND** t-test p < 0.10  (旧 G-B 加 t-test)
- **G2** Sharpe > 1.0 **AND** bootstrap CI 下界 > 0.3  (旧 G-C 加 CI)
- **G3** risk failure = 0  (不变)
- **G4** uptime ≥ 99.5%  (不变)
- **G5** max DD ≤ 8%  (新增)
- **G6** n_trades ≥ 50, 含 14d duration  (新增, 吸收旧 G-A)
- **G7** paper PnL > random baseline, paired, p<0.10  (旧 G-F 换为 random 而非 backtest baseline)

random baseline = 同窗口同事件下随机抽 BUY_YES/BUY_NO 1:1, size 同 Kelly, 跑 1000 次取 mean.

### 6.2 脚本结构 (`tools/m4_5_gate/run_gate_v2.py`)

仍是 Python tool-level (PR-8 红线: 不进 production hot path, 仅读 paper_audit WAL + metrics.json + random_baseline metrics.json). 入口函数 `run_m45_gate_v2(args) -> GateDecision`, 7 check 各独立函数, JSON schema 直接复用小董 §5.5. random baseline daily_pnl 由 C++ CLI `stcpp_random_baseline` 离线生成 1000 runs → metrics.json, Python 仅读.

### 6.3 W8 加 PBO + DSR (advisory, 不阻塞 gate)

- DSR (Deflated Sharpe): 小董 §6.1, advisory 输出 `dsr_score`
- PBO (Probability of Backtest Overfitting): 小董 §6.2, advisory `pbo_score`
- W8 加入后, gate JSON 增 advisory section, **不计入 PASS/HOLD** (避免 over-engineering MVP)

### 6.4 时间线

| W | 动作 | 协作 |
|---|---|---|
| W1 (现在) | 本 skeleton + paper_audit / shadow_audit WAL schema | 老王 + 小董 (review) |
| W2 | mock 接口实现 + Bernoulli sampler + 单测 | 小肖 + 小袁 |
| W3-W5 | VirtualMatcher 联调 + paper_audit 字段冻结 | 小袁 microstructure |
| **W6 (7/9)** | **gate v2 脚本重写完成 + 单测 + 与小董 review** | 小董 |
| W7 | shadow mode 联调 + dry-run | 全队 |
| W8 | 加 DSR + PBO advisory | 小董 |
| W9+ | paper D1 候补窗口 (v0.2 §9 9/12 gate 不变) | — |

---

## 7. 不耻下问 (W1 必答)

| # | 问 | 找谁 | 截止 |
|---|---|---|---|
| Q-PE1 | shadow_audit WAL 第 4 类 cpu core pin (避 core 7 thrash) | @老王 | W1 末 |
| Q-PE2 | shadow_audit replay 失败 = 重置 (paper 档) 还是拒启动 (risk 档) | @老王 | W1 末 |
| Q-PE3 | `INonceProvider::next_nonce` 在 paper 是否需要 thread-safe (多 signer 并发) | @老韩 (RM 接口) | W1 末 |
| Q-PE4 | `SlippageEstimate.expected_fill_rate` 是否已含 `time_since_quote_ms` 衰减 (避 sampler 重复扣) | @小肖 | W1 末 |
| Q-PE5 | Bernoulli seed 0=时间 seed 是否 OK (paper 不可复现) 还是必须 fixed (与 backtest 一致) | @小袁 + @小董 | W1 末 |
| Q-PE6 | G7 random baseline 是 C++ CLI 出 (我做) 还是 Python tool 出 (小董 做) | @小董 | W1 末 |

---

## 8. 验收清单 (W1 收工)

- [ ] 目录骨架 + CMake 三 target 跑通 + CI `nm` 隔离 test
- [ ] 三 mock 接口 (`VirtualNonceProvider` / `VirtualGasEstimator` / `VirtualConfirmWatcher`) + 单测
- [ ] `FillRateSampler` + 3 case 单测 (floor 拒 / cap 截 / 中间 Bernoulli)
- [ ] `ExecutionContext::Init` + abort-on-double-init 单测
- [ ] `shadow_audit` config 进 `wal.toml`, 老王 回 Q-PE1/Q-PE2
- [ ] `tools/m4_5_gate/run_gate_v2.py` stub + 7/9 deadline 上日历

---

**END v1.** 等 W2 收 Q-PE 回执, bump v1.1.

— 小蒋, 2026-05-28
