# 性能维度复盘评审 — 2026-05-30 会话

- Owner: 老姜 (performance-owner, 系统工程部 A)
- Last review: 2026-05-30
- 会话范围: MicroPUSD 全面接入 RiskConfig + P0-1 FeedRiskGateway 新增 + 伦敦 EC2 实测
- 参考: docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md (M2/M4 预算)
- 关联 ADR: ADR-027 (ABI lock) / ADR-004 (reject 顺序) / ADR-015 (vCPU pin)

---

## 执行摘要 (5 条)

| # | 结论 | 等级 | 行动 |
|---|---|---|---|
| 1 | MicroPUSD 全面接入 RM 零成本结论仍成立 — 但 bench 文件有编译断裂风险 | 黄 | bench 文件更新 (见 §1) |
| 2 | FeedRiskGateway 在 paper tick 路径 (500ms) 性能可接受, 但随仓位增长有 O(N) 退化风险 | 黄 | 补 bench + 增量化 backlog |
| 3 | t2.xlarge (Xeon E5-2686 v4) 无法达成 signer 8us 目标; 生产选 c6i.large 或 c7i.large | 红 | 换型号, Sprint-2 前落地 |
| 4 | 端到端延迟预算: FeedRiskGateway 不在热路径, 现有预算不受影响; 瓶颈仍是 SlippageModel 之后的 mutex 链 | 黄 | 评估 reader-writer 分离 |
| 5 | bench_risk_gateway / bench_e2e_latency 两个文件使用 v0.4 废弃字段, 在当前 OrderIntent v0.6 下已无法编译通过 | 红 | 立即修复 bench 文件 (P0 CI 门禁失效) |

---

## §1 MicroPUSD 全面接入后的热路径性能

### 1.1 零成本结论是否仍成立

结论: **仍成立**。

MicroPUSD 的类型属性保证了零额外运行期开销:

- `sizeof(MicroPUSD) == sizeof(int64_t)`, `standard-layout`, `trivially-copyable`
- 所有运算符均 `constexpr` 内联, 比较走 `operator<=>` defaulted, 编译器生成与裸 `int64_t` 相同的机器指令
- `from_micro(cur + size_micro)` — 整数加法一条指令, 无临时对象分配
- `from_pusd(double)` 仅在入口处调用一次 (TickOne 构造 sizing_cfg 时), 不在 evaluate() hot path 内

`check_position_caps_` 中 4 个比较点 (`per_order_cap`, `market_exposure_cap`, `per_outcome_cap`, `daily_loss_halt`) 全部 `.v` 取 int64 直比, 无 double 转换。

### 1.2 有没有全面铺开冒出的新开销

发现一个**编译断裂风险**但不是运行期开销:

`bench_risk_gateway.cpp` 和 `bench_e2e_latency.cpp` 中的 `make_cfg()` / `make_ok_intent()` 仍使用 OrderIntent v0.4 废弃字段:

- `it.market_id` — v0.5 已 rename 为 `condition_id`
- `it.is_buy` — v0.5 已改为 `side: Side enum`
- `it.size_usdc` — v0.6 已 rename 为 `size_pUSD_micro`
- `r.market_id` — AuditRecord 同样已 rename

另外 `make_cfg()` 里:
```
c.bankroll_usdc = 100'000;          // 应为 MicroPUSD::from_micro(100'000) 或 100'000_upusd
c.daily_loss_halt_usdc = 5'000;     // 同上, 现在是 MicroPUSD 字段
```
旧写法是给 `MicroPUSD` 字段直接赋 int 字面量, 因 explicit 构造被禁, **这两个文件无法编译**。

**结果**: CI perf 回归门禁已失效 (bench target 构建失败)。这是本会话最高优先级性能问题。

### 1.3 是否需要补 RM evaluate 的 micro-benchmark

需要, 且需要与 v0.6 接口对齐。建议改动点 (非本会话代码工作, 列入 backlog):

1. `make_ok_intent()`: 填 `condition_id` / `token_id` (合法 uint256 数字串) / `side = Side::Buy` / `outcome = Outcome::Yes` / `size_pUSD_micro = 1'000'000` (1 pUSD in micro) / `timestamp_ms` (当前 ms) / `metadata` / `builder` (合法 bytes32 hex 默认串)
2. `make_cfg()`: `per_order_cap_usdc = 1'000_pusd`, `market_exposure_cap_usdc = 5'000_pusd`, `bankroll_usdc = 100'000_pusd`, `daily_loss_halt_usdc = 5'000_pusd`
3. `NoopEmitter::emit()` 引用 `r.condition_id` 代替 `r.market_id`
4. 额外路径: 补 `BM_RiskGateway_Reject_Bytes32Format` (v0.6 Wave 3 新增字符校验 64 char loop, 值得单独测)

**触发机制**: `check_invalid_intent_` 新增了 bytes32 hex 格式校验 (`is_valid_bytes32_hex`, 64 次字符比较), 在 APPROVED 路径每次都跑完。这是 v0.6 Wave 3 引入的新开销, 量级预估 5-15ns (64 次 branch, cache hot), 在 100us 预算内可忽略, 但应有数据而非估算。

---

## §2 FeedRiskGateway 性能评估

### 2.1 现状 (paper tick 500ms 路径)

`FeedRiskGateway()` 调用链:

```
apply_fill 成功 → FeedRiskGateway()
  └── position_ledger_.get_per_condition_exposure()   // shared_lock + map copy
  │     → 返回 unordered_map<string, int64_t>
  └── position_ledger_.get_per_outcome_exposure()     // shared_lock + map copy
  │     → 返回 unordered_map<string, int64_t>
  └── 遍历 condition map → rm_.set_condition_exposure(cid, whole × 1e6)
  │     └── std::lock_guard<mutex> (s_->mu) + unordered_map 写
  └── 遍历 token map → rm_.set_outcome_exposure(tid, whole × 1e6)
        └── std::lock_guard<mutex> (s_->mu) + unordered_map 写
```

**N = active positions 数量** (condition + token 两类)。

每次 `FeedRiskGateway`:
- 2 次 `shared_lock` 加锁 + map copy (全量快照)
- N 次 `lock_guard<mutex>` (s_->mu) 加锁 + unordered_map 写 (set_condition/set_outcome)
- 触发频率: 仅在 fill 成功后调用 (paper tick 500ms, 非每 tick 必调)

MVP 场景 (N < 20 个 condition, < 40 个 token): 全量遍历开销在微秒级, 可接受。

### 2.2 退化风险 (全盘口场景)

全盘口覆盖时 (Moneyline + Totals + Spreads + 分节 + outright, N 可达 200-500 condition, 每 condition 2 token):

- `get_per_condition_exposure()` 返回 copy: 500 entry × ~32 byte/entry = 16KB allocation
- 500 次 `lock_guard(s_->mu)` 连续加解锁, 且 s_->mu 是 evaluate() 所有热路径共享的同一把锁
- **竞争风险**: evaluate() 在 check_position_caps_ 里也持 s_->mu; FeedRiskGateway 是 500 次连续持锁, evaluate() 会被 500 次短暂阻塞

量化估算 (N=500, t2.xlarge):
- 每次 mutex lock/unlock: ~100ns (无竞争) → 500次 = 50us
- unordered_map find+write: ~200ns/次 → 500次 = 100us
- map copy (shared_lock): ~2us/次 × 2 = 4us
- **合计约 150-200us/次 FeedRiskGateway**

paper tick 500ms 下此开销不在 critical path, 可接受。但未来降 tick 到 50ms 或仓位 N 增长, 需关注。

### 2.3 全量 vs 增量的权衡

老周选全量覆盖 (`set_condition_exposure` 写 RM map) 的理由是"自愈"(PositionLedger 是真值源, 全量覆盖防增量累积误差)。这个选择在 paper 模式下正确。

**增量方案的代价与收益**:
- 收益: FeedRiskGateway O(1) — 仅写刚成交的 condition/token
- 代价: RM exposure map 与 PL 的一致性依赖增量正确性; 若有 apply_fill 失败/回滚, 增量会漂移

**结论**: MVP 阶段维持全量。生产 live 路径进场前 (Sprint-3+) 评估增量化, 届时 N 会真正增长。

### 2.4 应补的 bench

`bench_risk_gateway.cpp` 需补: `BM_FeedRiskGateway_N_Positions(N)` — 测 N=1/10/50/100/500 时 FeedRiskGateway 的耗时, 建立 O(N) 基线防回归。这是目前唯一缺失的 O(N) 性能门。

---

## §3 生产实例选型建议

### 3.1 t2 vs 目标

t2.xlarge: Xeon E5-2686 v4, 2.3GHz base, 无 AVX-512, TSC 频率 2.3GHz。
实测: V53 signer 30us (debug), Release(-O2) 目标 8us 未达。

t2 比 Mac M 芯片慢 ~4x 的根因:
- M 芯片 NEON SIMD 宽度 + 乱序窗口远大于 Broadwell
- Xeon E5-2686 v4 是 2016 年 Broadwell-EP, IPC 劣势明显
- t2 无 CPU credit burst 保证 (T 系列 burstable), perf 不稳定

### 3.2 ed25519 签名延迟的架构决定因素

ed25519 (libsodium 或 donna 实现) 的瓶颈:
- 主路径: 点乘 + 标量乘法 (Montgomery ladder 或 GLV)
- 加速手段: AVX2/AVX-512 向量化 + 高主频

| 实例 | CPU | 主频 | AVX-512 | ed25519 估算 p99 |
|---|---|---|---|---|
| t2.xlarge | E5-2686 v4 (Broadwell) | 2.3GHz | 无 | ~30-40us |
| c5.xlarge | Xeon Platinum 8275CL (Cascade Lake) | 3.6GHz | 有 | ~10-15us |
| **c6i.xlarge** | **Intel Ice Lake Xeon (3rd Gen)** | **3.5GHz** | **有** | **~8-12us** |
| **c7i.xlarge** | **Intel Sapphire Rapids (4th Gen)** | **3.6GHz** | **有 (AMX 加)** | **~6-10us** |
| c6a.xlarge | AMD EPYC 3rd Gen (Milan) | 3.6GHz | AVX2 | ~12-18us |

**推荐**: c6i.xlarge 作为目标实例。理由:
- ICE Lake 有 AVX-512 + sha-ni + aes-ni
- libsodium 自动 dispatch AVX-512 路径, ed25519 比 Broadwell 快 2.5-3x
- 30us / 3 = 10us, 接近 8us 目标 (Release -O2 比 debug 再快 30-40%)
- 价格 vs c7i: c6i 约便宜 20%, MVP 阶段可接受

c7i.xlarge 若预算允许: Sapphire Rapids 有更大 L2 + 更优 branch predictor, ed25519 预估降到 6-8us, 有余量。

**不推荐 m 系列**: memory-optimized 为 NUMA 内存密度优化, 对签名 CPU bound 无增益。

### 3.3 Polymarket 2ms RTT 下 30us 签名的实战可接受性

**结论: paper 模式完全可接受; live 模式下 30us 是风险点但非阻断。**

延迟预算分配 (跨洋全链路):
```
数据到达 (Polymarket WSS push → EC2 网卡): ~2ms RTT/2 = ~1ms 单程
本机决策热路径 (critical path §1 合计 p99): ~1ms (W4 wave21 budget)
签名 (ec2 t2 现状): ~30us (debug), ~15us (Release)
Polymarket CLOB REST 提交 (EC2 → Polymarket): ~2ms RTT
```

总端到端约 4ms, 远低于 Polymarket 改单周期 (~Hz 级 = 1000ms)。30us 签名在 4ms 总链路里占比 < 1%, 对 alpha 窗口无实质影响。

**但**: 若未来降到 100ms tick 或使用更激进 quote 策略, 签名延迟的 p99.9 (可能 100-200us 尾刺) 需要关注。

---

## §4 延迟预算复盘

### 4.1 本会话改动对各段的影响

端到端分段 (引用 laojiang-latency-budget-w4-wave21-v1.md §1):

| 段 | 前预算 | 本会话改动 | 后影响 | 新估算 |
|---|---|---|---|---|
| M2 RM evaluate() | 100us | MicroPUSD 接入 + bytes32 校验 | +5-15ns (bytes32 loop) | ~100us 仍达标 |
| M4 PaperSigner.Sign | 50us | 无改动 | 无 | ~1us (paper mock) |
| FeedRiskGateway (新增) | 不在 critical path | 新增 fill 后调用 | **不在 critical path** (500ms tick 后执行) | ~50-200us (N=1-500) |
| PositionLedger.apply_fill | 30us (§1 step 9) | 无改动 | 无 | ~5us (estimated) |

**重要**: FeedRiskGateway 在 `apply_fill` 成功后、Step 8c 串行调用, 但这整条 (Step 7-8c) 不在 W4 wave21 定义的"决策 critical path"里 (那条路径终点是 VirtualFill emit, 不是 RM 喂数)。所以 FeedRiskGateway 的开销不影响现有预算表。

### 4.2 瓶颈分析

当前架构的性能瓶颈按优先级排:

**瓶颈 1 (P0): bench 文件编译失败** — 门禁失效。无法知道任何实测数据。

**瓶颈 2 (P1): evaluate() 中的单把互斥锁 (s_->mu)**

`s_->mu` 是 `RiskGateway::State_` 的唯一 mutex, 保护所有 map 访问。evaluate() 路径中以下步骤均持此锁:
- check_duplicate_: 1 次 lock (unordered_set insert)
- check_stale_data_: 1 次 lock (3 次 map find)
- check_market_: 1 次 lock (1 次 map find)
- check_position_caps_: 1 次 lock (3 次 map find)
- check_signal_: 1 次 lock (1 次 map find)
- check_strategy_decayed_: 1 次 lock (1 次 map find)

**合计 6 次 lock/unlock per evaluate()**, 且是同一把 `std::mutex`。在 paper 单线程下无竞争, 问题不大。但当 FeedRiskGateway (N 次连续写锁) 与 evaluate() 并发时 (即便 loop_thread_ 是串行, future 多策略场景), 会产生锁竞争。

**瓶颈 3 (P2): check_invalid_intent_ 的 `is_valid_bytes32_hex` 串行扫描**

每次 evaluate() 对 metadata (66 chars) 和 builder (66 chars) 各做一次线性扫描, 合计 128 次字符比较。无 SIMD 加速。估算 ~10-20ns。在 100us 预算里可忽略, 但可以用 SSE4.2 `pcmpestri` 一次 16 字节比较, 4 条指令完成 64 字符校验 (4x 加速)。这是可选优化, 非阻断。

**瓶颈 4 (P3): string 临时对象**

`signal_id = cfg_.strategy_id + "-" + std::to_string(intent_id)` 在 TickOne 每次调用时分配字符串。500ms tick 下无压力。未来降 tick 到 10ms 且跨多 token 时, 应改为 `fmt::format_to` 到栈 buffer 或 pre-allocated string。

### 4.3 端到端瓶颈总结

本会话改动不改变 W4 wave21 预算表的瓶颈排序:

```
critical path 主瓶颈 (排序):
1. WSS JSON parse: ~50us p99 (simdjson, step 1)
2. Signal tick: ~500us p99 (step 3, 5 触发条件 + Kelly)
3. RM evaluate: ~100us p99 (step 4, 6 次 mutex + SlippageModel)
4. 其余 (PaperSigner/VirtualMatcher/PositionLedger): <50us

FeedRiskGateway (不在 critical path, 500ms tick 后置):
- N=20 (MVP): ~10-20us, 可忽略
- N=500 (全盘口): ~150-200us, 需监控
```

---

## §5 应补的性能门禁

### 5.1 紧急 (P0 — bench 已断裂)

**立即修复**: `tests/perf/bench_risk_gateway.cpp` 和 `tests/perf/bench_e2e_latency.cpp` 与 OrderIntent v0.6 API 不兼容, 具体断裂点:

| 文件 | 断裂字段 | 正确字段 |
|---|---|---|
| bench_risk_gateway.cpp:56 | `it.market_id` | `it.condition_id` |
| bench_risk_gateway.cpp:60 | `it.is_buy = true` | `it.side = Side::Buy; it.outcome = Outcome::Yes;` |
| bench_risk_gateway.cpp:62 | `it.size_usdc = 1'000` | `it.size_pUSD_micro = 1'000'000LL` (1 pUSD) |
| bench_risk_gateway.cpp:36 | `r.market_id.size()` | `r.condition_id.size()` |
| bench_risk_gateway.cpp:73 | `c.bankroll_usdc = 100'000` | `c.bankroll_usdc = 100'000_pusd` |
| bench_risk_gateway.cpp:75 | `c.daily_loss_halt_usdc = 5'000` | `c.daily_loss_halt_usdc = 5'000_pusd` |
| bench_e2e_latency.cpp:116 | `intent.market_id` | `intent.condition_id` |
| bench_e2e_latency.cpp:120 | `intent.is_buy = true` | `intent.side = Side::Buy; intent.outcome = Outcome::Yes;` |
| bench_e2e_latency.cpp:122 | `intent.size_usdc = 500` | `intent.size_pUSD_micro = 500'000'000LL` (500 pUSD) |
| bench_e2e_latency.cpp:136 | `sreq.market_id` | `sreq.condition_id` (SignRequest 字段同步检查) |
| bench_e2e_latency.cpp:139 | `sreq.size_usdc` | `sreq.size_pUSD_micro` |
| bench_e2e_latency.cpp:152 | `vord.market_id` | `vord.market_id` (VirtualOrder 字段检查是否也已更名) |

此外 v0.6 OrderIntent 新增了 `timestamp_ms` / `metadata` / `builder` 必填字段, `check_invalid_intent_` 会因 `timestamp_ms == 0` 直接返回 `TS_V2_MISSING`, BM_RiskGateway_Approved bench 永远走 reject 路径, 测不到 approved。bench 修复时需补全这三个字段。

**修复后应立跑**: cmake -DSTCPP_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release, 确认 bench 构建+运行通过, 输出 JSON baseline 到 tests/perf/baselines/。

### 5.2 新增门禁 (P1)

**新增 BM_FeedRiskGateway_N_Positions(N)**:

应测 N = 1 / 10 / 50 / 100 / 500, 验证:
- O(N) 特性可见 (线性拟合 R^2 > 0.95)
- N=100 时 < 1ms (不阻塞 500ms tick 超过 0.2%)
- 建立 per-N baseline, CI 门设在 p99 退化 > 20% (此处 20% 不是 10%, 因为有 OS jitter 影响)

具体 bench 结构: 预填 N 条 PositionLedger 持仓 (apply_fill mock), 循环调用 FeedRiskGateway()。

**新增 BM_RiskGateway_Approved_V06_FullFields** (替换旧 BM_RiskGateway_Approved):

使用完整 v0.6 intent (timestamp_ms / metadata / builder 全填), 确保 Approved 路径走完 is_valid_bytes32_hex 全校验, 测得真实 p99。

### 5.3 现有门禁够不够

| bench | 状态 | 评估 |
|---|---|---|
| bench_risk_gateway (7 路径) | 已断裂 (v0.4 API) | 修复后覆盖主要路径, 但缺 v0.6 bytes32 路径 |
| bench_e2e_latency (SingleThread + LowerGuard) | 已断裂 (v0.4 API) | 修复后仍有效 |
| bench_paper_signer (Nonce/Gas/PitViolation/FullPath) | 完好 | 覆盖够 |
| bench_virtual_matcher | 完好 | 覆盖够 |
| bench_slippage (M1) | 完好 | 覆盖够 |
| bench_audit_emitter | 需检查 AuditRecord v0.6 字段兼容 | 待查 |
| FeedRiskGateway O(N) bench | 缺失 | 需新增 |

---

## §6 行动项汇总

| 优先级 | 行动 | Owner | ETA |
|---|---|---|---|
| P0 | 修复 bench_risk_gateway.cpp + bench_e2e_latency.cpp (v0.4 → v0.6 API + 补 timestamp_ms/metadata/builder) | GM (老雷主线写代码) | 本 Sprint |
| P0 | 确认 bench_audit_emitter.cpp AuditRecord 字段是否有同类断裂 | 老唐 (协助) | 本 Sprint |
| P1 | 新增 BM_FeedRiskGateway_N_Positions(N) bench | 老姜 (补 spec) + GM | Sprint-2 |
| P1 | 新增 BM_RiskGateway_Approved_V06_FullFields (完整 v0.6) | GM | 同上 |
| P2 | 生产实例从 t2.xlarge 迁移到 c6i.xlarge (ed25519 ~3x 加速) | 老吴 (SRE) | Sprint-2 前 |
| P2 | bytes32 hex 校验 SIMD 优化 (SSE4.2 pcmpestri, 128 → 32 比较) | 老姜 评估 + 小肖实施 | Sprint-3 |
| P3 | FeedRiskGateway 增量化 (全盘口 N=500+ 场景) | 老周 设计 + 老姜 bench 验收 | Sprint-3 |

---

## §7 未决 / 风险登记

| 风险 | 量化 | 是否阻断 MVP |
|---|---|---|
| bench 断裂导致 CI 门禁无数据 | 当前基线为空/错误 | 不阻断 MVP 功能, 但阻断性能可观测性 |
| t2 签名 30us (debug), Release 估算 ~15us, 目标 8us 未达 | 差距 ~2x | 不阻断 paper MVP (30us 在 4ms 链路 < 1%), 阻断 live 模式高频场景 |
| FeedRiskGateway N=500 ~200us | 500ms tick 下 0.04% 占用 | 不阻断 MVP |
| s_->mu 6 次 lock/evaluate, 未来多策略并发 | 竞争延迟不可预测 | 不阻断 MVP (单 loop_thread_), Sprint-3 评估 rwlock 分离 |

---

**备注**: 本文档为复盘供料, 产出供 GM + 老周 + 老韩 参考。代码修改由 GM 主线落地, 老姜 性能门定义 + benchmark spec 辅助支援。
