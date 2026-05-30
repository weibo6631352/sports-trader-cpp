# Phase B SelectSide 算法 spec v1

> **owner:** 小梁 (量化研究部主管, Sharpe/Kelly 主权)
> **last_review:** 2026-05-31
> **status:** 主权拍板 — GM 可直接照此落地，无需再上会确认
> **触发:** GM 落地 Phase B 前发现现网 edge 锚定方式与双边设计假设存在关键差异，需主权仲裁后方可实现
> **关联文档:**
> - `src/stcpp/paper/paper_loop.cpp` (现网实现)
> - `docs/RESEARCH/xiaoliang-binary-dual-side-strategy-v1.md` (策略仲裁 v1)
> - `docs/RESEARCH/xiaoyuan-binary-dual-side-decision-v1.md` (小袁微观结构设计)
> - `docs/RESEARCH/laohan-phaseB-buy-no-rm-checklist-v1.md` (老韩 RM checklist)
> - `docs/ADR/2026-05-29-adr-042-kelly-sizing.md`

---

## 0. TL;DR (GM 落地直读)

| 问题 | 拍板结论 |
|---|---|
| edge 定义 | **沿用 de-vig 锚定**。raw_edge = p_fair - p_market_devig，符号对称，禁止改回 fair-ask 锚定 |
| 两边 raw_edge 关系 | 精确相反数。raw_edge_NO = -raw_edge_YES，不可能双边同正（正常市场） |
| SelectSide 判据 | 看 raw_edge_YES 符号：正 → YES 候选；负 → NO 候选；再用 edge_ci 确认 |
| edge_ci 对称性 | edge_ci_yes 与 edge_ci_no 的 CI 项对称（两边 p_fair 互余，方差相等），因此 `|edge_ci_yes| == |edge_ci_no|`，选边等价于看符号 |
| Kelly f* 与 edge_ci 选边等价性 | de-vig 对称下完全等价——两边 f* 幅度相同，选边 = 看 edge_ci 符号。无需分别算两遍 Kelly 再比大小 |
| TickOne 重构时序 | 先算 p_fair_yes + p_market_devig（始终 YES-canonical）→ 选边 → 切被选边 book/ts/depth → sizing + intent |
| sizing 入参 | price=被选边 ask；fair_value=被选边 p_fair；edge_ci_lower=被选边 edge_ci；buy_yes=(选YES) |
| per-book slippage/depth | 被选边的 best_ask_size()；intent.book_depth_l1 切被选边 |
| 互斥 + cap | 每 tick 每 condition 只产一个 intent；condition cap 两边合并（老韩已确认天然合并，RM 零改） |
| C4 反向 fill 测试 | Phase B 真选边后可做：构造 p_fair_yes < p_market_devig 场景 → 选 NO → 验 intent.outcome=No |

---

## 1. edge 定义拍板：沿用 de-vig 锚定

**结论：Phase B 沿用现网 de-vig 锚定，不改回 fair-ask 锚定。**

### 1.1 现网 de-vig 锚定的定义（粘原文，禁转述）

`paper_loop.cpp:448`：
```cpp
const double edge_ci_lower = ComputeEdgeCiLower(p_fair, p_market_devig, cfg_.n_effective, cfg_.z_90);
// p_fair = blend(score_prior, p_market_devig, conf)  -- YES-canonical
// p_market_devig = devig_binary(yes_mid, no_mid)     -- 双边 mid 去 vig 的市场共识
// sz_in.price = best_ask                             -- 执行价是真实 ask
```

`ComputeEdgeCiLower`（`paper_loop.cpp:681-698`）：
```cpp
// edge_ci_lower = (p_fair - p_ask) - z * sqrt(p_fair * (1 - p_fair) / n_eff)
// 注：p_ask 参数在当前调用处传的是 p_market_devig，不是 best_ask
```

P1-8 设计注释（`paper_loop.cpp:446-448`）：
> 「锚在 de-vig 市场概率, 不再用带 vig 的 best_ask, 杜绝 overround 假 edge」

### 1.2 为何不改回 fair-ask 锚定

GM 问得对：原始 kelly-sizing-spec-v1.md §1.1 写的是 `edge = fair - ask`（fair 减执行价）。这与现网 de-vig 锚定不同。我在此明确仲裁**不回退**，理由：

**理由一：ask 含 vig，系统性制造假 edge。**

ask_YES 由 maker 报价，通常 ask_YES + ask_NO > 1（存在 overround）。若 edge = fair_YES - ask_YES，则：

```
edge_YES_ask = fair_YES - ask_YES
edge_NO_ask  = fair_NO  - ask_NO = (1-fair_YES) - ask_NO
```

两边相加：
```
edge_YES_ask + edge_NO_ask = 1 - (ask_YES + ask_NO)
```

当市场存在 vig（ask_YES + ask_NO > 1），两边 edge 之和为负。但由于 ask_YES 和 ask_NO 各自独立定价，可能出现 `edge_YES_ask > 0` 同时 `edge_NO_ask > 0` 的假象（小袁 §1.4 数据：cross-side vig 中位 0.1-2.5¢，不均匀分布）。

P0-3 dogfood 验证了这个问题：Spain outright mid=0.169，ask 含 vig，fair-ask 算出 edge_bps=1076，强诱导假 intent。de-vig 锚定后 edge 归零，阻断假信号。

**理由二：de-vig 锚定给出 coherent 对称结构，双边选边数学更干净。**

```
p_market_devig = yes_mid / (yes_mid + no_mid)     -- 两边去 vig 后的市场共识 YES 概率
1 - p_market_devig                                 -- 天然等于 NO 的市场共识概率
```

因为 p_fair_NO = 1 - p_fair_YES（见小袁 §1.3 强调的 coherent 要求），且 p_market_devig_NO = 1 - p_market_devig_YES，则：

```
raw_edge_NO = p_fair_NO - p_market_devig_NO
            = (1 - p_fair_YES) - (1 - p_market_devig_YES)
            = -(p_fair_YES - p_market_devig_YES)
            = -raw_edge_YES
```

**两边 raw edge 是精确相反数。** 在正常市场中不可能双边同正（与 ask 锚定不同）。选边逻辑从「比较两边的正 edge」变为「看 raw_edge_YES 的符号」，数学更纯粹，不存在双边假 edge 的病态。

**理由三：P1-8 设计已被验证，不引入新坑。**

de-vig 锚定在 dogfood 中经历了 P0-3 修复，现网稳定。fair-ask 锚定需要重新论证 overround 问题，而 de-vig 锚定已有现成防护（`devig_ok` gate，双边无效则 nullopt）。

**结论（不可再议）：** Phase B 沿用 de-vig 锚定。raw_edge = p_fair - p_market_devig。执行价（sz_in.price = best_ask）仍用真实 ask，但 edge 定义不变。ask 只影响 Kelly 分母（`1 - c` 或 `c`），不影响 edge gating。

---

## 2. SelectSide 精确算法

### 2.1 核心代数（de-vig 对称下的精确结论）

设：
```
p_fair_yes     = blend(score_prior_yes, p_market_devig, conf)  -- YES-canonical fair
p_market_devig = devig_binary(yes_mid, no_mid)                 -- 市场共识（双边去 vig）
p_fair_no      = 1.0 - p_fair_yes                              -- 严格互余，不独立计算
p_market_devig_no = 1.0 - p_market_devig                       -- 严格互余

raw_edge_yes = p_fair_yes - p_market_devig
raw_edge_no  = -raw_edge_yes                                   -- 精确相反数

sigma         = sqrt(p_fair_yes * (1 - p_fair_yes) / n_eff)   -- p_fair_yes*(1-p_fair_yes)
                                                               -- == p_fair_no*(1-p_fair_no)
                                                               -- 方差相等（互余性质）

edge_ci_yes  = raw_edge_yes - z * sigma
edge_ci_no   = raw_edge_no  - z * sigma
             = -raw_edge_yes - z * sigma
             = -(raw_edge_yes + z * sigma)
             = -(edge_ci_yes + 2 * z * sigma)       -- 两者不是相反数
```

**关键结论：edge_ci_yes 和 edge_ci_no 的 CI 下界之和 = -(2 * z * sigma) < 0。**

因此：
- 若 raw_edge_yes > z*sigma，则 edge_ci_yes > 0，edge_ci_no < -(2*z*sigma) < 0 → 选 YES
- 若 raw_edge_yes < -z*sigma，则 edge_ci_yes < 0，edge_ci_no > 0 → 选 NO
- 若 |raw_edge_yes| <= z*sigma，则两边 edge_ci 都 <= 0 → 不交易

**在 de-vig 对称结构下，两边 edge_ci 不可能同时 > 0。选边唯一由 raw_edge_yes 的符号和幅度决定。**

### 2.2 Kelly f* 与 edge_ci 选边的等价性（拍板）

GM 问的问题：「选 f* 大者（之前说的）vs 选 edge_ci 大且 > 0 的边，在 de-vig 对称下是否一致？」

**答：完全一致，等价。** 证明如下：

买 YES 的 Kelly f*（当 edge_ci_yes > 0 时）：
```
f*_yes = net_ci_edge_yes / (1 - ask_YES)
       = (edge_ci_yes - fee) / (1 - ask_YES)
```

买 NO 的 Kelly f*（当 edge_ci_no > 0 时）：
```
f*_no = net_ci_edge_no / (1 - ask_NO)
      = (edge_ci_no - fee) / (1 - ask_NO)
```

在 de-vig 对称下，两边不可能 edge_ci 同时 > 0（已证），所以「比较 f*」退化为「只有一边有正 f*」。

即使在罕见场景（ask_YES + ask_NO < 1，套利机器人已清除，实测 0 hit），若两边均显正 edge_ci（实际是数值噪声或 fair 不 coherent 导致），Kelly 判据等价于 edge_ci 判据：较大 edge_ci 的那边 f* 也更大（分母 ask_YES 和 ask_NO 相互补偿，不改变 argmax）。

**结论：不需要分别算两遍 Kelly 再比 f*，直接看 edge_ci_yes 的符号即可。这是 de-vig 对称结构赋予的简化，不是偷懒。**

### 2.3 SelectSide 精确算法（GM 落地用）

```
输入：
  p_fair_yes       -- YES-canonical fair（blend 后）
  p_market_devig   -- 双边去 vig 市场共识
  n_eff            -- cfg_.n_effective
  z                -- cfg_.z_90（CI 置信系数）

计算：
  raw_edge_yes = p_fair_yes - p_market_devig

  sigma = sqrt(p_fair_yes * (1.0 - p_fair_yes) / n_eff)
  -- 注：p_fair_yes*(1-p_fair_yes) == p_fair_no*(1-p_fair_no)，方差对称

  edge_ci_yes = raw_edge_yes - z * sigma      -- ComputeEdgeCiLower(p_fair_yes, p_market_devig, n, z)
  edge_ci_no  = -raw_edge_yes - z * sigma     -- 对称推导，无需独立调用

选边：
  if edge_ci_yes > 0:
      return {TradedSide::Yes, Side::Buy, conviction=edge_ci_yes}

  elif edge_ci_no > 0:   // 等价于 raw_edge_yes < -z*sigma
      return {TradedSide::No, Side::Buy, conviction=edge_ci_no}

  else:
      return {TradedSide::None}  // 两边无 edge，不交易（fail-closed）
```

**实现备注：**
- `edge_ci_yes` 直接复用现网 `ComputeEdgeCiLower(p_fair, p_market_devig, n, z)`，入参不变。
- `edge_ci_no` = `-raw_edge_yes - z * sigma`。可以用 `ComputeEdgeCiLower(1.0 - p_fair_yes, 1.0 - p_market_devig, n, z)` 等价计算（代入互余值），或直接用上述代数公式，两者数值等价。
- `conviction` 字段填被选边的 edge_ci（`DecisionSide.conviction` 字段已存在于 binary_market_snapshot.hpp）。
- `SelectSide` 函数签名中入参是 `BinaryMarketSnapshot`，但 p_fair_yes 和 p_market_devig 需要先在 TickOne 算好再传进去，或者 SelectSide 内部重新算。见第 3 节时序安排。

---

## 3. TickOne 重构时序（GM 落地用）

### 3.1 当前时序（Phase A，SelectSide 恒 YES）

```
Step 0: SelectSide (桩恒 YES)
Step 1: 取被交易边 book (始终 YES book)
Step 2: FairValueEstimator (用 YES book 入参)
        → p_fair = blend(prior, p_market_devig, conf)  -- YES-canonical
Step 3: ComputeEdgeCiLower(p_fair, p_market_devig, n, z)
        SizingCalculator.compute(sz_in)
Step 4: PublishQuoteSnapshot
Step 5: 构造 intent (outcome=Yes, token=YES, price=ask_YES)
```

问题：fair 在 SelectSide 之后算，SelectSide 用不了 fair 信息（所以才是恒 YES 桩）。

### 3.2 重构后时序（Phase B，真选边）

**原则：fair 计算始终 YES-canonical，选边是纯代数判断（无需跑两遍 FairValueEstimator）。**

```
─── 前置（始终执行，不依赖选边）───────────────────────────────────

Step A: 双边 book 读取（TickAll 已做，BinaryMarketSnapshot.yes.book + .no.book）
        yes_feat = mkt.yes.book    -- YES book（始终用于 FairValueEstimator）
        no_feat  = mkt.no.book     -- NO book（选 NO 时切换 ask/depth/ts）

Step B: 双边 mid 提取 + de-vig（始终 YES-canonical）
        yes_mid = yes_feat.microprice（或 feat.mid fallback，现网已有）
        no_mid  = no_feat.microprice（或 no_feat.mid fallback，从 mkt.no.book 取）
        p_market_devig = devig_binary(yes_mid, no_mid)   -- nullopt → fail-closed

Step C: FairValueEstimator（始终用 YES book 入参）
        game_row 照旧（Goalserve score/clock，A1 逻辑不变）
        book_row.token_side = "YES"（始终 YES side，FairValueEstimator 不支持 NO token 直接输入）
        fv_result = fv_estimator_.estimate(game_row, &book_row)
        -- 结果：fv_result.p_yes() = p_prior_yes（仅先验，blend 在下方）

Step D: p_fair_yes 计算（blend，现网逻辑不变，始终 YES-canonical）
        p_fair_yes = blend_prob(fv_result.prior_yes, p_market_devig, conf)
        -- 注：NO 的 fair 不独立计算，严格用 1 - p_fair_yes

─── SelectSide（p_fair_yes + p_market_devig 已知，可选边）──────────

Step E: SelectSide
        输入：p_fair_yes, p_market_devig, n_eff, z
        输出：DecisionSide {outcome=Yes/No/None, side=Buy, conviction}
        算法：见 §2.3

        if outcome == None: return（两边无 edge）

─── 以下按选边切换（原 Step 1-8 的参数化）─────────────────────────

Step F: 切换被选边 book
        is_yes = (decision.outcome == Yes)
        traded_feat = is_yes ? yes_feat : no_feat    -- 被选边 book
        token_id    = is_yes ? yes_token_id : no_token_id
        best_ask    = traded_feat.best_ask()
        book_depth_l1 = traded_feat.best_ask_size()（或 fallback）

        -- 被选边有效性校验（现网 L1 gate 逻辑，按被选边 book）
        if !isfinite(best_ask) || best_ask <= 0 || best_ask >= 1: return
        -- 4ts 从被选边透传（R-20）

Step G: edge_ci 按选边切
        if is_yes:
            edge_ci_lower = ComputeEdgeCiLower(p_fair_yes, p_market_devig, n, z)
            p_fair_selected = p_fair_yes
        else:
            raw_no = -(p_fair_yes - p_market_devig)    -- = 1-p_fair_yes - (1-p_market_devig)
            sigma  = sqrt(p_fair_yes * (1-p_fair_yes) / n_eff)
            edge_ci_lower = raw_no - z * sigma
            p_fair_selected = 1.0 - p_fair_yes

Step H: SizingInput 构造（按选边切）
        sz_in.fair_value         = p_fair_selected     -- YES: p_fair_yes; NO: 1-p_fair_yes
        sz_in.price              = best_ask            -- 被选边 ask
        sz_in.edge_ci_lower      = edge_ci_lower       -- 被选边方向 CI 下界
        sz_in.edge_bps           = |p_fair_yes - p_market_devig| * 10000  -- 毛 edge（绝对值，展示）
        sz_in.buy_yes            = is_yes
        sz_in.bankroll_usdc      = cfg_.bankroll_usdc
        -- exposure 读 position_ledger（按 token_id 切，现网 c4 逻辑不变）
        -- fill_rate, slippage_bps 现网 M1 固定值（0.65 / 8.0），未来接被选边 book model

Step I: SizingCalculator.compute（接口不变，入参已按选边填好）

Step J: PublishQuoteSnapshot（接口不变，注意 has_real_fair gate 在此之后维持不变）

Step K: advisory gate（P0-4，不变）

Step L: 构造 OrderIntent（按选边切）
        intent.outcome          = is_yes ? Outcome::Yes : Outcome::No
        intent.token_id         = token_id                     -- 被选边 token
        intent.price            = best_ask                     -- 被选边 ask
        intent.book_depth_l1_usdc = book_depth_l1 * 1e6       -- 被选边 L1 depth（micro）
        intent.book_snapshot_ts_ns = traded_feat.ingestion_ts_ns  -- 被选边 ts（R-20）
        intent.side             = decision.side                -- Buy（M1 只买）
        -- 其余字段（4ts/condition_id/size_pUSD_micro/...）不变

Step M: RiskGateway::evaluate（不变）
Step N: PaperSigner::Sign（不变）
Step O: VirtualMatcher（不变，vord.outcome 按选边）
Step P: PositionLedger::apply_fill（按选边 token_id/outcome）
Step Q: FeedRiskGateway（不变，遍历 ledger token-agnostic）
```

### 3.3 什么量用 YES-canonical，什么量用被选边

| 量 | 方向 | 理由 |
|---|---|---|
| p_fair_yes | 始终 YES-canonical | FairValueEstimator 只支持 YES token 输入；p_fair_no = 1 - p_fair_yes 严格互余 |
| p_market_devig | 始终 YES-canonical | devig_binary 返回的是 YES 概率 |
| fv_result（FairValueEstimator 输出）| 始终用 YES book 入参 | BaselineFairValueModel.extract_microprice 检查 token_side == "YES" |
| p_prior_yes（blend 输入）| 始终 YES-canonical | score_diff/time_frac 由 game_row 提供，YES 方向先验 |
| edge_ci_lower | **按选边切** | 选 YES 用 ComputeEdgeCiLower(p_fair_yes, p_market_devig)；选 NO 用对称公式 |
| sz_in.fair_value | **按选边切** | YES → p_fair_yes；NO → 1 - p_fair_yes |
| sz_in.price | **按选边切** | 被选边 ask |
| book_depth_l1 | **按选边切** | 被选边 best_ask_size() |
| 4ts（step L） | **按选边切** | traded_feat.ingestion_ts_ns 等（R-20 透传） |
| intent.outcome / token_id | **按选边切** | YES/NO token |

---

## 4. sizing 入参按选边切（确认）

`SizingCalculator::compute` 接口本身不变，只需调用方填好被选边的参数。

```cpp
// Phase B 新填法（选 NO 时）：
sz_in.fair_value     = 1.0 - p_fair_yes;   // p_fair_no
sz_in.price          = no_feat.best_ask(); // ask_NO
sz_in.edge_ci_lower  = edge_ci_no;         // NO 方向 CI 下界（> 0 才到这里）
sz_in.buy_yes        = false;              // Kelly 分母用 ask_NO（= c，即 /c）

// SizingCalculator::kelly_denom_：buy_yes=false → denom = c = ask_NO   ✓
```

`SizingCalculator.kelly_denom_`（`sizing_calculator.hpp:158-160`）：
```cpp
[[nodiscard]] static double kelly_denom_(double c, bool buy_yes) noexcept {
    return buy_yes ? (1.0 - c) : c;
}
```

`buy_yes=false` 时分母 = c = ask_NO，这正是买 NO Kelly 公式的正确分母（`f*_no = net_ci_edge / ask_NO`）。老韩联签（`laohan-kelly-cap-cosign-v1.md`）已覆盖此路径。**SizingCalculator 不需要改动。**

---

## 5. per-book slippage / depth（小袁 C2）

**结论：被选边 book_depth_l1 = 被选边 best_ask_size()。intent.book_depth_l1 切被选边。**

老韩已在 `laohan-phaseB-buy-no-rm-checklist-v1.md §4` 确认：RM check_liquidity_ 纯消费 intent 字段，不区分 YES/NO。但 **paper_loop 喂数侧必须填对**：

```cpp
// 选 YES：（现网逻辑，不变）
const double book_depth_l1 = yes_feat.best_ask_size();   // YES L1
intent.book_depth_l1_usdc  = book_depth_l1 * 1e6;
intent.book_snapshot_ts_ns = yes_feat.ingestion_ts_ns;

// 选 NO：（Phase B 新增）
const double book_depth_l1 = no_feat.best_ask_size();    // NO L1，不借 YES 的
intent.book_depth_l1_usdc  = book_depth_l1 * 1e6;
intent.book_snapshot_ts_ns = no_feat.ingestion_ts_ns;    // R-20：被选边 ts
```

fallback 逻辑（no_feat.best_ask_size() 无效时）同现网 YES 的 `1000.0 pUSD`。

---

## 6. 互斥 + cap

**每 tick 每 condition 只产一个 intent（选一边）。禁止同 condition 同 tick 同时产 YES intent + NO intent。**

SelectSide 函数返回唯一结果（Yes / No / None），TickOne 按此结果只走一条路，只构造一个 intent。结构上天然互斥，无需额外 gate。

老韩已确认（`laohan-phaseB-buy-no-rm-checklist-v1.md §2`）：`condition_exposure_[condition_id]` 按 condition_id 聚合，YES fill 和 NO fill 进同一个桶，两边合并 notional 自动满足小梁 §3.2 要求，RM 零改。

老韩 B-8 说明（粘原文）：
> 「互斥是策略纪律，cap 合并是 RM 兜底，两者都要。」

Phase B 落地时互斥在 TickOne 逻辑层 enforce（结构保证），cap 合并由 RM 兜底（天然支持）。

---

## 7. C4 反向 fill 测试（Phase A 建不起 NO intent 的根因 + Phase B 修法）

### 7.1 Phase A 为何建不起 NO intent

Phase A SelectSide 桩恒返 `{TradedSide::Yes, Side::Buy, 0.0}`，`is_yes = true`，永远走 YES book，永远构造 `intent.outcome = Outcome::Yes`。即使 NO 被低估，代码路径也不走到 NO intent 构造。

### 7.2 Phase B 后的测试设计

构造条件：让 `p_fair_yes < p_market_devig`，即 raw_edge_yes < 0，SelectSide 应选 NO。

```
测试场景：
  p_fair_yes = 0.35     -- 模型认为 YES 被高估（fair 低于市场共识）
  p_market_devig = 0.40 -- 市场共识（双边去 vig 后）
  
  raw_edge_yes = 0.35 - 0.40 = -0.05 < 0   → NO 方向有 edge
  p_fair_no = 0.65, p_market_devig_no = 0.60
  raw_edge_no = 0.65 - 0.60 = +0.05 > 0

  n_eff = 30, z = 1.645
  sigma = sqrt(0.35 * 0.65 / 30) ≈ 0.0871
  edge_ci_no = 0.05 - 1.645 * 0.0871 ≈ 0.05 - 0.143 = -0.093  < 0  -- CI 太宽，仍无 edge

调整为足够强的信号：
  p_fair_yes = 0.30, p_market_devig = 0.50
  raw_edge_yes = -0.20
  sigma = sqrt(0.30 * 0.70 / 30) ≈ 0.0742
  edge_ci_no = 0.20 - 1.645 * 0.0742 ≈ 0.20 - 0.122 = +0.078 > 0  ✓

  -- 此时 SelectSide 应返回 {TradedSide::No, Side::Buy, conviction=0.078}
```

**测试步骤：**
1. 构造 BinaryMarketSnapshot，YES book ask=0.52，NO book ask=0.45（模拟）
2. 注入 game_row（has_real_fair=true，time_status=InPlay）
3. 注入 score_store 返回比分使得 fair_yes ≈ 0.30（NO 方向被低估）
4. 禁用 advisory gate（cfg_.advisory_markets_no_intent = false）
5. 运行 TickOne
6. 验证：intent.outcome == Outcome::No，intent.token_id == NO_token，intent.price ≈ ask_NO
7. 验证：sizing 产正 notional（edge_ci_no > 0 → SizingCalculator.valid=true）
8. 验证：NO intent 经过 RM evaluate，无意外 reject（老韩 T-NO-1 覆盖此路径）

**这是解决「Phase A 建不起 NO intent」的直接路径。Phase B 真选边后，此测试是第一个端到端 NO intent 验证点。**

---

## 8. 给 GM 的实施清单（优先级排序）

Phase B 落地步骤（严格按此顺序，每步做完验一次 ctest）：

1. **实现 SelectSide（paper_loop.cpp，约 20 行）**
   - 入参：`BinaryMarketSnapshot + p_fair_yes + p_market_devig + cfg_.n_effective + cfg_.z_90`
   - 或：SelectSide 内部重算（直接读 mkt 中的 yes/no book mid，调 devig_binary，再 blend）
   - 推荐：SelectSide 接受 p_fair_yes + p_market_devig（已在 TickOne 算好，不重复计算），见 §2.3

2. **TickOne 重构时序（paper_loop.cpp）**
   - 把 p_fair_yes + p_market_devig 计算移到 SelectSide 之前（现在在之后）
   - SelectSide 调用移到 Step B/C/D 之后
   - Step F 起按 is_yes 切 book（traded_feat）
   - 注意：FairValueEstimator 调用保持 YES book 入参不变（不能因选 NO 就换成 NO book 喂给 FV）

3. **edge_ci 按选边切（paper_loop.cpp）**
   - 选 YES：现网逻辑不变（ComputeEdgeCiLower(p_fair_yes, p_market_devig, n, z)）
   - 选 NO：新增公式（raw_no = -raw_edge_yes，sigma 同，edge_ci_no = raw_no - z*sigma）

4. **sz_in / intent 字段按选边切（paper_loop.cpp）**
   - fair_value、price、edge_ci_lower、buy_yes、book_depth_l1、book_snapshot_ts、outcome、token_id
   - 参照 §3.2 Step H / Step L

5. **补 C4 反向 fill 测试（§7.2 测试设计）**
   - 验 NO intent 从 SelectSide 到 apply_fill 全链路

6. **老韩 checklist B-6/B-7/B-8 确认**
   - B-6：intent 字段全切 NO book（代码 review）
   - B-7：NO book freshness 喂数（`set_token_book_freshness_ms(token1, ...)`，从 TickOne 里加）
   - B-8：互斥 enforce 代码 review

---

## 附录：Phase B 变更影响范围

| 文件 | 变更 | 备注 |
|---|---|---|
| `src/stcpp/paper/paper_loop.cpp` | SelectSide 实现（约 20 行）+ TickOne 时序重构（约 40 行） | 唯一改动文件 |
| `include/stcpp/paper/paper_loop.hpp` | SelectSide 签名可能调整 | 若传参方式变则改 |
| `include/stcpp/paper/binary_market_snapshot.hpp` | 不需要改 | DecisionSide.conviction 字段已存在 |
| `include/stcpp/sizing/sizing_calculator.hpp` | **不需要改** | buy_yes 已支持 false 路径 |
| `include/stcpp/risk/risk_gateway.hpp` | **不需要改** | RM token-agnostic，老韩确认零改 |
| `src/stcpp/risk/position_ledger.cpp` | **不需要改** | condition_exposure_ 已按 condition_id 聚合 |
| 测试文件 | 补 T-PL-NO-1/2/3 + C4 反向 fill 测试 | 老韩 §8.3 已有设计 |

**不变的：** FairValueEstimator 调用方式、blend_prob 逻辑、has_real_fair gate、advisory gate、P0-3/P0-4 整改、R-20 4ts 透传、RM evaluate 链路、PaperSigner、VirtualMatcher。

---

**最后更新：** 2026-05-31 by 小梁 (Phase B SelectSide 算法 spec v1，Sharpe/Kelly 主权拍板，供 GM 直接落地)
