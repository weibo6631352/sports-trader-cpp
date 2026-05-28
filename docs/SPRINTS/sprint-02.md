# Sprint-02 Backlog

- **Owner**: 老胡 (pm-project-manager)
- **周期**: 2026-06-13 (Mon) → 2026-06-26 (Fri), 2 周
- **Sprint Goal**:
  1. ADR-001 整改 13 项 + 跨洋网络实测 + JD 发布三件大事 (老胡 retro §4.1)
  2. RM C++ 实现 + signer v4 C++ 重写 + nonce_mgr + paper engine skeleton 启动
  3. M4.5 gate 评估器 + PIT CI v0.1 + chaos framework + endpoint matrix v3
- **Planning 会议**: 2026-06-13 9:00
- **Mid-Sprint Check**: 2026-06-18 (Wed) 14:00
- **Retro 会议**: 2026-06-26 (Fri) 16:00
- **基于**:
  - 主纪要: `docs/MEETINGS/2026-05-28-sprint1-retro-all-hands.md`
  - GM 决议总表: `docs/ADR/2026-05-28-gm-signoff-sprint1-retro.md`
  - 16 份真实发言 Sprint-2 承诺合并

---

## 0. Sprint-2 启动前置 (6/4 - 6/12)

| # | 启动条件 | Owner | 截止 |
|---|---|---|---|
| P-1 | 各 owner ack GM 决议总表 | 全员 | 6/4 |
| P-2 | 老郭 ADR 模板 + 听取确认清单 (D-18) | 老郭 | 6/4 |
| P-3 | 老雷 BIP39 6 条 sign (D-14) | 老雷 + 老黄 | 6/4 |
| P-4 | 老孙 v4.1 patch GCP SG 主 (D-13) | 老孙 | 6/4 |
| P-5 | 老吴 `127.0.0.1:7890` 代理产权 (老黄 §6 #4) | 老吴 | 6/4 |
| P-6 | 老雷 Sygnum 主体身份 + UBO (老黄 §1.3) | 老雷 | 6/11 |
| P-7 | 老黄 Goalserve odds 商务方案 (老黄 §2.2) | 老黄 + 老胡 | 6/11 |

---

## 1. Sprint-2 Backlog (28 ticket)

### 1.1 战斗单元 A 核心实施 (11 ticket)

| Ticket | 责任人 | 部门 | 交付物 | 验收人 | 周次 |
|---|---|---|---|---|---|
| S2-001 | 老周 | A | 架构 v0.4 (落 R-12 + 听取义务 + vCPU3 nice + Wave 10 endpoint + WSS 2 conn D-05) | 老郭 | W1 末 (6/19) |
| S2-002 | 老周 | A | 生命周期 v1.1 (single-flight 异常路径 + audit WAL fsync hang 主备切换) | 老郭 | W3 末 (6/26) |
| S2-003 | 老韩 | B/A | RiskGateway::evaluate() C++ 实现, p99 ≤ 200us 含 audit emit | 老姜 + 老郭 | W3 中 (6/22) |
| S2-004 | 老韩 | B/A | audit WAL 接老王 framework (GroupCommit, fail-closed 反压 3 场景过) | 小宋 + 老王 | W3 末 |
| S2-005 | 老韩 | B/A | RM v0.3 设计稿 (5 档 STALE D-06 + hot 判定接 小袁 + AET_SIGN_FAILED D-11) | 老郭 + 小宋 | W3 末 |
| S2-006 | 老李 | A | endpoint matrix v3 (含 14 条 HMAC test vector D-17 + /books active 池过滤) | 老孙 (test vector) + 老郭 (review) | W2 (6/19) |
| S2-007 | 老李 | A | "401 调试 SOP" + 月度 SDK diff sweep cadence | 小米归档 + 老练 CI | W1 (6/13) |
| S2-008 | 老叶 | A | polygon-rpc-selection v1.1 (分 Polygon WSS vs PM WSS + newHeads watchdog 6s + raw tx zeroize 配合 D-08) | 老郭 + 老周 | W1 (6/19) |
| S2-009 | 老叶 + 老孙 | A | nonce_mgr C++ 实现 + rpc-router 双 vendor 实现 + receiver 21 月度 sweep 工具 | 老姜 + 老唐 audit schema | W3 (6/26) |
| S2-010 | 老孙 | A/B | signer v4 C++ 实现 (libsecp256k1 + OpenSSL + libsodium + libfido2 + SecureBuffer 三层) | 老沈 + 老高 + 老郭 (架构) | W3 |
| S2-011 | 老姜 + 老李 + 老周 | A | vCPU0 4-5 conn burst 压测 (D-07 验 p99 < 50us), 不达标降级 (C) MVP NBA only | GM 老雷 | W3 (6/22) |

### 1.2 战斗单元 B 顾问 (3 ticket)

| Ticket | 责任人 | 部门 | 交付物 | 验收人 | 周次 |
|---|---|---|---|---|---|
| S2-012 | 老郭 | B | ADR 模板 _TEMPLATE.md 加听取确认清单 (D-18) + ADR-003 老周 v0.4 评审 | 老雷 | W1 末 (6/19) |
| S2-013 | 老沈 | B | KMS v2 跨 vendor 落地 (GCP SG 主 / Azure CH 备 / AWS JP 紧急 / YubiHSM 离线) | 老黄 + 老孙 | W3 |
| S2-014 | 老黄 | B | Sygnum + Taurus contact-made + 合同模板 + 合规 review Goalserve odds ToS | 老雷 + 老胡 | W1 (6/11 已定) ~ W2 |

### 1.3 战斗单元 C 量化研究 (6 ticket)

| Ticket | 责任人 | 部门 | 交付物 | 验收人 | 周次 |
|---|---|---|---|---|---|
| S2-015 | 小梁 | C | market-structure v1.1 (吸收小袁 264 样本 + 5¢ 阈值 + portfolio 容量模型 v1) | 老钱 + GM | W3 末 |
| S2-016 | 小程 | C | P0-01 catalog YAML 改 `0.05` (D-02) + 12 信号在 INPLAY 不需全程跟盘排查 | 小梁 + 老钱 | W1 (6/12) |
| S2-017 | 小袁 | C | microstructure v1.1 (INPLAY_HOT_CRIT 一档 + Mode A++ + maker=v2/colo 明文) + hot 判定 code-level 给 RM v0.3 + Mode A++ fill_rate sampler 联调小肖+小蒋 | 老韩 + 小蒋 + 老钱 | W2-W3 |
| S2-018 | 小肖 | C | SlippageModel C++ lib header-only + 7 case 单测 + Google Benchmark p99 < 200ns + paper-mode/slippage-mode 命名联签 (D-10) | 小蒋 + 老韩 + GM | W1 (6/19) |
| S2-019 | 小董 | C | stats v1.1 (G8 RM 误拒率观察项 + §5.5 G7 marginal 预判 + bayes_decay_monitor.py MVP + tools/m45_gate_evaluator.py 公式定稿) | 老韩 + 老钱 | W3 末 |
| S2-020 | 小蒋 | C | backtest C++ skeleton + Pinnacle/PM book Parquet 接入 + paper engine skeleton (R-21 闸 1) + PIT CI v0.1 设计稿 (D-16) | 小梁 + 小宋 + 老韩 | W3 (6/26) |

### 1.4 战斗单元 D 数据 (4 ticket)

| Ticket | 责任人 | 部门 | 交付物 | 验收人 | 周次 |
|---|---|---|---|---|---|
| S2-021 | 小余 | D | etl-pipeline v0.1 (Sprint-1 漏交 spec) + 5 endpoint ETL 骨架 + 客户端 diff blake3 + 4 时间戳 schema + WSS cold storage 90 天预算落实 | 老胡 + 老吴 | W1 (6/19) |
| S2-022 | 小段 | D | Goalserve ETL-1~ETL-8 全接实现 + 月度 sweep `data_quality_monthly.parquet` + 11 sport 探针 | 小余 + 老胡 | W2-W3 |
| S2-023 | 小冯 | D | WSS raw frame 90 天 cold storage 落库 + 1s snapshot 重建 + active 池配合老李 /books 过滤 | 小余 + 老李 | W2-W3 |
| S2-024 | 小邓 | D | data-contract v1.1 review (小余 etl v0.1) + PIT CI v0.1 review (小蒋 D-16) + score_model 数据 schema 需求清单给小余 | 小余 + 小蒋 + GM | W3 |

### 1.5 战斗单元 E 跨域 (4 ticket)

| Ticket | 责任人 | 部门 | 交付物 | 验收人 | 周次 |
|---|---|---|---|---|---|
| S2-025 | 老胡 | E | 风险登记 v2 (含 R-21 paper 联调 + R-22 跨域 listening 域扩 + R-23 Goalserve push 延迟 + 红线 R-14..R-19) + GM 周报 W2/W3 (双轨 OKR + 实战 D-09) | 老雷 | W1/W3 |
| S2-026 | 小林 | E | HC-01 老冀 + HC-02 小秦 JD 发布 + 候选池 ≥ 5/岗 (老胡 P0) | 老胡 + 老雷 | W1 (6/20) |
| S2-027 | 小米 | E | Sprint-1 各 owner v0.2 文档归档 + 老钱 v1.1 拒绝清单 + 16 retro 发言整理 + 401 SOP 归档 | 各 owner + 老雷 | W3 |
| S2-028 | 老练 | E | CI hard block: R-12 静态扫 + grep `co_await` in vCPU0 + 14 HMAC test vector + future-leak SQL + schema_drift_chaos | 老郭 + 老周 + 老李 + 小蒋 + 小宋 | W3 |

---

## 1.6 W2-W4 详细派单 (Sprint-2 W1 复盘后新增, 2026-05-28 老胡 update)

> **背景**: W1 实际超额完成 13 项 (计划 6 项), 主要源于用户 4 项高优指令 (Rust 撤 / 上链 deferred / 撤地域 / R-20 时间戳) 大幅简化 backlog. W2-W4 派单基于:
> - 上链 deferred → S2-008 / S2-009 / S2-010 部分归档, 改 PaperSigner mock + virtual_nonce/gas/confirm 三 stub
> - R-20 数据时间戳红线 → 11 owner 横向工程加入
> - paper engine 联调时间窗提前到 9/12 (vs OKR M4.5 10/29)
>
> **三态明示**: 每条派单标 Agreed / Compromised / Escalated.

### 1.6.1 W2 派单 (2026-06-22 Mon → 2026-06-26 Fri)

| # | Owner | 交付 | 截止 | 验收人 | 状态 |
|---|---|---|---|---|---|
| W2-01 | 老郭 | ADR-004 评审 R-20 时间戳红线 + 11 owner 落地路径; ADR-003 评审老周 v0.4 + RM v0.3 | 6/22 (Mon) | 老雷 | **Agreed** |
| W2-02 | 老吴 | AWS us-east-1 实开 (c6i.large × 2: prod + warm standby); 单 region, 不跨 vendor (撤地域后简化); SG / IAM / VPC 配置出 | 6/24 (Wed) | 老周 + 老雷 | **Agreed** |
| W2-03 | 老王 | WAL 骨架 C++ 代码 (基于 v0.1 framework: GroupCommit + fsync hang 主备切换 + 4 路 WAL 物理隔离: risk/paper/shadow/audit) | 6/26 (Fri) | 老韩 + 小宋 | **Agreed** |
| W2-04 | 小宋 | 测试 framework v0.1 启动: chaos test 4 场景 (WAL 满 / fsync hang / schema_drift / network partition) + replay framework 接 paper engine | 6/26 (Fri) | 老周 + 小蒋 | **Agreed** |
| W2-05 | 小林 | HC-01 老冀 + HC-02 小秦 JD 发布; 候选池 ≥ 5/岗 (撤地域后 JD 放宽 "非美总部经验" 硬约束) | 6/20 (Sat) | 老胡 + 老雷 | **Compromised** (6/20 发, 6/30 入职目标若失守走兜底) |
| W2-06 | 小米 | Sprint-1 owner v0.2 归档 + 老叶 4 文档 deprecated 标记 + Sygnum 文档 Superseded 标记 + 401 SOP 归档 | 6/26 (Fri) | 各 owner + 老雷 | **Agreed** |
| W2-07 | 老唐 | audit schema BLAKE3 加 4 时间戳 payload (R-20 派单, W1 末延到 W2 初) | 6/22 (Mon) | 老郭 + 老韩 | **Agreed** |
| W2-08 | 老高 | clang-tidy 加 R-20 时间戳缺失检查规则 + PR 模板更新 | 6/22 (Mon) | 老练 + 老郭 | **Agreed** |
| W2-09 | 老李 | endpoint matrix v3.1 标 ts 字段位置 (R-20 派单) | 6/26 (Fri) | 老郭 + 小段 | **Agreed** |
| W2-10 | 小余 | etl-pipeline v0.1 (Sprint-1 漏交 + R-20 4 ts 不等式校验 + 4 时间戳 schema + WSS 90 天 cold storage 预算) | 6/26 (Fri) | 老胡 + 小邓 | **Compromised** (W1 漏交, W2 必交, 不达标升级老雷) |
| W2-11 | 老韩 | RM v0.3 §audit schema 加 4 ts 字段 + assert 强制 (R-20 派单) | 6/26 (Fri) | 老郭 + 老唐 | **Agreed** |
| W2-12 | 老孙 | Signer v5 IPC schema 加 4 ts 字段 (R-20 派单) | 6/26 (Fri) | 老沈 + 老郭 | **Agreed** |
| W2-13 | 老胡 | 风险登记 v2 出 (R-21~R-30 update + R-31~R-33 新增) + GM 周报 W2 | 6/22 (Mon) | 老雷 | **Agreed** |

**W2 仪式**: Mid-sprint check 6/24 (Wed) 14:00, 重点盯 W2-02 AWS 实开 + W2-10 小余 etl + R-20 11 owner 进度.

### 1.6.2 W3 派单 (2026-06-29 Mon → 2026-07-03 Fri)

| # | Owner | 交付 | 截止 | 验收人 | 状态 |
|---|---|---|---|---|---|
| W3-01 | 老孙 | PaperSigner mock 实施 (撤 v4 真实 KMS unwrap, 改 v5 simplified mock: virtual_nonce / virtual_gas / virtual_confirm 三 stub + SecureBuffer 仍生效) | 7/3 (Fri) | 老沈 + 小蒋 | **Agreed** (上链 deferred ADR §6 老孙派单) |
| W3-02 | 小蒋 | paper engine 联调 W1 (skeleton → 与 RM v0.3 + SlippageLib + PaperSigner mock 联通; 跑通虚拟下单 → RM 审批 → PaperSigner mock → 虚拟 confirm → paper_audit.wal 闭环) | 7/3 (Fri) | 老韩 + 小肖 + 老孙 | **Agreed** (R-21 闸 2, 比 8/14 deadline 提前 6 周) |
| W3-03 | 小袁 | microstructure C++ 实现 (v1.1 INPLAY_HOT_CRIT 一档 + Mode A++ fill_rate sampler + hot 判定 code-level 给 RM v0.3) | 7/3 (Fri) | 老韩 + 小蒋 + 老钱 | **Agreed** |
| W3-04 | 老韩 | RiskGateway::evaluate() C++ 实现, p99 ≤ 200us 含 audit emit | 7/3 (Fri) | 老姜 + 老郭 | **Agreed** |
| W3-05 | 老周 | 生命周期 v1.1 (single-flight 异常路径 + audit WAL fsync hang 主备切换) | 7/3 (Fri) | 老郭 | **Agreed** |
| W3-06 | 老叶 + 老孙 | nonce_mgr / rpc-router / receiver 21 sweep **deferred 归档** (上链 ADR §3); 设计保留作 future activation | 即日 | 老郭 + 小米 | **Agreed** (上链 deferred ADR) |
| W3-07 | 小邓 | ML shadow signal 加 model_id + feature_snapshot_id + inference_ts (R-20 派单) | 7/3 (Fri) | 小程 + 老郭 | **Agreed** |
| W3-08 | 小段 | Goalserve ETL-1~ETL-8 实现 + 11 sport 探针 + 月度 sweep data_quality_monthly.parquet | 7/3 (Fri) | 小余 + 老胡 | **Agreed** |
| W3-09 | 小冯 | WSS raw frame 90 天 cold storage 落库 + 1s snapshot 重建 + active 池配合老李 /books 过滤 | 7/3 (Fri) | 小余 + 老李 | **Agreed** |
| W3-10 | 小董 | stats v1.1 (G8 RM 误拒率观察项 + tools/m45_gate_evaluator.py 公式定稿 + bayes_decay_monitor.py MVP) | 7/3 (Fri) | 老韩 + 老钱 | **Agreed** |
| W3-11 | 小邓 | data-contract v1.1 review (小余 etl v0.1) + PIT CI v0.1 review (小蒋) + score_model 数据 schema 给小余 | 7/3 (Fri) | 小余 + 小蒋 + 老雷 | **Agreed** |
| W3-12 | 老沈 | KMS v2 跨 vendor **简化** (撤 ≥1 非美总部硬约束, 改 AWS us-east-1 主 + AWS us-west-2 备, 单 vendor + region failover) | 7/3 (Fri) | 老黄 + 老孙 | **Agreed** (撤地域 ADR §5) |
| W3-13 | 小宋 | schema_drift_chaos daily CI 落地 + 抓到必补 ADR + 24h SLA | 7/3 (Fri) | 小余 + 老郭 | **Agreed** |

**W3 仪式**: Daily standup 紧盯 W3-02 paper engine 联调 (R-21 闸 2 关键事件); 周三老胡周报给老雷 (双轨 OKR + paper 联调进度).

### 1.6.3 W4 派单 (2026-06-29 Mon → 2026-07-03 Fri) — 各部门落代码 v0.1

> **W3 末 update (2026-05-28 Wave 19, 老胡)**: W3 ADR-003 整改 4/4 全闭环 + C++ 骨架 + WAL/SlippageModel/测试 framework 落代码 + 51/51 ctest pass, **W4 重排为各部门落代码 v0.1 (代码 commits 不再是文档)**. W3-02 ~ W3-04 evaluate() 代码挪到 W4 是 Agreed (工程顺序依赖). M1 节点评审顺延到 W5 (与原计划 7/9 一致).

| # | Owner | 交付 | 截止 | 验收人 | 状态 |
|---|---|---|---|---|---|
| W4-01 | 老韩 | **RiskGateway::evaluate() C++ v0.1** (基于 v0.3.1, 接 WAL writer + audit emit + 5 档 STALE + AET_SIGN_FAILED, p99 ≤ 200us 含 audit) | 7/3 (Fri) | 老姜 + 老郭 + 老王 | **Agreed** |
| W4-02 | 老唐 + 老韩 | **audit_writer C++ v0.1** (BLAKE3 加 4 ts + GroupCommit 接老王 WAL framework + fail-closed 反压 3 场景) | 7/3 (Fri) | 小宋 + 老王 | **Agreed** |
| W4-03 | 小蒋 | **paper engine main C++ v0.1** (skeleton → main: 接 RM v0.1 + SlippageModel + PaperSigner mock + paper_audit.wal 闭环; R-21 闸 2) | 7/3 (Fri) | 老韩 + 小肖 + 老孙 | **Agreed** |
| W4-04 | 小程 + 小梁 | **P0-01 signal C++ v0.1** (catalog YAML 改 0.05 已 W1 交 → 落代码: signal_engine 输出 P0-01 触发条件 + INPLAY_HOT_CRIT hot 判定接小袁) | 7/3 (Fri) | 老钱 + 小袁 | **Agreed** |
| W4-05 | 老李 | **Polymarket client C++ v0.1** (基于 endpoint matrix v3 + 14 HMAC test vector + REST + WSS 2 conn + /books active 池过滤) | 7/3 (Fri) | 老孙 + 老郭 | **Agreed** |
| W4-06 | 小段 | **Goalserve client C++ v0.1** (基于 official-doc v3 + sport×odds v2.1, 11 sport 探针 + inplay/livescore/pregame 三 stream) | 7/3 (Fri) | 小余 + 老胡 | **Agreed** |
| W4-07 | 老孙 | **Polygon RPC mock C++ v0.1** (上链 deferred 后 mock: virtual_nonce / virtual_gas / virtual_confirm 三 stub + SecureBuffer 仍生效) | 7/3 (Fri) | 老沈 + 小蒋 | **Agreed** |
| W4-08 | 小袁 | **microstructure C++ v0.1** (INPLAY_HOT_CRIT 一档 + Mode A++ fill_rate sampler + hot 判定 code-level 给 RM v0.1) | 7/3 (Fri) | 老韩 + 小蒋 + 老钱 | **Agreed** |
| W4-09 | 小余 | **etl-pipeline C++ v0.1** (R-20 4 ts 不等式 + 5 endpoint ETL + WSS cold storage 90 天 + blake3 diff) — W2 Compromised 必交 | 7/3 (Fri) | 老胡 + 小邓 | **Compromised** (W2/W3 已推, W4 不可再推, 不达标升级老雷) |
| W4-10 | 小邓 | **ML shadow signal C++ v0.1** (model_id + feature_snapshot_id + inference_ts + data-contract v1.1 接口) | 7/3 (Fri) | 小程 + 老郭 | **Agreed** |
| W4-11 | 小冯 | **WSS raw frame cold storage v0.1** (90 天 + 1s snapshot 重建 + active 池配合) | 7/3 (Fri) | 小余 + 老李 | **Agreed** |
| W4-12 | 小宋 | **CI 反模式拦截**: (1) R-3 grep `sports-tail-trader` / `gh api repos/weibo6631352/...` (GM 错 #3 enforcement); (2) schema_drift_chaos daily; (3) R-20 PIT grep 升级到 hard block | 7/3 (Fri) | 老练 + 老郭 | **Agreed** |
| W4-13 | 老胡 | **风险登记 v2.1** (R-21 闸 2 验证 + R-31 PIT 闭环 + R-35/R-36/R-37 新增) + GM 周报 W4 | 7/1 (Wed) | 老雷 | **Agreed** |
| W4-14 | 小米 | W3 12 篇 R-20 回灌 → W4 增量 8 篇 (W3 落代码后的接口文档同步) + GM 错 #3 归档入 incident log | 7/3 (Fri) | 老胡 + 老雷 | **Agreed** |

**W4 仪式**: Daily standup 紧盯 W4-03 paper engine main (R-21 闸 2 核心事件) + W4-09 小余 etl-pipeline (W2/W3 累计 Compromised, W4 必交); Mid-week 7/1 (Wed) 老胡周报给老雷.

### 1.6.4 W5 派单 (2026-07-06 Mon → 2026-07-10 Fri) — M1 节点评审 + 端到端联调

| # | Owner | 交付 | 截止 | 验收人 | 状态 |
|---|---|---|---|---|---|
| W5-01 | 老郭 + 老周 + 老韩 + 老胡 | **M1 节点评审** (T+6 周 2026-07-09): 架构 v1.0 冻结 + 数据接入联调 + RM C++ v0.1 验收 + paper engine main W1 通过 + R-20 落地全闭环 | 7/9 (Thu) | GM 老雷 | **Agreed** (OKR M1 deadline) |
| W5-02 | 老陈 + 老吴 + 小段 | **跨洋网络实测联调** (S1-021 残留): us-east-1 → Polymarket / Goalserve / Polygon edge 实测 p50/p95/p99/p99.9/max 五档, 老郭基于实测重定 RM 阈值 (R-01 触发条件评估) | 7/10 (Fri) | 老韩 + 老郭 | **Agreed** |
| W5-03 | 老姜 + 老李 + 老周 | vCPU0 4-5 conn burst 压测 (D-07 验 p99 < 50us); 不达标走老钱 §5.2 升级路径 (MVP NBA only) | 7/8 (Tue) | GM 老雷 | **Compromised** (压测结果若不达标, 走兜底升级) |
| W5-04 | 小余 + 小邓 + 小蒋 | **数据接入端到端联调** (M1 KR-A-4 + KR-D-1/D-2): Goalserve + Polymarket REST/WSS 端到端跑通, p99 延迟 < 200ms; ETL pipeline + paper engine 接通; 4 ts 不等式全程校验 (R-31 闭环验证) | 7/9 (Thu) | 老周 + 老胡 | **Agreed** |
| W5-05 | 小蒋 + 老韩 | **paper engine 联调 W2** (W4 main 落地后 → 跑通虚拟下单 → RM 审批 → PaperSigner mock → 虚拟 confirm → paper_audit.wal 闭环; R-21 闸 3 — 联调真跑通) | 7/8 (Tue) | 老周 + GM | **Agreed** |
| W5-06 | 小米 | M1 文档归档 + 风险登记 v3 出 (M1 通过后关闭对应风险) | 7/10 (Fri) | 老胡 + 老雷 | **Agreed** |
| W5-07 | 老练 | CI hard block 全跑通: R-3 老项目反模式 + R-12 静态扫 + grep co_await + 14 HMAC test vector + schema_drift_chaos + R-20 时间戳缺失 | 7/10 (Fri) | 老郭 + 老高 | **Agreed** |
| W5-08 | 小林 | HC-01 老冀 + HC-02 小秦 入职状态确认; 6/30 deadline 后实际入职情况 → 触发兜底? | 7/10 (Fri) | 老胡 + 老雷 | **Escalated** (若 7/10 未入职, 触发甘特 §4.4 兜底: 老叶兼链上运维 [deferred 后影响降低] + 老周兼信号-执行, M3 顺延 2 周) |
| W5-09 | 老胡 | Sprint-2 retro + Sprint-3 backlog v0.1 启动 | 7/10 (Fri) | 老雷 | **Agreed** |

**W5 仪式**: M1 评审会 7/9 (Thu) 14:00, GM 主持; Sprint-2 retro 7/10 (Fri) 16:00.

### 1.6.4-bis W5 细化派单 (Wave 20 中期 update, 2026-05-28 老胡)

> **背景**: W4 Wave 19 6 部门并行落代码 (W4-01/02/03/04/06 5 项已 Agreed 已交, 193/193 ctest pass). 原 §1.6.4 W5 9 项是 "M1 评审 + 联调" 高阶视角, 本 update 把 M1 milestone 拆到**代码 owner 层** (9 项独立 ticket, 与原 §1.6.4 平行), W5 同时跑两批: M1 评审主线 (原 §1.6.4) + M1 代码补齐主线 (本 §1.6.4-bis).
> **依赖**: W5-05bis 依赖小蒋 W4-03 paper engine main 已 ✓ (Wave 19 1233 行 + 27 测试 E2E); W5-04bis 依赖 W5-01bis ~ W5-03bis.
> **三态明示**: 每条标 Agreed / Compromised / Escalated.

| # | Owner | 交付 | 截止 | 验收人 | 状态 | 依赖 |
|---|---|---|---|---|---|---|
| W5-01bis | 老李 | **polymarket-client C++ 实现** (paper engine 真 PM order layer; REST + WSS + 14 HMAC test vector + /books active 池过滤 + 4ts 标 ingestion_ts) | 7/10 (Fri) | 老孙 + 老郭 | **Agreed** | W4-05 推 W5 → 本 ticket |
| W5-02bis | 小冯 | **PM WSS subscriber + sports channel reconnect + back-pressure** (sports-api/ws + market 双 channel, exp backoff + bounded queue + drop policy) | 7/10 (Fri) | 老李 + 小余 | **Agreed** | W5-01bis (复用 client 框架) |
| W5-03bis | 小石 | **SPSC ring buffer 5 capacity 落地** (rigtorp SPSCQueue, lock-free, cap 65536, 接 PM WSS → SignalEngine 单写单读链路) | 7/9 (Thu) | 老周 + 老姜 | **Agreed** | 独立 (无依赖) |
| W5-04bis | 老李 + 老吴 | **end-to-end smoke test** (`build/scripts/e2e_smoke.sh`: paper engine + Goalserve client + PM client + WSS + SPSC + audit WAL 全链路启动, 60s 跑通, fail-fast) | 7/10 (Fri) | 老周 + GM 老雷 | **Agreed** | W5-01bis ~ W5-03bis 三项全 ✓ |
| W5-05bis | 小郑 | **Prometheus metrics v0.1** (prometheus-cpp 接入, paper engine 优先 12 指标: rg_evaluate_p99 / paper_orders_total / paper_audit_write_lag / wal_fsync_p99 / wss_reconnect_total / spsc_queue_depth / goalserve_4ts_violation_total / signal_p0_01_triggered / kelly_fraction_avg / virtual_fill_rate / mode_aclass / signer_paper_audit_path) | 7/10 (Fri) | 老吴 + 老韩 | **Agreed** | 小蒋 paper W4 ✓ (Wave 19) |
| W5-06bis | 小苏 | **UI wireframe v0.2 + Grafana mockup** (paper engine dashboard mockup, 12 指标可视化布局; UI 整体框架 v0.2 含 P0-01 trigger feed + audit timeline) | 7/10 (Fri) | 老钱 + 小宫 | **Agreed** | Wave 20 可并 (与 W5-05bis 并行) |
| W5-07bis | 小宋 | **integration test framework 起步** (`tests/integration/` 新建; `paper_audit.wal verify` test: 跑 stcpp_paper E2E 60s → parse paper_audit.wal → verify hash chain + 4ts 单调 + AET 12 类全到位 + R-11 build-time 分流不污染真账本) | 7/10 (Fri) | 老韩 + 老唐 + 小蒋 | **Agreed** | 小蒋 paper W4 ✓ + 老唐 audit W4 ✓ |
| W5-08bis | 老吴 | **Docker compose dev stack** (`docker-compose.dev.yml`: paper engine + prometheus + grafana 三服务, 本地 `docker compose up` 60s 内全起, dev 环境复现性入门) | 7/10 (Fri) | 老周 + 老练 | **Agreed** | W5-05bis Prometheus metrics ✓ |
| W5-09bis | 老高 | **PR review v1.1** (加 R-20 grep + R-11 grep + persona 边界 grep — 任何 PR 落到 `.claude/agents/<owner>.md` 范围外的代码必 review-block; 落档 `docs/RESEARCH/laogao-pr-review-v1.1.md`) | 7/8 (Tue) | 老郭 + 老练 | **Agreed** | 独立 (无依赖) |

**W5 细化仪式**: 7/8 (Tue) 14:00 W5 mid-week check (老胡主持), 重点盯 W5-04bis E2E smoke test 是否可在 7/10 跑通; 7/10 (Fri) 上午 10:00 老胡 + 老雷 1:1 review 9 项交付, 下午 M1 评审会前确认无阻塞.

### 1.6.5 W2-W5 派单总计 (W3 末 update, W4 Wave 20 中期再 update, W5 末 Wave 25 6/01 主管周同步 update)

- **W2**: 13 项 (Agreed 11 / Compromised 2 / Escalated 0)
- **W3**: 13 项 (Agreed 5 / 部分 Agreed 3 / Compromised 5 / Escalated 0) — 实际 ADR-003 整改 + C++ 骨架 + WAL/SlippageModel/测试 framework 落代码超额, evaluate() 代码挪 W4
- **W4**: 14 项 (Agreed 13 / Compromised 1 / Escalated 0) — 各部门落代码 v0.1; W4 中期 (Wave 20) **5/14 提前完成 = 36% 中期超额**, 详见 `docs/SPRINTS/sprint-02-w4-midweek-progress.md`
- **W5**: 9 项 (Agreed 7 / Compromised 1 / Escalated 1) — M1 节点 + 端到端联调 [§1.6.4]
- **W5 细化** (Wave 20 中期 update): 9 项 (Agreed 9 / Compromised 0 / Escalated 0) — M1 代码补齐 + e2e smoke + Prometheus + Docker compose [§1.6.4-bis]
- **合计**: **58 项** (Agreed 45 / 部分 3 / Compromised 9 / Escalated 1)
- **deferred 归档**: 5 项不变 (S2-008 / S2-009 / W3-06 / Sygnum / 跨 vendor KMS)

#### 1.6.5.bis W5 主管周同步 v1 outcomes (2026-06-01, Wave 25, 老胡)

> **背景**: ADR-005 立的"主管周同步"第一次正式会议 (W5 试点末 → W6 硬约束首次). 集成 5 主管 + 老郭 + GM/CPO/HR 7 个独立 input. 会议纪要 `docs/MEETINGS/2026-06-01-manager-sync-w5-v1.md`.

**W5 末 Wave 24 战果 (commit 3ab5dfb):**
- **测试**: 260 → 312 (100% PASS, 全模块 + integration 14 case 新增)
- **代码新增**: 老李 PolymarketClient v0.1 1050 行 + 小冯 PM WSS subscriber 1044 行 + 小宋 integration test framework 1132 行 + 老沈 ADR-004 patch 82 行 + 老沈 BUG-W5-001 P0 patch 97 行 + 老高 PR review v1.1 213 行 + 老徐 escalate flow v0.2 363 行
- **红线 enforce**: R-1 (audit_id 非空) + R-7 (paper/live CMake 物理隔离) + R-11 (4 wal) + R-12 (WSS 非阻塞 p99 3.9us) + R-20 (4ts UPSTREAM_PAYLOAD) + R-33 (第 5 host)

**W5 主管层试点 KPI 实测 (6/01 会上确认):**
- **主管派单覆盖率 100%** (目标 ≥ 70%, **超目标 30 pp**) — W5 Wave 24 6 IC + 4 follow-up 任务全部 spec by 主管
- **主管 SLA / GM SLA**: 6/1 EOD 截止收集 (会议纪要 §4 占位等回填)
- **协商会次数 0** (软肋, 6/02 W5 二启动第 1 次)
- **IC 越主管找 GM**: 0 次
- **GM 越主管派 IC**: 1 次 (BUG-W5-001 老沈 P0 紧急 < 2h, ADR-005 §3.2 例外允许)

**W5 末 W6 启动 8 决议 (6/01 会上拍板, 详见 §8 决议清单):**
1. ADR-004 patch + BUG-W5-001 patch 4 会签 closeout (老韩 + 老郭 + 老高 + GM)
2. ADR-006 候选 HTTP client cpp-httplib (老郭倾向)
3. ADR-007 候选 VirtualMatcher 切 Mode A — W5 末再切
4. **ADR-008 撤回 Pinnacle 路径 (小段 v3 推翻 v2.1) → 立 Goalserve fair value de-vig 算法选型** (小梁 W6 起建模)
5. **GM 错 #9 永久 enforcement 4 条 ack** (查 vN + owner 点名推翻 + 周报 SSOT 段 + Pinnacle 撤回)
6. **周报模板升 v2** (`docs/META/weekly-report-template-v2.md` 加 §4 SSOT 版本演进段, 老胡 owner, W5 五首次套用)
7. **主管层 W5 试点 → W6 硬约束转换** — 2026-07-13 (W6 一) 起 5 题自检第 5 题 enforce
8. 月度主管轮值 GM 助理 6 月启动 (老周)

**W5 末验收清单 (W6 一硬约束转换前):**
- [ ] 6/01 EOD: 14 跨主管 ASK 回填 ack 状态 (主管 SLA + GM SLA 真数字)
- [ ] 6/01 EOD: ADR-004 + BUG-W5-001 patch 4 会签 closeout
- [ ] 6/02 14:00: 第 1 次需求-工程协商会启动 (老胡主持 30min)
- [ ] 6/02 EOD: 小田归属仲裁升老雷拍板 (ASK-A-4 + ASK-D-1 同议题)
- [ ] 6/06 EOW: HC-04/05/06/07/08 5 JD 草稿 (小林 owner, 5 主管联签)
- [ ] W5 五 周报首次套用 v2 模板 (SSOT 版本演进段首次落地: 小段 v2.1 → v3 推翻 Pinnacle, Pinnacle 决议 6/01 撤回)

**W6 启动日期:** 2026-07-13 (W6 一), 主管层硬约束首次运行, GM 派 IC 5 题自检第 5 题 fail 拒派单, IC 越主管找 GM 拒接 (例外按 ADR-005 §3.2: 顾问团 / 紧急 P0 < 2h / 主管本人 / 跨多单元统筹)

**W6 派单总数:** 50+ ticket (老胡 master backlog 维护, W5 末 backlog v1 交, 周报 §6 公示)

---

## 2. 关键路径 (Critical Path)

| 优先级 | 事项 | Owner | 截止 | 阻塞下游 |
|---|---|---|---|---|
| P0 | ADR-001 整改 13 项落地 + v0.4 整合 (S2-001) | 老周 + 老韩 | 6/26 | 所有 C++ 实现 |
| P0 | S1-021 跨洋网络实测 (Sprint-1 残留, 老陈 + 老吴 + 小段) | 跨 D 单元 | 6/26 | RM 阈值校准 |
| P0 | HC-01 + HC-02 JD 发布 + 候选池建仓 (S2-026) | 小林 | 6/20 | 7/1 老冀 + 小秦入职 |
| P0 | vCPU0 4-5 conn 压测 (S2-011) | 老姜+老李+老周 | 6/22 | D-07 拍板 + 老周 v0.4 §17.1.1 |
| P0 | HMAC test vector 14 条 (S2-006 ack 老孙) | 老李 | 6/13 | 老孙 v4 wire 单测 |
| P0 | paper skeleton 并行 (S2-020 闸 1) | 小蒋 | 6/26 | R-21 缓解 + 8/14 联调 |

---

## 3. Sprint-2 风险登记 (W1 复盘后 update, 详见 `docs/RESEARCH/laohu-risk-registry-v2.md`)

> **W1 后变更**:
> - **降级**: R-01 (上链 deferred 后只伤 paper) / R-21 (skeleton W1 提前)
> - **撤销归档**: R-26 (Sygnum 承诺 Superseded) / R-30 (撤地域 ADR)
> - **新增**: R-31 (R-20 PIT 违例) / R-32 (paper 联调时间窗提前暴露失败) / R-33 (老吴 v1 inplay/oddsfeed misjudgment 流程红线)

| Risk | 概率 | 影响 | 缓解 | Owner |
|---|---|---|---|---|
| R-21 paper engine 联调时间不够 (8/14 deadline) | **中** (W1 skeleton 提前) | 高 | 小蒋 W3-02 联调 W1 提前 6 周 (R-21 闸 2) | 小蒋 + 老胡 周三 check-in |
| R-22 跨域 listening 域扩 (欧洲/亚洲场次) 破坏 us-east-1 同区前提, RM 阈值要重审 | 低 | 高 | ADR-001 §3.2 挂依赖, listening 扩域走 ADR 变更流程 | 老胡 + 老周 |
| R-23 Goalserve push p95 7s 延迟成 alpha 瓶颈 (INPLAY_HOT_CRIT 进入延迟 5-10s) | 中 | 中 | Sprint-3 若 alpha 数据证实瓶颈再重审; 小袁 v1.1 写入 known risk (C-06) | 小袁 + 小段 |
| R-24 vCPU0 4-5 conn 压测不达标 → 降级 MVP 单 sport (老钱 §5 拒绝清单冲突) | 中 | 高 | S2-011 压测验; 不达标走老钱 §5.2 战略升级路径 (D-07) | GM 老雷 |
| R-25 5¢ 阈值后 P0-01 触发频次 10-30 笔/月, M4.5 G6 (≥50 笔) 边际触碰 → 21 天窗口 | 中 | 中 | 小蒋 backtest v2 报告 (7/30) 给 trade-off 数字, 7/16 报告前小梁+小董+老雷三方决议 | 小蒋 + 小梁 + 小董 |
| R-26 ~~Sygnum onboarding 6/11 contact-made~~ | **撤销归档** | — | 撤地域 ADR § Sygnum 承诺 Superseded | 归档 |
| R-27 老孙 v4 SecureBuffer 反汇编 Q21 Sprint-3 内未自动化 | **降级** (上链 deferred 后无真签名) | 低 | v5 simplified 仅 PaperSigner mock, audit 频次降到季度 | 老何 + 老孙 + 老沈 |
| R-28 schema_version bump 放宽到 Slack + CI, schema_drift_chaos 漏抓 | 中 | 高 | 小宋 W3-13 落地 daily CI; 抓到必补 ADR + 24h | 小宋 + 小余 |
| R-29 老李 endpoint matrix v3 + HMAC test vector W1 给老孙 | **关闭** (W1 已交) | — | W1 已交 v3 + 14 HMAC test vector | 关闭 |
| R-30 ~~美国 entity D-15 Escalated~~ | **撤销归档** | — | 撤地域 ADR § 公司主体未来迁合规地区 | 归档 |
| R-31 (新) R-20 PIT 违例: 4 ts 不等式守不住 → backtest / paper / live 三方不一致 | 中 | 高 | 11 owner 派单 (W1 末 4 项 + W2 末 5 项 + W3 末 2 项); 老郭 ADR-004 W2 评审 | 老胡 + 老郭 |
| R-32 (新) paper trading 联调时间提前 (9/12 首判 vs OKR 10/29) 提前暴露失败风险 → 老雷决策窗压力 | 中 | 中 | 9/12 第 1 次窗口 = 早期信号, 不代表 M4.5 结论; 失败不触发 §138 "3 次失败 战略复盘"; 老雷预审 §7 §3 决策 | 老胡 + 老雷 |
| R-33 (新) 老吴 v1 inplay/oddsfeed misjudgment (cross-check 已 resolve) → 流程红线: 数据源 SSOT 撰写前必须 cross-check 一手文档 | 低 | 中 | 老陈 SSOT v1 已立, 老吴 / 小段 / 老李 类文档须经老陈 cross-check + 老郭 review | 老陈 + 老郭 |

---

## 4. 仪式表

| 仪式 | 日期 | 输出 |
|---|---|---|
| Sprint-2 Planning | 6/13 (Mon) 9:00 | Sprint-2 backlog 锁版 + owner ack |
| Daily Standup | 每日 10:00 (15min) | 昨完成 / 今做 / blocker; blocker 4h 内 owner 响应否则升级老雷 |
| Mid-Sprint Check | 6/18 (Wed) 14:00 | 进度通报 (重点: S2-011 vCPU0 压测 / S2-006 HMAC test vector / S2-020 paper skeleton) |
| GM 周报 W2 | 6/15 (Mon) 18:00 | 老雷 1 页纸 (双轨 OKR + 实战) |
| GM 周报 W3 | 6/22 (Mon) 18:00 | 同上 |
| GM 周报 W4 | 7/6 (Mon) 18:00 | 老雷 1 页纸 (M1 节点前最后一次盘整) |
| **M1 节点评审** | **7/9 (Thu) 14:00** | 架构 v1.0 + 数据接入联调 + R-20 落地 + paper 联调 W1 评审 (GM 主持) |
| Sprint-2 Retro | **7/10 (Fri) 16:00 (顺延)** | retro 文档 + risk registry v3 + Sprint-3 backlog v0.1 启动 |

---

## 5. Sprint-2 GM 周报状态预期 (W1 复盘后 update)

- **W1 (5/28 报)**: 绿. 13 项交付 (计划 6 项, +117%), 用户 4 指令红利落地, R-20 红线立
- **W2 (6/22)**: 绿/黄. 老郭 ADR-004 评审 (R-20) + AWS 实开 + WAL 骨架, 风险点 S2-021 小余 etl 不达标
- **W3 (6/29)**: 黄 (高概率). W3-02 paper engine 联调 W1 是 critical 事件, 不通则 R-21 升级红; HC 候选池若 < 3/岗 黄牌
- **W4 (7/6)**: 黄/绿. M1 节点 7/9 评审, 看跨洋实测 + vCPU0 压测 + 数据接入联调 + R-20 是否全闭环
- **红线 1**: 7/10 老冀 + 小秦未入职, 触发 R-07 + 甘特 §4.4 兜底 (老叶兼链上 [deferred 后影响降] + 老周兼信号-执行, M3 顺延 2 周)
- **红线 2**: 7/9 跨洋实测 p99.9 > 2s, RM 阈值重审 (R-01 触发条件); 上链 deferred 后只伤 paper, 不伤实盘资金, 影响降低但仍需 GM 决议
- **红线 3**: 7/3 paper engine W1 联调不通 (R-21 闸 2), 直接影响 9/12 首判窗口 → 落到 OKR M4.5 10/29

---

## 6. Sprint-2 完成定义 (DoD)

- [ ] 28 ticket 全部 owner 验收人签字
- [ ] CI hard block 4 条 (R-12 静态扫 + grep co_await + HMAC test vector + schema_drift_chaos) 跑通
- [ ] vCPU0 4-5 conn 压测数据交付 (D-07)
- [ ] 风险登记 v2 + 红线 R-14..R-19 全员签收 (老黄推送, 小米归档)
- [ ] paper skeleton PR 进 main (R-21 闸 1)
- [ ] PIT CI v0.1 设计稿出 (D-16)
- [ ] Sprint-3 backlog v0.1 启动 (7/3 begin)

---

**Sprint-2 启动签字**:
- 主持: 老胡 (PM)
- GM 批: 老雷

— 老胡 + 老雷, 2026-05-28 (Sprint-1 Retro 散会同日落档)
