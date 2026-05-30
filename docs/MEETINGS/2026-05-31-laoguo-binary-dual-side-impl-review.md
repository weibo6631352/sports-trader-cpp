# 老郭实现评审 — 二元市场双边决策 Phase A + Phase B (买 NO)

> reviewer: 老郭 (首席架构评审 + 架构否决权 + 顾问团协调人, F)
> last_review: 2026-05-31
> 性质: 只读实现评审 (之前评的是设计 C1-C6, 现评实际代码), 不改主干。
> 评审对象: commit 541aa75 (Phase A) + f2d3b5b (买-NO RM 单测) + 660d957 (Phase B)
> 设计基准: docs/RESEARCH/xiaoliang-phaseB-select-side-algo-v1.md (小梁 spec) +
>           docs/MEETINGS/2026-05-31-laoguo-binary-dual-side-arch-review.md (我自己的 C1-C6)
> 实测: build 全绿; test_paper_loop 32/32 PASS (30 原 + 2 PhaseB); test_risk_gateway_buy_no 10/10 PASS。

---

## 结论: APPROVE-with-nits

实现忠实于小梁 spec 与我的 C1-C6, 无 money-path bug, 无买错边/字段漏切/fair 泄漏/假绿。
选边代数正确, YES-canonical / 被选边 split 干净, fail-closed 齐全, 端到端 NO 测试真覆盖。
3 个 nit 全在「展示快照层」与「文档/死代码卫生」, 不阻塞、不上风险, 可纳入下个 housekeeping。

放行落地。本次实现质量明显优于此前 worktree 写代码的几次 (无返工、无单位失配)。

---

## 一、逐评审重点核对 (读码 + 实测)

### 1. de-vig 选边正确性 — PASS

`SelectSide` (paper_loop.cpp:272-278):
```cpp
const bool is_yes = (p_fair_yes - p_market_devig) >= 0.0;
```
- 忠实小梁 §2.3: 看 `raw_edge_yes` 符号, 不分别跑两遍 Kelly。✅
- 临界 `raw==0 → is_yes=true` (买 YES 默认), 下游 sizing CI gate 因 edge=0 不下单 — 安全, 无套利病态。✅

edge_ci 对称代数 (paper_loop.cpp:458-462):
```cpp
const double raw_edge_yes = p_fair - p_market_devig;
const double sigma = sqrt(max(0, p_fair*(1-p_fair)) / n_eff);
const double edge_ci_lower = (is_yes ? raw_edge_yes : -raw_edge_yes) - z_90 * sigma;
const double p_fair_selected = is_yes ? p_fair : (1.0 - p_fair);
```
- `edge_ci_no = -raw_edge_yes - z*sigma` 与小梁 §2.1 line 124 精确一致。✅
- **sigma 用 `p_fair`(=p_fair_yes) 算方差**: 因 `p_fair_yes*(1-p_fair_yes) == p_fair_no*(1-p_fair_no)` (互余 → 两边方差恒等), 选 NO 时复用 YES 方差**数学严格正确**, 非近似。小梁 §2.1 line 118-119 已证, 实现对。✅
- `max(0, ...)` 防负数开方, 数值稳健。✅

### 2. YES-canonical / 被选边 split 无泄漏 (最关键, 防买错边) — PASS

**fair 段 (始终 YES book):**
- `feat = mkt.yes.book` (L291), `book_row.token_side = "YES"` (L388), p_fair/p_market_devig/devig 全用 YES microprice + 对边 NO mid (L307-329)。✅ FairValueEstimator 入参恒 YES, 选 NO 不换喂 FV (符合小梁 §3 Step C 强调)。

**执行段 (全切被选边 exec_feat = traded.book):**
逐字段核 (这是我最关注的, 防漏切):
| 字段 | 行 | 来源 | 判定 |
|---|---|---|---|
| exec_ask | L448 | `exec_feat.best_ask()` | 切 ✅ |
| token_id | L447 | `is_yes ? yes_token_id : no_token_id` | 切 ✅ |
| exec_mark | L452 | `exec_feat.microprice/mid` | 切 ✅ |
| book_depth_l1 (C2 第6处) | L468 | `exec_feat.best_ask_size()` | **切 ✅ (我 C2 点的第6处现已切)** |
| edge_ci_lower | L461 | 被选边代数 | 切 ✅ |
| p_fair_selected | L462 | `is_yes ? p_fair : 1-p_fair` | 切 ✅ |
| sz_in.buy_yes | L481 | `is_yes` | 切 ✅ (修了旧 buy_yes 方向) |
| intent.4ts | L554-556 | `exec_feat.*_ts_ns` | 切 ✅ |
| intent.outcome | L562 | `is_yes ? Yes : No` | 切 ✅ |
| intent.book_depth_l1_usdc | L589 | `book_depth_l1`(被选边)×1e6 | 切 ✅ |
| intent.book_snapshot_ts_ns | L590 | `exec_feat.ingestion_ts_ns` | 切 ✅ |
| vord.outcome | L646 | `is_yes ? "YES":"NO"` | 切 ✅ |
| apply_fill outcome | L672 | `intent.outcome` | 切 ✅ |
| PublishLedgerSnapshot mark+4ts | L675 | `exec_feat` | 切 ✅ |

**13 个执行字段全切被选边, 0 漏网。** 我设计 review 时 C2 点的「第6处 book_depth」(老周原 5 处清单漏的) 已补切。**无买错边风险。**

### 3. YES 路径逐位等价 — PASS (实测证)

选 YES 时 `traded = mkt.yes` → `exec_feat` 别名指向 `mkt.yes.book` == `feat`。所有 `feat → exec_feat` 替换在 YES 路径是同一对象的别名, **逐位等价**, 无隐藏分叉。
实测: 全量 test_paper_loop **32/32 绿** (30 原有 YES 路径测试 + 2 新 Phase B), 30 原测试零改动通过 = YES 路径不变性的硬证据。✅

### 4. fail-closed 完整性 — PASS

- YES book 缺 → 无 YES-canonical fair → return (L287-290)。✅
- YES L1 价无效 (ask/bid) → return (L297-302)。✅
- 选 NO 但 NO book 缺 (`!traded.present`) → return (L442-445)。✅
- 选 NO 但 NO ask 无效 → return (L449-451)。✅
- devig 双边无效 → `devig_ok=false` → second gate return (L528-531)。✅
- **无「选 NO 却用 YES 价/深度」的漏网** — exec_ask/exec_feat 在 fail-closed 校验**之前**已切被选边, 校验对象正确。✅

### 5. R-20 / R-12 — PASS

- R-20: intent 4ts 来自 `exec_feat`(被选边 book), 非 `now()` (L554-556); 4ts 链顺序校验用 exec_feat (L544-545)。Goalserve game_row 4ts 切真上游 ts (L371-374)。✅
- R-12: TickAll 双边读 `hub_.Read(yes) + hub_.Read(no)` 各 token 独立 atomic 只读, 栈上组 BinaryMarketSnapshot, 零锁零 malloc, 不碰 WSS io_thread (在 loop_thread_)。✅

### 6. C4 端到端测试可信度 — PASS (真覆盖, 非假绿)

`T_PhaseB_BuyNo_EndToEnd`:
- 构造: YES book(0.69/0.71, microprice≈0.70) + NO book(0.29/0.31)。`devig_binary(0.70, 0.30)=0.70`。模型 fair_yes (0:3 落后, blend 后实测 0.5328) < 0.70 → raw_edge_yes 强负 → 选 NO。
- 断言强度核: `token_id=="1002"` (NO) + `outcome==Outcome::No` + **`EXPECT_NE(token_id, "1001")`** (不在 YES 成交) + `found_no` 必为 true。**三重断言, 排除「其实买了 YES」的假绿。**
- 实测 stderr: `FILL ... tok=1002 ... fill_px=0.3101` — 真在 NO token 以 NO ask 成交。✅
- 注: 这是**唯一**端到端 NO 成交验证点, 解了 Phase A「建不起 NO intent」的根因。断言够强。

### 7. 与设计的偏离 (防漂移) — 无实质偏离

SelectSide 从老周原设计的「DecisionSide + TradedSide::None」简化成「never None + 下游 sizing gate 兜底」:
- **合理。** 小梁 §2.3 spec 本身保留 None 分支 (两边 edge_ci 都 ≤0 → None), 但实现把 None 判断**下沉到 sizing CI gate** (L534 `sizing_out.valid==false || suggested_notional<=0 → return`)。
- 临界/无 edge 时 SelectSide 返 {Yes,Buy}, 但 raw_edge≈0 → edge_ci_lower<0 → sizing 不产正 notional → 不下单。**语义等价于 None, 无 None 路径漏洞**: 不存在「SelectSide 说交易但其实没 edge 却下单」的洞, 因为 sizing 是最终 gate。
- enum `TradedSide::None` 仍在 hpp 保留 (binary_market_snapshot.hpp:54), 为 M2 留口, 当前不走 — 死枚举值但非死代码风险。

---

## 二、nits (不阻塞, 下个 housekeeping)

### nit#1: PublishQuoteSnapshot 的 4ts 仍用 YES `feat`, 与 mark/edge 的被选边语义混栖 (展示层, 非下单链路)

paper_loop.cpp:511:
```cpp
PublishQuoteSnapshot(condition_id, fv_result, sizing_out, mark_price, edge_ci_lower, feat, has_real_fair);
//                                                          ↑被选边mark  ↑被选边CI    ↑YES book 4ts
```
- quote 快照里 `market_mid=mark_price`(被选边) + `edge_ci`(被选边) 已切, 但 4ts (`qf.event_ts_ns` 等, L862-864) 用 `feat`=YES book。`fair_value=fv_result.p_yes()` 是 YES-canonical (展示 YES 概率, 合理)。
- **不是 money-path bug**: QuoteSnapshot 是 /api/v1/quote 展示旁路, 不进 RM/不下单。但选 NO 时, 看板上「这条 quote 的 4ts」是 YES book 的 ingestion_ts, 而 edge/mark 是 NO 算的 → **观测者会被 4ts 误导以为数据来自 YES**。
- 这是我设计 review **C3「对边陈旧可追溯」缺口的延续**: 当 NO book 比 YES 陈旧时, quote 4ts 显示 YES(新) 而决策实际用了 NO — audit 无法从 quote 4ts 回溯被选边真实 staleness。
- **修法 (低优先级):** PublishQuoteSnapshot 改传 `exec_feat` 而非 `feat` 做 4ts 来源 (与 mark/edge 同源被选边); 或保留 YES 4ts 但补一个 `traded_side` + `traded_book_ts` 旁路字段 (C3 的 audit 留痕)。**纳入 C3 落地项, 非本次拦停。**

### nit#2: ComputeEdgeCiLower 静态函数在 TickOne 已不被调用 (仅 T06 测试用)

- TickOne 改用内联代数 (L458-461) 算 edge_ci, 不再调 `ComputeEdgeCiLower` (paper_loop.cpp:697)。
- 该静态函数现仅 T06 测试覆盖 (test_paper_loop.cpp:322), 注释 L456 标了「== 旧 ComputeEdgeCiLower」。两者数值等价我已核 (raw - z*sqrt(p(1-p)/n))。
- **不是 bug**, 但是「同一公式两处实现」(内联 + 静态), 有漂移风险: 将来改一处忘改另一处, T06 还绿但 TickOne 错。**建议**: 要么 TickOne 复用 ComputeEdgeCiLower (YES 分支直接调, NO 分支用对称值代入), 要么删静态函数 + 把 T06 改测内联路径。**housekeeping, 非拦停。**

### nit#3: edge_bps 显示用 |raw_edge_yes| 双边同幅 (展示语义, 正确但需文档明确)

- L477 `sz_in.edge_bps = abs(raw_edge_yes) * 10000` — de-vig 对称下两边 |raw_edge| 相等, 故选 NO 时显示的 edge_bps 仍是 YES raw 的绝对值 = NO raw 绝对值。数值对。
- 但 stderr FILL 日志打 `edge=1672bps` 时, 读者需知道这是「双边同幅的绝对 edge」, 非「NO 方向净 CI edge」。**仅文档澄清, 代码对。**

---

## 三、未发现的真问题

无 money-path bug。具体排除:
- ❌ 买错边: 13 执行字段全切, 端到端 NO 测试三重断言证实。
- ❌ 字段漏切: C2 第6处 book_depth 已补, 无残留 YES 字段进 NO 执行链。
- ❌ fair 泄漏: fair 段恒 YES-canonical, 无被选边量误入 FV/p_fair。
- ❌ 假绿测试: T_PhaseB_BuyNo_EndToEnd 断言 token==1002 + outcome==No + !=1001, 实测 FILL tok=1002 印证。
- ❌ None 路径漏洞: SelectSide never-None 简化由 sizing CI gate 兜底, 语义等价 None, 无漏单。
- ❌ R-20/R-12 违反: 4ts 被选边透传 + 双边零锁读。

---

## 四、防漂移结论

实现**未偏离**小梁 spec 与我的 C1-C6。3 个 nit 是边角 (展示层 4ts 一致性 = C3 延续 / 公式双实现卫生 / 日志语义文档), 均不在决策正确性与红线层。
SelectSide never-None 的简化是**有意且合理**的工程收敛, 非偷工。

**裁定: APPROVE-with-nits。Phase A + Phase B 落地确认, 无需回滚。nit#1 并入 C3 落地, nit#2/#3 纳 housekeeping backlog。**

---

**最后更新:** 2026-05-31 by 老郭 (二元双边 Phase A+B 实现评审, 只读, 不改主干)
