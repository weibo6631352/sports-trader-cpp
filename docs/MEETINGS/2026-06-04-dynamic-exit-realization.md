# 金融分析团队会议 — 动态持仓实现盈利 (退出/收敛兑现)

> owner: 老雷 (GM) | last_review: 2026-06-04 | 召集: 老板「我们瞄准的不是结算, 是动态持仓实现盈利; 以前讨论过, 隔太久忘了」
> 参会: 老彭 (财务) / 小袁 (微观执行) / 小程 (信号时机) | 综合: 老雷

## 0. 背景

老板纠正范式: **盈利来自动态持仓 (买事件错价边 → 收敛兑现 → 轮动), 不是持仓到结算。** 这是 `docs/RESEARCH/profit-scheme-v2-FINAL.md` 三引擎模型 (引擎A 事件延迟收敛套利, 出场=收敛+maker零费腿)。当前 paper 系统偏离了它。

## 1. 三方收敛诊断 (独立确认同一根因) —— 「只买不卖」= 两个独立 bug 叠加

| bug | 代码 | 机理 |
|---|---|---|
| **D1 卖价永不触发** | `position_controller.hpp` `sell_px = fair+fee+margin` | 卖价在 fair【之上】。我们因 sharp_fair>市场买入, 市场收敛到 fair 时 best_bid≈fair < sell_px → 卖腿永不 marketable。语义错位: 这是「等市场比我更乐观才卖」(赌结算), 不是「收敛兑现」。 |
| **D2 target 永不下降** | `paper_loop.cpp` `target_mag = Kelly(edge=fair−市场)` | live 比赛 sharp fair 随比分涨 → edge 恒正 → target 恒升 → gap>0 一路买, 永不进 gap<0 卖分支。 |

唯一减仓路径是 M2-a「选边翻转平旧边」(target=0), 不表达「同边收敛退出」。→ 结构性只买不卖 → realized=0 → 持到结算 (= alpha 退化成赌博)。

## 2. 收敛实证 (小程, 来自 shorthorizon-locking-feasibility-v1 §C2)

PM 向 bet365 收敛延迟: p25=8.5s / 中位=13.2s / p75=20.5s / p90=32s。**70%+ 够本移动在事件后 10s 内**, 30s 后无增量。α 衰减是 S 型: 前 5s 慢 (散户反应慢), 5-15s 快 (跟风涌入), 15s 后停。**退出甜区 t∈[5s,15s], 持仓 >30s = 把 edge 送给后来的 taker。**

## 3. 修法 (三方收敛, 规则版今天可落地, 无需模型)

**核心认知 (小程, 钉死)**: 退出的唯一正确判据是 **`current_edge < BE`** (市场是否追上 fair), **不是 sharp_fair 绝对值是否还在涨**。混淆这两个 = 只买不卖的认知根因。

### 3.1 必须同步改两处 (D1+D2, 缺一仍卖不掉)

- **D2 修 (target 降)**: 给持仓加显式退出触发, 命中 → 强制 `target_mag = 0` → 控制器涌现卖。触发 (任一, OR):
  - **时间门**: `hold_time > T_max` (推荐 **20s**, = PM reprice p75)。
  - **edge 衰减**: `current_edge < 0.5×BE` (≈0.7%; 市场追上 fair, 套利空间没了)。
  - **gap 翻号**: `sign(sharp_fair−mkt) != sign_at_entry` (方向反转, 快速止损)。
  - **sharp 断流**: sharp_fair 陈旧 → 无锚不持。
- **D1 修 (卖能成交)**: 退出腿卖价**改锚 `avg_entry`** (不是 fair): 卖触发 `best_bid ≥ avg_entry + min_net_profit` (锁利, 绕开 fair 漂移)。maker post-only 挂 `best_bid+tick` 跟 bid 上抬零费收割 (BE 1.4%); deadline/反向走 taker 砸 bid。buy 侧公式**一字不动** (零回归)。

### 3.2 需新增的最小状态 (per-position entry record)
进场 (首 fill) 记: `entry_edge = sharp_fair − microprice`, `entry_ts`, (avg_entry 账本现成)。挂 condition→EntryRecord map。

### 3.3 防 churn (小袁, 三道防线)
① `exit_sell_px ≥ avg_entry+净利` 地板 (物理上买完不可能立刻卖); ② `min_hold` 时间闸 (~2s, 挡单 tick 噪声); ③ OFI 同向确认 (复用 b_ofi_dyn, 真买盘推升非闪现)。

## 4. 与 Kelly 调和 (老彭, 理论闭环)
不是「Kelly vs 收敛退出」二选一。把 Kelly 的 edge 输入从「静态进场 fair」换成「**剩余 edge = fair − mark**」: 市场收敛 (mark→fair) → 剩余 edge→0 → target→0 → Kelly 自己产出收敛退出, 无需止盈开关。超半衰期后剩余 edge 折现 < 轮动成本 → 锁利轮动。

## 5. 轮动 vs ride-to-settlement (北极星 DD≤15% / Sharpe≥1.5)
| | ride-to-settlement | 动态退出轮动 |
|---|---|---|
| 单笔损失 | 全损 (−entry, 赌输归零) | 有界 stop −2.5% |
| 持仓 | 数小时 | 5-45s |
| Sharpe | 低 (厚尾+长持+低频) | **高 (√N 大数定律)** |
| maxDD | 连亏击穿 15% | stop 有界可控 |
**ride-to-settlement = alpha 退化成赌博的定义。** 轮动是北极星唯一可达路径。

## 6. 推荐参数 (起点, paper 标定)
| 参数 | 值 | 依据 |
|---|---|---|
| T_max | 20s | PM reprice p75 |
| edge 衰减阈 | 0.5×BE ≈ 0.7% | BE≈1.4% maker |
| min_net_profit (出场地板) | BE_maker 1.4% + 0.5% 垫 | round2-finance §4.1 |
| capture_frac (部分退) | 0.6-0.7 | 尾部成交概率陡降 |
| deadline 强平 | 30-45s (3×horizon) | 防退化裸方向 |
| 反向止损 | −2.5% (≈1×BE) | 有界 DD |
| min_hold | ~2s | 防 churn |

## 7. GM 决议 (落地顺序)
1. **先实现规则版退出** (3.1 两处同步 + 3.2 entry record + 时间门/edge 衰减/gap 翻号 OR 触发): 解「只买不卖」根因, 工程量小, 不阻塞模型。
2. maker post-only 出场腿 (零费) 次之 (需 OrderIntent 加 post_only, 归口高频系统)。
3. jump_h/y_h 模型版退出后续 (不阻塞)。
