# Sprint-1 Retro — 小余 发言 (数据基础设施部 owner)

- **会议:** Sprint-1 Retrospective
- **日期:** 2026-05-28
- **发言人:** 小余 (data-etl, 数据基础设施部 owner)
- **角色:** 数据契约供给方主代表 + ETL pipeline 负责人
- **约束:** 听取义务 + 双向收口
- **关联:** 小邓 data-contract-v1 / 老李 + 小段 v2 endpoint matrix / 老李+小段 复用矩阵 / 老陈 network bench / 老周 lifecycle / 老王 WAL / 小袁 microstructure / 小邓 ML roadmap / 老雷 ADR goalserve-odds-gap + standing policy 月度 sweep

---

## 0. 立场 (一句话)

**Sprint-1 我没单独出大文档, 因为我的活儿是把别人的文档变成可跑的 pipeline. 小邓 C-01 ~ C-20 我以供给方主代表身份逐条认领, Polymarket 5 endpoint + Goalserve 11 个可用 sport 的 ETL 骨架 Sprint-2 M1 截止前给 v0.1, 但有 4 个硬约束必须当面收口 — 不收口就是 ML / 回测 / 实盘三层全炸的红线.**

---

## 1. C-01 ~ C-20 数据契约供给方表态 (听取义务)

| # | 项 | 表态 | 关键条件 |
|---|---|---|---|
| C-01 | PM 1s inplay book 历史 6 月 | **部分能** | 小冯 NOW 起录, 6 月自然有; 历史 backfill 待老李问官方 archive; 落库容量预算 200 GB zstd |
| C-02 | Pinnacle 30s + 6 月历史 | 非我域 | 老李 + 老彭路径定后我落库 |
| C-03 | Goalserve inplay 1s | **接受 p95 7s** | 跨洋 + 无 WSS 硬限; 我侧 4 时间戳 + 客户端 diff 抠 sub-second event_ts |
| C-04 | ESPN PBP | Sprint-2 接 | 小段 7/3 接入, 我同步建 schema |
| C-05 | trade_side | **能** | 直给或 mid 推断 + `side_inferred=true`, 两条路径都准备 |
| C-06 | taker/maker wallet | **能** | data-api /trades 已实证 |
| C-07 | F-18~F-22 in-process feature store | 非我域 | **owner 待澄清**, 见 §8 收口请求 1 |
| C-08 | feature store atomic snapshot < 5ms | 非我域 | 同 C-07 |
| C-09 | 4 时间戳契约 | **硬保** | 见 §4 |
| C-10 | PIT-correct feature replay | **部分能** | 我出 time-versioned parquet, replay 引擎是小田+小蒋 |
| C-11 | 历史 outcome label (UMA 6 月) | **能** | 老李给字段语义, 我链上拉 + 落库 |
| C-12 | shadow random-entry bucket | 非我域 | 小蒋 paper engine; R-11 隔离 cold storage 我保 |
| C-13 | 实时 prediction log (M5+) | M5 后做 | NOW 不做 |
| C-14 | replay 接口 | **能** | 复用小宋 ReplayDriver + parquet → MessagePack adapter |
| C-15 | PM↔Goalserve↔Pinnacle mapping | **部分能** | NOW 起 CSV, M+2 入 DWH; 小段+老李给我每周 mapping diff |
| C-16 | schema 变更 ADR + 24h | **硬保** | 见 §4 + §7.3 反思 |
| C-17 | ONNX < 100MB | 非我域 | 老姜+老周 |
| C-18 | book WSS full tape | **能** | 小冯 raw frame 落 cold storage 90 天, 我侧 zstd-19 + parquet partition by event_date/market_id_prefix |
| C-19 | order arrival/cancel 拆分 | TBD | 等小袁 v1.1 |
| C-20 | UMA 仲裁挑战期 label | **硬保** | label.is_confirmed=false 训练 drop |

**汇总**: 11 条直领, 6 条非我域, 3 条 TBD. C-07/C-08 owner 是 6/12 必须闭环的.

---

## 2. Goalserve odds 0 字节 — ETL 怎么兜 (双向收口)

老雷 ADR 已定线 A (老黄商务) + 线 B (我 + 小段并行) + 线 C (老李/老彭 Pinnacle). 我对线 B 给 ETL 兜底.

### 2.1 兜底架构 (Sprint-1 末就能跑)

```
Goalserve 三态严格分:
  200 + body > 500B   → 解析 + 落 parquet (正常)
  200 + body < 500B + XML <scores/>  → OUT_OF_SEASON/SILENT, 不重试, 写监控
  500 / timeout / 0B   → 重试 3 次 + 退避, 失败 → endpoint_failed 告警

odds 字段 (oddsid / @oddsid):
  → raw 落 cold storage (90 天) 但不展开
  → fair value anchor 100% fallback PM midprice (老李 /books)
  → schema 不给信号层暴露 odds 字段, 避免误用
```

### 2.2 ETL-1 ~ ETL-8 (ADR §3, 我全接)

| # | 内容 | 我侧实现 | 截止 |
|---|---|---|---|
| ETL-1 | 单元素 dict / 多元素 array 兼容 | `goalserve_array_normalize.cpp` 强制包 array | M1 (6/19) |
| ETL-2 | `@prefix` 字段脱壳 | endpoint-level parser router (soccernew/* 真 JSON / soccer/home @prefix jq-like) | M1 |
| ETL-3 | 拼写硬编码 | `endpoint_registry.yaml`: `bsktbl/nba-shedule` 单c, `mma/schedule` 双c 反惯例 | M1 |
| ETL-4 | 200+0byte 三态 | §2.1 | M1 |
| ETL-5 | string → safe int | `safe_atoi64` + bounds, NaN 落 `is_imputed=true` | M2 前 (7/3) |
| ETL-6 | 时区统一 UTC | Goalserve ET / HTTP Date GMT 统一转 UTC ns | M1 |
| ETL-7 | status 枚举 catch-all | activity.type 用老李实测 11 个官方 enum (含 CONVERSION / MAKER_REBATE / REFERRAL_REWARD), 不在 enum 落 `unknown_status` + 告警 | M2 前 |
| ETL-8 | 客户端 diff | §3 | M1 |

### 2.3 月度 sweep (老雷 standing policy)

小段跑探针, 我侧把每月 sweep 落 `data_quality_monthly.parquet` (month/endpoint/sport/status_class/size_bytes/change_from_last_month), `0B → non-0B` 自动 INCIDENT.

---

## 3. 客户端 diff (Goalserve 无 incremental) — 我怎么实现 (双向收口)

老陈+老李+小段 联合实证: Goalserve `?lastupdate=` 无效, `If-Modified-Since` 不返 304, 无 ETag / Cache-Control. **客户端 diff 是硬约束.**

### 3.1 算法

```
per endpoint, in-memory:
  prev_payload_hash: blake3(body_bytes) -> uint128
  prev_record_index: map<match_id, blake3(record_subtree)>

每次抓取:
  1. body hash 整体比对
     hash_now == hash_prev → 全 unchanged, 不展开不落库, metric +1 dedup_hit
     hash_now != hash_prev → 进 step 2
  2. 解析 match, 按 match_id 子树 hash
     sub_hash == prev: skip
     sub_hash != prev: emit match_changed (event_ts = HTTP Date), 落 parquet, 更新 index
  3. match disappearance (上次有这次没): emit match_disappeared, flush final
```

### 3.2 复杂度 + 收益

- body hash p99 < 200 us (blake3 SIMD, 1 MB 内); 子树 hash 50 match × 5 us = 250 us; 整条 diff p99 < 5 ms, 不上 critical path
- dedup 收益预估: `bsktbl/nba-shedule` ~ 95% (赛程罕变, 节省 99% IO); `cricket/livescore` ~ 30%; `baseball/usa` ~ 40%; `soccer/home` ~ 80%. **总 ETL IO 节省 50-70%**

### 3.3 风险兜底

- blake3 collision 概率 2^-64 实操不可能, 但**每 1h 强制全量 anchor record**, replay 时按 anchor 重建. 这条为 PIT correctness 兜底
- HTTP Date header 时钟漂移: `event_ts = http_date_ts` 不可信时 fallback `ingestion_ts`, `data_source_ts` 永远存 raw 原值不修改

---

## 4. PIT correctness — 红线我怎么保 (双向收口, 最重要)

小邓 R-1 / R-3 / R-13 + 老周 D-04 + GM Wave 6 锁死: **回测/paper/实盘三层 feature 同一份代码, 不可穿越未来.** 我作为 supply 侧主代表必须给可验证保证.

### 4.1 数据落地侧 (我直管)

```
每条记录强制 4 时间戳:
  event_ts <= data_source_ts <= ingestion_ts <= as_of_ts (严格不等式)
parquet partition by as_of_ts (date), 违反 = ETL bug → dead-letter + 告警
backfill 走 corrected_at_ts, **train/backtest join 强制按 as_of_ts, 不许按 corrected**
```

### 4.2 schema 一致性侧 (我硬保)

- 历史 schema ⊆ 实时 schema, 命名/类型/单位完全一致
- daily schema diff CI: 实时 vs 历史, drift 报警
- replay 测试: 近 24h 实时数据按历史 schema 重算 feature, bit-identical diff = 0

### 4.3 PIT 校验侧 (跟小田/小蒋协作)

- parquet time-versioned, (market_id, as_of_ts) 唯一寻址
- replay loader 强制 as_of_ts <= query_ts mask
- 训练 join: feature.as_of_ts <= label_window_start, CI 扫 future-leak row 必须 zero

### 4.4 反模式 (我主动拒绝)

- t+1 outcome 当 t feature → R-3, CI 必扫
- corrected_at_ts 替代 as_of_ts → R-13, train join 不走 corrected
- closing line 当 pregame feature → R-8, label_source / feature_source 强制区分
- forward-fill 不落 imputation_method → §2.5, 必落字段可追溯
- paper / live 数据混 → R-11, 三套 namespace 我不接
- schema 静默变更 → R-2, ADR + 24h 必走 (但 Sprint-1/2 期间见 §7.3 反思)

---

## 5. Polymarket bulk + cache — ETL 接得住吗

老李 v2: 决策路径 5 endpoint (E0 sports + E1 events?tag_slug + E2 WSS market + E3 books 兜底 + E4 WSS user + E5 私有), 5000 RPS → 0.026 RPS.

### 5.1 接得住

| 数据类 | 来源 | 落库策略 | 体量 (NBA 6 月) |
|---|---|---|---|
| L3 静态 | E0 | 24h 全量 1 次 | < 10 MB/月 |
| L2 元数据 | E1 (5min) | diff 落仅 changed | ~ 2 GB/月 |
| L0 流式 book WSS | E2 | 小冯 raw 90 天 cold + 我 1s snapshot 给 ML | ~ 200 GB/6 月 (zstd) |
| L1 兜底 books REST | E3 | 仅冷启动 | < 100 MB/月 |
| L0 user channel | E4 | 我方 trade/order raw 落 audit (R-11 隔离, 老王 WAL) | < 1 GB/月 |
| L2 positions/value | E5 | per-funder 15s diff | < 100 MB/月 |

**总 ~ 200-250 GB / 6 月 / NBA+MLB**, 远低于现有容量预算.

### 5.2 接不住 — 必须收口

| 痛点 | 请求 |
|---|---|
| L0 in-memory orderbook 状态机 owner | **老周/小冯** 6/12 前确认 |
| 90 天 cold storage WSS raw 200+ GB 预算 | **老吴/老钱** 跨洋同步策略 |
| /books 500 batch silent skip 229/500 | **老李+小冯** 从 sampling-simplified-markets 取 active 池 |
| WSS resync event 推下游 invalidate | **老周/小蒋** topic schema 待定 |

### 5.3 老李 v1 spec 3+1 处错误 (我落库 raw 必按修正字段)

- `asset_type` 不是 `param_type` (v1 错)
- `signature_type=1` 不是 2 (Magic Safe 1-of-1)
- `activity.type` 11 个官方 enum, 不是 CONVERT
- HMAC base64 保留 padding 不 rstrip(=)

这 4 处我 schema v1 标 known issue, 老李+老孙 wire 修完, 我 schema_version bump.

---

## 6. 跨洋链路 — ETL 怎么活

老陈实测: PM REST p50 1-2s p95 3-5s, Goalserve REST proxy p50 2s p95 7s, WSS 握手 1.3-1.9s.

```
抓取层: async + 15s timeout + jitter retry; 老周资源池管 connection 池; 落库带 source_endpoint + latency_ms 字段
解析层: endpoint-level router (小段 v2 强制), 大 payload (NHL 3.7MB / racing 1.4MB) streaming parse 不 eager
落库层: parquet + zstd-19; partition event_date/market_id_prefix (NBA 单天 1-2 GB compressed); ETL 不上 critical path, 不与老王 WAL 三条冲突
```

US 节点 (老吴 colo) vs CN 本地部署待 6/12 决议. 双部署模式都准备好.

---

## 7. Sprint-1 自评 + 反思

### 7.1 做到 (听取那部分)

- 小邓 C-01~C-20 逐条供给方表态 (§1)
- 老李+小段 endpoint matrix v2, ETL-1~ETL-8 全接 (§2)
- 不上 critical path, 不与 WAL/RM/signer 冲突
- 等小袁 v1.1 字段需求 (C-19) 出后补 schema

### 7.2 没做到 (反思)

- **没出 v1 ETL 设计文档** — 太多时间被动 review, 自己没产出独立 spec. 这是失误.
  → Sprint-2 W1 (6/19) 出 `xiaoyu-etl-pipeline-v0.1.md`, 含 §3 客户端 diff + §4 PIT schema + §5 三态 + §6 cold storage
- **WSS receiver / orderbook 状态机 owner 没收口** — C-07/C-08 拖到现在, 应 Sprint-1 W3 push 老胡定. 拖久了 ML/信号层全卡
  → 6/12 联签会上点名要老胡当场指派

### 7.3 真心话 (不耻下问)

3 件担心直说:

1. **WSS raw 90 天 cold storage 预算没人扛** — 200+ GB / 6 月跨洋传+存+加密+保留, 老钱预算没看到. 不要 M3 才发现没钱
2. **schema_version bump 真要 ADR + 24h + 48h 冷却吗?** §8.1 写得硬, 但 Sprint-1/2 还快速迭代字段每周可能改 3 次. 担心 ADR 节奏拖死开发. 提议 **Sprint-1/2 期间放宽到 schema diff CI + Slack 通知**, M2 锁定后再上 ADR. 6/12 老郭+老周 评估
3. **PIT correctness CI 主笔没指定** — R-1/R-3/R-13 是红线但实施细节 (replay 测试 / future-leak 扫描) 没主笔. 提议 **小蒋主笔 + 我提供数据 + 小邓 review**. 不定主笔 = 红线就是空话

---

## 8. 给老胡 + 老雷 的 3 个收口请求

| # | 请求 | Owner | 截止 |
|---|---|---|---|
| 1 | C-07 / C-08 in-process feature store owner 指派 | 老胡 + 老雷 | 6/12 联签会 |
| 2 | WSS cold storage 90 天预算 + 部署位置 (US/CN) | 老钱 + 老吴 | 6/19 |
| 3 | PIT correctness CI 主笔 + 时间表 | 小蒋 主笔 / 小邓 review / 我+小郑 实施 | Sprint-2 末 (6/26) |

---

## 9. 一句话给 GM 老雷

**Sprint-1 我以数据契约供给方主代表身份, 对 C-01~C-20 全部表态 (11 条直领 / 6 条非我域 / 3 条 TBD), Goalserve odds 0 字节按线 B 兜底 + ETL-1~ETL-8 全接, 客户端 diff blake3 双层算法已设计 (节省 ETL IO 50-70%), 4 时间戳 + schema 一致性 + PIT replay 三层硬保红线 (R-1/R-3/R-13). 没出独立 v1 spec 是 Sprint-1 失误, Sprint-2 W1 (6/19) 补 etl-pipeline-v0.1. 3 个收口请求待 6/12 联签会处理: in-process feature store owner / cold storage 预算 / PIT CI 主笔.**

— 小余, 2026-05-28
