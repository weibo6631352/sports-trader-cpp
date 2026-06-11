# 策略会纪要 — 更多机会 / 更好持仓 / 更好对冲

date: 2026-06-09  主持: 老雷(GM)  参与: sports-market / quant-signal(小程) / quant-microstructure(老姜) / financial(小梁) / risk(老韩)
last_review: 2026-06-09

## 触发
老板诉求三条: ① 把握更多机会 ② 更好的持仓策略 ③ 更好的对冲。
地基硬数据 (paper restart 40min 实测): net −31.48, realized −45.95, fees 19.81, 189 笔平仓。亏损归因: **68% 是 sharp fair 场内真反转**(买 favorite, fair 翻 underdog), **32% 是 rel_stop 价格误杀**(fair 仍>0.5)。

## 五方收敛结论 (强一致)

**1. DD 不是问题，net EV 被 churn fee + 场内反转吃掉才是 (风控老韩定调)。** DD 6.4% 远低于 12% 预算。所以**反向腿对冲被否** —— 二元盘 buy NO≡卖 YES，对冲=部分平仓+多付费，往薄利伤口撒盐。老韩原话「我不批」。

**2. in-play sharp 偏离信号 half-life 仅 2-8s (小程+老姜)，远短于持仓周期** → 追场内移动靶必亏。出路不是调参，是换信号周期。

**3. 唯一绕开 2.3s 延迟的真机会 = 赛前 CLV (体育盘口+信号 一致点名)。** 赛前下注(线稳)、吃 PM-vs-bet365 收盘线收敛，慢变量(half-life 1-3h)，速度不咬人。**低成本验证: 记 3-5 天 pregame 快照(PM 价 vs bet365 pregame fair) → 结算对账看 hit rate >55%。** 复用现有 bm_slots 管线，不需新 feed。

**4. 真正有用的"对冲"是这三个，不是反向腿 (小梁+老韩):**
   - **lay-off 阶梯锁利** (盈利到入场 edge 的 0.3/0.6/1.0 倍分档落袋) → 在反转前把已赚的收敛锁死。预期 **DD 6.4%→3-4%, Sharpe +15-25%**。直击 68% 反转里"赢家回吐"那部分。
   - **YES+NO<1 同盘锁套** (零风险，`x_arb_free_edge` 已算好在 paper_loop.cpp:2063，只差接下单) → 对 fair 反转完全免疫。体育盘罕见但免费。
   - **corr cap (零成本)**: 多 favorite = N 倍押"热门赢"同向暴露，冷门日齐崩。开 `corr_mult_enabled`(现 false) + 跨赛事 favorite-β cap ≤40% equity。

## 立即执行 (Tier-0 止血，低风险共识)
| 改动 | 打击 | 来源 |
|---|---|---|
| **phase/decided 入场门**: 只在 `phase_frac<0.6 && game_decided_sign==0 && !near_end` 开新仓 | 68% 反转的末段/已决局部分 | 小程 |
| **rel_stop 精修**: `mark跌25% AND p_fair < 均入−0.05` 双确认 (不是 `<0.5`) | 32% 误杀 | 老姜 |
| **min-hold 门**: 建仓 N 秒(120-300)内不减仓(force_stop 除外) | churn fee (43% realized) | 老姜+老韩 |
| **入场收紧**: min_open_fair 0.50→0.58, edge 0.025→**0.03**(非0.04) | 质量换数量 | 老姜 |
| **corr_mult_enabled=true** (一行) | 组合同向暴露 | 老韩 |

## Tier-1 (真增长，需专项)
- **赛前 CLV**: 先 3-5 天 pregame 快照采集 → 验证 hit rate → 若 >55% 开赛前策略。**这是 +200u 的真引擎候选。**
- **lay-off 阶梯锁利** (config 默认关，A/B 验 Sharpe/DD)。
- **series-winner / outright 解锁** (现 fail-closed)，Totals Under FLB bias。

## 风控判断 (老韩，诚实)
风控开绿灯到 12% DD 预算，放大机制(G5 阶梯)备好；**但扣扳机前提是 CLV 先证明 net edge 为正。在那之前加对冲=往薄利撒盐，不批。** +200u 路径 = 验证 edge 为正 → 资金阶梯解锁放大，不是加对冲猛冲。

## agent 句柄 (可 SendMessage 续问)
sports: a6b1dce4ccbee8922 | signal: aaee4fab02689ab2b | micro: a771a3e0ff7c4c2df | finance: a4245b27562050c17 | risk: abd5bb26730b8df25
