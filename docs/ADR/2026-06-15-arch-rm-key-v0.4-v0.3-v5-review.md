# ADR-003: Sprint-2 W1 末综合评审 (老周 v0.4 + 老韩 v0.3 + 老孙 v5 + 老沈 v2)

- Owner: 老郭 (cpp-architecture-second-opinion)
- Date: 2026-05-28 (Sprint-2 W1 末名义日 6/19, 老郭 ADR-001 承诺履约 5/28 提前出)
- Status: **Conditional Accepted** (4 整改 + 3 GM 待会签)
- Reviewer: 老郭 (主审), 老雷 (GM 终签会签)
- Related: 老周 v0.4 / 老韩 v0.3 / 老孙 v5 / 老沈 v2 (STRIDE + vendor) / R-20 / R-11 / R-12 / 上链 deferred / 老王 WAL v0.1 / 老唐 audit v1 / Sprint-1 Retro 18 决议
- 三态: ✅ Agreed / ⚖️ Compromised / ⬆️ Escalated (不允许"先这样吧")

---

## 1. 评审范围

老周 v0.4 (676 行 8 diff + §18 mock + §19 ML shadow + §20 跨域 review), 老韩 v0.3 (379 行 STALE 5 档 + 21 reject + STRATEGY_DECAYED + G8 5%), 老孙 v5 (249 行 撤地域 + 单 vendor 双 region + 内部 3-of-5 Shamir + B5 不动), 老沈 v2 (STRIDE 11 P0 + 40 缓解 + AWS 双 region + YubiHSM), 横向 R-20 / R-11 / R-12 / 老王三 WAL / 老唐 BLAKE3.

---

## 2. 老周 v0.4 评审

### 2.1 8 条 diff 逐条

| diff | 内容 | 态度 | 理由 |
|---|---|---|---|
| D1 §15.6 vCPU 映射 (4-5 conn + nice 三档) | ✅ Agreed | D-05/D-07 + 老郭 D-05 fsync > 周期 一致 |
| D2 §17.1.1 T0a/T0b/T0c/T1 + T8-T11 | ✅ Agreed | T0c user 故障域硬隔离, Paper T1 不启 节省资源 |
| D3 §17.6 STALE 5 档 + Polygon 6s watchdog | ✅ Agreed | 小袁 n=23 实测 + 老叶 §1.5 落地 |
| D4 §17.1.2 ring 拆 4 路 | ✅ Agreed | D-05 故障域 + 老李 4-5 conn 精细 |
| D5 §17.6.3 stale-from-Goalserve 退档 | ✅ Agreed | 不污染 R-11 |
| D6 §18 Paper mock (PaperSigner/VirtualNonce/Gas/Confirm) | ⚖️ Compromised | spec OK, §18.6 测试场景不够 (仅 tcpdump+nm), 缺 fill 时序仿真验证, 见 C-1 |
| D7 §19 ML shadow + ML-R1~R8 落地 | ✅ Agreed | RiskGateway::evaluate() 编译期 type check 不接 MLSignal — 强 |
| D8 §20 跨域 review + §20.5 PR-merge 门 | ✅ Agreed | "不签字 ≠ 通过" 把听取义务转 CI 门 |

**7 Agreed / 1 Compromised / 0 Rejected.**

### 2.2 §18 mock 接口 + §19 ML shadow + §20 清单是否够

- §18 四接口同 schema, mode=Paper + tx_hash `0xPAPER_` 前缀, nm 静态扫 `chain_rpc|eip712_real` reject — **够** (除 C-1 fill 时序测试)
- §19 三接口 + paper only + Live mode `-DENABLE_ML_INFERENCE=OFF` + shadow_audit.wal R-11 隔离 + BR-1 RCU 共享 — **够**
- §20 6 议题带 owner + 听取人 + 截止日, §20.5 五条 PR-merge 门 (@-mention + approve/no-comment + 老郭签 + 老高 R-12/R-11 扫 + 老练 nm) — **够**, §20.4 自评无 Escalated 我复核同意

### 2.3 R-20 时间戳是否需要 v0.5 §21

**需要**. v0.4 通篇未提 4 ts 契约, §18.1.1 SignRequest 没明示 4 ts / §19.1 MLSignalCandidate 未说 feature_snapshot_id 在 envelope 何处 / §17.1.2 ring frame 没说 ingestion_ts 何时打. ⚖️ Compromised, 见 C-2.

---

## 3. 老韩 v0.3 评审

### 3.1 STALE 5 档

| 档 | 阈值 | 态度 |
|---|---|---|
| INPLAY_HOT_CRIT 200/800ms | ✅ 小袁 0.12-0.15s n=23 实测 4-5x |
| INPLAY_HOT 500/2000ms | ✅ 0.21-0.45s 实测 4-5x |
| INPLAY_COLD 2000/10000ms | ✅ 与老周 §17.6.1 PREGAME_NEAR 一致 |
| PREGAME 5000/15000ms | ⚖️ **与老周 §17.6.1 PREGAME_FAR 30000ms 不一致, 见 C-3** |
| SETTLED 10000/30000ms | ✅ 30s 硬上限 = D-06 红线 |

MarketStateClassifier constexpr 决策树 + fail-safe 退 INPLAY_COLD (而非 PREGAME) — 中间档保守, ✅. 进 PR-9/PR-10.

### 3.2 21 reject enum

14 (v0.2) + 4 (小肖 LOW_FILL_RATE/EXCESSIVE_SLIPPAGE/EDGE_NEGATED/EXCEED_BOOK_DEPTH) + 1 AET_SIGN_FAILED + 1 INVALID_INTENT 强化 + 1 STRATEGY_DECAYED = 21. ✅ Agreed. 封闭性不变 (不允许 OTHER, 新增 bump 版本).

**澄清**: §14.5 末行 "SETTLED → INVALID_INTENT + sub-reason" — sub_reason 是 INVALID_INTENT 字段还是新增 enum? 推荐字段 (保 21 总数), 见 C-4.

### 3.3 STRATEGY_DECAYED kill switch (§16)

- Bayesian BLACK = P(μ<0|data) > 0.3 持续 2 周 → 一律 REJECT (开 + 平都 reject, 平走人工撤单) ✅
- fail-safe `UNAVAILABLE` 中间态 (cron 故障不全盘 REJECT, DoS 防护) — **优于"BLACK"或"GREEN"两端** ✅
- 解锁三签 (老钱 + 老雷 + 小梁) + 7 天 MONITORING (KELLY 0.5x) ✅
- paper → live PnL 切换点 + D-12 ULID 方案 A 一致 ✅

### 3.4 G8 5% 月度观察

5% (非 v0.2 misalignment#5 的 2%) + 不进 M4.5 hard gate + 连续 2 月 > 5% → RM v0.3+ ADR. 小董 §2 "30 样本下 2% 离散无统计意义" 拒绝 hard gate 是对的 ✅. **永不动 G1-G7 / D-06 30s / SAFE_MODE 默认** — 底线明示, 同意.

### 3.5 Batch 2 立场覆盖度

小袁 (5 档 + T_half) / 小肖 (4 reject + book_snapshot_ts_ns) / 小董 (G8 5% + Bayesian + 三签解锁) — **3 人立场全数落地** ✅.

---

## 4. 老孙 v5 评审

### 4.1 simplified 合理性

- AWS us-east-1 主 + us-west-2 备 (单 vendor 同 IAM/CMK/SigV4 复用) + YubiHSM 2-of-2 离线兜底 ✅
- 内部 3-of-5 Shamir (老雷/老黄/老沈/老周/老吴 物理隔离) — 撤海关/跨境/国别监管 ✅
- ~3 周工程节省 (跨 vendor client 2 周 + 跨境 SOP 0.5 周 + Sygnum 预热 0.5 周) — 与 GM ADR §6 估算一致

### 4.2 8 Blocker C++ 等效全保留 ✅

B1 IPC 鉴权 / B2 SPKI pin (减 2 entry) / B3 WebAuthn / B4 minisign / **B5 typed-data 二次校验 (Polymarket EIP-712, 老孙 v5 §4 "最致命引用最严格", 对应 T-04 DREAD 19)** / B6 SecureBuffer (PaperSigner 通过 PR-9 共享) / B7 active-standby HA / B8 Conan + SBOM + OSV — **全 8 条等效**, 简化只动 vendor/Shamir 配置层不动技术核心 — 正确简化.

### 4.3 R-20 signer audit 4 ts

v5 §5.2 列了 4 audit event (received/verified/emitted/error), 未明示 4 ts schema (event_ts/data_source_ts/ingestion_ts/as_of_ts) 在 SignRequest envelope 何处. R-20 §9 已派单"老孙 IPC schema 加 4 ts Sprint-2 W2" — v5 需补 §5.4. ⚖️ Compromised, 见 C-2.

### 4.4 与 nonce_mgr/audit/WAL 接口 ✅

老叶 nonce_mgr v1 UDS / 老唐 BLAKE3 4 event + 私钥 redact / 老王 signer local WAL 物理隔离 + 1s/64 batch — 一致.

---

## 5. 老沈 v2 评审

### 5.1 STRIDE 11 P0 + 40 缓解 ✅

11 P0 (I-01 / E-01 / T-01 / T-04 / I-02 / S-05 / D-02 / D-04 / T-02 / T-03 / E-02) 全 DREAD ≥ 18, 撤跨境后无遗漏. 40 缓解 (K1-K8 / A1-A6 / N1-N6 / S1-S7 / R1-R4 / D1-D5 / P1-P5) 与 ADR-001 + 老孙 8 Blocker 一一对应.

### 5.2 关键 P0 缓解对照

| P0 | 缓解 | 老孙 v5 落位 |
|---|---|---|
| I-01 私钥落日志 | M-K1/K2/K7/K8 | B6 SecureBuffer + zeroize + ulimit -c 0 |
| T-04 calldata 篡改 | M-K3/K4 + 二次校验 | **B5 typed-data EIP-712** |
| S-05 仿冒 IPC | M-K3 PEERCRED + HMAC + nonce | B1 IPC 鉴权 |
| E-02 trader→signer 横向 | M-P3 seccomp + signer no egress | B1 + B6 |
| T-01 供应链投毒 | M-S1-S7 | B8 Conan + SBOM + OSV |

5 关键 P0 全部有可验证缓解 spec ✅.

### 5.3 vendor v2 (单 vendor 双 region) + YubiHSM 与老孙 v5 完全一致 ✅. 演练 4 → 2 项 (region 切换 + YubiHSM 离线) — 够.

### 5.4 残留 O-04 TEE / O-06 LLM 沙箱 / O-08 硬件 kill-switch 等延后 Sprint-3 ✅.

---

## 6. R-20 落地建议 (需架构层 enforcement)

R-20 §3 覆盖 11 场景横跨四份核心设计, 没架构层闭环 = 各 owner 自加 = 必遗漏. **需要**.

| Owner | 动作 | 截止 |
|---|---|---|
| 老周 v0.5 | 新增 §21 数据时间戳 enforcement (4 ts 全链路 T0/T2/T3/T8/T9 + PIT assert + DataSourceTimestamp::Source enum) | 6/26 |
| 老韩 v0.3.1 | §5 audit schema 加 4 ts + R-20 §7 4 assert | 6/26 |
| 老孙 v5.1 §5.4 | IPC SignRequest schema 加 4 ts + sign_request_ts/sign_complete_ts | 6/26 |
| 老唐 audit v1.1 | BLAKE3 envelope 加 event_ts + data_source_ts + source enum (现有 evaluated_at_ns/ingested_at_ns 部分覆盖) + 加 AET_STRATEGY_DECAYED + AET_STRATEGY_UNLOCK 2 个新 event | 6/19 |
| 老王 WAL | 不动 (P6 向前兼容, payload 内由各 owner 自管) | — |
| 老高 + 老练 CI | grep now()/localtime()/mktime() + INFERRED_FROM_* 连续 N 条告警 | 6/19 |

R-20 + R-11: paper 4 ts → `paper_audit.wal` 绝不流到 `risk_audit.wal` ✅. R-20 + R-12: ingestion_ts 必 `CLOCK_MONOTONIC_RAW` + < 50us, 老周 §17.1.1 T0 reactor 已覆盖, v0.5 §21 需显式落点.

---

## 7. WAL framework + audit schema 三 WAL 隔离评审

### 7.1 老王三 WAL R1 ✅

risk_audit / position / paper_audit 物理分离 (不同路径/fd/fsync 线程/group 权限) + position PerRecord (0 RPO) / audit + paper GroupCommit (≤ 1ms RPO) + R-11 fail-closed (write fail / fsync fail / WAL 损坏 / seq gap → SAFE_MODE) — **够**.

### 7.2 老唐 BLAKE3 hash chain ✅

prev_hash + payload_hash + current_hash 三 32B + sequence u64 + Merkle hourly anchor (上链 deferred 后链上 anchor 可缓). 12 event 封闭枚举需 v1.2 加 AET_STRATEGY_DECAYED + AET_STRATEGY_UNLOCK (老韩 §16.6).

### 7.3 ML shadow 是否需单独 WAL 检查? **不需要**, shadow 走 audit GroupCommit + ML-R2 双层兜底已闭合.

---

## 8. 跨文档一致性

| 议题 | 老周 v0.4 | 老韩 v0.3 | 老孙 v5 | 老沈 v2 | 一致? |
|---|---|---|---|---|---|
| PREGAME HALT | §17.6.1 30000ms | §14.1 15000ms | — | — | ❌ C-3 |
| Paper/Live/Shadow 三态 | §18 + §19 | §16.5 R-11 重申 | Live only signer | — | ✅ |
| R-11 paper 隔离 | §18.3 五 WAL | §16.5 | §5.3 signer local WAL | — | ✅ |
| R-12 event loop 非阻塞 | §15.6.4 + §17.6.2 | — | §1.4 KMS 启动期非 hot | — | ✅ |
| 三 WAL R1 | §18.3 | §12 sync only append | §5.3 | — | ✅ |
| 4 ts R-20 | ❌ 缺 v0.5 §21 | ⚖️ 部分 (evaluated_at_ns) | ⚖️ §5.2 缺 schema | — | ⚖️ C-2 |
| 8 Blocker | §18 PR-9 SecureBuffer | — | §4 全保留 | §3 P0 对应 | ✅ |

**6 一致 / 1 不一致 (C-3) / 1 部分 (C-2).**

---

## 9. GM 待会签项 (上 老雷)

| # | 议题 |
|---|---|
| GM-1 | STRATEGY_DECAYED 三签解锁 (老钱+老雷+小梁) — 老雷接受强制审批人 + paper/live 通用 |
| GM-2 | STALE_GLOBAL_HALT_MARKET_N=3 + TOPN=5 (老韩 §14.5) — 小梁会签 + 老雷拍板 |
| GM-3 | YubiHSM 2-of-2 物理 (老沈+老雷) — 老雷确认亲自持有 + Q2 演练参与 |

---

## 10. 整改清单

| # | 整改 | Owner | 截止 |
|---|---|---|---|
| **C-1** | 老周 §18.6 加第 6 测试: VirtualConfirm fill 时序 + maker 撤单分布对齐老姜 E2E latency + 小袁 fill_rate sampler (仅 tcpdump+nm 不够证 paper 仿真度) | 老周+小蒋+老姜+小袁 | 6/26 |
| **C-2** | R-20 4 ts 全链路落地: 老周 v0.5 §21 enforcement + 老孙 v5.1 §5.4 IPC schema + 老韩 v0.3.1 §5 audit + R-20 §7 PIT 4 assert + 老唐 v1.1 envelope 加 event_ts/data_source_ts/source enum | 老周+老孙+老韩+老唐 | 6/26 |
| **C-3** | 老周 §17.6.1 PREGAME_FAR HALT 30000ms vs 老韩 §14.1 PREGAME HALT 15000ms 不一致 — 取 15000ms (更保守 + ≤ 30s 红线), 老周 v0.5 改 PREGAME_FAR HALT 15000ms (SETTLED/OUTRIGHT 仍 30000ms 硬上限) | 老周+老韩 | 6/19 |
| **C-4** | 老韩 §14.5 末行 SETTLED→INVALID_INTENT+sub_reason 澄清 — 推荐 INVALID_INTENT.sub_reason 字段 (保 21 enum 总数) | 老韩 | 6/19 |

**C-1~C-4 Sprint-2 W2 末未关闭 → ADR-003 转 Rejected, v0.5/v0.3.1/v5.1/audit v1.1 重出.**

---

## 11. 综合签字

| 文档 | 状态 | 条件 |
|---|---|---|
| 老周 v0.4 | **Conditional Accepted** | 6/19 关 C-3 + 6/26 出 v0.5 §21 (C-2) + 加测试 (C-1) |
| 老韩 v0.3 | **Conditional Accepted** | 6/19 关 C-4 + 6/26 出 v0.3.1 (C-2) |
| 老孙 v5 | **Conditional Accepted** | 6/26 v5.1 §5.4 IPC 4 ts (C-2) |
| 老沈 v2 (STRIDE+vendor) | **Accepted** | 无整改, 与老孙 + 老周 一一对应 |
| R-20 横向落地 | **Accepted (派单 6 条)** | C-2 闭合即落地 |
| 三 WAL + BLAKE3 | **Accepted** | 老唐 v1.1 加 4 ts + 2 个新 AET (6/19) |

---

## 12. 会签栏

- 老郭 (主审): __签 2026-05-28__
- 老雷 (GM 终签 + GM-1/GM-2/GM-3): ____
- 老周 (C-1+C-2+C-3): ____
- 老韩 (C-3+C-4+4 ts): ____
- 老孙 (4 ts IPC): ____
- 老沈 (无整改): ____
- 老唐 (4 ts + 2 AET): ____
- 老王 (无整改): ____
- 老高 + 老练 (CI R-20+R-11+R-12+nm): ____

— 老郭 (cpp-architecture-second-opinion), 2026-05-28
