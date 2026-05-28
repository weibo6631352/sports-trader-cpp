# Paper Trading 引擎 v0.1

- Owner: 小蒋 (quant-backtest)
- Date: 2026-05-28
- 验收人: 小梁 (financial-expert) + 老周 (cpp-chief-architect) + 老韩 (risk-engineer, **paper trading 走 RM**) + 老雷 (GM, M4.5 gate)
- 关联:
  - 姊妹文档: `xiaojiang-backtest-framework-v0.1.md` (本人, backtest v0.1)
  - `xiaocheng-signal-catalog-v1.md` (小程 12 信号)
  - `xiaoxiao-kelly-slippage-model-v1.md` (小肖 Kelly + slippage v1; paper 用同份 slippage)
  - `laohan-riskmanager-design-v0.2.md` (老韩 RM v0.2; paper 必跑 RM)
  - `laozhou-architecture-v0.2.md` (老周架构 v0.2 §14 SAFE_MODE; paper signer 是其唯一替身)
  - `xiaoyuan-microstructure-v1.md` (小袁 Wave 6 在跑; paper fill 模拟依赖)
  - `xiaosong-test-replay-framework-v0.1.md` (小宋 replay 协议)
- Wave: 6
- 用户高优指令 (2026-05-28): "支持虚拟盘测试, 开始不投入真实资金. 虚拟盘稳定盈利后才跑实盘."
- 状态: v0.1 RFC, 待会签

---

## 0. TL;DR (老雷 + 用户 + 老周 + 老韩 看这段)

- **公司红线 (Wave 6 加固):** paper trading 是 **production 的一个 `mode = paper` flag**, 不是另写一套. **同一份 binary, 走完整 RiskManager + signer 链路**, 仅 signer 子进程在 paper 模式下产"模拟交易号", 不上链.
- **数据流真假对照:**
  - 真: Goalserve + Polymarket WSS / REST (从生产数据源拉)
  - 真: 信号 (小程 contract)
  - 真: 特征 pipeline
  - 真: RiskManager (同一份 RM, 同一份红线参数)
  - 真: signer 调用入口 (audit_id, IPC 协议都一致)
  - **假**: signer 子进程内部把 EIP-712 签名 + RPC 发送 跳过, 替换为 "虚拟成交模拟器", 按 §4 微观结构 fill 假成交
  - 假: PnL 是 paper PnL, 不是真钱
- **M4.5 门禁 (GM Wave 6 新红线 + 用户高优):**
  - 连续 2 周 paper trading
  - 累计 paper PnL > 0
  - Sharpe (按日) > 1.0
  - 风控失效次数 = 0 (任何 RM bypass / 数据 stale 未触发 halt = 失败)
  - 在线率 > 99.5%
  - 全部满足才允许切实盘; 不满足 = 不切
- **与 backtest 的差异**: backtest 用历史数据 + 历史 book, paper 用实时数据 + 实时 book. backtest signer 用 `BacktestSigner`, paper signer 用 `PaperSigner` (子进程).
- **与实盘的差异**: 仅 signer 子进程内部 swap 出来的 backend 不同, 全链路其他部分逐 bit 一致.
- **第一份 paper trading 启动**: M+4 (Sprint-4 末), 跑足 2 周 → M+4.5 gate 判定 (≈ 2026-09). 见 §7 时间表.

---

## 1. 设计目标 + 红线

### 1.1 目标

| # | 目标 | 度量 |
|---|---|---|
| P1 | paper trading 用同一份生产代码 (BR-1) | binary 完全一致 (md5sum 相同), 仅启动 flag `--mode=paper` 差异 |
| P2 | paper 与 live 行为不可区分 (除 fill backend) | RM / 信号 / feature pipeline / order intent / audit log 字段全同 |
| P3 | paper fill 模拟尽量贴近真实 (与小袁 microstructure 联动) | predicted vs actual fill slippage RMSE < 30bps (小肖 §6.2 同口径) |
| P4 | paper PnL 可与 backtest baseline 自动对比 | 同一份报告模板 (姊妹文档 §7.1) |
| P5 | M4.5 gate 自动判定脚本 | 1 个 Python 脚本, 读两周数据 → 出 PASS/FAIL + 理由 |
| P6 | 在线率 > 99.5% (M4.5 门禁) | 同实盘观测口径 (小郑 observability) |
| P7 | paper mode 任何时候可立即切回 backtest 或 live | 启动 flag 切换, 无需重 build |

### 1.2 红线 (不允许妥协, GM Wave 6 加固)

| # | 红线 | 来源 |
|---|---|---|
| PR-1 | **paper 不是另一套代码, 是同一份 binary 的 mode flag** | GM Wave 6 + D-04 + 用户高优 |
| PR-2 | **paper 必跑 RiskManager 同一份 (含同款 STALE / cap / Kelly 阈值)** | 老韩 RM v0.2 G1 + GM W-2 |
| PR-3 | **paper 数据源是真实生产数据源** (实时 Goalserve + 实时 Polymarket WSS) | 用户高优 |
| PR-4 | **paper signer 替身子进程在编译期决定**, 不是运行期切 | 老沈 TB-B 红线 (避免 paper backend 被 prod 调用) |
| PR-5 | **paper 报告与 live 报告同模板**, 一目了然对比 | 老雷 + 用户高优 |
| PR-6 | **不达 M4.5 gate 不切实盘** | GM Wave 6 + 用户高优 |
| PR-7 | **PaperSigner 严禁存在于 live binary** (CMake / 链接器隔离) | 老沈 + 老周 §2.4 三层防御 |

### 1.3 不在本文档范围

- backtest framework → 姊妹文档
- 信号实现 → 小程
- RM 内部 → 老韩
- signer 实现 → 老孙 (live 真 signer)
- 真实链上交互 → 老叶 (RPC) / 老李 (Polymarket CLOB)
- 微观结构模拟参数标定 → 小袁 Wave 6

---

## 2. 与 backtest / live 的差异矩阵

### 2.1 三向对比表

| 维度 | backtest | paper | live |
|---|---|---|---|
| 时间轴 | 历史时间 (virtual clock) | 实时 wall clock | 实时 wall clock |
| 数据来源 | Parquet 历史 (小余 / 小段) | Goalserve / Polymarket WSS 真实流 | 同 paper |
| feature pipeline | 同一份 .so (BR-1) | 同 | 同 |
| 信号 | 同一份 contract (小程) | 同 | 同 |
| RiskManager | 同一份 .so (backtest_config.toml) | 同一份 .so (paper_config.toml = prod_config.toml 派生) | 同一份 .so (prod_config.toml) |
| signer 入口 (audit_id + IPC) | 走 BacktestSigner (in-process 假) | 走 PaperSigner 子进程 (IPC 真) | 走 RealSigner 子进程 (IPC 真) |
| signer 后端 | 直接产虚拟 fill (无 IPC) | 子进程内 swap 出"虚拟撮合器" (有 IPC) | 子进程内真 EIP-712 + RPC |
| fill 来源 | slippage 模型按 size + book 模拟 | 微观结构 + 实时 book + Bernoulli (§4) | Polymarket 真实成交回报 |
| PnL | paper PnL (历史 settle) | paper PnL (实时 settle) | 真实 PnL (USDC 余额变动) |
| audit log | 写 backtest run dir | 写 prod-style audit WAL (同款 schema) | 同 paper |
| 监控 | 离线 | 完整 Prometheus / Grafana / 报警 | 同 paper |
| in-flight 失败处理 | 跑批不影响 | 触发 SAFE_MODE / HALTED 同 live | 同 paper |
| signer 子进程 crash | 不存在 | trader SAFE_MODE + 重连 | 同 paper |
| 在线率要求 | n/a | > 99.5% (M4.5 门禁) | > 99.9% |

### 2.2 "完全相同" 的部分 (清单)

PR-1 红线落地: 以下模块**逐 bit 相同 (binary 一致)**:
- L1 INFRA: 所有
- L2 DATA: 所有 (含 WSS / REST 客户端 / book builder / feature pipeline / heartbeat / clock)
- L3 STRATEGY: 所有 (含信号 / 定价 / 对冲)
- L4 RISK: RiskManager 内部全部, RiskGateway 接口
- L5 EXECUTION: order state machine / nonce manager / fill ingestion / recon 全部
- audit log schema / RPC 协议 / IPC 协议 / metric 名称 / 报警规则

### 2.3 "刻意不同" 的部分 (≤ 3 处, 共 ≤ 200 行代码)

| # | 位置 | 差异 | 实现 |
|---|---|---|---|
| D-1 | signer 子进程的 backend | live = RealSigner / paper = PaperSigner / backtest = BacktestSigner | CMake 编译期分支 (3 个 binary) **OR** 同一 binary 启动期注入 (见 §3.2) |
| D-2 | RM 配置文件 | paper_config 与 prod_config 极小差异: ledger 上限 capacity 标识 (paper bankroll 是虚拟) | TOML 文件差异, RM 代码不变 |
| D-3 | ledger 真值源 | live 从链上 USDC balance 拉 + Polymarket data-api recon; paper 内置虚拟 ledger | LedgerProvider 抽象 (§5) |

### 2.4 "完全不能跨越" 的红线 (CMake / 链接器隔离, PR-7)

- **PaperSigner / BacktestSigner 不允许进入 live binary**:
  - 三个 CMake target: `stcpp_trader_live` / `stcpp_trader_paper` / `stcpp_trader_backtest`
  - link 阶段, `RealSigner` 只链入 live target; PaperSigner 只链入 paper; BacktestSigner 只链入 backtest
  - CI 静态扫: `nm stcpp_trader_live | grep -E "Paper|Backtest" && exit 1`
  - 任何 PR 让 PaperSigner 出现在 live binary → CI 拒
- **共享 .so 反过来可以**: feature_pipeline.so / risk_manager.so 三个 target 共享 (这是 BR-1 红线落地)

---

## 3. 架构 (paper mode 是 production flag)

### 3.1 模块图

```
                          +-------------+
                          | Goalserve   |  (生产数据源, 真实实时)
                          | inplay/live |
                          +------+------+
                                 |
                                 ▼
+-------------+          +-------------+         +-------------+
| Polymarket  |          |   L2 DATA   |         | Polymarket  |
| gamma/clob  | ───────► | ingest +    | ◄────── | WSS market/ |
| REST snap   |          | book builder|         |   user      |
+-------------+          +------+------+         +-------------+
                                |
                                ▼
                          +-------------+
                          | L3 STRATEGY |  (小程信号, 同一份)
                          +------+------+
                                 |
                                 ▼ OrderIntent (paper 与 live 字段全同)
                          +-------------+
                          | L4 RISK     |  (RM 同一份)
                          | RiskGateway |
                          +------+------+
                                 |
                                 ▼ RiskDecision (含 audit_id)
                          +-------------+
                          | L5 EXEC     |  (state machine 同一份)
                          | order SM    |
                          +------+------+
                                 |
                       audit_id + intent
                                 |
                                 ▼ (IPC 经过 TB-B 边界)
                  +----------------------------+
                  | signer 子进程 (mode 差异)  |
                  |  paper: PaperSigner        |
                  |  live:  RealSigner         |
                  +----------+-----------------+
                             |
                             ▼ (paper) 模拟 fill              live: 链上 tx + 回执
                  +----------------------------+
                  |  paper: VirtualMatcher     |
                  |   (§4 微观结构)            |
                  +----------+-----------------+
                             |
                             ▼ (paper) Fill (虚拟)
                  +----------------------------+
                  |  L5 fill ingestion (同一份) |
                  |  → ledger update           |
                  +----------------------------+
                             |
                             ▼
                  +----------------------------+
                  |  L1 metrics / audit / log  |
                  |  (与 live 同一份)           |
                  +----------------------------+
```

### 3.2 signer 子进程 backend 切换实现

两个方案, 我推荐**方案 A (编译期)**:

**方案 A (推荐, PR-7 红线):** 三个独立 binary, CMake target 分流

```cmake
# CMakeLists.txt 节选
add_executable(stcpp_signer_live  src/exec/signer/signer_main.cc)
target_link_libraries(stcpp_signer_live PRIVATE real_signer infra crypto)

add_executable(stcpp_signer_paper src/exec/signer/signer_main.cc)
target_link_libraries(stcpp_signer_paper PRIVATE paper_signer virtual_matcher infra)

# CI 校验
add_test(NAME signer_isolation
         COMMAND ${CMAKE_COMMAND} -E env
                 bash -c "nm stcpp_signer_live | grep -E 'paper_signer|virtual_matcher' && exit 1 || exit 0")
```

trader 主进程通过启动 flag 决定 fork 哪个 signer:
- `--mode=live` → exec stcpp_signer_live
- `--mode=paper` → exec stcpp_signer_paper
- `--mode=backtest` → 不 fork, in-process BacktestSigner

**方案 B (启动期注入):** 同一份 signer binary, 启动期读 env / flag 决定 backend

劣势:
- live binary 中存在 PaperSigner 代码 → 攻击面增大 (老沈不喜欢)
- CMake 隔离失效, 退化为运行期防御

**决定:** 走方案 A. 与老沈 (TB-B owner) 会签 6/12.

### 3.3 IPC 协议 (paper 与 live 完全一致)

trader 主进程 → signer 子进程的 IPC 协议**任何 mode 都一致**:

```
struct SignRequest {
    audit_id        : ULID (16 byte)
    intent_id       : ULID (16 byte)
    market_id       : char[32]
    side            : enum {BUY_YES, SELL_YES, BUY_NO, SELL_NO}
    size_usdc       : decimal128
    limit_price     : decimal64
    order_type      : enum {LIMIT, TAKER}
    submission_deadline_ns : i64
}

struct SignResponse {
    audit_id        : ULID
    status          : enum {SUBMITTED, REJECTED_BY_SIGNER, TIMEOUT}
    tx_hash         : char[66]  # 0x... 64 hex chars
    submitted_at_ns : i64
    reject_reason   : string
}
```

paper mode 下:
- `tx_hash` 由 PaperSigner 内部生成 (`"paper_" + ulid_hex`), 形如 `paper_01HKQ...32 hex chars...`
- 协议字段不增不减, audit log / metric / dashboard 不必区分
- 唯一区分点: 在 `tx_hash` 是否以 `paper_` 前缀, 这是日志 grep 用的标识 (不是业务逻辑用)

### 3.4 fill 回报路径 (paper 与 live)

**live fill 回报:**
```
Polymarket data-api / WSS user channel → L5 fill ingestion → ledger
```

**paper fill 回报:**
```
PaperSigner 子进程内部 VirtualMatcher → IPC `FillNotify` 给 trader
                                       → L5 fill ingestion → ledger
                                       (与 live 同一份代码)
```

**关键点:** L5 fill ingestion 模块**不知道**自己接的是真 fill 还是 paper fill. `FillNotify` 消息格式完全一致. 这是 PR-1 落地的关键.

---

## 4. 虚拟成交模拟 (与小袁微观结构对接)

### 4.1 总体思路 (与小袁会签)

paper 模式核心难点 = 虚拟成交怎么模拟才不脱离实际. 三种交易 type 分别处理:

| order type | live 行为 | paper 模拟 |
|---|---|---|
| **TAKER (limit price = best_ask 或更激进)** | 立即吃 book, 按 depth 吃多档 | §4.2 depth-fill 模拟 |
| **LIMIT passive (远离 best, 挂单等)** | 进 book 当 maker, 等对手方吃 | §4.3 maker-trigger 模拟 (需要小袁 fill probability 模型) |
| **LIMIT crossing (limit price 越过 best)** | 同 TAKER 一样吃 | 同 TAKER |

### 4.2 TAKER fill 模拟 (按 depth fill, 复用小肖 §3.1)

```python
def simulate_taker_fill(intent, current_book, slip_estimate):
    """
    intent: SignRequest (从 trader 收到)
    current_book: 实时 Polymarket book snapshot (从 L2 DATA 拿)
    slip_estimate: SlippageEstimate (小肖 §3.1 已算过的, 复用)
    """
    # 1. 校验 quote_price 与 current_book 一致性 (容差: < 200ms)
    if abs(intent.limit_price - current_book.best_ask) > 2 * tick_size:
        # quote 失效, 模拟 RESEND or REJECT
        return Fill(status="UNFILLED", reason="QUOTE_STALE")

    # 2. 按 slippage model 已算的 expected_fill_price + fill_rate
    rng = self.rng
    actually_filled = rng.random() < slip_estimate.expected_fill_rate
    if not actually_filled:
        return Fill(status="UNFILLED", reason="WITHDRAWN_OR_RACE_LOST")

    # 3. fill price 用 expected_fill_price (vwap)
    # 4. fill latency: e2e 实测 (老姜 latency budget §11) 加 noise
    e2e_lat_ms = self.latency_sampler.sample()  # 用真实 percentile 分布

    fill = Fill(
        audit_id=intent.audit_id,
        status="FILLED",
        fill_price=slip_estimate.expected_fill_price,
        fill_size=intent.size_usdc * slip_estimate.expected_fill_rate,  # 部分成交也有可能, paper 简化为全或无
        fill_ts_ns=self.clock.now_ns() + e2e_lat_ms * 1_000_000,
    )
    return fill
```

**关键点:**
- 复用 slippage model, 不再造一份 (BR-5 红线)
- 实时 book 而非历史 (paper 与 backtest 关键差异)
- E2E latency 用真实分布抽样 (与老姜 latency-budget 联动), 而不是固定值

### 4.3 maker fill 模拟 (待小袁 microstructure)

挂单当 maker 难度更高: 需要预测 "我的挂单会不会被对手方吃". 这是小袁 Wave 6 的核心任务:

```python
def simulate_maker_fill(intent, current_book, micro_model):
    """
    micro_model: 小袁 microstructure 模型, 给定 (limit_price, current_book, 时间窗)
                  → P(fill in next Δt) + expected_fill_time
    """
    # 1. 估算 fill 概率
    p_fill_in_window = micro_model.fill_probability(
        limit_price=intent.limit_price,
        book=current_book,
        time_window_s=60,   # 我们的 fallback 是 60s 后转 taker
    )

    # 2. Bernoulli 抽样
    if rng.random() < p_fill_in_window:
        fill_time = micro_model.sample_fill_time(...)
        return Fill(
            status="FILLED",
            fill_price=intent.limit_price,  # maker 拿到 limit price (无 slippage)
            fill_ts_ns=self.clock.now_ns() + fill_time * 1_000_000_000,
            ...
        )
    else:
        # 60s 后转 taker (小程 §3.1 fallback 逻辑)
        # 在 paper 中也走完整 taker 路径
        return self.simulate_taker_fill(intent_now_taker, current_book, ...)
```

**对小袁的需求 (@小袁 Wave 6 输出):**
- `fill_probability(limit, book, window)` 模型, 输入 limit price + 实时 book, 输出 fill 概率
- `sample_fill_time(...)` 给 expected fill 时间分布
- 标定数据: Polymarket 历史 trade tape (小余) + book snapshots → 反推 maker fill rate

**v0.1 paper 启动时的占位:** 在小袁模型出来前, 用极简版本:
- p_fill_in_60s = max(0, 1 - 5 * abs(limit_price - mid) / spread) (距离越近概率越高)
- 这是 placeholder, 标定 RMSE 会差, 但能让 paper 跑起来

### 4.4 取消单 + 撤单模拟

撤单不经 RM evaluate (老韩 §13.4), 走 exec 直接撤. paper 模式:
- PaperSigner 收到 cancel 请求 → 立即从 virtual book 中拿掉 (假设 100% 撤单成功)
- 实盘有可能 race lost (撤单时单已成), paper 简化为忽略此 race

**反事实 (counterfactual) edge case (paper 跟踪但不影响 PnL):**
- 标记 cancel 时若 simulator 内部状态显示"刚刚刚 fill", paper 也跟着 fill (跟实盘行为一致)

### 4.5 部分成交 (partial fill)

live 经常出现 partial fill. paper 模拟:
- 简化: 全或无 (Bernoulli, 用 fill_rate 当 P(全部成交))
- v0.2 升级: 按 fill_rate 抽部分 fill size

**注意:** 老肖 §3.1 设计了 `FILL_RATE_FLOOR = 0.50` 红线, 半成交以下直接 RM REJECT. 所以 paper 模拟中 fill_rate < 0.5 的不会到 PaperSigner (RM 已 reject), 我们模拟范围 [0.5, 1.0].

---

## 5. RiskManager 在 paper 模式 (走完整 RM, PR-2 落地)

### 5.1 paper_config.toml vs prod_config.toml

```toml
# configs/rm/prod_config.toml (live 用)
[rm]
mode = "live"
PER_ORDER_CAP_HARD_USDC      = 5000     # constexpr 兜底
PER_ORDER_CAP_SOFT_USDC      = 2000
MARKET_EXPOSURE_PCT          = 2.0
KELLY_FRACTION               = 0.25
DAILY_LOSS_PCT               = 3.0
CONSEC_LOSS_N                = 5
STALE_WSS_WARN_MS            = 2000     # 老韩 v0.2 §11
STALE_WSS_HALT_MS            = 10000
STALE_GOAL_WARN_MS           = 5000
STALE_GOAL_HALT_MS           = 15000
STALE_RECON_WARN_MS          = 10000
STALE_RECON_HALT_MS          = 30000    # D-06 红线
SAFETY_BUFFER_PCT            = 5.0
[rm.ledger]
provider = "live_chain_recon"           # 链上 + data-api

# configs/rm/paper_config.toml (paper 用)
[rm]
mode = "paper"
# 红线参数全部一致 (PR-2)
PER_ORDER_CAP_HARD_USDC      = 5000
PER_ORDER_CAP_SOFT_USDC      = 2000     # 同 prod
MARKET_EXPOSURE_PCT          = 2.0
KELLY_FRACTION               = 0.25     # 同 prod
DAILY_LOSS_PCT               = 3.0
CONSEC_LOSS_N                = 5
STALE_WSS_WARN_MS            = 2000     # 数据源是真实生产, 同阈值
STALE_WSS_HALT_MS            = 10000
STALE_GOAL_WARN_MS           = 5000
STALE_GOAL_HALT_MS           = 15000
STALE_RECON_WARN_MS          = 10000
STALE_RECON_HALT_MS          = 30000
SAFETY_BUFFER_PCT            = 5.0
[rm.ledger]
provider = "paper_virtual"              # 虚拟 ledger (§5.2)
[rm.bankroll]
virtual_initial_usdc = 20000            # 初始虚拟 bankroll
```

**关键约束 (PR-2 + 红线):**
- 所有红线参数 (HARD cap, KELLY_FRACTION, STALE 阈值, daily/consec loss) **必须一致**
- 启动期 self-check: paper_config 加载时校验"红线字段与 prod_config 一致", 不一致 = abort

### 5.2 LedgerProvider 抽象

老韩 RM 内部 `positions_ledger` 通过 LedgerProvider 接口拿数据:

```cpp
class LedgerProvider {
public:
    virtual Positions current_positions() const = 0;
    virtual double bankroll_usdc() const = 0;
    virtual int64_t last_recon_sync_ts_ns() const = 0;
    virtual void apply_fill(const Fill& f) = 0;
};

class LiveLedgerProvider : public LedgerProvider {
    // 接 polygon RPC + polymarket data-api, 同步链上余额
};

class PaperLedgerProvider : public LedgerProvider {
    // 内置虚拟 ledger, apply_fill 直接更新内存 + 持久化到 paper_ledger.wal
    // last_recon_sync_ts_ns 总是 now() (永远 fresh, 因为是内置)
};
```

**老韩 RM 内部代码不感知 paper vs live**: 通过 DI 注入 provider, RM 调用 `provider->current_positions()` 即可.

### 5.3 paper 是否触发 stale halt?

**是.** PR-3 红线要求 paper 数据源是真实生产, 所以 WSS / Goalserve 真断流时, paper RM 同 live 一样进 HALT. 这是 M4.5 gate "风控失效 = 0" 的关键: paper 必须暴露生产数据基础设施的脆弱性, 不能"为了让 paper 看起来好看而忽略 stale".

### 5.4 paper 中的 SAFE_MODE / 崩溃恢复

老韩 v0.2 §13 SAFE_MODE 在 paper 模式**同样生效**:
- paper trader 重启 → 默认 SAFE_MODE → 对账 paper ledger (虚拟 ledger 从 WAL replay) → 5min OK + 手工 unlock
- 这是 M4.5 gate "在线率 > 99.5%" 验证的: 我们要看 paper 模式下 SAFE_MODE 累计耗时是否可接受

**虚拟 ledger 持久化:**
- `paper_ledger.wal` 在每次 fill 后 fsync (与 nonce WAL 同款保证)
- 崩溃恢复 = replay WAL → 重建虚拟 positions + bankroll
- 这条 WAL 不可丢, 否则 paper 状态错乱

---

## 6. 数据流详解 (真实数据 → 真信号 → 真风控 → 虚拟下单)

### 6.1 完整流程时序

```
T0:  Polymarket WSS push 一个 book update
     → L2 DATA 解析 → book builder 更新 → feature pipeline 拉新 mid
     → strategy 评估 P0-01 触发条件: |p_pm - p_pinn_novig| >= 3¢ ?
     → 触发 → 构造 OrderIntent (含 audit_id, market_id, size, edge, ...)

T0+50us:  intent 到 RiskGateway (本地 SPSC)
     → RM 跑 R0..R9
     → RiskDecision = APPROVED (audit_id 落 audit WAL)

T0+250us:  L5 EXEC state machine 接收 decision
     → 经过 IPC (TB-B) → signer 子进程

T0+300us (paper 模式):
     → PaperSigner 收 SignRequest
     → VirtualMatcher.simulate_taker_fill(intent, current_book, slip_estimate)
       → rng + slippage model + microstructure 抽 fill
     → 产 `paper_0x...` 假 tx_hash
     → IPC 回 SignResponse (status=SUBMITTED, tx_hash=paper_xxx)

T0+10s:  VirtualMatcher 内部定时器到期 (模拟 settle 延迟)
     → 产 FillNotify → IPC 回 trader
     → L5 fill ingestion → PaperLedgerProvider.apply_fill
     → audit log + metric 更新

T1 (settle 时, NBA 比赛结束):
     → ledger MTM → realized PnL accrual
     → 累计写 paper_pnl_daily.parquet
```

### 6.2 数据真实性约束

| 数据 | 来源 | 真假 |
|---|---|---|
| Polymarket book | Polymarket WSS 实时 | 真 |
| Polymarket trades | Polymarket WSS 实时 | 真 |
| Pinnacle odds | Pinnacle API 实时 (跨洋链路, 老李路径 A/B) | 真 |
| Goalserve livescore | Goalserve push 实时 | 真 |
| 比赛结果 (settle) | Polymarket data-api `markets/{id}/resolution` | 真 |
| 我方下单 tx | PaperSigner 生成 | 假 |
| 我方 fill 回报 | VirtualMatcher 生成 | 假 |
| 我方 ledger / PnL | PaperLedgerProvider 计算 | 假 |

### 6.3 settle 时机 (虚拟 vs 真实)

**live**: Polymarket 二元市场 settle 后, data-api 返回 resolution, 我们的 token 持仓变成 USDC.

**paper**: 同样监听 data-api resolution, 但 ledger 应用是虚拟的:
- 持仓 YES token, market resolve YES = win → PnL = (1 - fill_price) * size
- 持仓 YES token, market resolve NO = lose → PnL = -fill_price * size

**关键**: settle 时间, 真实 wait clock (paper 必须等真比赛结束), 不模拟. 这是 paper trading 与 backtest 最大差异之一. 比赛 4h, 我们就等 4h.

### 6.4 与 backtest baseline 联动

paper 跑 2 周, 我们同时拿当周历史数据跑 backtest:
- 每日定时 (UTC 04:00) 触发 backtest, 数据窗 = T-7d ~ T-1d, OOS 模式
- 输出 backtest baseline metrics.json
- 与 paper 当日 metrics 对比 (§7 报告)

```bash
# 每日自动化
python -m backtest run \
  --config configs/signals/p0_01_pinnacle_novig.yaml \
  --is-start "$(date -d '7 days ago')" \
  --is-end "$(date -d '1 day ago')" \
  --output runs/baseline_$(date +%Y%m%d)/

# 与 paper 对比
python -m paper.report \
  --paper-start "$(date -d '7 days ago')" \
  --paper-end "$(date -d '1 day ago')" \
  --baseline runs/baseline_$(date +%Y%m%d)/ \
  --output reports/paper_vs_baseline_$(date +%Y%m%d).html
```

---

## 7. M4.5 Gate 自动判定脚本

### 7.1 门禁条件 (GM Wave 6 + 用户高优)

| # | 条件 | 度量 | 阈值 | 拒绝行为 |
|---|---|---|---|---|
| G-A | 连续运行时长 ≥ 14 天 | `(last_ts - first_ts) > 14 * 86400` | 14d | 不足 14d → FAIL_DURATION |
| G-B | 累计 paper PnL > 0 | `sum(daily_pnl)` | > 0 USDC | ≤ 0 → FAIL_PNL |
| G-C | 日 Sharpe > 1.0 | `mean(daily_pnl_pct) / std(daily_pnl_pct) * sqrt(252)` | > 1.0 | ≤ 1.0 → FAIL_SHARPE |
| G-D | 风控失效次数 = 0 | 见 §7.2 失效定义 | 0 | > 0 → FAIL_RISK |
| G-E | 在线率 > 99.5% | `uptime_seconds / total_seconds` | > 0.995 | ≤ 0.995 → FAIL_UPTIME |
| G-F | OOS / IS Sharpe ratio > 0.6 (vs backtest baseline) | paper Sharpe / baseline backtest Sharpe | > 0.6 | ≤ 0.6 → FAIL_OOS_DECAY |

全部 PASS → `verdict = PASS_FOR_LIVE`. 任何一条 FAIL → `verdict = HOLD_OR_REWORK`.

### 7.2 "风控失效" 严格定义 (与老韩会签)

**风控失效** = 以下任意之一:
1. 任何 RM bypass 被检测到 (audit log 中有"signer 收到 SignRequest 但 audit log 中无 APPROVED 记录")
2. 数据 stale > HALT 阈值但 RM 状态机未进 HALTED (违反 §3.7)
3. 单笔 size 超过 PER_ORDER_CAP_HARD 但被 approved
4. SAFE_MODE 期间被允许开仓
5. EDGE_CI_NEGATIVE 被 approve (在 v0.2 单列 enum 之后这应该不再可能, 但保留检测)
6. fill_rate < FILL_RATE_FLOOR 被 approve
7. 任何 NaN / Inf 进入 RM 决策路径 (小肖 §0 红线)

每条由 `tools/m4_5_gate/risk_failure_detector.py` 离线扫 paper 期间 audit log + signer log 检测.

### 7.3 自动判定脚本 (一键运行)

```bash
# 脚本路径
tools/m4_5_gate/run_gate_check.py

# 用法
python tools/m4_5_gate/run_gate_check.py \
  --paper-start 2026-09-01 \
  --paper-end   2026-09-14 \
  --paper-run-dir /var/log/stcpp/paper/ \
  --baseline-backtest runs/baseline_p0_01_aug.parquet \
  --output reports/m4_5_gate_decision.json
```

**脚本实现 (伪代码):**

```python
def run_m45_gate(args) -> GateDecision:
    paper = load_paper_run(args.paper_run_dir, args.paper_start, args.paper_end)
    baseline = load_backtest(args.baseline_backtest)

    checks = {}

    # G-A duration
    duration_days = (paper.last_ts - paper.first_ts) / 86400
    checks["G-A_DURATION"] = Check(
        passed = duration_days >= 14,
        value = duration_days,
        threshold = 14,
        message = f"Paper duration: {duration_days:.1f} days",
    )

    # G-B PnL
    total_pnl = paper.daily_pnl.sum()
    checks["G-B_PNL"] = Check(
        passed = total_pnl > 0,
        value = total_pnl,
        threshold = 0,
        message = f"Total paper PnL: ${total_pnl:.2f}",
    )

    # G-C Sharpe
    sharpe = paper.daily_pnl_pct.mean() / paper.daily_pnl_pct.std() * np.sqrt(252)
    checks["G-C_SHARPE"] = Check(
        passed = sharpe > 1.0,
        value = sharpe,
        threshold = 1.0,
        message = f"Daily Sharpe: {sharpe:.2f}",
    )

    # G-D Risk failure
    risk_failures = scan_risk_failures(paper.audit_log, paper.signer_log)
    checks["G-D_RISK"] = Check(
        passed = len(risk_failures) == 0,
        value = len(risk_failures),
        threshold = 0,
        message = f"Risk failures: {len(risk_failures)} ({risk_failures})",
    )

    # G-E Uptime
    uptime_ratio = paper.uptime_seconds / paper.total_seconds
    checks["G-E_UPTIME"] = Check(
        passed = uptime_ratio > 0.995,
        value = uptime_ratio,
        threshold = 0.995,
        message = f"Uptime: {uptime_ratio:.4f}",
    )

    # G-F OOS decay
    paper_sharpe = sharpe
    baseline_sharpe = baseline.sharpe_oos
    decay_ratio = paper_sharpe / baseline_sharpe if baseline_sharpe > 0 else 0
    checks["G-F_OOS_DECAY"] = Check(
        passed = decay_ratio > 0.6,
        value = decay_ratio,
        threshold = 0.6,
        message = f"Paper/Baseline Sharpe ratio: {decay_ratio:.2f}",
    )

    all_pass = all(c.passed for c in checks.values())
    verdict = "PASS_FOR_LIVE" if all_pass else "HOLD_OR_REWORK"

    return GateDecision(
        verdict=verdict,
        checks=checks,
        ratified_at=datetime.utcnow(),
        ratified_by="auto",
    )

def scan_risk_failures(audit_log_path, signer_log_path) -> list[RiskFailure]:
    """7.2 中 7 条失效检测"""
    failures = []
    # 1. signer 收到但无 RM APPROVED
    audit_ids = set(load_approved_audit_ids(audit_log_path))
    signer_audit_ids = set(load_signer_audit_ids(signer_log_path))
    bypass = signer_audit_ids - audit_ids
    if bypass:
        failures.append(RiskFailure("BYPASS", details=list(bypass)[:10]))
    # ... 其他 6 条
    return failures
```

### 7.4 输出格式

```json
{
  "verdict": "PASS_FOR_LIVE",
  "paper_period": ["2026-09-01", "2026-09-14"],
  "checks": {
    "G-A_DURATION": {"passed": true,  "value": 14.0, "threshold": 14, "msg": "..."},
    "G-B_PNL":      {"passed": true,  "value": 412.5, "threshold": 0, "msg": "..."},
    "G-C_SHARPE":   {"passed": true,  "value": 1.34, "threshold": 1.0, "msg": "..."},
    "G-D_RISK":     {"passed": true,  "value": 0, "threshold": 0, "msg": "..."},
    "G-E_UPTIME":   {"passed": true,  "value": 0.9968, "threshold": 0.995, "msg": "..."},
    "G-F_OOS_DECAY":{"passed": true,  "value": 0.78, "threshold": 0.6, "msg": "..."}
  },
  "decision": "ALLOW_LIVE_TRADING",
  "next_steps": ["GM final approval", "switch --mode=live", "...]"
}
```

**联动:**
- verdict = PASS_FOR_LIVE → 自动通知 老雷 + 老韩 (Slack), 等 GM 最终拍板
- verdict = HOLD_OR_REWORK → 自动开 ticket 列失败检查项, 通知 owner

### 7.5 Gate 之后

PASS_FOR_LIVE 不代表立即 live, GM 还有最终一票. 流程:
1. 自动脚本出 PASS
2. 老雷 + 老韩 review 报告, 复核非自动化指标 (如 paper 期间是否有事故未自动捕获)
3. GM 批准 → 切 `--mode=live`, 起步用 PER_ORDER_CAP_SOFT 缩水 (比 paper 一半), 跑首周
4. 首周 live OK → 解锁 SOFT cap 到 paper 水平

---

## 8. 报告 (与 backtest 报告同模板)

### 8.1 模板复用

paper 报告**逐图逐表与 backtest 报告同**(姊妹文档 §7.1):

```
report/
├── 01_summary.png            # PnL curve, paper + baseline 双线对比
├── 02_sharpe.png             # rolling 7d Sharpe, paper + baseline
├── 03_drawdown.png
├── 04_hit_rate.png
├── 05_alpha_decay.png
├── 06_slippage_diag.png      # paper predicted vs actual (实时反馈, 与 backtest 不同)
├── 07_clv.png                # paper CLV (Pinnacle closing line 实时拉取)
├── 08_reject_breakdown.png
├── 09_uptime_chart.png       # paper 特有: 每日在线率
├── 10_risk_failure_log.txt   # paper 特有: §7.2 失效检测 trace
├── metrics.json              # 与 backtest 同 schema, 加 paper 特有字段
└── m4_5_gate_decision.json   # §7.4 输出
```

### 8.2 paper 特有 metric

```json
{
  ... (与 backtest metrics.json 完全一致的字段, 见姊妹文档附录 C) ...
  "paper_specific": {
    "uptime_seconds": 1203456,
    "total_seconds": 1209600,
    "uptime_ratio": 0.9949,
    "safe_mode_total_seconds": 600,
    "halted_total_seconds": 120,
    "warning_total_seconds": 1800,
    "risk_failures": [],
    "stale_events": [{"src": "wss", "duration_s": 8}, ...]
  },
  "vs_baseline": {
    "baseline_run_id": "baseline_p0_01_aug",
    "paper_sharpe":      1.34,
    "baseline_sharpe":   1.72,
    "decay_ratio":       0.78,
    "passes_threshold":  true
  }
}
```

### 8.3 对账 dashboard (与小郑联动)

实时 dashboard 显示 paper vs baseline:
- 当日 paper PnL vs baseline expected PnL (95% CI band)
- 偏离 > 2σ 持续 > 1h → yellow alert (小程信号 alpha decay?)
- 偏离 > 3σ 持续 > 1h → red alert (信号失效 / 数据源问题, 老雷电话)

dashboard panel 由小郑 (#42 observability) 出, 我提供 metric 定义.

---

## 9. 时间表

### 9.1 总体 (用户高优, M4.5 卡死)

| 月份 | 里程碑 |
|---|---|
| **W1 (5/28 - 6/4)** (现在) | 本文 RFC + backtest framework RFC, 等会签 |
| 6 月 (Sprint-1 末) | feature pipeline pyo3 + RM pyo3 + slippage model v1 各就位 |
| 7/2 | **backtest framework P0-01 第一份回测报告** (姊妹文档 §8.3) |
| 7 月 | paper trading engine 落代码 (PaperSigner + VirtualMatcher + PaperLedger) |
| 8 月 | paper engine 联调 + dry-run (1 周不计 gate, 验证基础设施) |
| **8/15 起** | **paper trading 正式 2 周连续运行** |
| **8/29** | **M4.5 gate 第一次判定** |
| 9 月起 | gate PASS → live; gate FAIL → 调整后再跑 2 周 |

### 9.2 关键里程碑 (M4.5)

| 时间 | 事件 | 负责 |
|---|---|---|
| 6/12 | Pinnacle 数据 + 信号 contract 锁版 | 老李 / 老彭 / 小程 |
| 6/19 | Polymarket / Goalserve Parquet schema 落地 | 小余 / 小段 |
| 6/26 | feature_pipeline + RiskGateway pyo3 binding | 老周 / 小田 / 老韩 |
| 6/26 | slippage model v1 + 单测 | 小肖 |
| 7/2 | backtest 第一份报告 (P0-01) | 我 |
| 7/15 | PaperSigner / VirtualMatcher skeleton | 我 + 小袁 |
| 7/31 | paper engine 联调通 + 小宋 chaos 验过 | 我 + 小宋 |
| 8/8 | dry-run 1 周 (不算 gate) | 我 |
| 8/15 | **paper trading 正式 D1** | 全队 |
| 8/29 | **paper D14, M4.5 gate 判定** | 我 + 老雷 + 老韩 |

### 9.3 风险路径 (gate 失败处置)

| 失败 | 处置 |
|---|---|
| FAIL_PNL (paper 亏钱) | 信号回炉, 小程 + 小梁 review; backtest 重跑; paper 再跑 2 周 |
| FAIL_SHARPE (波动大) | size 缩 (PER_ORDER_CAP_SOFT 砍半); 再跑 2 周 |
| FAIL_RISK (风控失效) | **P0**: 老韩 + 我 + 老沈 root cause; 全部 paper 报告作废, fix 后从 dry-run 1 周重启 |
| FAIL_UPTIME (在线率不够) | 老吴 / 小郑 / 我 review 故障, 修 infrastructure, 再跑 2 周 |
| FAIL_OOS_DECAY (paper << baseline) | 信号失效, 小程 / 小梁 review; 可能 P0-02 接力, P0-01 暂搁 |
| FAIL_DURATION (中途中断 > 0.5%) | 计天数清零, 重新跑满 14 天 |

---

## 10. 风险点 + 开放问题

### 10.1 已知风险

| # | 风险 | 缓解 |
|---|---|---|
| PR-1 (高) | VirtualMatcher 与真实 fill 偏差大 → paper Sharpe 与 live 实际差异大 | 与小袁 Wave 6 模型紧密 calibrate; 上线后影子模式 (paper + live 并跑) 比对 |
| PR-2 (高) | paper 数据源真实, 但市场冲击虚拟 (我们没真下单, 不会影响 book) | 这是 paper 天然不可避免限制. M4.5 gate 通过后 live 起步用 PER_ORDER_CAP_SOFT × 0.5 缩水首周, 检测 capacity drift |
| PR-3 (中) | settle 等真比赛 4h → paper 单笔 trade 周期长, 14 天可能不够 trade 数 | 14 天目标 trade 数 ≈ 40-60 (NBA + NFL), 接近统计显著边界. 必要时延长到 21 天 |
| PR-4 (中) | paper 跑期间生产数据源中断 → uptime 受真 infra 拖累 | M4.5 gate uptime 阈值 99.5% 已给 0.5% 余量; 真严重 → fix infra 再跑 |
| PR-5 (中) | PaperSigner 子进程 crash 导致 paper 失真 | 与 live signer 同款 SAFE_MODE 处理, audit 记录 |
| PR-6 (中) | paper 期间 alpha 真在 decay (Polymarket 市场进化) → 切实盘后立刻翻车 | 监控 paper vs backtest baseline 比值, ≤ 0.6 即拒. 切实盘后首周影子并跑双校验 |
| PR-7 (低) | virtual ledger WAL 损坏 → paper PnL 丢失 | fsync 同 nonce WAL 红线; replay 不变量检测 |
| PR-8 (低) | M4.5 gate 自动脚本逻辑 bug → 误判 PASS | 脚本 PR 必双人 review (我 + 老韩); 第一次判定前手工对账 1 次 |

### 10.2 开放问题

| # | 问题 | 找谁 | 截止 |
|---|---|---|---|
| PQ-1 | PaperSigner / RealSigner CMake 编译期分支 vs 启动期注入, 终选哪个 | @老沈 + @老周 | 6/12 |
| PQ-2 | VirtualMatcher 在 maker 路径上没小袁模型时的 placeholder 是否够 paper dry-run | @小袁 + 我 | 7/15 |
| PQ-3 | M4.5 G-F (paper vs baseline 比值) 阈值 0.6 是否合适, 或区分 NBA/NFL | @老雷 + @小梁 | 8/15 (M4.5 启动前) |
| PQ-4 | paper 期间是否允许人工干预 (e.g. 临时调 cap)? 默认我倾向"完全 hands-off, 干预 = 计天清零" | @老雷 | 7/15 |
| PQ-5 | 多策略 (P0-01 + P0-02 同时跑 paper) 还是单策略 paper | @小梁 + @老雷 | 7/31 |
| PQ-6 | virtual bankroll 初始值 ($20K? $50K?), 影响 trade 频率 + Sharpe 计算 | @老雷 + @小梁 | 7/15 |
| PQ-7 | paper 期间 PR 是否允许 deploy (代码变更影响行为) | @老吴 + @老韩 | 7/15 |
| PQ-8 | 影子模式 (paper + live 并跑) 是 M5 后的事, M4.5 阶段单 paper | @老雷 | 8/29 (M4.5 后) |
| PQ-9 | M4.5 多次 FAIL 后是否考虑废弃 P0-01 直接切 P0-02 | @老雷 + @小梁 | retry 第 3 次后 |

### 10.3 v0.1 不做, 留 v0.2+

- 影子模式 (paper + live 并跑) — M5+
- 多账户 paper (每个账户独立 ledger) — V2
- paper 反事实分析 ("如果 size 翻倍会怎样") — 用 backtest 而非 paper 做
- paper 跨 sport portfolio (NBA + NFL + soccer 同时跑) — 等 P0-01 单 sport 过 gate 再扩
- paper 期间在线 alpha 监控 dashboard 高级图 (与小郑迭代) — Sprint-3

---

## 附录 A — 共享 / 独有 模块清单 (PR-1 落地清晰版)

### A.1 三 mode 共享 (binary 一致)

```
src/
├── infra/                  # 全部
├── data/                   # 全部 (含 ingest / book / feature / heartbeat)
├── strategy/               # 全部 (含信号 / 定价 / 对冲)
├── risk/                   # 全部 (RiskManager + RiskGateway)
├── exec/
│   ├── state_machine/      # 全部
│   ├── nonce/              # 全部
│   ├── fill/               # 全部 (fill ingestion)
│   └── recon/              # 全部
```

### A.2 mode-specific (CMake 编译期分流)

```
src/exec/signer/
├── signer_main.cc          # 入口 (共享)
├── real_signer/            # only in stcpp_signer_live target
│   ├── eip712_signer.cc
│   └── chain_rpc_submit.cc
├── paper_signer/           # only in stcpp_signer_paper target
│   ├── paper_signer.cc
│   └── virtual_matcher.cc
└── backtest_signer/        # only in stcpp_trader_backtest target (Python in-process)
    └── backtest_signer.cc

src/data/ledger/
├── ledger_provider.h       # 抽象 (共享)
├── live_ledger.cc          # only live
└── paper_ledger.cc         # only paper (+ backtest 复用)
```

### A.3 CI 隔离校验

```bash
# 在 CI 中跑
nm stcpp_signer_live   | grep -E "paper_signer|virtual_matcher|backtest_signer" && exit 1
nm stcpp_signer_paper  | grep -E "real_signer|eip712|chain_rpc_submit"            && exit 1
nm stcpp_trader_backtest | grep -E "real_signer|paper_signer"                     && exit 1
```

任何符号泄漏 → CI 拒 merge.

---

## 附录 B — paper 启动命令

```bash
# paper 模式启动 (生产路径)
/usr/local/bin/stcpp-trader \
  --mode=paper \
  --rm-config=/etc/stcpp/paper_config.toml \
  --signer-binary=/usr/local/bin/stcpp_signer_paper \
  --paper-ledger-wal=/var/lib/stcpp/paper_ledger.wal \
  --audit-wal=/var/log/stcpp/audit.wal \
  --metrics-port=9090

# 同款 systemd unit, mode 字段差异
[Unit]
Description=stcpp paper trader
After=network.target

[Service]
ExecStart=/usr/local/bin/stcpp-trader --mode=paper ...
Restart=on-failure
RestartSec=5s
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

---

## 附录 C — paper 与 live 启动 flag diff (人审版)

```diff
# /etc/stcpp/stcpp-trader.service
- ExecStart=/usr/local/bin/stcpp-trader --mode=live   --rm-config=/etc/stcpp/prod_config.toml   --signer-binary=/usr/local/bin/stcpp_signer_live
+ ExecStart=/usr/local/bin/stcpp-trader --mode=paper  --rm-config=/etc/stcpp/paper_config.toml  --signer-binary=/usr/local/bin/stcpp_signer_paper

# /etc/stcpp/paper_config.toml vs /etc/stcpp/prod_config.toml
  PER_ORDER_CAP_HARD_USDC      = 5000      (一致)
  PER_ORDER_CAP_SOFT_USDC      = 2000      (一致)
  KELLY_FRACTION               = 0.25      (一致)
  STALE_*                                  (一致)
- mode = "live"
+ mode = "paper"
- [rm.ledger]
- provider = "live_chain_recon"
+ [rm.ledger]
+ provider = "paper_virtual"
+ [rm.bankroll]
+ virtual_initial_usdc = 20000
```

仅 3 行配置不同, 0 行代码不同 (因为是 CMake target 隔离, 不是同一 binary 内 if-else).

---

**END v0.1.** 等 PaperSigner/VirtualMatcher 设计与小袁 + 老沈会签, bump v0.2.

— 小蒋 (quant-backtest), 2026-05-28
