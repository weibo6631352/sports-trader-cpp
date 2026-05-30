# Phase B 买 NO — RM 放行 checklist + spec v1

> **owner:** 老韩 (风控合规部主管, RM 主权)
> **last_review:** 2026-05-31
> **status:** RM 主权裁定 — 供 GM 老雷据此落 Phase B (买 NO intent)
> **触发:** 小梁策略仲裁 (xiaoliang-binary-dual-side-strategy-v1.md §5.3) 点名「主动选边买 NO = M1 可做但需老韩 5 项 checklist 全绿放行」
> **边界:** 本 doc 只裁 RM 侧 (evaluate / exposure / MtM / liquidity / audit) 对 NO intent 的正确性 + 放行门禁 + 单测设计。不碰策略选边逻辑 (小梁/小袁主权)、不碰签名实现 (老孙主权)、不改主干 (只读分析 + 写 spec)。
> **关联读码 (本次核查的真实代码):**
> - `src/stcpp/risk/risk_gateway.cpp` (evaluate 9 gate 全链路)
> - `include/stcpp/risk/risk_gateway.hpp` (OrderIntent v0.6 / RiskConfig / FeedKey)
> - `src/stcpp/risk/position_ledger.cpp` (condition_exposure_ 聚合)
> - `src/stcpp/paper/paper_loop.cpp` (FeedRiskGateway / sizing 同源 exposure / MtM)
> - 三方设计: laozhou-binary-dual-side-arch-v1 / xiaoyuan-binary-dual-side-decision-v1 / xiaoliang-binary-dual-side-strategy-v1

---

## 0. TL;DR (RM 主权拍板)

| # | checklist 项 | 结论 | RM 必改? |
|---|---|---|---|
| 1 | RM token-agnostic (9 gate) | **PASS** (天然支持) | **零改** |
| 2 | per-condition cap 两边合并 | **PASS** (天然合并 — `condition_exposure_[condition_id] += delta`) | **零改** (有 2 个守护需补单测) |
| 3 | NO 边 MtM 语义 | **PASS** (token-agnostic best_bid mark) | **零改** |
| 4 | NO 边 liquidity gate | **PASS** (RM 侧透明) — 但 **paper_loop 喂数侧必改** (传 NO book depth) | RM 零改 / **paper_loop 必改** |
| 5 | NO intent audit emit | **PASS** (outcome/side/token_id/condition_id 全透传) | **零改** |

**核心裁定: RM module 本体对买 NO intent 零改动 — RiskGateway / PositionLedger 自 v0.5 起就是 token-agnostic 设计,买 NO 是 token_id=NO_token + outcome=No 的纯参数化路径,没有任何「只对 YES」的硬编码。**

**唯一真正的改动点不在 RM,在 paper_loop 决策侧的喂数 (第 4 项 / 第 2 项守护):选边为 NO 时必须把 `intent.book_depth_l1_usdc`、`intent.price`、`intent.outcome`、`intent.token_id` 全切到 NO book。这是小袁/小梁/老周的 paper_loop 改造范围,GM 落地。RM 只负责在这些字段正确填入后做 token-agnostic enforce。**

**不走 R-4。** 第 2 项无任何字段重命名 / 单位变更 / schema 改动 — `condition_exposure_` 的聚合 key 一直是 `condition_id`,买 NO 只是往同一个已存在的桶里加一笔,不触 R-4。

---

## 1. 第 1 项 — RM token-agnostic 验证 (逐 gate 核)

**买 NO intent 定义** (小梁 §5.1 层二 + 小袁 §6.1):
```
intent.outcome   = strategy::Outcome::No
intent.token_id  = token1  (NO token, uint256 十进制 string)
intent.side      = strategy::Side::Buy   (买 NO = 做多 NO token, 一笔 BUY 单, 非卖空)
intent.condition_id = 同 condition (YES/NO 共享)
intent.price     = ask_NO  ∈ (0,1)
```

逐 gate 核 `evaluate()` short-circuit 链 (risk_gateway.cpp:713-802),确认每个 gate 对 NO intent 是否真 token-agnostic:

| # | gate | 读什么字段 | 对 NO intent 是否 token-agnostic | 结论 |
|---|---|---|---|---|
| 1 | `check_state_` (:317) | `state_` + `it.is_close` + `it.side` | 全局状态 + side。NO 买入 `side=Buy`,DRAIN/SAFE_MODE 放行条件 (`is_close && side==Sell`) 对 NO 买入与 YES 买入**完全一致**。无 outcome/token 分支 | **PASS** 天然 |
| 2 | `check_invalid_intent_` (:345) | 4ts / price / token_id 格式 / condition_id / timestamp_ms / metadata / builder | `is_valid_token_id(it.token_id)` 校验「纯数字 ≤77 位」对 NO token (token1) 同样适用。`price ∈ (0,1)` 对 ask_NO 同样适用。**无任何 outcome 分支** | **PASS** 天然 |
| 3 | `check_duplicate_` (:420) | `it.signal_id` | 幂等 key 是 signal_id,与 outcome/token 无关。买 NO 用独立 signal_id (paper_loop intent_seq_ 单调) → 不与 YES 撞 key | **PASS** 天然 |
| 4 | `check_stale_data_` (:432) | `it.condition_id` (market freshness) + `it.token_id` (R8.4 book freshness) | **关键**: R8.4 块 (`:459-473`) 用 `it.token_id` 查 `token_book_freshness_ms`。NO intent 传 token1 → 查 NO token 的 book freshness。**这正是我们要的 token 维度** — 它会对 NO book 的陈旧度独立判断,不会拿 YES book 的 freshness 误判 NO。需喂数侧 (paper_loop) 对 NO token 调 `set_token_book_freshness_ms(token1, ...)` | **PASS** 天然 (喂数侧需对 NO token 喂 freshness,见 §9 守护) |
| 5 | `check_market_` (:479) | `it.condition_id` → `market_active` | 按 condition_id 查市场启用 / active。YES/NO 同 condition → 同一个 active 状态。无 token/outcome 分支 | **PASS** 天然 |
| 6 | `check_position_caps_` (:496) | per_order / per_condition / per_outcome / bankroll / DD / consec | per_order: 比 `it.size_pUSD_micro` (token 无关)。per_condition: 查 `condition_exposure_usdc[it.condition_id]` (**两边合并,见 §2**)。per_outcome: 查 `token_exposure_usdc[it.token_id]` (NO intent 查 token1 桶,**正是 NO token 独立 cap**)。bankroll/DD/consec 全局。**无 outcome 硬编码** | **PASS** 天然 (per_outcome 对 NO 是独立 token cap,符合设计) |
| 7 | `check_liquidity_` (:590) | `it.size_pUSD_micro` / `it.price` / `it.book_depth_l1_usdc` / `it.book_snapshot_ts_ns` / `it.tick_size` | 全部纯 intent 字段。`from_micro(size).to_pusd()` (:598) 与 token 无关。RM **透明消费** book_depth — 它信任 intent 里填的是「被交易那一边的 book」。**RM 侧 PASS,但前提是 paper_loop 填的是 NO book 的 depth/price/ts** (见 §4) | **PASS** RM 透明 (喂数侧必改) |
| 8 | `check_signal_` (:634) | `it.signal_id` → edge_ci_lower; `it.price` → fee | edge_ci_lower 按 signal_id 查 (NO 用 NO 方向的独立 signal_id + 独立 edge_ci)。fee = `kSportsTakerFeeRate * p * (1-p)` 用 `it.price`=ask_NO,公式对 NO 对称 (小梁 §4.1)。**无 outcome 分支** | **PASS** 天然 (需喂 NO 方向 edge_ci,见 §9) |
| 9 | `check_strategy_decayed_` (:672) | `it.strategy_id` → ev_ratio | 按 strategy_id 查,与 outcome/token 无关 | **PASS** 天然 |
| — | `emit_audit_` (:687) | 见 §5 | — | **PASS** (见 §5) |

**第 1 项裁定: PASS。9 个 gate 全部 token-agnostic,无任何「只对 YES」硬编码。买 NO intent 走 evaluate 全链路与买 YES 在 RM 内部走的是同一套字段消费,只是 token_id/outcome/price 值不同。RM 本体零改。**

> 已确认无隐含假设: grep 全 risk_gateway.cpp 无 `Outcome::Yes` / `"YES"` / token0 硬编码;唯一 `outcome` 出现处是 `emit_audit_` 的 `static_cast<std::uint8_t>(it.outcome)` 透传 (:699),纯转发,不分支。

---

## 2. 第 2 项 — per-condition cap 两边合并 (最关键, RM 主权)

### 2.1 小梁的要求 (粘原文, §8.1 红线治理纪律 #1 禁转述)

> xiaoliang-binary-dual-side-strategy-v1.md §3.2:
> 「`current_condition_exposure` 必须 = 该 condition 下所有 token (YES + NO) 的 notional 之和。即:
> `cond_exposure[condition_id] = sum(token_exposure[t] for t in tokens_of_condition) = yes_notional + no_notional`
> ... 如果老韩的 cap 语义不含 NO side, 双边选边后 exposure cap 会算错 → 须接口对齐。」

### 2.2 读码结论 — **天然合并,RM 侧零改**

`position_ledger.cpp::update_position_locked_` 末行 (:103):

```cpp
// 更新 condition_exposure_ (signed sum)
condition_exposure_[condition_id] += delta_usdc;
```

`get_per_condition_exposure()` (:137-140) 直接返回这个 map。

**关键事实链:**
1. `apply_fill(condition_id, token_id, outcome, fill)` (:33) 的聚合 key 是 **`condition_id`,不是 token_id**。
2. 买 YES fill → `condition_exposure_[cid] += yes_delta`。
3. 买 NO fill (token1, 同 cid) → `condition_exposure_[cid] += no_delta`。**同一个桶。**
4. → `condition_exposure_[cid] = yes_notional + no_notional`,**正是小梁要的 YES+NO 合并 sum**。
5. paper_loop `FeedRiskGateway()` (:759-761) 遍历这个 map 喂 `rm_.set_condition_exposure(cid, micro)`。
6. RM `check_position_caps_` R6.2a 块 (:507-522) 查 `condition_exposure_usdc[it.condition_id]` 比 `market_exposure_cap_usdc`。

**→ 买 NO 后,该 condition 的 YES+NO 合并 notional 自动累加进同一 condition 桶,RM 用合并值比 per-condition cap。天然满足小梁要求,无任何绕过双倍暴露的可能。**

### 2.3 精确裁定

| 问题 | 结论 |
|---|---|
| `get_per_condition_exposure()` 是否已对同 condition 下所有 token (YES+NO) 求 sum? | **是。** 聚合 key 是 condition_id,token 无关。YES fill 与 NO fill 进同一 condition 桶。 |
| RM 侧要不要改? | **不改。** `condition_exposure_usdc` map + R6.2a 比较逻辑已是 condition 维度,买 NO 自动落入同桶。 |
| 改哪? | **无需改任何地方。** position_ledger 聚合层 + RM 消费层都已 condition-keyed。 |
| 走不走 R-4 (schema 静默变更红线)? | **不走。** 无字段重命名 / 无单位变更 / 无 schema 改动。买 NO 只是往已存在的 `condition_exposure_[cid]` 桶加一笔,聚合 key 和单位 (signed micro) 都不变。R-4 触发条件 (粘原文,见 §2.5) 不满足。 |

### 2.4 必须标的两个守护 (天然支持 ≠ 无需验证)

虽然 RM 零改,但以下两点是「天然合并」成立的**前提条件**,必须单测守护 (见 §8):

**守护 A (符号方向):** `condition_exposure_[cid] += delta_usdc` 是 **signed sum**。买入 (Buy) → `delta_usdc = +fill_size`,所以买 YES 和买 NO **都是正向累加** → 合并值 = yes_notional + no_notional,正确。
- ⚠️ **M1 范围内安全**: M1 只买不平 (paper_loop intent.is_close=false,只 Buy)。两边都是正 delta,合并 sum 单调增,cap 永远是「两边名义和」,不会被负 delta 冲抵。
- ⚠️ **M2 警告 (前置标注,非本次范围)**: 当 M2 接平仓 (Sell, delta 负) 后,`condition_exposure_` 会变成 **net signed** (yes_long - yes_close + no_long - no_close)。届时 per-condition cap 语义从「两边名义和」变成「两边净敞口」。**这是 M2 必须重新裁定的点 (空头 cap 语义,小梁 §7.3 + 老周 M2-2 已挂在我名下)。本 spec 只对 M1 (只买) 背书 condition cap = 两边名义和。** 不要让 M2 静默继承 M1 的「signed sum == 名义和」假设 (M1 下两者恒等,M2 下分叉)。

**守护 B (单位一致):** `condition_exposure_` 存 signed micro pUSD (A1 ledger micro 化后)。`FeedRiskGateway` 直喂 (paper_loop:759 已删原 ×1e6 补偿乘)。RM `check_position_caps_` 用 `from_micro(cur + size_micro)` 比 `market_exposure_cap_usdc` (micro)。三处单位 (ledger / feed / cap) 全 micro,一致。买 NO 的 size 同样是 micro pUSD (小梁 §6.3 确认 token-agnostic),无单位坑。

### 2.5 R-4 红线原文 (粘,§8.1 #1)

> CLAUDE.md §8 红线:
> 「数据 schema 静默变更（不通知下游） → 责任人承担事故」
>
> CLAUDE.md §8.1 #3:
> 「ABI/字段单位变更必触发下游审计（补 R-4 配套）：任何字段重命名/单位变更（如 `size_usdc → size_pUSD_micro`）必须 audit 全部比较点/消费点的单位一致性,否则会「静默架空」依赖该字段的红线（caps/exposure/bankroll）。这类变更走 R-4（schema 静默变更红线）。」

**对照:** 买 NO 无字段重命名、无单位变更、无 schema 改动。`condition_exposure_` 的 key (condition_id)、value 单位 (signed micro)、消费点 (R6.2a 比较) 全不变。**不触 R-4。** (反例:若有人为了买 NO 把 `condition_exposure_` 改成 per-token keyed,那才触 R-4 — 但我们不需要这么改,现状 condition-keyed 已天然合并。)

---

## 3. 第 3 项 — NO 边 MtM 语义 (A5 DD)

### 3.1 读码结论 — token-agnostic best_bid mark, PASS

`FeedRiskGateway()` MtM 块 (paper_loop.cpp:771-787):

```cpp
for (auto const& pv : position_ledger_.get_all_positions()) {
    const double qty = static_cast<double>(pv.size_usdc) / 1'000'000.0;
    const auto bk = hub_.Read(pv.token_id);          // ← 按 pv.token_id 读各自 book
    if (bk.has_value()) {
        const double bid = bk->best_bid();
        if (std::isfinite(bid) && bid > 0.0 && bid < 1.0) {
            pnl_pusd += (bid - pv.avg_entry_price) * qty;   // ← token-agnostic MtM
        }
    }
}
pnl_pusd -= cum_fee_pusd_;
rm_.set_daily_pnl(static_cast<std::int64_t>(pnl_pusd * 1'000'000.0));
```

**关键: 遍历 `get_all_positions()`,每个仓位用 `hub_.Read(pv.token_id)` 读它自己 token 的 book best_bid。** NO 仓位 → pv.token_id = token1 → 读 NO book best_bid → `(best_bid_NO - avg_entry_NO) * qty_NO`。

### 3.2 语义正确性 (逐项核)

| 检查 | 结论 |
|---|---|
| NO 多头清算价 = NO book best_bid? | **正确。** 持有 NO share,平仓即在 NO book 砸 bid 侧成交,清算价 = best_bid_NO。与 YES 多头对称 (小梁 §6.1 确认)。 |
| 符号坑? | **无。** `(bid - avg_entry) * qty`,NO 仓位 qty > 0 (Buy 做多),bid_NO ∈ (0,1),avg_entry_NO ∈ (0,1),浮盈/浮亏符号天然对。亏损 → pnl 负 → RM `if(pnl<0)` DD 分支 (risk_gateway.cpp:549) 触发,符号对齐无需取反。 |
| 单位坑? | **无。** qty = size_usdc(signed micro) / 1e6 = whole share。bid ∈ (0,1)。pnl_pusd whole pUSD → ×1e6 喂 RM (micro)。NO 与 YES 同公式同单位。 |
| 无效 bid 兜底? | **保守正确。** NO book 常较薄,best_bid 可能无效 (0 或 ≥1)。代码跳过浮盈贡献 (不臆造正值),该 NO 仓位仍承担下方 cum_fee 扣减 → 偏保守 (铁律#2),不掩盖亏损。 |
| 跨边混算? | **无。** YES 仓位读 YES book,NO 仓位读 NO book,各自独立,合进同一个 `pnl_pusd` 标量 (整账户净 MtM)。这正是 DD 想要的 — 账户级日内 PnL 含两边。 |

**第 3 项裁定: PASS。A5 DD 的 MtM 自 spec 起就是 token-agnostic (遍历所有仓位 + 按各 token best_bid mark)。买 NO 仓位天然纳入,无符号/单位坑,无需改 A5。** (与小梁 §6.1 结论一致。)

---

## 4. 第 4 项 — NO 边 liquidity gate (P1-9 配套)

### 4.1 RM 侧透明 — PASS

`check_liquidity_` (risk_gateway.cpp:590-629) 消费 `it.book_depth_l1_usdc` / `it.price` / `it.book_snapshot_ts_ns` / `it.size_pUSD_micro`,**全是纯 intent 字段,RM 不知道也不关心这是 YES 还是 NO 的 book** — 它信任 intent 里填的是「被交易那一边的 book」。

P1-9 的核心转换 (:598):
```cpp
.order_size_usdc = domain::MicroPUSD::from_micro(it.size_pUSD_micro).to_pusd(),
```
`from_micro(...).to_pusd()` 是 micro→whole 的纯数值转换,**token 无关**。NO intent 的 `size_pUSD_micro` 同样是 micro pUSD (买 NO notional × 1e6),`.to_pusd()` 同样成立。ρ = order_size / book_depth_l1 两边同量纲 (whole pUSD)。**P1-9 对 NO intent 完全透明。**

### 4.2 关键改动点 — **不在 RM,在 paper_loop 喂数侧** (小袁点名)

小袁 §3.3 + §7.3 点名 (粘原文):
> xiaoyuan-binary-dual-side-decision-v1.md §7.3:
> 「`OrderIntent.book_depth_l1_usdc` 必须来自被选边的 book depth, 不能始终用 YES。选边为 NO 时, `intent.token_id` = NO token, `intent.outcome` = `Outcome::No`, `intent.price` = `no_ask`...RM 的 `check_liquidity_` 校验 `size_pUSD_micro vs book_depth_l1_usdc`, 用错误边的 depth 会导致流动性判断错误（可能放行实际无法成交的 NO 方向大单）。」

当前 paper_loop.cpp 硬编码 YES book depth (:538):
```cpp
intent.book_depth_l1_usdc = book_depth_l1 * 1'000'000.0;   // book_depth_l1 = feat.best_ask_size() (YES, :417-419)
```

**这是 GM 落 Phase B 必改的点 (paper_loop 决策侧,非 RM)**: 选边为 NO 时,`book_depth_l1` 必须来自 NO book 的 `best_ask_size()`,`intent.book_snapshot_ts_ns` 必须来自 NO book 的 `ingestion_ts_ns`,`intent.price` = ask_NO。

### 4.3 裁定

| 维度 | 结论 |
|---|---|
| RM `check_liquidity_` 对 NO 透明? | **是,PASS。** 纯 intent 字段消费,token 无关。 |
| P1-9 `.to_pusd()` 对 NO intent 成立? | **是,PASS。** micro→whole 纯数值,token 无关。 |
| 需改? | **RM 零改。paper_loop 喂数侧必改** (选 NO 时 book_depth/price/snapshot_ts 切 NO book) — 这是小袁/老周的 paper_loop 改造范围,GM 落地。 |
| 守护 | 单测:NO intent 填 NO book depth → liquidity gate 用 NO depth 算 ρ (见 §8 T-NO-7);防回归:误填 YES depth 给 NO 大单应被 EXCEED_BOOK_DEPTH 拦 (见 §8 T-NO-8)。 |

---

## 5. 第 5 项 — NO intent audit emit

### 5.1 读码结论 — PASS

`emit_audit_` (risk_gateway.cpp:687-709):
```cpp
rec.condition_id = it.condition_id;                   // 透传
rec.token_id = it.token_id;                           // 透传 (NO intent → token1)
rec.outcome = static_cast<std::uint8_t>(it.outcome);  // 透传 (NO intent → Outcome::No 底层值)
rec.side_val = static_cast<std::uint8_t>(it.side);    // 透传 (Buy)
rec.signal_id = it.signal_id;
```

`it.outcome` 对 NO intent = `Outcome::No`。`ToString(Outcome::No)` = "No" (signal_iface.hpp:73)。`Outcome` 枚举底层值固定 (ABI lock v1.7,老高 CI grep 守护)。

### 5.2 裁定

| 检查 | 结论 |
|---|---|
| outcome 字段对 NO intent 正确打标? | **正确。** `rec.outcome = static_cast<uint8_t>(Outcome::No)`,纯透传,不默认 Yes。 |
| side 字段? | **正确。** 买 NO 的 `side=Buy` 透传为 side_val。 |
| token_id? | **正确。** NO intent 的 token1 透传。 |
| condition_id? | **正确。** 透传。 |
| 可追溯 (R-1)? | **满足。** APPROVED 和 REJECTED 路径都走 `emit_audit_` (evaluate:724 reject 路径 + :797 approve 路径),audit_id 非空。NO intent 的 reject/approve 都有完整 audit (含 outcome=No)。 |

**第 5 项裁定: PASS。audit emit 对 NO intent (outcome=No) 正确打标且可追溯,R-1 满足。RM 零改。** (与小梁 §5.3 C5 / §7.3 一致。)

### 5.3 R-1 红线原文 (粘,§8.1 #1)

> CLAUDE.md §8 红线:
> 「任何下单链路绕过 `RiskManager` → 立即回滚 + post-mortem」
> CLAUDE.md §7 协作规范 #6:
> 「可追溯：风控拒单 / 关键决策 / API 变更 — 全部留 audit log」

NO intent 走 `rm_.evaluate()` 全链路 (paper_loop.cpp:549),不绕 RM;reject/approve 均 emit audit (含 outcome=No 标记),满足 R-1 可追溯。

---

## 6. RM 拒单原因表 (NO intent 适用性)

买 NO intent 可能触发的 reject code (与买 YES 完全同表,因 token-agnostic):

| RejectCode | 触发条件 (NO intent) | 与 YES 差异 |
|---|---|---|
| STATE_HALTED/DRAIN/SAFE_MODE | RM 状态机 (与 outcome 无关) | 无 |
| INVALID_INTENT (+sub) | NO token_id 非纯数字 / 4ts 违规 / ask_NO ∉ (0,1) / metadata 格式 | 无 (token_id 校验对 token1 同样跑) |
| DUPLICATE_INTENT | NO signal_id 重复 | 无 (NO 用独立 signal_id) |
| STALE_DATA (+BOOK_TOKEN_ID_MISMATCH) | NO condition freshness 超档 / **NO token book freshness 超档** | NO book freshness 独立判 (R8.4 用 token1) |
| MARKET_TYPE_NOT_ENABLED / MARKET_NOT_ACTIVE | condition 未启用 / 非 active | 无 (同 condition) |
| EXCEED_PER_ORDER_CAP | NO 单笔 size > per_order_cap | 无 |
| **EXCEED_CONDITION_EXPOSURE** | **YES+NO 合并 notional > per_condition cap** | **关键: 合并值触发 (§2),买 NO 不能绕 cap 双倍暴露** |
| EXCEED_PER_OUTCOME_CAP | NO token (token1) 独立敞口 > per_outcome cap | NO token 独立 token cap |
| INSUFFICIENT_BANKROLL / DAILY_LOSS_HALT / CONSEC_LOSS_HALT | 全局资金/DD/连亏 (含 NO 仓位 MtM,§3) | 无 |
| EXCEED_BOOK_DEPTH / LOW_FILL_RATE / EXCESSIVE_SLIPPAGE | NO book depth/fill/slippage (须填 NO book,§4) | depth 来源须是 NO book |
| EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE | NO 方向 edge_ci ≤ 0 / 被 slippage/fee 吃光 | NO 用 NO 方向 edge_ci + ask_NO fee |
| STRATEGY_DECAYED | strategy ev_ratio 衰减 | 无 |
| AUDIT_WAL_BACKPRESSURE | emit 失败兜底 | 无 |

---

## 7. Phase B 放行 checklist (GM 落地买 NO 前逐项绿)

> GM 落 Phase B (paper_loop 选边 + 买 NO intent) 前,以下每项必须绿。绿 = 单测通过 + 人工 review 签字。前 5 项是 RM 主权 (老韩 review),后 3 项是 paper_loop 喂数侧 (老周/小袁/小梁 改,老韩 verify 喂数正确性)。

| # | 放行项 | 谁验 | 绿的判据 | RM 改? |
|---|---|---|---|---|
| **B-1** | RM 9 gate 对 NO intent token-agnostic | 老韩 | §8 单测 T-NO-1..5 全绿 (NO intent 走完整 evaluate,各 gate 不误判) | 零改 |
| **B-2** | per-condition cap 两边合并生效 | 老韩 | §8 单测 T-NO-9 (YES+NO 合并越 cap → EXCEED_CONDITION_EXPOSURE) + T-NO-10 (合并未越 → 放行) | 零改 |
| **B-3** | NO 仓位 MtM 正确纳入 DD | 老韩 | §8 单测 T-NO-11 (NO 仓位浮亏 → daily_pnl 负 → DD 触发) | 零改 (A5) |
| **B-4** | NO liquidity gate 用 NO book depth | 老韩 verify 喂数 | §8 单测 T-NO-7/8 (NO depth 填对 → ρ 正确;误填 YES depth 给 NO 大单 → 应拦) | RM 零改 / **paper_loop 必改** |
| **B-5** | NO intent audit emit outcome=No | 老韩 | §8 单测 T-NO-6 (NO intent reject+approve 的 audit.outcome == No 底层值) | 零改 |
| **B-6** | paper_loop 选 NO 时字段全切 NO book | 老周/小袁 改, 老韩 verify | intent.{token_id, outcome, price, book_depth_l1_usdc, book_snapshot_ts_ns} 全来自 NO book (非 YES 残留) | paper_loop 必改 |
| **B-7** | NO 方向喂 RM 的 freshness/edge_ci 用 NO token/方向 | 老周/小梁 改, 老韩 verify | `set_token_book_freshness_ms(token1,...)` + NO 方向 signal_id 的 edge_ci 已喂 | paper_loop 必改 |
| **B-8** | 互斥选边 enforce (禁同 condition 同时买 YES+NO) | 小梁/小袁 改 | 每 tick 每 condition 只构造一个 intent (小梁 §3.1 / 小袁 §4.2) | 策略侧 (非 RM) |

**B-8 说明 (RM 视角):** RM 本身**不会**主动阻止同 condition 先后买 YES 又买 NO (两笔独立 intent,各自合法)。互斥选边的 enforce 在策略层 (paper_loop 每 tick 每 condition 只产一个 intent)。但即便策略层漏了,§2 的 per-condition cap 合并是**最后一道防线** — 两边合并 notional 会更快撞 condition cap,限制总暴露。RM 在此是 backstop,不是 primary enforce。这点需在 B-8 review 时向小梁/小袁明确:**互斥是策略纪律,cap 合并是 RM 兜底,两者都要。**

---

## 8. buy-NO 全链路 RM 专项单测设计

> 新增测试文件: `tests/unit/test_risk_gateway_buy_no.cpp` (新 fixture 复用 Wave3Test 模式)。
> 数据基础: position_ledger 真实例 + RiskGateway 真实例 (非 mock)。NO token id 用区别于 YES 的 uint256 string。

### 8.1 fixture 约定

```
kCidShared = "0xa9db...c3ff"        // YES/NO 共享 condition_id (bytes32 hex)
kTidYes    = "1234567890"           // YES token (uint256 十进制)
kTidNo     = "9876543210"           // NO  token (uint256 十进制, 区别于 YES)

make_no_intent():  在 make_ok_intent 基础上改:
    it.outcome  = Outcome::No
    it.token_id = kTidNo
    it.side     = Side::Buy
    it.price    = ask_NO  (e.g. 0.45)
    // book_depth/snapshot_ts 模拟 NO book (区别于 YES)
```

### 8.2 单测清单 (T-NO-1 .. T-NO-12)

| ID | 测什么 | 构造 | 期望 | 验的 checklist 项 |
|---|---|---|---|---|
| **T-NO-1** | NO intent 全链路 APPROVED | make_no_intent(), 喂 NO 方向 edge_ci>0, exposure 空, state RUNNING | `decision==APPROVED` | 第 1 项 (9 gate 不误拒 NO) |
| **T-NO-2** | NO token_id 格式校验 | NO intent + token_id="abc" (非数字) | `INVALID_INTENT` + `INVALID_TOKEN_ID_FORMAT` | 第 1 项 gate2 |
| **T-NO-3** | NO ask 边界 | NO intent + price=0.0 / price=1.0 | `INVALID_INTENT` + `NEGATIVE` | 第 1 项 gate2 (ask_NO ∈(0,1)) |
| **T-NO-4** | NO book freshness 独立判 (R8.4) | NO intent + `set_token_book_freshness_ms(kTidNo, 50000)` (超 INPLAY_HOT halt 2000ms) | `STALE_DATA` + `BOOK_TOKEN_ID_MISMATCH` | 第 1 项 gate4 (NO token freshness 独立) |
| **T-NO-5** | YES freshness 不误杀 NO | NO intent + `set_token_book_freshness_ms(kTidYes, 50000)` (YES 陈旧) + NO token fresh | `APPROVED` (NO 不受 YES book 陈旧影响) | 第 1 项 gate4 (token 隔离) |
| **T-NO-6** | NO audit outcome 打标 | NO intent → 故意 reject (edge_ci≤0) 一次 + APPROVED 一次 | 两条 audit 的 `rec.outcome == static_cast<uint8_t>(Outcome::No)` 且 `token_id==kTidNo` | 第 5 项 |
| **T-NO-7** | NO liquidity 用 NO depth (正常) | NO intent + book_depth_l1_usdc=NO_depth(够厚) + 合理 size | `APPROVED` (ρ 用 NO depth 算正常) | 第 4 项 |
| **T-NO-8** | NO 大单 vs 薄 NO depth | NO intent + book_depth_l1_usdc=NO 薄 depth + 大 size → ρ 超 | `EXCEED_BOOK_DEPTH` | 第 4 项 (防误填 YES 厚 depth 放行 NO 大单) |
| **T-NO-9** | **per-condition cap 两边合并越限** | apply_fill YES (cid, kTidYes) notional=4000 + apply_fill NO (cid, kTidNo) notional=4000 → FeedRiskGateway → set_condition_exposure(cid, 8000micro)。再来 NO intent size 让合并>5000 cap | `EXCEED_CONDITION_EXPOSURE` | **第 2 项 (最关键)** |
| **T-NO-10** | 合并未越 cap 放行 | YES notional=1000 + NO notional=1000 (合并 2000 < 5000 cap) + 新 NO intent size=500 (合并 2500<5000) | `APPROVED` | 第 2 项 |
| **T-NO-11** | NO 仓位浮亏 → DD 触发 | apply_fill NO 仓位 avg_entry=0.50 + hub NO best_bid=0.30 (浮亏) → FeedRiskGateway 算 daily_pnl 负超 -3% → 新 NO 开仓 intent | `DAILY_LOSS_HALT` | 第 3 项 (NO MtM 入 DD) |
| **T-NO-12** | position_ledger NO apply_fill + condition 聚合 | apply_fill(cid, kTidNo, Outcome::No, fill) → `get_per_condition_exposure()[cid]` 含 NO notional;`get_per_outcome_exposure()[kTidNo]` == NO notional | 两 map 值正确 | 第 2 项底层 (小梁 C2) |

### 8.3 集成单测 (paper_loop 侧,GM 落 B-6/B-7 后补)

| ID | 测什么 | 期望 |
|---|---|---|
| **T-PL-NO-1** | paper_loop 选 NO 时 intent 字段全切 NO | intent.token_id==NO_token && outcome==No && price==ask_NO && book_depth==NO_depth && book_snapshot_ts==NO_ingestion_ts |
| **T-PL-NO-2** | NO fill 不污染 YES ledger | apply_fill NO 后,YES token 仓位 size 不变,NO token 仓位 == NO fill size |
| **T-PL-NO-3** | 互斥选边 (B-8) | 同 condition 同 tick 双边都正 edge → 只产一个 intent (Kelly-optimal 那边) |

> T-NO-1..12 是 RM 主权单测 (B-1..B-5 放行判据,GM 落地前老韩即可写,不依赖 paper_loop 改造)。
> T-PL-NO-* 依赖 paper_loop B-6/B-7 改造完成 (老周/小袁/小梁),老韩 verify 喂数正确性。

---

## 9. 喂数侧守护清单 (RM 零改,但喂数必须对 — 否则 token-agnostic 形同虚设)

RM token-agnostic 的前提是「喂进来的字段是对的那一边」。以下喂数点 GM 落 Phase B 时必须对 NO 喂对 (老韩 verify):

| 喂数点 | 现状 (YES 硬编码) | 买 NO 必须 | 守护单测 |
|---|---|---|---|
| `intent.book_depth_l1_usdc` (paper_loop:538) | YES `feat.best_ask_size()` | NO book `best_ask_size()` | T-NO-7/8 + T-PL-NO-1 |
| `intent.book_snapshot_ts_ns` (:539) | YES `feat.ingestion_ts_ns` | NO `ingestion_ts_ns` | T-PL-NO-1 |
| `intent.price` (:520) | YES `best_ask` | ask_NO | T-NO-3 + T-PL-NO-1 |
| `intent.outcome` (:511) | 硬编码 `Outcome::Yes` | `Outcome::No` | T-NO-6 + T-PL-NO-1 |
| `intent.token_id` (:510) | token0 | token1 | T-PL-NO-1 |
| `set_token_book_freshness_ms` | 喂 YES token | 也须喂 NO token (token1) | T-NO-4/5 |
| `set_edge_ci_lower(signal_id, ...)` | YES 方向 signal_id | NO 方向独立 signal_id 的 edge_ci | T-NO-1 |
| FeedRiskGateway exposure | 自动 (遍历 ledger) | 自动 (NO fill 入 ledger 后自动喂) | T-NO-9/10 |

**核心提醒:** RM 不会替你检查「你填的 depth 是不是 NO 的」。RM 只信任 intent。所以喂数正确性是 paper_loop (老周/小袁) 的责任,RM 的 token-agnostic 只保证「字段填对了就 enforce 对」。§8 的 T-NO-8 (误填 YES 厚 depth 给 NO 薄 book 大单) 是防这类喂数错的回归网。

---

## 10. 最终裁定 (老韩 RM 主权)

1. **RM module 本体对买 NO intent 零改动。** RiskGateway 9 gate + PositionLedger 聚合自 v0.5 起就是 token-agnostic 设计,买 NO 是纯参数化路径 (token_id=token1 + outcome=No + side=Buy),无任何「只对 YES」硬编码。
2. **第 2 项 (最关键) per-condition cap 天然合并,RM 侧零改,不走 R-4。** `condition_exposure_[condition_id] += delta` 按 condition_id 聚合,YES+NO 进同桶,合并 sum 自动满足小梁要求。无字段重命名/单位变更/schema 改动 → 不触 R-4。
3. **唯一真改动点不在 RM,在 paper_loop 喂数侧** (选 NO 时 book_depth/price/snapshot_ts/outcome/token_id 全切 NO book) — 老周/小袁/小梁 改,老韩 verify。
4. **守护 A (符号):** M1 只买不平,condition_exposure signed sum == 两边名义和,正确。**M2 接平仓后此假设分叉,需重新裁定空头 cap 语义 (前置标注,挂老韩名下)。**
5. **5 项 checklist + 8 项放行门 + 12 个 RM 专项单测** 已给。前 5 项 (B-1..B-5) RM 主权,GM 落地前老韩即可写单测验证;后 3 项 (B-6..B-8) paper_loop/策略侧改,老韩 verify 喂数正确性。
6. **拒接的:** 策略选边逻辑 (小梁/小袁)、paper_loop 双边快照改造实现 (老周)、签名对 NO 的实现 (老孙)。本 spec 只裁 RM enforce 边界 + 放行门禁 + RM 单测。

**放行结论: 5 项 RM checklist 全 PASS (天然支持,RM 零改)。买 NO intent 在 RM 侧可放行,前提是 GM 落 paper_loop 喂数侧 B-6/B-7/B-8 改造 + §8 单测全绿。** RM 不阻塞 Phase B。

---

**最后更新:** 2026-05-31 by 老韩 (Phase B 买 NO RM checklist + spec v1, 供 GM 落地)
