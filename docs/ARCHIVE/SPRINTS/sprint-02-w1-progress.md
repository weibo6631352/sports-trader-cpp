# Sprint-2 W1 实际周报 (给老雷)

- **Owner**: 老胡 (pm-project-manager)
- **周期**: 2026-06-15 (Mon) → 2026-06-19 (Fri), Sprint-2 W1
- **报告日**: 2026-05-28 (Sprint-1 retro 散会同日, W1 提前盘整)
- **状态**: 绿 (远超 W1 计划) + 1 高优 ADR 红线 (R-20)
- **抄送**: 老郭 / 老韩 / 老周 / 老孙 / 老沈
- **关联**:
  - 主 backlog: `docs/SPRINTS/sprint-02.md` v0.1
  - 用户 4 项高优指令 (Rust 撤 / 上链 deferred / 撤地域 / R-20 时间戳)
  - GM 决议总表: `docs/ADR/2026-05-28-gm-signoff-sprint1-retro.md`

---

## 1. TL;DR 给老雷 (一段话)

W1 计划 6 件大事, 实际交付 13 件 (+117%). 主要原因不是赶工, 而是用户 4 项高优指令落地后 backlog 大幅简化 (上链 deferred 砍了 5 个模块的实施, 撤地域砍了跨境合规和跨 vendor KMS), 各 owner 把腾出来的时间用在了简化重构 + 文档收口. 当前 2 件事要老雷知道: **(1) 上链 deferred 后 R-01 跨洋链路抖动风险从 H×H 降到 M×H** (RM HALT 影响域缩小, 只伤 paper, 不伤实盘资金); **(2) R-20 数据时间戳红线立了**, 11 个 owner 收到派单, 是当前 Sprint-2 最重的横向工程, 老郭 W2 评审, 我会盯到位.

---

## 2. W1 计划 vs 实际 (与 Sprint-2 backlog v0.1 对照)

### 2.1 计划 6 项 (S2 backlog 中 W1 deadline)

| Ticket | Owner | 计划 | 实际 | 状态 |
|---|---|---|---|---|
| S2-007 | 老李 | 401 SOP + SDK diff cadence | 已交, 4 项补救合并到 v3 | Agreed |
| S2-008 | 老叶 | polygon-rpc v1.1 | **deferred 归档** (上链 ADR) | Agreed (砍单) |
| S2-012 | 老郭 | ADR 模板 + ADR-003 老周 v0.4 评审 | v0.4 出, 评审待 W2 | Agreed |
| S2-016 | 小程 | catalog YAML 改 0.05 + 12 信号排查 | 已交 | Agreed |
| S2-018 | 小肖 | SlippageModel C++ lib 联签 | v1 已交 (header-only + 7 单测) | Agreed |
| S2-021 | 小余 | etl-pipeline v0.1 (Sprint-1 漏交) | **W1 未交, 推 W2** | Compromised (W2 必交) |

W1 计划完成率 **5/6 = 83%**, 唯一掉队 S2-021 老胡 W2 紧盯.

### 2.2 实际超额交付 (13 项, 含计划 5 + 新增 8)

| # | Owner | 交付 | 性质 |
|---|---|---|---|
| 1 | 老周 | 架构 v0.4 (上链 deferred 整合 + Paper mock §18 + WebSocket 中间方案 §17) | 计划内 + 加章 |
| 2 | 老韩 | RM v0.3 (5 档 STALE / hot 判定 / AET_SIGN_FAILED) | 计划内 (原 W3 提前) |
| 3 | 老孙 | signer v5 simplified (撤 4 vendor + 撤 Shamir 5 地点) | 用户撤地域指令落 |
| 4 | 老沈 | threat-model v2 + key-vendor v2 (跨 vendor 撤) | 用户撤地域指令落 |
| 5 | 老王 | WAL framework v0.1 (GroupCommit + fsync hang 主备切换) | 计划内 |
| 6 | 小蒋 | paper engine skeleton v1 (R-21 闸 1 提前) | 计划 W3 提前到 W1 |
| 7 | 小肖 | SlippageLib v1 + 7 单测 + Google Benchmark | 计划内 |
| 8 | 老李 | endpoint matrix v3 (含 14 HMAC test vector + active 池) | 计划内 |
| 9 | 小段 | Goalserve official-doc v3 + sport×odds v2.1 | 计划内 |
| 10 | 老陈 | api-rate-latency SSOT v1 + network-bench v1 | 计划内 (跨洋实测前置) |
| 11 | 老吴 | proxy-goalserve-bandwidth v1 (代理产权确认) | 计划内 |
| 12 | 小邓 | ML roadmap v2 + data-infra v1 | 上链撤后 ML 仍跑 |
| 13 | 老黄 | us-entity-feasibility v1 + compliance-redline v2 (简化) | 用户撤地域指令落 |

**外加 ADR 6 件**: GM signoff sprint1 retro / GM signoff RM v0.2 / GM signoff paper-trade / defer-onchain / jurisdictional-deferral / R-20 timestamp redline.

### 2.3 W1 未做项 (推 W2)

| Ticket | Owner | 推迟原因 | W2 截止 |
|---|---|---|---|
| S2-021 | 小余 | Sprint-1 漏交 + R-20 红线后 schema 要重画 4 时间戳 | 6/26 |
| S2-026 | 小林 | HC JD 起稿在跑, 待 6/20 发布 | 6/20 |
| S2-006 review | 老郭 | 老周 v0.4 评审待 ADR-003 走完 | 6/22 |

---

## 3. 用户 4 项高优指令落地确认

| 指令 | 状态 | 落地证据 | 影响 backlog |
|---|---|---|---|
| **Rust 撤回 C++ ground-up** | Agreed (已闭环 Sprint-1) | 老周 v0.3 / v0.4 全 C++; 老孙 v4-cpp; 小蒋 v0.2-cpp | 无新增 ticket |
| **上链 deferred** | Agreed (GM 决议, 原 gm-decision-defer-onchain-until-profitable.md 已删) | 老叶 4 文档归档; 老孙 v5 simplified; 老周 v0.4 §18 Paper mock | S2-008 / S2-009 / S2-010 部分 deferred (砍 ~3 周工程量) |
| **撤地域合规纠缠** | Agreed (新 ADR) | `gm-policy-jurisdictional-deferral.md`; 老黄 v2 / 老沈 v2 / 老孙 v5 simplified; Sygnum 承诺 Superseded | 老黄 §1.3 撤 / 老沈 跨 vendor 撤 / 老孙 跨境 Shamir 撤 |
| **R-20 数据时间戳红线** | Agreed (新红线) | `gm-redline-data-source-timestamping.md`; 11 owner 派单 (老韩/老唐/小蒋/小邓/老高/老孙/老李/小段/小余/小米/老郭) | 见本周报 §5 |

---

## 4. 13 个 owner 名单 (W1 实际交付 by owner)

> 老雷如果要点名表扬, 这是名单. 没列出来的 owner 也在跑 (老姜/老钱/小董/小程/小袁/小宋/小冯/小石/老练/老彭等), W1 内部产出在 Sprint-2 W2-W3 deadline 内.

A (系统): 老周 v0.4 / 老王 WAL v0.1 / 老吴 bandwidth v1 / 老陈 SSOT v1
B (风控合规): 老韩 v0.3 / 老沈 v2 / 老黄 v2 + us-entity v1
A (链上 → 简化): 老孙 v5 simplified
C (量化): 小肖 SlippageLib v1 / 小蒋 paper skeleton v1
D (数据): 小段 v3 + sport×odds v2.1 / 小邓 ML v2 + data-infra v1
A (协议): 老李 endpoint v3

---

## 5. R-20 数据时间戳红线 — 11 owner 派单跟进

> 这是 W1 最重的横向工程, W2 评审, 我每天 standup 必盯.

| Owner | 动作 | 截止 | 状态 |
|---|---|---|---|
| 老郭 | ADR-004 评审本红线 + 全员落地路径 | 6/19 (W1 末) | 在跑 |
| 老唐 | audit BLAKE3 加 4 时间戳到 payload | 6/19 (W1 末) | 在跑 |
| 小米 | 文档审核加"数据采集时间是否明示" CI grep | 6/19 (W1 末) | 在跑 |
| 老高 | clang-tidy 加时间戳缺失检查规则 | 6/19 (W1 末) | 在跑 |
| 老韩 | RM v0.3 §audit schema 加 4 ts 字段 + assert | 6/26 (W2) | 待跑 |
| 老孙 | Signer v5 IPC schema 加 4 ts 字段 | 6/26 (W2) | 待跑 |
| 老李 | endpoint v3.1 标 ts 字段位置 | 6/26 (W2) | 待跑 |
| 小余 | ETL 每 record 加 ingestion_ts + 4 ts 不等式 | 6/26 (W2) | 待跑 |
| 小蒋 | paper engine + backtest PIT CI grep job | 6/26 (W2) | 待跑 |
| 小邓 | ML shadow 加 model_id + feature_snapshot_id + inference_ts | 7/3 (W3) | 待跑 |
| 小段 | Goalserve v3 标 endpoint ts 字段位置 | 已在跑 | Agreed (并入 v3) |

---

## 6. 关键风险变动 (R-21 ~ R-30, 上链 deferred 后)

| Risk | 原状态 | 新状态 | 变更原因 |
|---|---|---|---|
| R-01 跨洋链路抖动 → RM HALT | H×H = 20 | M×H = 12 | 上链 deferred 后只伤 paper, 不伤实盘资金; 实测仍是 P0 |
| R-04 链上 nonce 不一致 | H×M = 15 | **deferred 归档** | 不上链就没有真 nonce; mock nonce 设计保留 |
| R-21 paper engine 联调时间不够 | H×H | M×H | 小蒋 skeleton 提前到 W1 交付, 闸 1 落地 |
| R-26 Sygnum onboarding 失败 | L×H | **撤销归档** | Sygnum 承诺 Superseded |
| R-30 美国 entity Escalated | L×M | **撤销归档** | 撤地域合规, 未来迁主体时重审 |
| R-31 (新) R-20 PIT 违例 | — | M×H | 11 owner 派单, W2 评审 |

详见 `docs/RESEARCH/laohu-risk-registry-v2.md` (本周同步出).

---

## 7. 给老雷 3 个决策点

1. **R-20 评审优先级**: 老郭 W1 末 (6/19) 出 ADR-004, 是否需要老雷会签? 我建议**会签**, 这是 11 owner 横向工程, GM 站台落地快.
2. **paper engine 联调时间窗**: 小蒋 skeleton 提前 + 老孙 v5 PaperSigner mock 提前 → **9/12 paper trading 首判窗口可启动**, 比 OKR M4.5 (10/29) 提前 ~7 周. 这是 "实盘能不能赚钱" 数字提前出来, 但**也提前暴露失败风险**. 老雷是否同意"提前进 paper, 不提前推 M5 实盘"?
3. **HC-01/02 招聘**: 小林 W2 (6/20) 发 JD, 6/30 入职目标. 撤地域后岗位描述可放宽 (不再硬性要"非美总部经验"), 候选池预计宽 30%, 但截止日不变.

---

## 8. W2 (6/22 - 6/26) 重点

详见 `docs/SPRINTS/sprint-02.md` §1.6 W2 派单 (本周一并 update).

3 件大事:
1. 老郭 ADR-004 (R-20) 评审 + ADR-003 (v0.4) 评审
2. 老吴 AWS us-east-1 实开 (单 region, 不再跨 vendor)
3. 老王 WAL 骨架 C++ 代码 + 小宋 测试 framework 启动

---

## 9. 老胡附言

W1 这一波是用户 4 项指令的红利. 老雷你看 13 项里有 5 项是"用户撤地域 + 上链 deferred"直接拍下来的简化重构 (老孙 v5 / 老沈 v2 / 老黄 v2 / 老叶归档 / Sygnum 撤), 不是工程师赶工出来的. **这种红利后面不会再有了** — Sprint-2 W2 起就是真刀真枪的代码 + 联调, 我会把节奏拉回正常. 不耻下问 timeline 我已经问过 @小蒋 (paper engine 联调 8/14 deadline 可保) 和 @老周 (架构 v0.4 评审通过后才能起 RM C++); HC 我问过 @小林 (JD 6/20 发, 候选池 5/岗概率 60%, 不达标我升级).

— 老胡, 2026-05-28
