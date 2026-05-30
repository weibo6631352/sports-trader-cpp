# Sprint-1 Retro — 小肖发言 (数值算法 / Kelly + slippage owner)

- Speaker: 小肖 (quant-numerical-algo)
- Date: 2026-05-28
- Sprint: Sprint-1 Retro Batch 2
- 听取: 已读 Batch 1 八份 (老郭 / 老韩 / 老胡 / 老黄 / 老钱 / 老周 / 小梁 / 小余)

---

## 0. 一句话立场

**Batch 1 五件事我逐条 ACK + 给数字: 小梁 fill_rate 0.50 + KELLY 0.25 + PER_ORDER $5K/$2K 我会签; 老韩 4 字段 + 4 reject enum 接口对齐 (一处 NaN 检测补充); P0-01 阈值 5¢ Linear 模型重算 net edge floor 1.5-3¢; Sprint-2 只做 Mode A (Linear) 接入, Mode B M5 后切; SlippageModel C++ lib header-only 零依赖, Sprint-2 W1 交付小蒋共享.**

---

## 1. 小梁 fill_rate 0.50 + KELLY 0.25 + PER_ORDER_CAP 全套 — Agreed

**三参数会签, 不议价.**

- `KELLY_FRACTION = 0.25`: MacLean-Thorp-Ziemba 2010 Pareto 前沿低风险端, log growth 44% / DD var 1/16. 我 v1 §1.4 一致
- `FILL_RATE_FLOOR = 0.50`: 我 v1 §4.2 默认, "不允许试试下 50%"口子, MVP 不松
- `PER_ORDER_CAP_HARD = $5K` (constexpr) / `SOFT = $2K` (config 只可调低): 与小袁 gameday $2K/$10K 中位 0¢/1¢ 吻合, $5K hard 留 2.5x 余量给调 SOFT 不需 rebuild

**你 §2.3 乘积效应论证我 100% 同意**: $f^*_{adj} = f^* \cdot \mathbb{E}[\text{fill\_rate}]$, fill 期望已内化, **不存在重复保守**. "1/4 × 1/2 = 1/8 太死"的言论走 §5.2 升级路径, 我和你联签拒.

**给老韩 RM v0.2 §7 参数表会签 (我 + 小梁 联签)**: 上 4 项 + `MAX_SLIPPAGE_TICKS = 3` + `RHO_MAX = 3.0`. Sprint-1 锁死, 调整走三签 (我+小梁+老韩).

---

## 2. 老韩 4 字段 + 4 reject enum — 对齐

**对齐, v0.2 §3.10 我会签. 一处当面确认.**

- OrderIntent 4 字段: `book_depth_l1_usdc / book_snapshot_ts_ns / tick_size` (必填, 缺失/NaN → `REJECT(INVALID_INTENT)`) + `expected_fill_price_hint` (optional, RM 重算)
- 4 reject enum 全 ACK: `LOW_FILL_RATE / EXCESSIVE_SLIPPAGE / EDGE_NEGATED_BY_SLIPPAGE / EXCEED_BOOK_DEPTH` (注: `EDGE_NEGATED` 我 v1 用 `APPROVED(size=0, reason=...)` 语义不是真 REJECT)

**NaN 检测补充 (要老韩 ACK)**: `int64_t book_snapshot_ts_ns` 没有 NaN, 退化为 `== 0`. 0 是"未填"还是"epoch 起点"我 v1 没明示. **建议**: `== 0` OR `< (now_ns - 60s)` 双条件 → `REJECT(INVALID_INTENT)`, 等价"60s 以上 snapshot 视同未填". v0.2 §3.10 请 ACK, Sprint-2 W1 加单测.

---

## 3. P0-01 阈值 3¢ → 5¢ — Linear 模型 expected fill 修正

**Linear 模型重算, net edge floor 1.5-3¢ 与小梁账本一致, 不需要再加缓冲.**

设 $p_q = 0.50, L_1 = \$2.5K$ (NBA gameday 中位, 小袁 §3.3), $S = \$2K, \rho = 0.8$ 一档内, $\tau = 0.01$:

| 项 | 数值 |
|---|---|
| 阈值 (Pinnacle no-vig dev) | 5¢ |
| $p_f$ Linear | $0.50 + 0.01 \times 0.8 \times 0.5 = 0.504$ |
| slippage | 0.4¢ |
| fill_rate | $\approx 0.85$ (HIGH conf, $> $ floor) |
| taker fee (3% × $p_f$) | $\approx 1.51¢$ ($p_q = 0.50$, 不是 $p_q = 1.00$ 的 3¢) |
| **net edge** | $5 - 1.51 - 0.4 \approx 3.1¢$ |

**给小梁的反馈**: 你 §2.2 "net edge = 5-3-0 = 2¢" 把 fee 当 3¢ 是 $p_q = 1.00$ ceiling, 实际 $p_q = 0.50$ fee = 1.5¢. 你那 2¢ 是悲观 floor, **实战中位 3¢**, edge 比你账本宽. 边界 $\rho > 3$ → `REJECT(EXCEED_BOOK_DEPTH)` fail-closed (v1 §3.1 case 3).

`EDGE_NEGATED_BY_SLIPPAGE > 60% → 调到 6¢` 反馈逻辑 ACK, 我 v1 §6 已埋 metric `reject_by_reason`, Sprint-2 W3 exporter 出.

**结论**: 5¢ 我会签, Linear 模型不需要重调.

---

## 4. Mode A → B → C — Sprint-2 哪个 Mode 上 √?

**Sprint-2 只做 Mode A (Linear) 接入 RM + 单测 7 case. Mode B (√ Almgren-Chriss) M5 后切 (需 backtest RMSE 数据). Mode C (Polymarket CLOB lattice) v0.3 等小袁 batch + maker rebate 实测.**

| Mode | 名称 | 启用阶段 | 触发条件 | 责任人 |
|---|---|---|---|---|
| A | Linear | **Sprint-2 全程 MVP** | 默认 | 小肖 |
| B | √ Almgren-Chriss | M5 后切 | `RMSE(B)/RMSE(A) < 0.85` | 小肖 + 小蒋 |
| C | Polymarket CLOB | v0.3 (M6+) | 小袁 batch + maker rebate 实测 | 小肖 + 小袁 |

**为什么 Sprint-2 不切 Mode B**:
1. **数学保守**: Linear $S \to 0$ 时 slippage 不 → 0 (0.5 平均档位假设), 比 √ 悲观 ~30%. MVP 宁高估不乐观
2. **参数标定**: Mode B 需要 $\eta_{poly} / \sigma_{poly} / V_{24h}$. 没有 backtest 校准的 √ 模型是数值玄学
3. **小董 M4.5 G7**: paper 期间换 Mode → baseline 比较失义, Mode 锁死至 M4.5 通过

**Mode B 触发条件给小蒋 (精确)**: Sprint-2 paper 期间, 跑历史 2025-01..2026-04 NBA + MLB 200+ 笔 fill, 用 actual $p_f$ vs Mode A/B 预测算 RMSE. `RMSE(B)/RMSE(A) < 0.85` → 三签 (我 + 小蒋 + 小梁) M5 后切.

**命名碰撞**: 小钱 §5.1 P1.5 "paper-trade Mode A/B" 是 fill 模拟精度, **不是**我 slippage Mode. Sprint-2 W1 GM 决策统一命名: paper-mode → `{Sim, Hybrid, Real}`, slippage-mode → `{Linear, Sqrt, CLOB}`. 不再用字母.

---

## 5. SlippageModel C++ lib 共享小蒋 paper engine — 接口好吗

**好, header-only + 零依赖. Sprint-2 W1 我抽 lib 给小蒋.**

```cpp
// include/numerical/slippage_model.hpp (header-only, zero dep)
namespace stcpp::numerical {

enum class Confidence : uint8_t { HIGH, MEDIUM, LOW };
enum class SlippageMode : uint8_t { Linear, Sqrt, CLOB };  // v1 只实现 Linear

struct SlippageInput {
    double  order_size_usdc;       // 必填, > 0
    double  quote_price;           // 必填, (0,1)
    double  book_depth_l1_usdc;    // 必填, > 0
    int64_t time_since_quote_ms;   // 必填, >= 0
    double  tick_size;             // 必填, 0.001 或 0.01
};

struct SlippageOutput {
    double     expected_fill_price;  // VWAP, [quote_price, 1)
    double     expected_fill_rate;   // [0,1]
    Confidence conf;
    bool       exceed_depth;         // rho > RHO_MAX
};

SlippageOutput estimate(const SlippageInput& in,
                        SlippageMode mode = SlippageMode::Linear) noexcept;

struct Config {
    static constexpr double FILL_RATE_FLOOR  = 0.50;
    static constexpr double RHO_MAX          = 3.0;
    static constexpr int    MAX_SLIPPAGE_TICKS = 3;
    static constexpr double KAPPA_DEPTH      = 1.5;
};

}  // namespace
```

**三个保证**:
1. **header-only**: 不引额外 TU, 不影响 RM 200us 预算 / paper engine 编译速度
2. **零依赖**: 仅 `<cstdint>` + `<cmath>`
3. **constexpr 友好**: 常量编译期, 配合老周 §15.6 R-12 vCPU0 红线 < 50us 无 heap alloc

**红线 (我 v1 §6 + 小梁 §2.5 R-12 hash)**: backtest/paper/prod config 4 常量必须 hash 一致; Mode 枚举严格三态不允许字符串; NaN/Inf → `exceed_depth=true conf=LOW`, RM 转 `REJECT(INTERNAL_ERROR)` fail-closed.

**Sprint-2 W1 交付**: header lib + 单测 7 case (含 case 5 EDGE_NEGATED) + Google Benchmark p99 < 200ns + 给小蒋 + 老韩 PR review 联签.

## 6. 双向收口

| # | 事项 | 找谁 | Deadline |
|---|---|---|---|
| 1 | RM v0.2 §3.10 ACK `book_snapshot_ts_ns < (now-60s) → INVALID_INTENT` | 老韩 | 6/12 |
| 2 | paper-mode 与 slippage-mode 命名统一 (`Sim/Hybrid/Real` vs `Linear/Sqrt/CLOB`) | GM + 小袁 + 小蒋 | Sprint-2 W1 |
| 3 | Mode B 切换 backtest RMSE 跑通 | 小蒋 | M5 前 |
| 4 | `tick_size` 实测分布 (S1-002) | 老李 | 6/12 |
| 5 | maker rebate + batch 周期 (Mode C v0.3 输入) | 小袁 | M3 前 |
| 6 | `expected_fill_price` 透传到 limit 单定价 | 老叶 | Sprint-2 W2 |

---

## 7. 一句话给老雷

**Batch 1 五件事全 ACK + 给数字: KELLY 0.25 + FILL_RATE 0.50 + PER_ORDER $5K/$2K 会签老韩 v0.2 §7; 4 字段 + 4 reject enum 对齐 (1 处 NaN 检测补充); P0-01 阈值 5¢ 下 Linear net edge floor 1.5-3¢ 与小梁账本一致; Mode A Sprint-2 接入 + Mode B M5 后切 (RMSE < 0.85 触发) + Mode C v0.3 等小袁; SlippageModel C++ lib header-only 零依赖 Sprint-2 W1 交付小蒋共享.**

— 小肖 (quant-numerical-algo), 2026-05-28
