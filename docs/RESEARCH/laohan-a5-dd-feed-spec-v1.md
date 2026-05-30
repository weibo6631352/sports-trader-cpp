# A5 — daily_pnl → RM DD 熔断喂数 spec (v1)

- **owner:** 老韩 (risk-engineer, 风控合规部主管, RM 主权)
- **last_review:** 2026-05-31
- **status:** RM 主权拍板 spec，供 GM MVP 攻坚期亲自实现 + review。本文只读分析 + spec，不改主干。
- **前置满足:** A1 (commit 19c84b0, ledger PnL 单位根治) + P1-9 (slippage gate 单位) + A4 (commit 8a641c2, feed-liveness 自检)。老郭事前否决（retro-synthesis §2「谁在 ledger 单位根治前接 DD = 把错 1e6 的 PnL 喂进熔断阈值」）的前置条件 **已由 A1 清除**。
- **范围:** 激活 `daily_pnl` (DD / 每日亏损) 熔断。`consec_loss` 延 M2（§6 论证）。

---

## 0. 现状事实核对（已读代码确认，非转述）

| 事实 | 出处 | 确认值 |
|---|---|---|
| FeedRiskGateway 调用点 | `paper_loop.cpp:615` | 在 `apply_fill`(607) + `PublishLedgerSnapshot`(610) 之后，loop_thread_ 内 |
| daily_pnl 当前不喂 | `paper_loop.cpp:734-735` | 故意留空，注释挂「待 ledger PnL 单位修复后接」 |
| RM 入口 | `risk_gateway.hpp:358` | `set_daily_pnl(std::int64_t usdc)` 收 **signed micro**，内部 `daily_pnl_usdc_.store(usdc)` + `mark_fed_(FeedKey::DailyPnl)` |
| DD 比较逻辑 | `risk_gateway.cpp:548-578` | `pnl = daily_pnl_usdc_.load()`；**仅当 `pnl < 0`** 进 DD 块；`loss = -pnl`(>0)；`loss >= hard_threshold` → `DAILY_LOSS_HALT`；`loss >= soft_threshold` 且 `!is_close` → `DAILY_LOSS_HALT`，`is_close=true` 放行 |
| 硬阈值 | `risk_gateway.cpp:555-559` | `cfg_.daily_loss_halt_usdc.v > 0` (默认 5e9 micro = 5k pUSD) → 取绝对值 5e9；否则 `hard_pct(0.05) × bankroll` |
| 软阈值 | `risk_gateway.cpp:562-563` | `soft_pct(0.03) × bankroll`。默认 bankroll 1e11 micro → soft=3e9 micro=3k pUSD |
| HALTED 状态迁移 | `risk_gateway.cpp:765-776` | `DAILY_LOSS_HALT` 且 `-pnl >= hard_threshold` → `state_.store(HALTED)`（const helper 外、evaluate 主路径执行） |
| PositionView 字段 | `position_view.hpp:30-37` | `condition_id / token_id / outcome / size_usdc(signed **micro**, A1) / avg_entry_price∈(0,1) / last_update_ts(=as_of_ts_ns)` |
| mark 价来源 | `orderbook_snapshot_hub.hpp:141` | `hub_.Read(token_id)->best_bid()` = `bids[0].price`；亦有 `microprice` / `mid` |
| reject 码 | `reject_enum.hpp:40-41` | `DAILY_LOSS_HALT=8` / `CONSEC_LOSS_HALT=9`（已存在，无需新增） |

**单位现状（A1 后）:** `PositionView.size_usdc` = **signed micro pUSD**。`size_usdc/1e6 = qty (whole pUSD share 数)`。`avg_entry_price` / `best_bid` ∈ (0,1) 是价格（whole）。

---

## 1. daily_pnl 喂什么（M1 买入阶段语义）— 拍板

**结论：喂「净未实现 MtM − 累计 fee」，保守用 best_bid 作清算 mark。即**

```
daily_pnl_pUSD = Σ_pos [ (mark_bid(token) − avg_entry) × qty ]  −  Σ_pos fee_paid
其中  qty = size_usdc / 1e6   (signed micro → whole share)
      mark_bid = hub_.Read(token_id)->best_bid()   (多头清算价，保守)
```

转 micro 后 `set_daily_pnl((int64)(daily_pnl_pUSD × 1e6))`。

**论证（为什么这样）：**

1. **realized 在 M1 = 0，无意义。** M1 只买不平（`PublishLedgerSnapshot:694` `pnl_realized=0.0` 写死）。realized 当 DD 代理 = 永远 0 = DD 永不咬 = 纸面化。**否决用 realized。**

2. **M1 唯一的真实亏损来源是浮亏（unrealized MtM）。** 买入持仓后只有价格下跌才亏。用「当前若按 best_bid 全平能拿回多少 vs 入场成本」作当日亏损代理，是 M1 唯一有信息量的亏损度量。**DD 必须用 unrealized MtM。**

3. **必须减 fee。** fee 是已发生的确定性现金流出（taker fee 已扣）。MtM 浮盈 0 但 fee 已付时，真实净值已为负。`PublishLedgerSnapshot:696` 已按 `(fill_size/1e6) × kSportsTakerFeeRate × price × (1−price)` 算 per-fill fee。DD 代理 = `MtM − Σfee`，与 `pnl_gross − pnl_fee` 口径一致（gross 已含 unrealized）。

4. **为什么 mark 用 best_bid 而非 mid/microprice（保守原则，铁律#2 纪律高于收益）：** 多头平仓只能砸到 **bid** 侧成交，best_bid 是真实可清算价。用 mid/microprice 会高估持仓价值 → 低估亏损 → DD 晚咬甚至不咬。风控取保守：**多头清算价 = best_bid**。`hub_.Read(token)` 拿不到有效 best_bid（NaN/≤0）的仓位，该仓位 MtM 贡献按 **0 浮盈但保留已付 fee**（即只计 `−fee`，不臆造正浮盈），避免用脏价喂出假盈利掩盖亏损。

> M1 仅 YES 多头（`apply_fill(..., Outcome::Yes, ...)`，paper_loop:607）。空头/NO 腿留 M2，本 spec 只处理多头 best_bid 清算语义。

---

## 2. 聚合点 + 方式 — 拍板

**结论：在 `FeedRiskGateway()` 内新增一段，遍历 `position_ledger_.get_all_positions()`，逐仓位 `hub_.Read(token_id)` 取 best_bid 现算聚合，一次性 `set_daily_pnl`。不复用 PublishLedgerSnapshot 的 per-condition 累加。**

**为什么不复用 PublishLedgerSnapshot：**
- 它**按单个 condition** publish 到 `ledger_hub_`（`paper_loop.cpp:719` `ledger_hub_.Publish(condition_id, lf)`），**未做跨仓位总聚合**，且每 fill 只更新命中的那条 condition。
- 若在那里累加全局 daily_pnl，需要跨 tick 维护「上次该 condition 贡献多少」做增量冲销，否则**双计**（同一持仓每 tick 被重复加）。这是过度设计且易错。
- **FeedRiskGateway 已经是「全量覆盖喂 RM」语义**（见 738-739 注释「全量覆盖 (PL 真值, 自愈)」）。daily_pnl 走同款全量覆盖：每 tick 重算全部仓位的净 MtM，`set_daily_pnl` 覆盖写（atomic store，非累加）。**天然无双计，自愈。** 与现有 exposure 喂法同构，最干净。

**精确改动点：`src/stcpp/paper/paper_loop.cpp` FeedRiskGateway()（737-746 行），在 exposure 两段 for 之后追加：**

前（735 注释 + 746 函数尾）：
```cpp
// daily_pnl (DD) / consec: M1 暂不喂 (... 待 ledger PnL 单位修复后接)。
...
    for (auto const& [tid, micro] : position_ledger_.get_per_outcome_exposure()) {
        rm_.set_outcome_exposure(tid, micro);
    }
}
```

后（删 734-735 旧「暂不喂」注释中 daily_pnl 部分，consec 保留为 M2；在 outcome for 之后加）：
```cpp
    for (auto const& [tid, micro] : position_ledger_.get_per_outcome_exposure()) {
        rm_.set_outcome_exposure(tid, micro);
    }

    // A5 (老韩 spec §1-§4): daily_pnl → DD 熔断。M1 买入阶段语义 = 净未实现 MtM − 累计 fee。
    //   全量覆盖 (set_daily_pnl 是 atomic store 非累加) → 天然无双计, 与 exposure 喂法同构, 自愈。
    //   保守: 多头清算 mark 用 best_bid (砸 bid 侧成交真值); 无效 bid 仓位 MtM 贡献按 0 浮盈 (不臆造正盈余)。
    double pnl_pusd = 0.0;
    for (auto const& pv : position_ledger_.get_all_positions()) {
        const double qty = static_cast<double>(pv.size_usdc) / 1'000'000.0;  // unit-contract-ok: signed micro→whole share
        const auto bk = hub_.Read(pv.token_id);
        if (bk.has_value()) {
            const double bid = bk->best_bid();
            if (std::isfinite(bid) && bid > 0.0 && bid < 1.0) {
                pnl_pusd += (bid - pv.avg_entry_price) * qty;  // 浮动 MtM (清算价 best_bid)
            }
            // 无效 bid: 跳过浮盈贡献 (保守, 不臆造正值)
        }
    }
    pnl_pusd -= cum_fee_pusd_;  // §1.3: 减累计 fee (见 §4 fee 累加器)
    // §4 单位门: pUSD → micro, ×1e6 (unit-contract-ok: pUSD→micro, 防 P0-2/P1-9 同型 bug)
    rm_.set_daily_pnl(static_cast<std::int64_t>(pnl_pusd * 1'000'000.0));
}
```

> 注：`hub_.Read` 返回 `std::optional<OrderBookFeatures>`（与 234 行 `const auto opt = hub_.Read(token0)` 同款），故 `has_value()` + `bk->`。GM 实现时按实际返回类型对齐（若返回引用/指针，调整解引用），语义不变。
> `<cmath>`（std::isfinite）应已 include；若无，GM 补。

---

## 3. daily 窗口 vs 时点 — 拍板（MVP 不过度设计）

**结论：M1 用「时点净 MtM」，不上 daily 累加器 + 日界 reset。**

**论证：**
- M1 全程只买不平，无 realized 现金流，**没有「跨日累计已实现亏损」这回事**。当日亏损 100% 等于当前持仓的浮亏 + 已付 fee，这是个**时点量**，每 tick 重算即反映「截至此刻若清算的当日盈亏」。
- 时点净 MtM 是**单调反映当前风险敞口浮亏**的代理，正是 DD 熔断要拦的东西（继续加仓只会放大浮亏）。
- daily 累加器 + 日界 reset 需要：event_ts 日界判断、reset 状态、隔夜持仓的「昨日浮亏是否结转」语义裁定 —— 这些在「只买不平、无 realized」的 M1 **全是空转复杂度**，且引入 reset 状态 = 新 bug 面。**MVP 否决累加器。**
- **M2 必须升级：** 一旦 M2 引入平仓（realized ≠ 0），DD 必须改为 `realized(当日累计) + unrealized(时点)`，且 realized 需要日界 reset 累加器。本 spec 的时点 MtM 是 M1 专用代理，**M2 接平仓时必须重做 §1/§3**（在代码注释标 `// M2: 接平仓后须加 realized 日界累加器`）。

---

## 4. 单位 — 拍板（防 P0-2/P1-9 同型复发）

**结论：**
1. **qty 转换：** `size_usdc(signed micro) / 1e6 = qty (whole share)`，行内注释 `unit-contract-ok: signed micro→whole share`。
2. **MtM 在 whole pUSD 域算：** `(bid − avg_entry)[价格,whole] × qty[whole share] = pUSD`。
3. **fee 在 whole pUSD 域：** 见下 fee 累加器，单位 whole pUSD。
4. **最终转 micro：** `set_daily_pnl((int64)(pnl_pusd × 1e6))`，行内注释 `unit-contract-ok: pUSD→micro`。这是唯一的 ×1e6 通道点。
5. **不引入 domain helper 强转：** daily_pnl 是 **signed**（可正可负），现有 `MicroPUSD` helper 多为非负 cap 语义（`from_micro/to_pusd`），signed PnL 走显式 `× 1e6` + `unit-contract-ok` 注释更清晰、零歧义。RM 入口 `set_daily_pnl(int64)` 本就收裸 micro int64，对齐。

**fee 累加器（§1.3 配套，新增最小状态）：**
- PublishLedgerSnapshot 已按 per-fill 算 `pnl_fee`（696-697），但它只 publish 到 ledger_hub_、**不跨 fill 累加**。DD 需要「累计已付 fee」。
- **最小改动：** PaperLoop 加一个成员 `double cum_fee_pusd_{0.0};`，在 `PublishLedgerSnapshot` 算出 `pnl_fee` 后 `cum_fee_pusd_ += pnl_fee;`（单调累加，whole pUSD）。FeedRiskGateway §2 段读它。
- **为什么累加 fee 而非时点重算：** fee 是已付沉没成本，跨所有历史 fill 累计，不是时点量。MtM 是时点（每 tick 覆盖），fee 是累计（单调加）。二者口径不同但都正确：`daily_pnl = 时点MtM − 累计fee`。M1 不平仓，fee 只增不减，单调累加正确。
- 线程安全：`cum_fee_pusd_` 与 `set_daily_pnl` 都在 loop_thread_ 内（PublishLedgerSnapshot:610 与 FeedRiskGateway:615 同线程顺序调用），单 writer，无需 atomic。若 monitor 线程要读，再升 atomic（M1 不需要）。

**unit_contract_check.py 护栏：** 建议给该脚本加一条匹配 `set_daily_pnl(` 的调用点必须含 `unit-contract-ok: pUSD→micro` 或 `× 1'000'000` 的 grep 护栏（与 P0-2/P1-9 既有护栏同档）。**派小宋/小余落地，非 M1 阻塞项**，但强烈建议本次一并加，闭合「单位 bug 静默复发」土壤（§8.1 纪律#3）。

---

## 5. 熔断阈值语义 — 确认（喂入符号/单位与 RM 比较对齐）

**已读 `risk_gateway.cpp:548-578` 确认，喂入契约：**

| 维度 | 值 | 对齐 |
|---|---|---|
| daily_pnl 符号 | **signed**，亏损为**负**（盈利为正） | RM `if (pnl < 0)` 才进 DD 块；`loss = -pnl > 0` |
| daily_pnl 单位 | **micro pUSD** | RM 阈值同 micro（halt_usdc.v / pct×bankroll(micro)） |
| 硬阈值 | 默认 `daily_loss_halt_usdc.v = 5e9 micro = 5k pUSD`（>0 覆盖 pct） | `loss >= 5e9` → DAILY_LOSS_HALT + state→HALTED |
| 软阈值 | `soft_pct 0.03 × bankroll`。默认 bankroll 1e11 micro → `3e9 micro = 3k pUSD` | `loss >= 3e9` 且 `!is_close` → DAILY_LOSS_HALT（拒新开仓，放平仓） |

**关键对齐点（GM 实现必须保证）：**
- 喂入的 `pnl_pusd` **亏损时为负**（`(bid − avg_entry) < 0` 时 MtM 为负 + 减 fee 更负）→ ×1e6 后仍为负 → RM `pnl < 0` 分支正确触发。**符号方向天然正确，无需取反。**
- 默认配置下硬阈值(5k)**大于**软阈值(3k)。亏损路径：浮亏达 3k → 软熔断拒新开仓；达 5k → 硬 kill + HALTED。**符合 -3%软/-5%硬 GM §9 裁决。**
- 注意 bankroll 必须已喂（A4 FeedKey::Bankroll，paper_loop 已 `set_bankroll`），否则软阈值 `pct × 0 = 0` → 任何浮亏立即触发软熔断。**确认 paper_loop 启动即喂 bankroll（已有），FeedRiskGateway 喂 daily_pnl 时 bankroll 已就位。** 这是 §5 唯一的隐性依赖，GM 实现后用 feed_liveness_report() 验 Bankroll ever_fed=true。

---

## 6. consec_loss — 延 M2（拍板）

**结论：consec_loss 本次不喂，延 M2。** 依据：连续亏损 (consecutive losing trades) 的计数源是**平仓产生的 realized 盈亏**——一笔 trade 平仓后才知道它是盈是亏。M1 只买不平，**没有任何已结束的 trade**，consec 无数据源，喂 0 是唯一诚实值（保持 `set_consec_loss` 不调，feed-liveness 显示 ConsecLoss NEVER FED 是**正确状态**，M1 不算缺陷）。M2 接平仓后，在 realized 结算点喂 consec。与 retro-synthesis 一致。

**代码处理：** FeedRiskGateway 不调 `set_consec_loss`。在 §2 改动的注释里保留一行 `// consec_loss: 延 M2 (M1 只买不平 → 无平仓 trade = 无连亏源)`。A4 feed-liveness 自检会把 ConsecLoss 标 NEVER FED——这是**预期**，不应在 M1 当告警阻塞（见 §7）。

---

## 7. 测试 — 设计

**T-A5-1（DD 喂入触发硬 kill，paper_loop 层）：**
1. 构造一个亏损持仓：`apply_fill` 买入 YES，`avg_entry=0.80`，`qty` 足够大（如 size 使 |浮亏| > 5k pUSD）。
2. hub_ 注入该 token 的 book，`best_bid = 0.20`（深跌）→ 浮亏 = `(0.20−0.80)×qty`。选 qty 使浮亏 −fee ≤ −5000 pUSD。
3. 调 `FeedRiskGateway()` → 断言 `rm_.feed_liveness_report()` 中 DailyPnl `ever_fed=true`。
4. 构造一笔新 BUY OrderIntent 过 `rm_.evaluate()` → 断言 `reject == DAILY_LOSS_HALT` 且 `rm_.state() == HALTED`。
5. 反向用例：best_bid=0.79（浮亏极小 < soft 3k）→ evaluate 不因 DD reject。

**T-A5-2（软熔断方向，is_close 放行）：**
1. 构造浮亏 ∈ [3k, 5k) pUSD（软触硬不触）。
2. BUY intent（is_close=false）→ `DAILY_LOSS_HALT`。
3. SELL/平仓 intent（is_close=true）→ **不因 DD reject**（放行平仓）。

**T-A5-3（无双计自愈）：**
1. 同一持仓，连调 `FeedRiskGateway()` 两次（模拟两 tick）。
2. 断言两次后 `daily_pnl_usdc_` 值相同（覆盖写，非累加）→ 证明全量覆盖无双计。

**T-A5-4（无效 bid 保守）：**
1. hub_ 该 token best_bid = NaN（或无 book）。
2. FeedRiskGateway → 该仓位 MtM 贡献 0 浮盈，但 fee 仍计入 → daily_pnl = −cum_fee（≤0），不臆造正盈余。

**T-A5-5（feed-liveness 状态转移）：**
1. 喂前：`feed_liveness_report()` DailyPnl `ever_fed=false`（NEVER FED）。
2. FeedRiskGateway 一次后：DailyPnl `ever_fed=true`，`last_fed_ns > 0`。
3. ConsecLoss 仍 `ever_fed=false`（§6 预期，M1 正确状态，断言它**仍为 false** 以锁 M2 边界）。

**单位门测试（P0-2/P1-9 同档）：** T-A5-1 的 5k 触发用例即单位门——若 GM 漏 ×1e6，喂入值会小 1e6（−5000 而非 −5e9 micro），`loss=5000 < hard 5e9` → **不触发**，测试 step 4 失败暴露。故 T-A5-1 兼任单位回归门。

---

## 8. 红线引用（§8.1 纪律#1：粘原文，禁转述）

**铁律#2（CLAUDE.md §3）原文：**
> 2. **纪律高于收益（Discipline > PnL）** — 风控红线不可越界，宁可错失机会不可踩线。

→ §1.4 mark 价取 best_bid（保守，宁可早熔断不可晚熔断）、§1.4 无效 bid 不臆造浮盈，均落此铁律。

**§8 红线原文（相关条）：**
> - 任何下单链路绕过 `RiskManager` → 立即回滚 + post-mortem
> - **所有数据源使用必须标记时间信息（4 时间戳契约：event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts）→ P0（ADR R-20）；时间戳优先用数据源自带，禁本地 `now()` 替代上游 ts**

→ 本 spec 不新增下单链路（仍走既有 evaluate），不绕 RM。daily_pnl 喂入不携带新 ts（set_daily_pnl 无 ts 参数，仅记 last_fed_ns 诊断用，非 R-20 4ts 链路），不触 R-20。

**§8.1 纪律#3（ABI/字段单位变更必触发下游审计）原文：**
> 3. **ABI/字段单位变更必触发下游审计（补 R-4 配套）：** 任何字段重命名/单位变更（如 `size_usdc → size_pUSD_micro`）必须 audit 全部比较点/消费点的单位一致性，否则会「静默架空」依赖该字段的红线（caps/exposure/bankroll）。

→ §4 单位门 + §7 单位门测试 + unit_contract_check.py 护栏建议，落此纪律。daily_pnl 是新接通的消费点，必须过单位 audit。

**DAILY_LOSS_HALT 拒单码定义原值（`reject_enum.hpp:40`）：**
> `DAILY_LOSS_HALT = 8,`

软 + 硬熔断均复用此码（`risk_gateway.cpp:567,573`），无新增拒单码。

---

## 9. M1 必须 / M2 可延 清单（诚实标注）

| 项 | M1 必须 | M2 延 | 备注 |
|---|---|---|---|
| daily_pnl 喂入（净 MtM − fee，best_bid 清算） | ✅ | | §1/§2/§4 |
| 时点 MtM（不上 daily 累加器） | ✅ | | §3 MVP 取舍 |
| 软/硬熔断方向（is_close 放行） | ✅ | | §5，逻辑 RM 侧已有，仅喂数激活 |
| 无效 bid 保守处理 | ✅ | | §1.4 |
| cum_fee 累加器成员 | ✅ | | §4 |
| consec_loss 喂入 | | ✅ | §6，无平仓源 |
| daily 累加器 + 日界 reset | | ✅ | §3，M2 接平仓 realized 后 |
| 空头/NO 腿 MtM | | ✅ | §1，M1 仅 YES 多头 |
| realized 入 daily_pnl | | ✅ | §3，M2 平仓后 |
| unit_contract_check.py set_daily_pnl 护栏 | 建议本次 | | §4，派小宋/小余，非阻塞 |

---

## 10. GM 实现 checklist（一页）

1. `paper_loop.cpp` PaperLoop 加成员 `double cum_fee_pusd_{0.0};`
2. `PublishLedgerSnapshot` 算出 `pnl_fee` 后加 `cum_fee_pusd_ += pnl_fee;`（698 行附近）
3. `FeedRiskGateway`（737-746）outcome for 之后追加 §2 daily_pnl 段；删 734-735「暂不喂 daily_pnl」旧注释，consec 注释保留为 M2
4. 确认 `hub_.Read` 返回类型（optional），对齐 `has_value()/->`
5. build 全绿 + ctest，新增 T-A5-1..5
6. 跑 daemon 首 tick：`feed_liveness_report()` 应显示 DailyPnl `ever_fed=true`、ConsecLoss `ever_fed=false`（预期）
7. （建议）unit_contract_check.py 加 set_daily_pnl 护栏

**审：** GM 实现后回风控 review（老韩），重点核 §5 符号方向 + §4 ×1e6 单位门 + best_bid 保守取价。
