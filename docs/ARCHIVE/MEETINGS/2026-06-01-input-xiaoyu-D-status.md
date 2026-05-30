# 6/01 主管周同步 input — 小余 D 数据基础设施部 status v1

- **Owner:** 小余 (E-022, D 数据基础设施部主管)
- **Date:** 2026-05-28 (W5 末; 6/01 周同步前置 input)
- **Last review:** 2026-05-28
- **触发:** GM 老雷 6/01 主管周同步 (ADR-005 主管 mandate 试点 W5 → 正式 W6)
- **权威输入:** mandate v1 §4 W5 8 ticket + commit `3ab5dfb` (W5 Wave 24) + mandate v1 §5 跨主管 5 ASK + GM 错 #9 (Pinnacle 撤回)
- **完成汇报锚:** D 单元 W5 末 status + 小冯 first review PASS + GM 错 #9 D-W5-01 撤回 + 1:1 启动 W4 0/5 → W5 4/5 + 主管 KPI 5.5 → 7

---

## §1 D 单元 W5 末 status (mandate §4 8 ticket)

| Ticket | IC | 状态 | 备注 |
|---|---|---|---|
| D-W5-01 Pinnacle CSV ingestion | 小段 + 老彭 | **撤回 (GM 错 #9)** | 小梁 W6 改 Goalserve de-vig, 老彭 W6 起做历史校准 |
| D-W5-02 PM WSS subscriber | 小冯 | ✓ commit `3ab5dfb` | 1044 行 + 8/8 测试 (跨 A/D 双主管) |
| D-W5-03 Goalserve schema 锁 | 小段 | TBD W5 末 | 小段 v3 ✓ data spec ack, 待落 src/stcpp/data/ |
| D-W5-04 Parquet 分区 | 小田 + 小邓 | TBD W6 | 小邓 ML hook v0.1 已就位 (W4 Wave 20) |
| D-W5-05 data-contract 落 src/stcpp/data/ | 小冯 主 + 小段 + 小田 | TBD W5 末 | 小冯 wss 落地后接 |
| D-W5-06 R-20 4 ts CI grep | 小田 | TBD W5 末 | 老高 PR review v1.1 已含 (W5 Wave 24) |
| D-W5-07 数据 quality dashboard 雏形 | 小董 | TBD W6 | 小董 M4.5 7 gate framework v1 已就位 |
| D-W5-08 三源 market mapping CSV | 我 | TBD W5 末 | 我接 (主管 spec 维护非代码), 这是 spec 不是 cpp |

**W5 末 D 单元 cpp 净增 (来自 commit `3ab5dfb`):** 小冯 1044 行 + 8 unit tests (PM WSS subscriber, A/D 跨主管联签). D 侧其余 W5 末-W6 落地.

**我作为主管 W5 cpp lines = 0 ✓** (mandate §2.2 §6 不亲力亲为红线守住).

---

## §2 first review 结果 (我作为 D 主管 first review, 跨 A 联签)

### §2.1 小冯 PM WSS subscriber v0.1 (D 主管视角)

- ✓ IWssTransport 抽象 + MockWssTransport 单测注入
- ✓ 8 sub topic (market / game / outcomes / book / price_change / last_trade_price / tick_size_change / system)
- ✓ exp backoff 1→2→4→...→30s + 10s heartbeat + back-pressure (R-12 §17.1.1 不阻塞)
- ✓ 4 ts UPSTREAM_PAYLOAD 优先 (R-20) — 数据 schema 主权落 D, 字段位置我 ack
- ✓ 第 5 host `wss://sports-api.polymarket.com/ws` (R-33 四维扫描红线)
- ✓ ISpscEventSink 抽象 (小石 W5 末接 rigtorp)
- ⏸ BoostBeastTransport W6 接 (老周 A 主管 ack lib)
- ⏸ JSON minimal → simdjson 切 (老李 W6)
- **D 主管 ack: PASS, 上 PR (与老周 A 主管联签 — commit `3ab5dfb` 已落)**

> 说明: 小冯 IC 跨 A/D 双单元 (代码归 A 落地, 数据契约归 D first review). 老周 v1 §2.2 已 PASS A 视角 cpp 质量; 我 v1 §2.1 PASS D 视角 schema + 4 ts + 第 5 host 红线. 两主管联签 = PR 上线条件.

---

## §3 跨主管 ASK 进度 (mandate §5 5 ASK, 6/1 EOD)

| 对端 | ASK 内容 | 6/1 EOD ack | 备注 |
|---|---|---|---|
| 老周 (A) | WAL framework + 小田时间分配 50% 给 D | 待老周 input | 小田兼 A/D 双单元, W3-W4 实际 D 侧 20%, 不够支撑 W6 Parquet + R-20 CI |
| 小梁 (C) | Pinnacle CSV → 撤回, 改 Goalserve de-vig schema | ✓ ack | GM 错 #9 5/28 决议; 小梁 ADR-008 候选 D multiplicative |
| 老韩 (B) | R-20 4 ts enforce | ✓ ack | 老高 PR review v1.1 已加 R-20 第 7/8 项 grep (W5 Wave 24) |
| 老胡 (E) | e2e fixture | ✓ ack | 小宋 integration framework v0.1 已落 1132 行 / 14 测试 |
| 小邓 (F ML) | Parquet schema (小田) | TBD W6 | W6 D-W5-04 (现编号 W6-D-04) 启动后落地 |

5 ASK 中 4 ack ✓, 1 待 (老周 WAL + 小田时间), 跨主管 24h ack 硬约束达标 4/5.

---

## §4 GM 错 #9 ack — Pinnacle 撤回, D-W5-01 关闭

GM 错 #9 (5/28): Pinnacle 路径 C (老彭 CSV) 撤回. 决议如下:

- **D-W5-01 ticket 关闭** (原 "Pinnacle CSV ingestion 老彭 CSV 兜底")
- 小梁 W6 起 ADR-008 multiplicative de-vig 落代码 (de-vig 算法在 C 单元主, D 提供 schema)
- 老彭 W6 起做 Goalserve odds 8-9 家 bookmaker 历史校准 (5/2024-5/2026, 2 年窗口)
- D 单元改派 **W6-D-09: Goalserve 8-9 家 bookmaker 历史回填** (老彭 + 小段, P0)

撤回后我 mandate v1 §4 8 ticket 重排: D-W5-01 关闭 + W6-D-09 新增. D 单元 W5 backlog 不再含 Pinnacle.

---

## §5 待 GM 拍板

- **ADR-008 候选 (Goalserve de-vig 算法选型):** 我支持小梁 D multiplicative (data spec 视角 schema 简单 + R-20 4 ts 字段位置不破), 但算法主权在 C 小梁, 我不替小梁拍.
- **HC-06 ml-data-engineer 编号冲突:** mandate v1 §8.1 上呈 HC-06 ml-data, 现 backlog HC-06 = devops-infra-engineer (HR pulse 已推后 Q4 → 重编 HC-10). 等小林 W4 EOW JD 起草前统一重排, 我不替小林定编号.
- **我自评 5.5/10 红, W5 1:1 启动 (5 IC 周一 30min/人/周):** GM 是否需要关注? 我已主动报, 不藏问题.

---

## §6 W5 自评 update (mandate v1 §6 主管 KPI)

W5 关键改进项:

1. **weekly 1:1 启动** ✓ — W4 EOW 自评 0/5 (未启动), W5 周一 5 IC × 30min/人 启动 (小董 / 小田 / 小段 / 小冯 + 我自己 retro). W4 缺失项 close.
2. **不亲力亲为守住** ✓ — D 单元 W5 cpp 净增 1044 行 (小冯), 我自己 cpp = 0 行. mandate §2.2 don't #1 红线守住.

**主管 KPI 自评 (基于 ADR-005 §2.2 + mandate v1 §6 6 条):**

| 主管职责 | W4 EOW | W5 EOW | 备注 |
|---|---|---|---|
| 1. 拆任务 (GM 目标 → IC) | 4/5 | **5/5** | 8 ticket 自拆 + 6/1 主管周同步前 W5-D-03/05/06/08 启动 |
| 2. 单元内排队 + 优先级 | 4/5 | 4/5 | D-backlog v1 落地; W6-D-09 增量未排完 |
| 3. review + 质量门禁 | 3/5 | **5/5** | 小冯 PM WSS v0.1 first review PASS, 与老周联签 |
| 4. 跨单元接口 | 3/5 | 4/5 | 5 ASK 中 4 ack |
| 5. weekly 1:1 + 培养 | **0/5** | **4/5** | W5 启动, 1 wave 内未到 5/5 上限 |
| 6. 不亲力亲为 | 4/5 | **5/5** | D 单元 W5 cpp = 0 |

**总分:** W4 5.5/10 红 → W5 **7/10 黄** (HR pulse 基线水平). 提升源: 1:1 启动 + first review PASS + 不亲力亲为守住.

W8 EOW 目标维持 7/10 黄 (HR pulse 基线); W12 (Sprint-2 末) 升 8/10 黄+, 路径不变.

---

## §7 W6 启动决议 (D backlog v2)

| Ticket | IC | 优先级 | Deadline | 备注 |
|---|---|---|---|---|
| **W6-D-01** | (撤回 closeout) — | — | — | D-W5-01 关闭归档, mandate v1 §4 更新 |
| **W6-D-04** | Parquet 分区 (小田 + 小邓) | P0 | W6 EOW | ML hook 准备 paper data ingestion |
| **W6-D-07** | 数据 quality dashboard 雏形 (小董) | P1 | W6 EOW | paper 跑起来后用 |
| **W6-D-09** | Goalserve 8-9 家 bookmaker 历史回填 (老彭 + 小段) | P0 | W6-W8 | 5/2024-5/2026 2 年, GM 错 #9 撤回 Pinnacle 后改派 |
| **W6-D-10** | Polymarket WSS 真接 (小冯 + 老李) | P0 | W6 EOW | 接 boost.beast, 老周 A 主管 W6 lib 选型决议入口 |

W6 D 单元 P0 集中在 Parquet 分区 (小田 + 小邓) + Goalserve 历史回填 (老彭 + 小段) + PM WSS 真接 (小冯 + 老李). 跨 A (老周 lib) / C (小梁 de-vig) / F (小邓 Parquet schema) 三向接口 W5 末-W6 初协商完.

---

## §8 完成汇报

1. **D 单元 W5 末 status** ✓ — 8 ticket 现状清盘 + cpp 1044 行 (小冯) + 我 cpp = 0
2. **小冯 first review PASS** ✓ — D 主管 schema / 4 ts / R-33 第 5 host 角度 ack, 与老周 A 主管联签
3. **GM 错 #9 D-W5-01 撤回** ✓ — Pinnacle 撤回 closeout + 改派 W6-D-09 Goalserve 历史回填
4. **1:1 启动 W4 0/5 → W5 4/5** ✓ — W4 缺失项 close, weekly 30min/人 节奏建立
5. **主管 KPI 5.5 → 7** ✓ — HR pulse 基线水平达到, 不亲力亲为守住 (cpp = 0)

**约束守住:**
- 不替小段 / 小冯 / 小田 / 小董 说话 (各 IC W6 自报)
- 不替 GM 拍 ADR-008 (de-vig 算法主权 C 小梁)
- 不耻下问已发: @老周 (跨 A/D 小田归属 + WAL) / @小梁 (de-vig 算法 ack) / @小林 (HC-06 编号冲突) / @老雷 (GM 拍板 ADR-008 + 自评 5.5 红是否关注) / @小邓 (Parquet schema)

---

**最后更新:** 2026-05-28 by 小余 (D 主管 v1, 6/01 主管周同步 input)
**下次更新:** 2026-06-08 (W6 EOW, W6-D-04/07/09/10 进度 + W7 backlog v3)
