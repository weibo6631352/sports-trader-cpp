# Paper 长跑攒数据 + 复盘统计 — 执行计划

> owner: 老雷 (GM) | last_review: 2026-06-14
> 背景: 真钱已停 (LIVE_ARMED=0, 后端 halted)。结论 (三方调研: GM + data-stats 小董 + quant-backtest):
> 要回答"策略到底有没有 edge / 预期 vs 实际胜率偏差"——数据已 ~80% 够,真瓶颈是
> **纪律(别重置/别改参/打版本标签)+ 样本量(挂几周攒 150+ 结算)+ 几个便宜的加性字段**。
> 字面 17/18=94% 与 live ~67% 两个样本太小、置信区间重叠,统计上分不出 edge vs 运气。

## 原则
- 字段改动一律**加性、零风险**(NaN 省略机制, 不破坏现有 schema)。
- 分析脚本走 **Python (非生产, 离线)**, 不进热路径。
- **不重置账本、不改策略参数**是攒有效样本的前提; 任何改参必打版本标签行。

---

## Phase 1 — 长跑前必做 (P0)
- [ ] **1.1 token_id 进 fills_journal** — FillRow 加 `tok` + JournalFill 落 `"tok"`。消除跨文件 join 的 cond+side 反查歧义。三方一致。
- [ ] **1.2 版本标签行** — 启动时往 fills_journal 写一行 `{"type":"version", sharp_only_min_edge, min_open_fair, kMaxOpenAsk, git_commit, mode, ts}`。**不打标签 → 跨时间样本混不同策略版本 = 污染, 白攒**(回测 agent 命门点)。
- [ ] **1.3 start 脚本默认不 reset** — 确认 start_paper.sh / 重启流程不清账本快照 (fills_journal/settlements 本就 append-only; 确认快照恢复链完整)。
- [ ] **1.4 订单簿深度历史** (老板「订单簿挺重要的」) — 当前只在成交刻记 book(L1+d5)、position_path 30s 只记 L1 bid/ask/mid 无深度。
  - **Tier 1 (做)**: 扩 position_path 加 L1-L5 深度 + spread + imb + microprice + book_age → 持仓期订单簿深度时间序列(读 hub, 无新网络)。
  - **Tier 2 (可选, 另议)**: 候选/活跃盘 book_history.jsonl firehose (粗间隔 + 节流 + 磁盘预算), 给非持仓盘的微观结构研究。

## Phase 2 — 用现有数据出"第一刀" (最解渴, 不等新数据)
- [ ] **2.1 离线分析脚本** (Python, experiments/) — join fills_journal × settlements.jsonl:
  - 胜率: **只用 `exit_reason="settlement"` 行**(提前卖出卖价≠真实输赢, 混入系统性低估); drop `settlement_value=-1`(解析失败)。
  - 逐笔 CLV: buy 行 `px` vs 结算行 `close_mid` (注意 close_mid 陈旧问题, 过滤 bk_age 大的)。
  - 校准曲线: `fair` 分桶 vs 实际 `settlement_value` 命中率。
  - **Wilson CI** (n<30 用 Clopper-Pearson); NaN 覆盖率体检 (低于 80% 不做校准)。
- [ ] **2.2 跑出第一刀** — 给出带置信区间 + 诚实标注(小样本/跨版本污染)的初步胜率/CLV/校准。

## Phase 3 — 富化 + 数据质量 (P1)
- [ ] **3.1 EntryCtx stash → 富化 live WSS 行** — 下单时按 order_id stash 决策上下文 (EntryCtx + fair + 簿 + equity + 进场时间/赛段), WSS 确认时取出写富行。live 行变得与 paper 一样富 + 自然产出每仓生命周期行。(老板"内存里都有")
- [ ] **3.2 settlement_value=-1 标记** — SettlementRecorder 加 `parse_ok` (=settlement_value!=-1)。PM 二元市场无平局, -1 一定是解析失败, 必 drop。
- [ ] **3.3 CLV close_mid 陈旧检测** — 结算行若 bk_age 过大则标记 (别当收盘线, 否则 CLV 正率虚高)。

## Phase 4 — 锦上添花 (P2)
- [ ] **4.1 sharp-gap 近失日志** (节流) — gap < sharp_only_min_edge 的盘当前在 target_mag 归零处不触发 LogGateBlock = 完全不可见。补一条节流日志 → 解锁"0.05 门槛该不该下调"分析(比 gate_blocks 更大的盲区)。
- [ ] **4.2 离散赛段标签** (第几盘/节/局) + 入场账本快照(现金/持仓数/本盘敞口) + 入场后 +10s/+30s Δmid(逆选探针) + position_path 加 entry_ts。

## Phase 5 — 长跑攒样本
- [ ] **5.1 锁参数 + 挂 paper 4-6 周** — 目标 150-200 独立结算 (settlement 口径)。中途不重置、不改参。
- [ ] **5.2 周期复盘** — gate_blocks × settlements 单独验"每道门挡对没"(DuckDB); 定期跑 2.1 脚本看胜率/CLV 区间收敛。

## 分析纪律 (写进脚本 + 复盘 SOP)
1. 胜率分母只用 settlement 行; PnL 分母用全部 (两个数都报, 防 survivorship 虚高)。
2. gate_blocks/未入场盘**不混进 edge 验证样本**(不同总体), 只单独验门。
3. 预注册检验维度; 多重比较 Bonferroni/BH 校正; 分层结果 n 不足只当探索。
4. 离线 CLV 从 journal 重算, 不信内存聚合量。
