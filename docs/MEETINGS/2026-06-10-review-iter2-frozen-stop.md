# 复盘 iter2 — 换端口后首轮 37 笔交易复盘 + 冻结期硬下行保护

owner: 老雷 (GM) | last_review: 2026-06-10 | 参与: 量化(小梁/微观) + 风控(老韩) 供料, GM 拍板

## 1. 数据 (7080, 旧 binary, ~换端口后一轮)
- 净 PnL **−16.48** (realized −11.24 + 浮亏 −2.94 + 费 2.31), 权益 983.52, maxDD 1.8%, sharpe −0.23
- 37 笔: 买 18 / 卖 19。**进场 18/18 全买在 fair 下方 (有 edge 进场, 执行干净)**
- 平仓: 赢 7 笔 +5.27 / 亏 12 笔 −16.50。**胜率 37%, 亏 > 赢 = −EV 签名 (复现已记录的 in-play directional −EV 判决)**
- 单笔最大亏 **−10.43** (+ 同盘 −3.11 = 单盘 −13.75, 占总亏 84%)

## 2. 元凶盘解剖 (0x5f2ed5aa, −13.75)
- 买 YES @0.821/0.824 (fair 0.855, 3pt edge, Kelly 放大 sz≈49 = 5% 账本)
- **sharp fair 从 0.855 崩到 0.550 (30pt)** → sold @0.541 (贴 fair 0.550 卖 = 执行正确)
- **全程 fair_is_sharp=TRUE** → 走 rel_stop/vel_exit 现有路径, **不是冻结盲区**
- 性质: 不是执行 bug。是「**高 favorite + 薄 edge + Kelly 放大仓 → fair 真崩盘**」的方向/不对称风险。
  favorite @0.823: 赢 +0.18/share, 崩 −0.28/share → 下行 > 上行, Kelly 因 1−p 小而放大仓 → 崩盘单笔巨亏。

## 3. 本轮落地 (已部署 7080)
**冻结期硬下行保护** `frozen_hard_stop_pct=0.40` (commit 31c9b979): sharp 掉档冻结态 (rel_stop/vel_exit 都失效)
下, mark 跌破均入×0.60 且双边簿紧 → 灾难止损。仅 fair_is_sharp=false 触发 → 与 −5.80 退化簿(sharp 有效)
签名互斥, 不回归卖飞。纯函数 + 8 断言单测。ctest 1398/1398。
**诚实**: 此修覆盖真实尾部缺口, 但**救不了本轮主亏**(主亏是 fair_is_sharp=TRUE 的真崩盘)。

## 4. 核实驳回 (verify-before-implement)
- 小梁「rebuy=0.0 建议开 30s」+ 微观「cooldown≥4s/premium≥0.03」**全作废**: daemon 实际 rebuy 0.06 + cooldown
  180s 早已远超, 两人读了陈旧/未构建代码。
- 微观 bug#3 (per_order_cap 未缩放): 真但仅回撤期生效且 sizing 先生效不会真超, 低优先, 未改。

## 5. CLV 仍 = 0 的真因 (关键)
37 笔成交**无一场结算** → 全是中途止损/止盈离场 → settlement-based CLV 永远攒不起来。
**「等 CLV 攒够出 edge 判决」的计划在 in-play churn 范式下不成立** —— 我们总在赛中止损出场, 拿不到赛末结算。

## 6. 下一步杠杆 (待 GM/老板拍板)
**高 favorite 仓位上层设计**: 元凶是 Kelly 在高 fair(1−p 小)上放大薄 edge 仓 → 崩盘巨亏。
候选: ① 高 fair 入场仓位封顶 (如 fair>0.80 时 per-position cap 砍半); ② 入场 fair 上限 (太贵的 favorite 不追);
③ 降 Kelly λ。需量化预期 + 老板定调是否动核心 sizing。

---

## 复盘 iter3 (v2 binary 上线后首查, 18:02)

**部署:** 赢面门 v1(rel_stop) + v2(扩到 Kelly减仓/predictive_unwind, 治「亏卖赢家」) + 订单簿方向 + 冻结硬止损 0.40。

**数据 (太薄, 7 买 0 卖):** 净 −4.56(全浮亏), 5 持仓, maxDD 1.7%, 费 0.56。
- ✅ below-fair 卖 0 (上轮 3) | churn 往返 0 (上轮 7 盘) | 赢面在却亏卖 0 | 全在长持
- ⚠️ 浮亏 −3.9 (5 仓) = 过度持有**观察项**, 未失控
- ❌ 无平仓 → 胜率/realized/单笔最大亏 **无法判** → 核心假设(砍 churn 让 FLB 浮现)未验

**结论:** 离场逻辑行为符合设计(持有/不 churn/不亏卖赢家), 但需穿过结算才能判 PnL。

**纪律 (GM 今日教训):** 今日已重启 4 次(端口/冻结/赢面门/v2), 每次清零数据 = CLV/胜率判决拿不到的根因。
**坚决不再重启**, 让 v2 binary 跑足够久(穿过结算)。高 favorite 仓位封顶(治 −10.43 blowup)候选**暂缓**, 待这轮数据成熟,
若 blowup 复现再批量改 1 次。

---

## 复盘 iter4 (exit_reason+M2-a binary, 18:34) — exit_reason 立功

**账本健康 (vs 上轮 −16.48 质变):** 净 **+1.63** / 浮 **+2.43** / maxDD **0.4%** / 单笔最大亏 **−0.07** / 高favorite blowup **无**。

**每笔卖出原因 (老板「出现卖出就检查是否合理」已落地):** 6 笔卖出**全 vel_exit**。below-fair 卖 0 / churn 0 / 严格可疑割肉 0。

**⚠️ exit_reason 暴露的边界问题:** vel_exit 在 fair 还 0.640(赢面在) 时因 sharp velocity 急转向下, 把 YES 卖在 0.610-0.615(fair 之下)。
realized ~0 到 −0.07(近打平, 非大割肉)。**vel_exit 是 force_stop 路, 绕过 v2 赢面门** → 「赢面还在却因 velocity 卖赢家」。
- 待老板拍板: vel_exit 是否该加赢面门 (fair 仍高时即使 velocity 急跌也先扛, 等 fair 真跌破再走)?
- **未改、未重启 (铁律: 今日重启 6 次毁了 +2.68 好运行)**。记录待批。

**结论:** 离场逻辑整体健康(+1.63), exit_reason 观测到位。vel_exit 调参是下一个候选, 但攒数据穿结算优先, 不为单点重启。

---

## 会话总收尾 (2026-06-10 晚) — 策略全套重建 + 关键 bug 修复 + 待决项

### 本会话落地 (按 commit 时序)
**离场逻辑收敛 (老板连环指令):**
- 冻结期硬下行保护 frozen_hard_stop=0.40 (sharp 掉档崩盘 backstop)
- 赢面门 v1/v2 → 最终收敛为**统一铁律: 只 book 恶化且 fair≤0.46 才割, 否则骑到底, 无止盈** (vel_exit/rel_stop/near_settle 全关)
- 不切边: 持有某边锁定该边, 不追对面 edge (game_decided 兜底对面赢家)
- 再入场当新机会 (撤 rebuy 冷却/改善门)

**入场:**
- min_open_fair 0.58→0.65 (只买赢面≥65%强 favorite)
- 入场先动者闸: sharp 先动(velocity>0)才进, sharp 滞后+簿坏=假 edge 挡 (治 2.3s 逆选)

**仓位:** caps 砍半 (per_order 25/per_outcome 50/market 60) 限 blowup

**关键 bug 修复:**
- 端口 8080/8081→7080/7081 (防窥视)
- 资金恒等式 cash+position_mtm≡equity (无 mark 仓回退入场价)
- 净值曲线口径统一 mark (=net_pnl)
- 成交流水补结算笔 (cum_realized 对账)
- 每笔卖出带 exit_reason (可审「为什么卖」)
- 孤儿结算安全重做 (SettlementPoller 轮询持仓 cid + loop 线程直接结算; 避开上次 GP fault 崩溃区) — 治 CLV=0 真根因
- **★ staleness 门放宽 (5s→60s/90s→600s): 治「活比赛突然无赔率源」—— 根因是用单场 last_update 当 feed 在线性, 正常静默被误判冻结; matched_with_sharp 1→46**

### 当前状态
系统稳定、覆盖恢复(46 赔率源盘)、入场漏斗健康(持续买强 favorite 带 edge)、攒数据中。净 −4.55(浮亏)、realized 0、**clv_n=0 (等结算)**。

### 待老板决策 (回来定)
1. **入场门松紧 (量 vs 质):** 0.65 选择性强→入场少→CLV 攒得慢。要更快判决可松(量), 要质量保持 0.65。
2. **扩赔率源覆盖:** no_match=162 是真覆盖限制(futures/未开赛/bet365 不覆盖)。加 pregame odds/别 bookmaker = 更多盘但「扩源/转赛前」大决策。
3. **esports 字典:** 抓取失败→启发式兜底(疑 Goalserve 无 esports dict, 良性噪声; 确认需探 Goalserve)。

### 核心未决 (需时间, 非代码)
**「赔率源 in-play directional 路有没有真 edge」的客观判决 = clv_close_mean 攒够样本(~15-50 场结算)。** 正=继续放大冲 +200u / 负=结构性 −EV 转赛前。此前历轮 realized 持续 −EV, 但都受 churn/早平/覆盖 bug 污染; 本会话全修后的「干净」数据待积累。**铁律: 不重启(清零 clv_n)、不频繁动。**
