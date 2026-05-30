# A5 DD-feed 落地代码 review 结论 (v1)

- **owner:** 老韩 (risk-engineer, 风控合规部主管, RM 主权)
- **last_review:** 2026-05-31
- **范围:** review GM 落地的 commit `fe82fdb` (A5 daily_pnl → RM DD 熔断喂数), 对照 spec `laohan-a5-dd-feed-spec-v1.md`。只读 review, 不改主干。
- **裁决: APPROVE-with-nits** (无阻塞项; 2 个非阻塞 nit + M2 边界已正确标注)

---

## 裁决: ✅ APPROVE-with-nits

spec §10 点名的三处核心 (符号方向 / ×1e6 单位门 / best_bid 保守取价) **全部正确落地**, 无符号反、无单位漏、无溢出/竞态、无测试假绿。可进 MVP。下列 2 个 nit 非阻塞, 记 backlog。

---

## review 重点逐项核实 (核实际落地, 非凭 spec 想象)

### 1. §5 符号方向 — ✅ 正确, 天然对齐无需取反

落地: `paper_loop.cpp:765` `pnl_pusd += (bid - pv.avg_entry_price) * qty;` → `:771` `pnl_pusd -= cum_fee_pusd_;` → `:774` `set_daily_pnl((int64)(pnl_pusd*1e6))`。

- 亏损 (bid < avg_entry) → `(bid-avg_entry)<0` → MtM 为负; 减 cum_fee 更负; ×1e6 后仍负。
- RM 侧 `risk_gateway.cpp:548-550` `pnl = daily_pnl_usdc_.load(); if (pnl < 0) { loss = -pnl; ...}` — 喂负值正确进 DD 块, `loss>0` 与 `>= hard/soft_threshold` 比较方向对齐。
- HALTED 迁移 `risk_gateway.cpp:766-776` 同步用 `-pnl >= hard_threshold` 判, 与 check_position_caps_ DD 块同源, 无第二套阈值漂移。
- **核实 BUY 持仓符号: `size_usdc` 为 signed, BUY → 正 (`apply_fill` delta=+fill_size_usdc), qty>0**, 故亏损时 MtM 确为负, 不存在 qty 符号把 MtM 翻正的风险。无符号反掉风险。

### 2. §4 ×1e6 单位门 — ✅ 唯一通道, qty 转换正确, 无溢出

- **唯一 ×1e6 通道**: 全函数仅 `:774` 一处 `* 1'000'000.0` 喂 RM, 无第二通道。
- **qty 转换** `:762` `qty = (double)pv.size_usdc / 1'000'000.0` — `size_usdc` 是 signed micro pUSD (A1 后, position_view.hpp:35 实际单位 = micro, 见 nit#2 注释笔误), /1e6 → whole share, 正确。
- **截断/溢出核实**: `pnl_pusd × 1e6` 后 cast int64。M1 bankroll 量级 ~1e5 pUSD, 即便全仓浮亏 pnl_pusd 量级 ~1e5, ×1e6 = ~1e11 micro, 远低于 int64 上限 ~9.2e18 (留 7 个数量级余量)。double 在 ~1e11 量级整数部分精确 (< 2^53≈9e15), 无精度丢失。无溢出风险。

### 3. best_bid 保守取价 — ✅ 如 spec §1.4 落实, 无反向臆造亏损风险

- `:763-768` `hub_.Read(pv.token_id)` → `has_value()` 守卫 → `bid = bk->best_bid()` → `isfinite(bid) && bid>0 && bid<1` 三重守卫才计 MtM。
- **无效 bid 跳过浮盈贡献, 不臆造正值** — 落实 §1.4「不臆造正盈余掩盖亏损」。
- **无「反向臆造亏损」风险核实**: 空 book 时 `OrderBookFeatures.bids` 是 `std::array<OrderBookLevel,5>{}` 值初始化, `best_bid()=bids[0].price=0.0` (orderbook.hpp:67 `price{0.0}`), 被 `bid>0` 守卫挡掉 → **不会用 0 价算出 `(0-avg_entry)*qty` 的假浮亏**。无效 bid 该仓位 MtM 贡献净 0, 只承担下方 cum_fee, 正确。**且 `bids[0]` 是 array 非 vector, 空 book 无越界 UB。**

### 4. cum_fee 线程安全 + 单调性 — ✅ 单 writer 无竞态

- `cum_fee_pusd_` (hpp:283 `double{0.0}`): writer 仅 `PublishLedgerSnapshot:701` `cum_fee_pusd_ += pnl_fee` (loop_thread_), reader 仅 `FeedRiskGateway:771` (loop_thread_)。二者同线程顺序调 (paper_loop 调用序 PublishLedgerSnapshot→FeedRiskGateway), **单 writer 单 reader 同线程, 无竞态, 无需 atomic**, spec §4 论证落地正确。
- 单调累加 (M1 只买不平, fee 只增不减) 正确。

### 5. 5 个 T-A5 测试覆盖度 — ✅ 真覆盖 §7 五场景, 单位门真能抓

- **T-A5-1 (硬 kill + 单位门)**: -6000 pUSD 浮亏 → 喂 -6e9 micro ≥ 5e9 硬阈 → DAILY_LOSS_HALT + HALTED。**单位门真有效**: 断言注释明确「漏 ×1e6 则 = -6000 micro ≈ 0, loss < 5e9 不触发」→ 若 GM 漏乘, step `EXPECT_EQ(reject, DAILY_LOSS_HALT)` 会失败暴露。非假绿。
- **T-A5-2 (软方向 + is_close)**: -4000 ∈ [3k,5k), 开仓拒 / 平仓放行 / state 不迁 HALTED, 三断言齐全。
- **T-A5-3 (无双计)**: 连喂两次 -2k, 用软阈 3k 判别 (覆盖写 -2k<3k 不触; 累加 -4k≥3k 误触)。**判别严谨**: 2k 与 4k 跨在 3k 两侧, 软阈值是硬性 reject 边界, 区分度足够, 非「软阈值模糊」。
- **T-A5-4 (无效 bid)**: 不 Publish book → Read nullopt → MtM=0, daily_pnl≥0 不触 DD, 且仍 ever_fed。覆盖保守路径。
- **T-A5-5 (feed-liveness)**: daily_pnl NEVER FED→ever_fed; consec_loss 断言**仍 false** 锁 M2 边界。正确。
- **漏测边界 (nit#1, 非阻塞)**: 缺 best_bid == avg_entry 的浮亏临界 (MtM=0 边界) 与多仓位聚合 (≥2 token 求和) 用例。当前 5 测均单仓位。M1 实盘多仓位聚合是真实路径, 建议 M2 前补。不阻塞 (聚合逻辑是直白 for-sum, 风险低)。

### 6. §3/§9 M2 边界 — ✅ 注释已标清, consec_loss 正确未喂

- `paper_loop.cpp:755` 注释「M2: 接平仓 (realized≠0) 后须改为 realized(日界累加) + unrealized(时点), 见 spec §3」— M2 升级路径已在代码标清。
- `:738` 注释 consec_loss 延 M2, FeedRiskGateway 确实**不调** `set_consec_loss` → feed-liveness NEVER FED 是预期正确态, T-A5-5 锁定。符合 §6。

---

## Nits (非阻塞, 记 backlog)

- **nit#1 (测试覆盖)**: T-A5 缺「best_bid==avg_entry 浮盈浮亏临界 (MtM=0)」+「多仓位聚合 (≥2 token MtM 求和)」两个边界用例。建议 M2 前补。风险低 (聚合是直白 for-sum)。
- **nit#2 (注释单位笔误)**: `position_view.hpp:35` `std::int64_t size_usdc{0}; // signed cent` — 注释写 "cent", 但 A1 后实际单位是 **signed micro pUSD** (同文件头部行 12 注释也写 "signed cent", 而 paper_loop.cpp:762 与 spec §0 确认实际是 micro)。**单位实现正确 (落地代码按 micro 处理), 仅注释陈旧**, 不影响行为。建议派小米/小余清注释, 闭合 §8.1 纪律#3「单位语义文档漂移」土壤。
- **nit#3 (F6 守护宽松度, 观察项)**: `unit_contract_check.py` F6 的 `_F6_HAS_SCALE` 接受 `unit-contract-ok` 标记即放行, 理论上「写了注释但漏乘」可逃过 grep 守护。但实际代码行真含 `1'000'000`, 且 T-A5-1 单位门测试是运行时兜底, 双层防护下此逃逸不构成真风险。记观察, 不改。

---

## 结论

**APPROVE-with-nits。** spec §10 三处核心 (符号 / 单位门 / best_bid 保守) 全部正确落地, 无符号反 / 无单位漏 / 无溢出 / 无竞态 / 无测试假绿。RM DD 熔断从 NEVER FED 纸面化状态正式接通。3 个 nit 全非阻塞, 记 backlog (M2 前补多仓位+临界测试, 清 position_view 注释笔误)。M1 可带此实现进 MVP。
