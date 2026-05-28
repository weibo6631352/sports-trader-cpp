# ML 数据基础设施 v1 — Sprint-2 W1 立即启动

- Owner: 小邓 (ml-engineer)
- Date: 2026-05-28
- 验收: 老雷 (GM) + 老胡 (pm) + 小余 (etl) + 小蒋 (paper engine) + 小田 (feature store)
- 关联: `xiaodeng-ml-roadmap-v2.md` / `data-contract-v1.md` / `MEETINGS/sprint1-retro/xiaoyu-speech.md` / `xiaojiang-paper-trading-engine-v0.2-cpp.md` / `xiaojiang-backtest-framework-v0.2-cpp.md` / `laowu-toolstack-install-v1.md`
- Status: v1, 阶段 0 (6/15 → 7/24, 6 周) 施工图

> 小邓按: 这是 v2 路线图阶段 0 施工图. v2 给"做什么 + 截止 + 红线", 本文给"怎么做 + 工具栈 + 接口 + 落地路径".

---

## 0. 立场

阶段 0 = 建训练资产, **0 行 ML 进 binary**. Deliverables 集中在: parquet schema (PIT) + Feature store 双面 + 训练 pipeline + mlflow + shadow signal C++ 接口 spec.

---

## 1. Parquet Schema (PIT, 与小余 ETL 联签)

### 1.1 数据 lake 目录

```
data_lake/                            # 小余直管 (etl-pipeline v0.1, 6/19)
├── raw/{polymarket_book,polymarket_trade,goalserve_inplay,pinnacle_odds,espn_pbp}/
├── features/snapshot/                # 小田 → 小余
├── signals/{signal_event,paper_trade,shadow_signal}/   # 小蒋 paper engine; shadow R-11 隔离
├── labels/{outcome_binary,forward_return,closing_mid}/
└── ml/{training_dataset_snapshot,models,shadow_pnl}/   # 小邓直管
```

**partition**: `as_of_ts` 时间分 (防 backfill 跨日) + 业务 (`sport` 或 `market_id_prefix` 前 2 字节 hash). **压缩**: zstd-19. 6 月 NBA+MLB ~ 200 GB. **单文件** < 1 GB.

### 1.2 4 时间戳 (data-contract §2.3 同款)

必需: `event_ts`, `data_source_ts`, `ingestion_ts`, `as_of_ts` (PIT 主键). 可选: `corrected_at_ts` (训练 join **禁用**, R-13), `imputation_method`, `is_stale`, `is_imputed`, `schema_version` (R-6), `source_endpoint`. 严格不等式 `event_ts <= data_source_ts <= ingestion_ts <= as_of_ts`.

### 1.3 关键表 `features/snapshot/`

字段: `snapshot_id` (UUID), `as_of_ts` (ns), `market_id`, `F-01..F-22` + `ML-F-P01..M02` (28 features, 见 v1 §4), `feature_schema_version` (R-6), `imputation_methods` (json), `is_complete` (bool), `computed_in` (enum: backtest/paper/live, BR-1 校验).

**关键**: `computed_in` 三 mode bit-identical (浮点 < 1e-9). R-1 + BR-1 红线.

其它表 (`raw/polymarket_book/`, `signals/signal_event/`, `labels/outcome_binary/`) 字段集 = data-contract §3 / §4.1 / §4.2, 不重列.

### 1.4 PIT 校验

| # | 项 | 实施 |
|---|---|---|
| PIT-1 | feature 落库带 `as_of_ts` | 小余 ETL enforce |
| PIT-2 | 训练 join `feature.as_of_ts <= label.label_ts - 30s` | `ml/tools/check_pit_leakage.py` (D0-7) |
| PIT-3 | backfill 落 `corrected_at_ts`, train join 强制 `as_of_ts` | CI: future-leak row = 0 |
| PIT-4 | replay 测试: 近 24h 重算 bit-identical | 小蒋 replay + 小余 schema diff |
| PIT-5 | UMA 挑战期 2h 内 `is_confirmed=false` 训练 drop | 训练 pipeline assert |

---

## 2. Feature Store 双面设计

- **Online (in-process C++, 小田 #24 待指派)**: RCU atomic snapshot, 拉取 < 5ms, 策略 hot path 内. 每 5 min 异步 flush 到 offline.
- **Offline (parquet, 小余 + 小邓)**: time-versioned, replay (小蒋) + 训练 dataset (小邓).

### 2.1 Online 接口 (与小田)

`src/feature_store/feature_store.h`:

- `struct FeatureSnapshot { snapshot_id, as_of_ns, market_id, array<float,28> values, schema_version_hash, is_complete }`
- `class IFeatureStore::snapshot(market_id, as_of_ns) const -> FeatureSnapshot` — < 5ms hard cap, BR-1 同份实现
- `IFeatureStore::update(market_id, FeatureUpdate)` + `flush_to_parquet(path)`

实现走 RCU 或 seqlock 不阻塞读. 任何 feature 变更走 ADR (R-6).

### 2.2 Offline replay 接口 (与小蒋)

`class IReplayFeatureLoader::load_at(market_id, ts_ns) const -> FeatureSnapshot` — PIT 强制 (只用 `as_of_ts <= ts` 的数据). 实现: polars + Arrow C++ 读 parquet, time-versioned index (market_id, as_of_ts), binary search.

### 2.3 不耻下问

| # | 问题 | 对象 |
|---|---|---|
| MQ-F1 | in-process feature store owner 6/12 联签会必须定 (小余 §8 收口 1) | 老胡 + 老雷 |
| MQ-F2 | feature snapshot 落 parquet 触发: 每信号触发时 vs 每 1s. 折中: 触发时 + 1min anchor | 小余 + 小田 |

---

## 3. 训练 Pipeline (Python .venv, 与老吴对齐)

### 3.1 工具栈 (D0-2, 6/19, 老吴 S1-025 加装)

`lightgbm>=4.3, xgboost>=2.0, sklearn>=1.4, pandas>=2.2, polars>=0.20, pyarrow>=15, duckdb>=0.10, numpy>=1.26, matplotlib, seaborn, jupyter, jupyterlab, onnx>=1.16, onnxmltools, skl2onnx, shap, statsmodels (DSR/PBO 参考), mlflow>=2.10, nbstripout, black, ruff`

### 3.2 目录

`src/` (C++ 生产, M4 paper 后才有 `src/ml/`), `ml/{feature_pipeline,models,notebooks,evaluation,tools,mlflow_runs}/` (Python, Sprint-2 W1 起), `models/` (ONNX + registry.yaml, 阶段 1 才有), `data_lake/` (小余).

### 3.3 训练管道 SOP (阶段 1 一键跑通)

5 步: (1) `ml/feature_pipeline/build_dataset.py --signal-id SIG-P0-01 --tau-min 30s` 构造 PIT-safe dataset; (2) `ml/tools/check_pit_leakage.py` CI 校验; (3) `ml/models/train_lgbm.py --folds 5 --mlflow-experiment ...` walk-forward 训练; (4) `ml/evaluation/walkforward_eval.py` DSR + PBO; (5) `ml/tools/export_onnx.py` 导 ONNX 入 `models/SIG-P0-01@lgbm-{sha}-{ts}.onnx`.

---

## 4. mlflow vs 自研 (D0-5, 7/3 决策)

| 维度 | mlflow | 自研 |
|---|---|---|
| 工作量 | 装包即用, 1 天 | 2-3 周 |
| Registry / 实验追踪 / ONNX | 全支持 | 需自写 |
| 跨洋部署 | server + S3, 跨洋 rsync | 自维护 |
| 锁定 | OSS Apache 2.0 低 | 无 |

**默认 mlflow**, 理由: 省 2-3 周给阶段 1, 业界主流. 触发自研条件: mlflow 跨洋同步 > 5 min p99 OR registry 格式冲突.

**model registry 补强** (mlflow 之外的本地映射):

```yaml
# models/registry.yaml
- model_id: SIG-P0-01@lgbm-a1b2c3d-20260911T1200Z
  onnx_path: ...
  feature_spec_path: ...
  feature_schema_version: fs_schema@v1.3
  trained_ts: ...
  git_sha: a1b2c3d
  dataset_id: abc...
  mlflow_run_id: ...
  status: staging | shadow | retired
  onnx_size_bytes: 4500000     # ML-R7 < 100MB
  metrics: {brier_oos, hit_rate_oos, sharpe_oos, dsr, pbo}
```

---

## 5. Shadow Signal C++ 接口 (D0-4, 6/26)

### 5.1 三个核心接口 (`src/ml/`)

- **`struct MLSignalCandidate`**: 字段 = data-contract §4.1 signal_event 全字段 + `model_id`, `feature_snapshot_id`, `bucket="D-shadow-ml"`, `inference_latency_ns`
- **`class IMLSignalEngine::tick(FeatureSnapshot) -> optional<MLSignalCandidate>`** — paper / backtest only, live mode 阶段 3 才启用
- **`class IShadowSignalSink::emit(MLSignalCandidate)`** — 写 `shadow_audit.wal` (R-11 隔离)
- **`class IModelRegistry::current_model(signal_id) -> shared_ptr<OnnxModel>`** — SIGHUP hot reload + atomic ptr swap

### 5.2 paper engine v0.2 集成

| hook | 实现 |
|---|---|
| L3 STRATEGY (`strategy_engine.cc`) | rule signal 出后异步调 `ml_signal_engine.tick()` |
| L3 STRATEGY 出口 | `ShadowSignalSink::emit()` 写 shadow_audit.wal |
| L3 启动期 | `ModelRegistry` load `registry.yaml`, SIGHUP hot reload |
| L2 DATA | 共享 `FeatureAssembler::snapshot()` (BR-1) |

CMake: paper engine v0.2 §3.2 已留 `stcpp_ml_inference` 库链入 `stcpp_trader`. live mode 编译期 flag `-DENABLE_ML_INFERENCE=OFF` (阶段 3 才 ON).

### 5.3 CI 校验 (ML-R 红线)

- `ldd stcpp_trader | grep python` 必空 (ML-R4)
- `stat models/*.onnx` size < 100MB (ML-R7)
- `[ "$BUILD_MODE" = "live" ] && nm stcpp_trader | grep MLSignalEngine` 必失败 (ML-R1+R2)

### 5.4 不耻下问

| # | 问题 | 对象 |
|---|---|---|
| MQ-S1 | `shadow_audit.wal` 纳入 WAL framework v0.1 第 4 类? | 老王 + 小蒋 |
| MQ-S2 | M4 paper 启动时 ML candidate 必须 ready? 倾向否 | 小蒋 |
| MQ-S3 | ML signal 在 backtest mode 启用? 倾向启 (Bucket D-shadow-ml 单独账本) | 小蒋 + 小董 |
| MQ-S4 | `tick()` latency hard cap 50ms vs 100ms? 倾向 50ms 异步 | 老姜 |

---

## 6. 阶段 0 6 周时间线

```
Sprint-2 W1 (6/15-6/19):
  - data-contract v1 联签 (6/12 已发生)
  - D0-1 parquet schema (6/19, 与小余 etl-pipeline v0.1 同日)
  - D0-2 .venv 训练栈 (6/19, 老吴)

Sprint-2 W2 (6/20-6/26):
  - D0-3 feature store 双面设计 (6/26, 小田 + 小蒋)
  - D0-4 shadow signal C++ 接口 spec (6/26, 小蒋 + 老周)

Sprint-2 W3 (6/27-7/3):
  - D0-5 mlflow 选型决策 (7/3, 默认 mlflow)

Sprint-3 W1 (7/4-7/10):
  - D0-6 DuckDB 探索 notebook (7/10)

Sprint-3 W2 (7/11-7/17):
  - D0-7 check_pit_leakage.py CI (7/17, 小蒋 + 小董)

Sprint-3 W3 (7/18-7/24):
  - D0-8 阶段 0 验收报告 (7/24, 老胡协调)
```

---

## 7. 给 6/12 联签会的收口请求

| # | 请求 | Owner | 阻塞 |
|---|---|---|---|
| 1 | in-process feature store owner 指派 (是不是小田 #24) | 老胡 + 老雷 | D0-3 (6/26) |
| 2 | `shadow_audit.wal` 纳入 WAL framework v0.1 (第 4 类) | 老王 + 小蒋 | D0-4 (6/26) |
| 3 | mlflow artifact store 跨洋 rsync 部署位 (US colo / CN) | 老吴 + 老钱 | D0-5 (7/3) |
| 4 | `shadow_signal/` partition R-11 合规判定 | 小余 + 老周 | D0-1 (6/19) |

---

## 8. 修订记录

| 版本 | 日期 | 修订 | 主笔 |
|---|---|---|---|
| v1.0 | 2026-05-28 | 初版, 阶段 0 (6/15 ~ 7/24, 6 周) 施工图 | 小邓 |
| v1.1 | 7/24 (预计) | 阶段 0 验收灌入实测 | 小邓 |

---

— 小邓, 2026-05-28
