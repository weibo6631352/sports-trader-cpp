# 凯利公式对接 & 虚拟盘现金/估值显示 — 六席专家评审

> owner: 老雷 (GM) | last_review: 2026-06-01
> 触发: 老板「虚拟盘要有现金、估值显示, 不然凯利公式怎么对接? 商量凯利用法是否合理、能否利益最大化」
> 参会(并行评审): 小梁(数学/Kelly主权) · 老韩(RM主权) · 小肖(数值算法) · 老李(Polymarket协议) · 小苏(前端) · 小郑(后端可观测)

---

## 1. 评审结论 — 老板直觉戳中两个真 bug

1. **虚拟盘无账户级现金/估值**: cash 账、equity 净值、equity 曲线全缺 (`pnl_timeseries()` 是空 stub), 前端无从显示。
2. **凯利 bankroll 写死 100k 静态值** (`paper_loop.cpp:806` `sz_in.bankroll_usdc = cfg_.bankroll_usdc`):
   回撤不缩 (亏 15% 仍按 100k 下注 → 分母虚大 → 加速突破 MDD≤15%)、盈利不涨 (复利失效)。
   老韩早裁定 = 「风控纸面化」(`paper_loop.cpp:1306` 注释)。**复利输入断了 → 当前谈不上利益最大化。**

## 2. 六席完全一致 (无需拍板, 直接做)

- **equity = cash + Σ(持仓 × 市价)**; 必须收敛成**单一 `AccountEquity` 函数** (当前 `RecordEquity@304` 与 `FeedRiskGateway@1348` 两套口径打架 = 审计噩梦)。
- **双口径分离**: 展示/前端用 microprice; **风控/DD熔断/凯利分母用 best_bid 保守清算价**。无效 bid 仓位按 0 浮盈 (不臆造正值, 老韩铁律#2)。
- bankroll 必须动态化 (无争议 bug); 新增 `GET /api/v1/account` 端点 + 前端资金面板; `pnl_timeseries` 用 `PortfolioMetrics` 现成 equity deque 落地。
- **fail-closed**: 净值分量 NaN/Inf/ledger 读失败 → bankroll 回落 `min(initial_cash, realized_equity)`, ≤0 → sizing 出 0。**绝不静默回落 100k** (= 偷偷放大下注)。
- R-11: equity 聚合只读 paper ledger 实例, 不碰真账本。

## 3. 老板拍板的 4 个决策 (2026-06-01)

| # | 议题 | 老板定 | 落地约束 |
|---|---|---|---|
| D1 | 凯利 bankroll 口径 | **裸 equity 含浮盈** (小梁案, 几何增长最优) | 覆盖老韩「不签裸 equity」立场 → GM 用安全护栏补偿 (见 §4) |
| D2 | 喂凯利的持仓 MtM 价 | **best_bid 保守价** (老韩/小肖案) | 与 DD 熔断同源; 浮盈虽计入但按保守价估 → 化解「浮盈幻觉超注」 |
| D3 | λ 系数 (现 0.25) | **直接 0.35 写死** (老板 2026-06-01 复议: 不做可配, paper 直接试不回测) | paper 非真钱直接跑看实测 maxDD; z 门保持 1.645 不动; **真钱开闸前拿 paper 实测 maxDD≤15% 找老韩签** |
| D4 | cash 精度 + neg_risk | **MVP 近似 cash + neg_risk 屏蔽** | cash=realized-gain 聚合近似 (老李标注「尚可但须标清」); neg_risk 市场暂不参与 paper 交易 |

**D1+D2 自洽性 (GM 注)**: 老板选的「裸 equity 含浮盈」+「best_bid 估值」组合自洽 ——
浮盈计入满足几何增长诉求, 但用 best_bid 保守价估值正好堵住老韩担心的顺周期浮盈幻觉。
即 `bankroll_for_kelly = realized_equity + Σ(best_bid − avg_entry)×qty` (浮盈用清算价, 不用 microprice)。

## 4. GM 钉死的两条红线纪律

1. **进攻口径 + 安全护栏并存**: 老板选裸 equity (进攻), GM 叠老韩护栏 ——
   ① fail-closed 回落小值; ② 净值跌破 `initial×0.30` → 停新单 (与 DD 熔断对齐); ③ tick 入口冻结快照防 read-skew/抖动; ④ 浮盈用 best_bid 非 microprice。
2. **λ 升 0.35 不随 bankroll 动态化静默上线**: §8 红线 MDD≤15% + 老韩 RM 主权。
   执行法: 代码先把 λ **参数化** (默认仍 0.25), 灰度到 0.35 前**必须**跑回测出 maxDD≤15% 实测 + 老韩签字。给老板的决定配一道「数字说话」验证门。

## 5. 落地计划 (GM 亲自写代码, 主干, §10.2)

| Step | 内容 | 状态 | commit |
|---|---|---|---|
| 0 | 单一 `account_equity()` (抽 `FeedRiskGateway` best_bid 聚合); `RecordEquity@304` 改传完整 equity; 加 `cum_fee_pusd()` getter | ✅ 完成 | fb70f2b |
| 1 (P0) | `paper_loop.cpp:806` 改读 tick 冻结 `bankroll_for_kelly`; fail-closed | ✅ 完成 (修「纸面化」) | fb70f2b |
| 3 (P1) | `GET /api/v1/account` 端点 + `AccountSnapshot` + 线程安全发布 + 回调注入 | ✅ 完成 | e8ec18c |
| 4 (P2) | 前端资金概览面板 (8 StatCard + Kelly bankroll 说明条, 5s 轮询) | ✅ 完成 (stub 截图核验) | 67cb1a2 |
| 2 (P1) | cap 链净值联动 (C1-3 叠 `clamp(bankroll/initial,0.5,1)`) | ⏳ 待做 (C4 已随动态 bankroll 自动收紧) | — |
| pnl_ts | `pnl_timeseries` 真实落地 (equity_snapshot 已暴露) | ⏳ 待做 (前端先用 account 卡片) | — |
| staleness | stale book→0 浮盈 gate (改 DD 红线路径) | ⏳ 另案 (需老韩签 + 改 A5 测试) | — |
| **λ** | λ 0.25→**0.35** 直接写死 (kLambdaBase) | ✅ 完成 (老板 2026-06-01 拍板 paper 直接试, 不做可配/不回测) | (本批) |

全量 ctest 1352/1352 绿; 前端 tsc+vite build + vitest 14/14 绿。

## 6. 未决/follow-up (归口)

- D6 复利锚点 initial vs HWM → 小梁主权 (影响回撤恢复期 sizing 快慢)
- D7 aggregate_unrealized 聚合粒度 全账户 vs Σmin(0,per-condition) → 小梁/小袁评微观
- D8 cap 缩放 floor=0.5 是否够严 → 小梁几何增长角度评
- D9 neg_risk NegRiskAdapter 结算正确性 → 老李协议层 + 老胡 PM 排期 (本期已 gate 掉)
- staleness gate (比赛暂停/WSS 断流 stale book 不污染 equity) → 小肖, 并入 Step 0
