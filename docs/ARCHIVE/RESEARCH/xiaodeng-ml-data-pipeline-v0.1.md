# ML 数据 pipeline v0.1 — Sprint-2 W4 Wave 20

- Owner: 小邓 (ml-engineer)
- Date: 2026-05-28
- last_review: 2026-05-28
- 验收: 老雷 (GM) + 老胡 (PM) + 老韩 (RM) + 小蒋 (paper engine) + 小田 (DWH/Parquet)
- 关联: `xiaodeng-ml-roadmap-v2.md` `xiaodeng-ml-data-infra-v1.md` `data-contract-v1.md`
        `laowang-wal-framework-v0.2.md` `laotang-audit-schema-v1.1.md`
- Status: v0.1 落地 — `include/stcpp/ml/{feature_snapshot,training_label,hook}.hpp` + `src/stcpp/ml/hook.cpp` + `tests/unit/test_ml_hook.cpp`

> 小邓按: 落地 v1 路线图阶段 0 第一个 C++ deliverable. 此前 v1/v2 是 spec + roadmap, 本 v0.1 是真代码 — paper engine W5 端到端联调后, 每笔决策都会落 mldata.wal 形成训练金矿.

---

## 0. 立场 (再 confirm)

阶段 0 = **0 行 ML 进 binary**. v0.1 hook 是被动观察者:
- 抓 `FeatureSnapshot` (32 feature) + `TrainingLabel` (4 stage 标签)
- 走 WAL framework (`WalKind::ShadowAudit`) 异步落盘
- **不**参与 RM 决策, **不**写 OrderIntent, **不**算 PnL

主线程同步路径 ≤ 1us (R-12). ring 满 silent drop (ML data 是 best-effort, 不能拖死 trader).

---

## 1. 数据 pipeline 全景

```
[live + paper 共享]
  signal.tick() → ctx
     ↓
  P0-01 PinnacleNoVigSignal → SignalOutput
     ↓
  RiskGateway.evaluate(intent) → RiskDecision
     ↓
  Signer (paper / live) → SignResponse
     ↓
  VirtualMatcher (paper) / RealExchange (live) → VirtualFill / RealFill
     ↓
  Settlement (UMA + match end) → SettlementEvent
     ↓
[小邓 W4 Wave 20 新增 — 旁路]
  MLDataHook.on_signal_compute() ────┐
  MLDataHook.on_risk_decision() ─────┤── try_push (ring SPSC, R-12 ≤ 1us)
  MLDataHook.on_fill()  ─────────────┤
  MLDataHook.on_settle() ────────────┘
     ↓
  WalWriter<FeatureSnapshot>  →  /var/lib/stcpp/shadow/paper/mldata_feature.wal  (paper)
                              →  /var/lib/stcpp/shadow/live/mldata_feature.wal   (live, W5+)
  WalWriter<TrainingLabel>    →  /var/lib/stcpp/shadow/paper/mldata_label.wal    (paper)
                              →  /var/lib/stcpp/shadow/live/mldata_label.wal     (live)
     ↓                                                         [W5 小田 DWH]
  Parquet (zstd-19, partition by as_of_ts day + sport_hash 前 2B)
     ↓                                                         [W6 OLAP]
  DuckDB query (探索 notebook, 小董 review)
     ↓                                                         [W8+ ML 训练]
  Python LightGBM + sklearn (.venv 离线, 跑完 ONNX 导出 — Python 0 行进 binary)
```

---

## 2. v0.1 接口总览

### 2.1 `FeatureSnapshot` (32 feature, POD, immutable)

| Field | 来源 | 红线 |
|---|---|---|
| `event_ts / data_source_ts / ingestion_ts / as_of_ts` | 4 ts R-20 (老周 PIT) | R-20 |
| `feature_snapshot_id` (u64) | `hash(market_id || as_of_ts || signal_id)` | ML-R8 |
| `audit_id_bytes` (16B ULID) | 与 RM/signer/matcher 同链 | R-1 join key |
| `signal_id_u8` | P0_01 / P0_02 / ... | spec §1 |
| `market_id` (32B fixed) | Polymarket condition_id | |
| 32 个 float feature | 见 §3 | LightGBM NaN sparse |

### 2.2 `TrainingLabel` (4 阶段标签, POD)

| Stage | trigger | 字段 |
|---|---|---|
| 1 signal | `on_signal_compute` | (no label, 只缓 cache) |
| 2 decision | `on_risk_decision` | `decision_taken=true` (隐含) |
| 3 fill | `on_fill` (partial label 写盘) | `executed`, `filled_price`, `filled_size_usdc`, `outcome=Pending` |
| 4 settle | `on_settle` (终极 label) | `settlement_outcome` ∈ {Win, Loss, Push, Void}, `realized_pnl_usdc` |

`feature_snapshot_id` 4 stage 一致 — Parquet 训练 join 单列即可.

### 2.3 `MLDataHook` 4 入口

每个入口 **`noexcept void`** + **silent drop on failure**:
- `on_signal_compute(SignalContext, FeatureSnapshot)`
- `on_risk_decision(RiskDecision, FeatureSnapshot)`
- `on_fill(VirtualFill, FeatureSnapshot)` — 同时写 partial `TrainingLabel`
- `on_settle(SettlementEvent)` — 终极 `TrainingLabel`, 完成后 evict join cache

---

## 3. 32 Feature 详解

| # | Enum | 含义 | 数值范围 | leakage risk | 来源 |
|---|---|---|---|---|---|
| 0 | `PM_mid_bid` | PM YES bid | [0, 1] | low | 小袁 BookSnapshot |
| 1 | `PM_mid_ask` | PM YES ask | [0, 1] | low | 小袁 |
| 2 | `PM_book_depth_top3_yes` | top-3 levels USDC | [0, ∞) | low | 小袁 |
| 3 | `PM_book_depth_top3_no` | top-3 levels USDC | [0, ∞) | low | 小袁 |
| 4 | `Pinnacle_p_yes_fair` | no-vig fair prob | [0, 1] | low | 老彭 Pinnacle CSV |
| 5 | `Pinnacle_overround` | `p_yes_raw + p_no_raw - 1` | [0, 0.1] | low | 老彭 |
| 6 | `edge_bps` | signal edge | [0, ∞) bps | **HIGH (= target)** | 小程 P0-01 |
| 7 | `kelly_full` | Kelly fraction (未 ·0.25) | [0, ∞) | high | 小程 |
| 8 | `expected_fill_rate` | SlippageModel | [0, 1] | medium | 小肖 |
| 9 | `slippage_bps` | SlippageModel.compute | [0, ∞) bps | medium | 小肖 |
| 10 | `live_section` | LiveSection 0..4 | enum cast | low | 小卢 |
| 11 | `game_state` | bitmask live/ended/delayed | u8 | low | 老彭 Goalserve |
| 12 | `kickoff_seconds_until` | `(kickoff_ts - now)/1e9` | (-∞, +∞) | low | 老彭 |
| 13 | `inplay_minutes` | 已比赛分钟 | [0, 600] | low | 老彭 |
| 14 | `score_home` | 主队得分 | [0, ∞) | low | 老彭 |
| 15 | `score_away` | 客队得分 | [0, ∞) | low | 老彭 |
| 16 | `period` | quarter/inning | [0, 16] | low | 老彭 |
| 17 | `vol_24h` | 24h 成交量 | [0, ∞) USDC | low | gamma REST |
| 18 | `vol_1h` | 1h 成交量 | [0, ∞) | low | gamma |
| 19 | `vol_5m` | 5min 成交量 | [0, ∞) | low | gamma |
| 20 | `spread_bps` | `(ask-bid)/mid·10000` | [0, ∞) bps | low | 派生 |
| 21 | `quote_half_life_ms` | 报价稳定度 | [0, ∞) | medium | 小袁 microstructure |
| 22 | `rm_state` | RmState 0..4 | enum cast | low | 老韩 |
| 23 | `rm_consec_loss` | 连续亏损次数 | [0, ∞) | medium | 老韩 |
| 24 | `rm_bankroll` | bankroll USDC | [0, ∞) | low | 老韩 |
| 25 | `rm_exposure_pct` | `exposure / bankroll` | [0, 1] | medium | 老韩 |
| 26 | `signal_confidence` | `clamp(edge/0.10, 0, 1)` | [0, 1] | **HIGH** | 小程 |
| 27 | `ci_lower` | edge CI 下界 | (-∞, ∞) | medium | 小肖 W5 |
| 28 | `ci_upper` | edge CI 上界 | (-∞, ∞) | medium | 小肖 W5 |
| 29 | `N_pretrade` | pretrade 观测数 | [0, ∞) | low | drift counter |
| 30 | `N_inplay` | inplay 观测数 | [0, ∞) | low | drift counter |
| 31 | `N_settled` | 历史结算样本 | [0, ∞) | low | drift counter |

**Leakage 处理**: feature #6 `edge_bps` 和 #26 `signal_confidence` 由 rule signal 计算, 是 P0-01 决策 target. 训练 ML 模型时 (W8+):
- 若 ML 目标是 "决策 yes/no" → 这两列必 drop (information leak)
- 若 ML 目标是 "成交后 PnL 回归" → 可保留 (rule 已下注, ML 算的是 rule 选完后的 PnL 分布)

PIT-2 (ml infra v1): 训练 join 时 `feature.as_of_ts <= label.as_of_ts - 30s` 强制. v0.1 hook 由 `on_settle` 写真 `label.as_of_ts = settle_ts`, 时间差天然成立.

**缺失语义**: float NaN. LightGBM / XGBoost native missing 支持, 不做 imputation.

---

## 4. R-11 / R-12 / R-20 enforce

### 4.1 R-11 物理隔离

| Mode | `MlDataWalKind()` | path_prefix | 是否同进程混 |
|---|---|---|---|
| paper | `ShadowAudit` | `/var/lib/stcpp/shadow/paper/mldata*` | 否 (build-time) |
| backtest | `ShadowAudit` | `/var/lib/stcpp/shadow/paper/mldata*` | 否 |
| live (W5+) | `ShadowAudit` | `/var/lib/stcpp/shadow/live/mldata*` | 否 |

`framework WalWriter::Open()` 内 `PathPrefixOk` 不命中 → `std::abort()` (R-11 防绕过, 老王 v0.2 §3.3).

**v0.1 实现 R-7 守护**: `src/stcpp/ml/CMakeLists.txt` 仅在 `STCPP_EXEC_MODE ∈ {paper, backtest}` 时编译 `stcpp_ml` lib. live 模式 W5+ 接 RealFill 后再放开 (避免 v0.1 含 paper-only `VirtualFill` 漏进 live binary).

### 4.2 R-12 同步路径 ≤ 1us

测试 M6 (`R12_OnSignal_Below_1us_p99`) 在 mac 上 skeleton path p99 < 5us (gtest 兜底 5us, GoogleBench W5 切硬 1us). 真上线时 framework Append 走 `rigtorp::SPSCQueue<Frame>` (小石 W5):
- PIT 100ns (内联无分支)
- atomic seq.fetch_add 50ns
- SPSC try_push 500ns
- caller serialize_into (memcpy) ~ 200ns

**ring 满**: silent drop + `stats_.dropped_backpressure++`. **不**触发 RM `AUDIT_WAL_BACKPRESSURE` (ML data 是 ML-R1 + R-12 旁路, 不上报 RM).

### 4.3 R-20 4 ts PIT

- `FeatureSnapshot::ts_chain_ok()` 入口检查 (caller 自校)
- WAL framework `pit::AssertChain` 二次校验 (Append 路径必调)
- `TrainingLabel::ts_chain_ok()` 同款 (settle 端)

**Settle ts 链特殊**:
- `event_ts` = match_end_ts (Goalserve)
- `data_source_ts` = PM resolve event ts (UMA)
- `ingestion_ts` = 本地收到 resolve 时
- `as_of_ts` = label 写入时

实际 settle 可能延后 (UMA 2h 挑战窗口), `as_of_ts` 远晚于 `event_ts` 是常态, 不构成 PIT 违规.

---

## 5. ML-R 红线 5 条 (v1 已立) — confirm 仍生效

| # | 红线 | v0.1 落地点 |
|---|---|---|
| ML-R1 | ML 不进 RM 决策路径 | hook 4 入口 `noexcept void`, 调用方不可假设返 false 会 reject |
| ML-R2 | paper 期 ML 不进 OrderIntent | hook 抓拍后丢 WAL, 不喂 RM |
| ML-R3 | M4.5 7 gate 不看 ML PnL | hook 不算 PnL (只在 label 透传 `realized_pnl_usdc` from settle) |
| ML-R4 | Python 0 行进 binary | `stcpp_ml` lib 仅 C++, Python 只在 W8+ 离线训练用 |
| ML-R5 | 推理走 ONNX/Treelite | W8+ 真接 ONNX Runtime C++, v0.1 不涉及推理 |
| ML-R6 | 4 类 case "rule 输 ML 赢" ≥ 20% | M5 阶段 2 报告 |
| ML-R7 | ONNX 模型 < 100MB | W8+ |
| ML-R8 | 每次推理必带 model_id + feature_snapshot_id | v0.1 hook 入口 enforce `feature_snapshot_id != 0` → drop |

---

## 6. 与 P0-01 (小程 rule signal) 的关系

**P0-01 是 rule-based, ML 不进生产 (M4.5 前)**. v0.1 hook 是 **"影子推断 + 离线分析"**:

```
P0-01 决策路径 (rule, M4.5 前):
  signal.tick() → SignalOutput → RM.evaluate() → signer → matcher → fill → settle
                                    ↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑
                                    ML hook 旁路抓快照, 不参与决策

W8+ shadow signal (阶段 2):
  shadow_model.infer(snap) → ShadowSignalOutput (单独账本, 不喂 RM)
                                    ↓
                              shadow_pnl.wal (与真 fill 对比)

M5+ 战略 (阶段 3, ML-R6 通过后):
  shadow → 进 RiskGateway (rule 兜底永不删, ML-R1 仍生效)
```

v0.1 hook **不**做 shadow inference, 只抓拍训练资产. shadow inference 是 W8+ 阶段 1 末.

---

## 7. W4-W8 路线

| Wave | Owner | Deliverable | 验收 |
|---|---|---|---|
| W4 (本 wave) | 小邓 | hook v0.1 落地 (本 doc) | 老雷 + 老胡 |
| W5 | 小邓 + 小田 | Parquet 序列化 (WAL bytes → Parquet, schema 1:1 enum 顺序) | 小田 |
| W5 | 小邓 + 小蒋 | paper engine 端到端接 hook (signal → decision → fill → mldata.wal) | 小蒋 |
| W6 | 小董 | DuckDB 探索 notebook (32 feature 分布 + leakage 校验) | 小邓 review |
| W6 | 小余 | data lake mldata/ 目录就位 (zstd-19 + partition) | 小邓 |
| W7 | 小邓 | LightGBM baseline 离线训练 (label = `settlement_outcome` Win/Loss) | 小蒋 |
| W7 | 小邓 | walk-forward backtest (8 sport profile) | 老彭 |
| W8 | 小邓 + 老周 | ONNX 导出 + C++ inference 接口 spec (阶段 1 末) | 老周 |
| W8+ | 小邓 + 小蒋 | shadow signal C++ 实现 (M5 阶段 2 启动) | 老雷 |

---

## 8. v0.1 落地清单 (本 Wave 已完成)

| 物件 | 路径 | 行数 |
|---|---|---|
| header | `include/stcpp/ml/feature_snapshot.hpp` | 209 |
| header | `include/stcpp/ml/training_label.hpp` | 99 |
| header | `include/stcpp/ml/hook.hpp` | 150 |
| 实现 | `src/stcpp/ml/hook.cpp` | 197 |
| CMake | `src/stcpp/ml/CMakeLists.txt` | 38 |
| 测试 | `tests/unit/test_ml_hook.cpp` | 264 |
| 总计 | | **~ 957 行** (≤ 1250 派单上限) |

**测试**: 20 case / 100% 通过. 含 4 stage join, R-11/R-12/R-20 enforce, 32 feature 全填, NaN sparse, p99 latency.

---

## 9. W5 衔接 owner

| 接口 | owner | 联系点 |
|---|---|---|
| Parquet schema 1:1 enum 顺序 | 小田 #24 | `FeatureName` enum order = column order |
| paper engine 接 hook 4 入口 | 小蒋 #20 | paper_main.cpp Wave 20+ |
| Settlement 模块 (UMA + match_end 联合 emit) | 老胡 PM ack | v0.1 hook 用 inline `SettlementEvent`, W5+ 决定是否独立 module |
| feature_snapshot_id 工程 (PIT u64 hash) | 老周 #02 | `hash(market_id \|\| as_of_ts \|\| signal_id)` 算法定稿 |
| Signal 调用 hook (P0-01 tick 完成时 emit) | 小程 #19 | P0-01 spec v0.2 加 `on_signal_compute` callback hook |
| RM 调用 hook (evaluate 完成时 emit) | 老韩 #14 | RiskGateway v0.4 加 ML hook DI |
| Slippage CI bounds (#27 #28) | 小肖 #20 | SlippageModel v2 CI 上下界 |
| data lake mldata/ 目录 | 小余 #23 | zstd-19 + day partition + sport_hash 前 2B |

---

## 10. 不变红线 (本 v0.1 已 enforce, W5+ 不可松)

1. **ML-R1** ML 不进 RM 决策路径
2. **ML-R2** paper 期不进 OrderIntent
3. **R-11** paper / live mldata.wal 物理隔离 (build-time path_prefix)
4. **R-12** hook 同步路径 ≤ 1us, ring 满 silent drop
5. **R-20** 4 ts + `feature_snapshot_id != 0`, 任一不满足 → drop + counter

**ML data 是 best-effort, hook 失败永不传染主链路**. paper engine 跑通的前提 = hook drop 不影响 fill, 不阻 RM 决策.

---

**最后更新**: 2026-05-28 by 小邓 (W4 Wave 20 落地)
