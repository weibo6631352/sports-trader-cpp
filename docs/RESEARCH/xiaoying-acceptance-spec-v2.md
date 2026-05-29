# Acceptance Spec v2 — M1 38 条 Update + GM-PAPER-G 验收 + O1-O4 映射

- **Owner:** 小颖 (requirements-analyst, E 产品业务保障部)
- **Last review:** 2026-05-29
- **验收人:** 老雷 (GM) + 老钱 (CPO) + 老胡 (PM)
- **任务来源:** E 主管老胡 → 小颖, W10 W3 截止, 无依赖 (P1)
- **输入文档 (权威):**
  - `docs/OKR/laolei-2026-drive-directive-paper-profit-v1.md` (§2 O1-O4 / §3 GM-PAPER-G v2 八条门禁)
  - `docs/RESEARCH/laohan-w9-orderintent-v05-spec-v1.md` (ABI v0.5: token_id/outcome/side/condition_id 四处 breaking)
  - `docs/RESEARCH/laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md` (SignerV62 v6.2: timestamp_ms/metadata/builder/pUSD)
  - `docs/RESEARCH/xiaoying-acceptance-spec-v1.md` (v1 基线 38 条)
  - `docs/OKR/laoqian-w8-w5-profitability-kr-v1.md` (盈利 KR spec)
- **v2 vs v1 delta 摘要:** 38 条 ABI v0.5 对齐标注 + 新增 GM-PAPER-G 八条验收 criteria + O1-O4 验收映射 + 缺口清单

---

## 0. v2 文档目的与 SSOT 边界

**目的:** 在 v1 基础上完成三件事:

1. 把 M1 38 条与 **ABI v0.5** (OrderIntent v0.5 + SignerV62 v6.2) 对齐, 标注哪些条款因字段变更需修订.
2. 把 **GM-PAPER-G v2 八条门禁** 翻译成可验收的 acceptance criteria (如何测 / 通过标准 / 谁签字).
3. 建立 **O1-O4 验收映射** (管线贯通 / 盈利可证 / 持续盈利 / 零浪费 各自的判定口径).
4. 标注 **缺口** (无对应实现 / paper 阶段专属).

**SSOT 边界 (同 v1):**
- 本文是验收 SSOT, 不重写 RM 规则 / BC / PRD / KR 文档.
- GM-PAPER-G 阈值数字以 `laolei-2026-drive-directive-paper-profit-v1.md §3 v2` 为准, 本文不修改阈值.

---

## 1. ABI v0.5 影响分析 — M1 38 条标注

### 1.0 ABI v0.5 变更速览

**OrderIntent v0.4 → v0.5 (老韩 W9, `laohan-w9-orderintent-v05-spec-v1.md`):**

| 变更 | 类型 | 影响等级 |
|---|---|---|
| `market_id` rename → `condition_id` | ABI break #1 | L2 |
| 新增 `token_id: string` (uint256 十进制) | ABI break #2 | L2 |
| 新增 `outcome: Outcome enum` (Yes/No/...) | ABI break #3 | L2 |
| `is_buy: bool` → `side: Side enum` (Buy=0/Sell=1) | ABI break #4 | L2 |

**SignerV62 v6.2 新字段 (老孙 W10 W1, `laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md`):**

| 变更 | 类型 |
|---|---|
| 移除 `nonce/feeRateBps/expiration/taker` | 删除 |
| 新增 `timestamp_ms` (替代 nonce) | V2 新增 |
| 新增 `metadata` (bytes32) | V2 新增 |
| 新增 `builder` (bytes32, optional) | V2 新增 |
| `size_usdc_micro` → `size_pUSD_micro` | rename + 抵押品变更 |

### 1.1 受 ABI v0.5 直接影响的 M1 条款

下列 M1 acceptance 条款因 ABI v0.5 字段变更需要修订 (标记 `[ABI-UPDATE]`):

#### 1.1.1 M1-A03 — OrderIntent 100% 经 RM (0 绕过)

**v1 原文:** `(1) 静态扫无绕 RM 路径; (2) 1h 窗 signer.wal audit_id 数 == risk.wal allow 数`

**v2 修订 [ABI-UPDATE]:**
- `M1-A03` OrderIntent 100% 经 RM (0 绕过) | CI grep+WAL audit | 老高+老唐
  - (1) 静态扫: 无绕 RM 路径 (grep 拦截, 含 V1 signer_v52 禁 live binary)
  - (2) WAL 字段核查: audit_id 数 == risk.wal allow 数 (1h 窗)
  - **(3) [NEW] token_id 透传核查:** audit record 中 `token_id` 字段非空 + 与 OrderIntent.token_id 一致, 0 丢失; 覆盖 ABI break #2
  - (4) [NEW] `side` 字段核查: audit record 中 `side` (uint8) 值在 {0,1} 内, 无旧字段 `is_buy` 残留; 覆盖 ABI break #4
  - **过: (1)+(2)+(3)+(4) 全过**

#### 1.1.2 M1-A04 — 21 种 RejectCode 各 ≥1 单测

**v2 修订 [ABI-UPDATE]:**
- `M1-A04` RejectCode 覆盖 | Unit | 小宋
  - v1 "21 种" 基础上新增 ABI v0.5 引入的 5 个新 sub_reason:
    `MISSING_TOKEN_ID / MISSING_CONDITION_ID / INVALID_TOKEN_ID_FORMAT / TOKEN_OUTCOME_MISMATCH / BOOK_TOKEN_ID_MISMATCH`
  - 以及新增 RejectCode: `EXCEED_PER_OUTCOME_CAP`
  - **总覆盖: 21 种 RejectCode + 6 种 v0.5 新增 sub_reason/code, 各 ≥1 单测**
  - **过: 全覆盖**

#### 1.1.3 M1-C01 — P0-01 信号 5 条件 AND

**v2 修订 [ABI-UPDATE]:**
- `M1-C01` 信号 → OrderIntent 输出字段完整性 | Unit+Sim | 小程+小梁
  - 信号触发后输出 OrderIntent v0.5 必含: `condition_id` (原 market_id rename), `token_id` (非空), `outcome` (Outcome enum), `side` (Side enum, 非 is_buy bool)
  - **25 case (5×5) 全过, 且 OrderIntent 字段 schema v0.5 100% 合规**
  - **过: 25 case + 字段 schema check 全过**

#### 1.1.4 M1-C05 — signal→intent 时序透传

**v2 修订 [ABI-UPDATE]:**
- `M1-C05` signal→intent 时序 + 字段透传 | Integration | 小程+老唐
  - (event_ts, ds_ts, as_of_ts) 完整透传 (原 R-20, 不变)
  - **[NEW] token_id 透传:** signal 层填入 `token_id`, OrderIntent 到 audit record 全链路一致, 无中间层自行推断/替换
  - **[NEW] condition_id 透传:** 全链路 `condition_id` 不退化回 `market_id` 字符串
  - **过: 4ts 100% + token_id 100% + condition_id 100%**

#### 1.1.5 M1-D04 — 4 ts 违例 → INVALID_INTENT.TS_*

**v2 修订 [ABI-UPDATE]:**
- `M1-D04` INVALID_INTENT 校验扩展 | Unit | 小宋
  - 原 4 种 ts 违例 (不变)
  - **[NEW] ABI v0.5 新增 3 种 invalid_intent 触发 (老韩 §4.4):**
    - `MISSING_TOKEN_ID`: `token_id.empty()` → REJECTED
    - `MISSING_CONDITION_ID`: `condition_id.empty()` → REJECTED
    - `INVALID_TOKEN_ID_FORMAT`: token_id 含非数字字符 → REJECTED
  - **过: 4+3 = 7 种各 ≥1 case**

#### 1.1.6 M1-D05 — VirtualFill 可解释

**v2 修订 [ABI-UPDATE]:**
- `M1-D05` VirtualFill 公式可解释 | Unit+Sim | 小袁+小蒋
  - 原公式验证: (filled_size, avg_price, slippage) 偏差 <1bp (不变)
  - **[NEW] size 字段单位:** VirtualFill 计算基于 `size_pUSD_micro` (V2 抵押品 pUSD), 不接受 `size_usdc_micro` (V1 已废弃); 单位混用 → 测试 fail
  - **[NEW] timestamp_ms 唯一性:** paper fill 的 timestamp_ms 在 1ms 内不可重复 (V2 唯一性机制替代 nonce)
  - **过: 偏差 <1bp + size 单位正确 + timestamp_ms 唯一**

#### 1.1.7 M1-E01 — audit_id 完整链路复盘

**v2 修订 [ABI-UPDATE]:**
- `M1-E01` audit_id 完整链路复盘 | Integration+Manual | 老唐+老雷
  - 原 6 段齐 + <10s (不变)
  - **[NEW] 复盘 record 必含 ABI v0.5 新字段:** `token_id` / `outcome` (uint8) / `side` (uint8) 三字段在 audit record 可查, 0 缺失
  - **[NEW] V2 signer 字段:** `timestamp_ms` / `metadata` / `builder` 在 signer audit 段可见
  - **过: 6 段齐 + <10s + 新字段 6 项 100%**

### 1.2 不受 ABI v0.5 影响的 M1 条款 (维持 v1)

以下条款的验收内容与 ABI 字段无直接绑定, v2 维持 v1 原文不改:

`M1-A01/A02/A05/A06/A07/A08` (RM 状态机 / 告警 / 性能)
`M1-B01~B07` (数据接入, 与 OrderIntent 字段无关)
`M1-C02/C03/C04/C06` (LiveSection 分类 / 信号延迟 / snapshot_id 唯一 / lookahead)
`M1-D01/D02/D03` (paper 端到端延迟 / WAL 隔离 / 不写真账本)
`M1-E02/E03/E04` (snapshot_id join / audit chain hash / 复盘导出)
`M1-F01~F06` (在线率 / WAL fsync / CPU affinity / R-12 / 延迟观测 / 72h 无崩溃)
`M1-G01~G04` (紧急操作 / halt / signer 异常)
`M1-H01/H02/H03` (PnL 看板)

**M1 38 条受影响汇总:** 7 条需更新 (`M1-A03 / A04 / C01 / C05 / D04 / D05 / E01`), 31 条维持 v1.

---

## 2. GM-PAPER-G v2 八条门禁 — 可验收 Acceptance Criteria

**背景:** 八条全满足 = paper 持续盈利门禁通过 (O3 done). 三方否决权: 老韩 (RM) / 小余 (数据 attestation) / 小梁 (Sharpe) 任一不签字 = 门禁不通过.

**验收窗口结构:** 14 日软验证 → 通过后进入 30 日正式窗口 → 30 日窗口结束后核算八条.

---

### G-00 软验证前置 Gate (14 日)

`G-00` 14 日软验证 pre-gate | 长跑 | 小郑+老吴+小梁
- **如何测:** paper runtime 在 14 日内持续运行, 日末统计 net PnL (扣 fee+slippage+spread); 14 日累计净 PnL > 0 且无单日亏损 > 权益 3%
- **通过标准:** 14 日 (日历天, 非交易日) 累计净 PnL > 0 AND 每日亏损 ≤ 3% (以模拟权益计)
- **谁签字:** 小梁 (PnL 数字确认) + 小余 (数据无 look-ahead 确认)
- **失败处理:** 任一日触发 3% 亏损上限 → 14 日计时重置; 14 日结束 PnL ≤ 0 → 不进入 30 日正式窗口
- **过: 14 日 PnL > 0 AND 0 单日 >3% 亏损**

---

### G-01 连续窗口 (30 日正式)

`G-01` 30 日正式窗口连续运行 | 长跑 | 小郑+老吴
- **如何测:** paper runtime 在 30 日正式窗口内持续在线, uptime metric 每日记录; 窗口中断 (崩溃 / RM 失效 / 数据断流 >1h) → 以中断日为界判断是否重置
- **通过标准:** 30 日日历天内 paper runtime 在线率 ≥ 99.0% (M1-F01 标准), 窗口不中断 (中断 → 重置或豁免裁决上升 @老雷)
- **谁签字:** 老吴 (ops 基础设施) + 小郑 (监控确认)
- **失败处理:** 中断 → @老胡主持协商, 决定重置或豁免 (48h ack); 不可自动延期
- **过: 30 日在线率 ≥ 99.0% AND 窗口无未豁免中断**

---

### G-02 净 PnL > 0 (30 日累计)

`G-02` 30 日累计净 PnL > 0 | 统计+Integration | 小梁+小董+老唐
- **如何测:**
  - 数据来源: VirtualMatcher 撮合记录, 经 PnL 看板 (M1-H03) 汇总
  - 扣除项: fee 估算 (`sports_taker_fee_estimate = size_pUSD_micro * 0.03 * price * (1-price)`) + slippage (VirtualFill 公式) + spread cost (bid-ask spread × 成交量)
  - 计算: 30 日 SUM(net_pnl_per_fill) > 0
  - 数据对齐: PnL 数字 == SUM(virtual_fill.pnl) (M1-H03 标准, diff == 0)
- **通过标准:** 30 日净 PnL (扣 fee+slippage+spread) > $0 (不是毛利)
- **谁签字:** 小梁 (策略 net edge 确认) + 老唐 (audit chain PnL 对账确认) + **小余 (数据 attestation, 必要前置)**
- **过: SUM(net_pnl) > 0 AND audit chain diff == 0 AND 小余 attestation 已签**

---

### G-03 样本量与统计显著 (n_trades ≥ 100 + bootstrap CI)

`G-03` 样本量 + bootstrap Sharpe 显著性 | 统计 | 小梁+小董
- **如何测:**
  - Step 1: 统计 30 日窗口内 VirtualFill 记录数 n_trades
  - Step 2: 用 5000 次 bootstrap 重采样计算 daily Sharpe 分布, 取 95% CI 下界
  - Step 3: 对 30 日 net PnL 序列做 one-sample t-test vs 0 (单尾)
- **通过标准:**
  - n_trades ≥ 100 (填满交易日, 不是 1 日 100 笔; 需覆盖 ≥ 10 交易日各 ≥ 1 笔)
  - bootstrap Sharpe CI 下界 > 0 (5000 次重采样)
  - t-test p < 0.10 (单尾, 30 日窗口)
- **谁签字:** 小梁 (统计方法正确性) + 小董 (代码实施确认)
- **注意:** 30 日日历天 ≠ 统计显著 (GM-PAPER-G §3 原话: "老梁强调 30 日不等于统计显著, 看 n_trades"). 30 日内 n_trades < 100 → 门禁不通过, 不可豁免.
- **过: n_trades ≥ 100 AND CI_lower > 0 AND t p < 0.10**

---

### G-04 稳定性 (正收益日 ≥ 52% + 单日亏损上限 3%)

`G-04` 日级稳定性 | 统计 | 老韩+小梁+小董
- **如何测:**
  - 每日结算: 日末 SUM(net_pnl_that_day), 正为 win_day, 负为 loss_day
  - 单日亏损检查: SUM(net_pnl_that_day) < -(equity * 0.03) → 触发 kill switch 风险 (老韩 RM 主权, 见 GM §9 裁决 #1)
  - 胜率: win_days / total_days ≥ 0.52
- **通过标准:**
  - 正收益日占比 ≥ 52% (30 日内)
  - 0 单日亏损超权益 3% (任一触发 → 门禁失败, 因 RM kill switch 在 3% 处; 且 GM §9 #1 已裁决采用 3% 非 5%)
- **谁签字:** 老韩 (RM kill switch 合规确认) + 小梁 (日 PnL 数字确认)
- **过: win_rate ≥ 52% AND 0 单日 > 3% 亏损**

---

### G-05 统计显著 (OOS Sharpe ≥ 0.5 + t-test p < 0.10)

`G-05` OOS 统计显著性 | 统计 | 小梁+小董
- **如何测:**
  - 30 日窗口 daily PnL 序列 → 计算 annualized Sharpe = mean(daily_ret) / std(daily_ret) * sqrt(252)
  - t-test: one-sample t-test vs 0, p 值 (单尾)
  - 注: OOS = 30 日正式窗口, 非回测 IS
- **通过标准:**
  - OOS Sharpe ≥ 0.5 (30 日窗口, annualized)
  - t-test p < 0.10 (与 G-03 共用计算, 两条同时满足)
- **谁签字:** 小梁 (唯一签字, 量化研究主权)
- **注意:** Sharpe 与 n_trades 需同时满足 (G-03 + G-05 不可拆分); Sharpe ≥ 0.5 但 n_trades < 100 → 仍不通过
- **过: Sharpe ≥ 0.5 AND p < 0.10**

---

### G-06 分盘口核算 (pregame Moneyline 单独分桶)

`G-06` 盘口分桶 gate | 统计+Integration | 小梁+老钱+老彭
- **如何测:**
  - 每笔 VirtualFill 携带 market_type 标签 (pregame / inplay)
  - PnL 汇总按 market_type 分桶: `pnl_by_bucket = GROUP BY market_type`
  - G-02~G-05 全部基于 `market_type = 'pregame_moneyline'` 分桶单独计算
  - inplay 数据不计入 30 日 gate 核算 (GM-PAPER-G §3 + §8.3 一致结论)
- **通过标准:**
  - pregame Moneyline 分桶 PnL > 0 (单独计算, 不被 inplay 拉高/拉低)
  - market_type 标签 100% 覆盖 (无标签 fill → 拒入 gate 计算)
  - inplay fill 数量 / 总 fill 数量 在报告中单独展示 (透明度, 非 gate 条件)
- **谁签字:** 老钱 (CPO scope 确认: 2026 = pregame only) + 老彭 (inplay 风险确认, inplay 净 edge 50-65% 概率为负)
- **过: pregame 分桶单独 PnL > 0 AND 100% 标签覆盖**

---

### G-07 风控零失效

`G-07` RM 零失效 + paper 零污染真账本 | Integration+CI | 老韩+老唐+老高
- **如何测:**
  - RM 绕过检测: M1-A03 标准持续 enforce (CI grep + WAL audit)
  - paper 零污染: M1-D02/D03 标准持续 enforce (CI grep + 1h 窗 cross-write==0)
  - RM 拒单率: 拒单数 / 总 evaluate 数, 计算 30 日平均拒单率
  - R-11 验证: 30 日窗口内 paper WAL 与 live ledger 物理隔离, 0 交叉写
- **通过标准:**
  - RM 拒单率 ∈ [8%, 20%] (GM §9 裁决 #5: 老韩 RM 主权, [8%,20%] 区间合理; <8% 可能策略边界试探; >20% 过度拒单)
  - RM 绕过次数 == 0 (WAL audit 确认)
  - paper 污染真账本 == 0 (R-11)
- **谁签字:** 老韩 (RM 主权, 唯一可否决签字人)
- **过: 拒单率 ∈ [8%,20%] AND 绕过==0 AND R-11 zero**

---

### G-08 数据 attestation (30 日窗口附件)

`G-08` 数据完整性 attestation | Manual+Integration | 小余+小董
- **如何测:**
  - Step 1: 小余 (D 主管) 对 30 日窗口内所有 paper fill 的数据来源执行 attestation checklist:
    1. 4 时间戳契约 (R-20): event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts, 0 违例
    2. look-ahead 检测: signal 在 t 仅使用 data_source_ts ≤ t 数据 (无未来数据泄漏)
    3. feature_snapshot_id 与 fill 关联: 100% join, 0 孤儿
    4. Goalserve + Polymarket 数据源完整性: 30 日内无 silent drop (drop counter == 0, 或 drop 已记录 + 对应时段 fill 已排除)
  - Step 2: 小余出具签字 attestation 报告 (markdown, 附 4 checklist 结果)
  - Step 3: 无 attestation 报告 → G-02/G-03/G-05 PnL 数字视为未验证, 门禁不通过
- **通过标准:**
  - attestation 报告存在 + 小余签字 (D 主管主权)
  - 4 checklist 全绿: R-20 违例==0 + look-ahead==0 + join 0 孤儿 + 数据完整性确认
- **谁签字:** 小余 (D 主管, 具有否决权, 不签 = 门禁不通过)
- **过: 小余签字 attestation 报告 + 4 checklist 全绿**

---

### GM-PAPER-G 整体通过判定

`G-ALL` GM-PAPER-G 门禁通过 | Manual | 老雷+老钱
- **通过条件:** G-00~G-08 全部通过 (8 个正式 gate, G-00 为前置)
- **签字人与否决权:**
  - 老韩 (G-07 RM 零失效): 任一 RM 失效 → 否决
  - 小余 (G-08 数据 attestation): 未签字 → 否决
  - 小梁 (G-03/G-05 Sharpe): 统计不显著 → 否决
  - 老钱 (G-06 盘口分桶): scope 不符 → 否决
- **联决路径:** 老钱 co-sign → 老韩 + 小梁 确认 → 老雷 最终拍板 (O3 done)
- **失败后处理:** 任一 gate 失败 → 30 日窗口重置 (从失败日起重新计 14 日软验证)
- **过: 全部 gate 通过 + 四方无否决**

---

## 3. O1-O4 验收映射

### O1 管线贯通 — 验收口径

**目标:** 端到端 paper 管线无断点 (截止 2026-09-30)

| 验收项 | 判定标准 | 来源 M1 条款 | Owner |
|---|---|---|---|
| ABI v0.5 全链 merge | OrderIntent v0.5 + SignerV62 v6.2 在 paper binary 链路无 V1 残留; CI abi_lock v1.8 通过 | M1-A03 (ABI-UPDATE) | 老高+老孙 |
| RM v0.5 整合 | RM v0.5 evaluate() 在 paper runtime 启动时正常处理 v0.5 字段; 无 `market_id` / `is_buy` 旧字段 | M1-A01~A08 | 老韩+老沈 |
| audit replay 通 | 老唐 audit schema v1.4 replay 不丢字段; v1.3 backward compatible | M1-E01 (ABI-UPDATE) | 老唐 |
| REST 接真 state | paper runtime 启动后 REST 9 endpoint 响应真实数据 (非 stub) | M1-B01/B02 | 小卢 |
| paper runtime 启动 | 72h 无崩溃 (M1-F06 标准) + RM SAFE_MODE 启动 (M1-A01) | M1-F06/A01 | 老吴+小肖 |
| **O1 通过判定** | 上述 5 项全过 + 老周 (A 主管) sign-off + 老郭 架构 ok | — | 老周联决 |

**O1 vs paper runtime 关键区分 (GM §8.3):** O1 owner = A 单元, 交付"空管道" (paper 基座能启动 + 数据流通); **O3 管道里跑出正 PnL = C 单元 net edge + 全链协同**, O1 不承诺盈利.

---

### O2 盈利可证 — 验收口径

**目标:** PnL 看板 8/8 + paper 撮合可量化盈亏 (截止 2026-10-31)

| 验收项 | 判定标准 | 来源 M1 条款 | Owner |
|---|---|---|---|
| PnL 看板 8/8 | 8 个 PnL 看板 panel 全部在线 (M1-H01~H03 扩展, 见 §8.2 §4 E 主管 objective) | M1-H01/H02/H03 | 小苏+老胡 |
| VirtualMatcher Mode A | Mode A (market order 撮合) 上线, paper fill 有 avg_price + slippage | M1-D05 (ABI-UPDATE) | 小袁 |
| 14 日 paper 净 PnL 可出报表 | 给定 [t1,t2] 可导出 fills+rejects+signals (M1-E04); 含 fee+slippage 扣除明细 | M1-E04/H03 | 老唐+小苏 |
| PnL 与 audit chain 一致 | PnL 看板数字 == SUM(virtual_fill.pnl), diff == 0 (M1-H03) | M1-H03 | 老唐+小董 |
| **O2 通过判定** | 4 项全过 + 老胡 (E 主管) sign-off | — | 老胡联决 |

---

### O3 持续盈利 — 验收口径

**目标:** paper 模式连续盈利达 GM-PAPER-G 门禁 (截止 2026-12-31)

| 验收项 | 判定标准 | Gate 编号 | Owner |
|---|---|---|---|
| 14 日软验证通过 | G-00 通过 → 进入 30 日窗口 | G-00 | 小梁+小余 |
| 30 日净 PnL > 0 | G-02 通过 | G-02 | 小梁+老唐+小余 |
| 样本量与统计显著 | G-03 通过 (n_trades ≥ 100 + CI > 0 + p < 0.10) | G-03 | 小梁+小董 |
| 日级稳定性 | G-04 通过 (52% + 0单日 >3%) | G-04 | 老韩+小梁 |
| OOS Sharpe | G-05 通过 (Sharpe ≥ 0.5) | G-05 | 小梁 |
| 盘口分桶 | G-06 通过 (pregame only) | G-06 | 老钱+老彭 |
| RM 零失效 | G-07 通过 | G-07 | 老韩 |
| 数据 attestation | G-08 通过 + 小余签字 | G-08 | 小余 |
| **O3 通过判定** | G-00~G-08 全通过 + 老雷+老钱联决 + 无否决 | G-ALL | 老雷拍板 |

**9-30 checkpoint (GM §9 裁决 #8):** 2026-09-30 若 paper runtime 未启动 OR net edge 未出正数 → 触发全体争议会重排 O3, 不硬撞 12 月 deadline.

---

### O4 零浪费 — 验收口径

**目标:** Idle 率从 ~70% 降到 ≤ 20% (2026-06 月内持续 enforce)

| 验收项 | 判定标准 | 度量方式 | Owner |
|---|---|---|---|
| W10 idle 率 ≤ 40% | active persona 数 / total persona 数 ≥ 60%; idle = 无明确 ticket 且无合理 standby 理由 | 老胡周报 §6 | 老胡 |
| W12 idle 率 ≤ 25% | 同上, ≤ 25% | 老胡周报 §6 | 老胡 |
| 6 月底 idle 率 ≤ 20% | 同上, ≤ 20% | 老胡周报 §6 | 老胡 |
| standby 须有理由 | 每个 standby persona 写明前置依赖 + 解锁 ETA; 无理由视为 idle | PR / weekly sync | 各主管 |
| **O4 持续 enforce** | 每周老胡周报 §6 出 idle 名单; 任一主管 idle 名单不清零 + 无理由 → 升级 @老雷 | 周报 | 老胡 |

---

## 4. 缺口清单

### 4.1 当前 38 条中尚无对应实现的条款

以下条款在 v2 spec 时点 (2026-05-29) 无对应 C++ 实现, 属于 "spec 先行, 实现待补" 状态:

| 条款 | 缺口描述 | 预计解锁依赖 |
|---|---|---|
| `M1-C01` (ABI-UPDATE) | OrderIntent v0.5 字段 schema 校验未进 ctest; 旧 is_buy 残留未清 | 老沈 W10 W3 ctest migration 完成 |
| `M1-D05` (ABI-UPDATE) | VirtualMatcher 尚未实现 (小袁 W11); size_pUSD_micro 字段 V2 迁移未验 | 小袁 W11 FillRateModel + VirtualMatcher |
| `M1-E01` (ABI-UPDATE) | audit schema v1.4 (timestamp_ms/metadata/builder) 未落盘 | 老唐 W10 W4 audit schema v1.4 |
| `M1-H01/H02/H03` (PnL 看板) | PnL 看板 0/8 (老胡 audit 重灾区); 前端 REST 依赖 W10 W3 | 小苏 9~10-31 前端看板 |
| `M1-F01` (在线率 ≥99%) | paper runtime 基座未启动 (小肖 9-12 骨架); 无法 measure | paper runtime 9-30 启动 |
| `M1-B02` (Goalserve inplay) | 实测 3 sport live event 接入未完成 (小冯 8-15) | 小冯 W? ETL 实时 feed |
| `G-03/G-05` (bootstrap Sharpe CI) | 统计框架 (小董 W10 W3 stats validation) 未完成 | 小董 8-31 stats validation framework |

### 4.2 Paper 阶段专属条款 (M4.5/M5 才能验)

以下 M1 条款在 paper 阶段有限制, 需等后续阶段才能完整验收:

| 条款 | Paper 阶段限制 | 完整验收阶段 |
|---|---|---|
| `M1-A02` (三签解锁) | paper 阶段三签流程是否需等 paper runtime 上线? | M1 paper runtime 启动后可验 |
| `M1-D02/D03` (R-11 paper/live 隔离) | live ledger 不存在于 paper 阶段; 只能验 paper WAL 不含 live 写调用 | M4.5 (live 引入后完整验) |
| `M5-A16` (halt 后 open position 处置) | v1 §9.1 标注 open question, 老钱 + 老韩 + 老雷 未联决 | M5 实盘前必须联决 |

### 4.3 GM-PAPER-G 门禁专属缺口

| 缺口 | 描述 | 依赖 |
|---|---|---|
| VirtualMatcher Mode A 未上线 | G-02/G-06 PnL 核算依赖 VirtualMatcher 撮合; 当前无法跑 30 日窗口 | 小袁 W11 |
| PnL 看板 0/8 | G-02 PnL 报表依赖看板; 当前无输出口 | 小苏 10-31 |
| stats validation framework | G-03/G-05 bootstrap CI 计算依赖框架 | 小董 8-31 |
| 数据 attestation checklist 流程 | G-08 小余签字 SOP 未成文 | 小董 10-15 data attestation |
| pregame/inplay 分桶标签 | G-06 要求每笔 fill 有 market_type 标签; 当前未实现 | 信号层 + VirtualMatcher (C单元) |

---

## 5. 澄清清单 (open questions, 待拍板)

以下问题在 v2 spec 时点尚未有 GM/CPO 明确结论, 小颖挂单等回复:

| # | 问题 | 影响条款 | 问谁 | 时限 |
|---|---|---|---|---|
| Q1 | M1-H01~H03 PnL 看板"8/8"中 8 个 panel 的完整清单是否已定义? 当前 v1 只见 3 条 (H01/H02/H03). | M1-H 全系 / O2 | 老胡 + 小苏 | W10 W4 |
| Q2 | GM-PAPER-G G-00 软验证 14 日中断如何计算? 周末 / 非交易日是否算天数? | G-00 | 老梁 | W10 W4 |
| Q3 | G-04 正收益日占比 52%: "日"的定义是 UTC 0:00 还是美东市场结束时间? | G-04 | 老韩 + 小梁 | W10 W4 |
| Q4 | G-07 拒单率 [8%, 20%] 区间: 分母是"所有 evaluate 次数"还是"仅有信号触发时"? | G-07 | 老韩 | W10 W3 |
| Q5 | M5-A16 (halt 后 open position 处置): 老钱 + 老韩 + 老雷 何时联决? | M5-A16 / M4.5 | 老钱 | M5 实盘前 |
| Q6 | ABI v0.5 → v0.6 (SignerV62 新增 timestamp_ms/metadata/builder): 这三个字段是否需要补进 OrderIntent v0.6 并触发 M1 acceptance 条款的第二轮 ABI-UPDATE? (老韩 W10 W2 配套) | M1-A03/C05/E01 | 老韩 | W10 W2 |

---

## 6. 自检 (小颖)

- [x] 读 §2 O1-O4 + §3 GM-PAPER-G v2 八条 (权威输入)
- [x] grep 现有 acceptance spec v1 (38 条全读)
- [x] 读 ABI v0.5 spec (老韩 W9 + 老孙 W10 W1)
- [x] 标注 7 条受 ABI 影响条款 (M1-A03/A04/C01/C05/D04/D05/E01)
- [x] 31 条维持 v1 的条款明确说明不修改
- [x] GM-PAPER-G 八条全部翻译为可验收 criteria (如何测 / 通过标准 / 谁签字)
- [x] G-00 软验证前置 gate 单独列出
- [x] G-ALL 整体通过判定含四方否决权
- [x] O1-O4 验收映射表完整
- [x] 9-30 checkpoint 写入 O3 映射
- [x] 缺口清单分三类: 无实现 / paper 专属 / 门禁专属
- [x] 澄清清单 6 项挂单
- [x] 不写代码 / 不写 endpoint / 不写 PRD / 不跟进 ticket
- [x] 每条 acceptance testable + measurable (无主观)
- [x] owner + last_review 开头

---

**最后更新:** 2026-05-29 by 小颖 (requirements-analyst, E 产品业务保障部)
**下次 review:** ABI v0.6 字段落定 (老韩 W10 W2) + Q1~Q6 拍板后做第三轮对齐
**升级路径:** 冲突 / 缺口 → @老胡 (24h ack) → @老雷 (48h 不下升级)
