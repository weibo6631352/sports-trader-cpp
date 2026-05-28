# ADR-003 闭环 Sign-off

- Owner: 老郭 (cpp-architecture-second-opinion)
- Date: 2026-05-28 UTC
- Status: **Accepted (ADR-003 关闭, Conditional → Accepted)**
- 关联: ADR-003 (`docs/ADR/2026-06-15-arch-rm-key-v0.4-v0.3-v5-review.md`) + 老周 v0.5 / 老韩 v0.3.1 / 老孙 v5.1 / 老唐 v1.1
- 三态: Agreed / Compromised / Escalated (无"先这样吧")
- 死线: 6/26 (我自己设的) — 实际 5/28 提前 28 天交齐, **不发奖, 是底线**

---

## 1. C-Z2 R-20 §21 评审 (老周 v0.5)

**Agreed.** §21 五小节闭环, 不重定义字段 (引老王 v0.2 PIT / 老唐 v1.1 envelope / 小邓 data-contract), 只定架构层落点 — 边界正确.

- PIT assert 6 调用点 (§21.2): RiskGateway / PaperSigner / LiveSigner / BacktestSigner / IMLSignalEngine / audit::emit_decision — **完整, 6 个皆 hot/cold path 入口**. `[[nodiscard]]` 编译期防漏调, 双保险 (caller + framework Append) 同意. 600ns / 200us 性能预算未挤 G3.
- CI grep 8 项 (§21.5): now()/gettimeofday()/localtime()/mktime()/int 时间字段/缺 feature_snapshot_id/缺 model_id+inference_ts/缺 pit::AssertChain + 1 docs grep — **8 项覆盖反模式全集**, 与小宋 fixture + 老高 PR 模板 + 老练 nm 三方联签可执行.
- 上游 ts 优先级 5 档 enum + 7 数据源退化策略 (§21.3) 落表, 退化标识 audit emit 走现有 AET_RECON_DRIFT.kind 不开新 AET — 守住 12 AET 封闭性 (与老唐 v1.1 新 2 AET 解耦正确).

**评分: A** (无整改, 唯一 Compromised 是上游 ts 字段确认派单 @老李/@小段, 6/19 完成).

---

## 2. C-3 PREGAME 15s 取齐 (老周 + 老韩)

**Agreed.**

- 命名取齐: 老周 v0.5 §17.6.1 把 v0.4 PREGAME_NEAR/PREGAME_FAR/OUTRIGHT 改为老韩 v0.3 §14.1 命名 INPLAY_COLD/PREGAME/SETTLED — **命名权归阈值消费方 (RM owner)**, 设计正确.
- PREGAME HALT 15000ms 双侧落地: 老周 v0.5 §17.6.1 改 15000ms, 老韩 v0.3.1 §11.x 不动一行 + 加 `static_assert` 双校验 (PREGAME==15000 + ≤30000 D-06).
- OUTRIGHT/SETTLED 30000ms 红线: 老周顺手把 v0.4 OUTRIGHT 60000ms 收齐到 30000ms (D-06 ≤30s 硬上限) — 附带整改正确, **未被 ADR-003 C-3 明面要求但应落**.

**评分: A** (一致性 6 一致 / 0 不一致, ADR-003 §8 表 C-3 行从 ❌ 转 ✅).

---

## 3. C-4 InvalidIntentSubReason (老韩 v0.3.1)

**Agreed + 1 Compromised.**

- 设计: sub_reason 是 INVALID_INTENT enum 的**二级字段**不是第 22 enum — 21 reject enum 总数不破 ✅. RejectDetail struct 含 `code + sub_reason`, 仅 code==INVALID_INTENT 时非 NONE, 其他 reject 一律 NONE — 编码约束清晰.
- 5 sub_reason 完整性: BOOK_TS_ZERO / BOOK_TS_STALE / NAN_OR_INF / NEGATIVE / ILLEGAL_TICK — 覆盖小肖 §1 R-1 + §4.2 + §5 Case 6 全 4 项 + 老李 S1-002 tick_size 1 项 = **5 项匹配**.
- R3 评估顺序按廉价→昂贵 short-circuit (NaN→负数→tick→ts_zero→ts_stale), ≤50ns 不挤 200us 预算 ✅.
- 跨文档 sub_reason 扩展: 老孙 v5.1 §5.4.3 加 TS_ORDER_VIOLATED / TS_FUTURE / TS_UNKNOWN_SRC 3 项 — **8 项 sub_reason (5 + 3) 仍是字段而非 enum**, 21 总数硬约束未破.

**Compromised**: 老韩 v0.3.1 §3.10.x.3 撤回了 v0.3 §14.5 末行 "SETTLED→INVALID_INTENT+sub-reason" — 改走既有 MARKET_NOT_ACTIVE enum. 我裁定: **撤回正确**, 市场关闭非字段污染, 语义不应共用 INVALID_INTENT 通道. 但需注意老孙 v5.1 §5.4.3 表里写的是 `INVALID_INTENT (#19)`, 老韩 v0.3.1 §3.10.x.1 注释写的是 `INVALID_INTENT = 17` — **enum 序号两边不一致** (跨文档冲突, 见 §6).

**评分: A-** (设计 A, 序号小冲突待 6/19 修正).

---

## 4. C-2 IPC + BLAKE3 4 ts (老孙 v5.1 + 老唐 v1.1)

**Agreed + 1 Compromised.**

- schema 字段对齐: 老孙 v5.1 §5.4.1 SignRequest 4 ts (event_ts/data_source_ts/data_source_ts_source/ingestion_ts/as_of_ts) + SignResponse 2 ts (sign_request_ts/sign_complete_ts) ↔ 老唐 v1.1 §2.x WAL2 header 偏移 16/24/32/40 4 ts + payload 内 DataSourceTimestamp.Source enum — **字段级对齐**.
- signer ↔ audit 跨进程兼容: signer 端 PIT 4 不等式 assert (§5.4.2) + now+60s 上界 + UNKNOWN(255) 直拒 + INFERRED(2/3) audit P1 — 与老周 §21.2 framework 调用点 LiveSigner / PaperSigner **同一函数路径** (不双实现).
- 透传不重算: 老唐 v1.1 §6.2 明示 signer emit AET_ORDER_PLACED **透传 L3 的 4 ts**, 取证可区分决策 vs 签名时刻 — 与 R-20 §3 表一致.
- 双轨保留: 老唐 v1.1 §2.x header 4 ts 给 framework PIT + payload 内 v1 `evaluated_at_ns`/`ingested_at_ns` 仅作 BLAKE3 完整性双校验, 启动期 replay 验等价, 不一致 = P0 (T-04 篡改防护) ✅.

**Compromised**: 老唐 v1.1 §2.y 新增 AET_STRATEGY_DECAYED=30 + AET_STRATEGY_UNLOCK=31, 12 → 14 — 与老周 §21.4 "新 AET 不加, 用现有 AET_RECON_DRIFT.kind" **看似冲突**. 我裁定: **不冲突, 各管各事**. 老周 §21.4 说的是 R-20 退化标识不开新 AET (用 kind 子类), 老唐 v1.1 §2.y 说的是 STRATEGY_DECAYED/UNLOCK 是老韩 §16 kill switch 独立语义 (ADR-003 §7.2 单列), 与 R-20 4 ts 落地无关. 12→14 是 RM kill switch 派生, 不破 R-20 enum 经济性原则.

**评分: A**.

---

## 5. C-1 fill 仿真测试 (小肖 + 小蒋)

**Escalated → 决议: W3 末紧急派, 不推 W4.**

理由 3 条:
1. ADR-003 §10 C-1 死线 6/26, 与 C-2/C-3/C-4 同 deadline. 4 整改 3 个已交 (今天 5/28 提前 28 天), 唯独 C-1 测试未派 — 推 W4 = 拖 1 周, **打破"4 整改并行闭环"的承诺**.
2. paper engine spec OK 但 §18.6 仅 tcpdump+nm 不证仿真度 — fill 时序 + maker 撤单分布对齐老姜 E2E latency + 小袁 fill_rate sampler 不验证, **paper→live PnL 切换点失去基线**, 反过来污染 ADR-003 §7.1 D6 整改 (PaperSigner Compromised 转 Rejected 风险).
3. W4 已是 Sprint-2 收尾, 不能让一个 W1 末提出的整改条目跨 Sprint.

派单决议 (今天 5/28 立即发):
- @老周 §18.6 加第 6 测试: VirtualConfirm fill 时序对齐 — 6/05 截止 (W3 末)
- @小蒋 (paper engine v0.2): backtest framework 提供 fill 时序回放 fixture — 6/05
- @老姜 (latency-engineer): E2E latency 实测 baseline 提供 (n≥30 样本) — 6/03
- @小袁 (data-science): fill_rate sampler 输出 percentile 表 (p50/p90/p99) — 6/03

**Owner**: 老周主笔 §18.6, 小蒋+老姜+小袁 联签 fixture/baseline. **回到我手 6/05**, ADR-003 §10 C-1 闭合, 6/26 死线提前 21 天.

---

## 6. 跨文档一致性

**5 一致 / 1 小冲突 / 0 大冲突**:

| # | 议题 | 状态 | 解决路径 |
|---|---|---|---|
| 1 | PREGAME 15000ms (老周 §17.6.1 = 老韩 §11.x) | Agreed | C-3 已闭 |
| 2 | sub_reason 字段化 (老韩 + 老孙 + 老唐 三方协约) | Agreed | C-4 已闭 |
| 3 | 4 ts 全链路 (老周 §21 + 老孙 §5.4 + 老唐 §2.x WAL2 header) | Agreed | C-2 已闭 |
| 4 | 21 reject enum 总数不破 (老韩 sub_reason 字段 + 老孙 TS_* 字段) | Agreed | 守住 |
| 5 | 12→14 AET (STRATEGY_DECAYED/UNLOCK) vs R-20 不开新 AET | Agreed | §4 已澄清, 两件事 |
| 6 | **INVALID_INTENT enum 序号: 老韩 §3.10.x.1 = 17 vs 老孙 §5.4.3 = 19** | Compromised | 派 @老韩 6/19 v0.3.2 patch 校正一份, 老孙 v5.1 §5.4.3 改注释 (实际 enum 值以老韩为准) |
| 7 | 命名取齐 INPLAY_COLD/PREGAME/SETTLED (老周采老韩命名) | Agreed | classifier code 同步小袁 PR-9 |

冲突清单仅 #6 一条小数字冲突, 不影响 ADR-003 关闭.

---

## 7. 总评: ADR-003 关闭

**ADR-003 转 Accepted, 关闭.**

| 整改 | 评分 | 状态 |
|---|---|---|
| C-1 fill 仿真 | (W3 末派单) | **Escalated → 6/05 闭** |
| C-2 R-20 4 ts | A | **Accepted** |
| C-3 PREGAME 15s | A | **Accepted** |
| C-4 sub_reason | A- | **Accepted** (序号小冲突 6/19 patch) |

整体: **A** (4 整改 3 个 A 级, 1 个待 W3 末紧急闭).

ADR-003 §11 综合签字栏 5/28 全数关闭. GM-1/GM-2/GM-3 三项仍待老雷会签, 与本闭环解耦 (那是 RM kill switch + STALE_HALT + YubiHSM 物理三签, 不阻塞架构整改).

---

## 完成汇报

- **ADR-003 闭关**: **是**. 4 整改 3 已闭 (C-2/C-3/C-4 提前 28 天), C-1 转 W3 末紧急派 (6/05 闭), 提前 21 天.
- **4 整改最终评分**: 老周 v0.5 = **A** / 老韩 v0.3.1 = **A-** (enum 序号 6/19 patch) / 老孙 v5.1 = **A** / 老唐 v1.1 = **A**. 综合 **A**.
- **C-1 fill 仿真派单时间**: **今天 5/28 立即派**, 不推 W4. 老周 §18.6 + 小蒋 fixture + 老姜 baseline + 小袁 sampler, 6/05 (W3 末) 闭合, 老郭复核.

— 老郭 (cpp-architecture-second-opinion), 2026-05-28 UTC
