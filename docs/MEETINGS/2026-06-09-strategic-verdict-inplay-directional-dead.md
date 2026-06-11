# 战略裁决会 — in-play 方向交易判死 + +200u 方向重定

date: 2026-06-09
chair: 老雷 (GM)
verdict_by: 老钱 (CPO, 产品方向决策权)
participants: 老张(博彩) / 小程(量化信号) / 老姜(微观结构) / 小梁(金融) / 老钱(CPO) / 老雷(GM 执行)
status: **CLOSED — 目标「+200u via in-play 持仓管理」结构性不可达, 已穷尽验证**

---

## 1. 背景
老板 /goal: 「从前端调试持仓逻辑, 通过持仓管理盈利 +200u, 可改代码重新部署」。GM 执行一整程: 修全部执行 bug + 召 4 专家会诊 + CPO 战略裁决 + 实盘验证 + 探索免费源。

## 2. 已完成的执行层修复 (无沉没价值, 任何新方向复用)
- churn 9× ↓; 手续费 13→1.4; force_cross/force_stop 解耦 (taker_exit=predictive_unwind||force_stop, 修 71%→50% churn 元凶); 持有到结算; rel_stop taker 止损; 决策观测面板 (盯盘页); λ haircut 0.35→0.25。
- **执行层结论: 无可挑剔。但执行优秀救不了 alpha 缺失。**

## 3. 决定性证据 (三层独立, 物理根因)
| 证据 | 结论 |
|---|---|
| 6-04 1s 精测 | PM 真实只滞后 bet365 ~0.3s → 收敛套利不存在 |
| 6-04 延迟探针 | 我们经 Goalserve 落后 bet365 P50 **2.3s**, 数据源硬天花板 |
| 6-09 实盘 8 笔 | ≥5% 偏离桶 **0% 胜 / −51**; 偏离越大亏越多 = 逆向选择 |

**物理根因: 我们手里 bet365 信号是 2.3s 化石, PM 早被现场看客在 0.3s 内定价完 → 我们系统性买"已移动过自己还没看到"的那侧 = 结构性逆选。不是 bug/参数/sizing, 是体质与赛道不匹配。**

## 4. 穷尽探索的所有替代方向 (全失败)
- **赛前 CLV** (CPO 唯一活路): PM 体育几乎无赛前盘 (实测 tag 1/2/100 共 0 个未开赛盘) — 撞市场墙。
- **免费源** (ESPN/sofascore/OddsAPI, 隔离在 experiments/laolei-freesource-latency/): 有免费比分、**无免费实时 fair**; 且任何聚合 API 都慢于 PM 现场看客。
- **自建真模型 alpha**: 我们输入(延迟 Goalserve 比分)比 PM 拥有的还差, 模型不可能赢过市场。
- **套利/做市/carry**: YES+NO<1 不存在 / 做市已否决 / carry 实测 −12 / 简单套利斗不过 PM 费+价差。

## 5. CPO 裁决 (老钱拍板)
1. **判死 in-play sharp 偏离方向交易, 立即止损** (已 halt paper_server)。
2. **+200u via PM 体育持仓管理: 结构性不可达 (<15%)**, 不粉饰。
3. 否决: 继续调参/验 CLV(污染前提) / fade(n=2) / 重启旧三引擎(13s 底座塌)。
4. **北极星 $5M 的可行方向收窄到三条**: 慢变量 edge / 覆盖广度(C++ 工程力) / 真正不同的市场或业务。**"靠比 PM 快"这条 alpha 路对我们物理关闭。**

## 6. 待老板拍板的战略选择 (创始人级, GM 无权定)
- (a) 接受结论, 目标收尾, 重定公司方向
- (b) 指一个新战场 (不同市场/alpha 源) → GM 带队调研可行性, 复用全部资产

## 7. 关联
memory: [[inplay-directional-dead-2026-06-09]] / [[flb-favorite-edge-2026-06-09]] / [[no-bet365-pm-convergence-edge]] / [[gs-bet365-latency-2300ms]]。前序: docs/RESEARCH/laolei-pm-inplay-no-edge-decision-2026-06-04.md (本会正式 close 它)。
