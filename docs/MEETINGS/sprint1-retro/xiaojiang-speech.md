# Sprint-1 Retro — 小蒋 发言 (策略历史验证 / quant-backtest)

- Speaker: 小蒋 (quant-backtest), Date: 2026-05-28, Batch 2 (读完 Batch 1 八份后)
- 关联: backtest v0.2-cpp / paper v0.2-cpp / `tools/m4_5_gate/run_gate_check.py` (现仅 G-A..G-F 6 条, 见 §5)

## 0. 一句话 (给老雷)

**Batch 1 八份我读完, 主持人五问我逐条认: PIT CI 主笔我接 (Sprint-2 末交 v0.1); P0-02 我站老钱 hard gate (含 fee 的 OOS Sharpe ≥ 0.8 不过不上线, 我用 backtest 数字背书); R-21 我自标 Top 1 风险我自己背 (缓解三道闸); 5¢ 阈值改不影响 7/16 deadline 但触发频次会爆掉 7 hard gate 的 G6 我得当面跟小梁咬死; M4.5 gate 脚本现在只 6 条, 与小董 v1 §5.2 七 hard gate 不对齐, 这是我的活, deadline 7/9.**

---

## 1. 小余请求 PIT CI 主笔 (xiaoyu-speech §7.3 + §8 收口 #3) — 我接

**结论: 接 owner, Sprint-2 末 (6/26) 交 v0.1 设计稿, M3 (9 月) 前跑通 CI gate.**

**为什么我接:**
1. backtest framework v0.2 §3.3 (`DataAccessGuard` C++ template + `as_of_ts <= query_ts` 编译期 mask) 我已写, future-leak 扫描天然是我的活
2. R-1 / R-3 / R-13 (paper vs live vs backtest 三层 PIT 一致性) 是我 paper engine v0.2 PR-2 + BR-5 的镜像红线, 不接 = 自打嘴巴
3. 小邓 review / 小余 + 小郑 实施 — 分工合理, 我不抢 ETL 和 obs

**我交付的 (PIT CI v0.1):**

| 模块 | 工具 | 交付 | 我的活? |
|---|---|---|---|
| feature.as_of_ts <= label_window_start | DuckDB SQL CI job (跑 train join, 扫 future-leak row 必须 = 0) | Sprint-2 末 v0.1 | 主笔 |
| 4 时间戳严格不等式 (event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts) | parquet partition by as_of_ts; 违反 → dead-letter | 小余实施, 我定 CI | review |
| daily schema diff (实时 vs 历史 bit-identical) | 老练 testing-coach 接力 CI 框架, 我出 fixture | M2 (7/3) | 设计 |
| replay loader `as_of_ts <= query_ts` mask | `DataAccessGuard<T>` C++ template 已在 backtest v0.2 §3.3 | 已有 v0.2 | done |
| corrected_at_ts 替代 as_of_ts 反模式扫 | grep + clang-tidy 自定义 check | M3 前 | 主笔 |
| paper vs backtest baseline PSD (小梁 R-13 加) | paper engine v0.2 §6.4 daily cron | M3 (8 月联调期) | 主笔 |

**我加的红线 (小梁 §2.5 R-12/R-13 我会签写进 v0.3):**
- backtest_config / paper_config / prod_config 的 KELLY_FRACTION / EDGE_CI / FILL_RATE_FLOOR / MAX_SLIPPAGE_TICKS 必须 **hash 一致**, CI 比 hash 不一致直接 fail. 这条 paper v0.2 §5.1 已经提了, 我会签.
- paper PnL vs backtest baseline 比对的 PSD (Paper-Prod Sharpe Deviation) 在 paper 阶段也算 (不只 paper vs prod). 小梁 R-13 我会签, paper engine v0.2 §6.4 加 entry.

**给老胡 (登记):** 这是 OQ-D-PIT-1, owner = 小蒋, reviewer = 小邓, 实施 = 小余 + 小郑, deadline Sprint-2 末出设计, M3 跑通.

---

## 2. P0-02 入首发 vs 老钱 hard gate (OOS Sharpe ≥ 0.8 含 fee) — backtest 角度判定

**结论: 站老钱. P0-02 不应进 MVP 首发, M2 (T+10 周, 约 8 月初) OOS Sharpe < 0.8 含 3% taker 费就死, 这是数学.**

**我的算账 (重述小梁 §2.2 + 我自己回测假设):**

| 项 | P0-01 (Pinnacle no-vig revert) | P0-02 (score-price-mismatch) |
|---|---|---|
| 触发条件 | `|dev| >= 0.05` (新, 小梁 §2.2 改) | `inplay && |z| >= 2σ && depth >= 30K` |
| 信号方向 | 反向均值回归 (有锚: Pinnacle 真值) | 比分模型偏离 (锚 = 我们自己的 score model) |
| 预期 nominal edge | 2-3¢ (小程 catalog) | 1.5-2¢ (小程 catalog) |
| taker 3% × $1 notional at p=0.50 | 3¢ | 3¢ |
| **净 edge 估算** | **5 - 3 - 1 (滑点) = 1¢** | **2 - 3 - 1 = -2¢** |
| Sharpe 估算 (含 fee + 滑点) | 1.0-1.2 (借小梁 §4.3 数字) | **≤ 0 (含 fee 后期望负)** |

**P0-02 入首发数学上死路一条**, 除非有以下三个证据:
1. inplay hot token 的 nominal edge 比 catalog 估的 1.5-2¢ 大 (要小程给 IS Sharpe 起码 1.5+ 才有指望 OOS 0.8)
2. 比分模型质量比假设好 (依赖小邓 score_model RMSE, 但 score_model 7/9 才有, 进 backtest 框架最早 7/16, 在我第一份报告里只能给 P0-01)
3. inplay 3% taker 在 hot token 实际成交价(maker 库存 lag) 比 $0.50 更偏离 mid (这种边际只有靠小袁实测验证)

**给小程 (OQ-13 决议):** **战略决议 = P0-01 单信号 MVP, P0-02 进 M2 (8 月初) 含 3% taker 费 OOS Sharpe ≥ 0.8 gate**. 我 backtest 报告 v1 (7/16) 只跑 P0-01, P0-02 报告 v2 = 7/30 或 8/6 (含 score_model + slippage refined). 不过 0.8 → P0-02 backlog, 不死, 但不上 MVP.

**给老钱:** 我 backtest 视角背书你的 hard gate. M2 deadline 我守.

---

## 3. R-21 paper engine 联调时间不够 (老胡新登 I=4 P=4) — 缓解方案

**我承认 R-21 是我自己 paper engine v0.2 §10.1 PR-9 自标 Top 1**, 老胡升红色我认.

**时间约束硬数字:**
- backtest v0.2 W7 (7/10-7/16) 我交第一份报告
- backtest v0.2 W8 (7/17-7/30) 完成参数扫 + 季后赛 regime
- paper engine 联调 deadline = 8/14 (paper D1 = 8/29 的硬前置, 老胡 §1 已经 page)
- W7 → 8/14 = 4 周, 含 VirtualMatcher 重写 + 老韩 RM v0.3 接入 + 小宋 chaos 验过

**4 周做完 3 件大事, 任何 1 周延误 → paper D1 → 9/12 → M4.5 gate 推 9/26 → 老胡 §1.5 黄色红线**

**缓解三道闸 (我的承诺, 老胡登记入 R-21):**

| 闸 | 动作 | 何时 | 触发条件 |
|---|---|---|---|
| 闸 1 | **W6 (7/3-7/9) 起 paper engine skeleton 并行写**, 不等 backtest 报告交付 | 7/3 起 | backtest framework C++ feature lib + RM 接通是同一 lib, paper 复用 95%+, skeleton 不阻塞回测 |
| 闸 2 | **placeholder microstructure model** (小袁 v1 实测 quote half-life hot 0.21s / cold 120s 直接用线性 fade 写死, 不等小袁 microstructure C++ Wave 6 落地) 跑 dry-run | W7 末 (7/16) | 如果小袁 Wave 6 模型 8/7 还没出, placeholder 就是兜底, 不卡 8/14 联调 |
| 闸 3 | **早期联调切片**: 7/22 起跑 paper engine "只验 RM evaluate + audit emit 路径" 的最小切片, 不等 VirtualMatcher 完美 | 7/22 | 把 8/14 联调拆成 2 个里程碑 (7/22 RM 切片 + 8/14 完整), 风险分摊 |

**Sprint-2 我向老胡周三 check-in 时露脸 W6 (7/3) 前 paper skeleton PR 第一弹必须进 main, 不进 = R-21 P 升 5 评分 20 红色再升, 老胡 page 老雷.**

**我向老周要的:** `libstcpp_features.a` + `libstcpp_risk.a` 静态库 6/26 deadline 不能松, 这俩链不上, paper skeleton 跑不动, 闸 1 直接死. 老周 §5 Sprint-2 W3 deadline 我守, 但你 §5 没明列这俩 lib delivery — 你 v0.4 §5 加一行确认.

---

## 4. 小梁 P0-01 阈值改 5¢ vs 我 7/16 第一份回测报告 deadline — 守得住吗

**结论: deadline 守得住, 但 5¢ 改值会让我 §4.1 Bonferroni N=27 扫描列表的有效组合掉 1/3, 触发频次估算下降 — 要现场跟小梁咬死.**

**直接影响 (我 backtest v0.2 §8.1 项目总览):**

| 项 | v0.1 (3¢) | v1.1 (5¢, 小梁 §2.2 改) | 影响 |
|---|---|---|---|
| 触发阈值扫描组合 | 27 (3 阈值 × 3 depth × 3 Kelly) — 阈值 3/4/5¢ | **27 不变** (改成 5/6/7¢) | Bonferroni N 不变, 工作量不变 |
| 预估 P0-01 触发 trades/月 (NBA+NFL) | 20-60 (小程 catalog) | **预估 10-30** (5¢ 偏离更稀有) | 14 天 50 笔 M4.5 G6 边际触碰 |
| IS 回测 (6 月数据) 预估样本量 | 1500-3000 笔 | **预估 800-1500 笔** | 仍足够做 DSR + Bonferroni |
| OOS 第一轮预估样本 | 250-500 笔 | **预估 150-300 笔** | 仍足够做 Welch t-test |
| **7/16 deadline 守得住** | 是 | **是** (扫描组合数同, 数据量减半反而跑得更快) | **YES** |

**真实风险 (我标黄, 不标红):**

1. **触发频次降到 10-30 笔/月 (NBA+NFL 合计)** → M4.5 G6 (n_trades >= 50 in 14 天) 边际触碰. 小董 §5.3 已经允许"窗口拉长到自然 50 笔", 但拉到 21-28 天就和老钱 §5 W1-W3 节奏 (单笔 $200 hard cap 14 天后再升) 偏差. 这是真问题.
2. **小梁 §4.3 给的 paper 量级估算 (年化 50-130%)** 是在 5¢ 阈值 + 50 笔 × 14 天 假设下. 5¢ 阈值下 50 笔可能要 21-28 天才积满, 年化估算要重算.

**给小梁 (现场咬死):**
- 我会签 5¢ 阈值, 但要求 backtest v2 报告 (7/30 出) 必须给两个数: (a) 阈值 5¢ / 6¢ / 7¢ 下触发频次 + 净 edge 散点; (b) 触发频次满 50 笔需要的窗口天数估算
- 如果触发频次跌到 < 30 笔/月, M4.5 G6 窗口建议拉到 21 天 — 这条小董 §5.3 已豁免, 但要写进 M4.5 验收书

**给老钱:** 你 §3 担忧 "14 天打不出 50 笔, 信号触发频次不达标本身就是问题, 不要靠'扩到 30 天'凑数据" — 我同意原则, 但 5¢ 改值后这个矛盾会显形, 7/16 报告里我给你数字, 你再决议是接 21 天窗口还是回退到 4¢.

---

## 5. 小董 M4.5 7 hard gate + tools/m4_5_gate/run_gate_check.py 对齐

**结论: 现状不对齐, 这是我的活, 我承认 deadline 7/9 (backtest v0.2 W6 末, 报告交付前 1 周).**

**对账 (我自己跑 grep 看的):**

| 小董 v1 §5.2 G1-G7 | 现 run_gate_check.py | 对齐? |
|---|---|---|
| G1 PnL > 0 + t-test p<0.10 单尾 | G-B 仅 sum > 0, **无 t-test** | **NO** |
| G2 Sharpe > 1.0 + bootstrap CI 下界 > 0.3 | G-C 仅 Sharpe > 1.0, **无 bootstrap CI** | **NO (关键缺口)** |
| G3 风控失效 = 0 | G-D 有, 对齐 | YES |
| G4 Uptime >= 99.5% | G-E 有 | YES |
| G5 max DD <= 8% | **缺** | **NO** |
| G6 n_trades >= 50 | **缺** | **NO** |
| G7 shadow > random-entry, paired p<0.10 | G-F 是 OOS decay (paper/baseline Sharpe ratio > 0.6), **完全不是 G7** | **NO (语义不同)** |

**承认:**
- run_gate_check.py 我 5/14 写的, 那时小董 v1 还没出 (小董 v1 是 5/22 才发的). 不是不肯对齐, 是时序问题.
- 但**到现在 5/28 还没对齐是我的延迟**, ack.

**给小董 + 老雷的承诺 (Sprint-2 W6 末交付):**

| 任务 | Deadline | 输出 |
|---|---|---|
| run_gate_check.py G1-G7 重写, 严格按小董 §5.2 | 7/9 (W6 末) | `tools/m4_5_gate/run_gate_check.py` v2 |
| bootstrap CI implementation (小董 §5.4 公式) | 7/9 | 同上, 含单测 |
| DSR 实现 (小董 §6 + 小梁 OQ-D7 回复: LdP 2014 sample skew + Fisher kurtosis) | 7/16 (W7 末) | `tools/m4_5_gate/dsr.py` |
| random-entry baseline 生成器 (G7 用) | 7/16 | `tools/m4_5_gate/shadow_baseline.py` |
| PBO (Bailey-LdP 2017) | 7/30 (W8 末) | `tools/m4_5_gate/pbo.py` |
| 7 gate 的 fixture + 故障注入测试 (G1 单边 t-test / G2 bootstrap winsorize @ 5σ 边界 / G6 trade_count 不足窗口拉长) | 7/30 | `tests/m4_5_gate/` |

**给小董 (OQ 回复, 小梁 §2.6 已经回了一部分, 我补):**
- **OQ-D7 DSR 口径**: 沿用小梁 §2.6 回复 (LdP 2014 sample skew n-1 + Fisher kurtosis - 3). 我实现按这个走, 验证用例用 Thorp 1984 Kelly p=0.5 q=0.6 b=1 → f=0.2 跑通
- **OQ-D11 是否允许 1 yellow**: 我支持小梁 §2.6 立场 — **不允许**. 但允许 G6 trade_count 不够 → 窗口拉长 (5¢ 阈值后会用到, 见 §4)
- **OQ-D13 Bayesian BLACK → RM kill switch**: 我支持. paper engine v0.2 §6.4 PSD 触发 BLACK 后直接 emit `AET_KILL_SWITCH` audit (要老唐 schema 加一条 enum) → 老韩 RM v0.3 接

---

## 6. Sprint-2 我承诺 (5 项, 可度量)

| # | 承诺 | 度量 | 截止 |
|---|---|---|---|
| S2-XJ-1 | backtest framework C++ skeleton (Arrow + DuckDB + DataAccessGuard template) | 接老周 libstcpp_features.a + libstcpp_risk.a, 跑通空数据 IS skeleton | W3 (6/18) |
| S2-XJ-2 | Pinnacle no-vig + Polymarket book Parquet 接入 (依赖小余 6/19) | C++ feature lib 跑通 P0-01 单元测试 | W4 (6/25) |
| S2-XJ-3 | run_gate_check.py G1-G7 + bootstrap CI 重写 | 与小董 v1 §5.2 100% 对齐, 含单测 | W6 (7/9) |
| S2-XJ-4 | paper engine skeleton 并行写, 不等 backtest 报告 (R-21 闸 1) | paper skeleton PR 第一弹进 main | W6 (7/3) |
| S2-XJ-5 | PIT correctness CI v0.1 设计稿 (小余 §8 收口 #3) | docs/RESEARCH/xiaojiang-pit-ci-v0.1.md | W4 末 (6/26) |

**不在 Sprint-2 (留 Sprint-3+):**
- PBO 实现 (W8 = 7/30)
- 第一份 backtest 报告 (W7 = 7/16, Sprint-3 内)
- paper D1 (8/29, Sprint-4 内)

---

## 7. 双向收口 — 我反向要的

| # | 要 | 找谁 | Deadline |
|---|---|---|---|
| 1 | `libstcpp_features.a` + `libstcpp_risk.a` 静态库交付 (paper skeleton 闸 1 依赖) | 老周 + 老韩 | 6/26 |
| 2 | score_model ONNX (P0-02 用, backtest v2 7/30 报告依赖) | 小邓 + 小余 | 7/9 |
| 3 | 小袁 Wave 6 C++ microstructure model (VirtualMatcher 用; 晚了就 placeholder) | 小袁 + 老周 | 8/7 |
| 4 | M4.5 OQ-D11 (1 yellow 是否允许) + paper/live ULID 命名空间 (我倾向方案 A) | 老雷 | Sprint-2 W2 |
| 5 | M4.5 G6 触发频次不足时是否允许 21 天窗口 (5¢ 阈值后会触碰) | 小梁 + 小董 + 老雷 | 7/16 报告前 |
| 6 | "P0-01 月触发笔数 (5¢ 阈值)" catalog 重估 | 小程 | Sprint-2 W3 |

---

## 8. 我承认做错的

1. **`tools/m4_5_gate/run_gate_check.py` 未对齐小董 v1 §5.2 七 hard gate** — 小董 v1 5/22 出, 我没在 5/28 retro 前更新, ack. Sprint-2 W6 (7/9) 交付重写版.
2. **PIT CI 主笔小余 §8 #3 已经请我接, 我没主动认** — 应该 Sprint-1 主动接, 不该等 retro. 反思: Sprint-2 起每日 30min 扫别人开放问题 (借老韩 §7 第 4 条原则, 老米 doc-curator 帮 watcher).
3. **R-21 paper engine 联调缓解方案 (闸 1/2/3) 是 retro 当天才整理出来的** — v0.2 §10.1 PR-9 只写"必要时启用 placeholder", 没给时间表. 反思: 自标高风险必须当 sprint 内给具体缓解时间表.

---

## 9. 一句话给老雷

**Sprint-1 我交了 backtest v0.2 + paper v0.2 两份 cpp 化 RFC, GM Sign-off 已 ack. PIT CI 主笔我接 (Sprint-2 末 v0.1). M4.5 gate 脚本我承认现状只 6 条不对齐小董 v1 七 hard gate, deadline 7/9. P0-02 我站老钱 hard gate (含 fee OOS Sharpe ≥ 0.8 不过不上线). R-21 三道闸缓解我自己背. 5¢ 阈值我会签但 backtest v2 报告 (7/30) 必须给小梁/小董 触发频次 vs G6 50 笔窗口的 tradeoff 数字, 必要时拉到 21 天窗口. 第一份 P0-01 报告 7/16 我守.**

— 小蒋 (quant-backtest), 2026-05-28
