---
owner: 小程 (quant-signal-research, C 单元 IC #19)
last_review: 2026-05-29
status: v0.1 草稿 — 待老彭联合 review + 小梁 ack
relates_to:
  - docs/RESEARCH/laopeng-w9-w5-betting-industry-research-update-v1.md
  - docs/RESEARCH/xiaocheng-w10-w1-p0-02-spec-v02.md
  - docs/RESEARCH/xiaocheng-signal-catalog-v1.md
  - docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md
---

# P0-03 Cross-Platform Vig-Arb Signal — 信号 Spec v0.1

- **Owner**: 小程 (quant-signal-research, C 单元 IC #19)
- **Date**: 2026-05-29
- **Last review**: 2026-05-29
- **Status**: v0.1 草稿 — 待老彭联合 review + 小梁 ack
- **验收人**: 小梁 (C 主管)
- **ADR cite**: ADR-027 (N/A — 本文件无核心 struct 改动, 纯 signal spec doc)
- **候选来源**: 老彭 Wave 87 W9 W5 行业调研 §3.2 §5.1 (最高优先候选 #1)

**关联文档**:
- `laopeng-w9-w5-betting-industry-research-update-v1.md` (老彭 §3.2 §5.1 §5.2 P0-03 候选提案)
- `xiaocheng-w10-w1-p0-02-spec-v02.md` (P0-02 v0.2 配套, OQ-P02-5 P0-02 vs P0-03 反向触发检测)
- `laopeng-multiplicative-devig-calibration-v1.md` (ADR-008 de-vig, P0-01/P0-03 共享 pregame 多源路径)
- `xiaocheng-signal-catalog-v1.md` (信号目录 v1, P0-03 候选槽)

**拒接声明**: 本文件是 signal spec, 不含 C++ 代码。实现由小卢 IC pool 接单。回测由小蒋接单。

---

## 0. 信号基本信息

| 字段 | 值 |
|---|---|
| 信号 ID | `P0_03_CrossPlatformVigArb` |
| 信号全名 | Cross-Platform Vig-Arb Signal — Polymarket vs Pinnacle/DK 双锚方向性交易 |
| 信号类型 | pregame 价值回归, 跨平台隐式概率差驱动 |
| alpha 来源 | PM implied_prob vs Pinnacle no-vig + DK no-vig 双锚差异 (散户 PM 偏好溢价) |
| 盘口 | Moneyline (Soccer / NBA 首发), M4 后扩其他盘口 |
| MVP scope | NBA + Soccer 五大联赛 (高流动性场次优先) |
| de-vig 方法 | ADR-008 pregame 多源路径 (Pinnacle 为主锚, DK 为辅助锚) |

**本质澄清**: P0-03 是**方向性交易信号，不是真套利**。命名中的 "arb" 指 vig 差驱动的方向性 edge，不是跨平台双腿无风险套利。
- 直接套利无法实现：PM 是链上结算 (Polygon)，资金转入转出 30min-24h，机会窗口 < 5min
- 实际操作：识别 PM 相对 Pinnacle 的方向性 mispricing，单腿在 PM 做方向性押注

---

## 1. 信号选型依据

### 1.1 与 P0-01 的区别

| 维度 | P0-01 (pinnacle-novig-revert) | P0-03 (cross-platform-vig-arb) |
|---|---|---|
| 锚源 | Pinnacle 单锚 | Pinnacle (主) + DK/FD (辅) 双锚 |
| 门槛 | PM-Pinnacle_novig ≥ 3¢ | PM-Pinnacle_novig ≥ 4% (更严格) |
| 方向 | 均值回归 (PM fade Pinnacle) | 相同方向 + 辅助锚确认 (信号更纯净) |
| 触发频率 | 高 (3¢ 低门槛) | 低 (4% 高门槛) |
| Alpha 纯度 | 中 (单锚, 部分触发是 Pinnacle 噪声) | 高 (双锚交叉确认, 假阳性更少) |
| Covariance | 自身参考 | 与 P0-01 covariance ~0.4-0.6 (相关, 但不完全重叠) |

**P0-03 不替代 P0-01**。P0-01 是高频低门槛，P0-03 是低频高精度。两者 covariance 待老彭回测精确测量，若 > 0.5 需要 P0-03 触发逻辑增加与 P0-01 的正交化过滤。

### 1.2 为何双锚 (Pinnacle + DK) 比单锚更优

老彭 W9 W5 §3.3 实测案例分析:

```
NBA 总决赛 Celtics vs Pacers (样例):
  Pinnacle ML no-vig: 65.5% (CLV 锚, 最准)
  DraftKings ML no-vig: 66.7% (public 偏好拉高)
  Polymarket mid: 70.0% (散户溢价更大)

PM-Pinnacle gap: 4.5pp → P0-01 触发 + P0-03 触发
PM-DK gap: 3.3pp → 辅助确认 (PM 高估方向一致)
DK-Pinnacle gap: 1.2pp → 正常 book bias, 不触发
```

**双锚逻辑**: Pinnacle 是最准锚，确定方向；DK 同向确认，说明 PM 高估不是 Pinnacle 偶发噪声，而是 PM 散户系统性偏好。两个锚同向时，信号纯度更高。

### 1.3 P0-01 vs P0-03 触发重叠分析

```
P0-01 触发: |PM_mid - Pinnacle_novig| ≥ 3¢
P0-03 触发: |PM_mid - Pinnacle_novig| ≥ 4% && DK 同向确认

P0-03 是 P0-01 的子集 (门槛更严格) + 额外 DK 验证。
P0-03 触发 → P0-01 几乎必定触发 (4% > 3¢ in near-even market)
P0-01 触发 → P0-03 不一定触发 (P0-01 低门槛允许更多噪声)
```

**同 event 持仓叠加规则**: P0-01 和 P0-03 可能同 event 同方向触发。MVP 阶段的持仓叠加上限由老韩 RM per-outcome cap (R6.3) 统一管控，不在本 spec 定义。

---

## 2. 触发条件 (4 条 AND, short-circuit C4→C3→C1→C2)

### C1: PM vs Pinnacle no-vig 主锚偏离

```
let pinnacle_novig = Pinnacle_fair_value(sport, match_id, as_of_ts)
                   // ADR-008 pregame 多源路径: Pinnacle closing line no-vig
                   // 老彭 W9 W5 确认: Pinnacle 是最准 CLV 锚

let p_pm = polymarket.moneyline.yes.microprice()

let dev_pinn = p_pm - pinnacle_novig   // 正 = PM 高估 Yes → 卖 Yes (买 No)

C1 = |dev_pinn| >= 0.04            // ≥ 4pp (比 P0-01 的 3¢ 严格, 老彭 W9 W5 §3.2 建议)
  && (dev_pinn 方向一致性: 持续 ≥ 60s)  // 不是瞬时噪声, 是持续偏离
```

**持续 60s 要求**: PM pregame 价格有短时波动，60s 持续偏离说明偏离是结构性的，不是瞬时报价 bounce。

### C2: DK/FD 辅助锚同向确认

```
let dk_novig = DraftKings_fair_value(sport, match_id, as_of_ts)
             // ADR-008 pregame 路径, DraftKings implied_prob 去 vig
             // DK 精度比 Pinnacle 低约 1-2pp (public book), 但仍可确认方向

let dev_dk = p_pm - dk_novig          // 正 = PM 高估 vs DK

C2 = sign(dev_dk) == sign(dev_pinn)   // 两个锚方向一致
  && |dev_dk| >= 0.02                 // DK 辅锚偏离 ≥ 2pp (噪声过滤)
```

**注意**: DK 作为辅助锚仅确认方向，不用于定量计算 fair_value (Pinnacle 是定量锚)。若 DK 方向与 Pinnacle 反向 (C2=false)，说明 Pinnacle noise 可能性更高，不触发。

### C3: PM book 流动性 ≥ $3K

```
let depth_3tick = polymarket.orderbook.depth_within_3_ticks(side=direction)

C3 = depth_3tick >= 3000   // $3K, 比 P0-02 的 $2K 严格 (pregame 流动性更好, 可提高门槛)
```

### C4: 时间窗口 (pregame only)

```
let hours_to_game = game.start_time - now()

C4 = hours_to_game >= 0.5h       // 开赛前 30min 还有信号窗口
  && hours_to_game <= 6.0h       // 开赛前 6h 内才值得关注 (6h 外流动性过低)
  && (game.status == "pregame")  // 非 inplay
```

**6h 窗口依据**: Pinnacle closing line 在 T-6h 到 T-0 期间信息密度最高，PM 与 Pinnacle 的价差收敛速度最快 (老彭 `laopeng-multiplicative-devig-calibration-v1.md §3.2`)。

---

## 3. 入场 / 出场逻辑

### 3.1 入场

```
direction  = sign(pinnacle_novig - p_pm)   // +1 = PM 高估 Yes → 买 No (fade PM)
                                            // -1 = PM 低估 Yes → 买 Yes
size_base  = kelly_fractional(pinnacle_novig, 1/p_pm - 1) * 0.25   // 1/4 Kelly (pregame 比 inplay 保守少)
size       = clamp(size_base * bankroll, 500, 3000)                 // $500-$3K, 比 P0-02 上限更高

// 死区: pinnacle_novig > 0.80 或 < 0.20 → size = 0 (极端赔率噪声更大)

order_type = LIMIT @ (microprice + direction * 0.5 * tick)   // 半 tick 进, passive
fallback   = TAKER @ best_ask/bid  if not filled in 30s      // pregame 时效不如 inplay 紧, 30s fallback
```

### 3.2 出场

```
take_profit:  |dev_pinn_t| < 1¢                          → close (Pinnacle gap 收敛)
stop_loss:    |dev_pinn_t| 反向扩大到 > 6%                → close (Pinnacle 自己跳了)
time_stop:    持仓至开赛前 5min 未触发 TP/SL             → close (inplay 前强制退出)
game_stop:    game.status 变为 "inplay"                   → 立即 close (不持仓过盘)
line_move:    Pinnacle line 单向移动 > 2pp 不回            → re-evaluate, 条件不满足则 close
```

**pregame 不持仓入 inplay**: P0-03 是 pregame 信号，一旦开赛立即关闭，防止与 P0-02 inplay 信号相互干扰。

---

## 4. Alpha 估计

### 4.1 预期 Alpha 参数 (老彭 W9 W5 §5.2 数字)

| 参数 | 预估值 | 来源 | 备注 |
|---|---|---|---|
| Hit rate | **62-68%** | 老彭 W9 W5 §5.1 估算 | 均值回归基础下, dev ≥ 4pp 时收敛概率高 |
| Edge (gross) | **3-5¢** | 老彭 W9 W5 §5.2 | 大 dev 触发, 较 P0-02 edge 更大 |
| Net edge (估算) | **+1.5-2.5¢** | 老彭估算 | gross 3-5¢ - 3% fee - slippage ≈ 1.5¢ 以上 |
| Sharpe (年化 paper 目标) | **1.2-1.8** | 估计 | IS 目标 1.2, OOS 目标 0.9 |
| Max drawdown | ≤ 10% | RM 硬 cap | |
| Trades / day (NBA 大场) | **3-8** | 老彭 §5.1 | dev ≥ 4% 的高质量信号频率较低 |
| Trades / Finals day | **5-15** | 老彭 §4.1 | Finals 期间 PM 散户最活跃 |
| 容量 / 单笔 | **$1-3K** | 老彭 §3.2 | 比 P0-02 inplay 高, 因 pregame 深度更好 |
| 日累计容量 | **$10-30K** | 估算 | 3-8 次 × $1-3K |

**hit rate 62-68% 可信依据** (老彭 §5.1): PM vs Pinnacle gap ≥ 4pp 时，历史均值回归统计下，PM 概率向 Pinnacle 方向收敛的比率在 62-68% 区间。PM 散户偏好结构性存在（公众偏爱热门/主队），偏离超过 4pp 说明偏离程度高于 Pinnacle 定价噪声，均值回归更确定。

### 4.2 Alpha Decay 曲线 (pregame 特性)

P0-03 是 pregame 均值回归信号，decay 模式与 P0-02 inplay 不同：

```
decay 驱动: sharp 资金流入 + Pinnacle 向 closing line 收敛
decay 时间尺度: 分钟到小时级别 (与 P0-02 的 秒级 decay 完全不同)

典型 decay 轨迹:
  T-6h: dev=4pp, alpha 窗口打开
  T-4h: dev=2.5pp, sharp 资金已部分进入, 60% alpha 残余
  T-2h: dev=1.5pp, Pinnacle 调线或 PM 做市商调价, 35% 残余
  T-1h: dev=0.8pp, 接近收敛, 10% 残余
  T-5min: 强制 time_stop
```

decay 不用 exp 函数拟合 (pregame 均值回归是非线性的，受盘口信息事件驱动)，用分段时间策略替代。

### 4.3 与 P0-01 的 Covariance 估计

```
P0-03 与 P0-01 的 alpha 来源部分重叠:
  - 两者都用 Pinnacle no-vig 作主锚
  - 两者都在 PM 高估时 fade PM

但有独立分量:
  - P0-03 额外要求 DK 同向确认 → 过滤 Pinnacle 噪声 → 捕捉 PM 对 "公众偏好" 的 excess pricing
  - P0-01 在 Pinnacle 噪声场景也会触发 → P0-03 不触发

预估 covariance: 0.35-0.55  (与 P0-01 相关但不完全重叠)
```

若实测 covariance > 0.5, 需评估是否需要正交化过滤 (P0-03 触发时检查 P0-01 是否已持仓，若 P0-01 已持仓同方向则 P0-03 只触发剩余容量)。精确数字由老彭 W10 W3 历史回测三信号联合矩阵输出。

### 4.4 验收度量

| 度量 | IS 阈值 | OOS 阈值 | 备注 |
|---|---|---|---|
| Hit rate | ≥ 62% | ≥ 58% | 低于 P0-01 OOS 门槛 58% → 信号不上 paper |
| Net edge / trade | ≥ 1.0¢ | ≥ 0.7¢ | 扣 3% fee 后净值 |
| Sharpe (年化) | ≥ 1.2 | ≥ 0.9 | |
| Max drawdown | ≤ 10% | ≤ 12% | |
| P0-03 vs P0-01 covariance | — | ≤ 0.6 | 超 0.6 则需要正交化过滤 |
| OOS/IS Sharpe ratio | ≥ 0.6 | — | 过拟合检验 |

---

## 5. 数据源依赖

### 5.1 数据源清单

| 数据 | 接口 | Owner | 用途 | 状态 |
|---|---|---|---|---|
| Pinnacle no-vig | Pinnacle API / Goalserve pregame getodds (Pinnacle 来源) | 老李 + 老彭 | C1 主锚 fair_value | 老李 W9 W5 调研 Pinnacle API 状态 (见老彭 §8 @老李) |
| DraftKings odds | DK 公开 API 或 Goalserve pregame (DK 来源) | 老李 + 小段 | C2 辅助锚方向确认 | **未接入 — W10 W2 评估** |
| PM mid / microprice | Polymarket WSS market channel | 小冯 | C1/C2 PM 定价 | 已接入 (P0-01 共享) |
| PM orderbook depth | 同上, `book` event | 小袁 | C3 流动性 | 已接入 |
| game.status / start_time | Goalserve pregame (小段 v3) | 小段 | C4 时间窗口 | 已接入 |

### 5.2 DK 数据源评估 (关键 Open Question)

**DraftKings 没有官方公开的批量 odds API**。老彭 W9 W5 §8 已经 @老李 询问 DK API 状态。

可行数据路径 (老彭 + 老李 W10 W2 评估):

| 路径 | 优点 | 缺点 | 可行性 |
|---|---|---|---|
| Goalserve pregame getodds (若含 DK) | 已有 Goalserve 接入 | 老彭 v3 §1 显示 pregame 有 8-9 家但不确定含 DK | 待小段 audit |
| DK Partner API (需商务合作) | 官方稳定 | 需要合规审查 + 商务协商, M5 前难落地 | 低 |
| 公开价格爬取 (单次探索用) | 快速验证 | 违反 ToS 红线, 正式使用不允许 | 仅 PoC |
| 无 DK 降级: 单 Pinnacle | 工程最简 | P0-03 退化为 P0-01 + 持续时间过滤 | 可行但 alpha 独立性降低 |

**MVP 阶段降级方案**: 若 W10 W2 前 DK 数据源无法接入，P0-03 使用 **单 Pinnacle 锚 + C2 = 持续 90s 过滤** (替代 DK 确认)。降级版 alpha 独立性低于双锚版，但可进入 paper 阶段。

### 5.3 de-vig 方法 (ADR-008 pregame 路径, 共享 P0-01)

```
// P0-03 使用 ADR-008 pregame 标准路径 (多家 bookmaker 均值)
// Pinnacle 在 pregame 多家均值里权重最高 (sharp book, 最准)
// P0-03 从均值中额外提取 Pinnacle_specific no-vig

pinnacle_implied_p = 1 / pinnacle_value_eu
pinnacle_overround = sum_k(1 / value_eu_k_pinnacle)  // 仅 Pinnacle 单家
pinnacle_novig     = pinnacle_implied_p / pinnacle_overround

// DK 辅锚同理 (若可用)
dk_novig = dk_implied_p / dk_overround
```

---

## 6. 与 P0-02 的关系 (OQ-P02-5: 同 event 反向触发)

### 6.1 触发时间维度不同

```
P0-02: inplay event-driven (goal/red_card/penalty 后 90s 窗口)
P0-03: pregame 持续偏离 (开赛前 0.5-6h 窗口)

两者触发时间完全不同 → 不会在同一 event 反向触发
```

### 6.2 潜在冲突场景

**场景一: pregame P0-03 持仓 + 开赛后 P0-02 同方向触发**

例: 开赛前 P0-03 判断 PM 高估 Yes → 买 No (持仓). 开赛后 P0-01 goal event → P0-02 判断 PM 低估 Yes (方向相反) → 触发.

此时 P0-03 和 P0-02 持仓方向相反。解决方案:
- P0-03 有 `game_stop` 规则 (game.status = "inplay" 时立即平)，因此 P0-02 触发时 P0-03 已平仓
- 只有 P0-03 平仓延迟的极端情况下才会双向持仓，RM per-outcome cap (R6.3) 兜底

**场景二: P0-02 持仓期间 P0-03 再入场 (pregame 卖出已被 P0-03 买入)**

此场景不存在，因 P0-03 是纯 pregame，P0-02 是纯 inplay，二者不重叠。

**结论 (OQ-P02-5 初步分析)**: P0-02 与 P0-03 在时间维度完全分离，不会同 event 反向持仓。精确分析由老彭 + 小程 W10 W2 联合 spec 时确认。

---

## 7. ADR-027 Cite

本 spec v0.1 不涉及 OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot 6 个核心 struct 改动。

ADR-027 cite: **N/A** (纯 signal spec doc, 无核心 struct 改动)。

C++ 实现阶段若修改 FairValue struct 接入 DK 双锚路径，需补 ADR-027 Enforce-1 cite。

---

## 8. W10 派单 (小梁 ack 后执行)

### 8.1 老彭 W10 W2 — 双锚数据源评估 + alpha 精细化

```
Owner: 老彭 (波 87 自提, 小梁 W10 W1 派单)
截止: W10 W2 EOD
内容:
  1. DK 数据源接入可行性评估 (配合老李 §5.2 路径评估)
  2. Pinnacle closing line data (手头历史数据) → P0-03 hit rate / edge 历史估算
  3. P0-01 vs P0-03 covariance matrix 样本 (2024 NBA regular season 子集)
  4. 降级方案 alpha 对比: 双锚版 vs 单 Pinnacle + 60s 持续过滤版
验收: 小梁
```

### 8.2 小程 W10 W2 — P0-03 spec v0.1 更新 (本文件)

```
Owner: 小程
截止: W10 W2 EOD
内容:
  1. 纳入老彭 W10 W2 DK 数据源评估结果 → 决定是否走双锚或降级单锚
  2. OQ-P02-5 P0-02 vs P0-03 同 event 冲突确认
  3. 验收度量与 P0-01/P0-02 covariance 确认 (老彭提供数字)
验收: 小梁 + 老彭
```

### 8.3 小蒋 W10 W3 — Backtest Framework 跨平台数据接入

```
Owner: 小蒋 (老彭 §7.4 提议, 小梁 W10 W2 派单)
截止: W10 W3 EOD
内容:
  1. backtest framework 加入多平台锚 (Pinnacle + DK) 数据 ingestion
  2. 历史数据切片: 2024 NBA season + 2024 UCL + 2025 NBA Finals
  3. 支持 P0-03 信号 backtest (两锚 implied prob 差)
依赖: 小余历史数据仓库; 老李 Pinnacle API 接入状态
验收: 小梁
```

### 8.4 小卢 W10 W4 (依赖 spec ack + 回测通过) — P0-03 cpp 骨架

```
Owner: 小卢 (IC pool, 老周统筹)
条件: spec v0.1 ack + 老彭 W10 W3 回测 OOS 验收通过
内容:
  1. `p0_03_cross_platform_vig_arb.hpp/.cpp` 骨架 (继承 ISignalEngine)
  2. C1-C4 触发条件实现
  3. `tests/unit/test_p0_03.cpp` ≥ 20 test case, ctest 100% pass
  4. DK 降级路径: 若 DK 数据源未接入, fallback 到单 Pinnacle + 持续时间过滤
验收: 小梁 (功能) + 小蒋 (paper engine 联调)
```

---

## 9. 开放问题

| # | 问题 | Owner | 截止 |
|---|---|---|---|
| OQ-P03-1 | DK 数据源接入可行路径 (Goalserve pregame 是否含 DK? Partner API 可行性?) | 老李 + 小段 + 老彭 | W10 W2 |
| OQ-P03-2 | Pinnacle API 实测 RTT 状态 (老李 W9 W5 @老彭 问询) | 老李 | W10 W2 |
| OQ-P03-3 | P0-01 vs P0-03 covariance 精确值 (双锚版 vs 单 Pinnacle 版各自 covariance) | 老彭 回测 | W10 W3 |
| OQ-P03-4 | DK 降级方案 alpha 损失量化: 双锚 hit 62-68% vs 单锚 hit 58-62% (估算, 老彭) | 老彭 | W10 W2 |
| OQ-P02-5 | P0-02 vs P0-03 同 event 反向触发分析 (本文件 §6 初步分析) | 小程 + 老彭 | W10 W2 spec 联合 |
| OQ-P03-5 | P0-03 MVP 阶段是否进 Sprint-3, 还是 M2 后开? | 小梁 + 老钱 | W10 W1 信号评审会 |

**OQ-P03-1 是最高优先级**: DK 数据源决定 P0-03 是双锚版还是降级版，影响 alpha 独立性和工程复杂度。W10 W2 前必须有结论。

---

## 10. 不耻下问记录

- **@老彭**: W10 W2 DK 数据源评估 + Pinnacle 历史 P0-03 hit rate 估算 (联合 spec)
- **@老李**: Pinnacle API RTT 实测状态 + DK Partner API 可行性评估 (OQ-P03-1/P03-2)
- **@小段**: Goalserve pregame getodds 8-9 家 bookmaker 中是否含 DraftKings 数据 (OQ-P03-1)
- **@小梁**: P0-03 是否进 MVP 首批 vs M2 后开 (OQ-P03-5)，W10 W1 信号评审会时决策
- **@小蒋**: backtest framework 多平台锚数据 ingestion 工期估算 (W10 W3 目标)

---

## §X ADR-027 cite 汇总 + W10 W2 派单小结

### 派单汇总 (W10 W2, 待小梁 ack)

| 任务 | Owner | 截止 | 依赖 |
|---|---|---|---|
| P0-02 spec v0.2 ack | 小梁 | W10 W1 | 本 wave 完成 |
| P0-03 spec v0.1 ack | 小梁 + 老彭 | W10 W2 | 本 wave 完成 |
| P0-03 DK 数据源评估 | 老彭 + 老李 | W10 W2 | 老李 Pinnacle API 调研 |
| P0-02 v0.2 C2=6¢ 回测 | 老彭 | W10 W3 | spec ack |
| P0-03 hit rate / covariance 回测 | 老彭 | W10 W3 | 数据源评估 + spec ack |
| backtest framework 多平台锚接入 | 小蒋 | W10 W3 | 老彭 数据需求 |
| P0-02 cpp 实现 | 小卢 IC pool | W10 W4 | v0.2 spec ack + 回测通过 |
| P0-03 cpp 骨架 | 小卢 IC pool | W10 W4 | spec ack + 回测通过 |
| paper engine 联调 (P0-02 + P0-03) | 小蒋 | W11 W1 | 小卢 cpp 完成 |

### ADR-029+032 流程声明

本文件按 ADR-029 §3.1 + ADR-032 §3 策略 3 执行：

- 本地 spec 完成
- push 后不等远端 CI (ADR-032 策略 3)
- 回汇 GM: commit hash + PR URL + "doc only, ctest N/A"

ADR-027 cite: N/A (两份文件均为纯 signal spec doc, 无核心 struct 改动)。

---

**v0.1 完成汇报**:

P0-03 cross-platform vig-arb spec v0.1：双锚策略 (Pinnacle 主 + DK 辅)，预期 hit 62-68%，net edge +1.5-2.5¢，Sharpe 1.2-1.8 (paper 目标)。本质是单平台方向性交易，不是真套利 (链上资金速度限制直接套利)。decay 是分钟到小时级 pregame 均值回归，与 P0-02 秒级 inplay decay 完全独立。DK 数据源接入可行性 (OQ-P03-1) 是最高优先级开放问题，W10 W2 前必须有结论。

待 **老彭 W10 W2 DK 评估 + 小梁 ack** 后进入回测 → cpp 骨架阶段。

— 小程，2026-05-29
