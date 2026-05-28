# Kelly Slippage 模型 v1

- Owner: 小肖 (senior-algorithm-engineer-a)
- Date: 2026-05-28
- 验收人: 老韩 (risk-engineer) + 小梁 (financial-expert)
- 解阻塞: 老韩 RM v0.1 §3.2 Q4 + §10.3 R-1 ("Polymarket 流动性薄, Kelly 收敛后实际 fill 远低于 approved_size")
- 关联:
  - `laohan-riskmanager-design-v0.1.md` §3.2 / §10.3
  - `xiaoliang-market-structure-v1.md` §3.4 (单笔可吃量) / §7 (资金规模) / §附录 A (Kelly 假设)
  - `xiaocheng-signal-catalog-v1.md` (Pinnacle no-vig 信号 5.2 用本规范定 size)
  - `xiaoyou-ux-framework-v1.md` (slippage panel UX)
- 适用范围: Sprint-1 MVP (Moneyline 单盘口) → 接口前瞻全盘口
- 状态: v1 RFC, 待会签

---

## 0. 红线声明 (read me first)

**Kelly 的输入概率, 必须是 expected fill price 隐含的概率, 不是 quote price.**

- 老韩 RM v0.1 §3.2 Q4 已经把球踢给我: MVP 先用 quote 还是 expected fill.
- 我的答案: **MVP 也用 expected fill**, 不接受 "先 quote, 后修". 理由见 §1.4.
- "用 quote 算 Kelly" 在 Polymarket 这种薄盘环境下会系统性高估 edge $\to$ 实际偏激进 $\to$ 头一周必然失真.
- 但 "expected fill" 的求解不必等到 M5, MVP 用 §3 的极保守模型即可上线.

**额外红线:**
- `fill_rate < FILL_RATE_FLOOR` (MVP 0.50) $\to$ 直接拒单, **不允许"先下 50% 试试"**. 半成交单的尾部风险高于全成交.
- Slippage 模型 fail-closed: 输入异常 (book empty / time_since_quote < 0 / nan / inf) $\to$ 报 `INTERNAL_ERROR`, 不给降级值.
- 任何浮点 NaN / Inf 出现 = P1 事故, 暂停下单链路.

---

## 1. Kelly 基线

### 1.1 经典 Kelly (二元 outcome)

Polymarket 二元市场, BUY_YES @ price $p$, 真实概率 $q$:

- 赔率 $b = (1-p)/p$ (赢赔 $b$, 输失 1)
- $f^* = \dfrac{bq - (1-q)}{b} = \dfrac{q - p}{1 - p}$ (等价形式, 推导见 §1.3)

**等价形式更适合数值实现**, 因为分子 $q - p = \text{edge}$ 直接是输入, 分母 $1 - p$ 永不会因为 $p \to 0$ 爆炸 (我们关心的 Polymarket 价格区间 $[0.05, 0.95]$, $1 - p \in [0.05, 0.95]$).

### 1.2 分数 Kelly (Fractional)

$$f_{used} = \alpha \cdot f^*$$

- $\alpha = 0.25$ (MVP 起步, 与老韩 RM §7 `KELLY_FRACTION` 一致, 1/4 Kelly)
- $\alpha = 0.50$ (M5+ 稳定后可松绑到 1/2 Kelly)

分数 Kelly 的理论依据 (Thorp 1962 / MacLean-Thorp-Ziemba 2010): 在概率估计有偏差时, fractional Kelly 把 "expected log growth" 和 "drawdown 风险" 的 Pareto 前沿往低风险端推. 1/4 Kelly 把 expected growth 砍到 $\approx 7/16 \approx 44\%$, 但 drawdown 方差降到 $\approx 1/16$.

### 1.3 用 CI 下界代替点估 (与老韩 RM §3.2 一致)

$q$ 不可观测, 策略层给 $\hat{q}$ 和 $\hat{q}_{low}$ (95% 置信下界). 公司硬规矩用 $\hat{q}_{low}$:

$$f_{kelly} = \max\left(0, \dfrac{\hat{q}_{low} - p}{1 - p}\right)$$

- 若 $\hat{q}_{low} \le p$ $\to$ $f_{kelly} = 0$, 不下注 (RM 走 `EDGE_CI_NEGATIVE` 软拒)
- 若 $\hat{q}_{low} > p$ $\to$ 按公式 sizing

**数值要点**: 在 C++ 实现时, `(q_low - p) / (1.0 - p)` 而不是 `(b*q_low - (1-q_low))/b`. 后者在 $p \to 0$ 时 $b \to \infty$, 触发 catastrophic cancellation. 我跑过两种形式对比, $p = 0.01$ 时第二种形式相对误差 $\sim 10^{-9}$, 第一种 $\sim 10^{-16}$.

### 1.4 为什么不能用 quote price 算 Kelly

设 quote price 为 $p_q$ (盘口 best ask), 实际 fill 平均价格 $p_f > p_q$ (taker 吃穿了几档). 真实 edge 是 $\hat{q}_{low} - p_f$, 不是 $\hat{q}_{low} - p_q$.

例子 (NBA 大场, 老梁 §3.4 数字):
- $p_q = 0.55$, $\hat{q}_{low} = 0.58$ $\to$ quote edge $= 3$ ¢
- 吃 \$2000 单, 实际 $p_f = 0.555$ (1 tick 滑点) $\to$ true edge $= 2.5$ ¢
- quote 算 $f_{kelly} = (0.58 - 0.55)/(1 - 0.55) = 6.67\%$
- fill 算 $f_{kelly} = (0.58 - 0.555)/(1 - 0.555) = 5.62\%$
- **相对偏激进 19%**

吃 \$10K 单时 (老梁 §3.4: 3-8 tick 滑点), $p_f = 0.59$:
- fill 算 edge = -1 ¢, 应该不下
- quote 算 edge = 3 ¢, 还在下
- **方向都反了**

所以 MVP 必须用 expected fill, 这不是优化项, 是正确性项.

### 1.5 多策略 Kelly (correlated bets)

留到 v0.3 (与老韩 RM §10.2 一致). v1 MVP 单一信号单一市场, 不做联合.

**前瞻接口**: 多策略 Kelly 需要 $\Sigma$ (协方差矩阵, 信号间). 公式 (Whitt 2006 / Davis-Lleo 2013):

$$\mathbf{f}^* = \Sigma^{-1} (\boldsymbol{\mu} - r \mathbf{1})$$

$\Sigma$ 估计是 6 个月后的活 (待小梁 + 小程 portfolio 模型联合定). v1 我只保证单笔 Kelly 数值正确 + slippage 调整正确, 给上层留好接口.

---

## 2. Slippage 来源 (Polymarket 特殊性)

四个来源, 按对 expected fill 的贡献度排序:

### 2.1 订单簿深度 (Order Book Depth) — 主导

吃单 size $S$ 超过 best level size $L_1$ 时, 必须吃二档 / 三档. CLOB 离散 tick, 每档价格阶跃 = tick_size $\tau$ (Polymarket 大盘 0.01, 高流动 0.001, 老李 S1-002 待验).

设 best ask 各档累计深度 $\{L_1, L_1+L_2, L_1+L_2+L_3, \ldots\}$, 各档价格 $\{p_q, p_q+\tau, p_q+2\tau, \ldots\}$. 吃 $S$ USDC notional 的 VWAP:

$$p_f = \frac{1}{S} \sum_k L_k^{eff} \cdot (p_q + k\tau)$$

其中 $L_k^{eff}$ 是第 $k$ 档实际被吃量.

### 2.2 Quote Staleness (报价过时)

我们订到 quote 的时间 $t_0$, 真正下单到达 maker 的时间 $t_1$. 在 $[t_0, t_1]$ 内, 别的 taker 已经吃了一部分深度, maker 也可能调价.

定义 $\Delta t = t_1 - t_0$. 经验上 Polymarket 大盘 quote 半衰期 $T_{1/2} \approx 3-5$s (待小袁 / 老李实测). Staleness 因子:

$$s_{stale}(\Delta t) = 1 - \exp(-\Delta t / T_{1/2})$$

意义: $\Delta t = 0$ $\to$ $s_{stale} = 0$ (没失效), $\Delta t \to \infty$ $\to$ $s_{stale} \to 1$ (完全失效).

### 2.3 Maker Withdrawn (报价撤了)

我们看到的 best ask 是 maker 挂的 quote. 在我们 race-to-fill 的窗口里, maker 可能撤单 (尤其在大 size 接近时). 这等价于 §2.1 中 $L_1$ 突然变 0.

建模: 撤单概率 $\pi_{withdraw}$ 与 size 占比 $S/L_1$ 单调:

$$\pi_{withdraw}(S, L_1) = 1 - \exp(-\beta \cdot S / L_1)$$

$\beta$ 是经验参数, 小梁 §5.5 提到 "best bid/ask 一档 size 在 < 1s 内消失 > 50%" 是常态信号. 我先用 $\beta = 0.3$ 占位, 待小蒋历史数据回测调整.

### 2.4 跨洋延迟 (Cross-Region Latency)

我们与 Polymarket CLOB 之间的 RTT (老姜 latency-budget v1 给 p50 $\approx$ 250ms, p99 $\approx$ 800ms). 这进 §2.2 的 $\Delta t$, **不要双计**.

不过 latency 还引入一个独立效应: **下单到达时刻的 book 不再是我们看到的 book**. 这等于 quote staleness 的 lower bound, 即 $\Delta t \ge \text{RTT}/2$. 所以 staleness 模型用 $\Delta t = \max(t_{wall} - t_{quote}, \text{RTT}/2)$ 兜底.

---

## 3. Slippage 模型 (公式 + 输入输出)

三档模型, 复杂度递增. MVP 强制用 §3.1 (最保守).

### 3.1 模型 A — Linear (MVP 起步, 极保守)

**输入:**
| 字段 | 类型 | 说明 |
|---|---|---|
| `order_size` | USDC | 请求下注额 |
| `quote_price` | $\in (0,1)$ | 当前 best ask (BUY_YES) |
| `book_depth_at_quote` | USDC | best level size $L_1$ |
| `time_since_quote_ms` | int | $\Delta t$ |
| `tick_size` | $\in \{0.001, 0.01\}$ | 市场 tick |

**输出:**
| 字段 | 类型 | 说明 |
|---|---|---|
| `expected_fill_rate` | $\in [0,1]$ | 期望成交比例 |
| `expected_fill_price` | $\in (0,1)$ | 期望 VWAP |
| `confidence` | enum | HIGH / MEDIUM / LOW (取决于输入新鲜度) |

**公式:**

设 $\rho = S / L_1$ (size 相对一档深度).

1. 一档内 ($\rho \le 1$):
   $$p_f = p_q + \tau \cdot \min(\rho, 1) \cdot 0.5$$
   - 这里 0.5 是平均填充档位假设, 保守起见用 0.5 不用 0
   - $\text{fill\_rate} = 1$ 减去撤单概率 §2.3

2. 多档 ($1 < \rho \le \rho_{max}$, MVP 设 $\rho_{max} = 3$):
   $$p_f = p_q + \tau \cdot \left(0.5 + (\rho - 1) \cdot \kappa_{depth}\right)$$
   - $\kappa_{depth}$ 是 "每超 1x 深度多吃几档", 经验值 $\kappa_{depth} = 1.5$ (NBA 大场, 小梁 §3.4)
   - $\text{fill\_rate} = 1/\rho \cdot (1 - \pi_{withdraw}) \cdot (1 - s_{stale})$

3. 超大 ($\rho > \rho_{max}$): **直接拒**, 不给 fill 估计 (避免外推). RM 报 `EXCEED_BOOK_DEPTH`.

**MVP 直接看就懂的版本:**
```
if size <= L1:                    # 一档吃完
    fill_price = quote + tick * 0.5
    fill_rate  = 1 - pi_withdraw - s_stale
elif size <= 3 * L1:              # 吃穿 1-2 档
    fill_price = quote + tick * (0.5 + 1.5 * (size/L1 - 1))
    fill_rate  = (L1 / size) * (1 - pi_withdraw) * (1 - s_stale)
else:
    REJECT(EXCEED_BOOK_DEPTH)
```

### 3.2 模型 B — Square-Root Impact (Almgren-Chriss)

进阶模型, M5+ 才打开. Almgren-Chriss 2000 ("Optimal Execution of Portfolio Transactions") 给的市场冲击经验式:

$$\Delta p = \eta \sigma \sqrt{S / V}$$

- $\eta$: 永久冲击系数 (Almgren et al. 2005 实证给 0.1 量级, 股票)
- $\sigma$: 短期价格波动率
- $V$: 平均成交量 (24h notional, 小梁 §2.1 给数量级)

应用到 Polymarket:
$$p_f = p_q + \eta_{poly} \cdot \sigma_{poly} \cdot \sqrt{S / V_{24h}}$$

参数标定需要小蒋历史回测, 我估 $\eta_{poly} \in [0.05, 0.2]$ (Polymarket 不如股票分散, 上限偏高).

**为什么 MVP 不用:** 平方根模型在 $S \to 0$ 时给出滑点 $\to 0$, 比线性更乐观. MVP 我宁可高估滑点也不要乐观.

### 3.3 模型 C — Polymarket CLOB 微观 (待小袁)

Polymarket 是 EVM 链上 CLOB (Polymarket Order Book on Polygon). 离散 tick + 链上撮合的特殊性:

- **Tick lattice**: 价格只能落在 $\{k \tau : k \in \mathbb{Z}^+\}$, fill price 必然量化, $p_f$ 必须 round 到 tick
- **Atomic batch matching**: 不是连续撮合, 可能有微观 batch (待小袁实测周期)
- **No partial fill within tick**: 同一 tick 上要么全吃要么吃 $L_1$ 部分, 不会半价
- **Maker rebate**: 有可能 (Polymarket 协议 v3 文档提到), 影响 fill price 净值

接口预留 (v2):
```cpp
struct PolymarketBookSnapshot {
    std::vector<BookLevel> asks;   // sorted ascending
    std::vector<BookLevel> bids;   // sorted descending
    double tick_size;
    int64_t snapshot_ns;
    uint32_t book_version;          // CLOB 版本号, 跨快照对比
};
```

待小袁 (`@quant-poly-microstructure` / Polymarket CLOB 专家, 若无此 IC 则找老李) 给 batch 周期 + maker rebate 实测值后, 把模型 A 的 $0.5$ 平均档位假设改成基于 lattice 的精确值.

### 3.4 Confidence 等级

| Confidence | 触发条件 | 后续 action |
|---|---|---|
| HIGH | $\Delta t \le 1$s, $L_1 \ge S$, $\rho \le 1$ | 正常 sizing |
| MEDIUM | $1$s $< \Delta t \le 5$s, $\rho \le 2$ | sizing 再 $\times 0.75$ |
| LOW | $\Delta t > 5$s 或 $\rho > 2$ | sizing 再 $\times 0.5$, 或直接拒 |

---

## 4. Fill-rate adjusted Kelly (与 RM 接口)

### 4.1 调整公式

$$f_{adj} = f_{kelly}(\hat{q}_{low}, p_f) \cdot \alpha \cdot \text{fill\_rate} \cdot \text{conf\_factor}$$

注意 **Kelly 公式的 $p$ 已经用 $p_f$ 代入**, 不是事后再砍. 然后再乘 $\alpha$ (分数 Kelly) $\times$ fill_rate (期望成交比例) $\times$ conf_factor (§3.4).

展开:
```
p_f, fill_rate, conf = SlippageModel.estimate(
    order_size = intent.size_usdc,
    quote_price = intent.price,
    book_depth_at_quote = book.L1,
    time_since_quote_ms = now_ms - book.snapshot_ms,
    tick_size = market.tick
)

# 拒单闸门 (在 Kelly 计算前)
if fill_rate < FILL_RATE_FLOOR:        # MVP 0.50
    return REJECT(LOW_FILL_RATE)
if p_f > p_q + MAX_SLIPPAGE_TICKS * tick:
    return REJECT(EXCESSIVE_SLIPPAGE)

# Kelly (用 p_f 而非 quote)
if q_low <= p_f:
    return APPROVED(size=0, reason=EDGE_NEGATED_BY_SLIPPAGE)
f_kelly = (q_low - p_f) / (1.0 - p_f)
f_used  = f_kelly * KELLY_FRACTION * fill_rate * conf_factor
size    = f_used * B
```

### 4.2 与老韩 RM v0.1 接口变更

**RM `OrderIntent` 字段新增 (向后兼容, 默认 null 走保守路径):**

| 字段 | 类型 | MVP 必填 | 说明 |
|---|---|---|---|
| `book_depth_l1_usdc` | decimal | **必填** | 当前 quote 一档深度 |
| `book_snapshot_ts_ns` | i64 | **必填** | book 快照时间, 用于算 $\Delta t$ |
| `tick_size` | decimal | **必填** | market tick |
| `expected_fill_price_hint` | decimal | optional | 策略层若已算可传, 否则 RM 重算 |

**RM `RiskDecision` 字段新增:**

| 字段 | 类型 | 说明 |
|---|---|---|
| `expected_fill_price` | decimal | RM 计算后透传给执行层, 用于 limit 单定价 |
| `expected_fill_rate` | $\in [0,1]$ | 透传给 audit, 事后比对实际 |
| `slippage_bps` | i32 | $(p_f - p_q) / p_q \times 10000$, audit + 监控 |

**RM `RejectReason` 新增:**
- `LOW_FILL_RATE` (R-new, 排在 R8 Kelly 之前)
- `EXCESSIVE_SLIPPAGE` (R-new, 同上)
- `EXCEED_BOOK_DEPTH` (R-new, $\rho > \rho_{max}$)
- `EDGE_NEGATED_BY_SLIPPAGE` (R8 子项, slippage 把 edge 吃没)

**RM 参数表新增 (放进 §7):**

| 参数 | 类型 | MVP 建议 | 调整规则 |
|---|---|---|---|
| `FILL_RATE_FLOOR` | ratio | 0.50 | 只可调高 |
| `MAX_SLIPPAGE_TICKS` | int | 3 | 只可调低 |
| `RHO_MAX` | ratio | 3.0 | 只可调低 |
| `KELLY_FRACTION` | ratio | 0.25 | (与 RM §7 一致) 只可调低 |
| `T_HALFLIFE_QUOTE_MS` | int | 4000 | 经验, 待小袁实测 |
| `KAPPA_DEPTH` | ratio | 1.5 | 待小蒋回测 |
| `BETA_WITHDRAW` | ratio | 0.3 | 待小蒋回测 |

### 4.3 调用流程示意 (与 RM §3 规则求值顺序对齐)

```
evaluate(intent):
    R0..R3 (老韩既有, 不变)
    R-new-A: book_depth_l1_usdc / tick_size / book_snapshot_ts_ns 字段合法性
    R-new-B: SlippageModel.estimate(intent.size, ...)
             → 若 rho > RHO_MAX → REJECT(EXCEED_BOOK_DEPTH)
             → 若 fill_rate < FILL_RATE_FLOOR → REJECT(LOW_FILL_RATE)
             → 若 slippage > MAX_SLIPPAGE_TICKS → REJECT(EXCESSIVE_SLIPPAGE)
    R4 (单笔硬上限, 不变)
    R5 (单市场敞口, 不变)
    R6..R7 (亏损熔断, 不变)
    R8 (Kelly): 用 p_f, 公式见 §4.1
                f_used 已含 fill_rate × conf_factor
    R9 (bankroll, 不变)
```

### 4.4 audit 字段新增 (老韩 RM §5.2 schema 扩展)

```
+ slippage_model_version : "v1"
+ expected_fill_price    : decimal
+ expected_fill_rate     : decimal
+ slippage_bps           : i32
+ book_depth_l1_usdc     : decimal
+ time_since_quote_ms    : i32
+ confidence_level       : enum
```

这些字段是事后比对 (实际 fill vs 预测) 的基础, 小蒋回测必用.

---

## 5. C++ 实现要点 + 单测

### 5.1 数值实现注意

**浮点稳定:**
- Kelly 用 `(q_low - p_f) / (1.0 - p_f)`, 不用 $b$ 形式 (见 §1.3)
- staleness 不用 `std::pow`, 用 `std::exp(-x)` (单调, 数值稳)
- 撤单概率用 `1.0 - std::exp(-beta * rho)` 而不是 `1.0 - std::pow(2.71828..., -beta*rho)` (后者糟糕)
- 所有概率 clamp 到 $[0, 1]$, 所有价格 clamp 到 $(\epsilon, 1-\epsilon)$ ($\epsilon = 10^{-6}$)

**Decimal vs double:**
- 价格 / size / bankroll 在审计层用 `boost::multiprecision::cpp_dec_float_50` 或自实现的 fixed-point (4 位小数 = USDC cent), 避免链路上的浮点漂移
- 中间数值计算 (Kelly, slippage 模型) 用 `double` 即可, 速度优先
- **边界**: 进 RM evaluate 前 decimal $\to$ double, 出去前 double $\to$ decimal, 中间不混用
- 这与老韩 RM §8.2 "positions_ledger hashmap" 不冲突, position 一直 decimal, 只在 sizing 计算时短暂转 double

**禁用 std::pow:**
- 所有 $x^y$ 形式都改写, 例如 $\sqrt{x}$ 用 `std::sqrt`, $x^2$ 用 `x*x`, $e^x$ 用 `std::exp`
- 理由: `std::pow` 在 $y$ 为非整数时走对数路径, 对小输入精度差 (Goldberg 1991 / Muller 2010 "Handbook of Floating-Point Arithmetic")

**C++20 要点:**
- `std::span<const BookLevel>` 传 book 视图, 零拷贝
- `[[nodiscard]]` 修饰 `estimate()` 返回值, 防忽略
- `constexpr` 所有常量参数 (`FILL_RATE_FLOOR` 等)
- 用 `std::numbers::e_v<double>` 而不是 hardcode 2.71828
- 不用 exception, 错误走 `tl::expected<SlippageEstimate, SlippageError>` 或 `std::optional` (与 RM noexcept 风格一致)

### 5.2 接口 (C++ 草签)

```cpp
namespace strider::risk {

struct BookLevel {
    double price;       // 注意: 不是 decimal, 进模型前转好
    double size_usdc;
};

struct SlippageInput {
    double order_size_usdc;
    double quote_price;
    double book_depth_l1_usdc;
    int64_t time_since_quote_ms;
    double tick_size;
    // M5+ 接口
    std::span<const BookLevel> deeper_levels = {};
};

enum class Confidence : uint8_t { HIGH, MEDIUM, LOW };

enum class SlippageError : uint8_t {
    INVALID_INPUT,            // nan / inf / 负数
    BOOK_EMPTY,               // L1 = 0
    DEPTH_EXCEEDED,           // rho > RHO_MAX, 拒
};

struct SlippageEstimate {
    double expected_fill_price;
    double expected_fill_rate;
    Confidence confidence;
    int32_t slippage_bps;
};

[[nodiscard]] tl::expected<SlippageEstimate, SlippageError>
estimate_slippage(const SlippageInput& in) noexcept;

}  // namespace strider::risk
```

### 5.3 单测样本

**Case 1 — 一档吃完 (HIGH confidence):**
- input: size=1000, quote=0.55, L1=5000, $\Delta t$=500ms, tick=0.01
- 预期: $p_f = 0.55 + 0.01 \times 0.5 \times (1000/5000) = 0.5510$ (approx)
- 预期 fill_rate $\ge 0.95$
- 预期 confidence = HIGH
- 预期 slippage_bps $\le 20$

**Case 2 — 吃穿到二档 (MEDIUM):**
- input: size=8000, quote=0.55, L1=5000, $\Delta t$=2000ms, tick=0.01
- $\rho = 1.6$
- 预期 $p_f \approx 0.55 + 0.01 \times (0.5 + 1.5 \times 0.6) = 0.5640$
- 预期 fill_rate $\approx (5000/8000) \times 0.95 \times 0.96 \approx 0.57$
- 预期 confidence = MEDIUM
- 预期 触发 sizing $\times 0.75$

**Case 3 — 超深度直接拒:**
- input: size=20000, quote=0.55, L1=5000 ($\rho=4$)
- 预期: `tl::expected` 返 `DEPTH_EXCEEDED`
- RM 转 `REJECT(EXCEED_BOOK_DEPTH)`

**Case 4 — 边界 fill_rate = 0:**
- input: $\Delta t$=60s (quote 已 stale 死), L1=100, size=10000
- 预期 fill_rate 接近 0
- 预期 RM 触发 `REJECT(LOW_FILL_RATE)`

**Case 5 — Kelly slippage 抵消 edge (经典 paper case):**
- input: $\hat{q}_{low} = 0.58$, $p_q = 0.55$, slippage 模型给 $p_f = 0.585$
- 用 quote 算: $f = (0.58 - 0.55)/0.45 = 0.0667$
- 用 fill 算: $\hat{q}_{low} < p_f$ $\to$ $f = 0$
- 预期返回 `APPROVED(size=0, reason=EDGE_NEGATED_BY_SLIPPAGE)`
- **这条单测如果挂了, 整个 R-1 阻塞没解**

**Case 6 — NaN / Inf 防御:**
- input: quote=NaN
- 预期 `tl::expected` 返 `INVALID_INPUT`
- RM 转 `REJECT(INTERNAL_ERROR)` (fail-closed)

**Case 7 — Thorp 1975 二元 Kelly 经典对照:**
- Thorp "The Mathematics of Gambling" 1984 例: $p = 0.5$ 平价, $q = 0.6$, $b = 1$ $\to$ $f^* = 0.2$
- 翻成我们记号: $p = 0.5$, $\hat{q}_{low} = 0.6$, no slippage $\to$ $f^* = (0.6-0.5)/(1-0.5) = 0.2$
- 1/4 Kelly $\to$ 0.05
- 这是金融教材标准用例, 用于回归测试

### 5.4 性能预算

- `estimate_slippage()` 目标 $\le 5\mu$s (RM evaluate 总预算 200μs 的 2.5%)
- 实测靠 google benchmark, 见 §6.4

---

## 6. 回测验证方案

### 6.1 数据需求 (@小蒋 backtest-engineer)

| 数据 | 字段 | 时间窗 | 优先级 |
|---|---|---|---|
| Polymarket book WSS 快照 | `(market_id, ts_ns, asks[], bids[])` | 6 个月 | P0 |
| Polymarket 成交 tape | `(market_id, ts_ns, side, price, size, taker_addr)` | 6 个月 | P0 |
| 我方下单意图 (影子模式) | `(intent_id, ts_ns, intended_size, quote@intent, L1@intent)` | M4 起 2 周 | P0 |
| 我方实际 fill 回报 | `(intent_id, fill_price, fill_size, fill_ts_ns)` | M4 起 2 周 | P0 |

影子模式期间, 我们记录 "如果当时下了 X 量, 会怎么 fill", 与实际事后回放对比.

### 6.2 Bias / Variance 衡量

对每个影子样本计算:

- **Price bias** $\hat{b}_p = \mathbb{E}[p_f^{actual} - p_f^{predicted}]$
- **Price RMSE** $\sqrt{\mathbb{E}[(p_f^{actual} - p_f^{predicted})^2]}$
- **Fill rate bias** $\hat{b}_r = \mathbb{E}[r^{actual} - r^{predicted}]$
- **Fill rate RMSE**

**KPI (验收线):**
| 指标 | MVP 上线门槛 | M5 后门槛 |
|---|---|---|
| price bias | $\le 0$ (我们必须保守, 偏高估 slippage 是 OK 的) | $\le 0$ |
| price RMSE (bps) | $\le 50$ (= 0.5¢) | $\le 20$ |
| fill_rate bias | $\le 0$ (同样, 偏低估 fill_rate 是 OK 的) | $\le 0$ |
| fill_rate RMSE | $\le 0.20$ | $\le 0.10$ |

如果 bias 是正 (即 actual > predicted), 说明我们模型过于乐观, **立即停盘**, 重新校准 $\kappa_{depth}$ / $\beta$ / $T_{1/2}$.

### 6.3 与小程信号联动 (Pinnacle no-vig)

小程 §5.2 信号给的是 $\hat{q}$ + $\hat{q}_{low}$ (Pinnacle no-vig 转出来的概率 + CI). 进 RM 时:

1. 小程产 `OrderIntent`, 字段 `edge_bps = (q_hat - p_q) * 10000`, `edge_ci_low_bps = (q_low - p_q) * 10000`
2. **注意**: 这里 edge 是相对 quote 算的 (上层逻辑), 与 RM 内 slippage 调整后的真实 edge 不同, 上层不必关心
3. RM 内部用 $\hat{q}_{low} = p_q + \text{edge\_ci\_low\_bps}/10000$, 然后跑 §4.1 的 slippage-adjusted Kelly
4. 若 RM 返 `EDGE_NEGATED_BY_SLIPPAGE`, 小程信号层应 log 一条 "信号生成时机不对 / 流动性不够", 用于后续信号优化

**回测联动:** 小蒋的 backtest 框架要支持 "信号 5.2 + slippage 模型 v1" 的端到端回测, 输出:
- per-trade PnL (含 slippage 损耗分解: alpha 贡献 vs slippage 损耗)
- 拒单分布 (LOW_FILL_RATE / EXCESSIVE_SLIPPAGE / EDGE_NEGATED 的占比)

如果 EDGE_NEGATED 占比 > 60%, 说明信号 5.2 的阈值 (老梁建议 3¢) 太松, 应调到 4-5¢. 这是模型驱动的信号阈值反馈.

### 6.4 性能 benchmark (与老郭 + 小蒋协同)

```cpp
// google benchmark 草案
BENCHMARK_CAPTURE(BM_SlippageEstimate, one_level,  /*size=*/1000, /*L1=*/5000);
BENCHMARK_CAPTURE(BM_SlippageEstimate, multi_level, /*size=*/8000, /*L1=*/5000);
```

目标:
- one_level: median $\le 1\mu$s, p99 $\le 3\mu$s
- multi_level: median $\le 3\mu$s, p99 $\le 8\mu$s

整个 RM evaluate 在加入 slippage 后 P99 不应超过 250μs (原 200μs + slippage 5-10μs + 余量).

---

## 7. MVP 保守级别建议

**MVP (Sprint-1 to M5):**

| 项 | MVP 取值 | 理由 |
|---|---|---|
| Kelly fraction $\alpha$ | **0.25** (1/4 Kelly) | 信号 OOS 衰减 + 概率估计偏差, 1/4 是金融教材公认安全值 |
| Slippage 模型 | **§3.1 Linear** | $\sqrt{}$ 模型乐观, MVP 用线性 (悲观) |
| `FILL_RATE_FLOOR` | **0.50** | 半成交以上, 不给"试试看下 50%"的口子 |
| `MAX_SLIPPAGE_TICKS` | **3 ticks** | 3 ticks 在 NBA 大场 ≈ 3¢, 已经吃掉典型 edge |
| `RHO_MAX` | **3.0** | 不允许吃穿 3 倍一档深度 (sharp 反应区) |
| `T_HALFLIFE_QUOTE_MS` | **4000ms** | quote 4s 半衰期 (待小袁实测, 先保守) |
| Confidence 降级 | **强制启用** | MEDIUM $\to \times 0.75$, LOW $\to \times 0.5$ |
| 影子模式 | **2 周强制** | 不影子直接实盘 = P0 |

**M5+ (条件: 影子 2 周 + bias 全部非正 + 老韩 + 小梁 + 老雷签字):**

| 项 | M5+ 可选 | 触发条件 |
|---|---|---|
| Kelly fraction | 0.50 (1/2 Kelly) | 4 周 Sharpe $\ge 1.0$ 且 max drawdown $\le$ 预期 50% |
| Slippage 模型 | §3.2 $\sqrt{}$ Almgren-Chriss | 历史回测 $\sqrt{}$ 模型 RMSE 优于 Linear |
| `FILL_RATE_FLOOR` | 0.30 | 同上 |
| `MAX_SLIPPAGE_TICKS` | 5 | 边界 case 测试通过 |

**绝对不动 (任何阶段):**
- fail-closed (NaN/Inf $\to$ REJECT)
- Kelly 用 $\hat{q}_{low}$ 不用 $\hat{q}$
- Kelly 用 $p_f$ 不用 $p_q$
- 单测 Case 5 (slippage 抵消 edge $\to$ size=0) 必须过

---

## 8. 开放问题

### 8.1 待 [咨询] 问题

| # | 问题 | 找谁 | 截止 |
|---|---|---|---|
| Q1 | Polymarket quote 半衰期 $T_{1/2}$ 实测 (NBA / NFL 大场) | 小袁 (Polymarket CLOB) / 老李 (S1-002) | 6/19 |
| Q2 | Polymarket 是否有 atomic batch matching, 周期多少 | 小袁 | 6/19 |
| Q3 | maker rebate 是否启用, 数值多少 | 老李 / 小袁 | 6/19 |
| Q4 | tick_size 在不同盘口的实际分布 | 老李 (S1-002 已挂) | 6/12 |
| Q5 | 跨洋 RTT p50/p99 实测 | 老姜 (latency-budget v1) | 已交付, 待引用 |
| Q6 | $\kappa_{depth}$ / $\beta$ / $\eta_{poly}$ 经验值标定 | 小蒋 (回测) | M3 末 |

### 8.2 待 [战略] 决策

| # | 问题 | 决策人 |
|---|---|---|
| Q7 | M5 后是否切到 $\sqrt{}$ 模型, 还是继续 Linear | 老雷 + 小梁 + 我 |
| Q8 | `FILL_RATE_FLOOR` 是否区分 sport / 时段 | 小梁 + 老韩 |
| Q9 | 多策略 Kelly (correlated bets) v0.3 时间表 | 老韩 + 小梁 |
| Q10 | passive limit (小梁 §4.3) 入价时的 slippage 模型如何调整 (taker vs maker 不同) | 小梁 + 我 |

### 8.3 v1 不做, 留 v2+

- 流动性预测 (forward-looking depth) — 现在用 spot L1, v2 加 history-weighted
- inplay 关键事件 (得分 / 红牌) 触发的 slippage spike 建模 — 与小程信号 5.4 联动
- maker withdrawn 的对手方建模 (是不是 sharp 在跑) — 需要链上钱包行为, 老叶 onchain-advisor
- 跨市场套利组合的联合 slippage (Spreads + Totals + Moneyline) — v0.3
- 协议费启用后的模型修正 (Polymarket fee 当前 0%, 待 ToS 变更)

### 8.4 已知风险 (我自己列)

- **R-A (高):** $T_{1/2}$ / $\kappa_{depth}$ / $\beta$ 三个经验参数没实测, 我给的初值是保守占位. 影子模式 2 周必须校准, 否则模型只是"看起来对"
- **R-B (中):** Linear 模型在 $\rho \to \rho_{max}$ 边界突变, 不光滑. 实际 fill price 可能有不连续跳跃. 单测 Case 3 边界附近要加 fuzzing
- **R-C (中):** Polymarket CLOB 微观 (atomic batch / lattice) 待小袁实测, 当前模型可能在 partial fill 上有偏差
- **R-D (低):** 多策略 Kelly 不在 v1, 单笔信号正确, 但同时跑多个相关信号时 (老梁 §8.2 单边集中风险) 实际杠杆会高于 sum(单笔 Kelly). v0.3 再修
- **R-E (低):** Decimal $\leftrightarrow$ double 转换在 sizing 边界 (size = approved_size $\pm \epsilon$) 可能有 1¢ 量级抖动, 与 RM §3.2 的 $\hat{q}_{low}$ 比较时要 epsilon-safe 比较

---

## 附录 A — 数学推导补充

### A.1 Kelly 公式两种形式等价性

经典: $f^* = \dfrac{bq - (1-q)}{b}$

设 $b = (1-p)/p$, 代入:
$$f^* = \dfrac{q(1-p)/p - (1-q)}{(1-p)/p} = \dfrac{q(1-p) - p(1-q)}{1-p} = \dfrac{q - p}{1-p}$$

$\square$

### A.2 staleness 半衰期推导

设 quote 在 $t = 0$ 发出, $\tau$ 时刻保持有效的概率为 $P(\tau)$. 假设 quote 失效服从 Poisson 过程 (撤单 / 调价独立同分布), 强度 $\lambda$:

$$P(\tau) = \exp(-\lambda \tau)$$

半衰期 $T_{1/2} = \ln 2 / \lambda$.

stale 因子 $s_{stale}(\tau) = 1 - P(\tau) = 1 - \exp(-\tau \cdot \ln 2 / T_{1/2})$

正文用 $T_{1/2}$ 不用 $\lambda$ 是为了和 "半衰期" 的直觉一致, 实际实现可以预算 $\lambda = \ln 2 / T_{1/2}$ 一次, 后续用 $\exp(-\lambda \tau)$.

### A.3 Almgren-Chriss $\sqrt{}$ 形式的简短论证

Almgren 假设短期价格冲击 = permanent + temporary. Permanent 部分线性于 $\dot{x}$ (成交速率), temporary 部分 $\propto |\dot{x}|^k$ ($k \approx 1/2$ 实证). 在我们 single-shot taker 场景下, 退化为 $\Delta p \propto \sqrt{S/V}$.

Almgren-Chriss "Optimal Execution" J. Risk 2000 / Almgren et al "Direct Estimation of Equity Market Impact" Risk 2005.

---

## 附录 B — 与老韩 RM v0.1 接口 diff (供老韩 v0.2 直接 cherry-pick)

```diff
# RM v0.1 → v0.2 (slippage 集成)

## §2.2 OrderIntent
+ book_depth_l1_usdc : decimal       (必填)
+ book_snapshot_ts_ns : i64          (必填)
+ tick_size : decimal                (必填)
+ expected_fill_price_hint : decimal (optional)

## §2.3 RiskDecision
+ expected_fill_price : decimal
+ expected_fill_rate  : ratio
+ slippage_bps        : i32

## §3 规则顺序
  R0..R3 不变
+ R3.5 (新): book depth / tick 合法性 (合并到 R3 INVALID_INTENT 也行)
+ R3.6 (新): slippage 估算, 触发 LOW_FILL_RATE / EXCESSIVE_SLIPPAGE / EXCEED_BOOK_DEPTH
  R4..R7 不变
  R8 (Kelly): 公式从用 p 改用 p_f, 加入 fill_rate × conf_factor 因子

## §3.10 RejectReason
+ LOW_FILL_RATE
+ EXCESSIVE_SLIPPAGE
+ EXCEED_BOOK_DEPTH
+ EDGE_NEGATED_BY_SLIPPAGE

## §5.2 audit schema
+ slippage_model_version
+ expected_fill_price
+ expected_fill_rate
+ slippage_bps
+ book_depth_l1_usdc
+ time_since_quote_ms
+ confidence_level

## §7 参数表
+ FILL_RATE_FLOOR          ratio  0.50  只可调高
+ MAX_SLIPPAGE_TICKS       int    3     只可调低
+ RHO_MAX                  ratio  3.0   只可调低
+ T_HALFLIFE_QUOTE_MS      int    4000  待实测
+ KAPPA_DEPTH              ratio  1.5   待实测
+ BETA_WITHDRAW            ratio  0.3   待实测
```

---

**END v1.** 等老韩 + 小梁 + 小蒋会签, 我再 bump v1.1 (含小袁实测后的 $T_{1/2}$ + 影子模式 2 周回测结果).

— 小肖, 2026-05-28
