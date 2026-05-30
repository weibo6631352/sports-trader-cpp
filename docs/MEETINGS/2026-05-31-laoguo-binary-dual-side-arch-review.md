# 老郭架构第二意见评审 — 二元市场双边快照 + per-condition 决策

> reviewer: 老郭 (首席架构评审 + 架构否决权 + 顾问团协调人, F)
> last_review: 2026-05-31
> 评审对象:
> - docs/RESEARCH/laozhou-binary-dual-side-arch-v1.md (老周 架构主设计)
> - docs/RESEARCH/xiaoyuan-binary-dual-side-decision-v1.md (小袁 微观选边)
> - docs/RESEARCH/xiaoliang-binary-dual-side-strategy-v1.md (小梁 策略仲裁)
> 性质: 只读评审, 不改主干。GM 正并行落 Phase A (结构地基 + 桩)。

---

## 结论: APPROVE-with-conditions

老周的核心架构裁定全部成立, 无架构性问题需拦在落地前。Phase A (结构地基 + SelectSide 桩恒 YES/Buy + 逐位等价回归) 进 main 安全。
但落地前须满足 6 项条件 (C1-C6), 主要堵 3 个被三份 doc 集体低估或引用错的耦合点。建议立 ADR。

---

## 一、核对老周「不动」清单 vs 实际代码 (读码结论)

| 声称不动 | 核实 | 裁定 |
|---|---|---|
| hub 不动 (per-token SWMR double-buffer) | orderbook_snapshot_hub.hpp: `TokenSlot` per-token double-buffer + `front` atomic, Publish vCPU0 写 back→store(release), Read load(acquire)+value copy。WSS 单边刷新天然映射单 slot flip。 | **成立。** condition 级聚合会逼 writer 读对边 = 退步, 否决正确。R-12 无锁性保住。 |
| OrderBookFeatures 栈上值拷贝零成本 | `static_assert(is_trivially_copyable_v<OrderBookFeatures>)` 命中。 | **成立。** 双读本就发生 (现状 TickAll 读 token0 + 读 token1 取 mid), 净增延迟 ~0。 |
| sizing 不动 (方向中立) | SizingInput 已有 buy_yes 字段, 调用方定方向。 | **成立。** |
| RM 不动 (per-outcome cap 按 token_id keyed) | risk_gateway.cpp check_position_caps_ R6.2b 按 token_id, buy NO 自然走 NO token cap。 | **per-outcome 成立; per-condition 有陷阱见 C1。** |
| ledger/signer/matcher 不动 | apply_fill 已 token-agnostic (任意 token_id + Outcome)。 | **成立 (M1 买入)。** |

**总评:** 老周的「改动收敛在 paper_loop + 1 新头文件」整体诚实。hub / 并发 / 性能三项裁定经读码确认无误。低估的耦合全在「字段单位/语义」层, 不在「要不要改文件」层 —— 即典型的 §8.1 红线治理 #3「单位静默架空」型风险, 必须挂条件。

---

## 二、逐评审点裁定

### 评审点 1: 存储 vs 决策侧值聚合 — APPROVE

hub 不动 + 决策侧栈上组 BinaryMarketSnapshot 的边界划分**正确且是最优解**。理由:
- 老板 C1 (既持 YES 又持 NO) 在 hub 层**已满足** (两 token 各有独立 TokenSlot)。缺口纯在「决策读取层只取 NO 一个 mid 标量」。
- condition 级聚合 slot 会把「单边刷新不碰对边」的无锁原语破坏成 read-modify-write 整 condition slot, 是并发退步。老周否决正确。
- BinaryMarketSnapshot 是纯值 view, 零锁零 malloc, R-12 不触碰。**无更优解, 这就是对的解。**

### 评审点 2: 两边各持独立 4ts 不合并 — APPROVE, 但补 C3 堵可追溯漏洞

R-20 应用 SOP 正确: 4ts 是 per-data-source, YES book 与 NO book 是两条独立 WSS 流, 强行合并丢「哪边陈旧」信息确违 R-20 透传精神。freshness 判断交决策层正确。

**但用户问的「决策用陈旧对边 book 而 intent 4ts 只透传被交易边」的可追溯漏洞真实存在**, 老周 doc §3.3 已意识到 (de-vig 用对边 4ts 只作 freshness 不写 intent 链), 但**没把它落成强制审计字段**。现状 paper_loop de-vig 用 no_token_mid 但 intent 4ts 全来自 YES feat —— 一旦 NO book 陈旧, intent 4ts 链「看起来新鲜」却用了陈旧对边算的 fair, audit 无法回溯。**这是 R-20 透传精神的真实缺口, 挂 C3。**

### 评审点 3: 逐位等价回归门禁 — APPROVE-with-conditions (门禁不够, 补 C4)

「桩恒 YES/Buy + fills/rejects/quote 逐位等价」是**正确的方向**但**不充分**。问题:
- 现状 advisory_markets_no_intent 恒 true → paper 在 advisory gate (Step 4b) 即 return, **根本不产 intent / 不产 fill**。所以「fills 逐位等价」在当前 paper 默认配置下是「两边都是 0 fill」的空等价, **证明力弱**。
- 老周自标 M1-3 字段参数化 (5 处硬编码: book_row.token_side L359 / intent.outcome L511 / intent.side L512 / vord.outcome L596 / apply_fill outcome L621) 是最大风险点, 正确。但「逐位等价」只能证「桩选 YES 时没改坏」, 证不了「参数化通道在选 NO 时正确」。

→ C4: 不能只靠 advisory=true 路径的空等价。回归必须**临时构造 advisory=false + has_real_fair=true 的受控 fixture**, 让 YES 路径真走到 fill, 逐位比对 intent/vord/apply_fill 的 5 个字段全部为 YES。再加一条「桩改成恒 NO」的**反向 fixture 测试**: 验 5 处字段全部翻成 NO/token1, 证参数化通道双向都对 (这条测 NO 通道不下真单, 是结构正确性测试, 不是放行买 NO)。

### 评审点 4: 改动面收敛性 — 3 个被低估耦合, 挂 C1/C2/C5

1. **per-condition cap 两边合并 (小梁/小袁标"待老韩确认")** — 读码: position_ledger.cpp `condition_exposure_[condition_id] += delta_usdc` **已经是按 condition_id 的 signed sum**, YES fill 与 NO fill 写同一 key, 天然合并。RM check_position_caps_ L518 `from_micro(cur + size)` 比 cap。**M1 买入 (delta 恒正) 下两边合并语义已满足, 无需改 RM/PL。** → 小梁/小袁的担忧在 M1 不成立, 但**埋了 M2 雷**: signed sum 在 sell-to-open (负 delta) 时会被对边抵消, condition_exposure 失真, cap 静默架空。挂 C1 (文档固化此边界 + M2 红线)。
2. **RM intent.book_depth_l1_usdc 按选边切换 (小袁 §3.3)** — 现状 L538 恒用 YES best_ask_size。选 NO 时若仍喂 YES depth, check_liquidity_ 用错边深度 → 可能放行 NO 大单。**M1 桩恒 YES 不触发, 但这是 M1-3 字段参数化必须一并参数化的第 6 处** (老周 doc 只列 5 处, 漏了 book_depth_l1 来源)。挂 C2。
3. **BinaryMarketBookView vs BinaryMarketSnapshot 命名混淆** — 小袁 doc §7.1 把 state_provider.hpp 的 `BinaryMarketBookView` (ADR-040, debug_api 看板用, BookSnapshot×2 + cross_spread) 当成老周决策结构引用。**这是两个不同载体**: 看板 view (debug_api) ≠ 决策值聚合 (paper::BinaryMarketSnapshot)。小袁引用错载体。挂 C5 (消歧, 防实施方拿错结构)。

### 评审点 5: M1/M2 边界 — APPROVE

分期正确。Phase A (结构 + 桩) / Phase B (真选边 + 买 NO, 待老韩 C1-C5 checklist) / M2 (sell-to-open) 切得干净。Phase A 桩态进 main **安全** —— 结构换行为不变, 且 advisory gate 仍恒拦 intent, 不存在「桩态误下真单」风险。

注: 小梁把「买 NO」放 M1 有条件做, 老周放 M2。**这不冲突**: 老周的 Phase B (真选边 + 买 NO) ≈ 小梁的 M1-层二, 命名不同实质同一。M2 老周专指 sell-to-open 空头, 小梁也同意空头延 M2。二者一致, ADR 里统一术语即可。

### 评审点 6: ADR — 该立

改动触及决策入参契约 (TickOne 签名)、R-20 双边应用 SOP、R-12 无锁裁定、M1/M2 分期红线, 属「重大设计 + 跨单元契约」, 该立 ADR (CLAUDE.md §6: 架构争议进老郭评审, 结论入 ADR)。要点见末节。

---

## 三、放行条件 (GM 落 Phase A 必须满足)

- **C1 (per-condition signed-sum 边界固化):** 文档明确 condition_exposure_ 是 signed sum, M1 买入下两边合并语义已天然满足 (无需改 RM/PL)。但写死 M2 红线: **sell-to-open 引入负 delta 前, 必须先处理 signed sum 被对边抵消导致 condition cap 失真的问题** (gross exposure vs net exposure 语义分离)。此条入 ADR + M2 checklist。
- **C2 (book_depth_l1 列为第 6 处参数化点):** M1-3 字段参数化清单从 5 处补到 6 处, 加 `intent.book_depth_l1_usdc` 来源 (L538) + `vord.book_depth_l1_usdc` (L598)。桩态恒取 traded(=YES) book depth, 选边后取被选边 depth。漏改 = check_liquidity 用错边 (§8.1 #3 单位/语义审计型红线)。
- **C3 (对边陈旧可追溯):** 决策用对边 book (de-vig) 时, 若对边 staleness 超阈值, 必须在 quote/audit 留一个标记字段 (e.g. `opposite_book_stale: bool` 或对边 data_source_ts) , 使「intent 4ts 新鲜但 fair 用了陈旧对边」可回溯。不写进 intent 4ts 链 (正确), 但要在 QuoteFeatures / audit 旁路留痕。
- **C4 (回归门禁加强):** 逐位等价不能只靠 advisory=true 空等价。须 (a) 受控 fixture 让 YES 路径真走到 fill, 逐位比对 5+1 字段; (b) 加「桩恒 NO」反向 fixture, 验参数化通道双向正确 (结构测试, 不下真单)。
- **C5 (命名消歧):** ADR 显式区分 `BinaryMarketBookView` (debug_api 看板, ADR-040) 与 `paper::BinaryMarketSnapshot` (决策值聚合)。小袁 doc §7.1 引用错载体, 实施方必须拿 paper::BinaryMarketSnapshot。
- **C6 (会签术语统一):** ADR 统一「买 NO = Phase B / M1-层二」「sell-to-open 空头 = M2」, 消老周/小梁命名差。

---

## 四、未发现的架构漏洞

无架构性 (无法回退 / 破坏并发原语 / 违红线地基) 漏洞。所有挂条件均为「字段参数化 + 单位/语义审计 + 可追溯留痕」工程门禁层, 不动 Phase A 的结构骨架。老周主设计的 hub/R-12/R-20/分期四项裁定经读码全部确认成立。

---

## 五、ADR 立项建议

**标题:** ADR-NNN 二元市场双边快照决策入参架构 (BinaryMarketSnapshot)

**决策:**
1. hub 不加 condition 级聚合; 决策线程栈上组 paper::BinaryMarketSnapshot (值 view, 零锁)。
2. 两边各持独立完整 4ts, 不合并; freshness 判断交决策层。
3. TickOne 改 per-condition 整盘口入参; M1 SelectSide 桩恒 YES/Buy + 6 处字段参数化。
4. 分期: Phase A 结构地基 (桩) / Phase B 真选边+买 NO (待老韩 C1-C5) / M2 sell-to-open。

**取舍:**
- 选「决策侧值聚合」弃「hub condition slot」: 换栈上多组一个 view, 保住 WSS 单边刷新无锁原语 (R-12)。
- 选「两边独立 4ts」弃「合并 4ts」: 保 R-20 透传精神, 代价是决策层自己算 staleness。

**边界 (红线):**
- condition_exposure_ signed sum: M1 买入安全; M2 sell-to-open 前必须解 net/gross 语义 (C1)。
- intent 4ts 只透传被交易边; 对边陈旧须旁路留痕 (C3)。
- 字段参数化 6 处, 漏 1 处 = 买错边 / 用错边 depth (C2 + §8.1 #3)。

**会签:** 老郭 (架构, 本报告) + 小袁 (微观接口, 需修正 §7.1 载体引用) + 小梁 (策略接口) + 老韩 (Phase B RM checklist 主权)。

---

**裁定: APPROVE-with-conditions (C1-C6)。Phase A 桩态进 main 安全, 不阻塞 GM 主线。**
