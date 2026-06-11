# 持仓离场策略会 — 2026-06-10

> 主持：老雷（GM）| 参与：老姜（微观结构）、小程（量化信号）、老韩（风控）| 触发：老板「现在开会不等 1h」
> last_review: 2026-06-10

## 议题（老板提出）

止盈过于粗糙：「盘口没逆转为什么不继续持有获取更大利益，对风险判断不够细致」→「不是切换，是两边都要考虑」→「到底什么时候离场合适，赢面还很大卖了可惜」。

## 根因（GM 技术诊断）

现「骑住赢家」逻辑（`paper_loop.cpp` book_exit 块）用 **orderbook 失衡**（imb<−0.15 且 micro<mid）作逆转信号——微观噪声、会抖动，sharp fair 还强时一个失衡抖动就卖飞赢家。离场被 **bid 价格**驱动，不是被**赢面（fair）**驱动。

## 三方意见

- **老姜（微观）**：用 sharp fair velocity 替 book 失衡作主信号正确（velocity 跨 Goalserve 轮询、被反选过、方向性强）；book_down 保留作 OR 快速安全网（加 3-5s 冷却防噪）。参数 tp_reversal_vel_thr −0.003、near_settle 0.05。
- **小程（信号/EV，诚实保留）**：velocity 有 2.3s 滞后，正向「骑住」需 Vol 滤波防噪（Regime B 快变盘会骑住崩盘）；EV 上 high-fair 时早止盈 ≈ hold（市场已定价赢面），真「卖飞」只在 fair 0.70-0.80 中段；**关键：此为执行层优化，救不了 −EV 入场**（68% 亏损是 fair 场内反转，持有更久赢面会真翻向对面），CLV 验证比止盈精细化更根本。
- **老韩（风控）**：支持 velocity 替 book，**但「骑住更久」必须配盈利区 velocity 急转门**（否则 2.3s 滞后下单方向放大尾损）；rel_stop 只在亏损区生效，赢家从顶部逆转无保护；near_settle 只锁盈利仓利、不强割亏损仓（亏损仓走结算无 slippage）；改动在 paper 出场逻辑、不碰 RM 门/不开真钱闸，**无新会签**（在已签包络内）。

## 决策（GM 综合 + 老板「赢面」原则，已落地 commit 70ce0f53）

**离场由【赢面=sharp fair velocity 趋势】驱动，两边都考虑：**

| 赢面状态 | 动作 |
|---|---|
| 涨/稳（velocity ≥ −0.003） | 骑住捕获完整收敛（赢面大卖了可惜） |
| 真降（velocity < −0.003） | 放行止盈离场 |
| 急跌（盈利仓 velocity < −0.015） | 立即止盈（下行保护，补 rel_stop 太慢） |
| 近结算（剩余 < 5% 时长） | 盈利仓锁利（亏损仓让其结算无 slippage） |

book 失衡仅作快速安全网（赢面在升时忽略其噪声）。参数待回测：0.003 / 0.015 / 0.05。配置默认 0 → 回退旧逻辑，契约不变。

## 共识保留 / 后续

- 小程保留有效：velocity hold 救不了 −EV 入场，CLV 验证（150 笔）是更根本的事。本次是执行层减损，不是造 edge。
- 老姜 #2（reservation_sell margin 调宽）、book_down 冷却：后续轮评估。
- 监控 rebuy 门（iter1）+ 离场精细化（本次）的实效（fee 拖累 / churn / 净 PnL 路径平滑度）。

---

## 续会：开盘后交易历史复盘 — 2026-06-10（盯盘+看 fills，不等 1h）

参与：老姜（微观）、老韩（风控）| 基于新二进制 ~15min 真实成交（net −4.61, fee 1.79 吃掉近半 realized）

**两条收敛修复（commit 96111a6d 已部署）：**
1. **8pt-below-fair 卖根因（老韩钉死）**：taker 护栏 `bid_not_degenerate` 锚 `reservation_buy_px`(=fair−fee−margin) 非 fair → 真实容差 = 0.05+fee+半vig，正常 vig 把 5pt 顶成 8pt。改锚 `in.fair` → 真 5pt。
2. **碎卖 fee 头号成本（老姜+老韩）**：force_stop taker 砸 bid 被 per_order_cap 截成 12 笔碎卖（0x1ef76b），纯多付 fee。改：force_stop 一次卖全仓绕 cap。

**评估**：iter1 rebuy 门 + iter2 离场精细化机制逻辑正确，15min 样本看不到被触发/失效的明确证据（0x1ef76b 4 买大概率是初始建仓非 rebuy-churn，需 fills 时序确认）；0x9f991860 买1卖0=骑住赢家正面信号。中间档止损缺口（vel 门只在盈利区、rel_stop 要跌 25%）先不补，修完上两条再观测（老韩定序）。全部已签包络内无会签。
