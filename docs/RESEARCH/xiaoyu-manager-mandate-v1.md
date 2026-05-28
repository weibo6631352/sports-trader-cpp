# 主管就职宣言 — 小余 (D 数据基础设施部)

- **Owner:** 小余 (data-etl, E-022, Manager)
- **Date:** 2026-05-28
- **last_review:** 2026-05-28
- **职位:** D 战斗单元主管 (ADR-005, 2026-05-28)
- **顶层引用:** ADR-005 部门主管 mandate, CLAUDE.md §4/§7/§8, HR pulse 2026-05-28 (我自评 7/10 黄)
- **会签:** 老雷 (GM 拍板) / 小林 (HR) / 老周 (A 接口) / 小梁 (C 接口) / 老韩 (B 接口)
- **状态:** v1 草稿, W4 EOW (2026-06-06) deadline 内交付

> 小余按: 我接 ADR-005 部门主管职责. HR pulse 给我 7/10 黄, 自评 7/10 黄, 不替自己挑染. 本 v1 三件事 — ① 接 D 单元 5 人, ② 把 W5 backlog 自己拆出来 (不让 GM 拆), ③ 上呈 HR 一个 HC-06 (ml-data-engineer) 跨边界缺口申请. 不亲力亲为是硬约束, 我不再自己写 spec 抢 IC 活, 改写 review + 派单.

---

## §1 单元成员清单 (5 人含我)

对齐 employee-registry.md `D. 数据基础设施部`:

| 工号 | 姓名 | persona | 角色 | 兼任 | 状态 |
|---|---|---|---|---|---|
| **E-022** | **小余** | `22-data-etl.md` | **Manager (主管)** | — | Active (ETL 主权 + 主管) |
| E-023 | 小董 | `23-data-stats.md` | Senior IC (统计推断) | — | Active |
| E-008 (=E-024) | 小田 | `08-sports-market-expert.md` (兼 `24-data-warehouse.md`) | IC (DWH) | 兼 A | Active |
| E-037 | 小段 | `37-goalserve-api-watch.md` | Senior IC (Goalserve 专精) | — | Active |
| E-034 | 小冯 | `34-api-watch-general.md` | IC (PM API watch) | — | Active |

**关系图:**
```
         小余 (主管, ETL 主权)
        /     |       \
   小段      小冯      小田 (兼 A, DWH)
  (Goalserve) (PM)
              |
             小董 (跨横切: stats 验证, 给 B/C 用)
```

**对外接口承担人:**
- 与 A (老周): 小田 (兼 A) + 我
- 与 B (老韩): 小董 (audit / RM fail 计数) + 我
- 与 C (小梁): 小董 (signal stats) + 我
- 与 F (小邓 ML): 小田 (Parquet schema) + 我
- 与 E (老胡): 我 (Sprint 节奏)

---

## §2 我作为主管要做的 vs 不做的

### 2.1 8 项 do (ADR-005 §2.2 + D 单元定制)

1. **接 GM 数据目标拆 D backlog:** GM 下 "数据要这个" 我自己拆派到 5 个 IC, GM 不指定 "谁做什么". 24h ack / 48h 拆完.
2. **D 内排队 + 优先级:** 每 wave 末出 D-backlog v(N), 标 P0/P1/P2 + owner + deadline. 跨 IC 排队我定.
3. **数据 schema first review (主权落锤):** 任何 schema (Goalserve / PM / Pinnacle / 内部 feature store) 上线前我 first review, schema 主权落到 D, 不让 C / F 越权改 schema.
4. **R-20 4 时间戳契约 enforce:** 每条数据流上线前自查 `event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts` 全携带, 字段位置入 schema, CI grep 反模式由小田维护.
5. **跨主管 24h ack 硬约束:** 老周 / 老韩 / 小梁 / 老胡 派数据需求 24h ack, 拆完 48h 内回派 IC + deadline.
6. **D 内 weekly 1:1:** 5 个 IC (含小田跨 A 那部分) 每周 1:1 30min, 摘要进主管周同步.
7. **HR pulse 联动:** 与小林 W6 / W9 联动 pulse, 主动报 IC 工作量 + blocker.
8. **数据质量门禁:** 任何 D 产出 (clean dataset / schema / pipeline) 上线必经我 + 老高 (PR review v1.1) 双签, 触红线 → 升老郭.

### 2.2 5 项 don't (ADR-005 §2.3 + 我自己警戒)

1. **don't 写代码** (例外: < 2h hotfix). Sprint-1 我没出独立 v1 spec 一部分原因是想自己写, 主管不能再这么干. ETL 代码归小段 / 小冯 / 小田; stats 代码归小董.
2. **don't 替老雷拍战略** (Pinnacle 路径 A/B/C 是 GM + CPO 拍, 我执行).
3. **don't 一票否决其他主管产出** (例外: 触 D 红线 → 升老雷 / 老郭).
4. **don't 单方面承诺其他 IC 工作量** (我可以承诺 D 内 IC, 跨主管协商).
5. **don't 越主管直接派 IC** (GM 错 #8 5 题自检第 5 题, 我也要遵守 — 老周单元的 IC 我不直接派, 走老周).

---

## §3 D 内 IC 当前工作量评估 (0-10)

> 小余自评 + HR pulse 2026-05-28 对齐. 个人评分以稳健为主, 写明可见证据.

### 3.1 小董 E-023 — 8/10 黄+

**Sprint-1 + W3-W4 交付:**
- `xiaodong-stats-validation-framework-v1.md` (653 行) — Sharpe / VaR / 假设检验口径
- `xiaodong-m45-gate-framework-v1.md` (262 行) — 7 hard gate framework v1
- `include/stcpp/stats/gate_evaluator.hpp` + `src/stcpp/stats/gate_evaluator.cpp` + **28 单测全过**
- 5 GM 决议建议 (Sprint-1 retro 真发言)

**信号:** 产出节奏快, 4 评委会签 (小蒋 / 老韩 / 老吴 / 小梁), W6 P0-01 回测出来后他立刻是统计验收 critical path.

**给他的 W5:** PnL 检验 + RM fail 计数 + paper vs random baseline (G7) 接 paper engine 数据契约 (小蒋), 不加新任务. 8/10 不再压.

### 3.2 小田 E-008/E-024 — 6/10 绿 (兼 D 部分 5/10)

**Sprint-1 + W3-W4 交付:**
- 兼 A 单元 sports-market-expert 主项 (老周分配)
- D 侧 DWH Parquet + DuckDB **待 W5+ 启动** (Pinnacle 路径定后才能落 schema)

**信号:** 双单元身份导致 D 侧 W3-W4 慢, 不是他懒, 是路径未定. W5 起 DWH 是 D 主线.

**给他的 W5:** Parquet 文件分区策略 + DuckDB schema v0.1, 接小邓 ML hook (跨 F 单元接口需求, 我配合).

### 3.3 小段 E-037 — 7/10 黄

**Sprint-1 + W3-W4 交付:**
- `xiaoduan-goalserve-api-spec-v1.md` (390 行) + endpoint matrix v2 + odds by sport v2.1 + 官方 doc v3
- Goalserve C++ client v0.1 (**1121 行 + 28 测试, 用户题面给出**)
- ADR-2026-05-28-gm-decision-goalserve-odds-gap 配合落地
- 跨洋 latency 实测数据 `latency-runs-20260528.csv`

**信号:** 是 D 单元产出最实的 IC, HR pulse 提 "小段被压实". 我接主管后必须分流, 不能再把 W5 三件 (Pinnacle CSV + WSS subscriber + schema 锁) 全压他.

**给他的 W5:** Goalserve schema 锁 (W5 主线) + 协助小冯 Pinnacle CSV ingestion (兼活, 20% 时间 cap), 不再加单. 工作量降到 6/10.

### 3.4 小冯 E-034 — 5/10 绿 → W5 升 7/10

**Sprint-1 + W3-W4 交付:**
- Polymarket api-watch 待命, W3-W4 实际产出偏轻 (W1-W2 在等 Pinnacle 路径决议)
- 协助 `data-contract-v1.md` §3.1-3.2 (PM book / trade tick) 字段对齐

**信号:** W5 PM WSS subscriber 启动后他成 critical path, 现在低载是因为前置 unblock.

**给他的 W5:** **PM WSS subscriber 即将启动** (W5 主线), 接 sub-sec book / trade tick 落地到 ingestion 层, R-20 4 ts 字段位置自查.

### 3.5 我自己 E-022 (主管) — 自评 7/10 黄

**Sprint-1 W3 8 必做项现状 (HR pulse 给我 7/10 黄 + 我自评对齐):**

| # | 必做项 | 现状 | 自评 |
|---|---|---|---|
| 1 | D 单元独立 v1 spec | **未出** | **黄** — Sprint-1 没出独立 v1 spec, 这是我最大的扣分项 |
| 2 | Goalserve schema 协同 (小段) | 部分协同 | 黄 — 小段顶住, 我没收口 |
| 3 | PM API schema 协同 (小冯) | W5 启动 | 黄 — 等 W5 |
| 4 | DWH Parquet/DuckDB 待 (小田) | 待 W5+ | 黄 — Pinnacle 路径未定我没法 force |
| 5 | data-contract-v1.md 供给方签字 | 待 6/12 联签 | 黄 — 我没主动 push |
| 6 | R-20 4 ts CI grep 反模式 | 雏形 | 黄 — 需小田落地 |
| 7 | 数据 quality dashboard | 未起 | 红 — Sprint-2 必出 |
| 8 | D 单元 OKR Q2 写明 | 未独立 | 黄 — 跟着 OKR 总 |

**总评 7/10 黄: HR pulse "小余自己 spec 产出节奏慢于其他 owner, 小段被压实" 我接受, ADR-005 主管职责后我**不再自己写 spec 抢 IC 活**, 改 review + 派单, 把节奏拨回来.

---

## §4 W5 派单 backlog v1 (主管自己拆)

> ADR-005 §3 GM → 主管 → IC 3 层流程. 以下 W5 派单我自己拆, GM 不指定 "谁做什么".

| # | 任务 | Owner | 协作 | 类型 | Deadline | 产出 |
|---|---|---|---|---|---|---|
| **D-W5-01** | Pinnacle CSV ingestion (老彭 CSV 路径 C 兜底) | 小段 (主) | 老彭 (CSV 供给) | ingest pipeline | W5 EOW | `src/stcpp/data/pinnacle_csv_loader.{hpp,cpp}` + 单测 + R-20 4 ts 字段 |
| **D-W5-02** | PM WSS subscriber 启动 | 小冯 (主) | 老李 (PM protocol) | network ingest | W5 EOW | `src/stcpp/data/pm_wss_subscriber.{hpp,cpp}` + book/trade tick 落地 + R-20 4 ts |
| **D-W5-03** | Goalserve schema 锁 (v1 final) | 小段 (主) | 我 review + 小邓 ML 反馈 | schema 锁定 | W5 mid | `src/stcpp/data/goalserve_schema.hpp` (字段名 + 类型 + null 策略 + 4 ts) + CI grep |
| **D-W5-04** | Parquet 序列化 + 分区策略 v0.1 | 小田 (主) | 小邓 (ML 消费) | DWH spec | W5 EOW | `docs/RESEARCH/xiaotian-parquet-partition-v0.1.md` + 原型 |
| **D-W5-05** | data-contract 落 `src/stcpp/data/` C++ 头文件 | 小冯 (主) + 小段 + 小田 | 小邓 review | code-as-contract | W5 EOW | `include/stcpp/data/contract_v1.hpp` (4 ts struct + 字段命名常量 + null sentinel) |
| **D-W5-06** | R-20 4 ts CI grep 反模式落地 | 小田 (主) | 老郭 review | CI guard | W5 EOW | `tools/ci-checks/check-4ts.sh` + 接 `now()` 替代上游 ts 的 grep 反模式 |
| **D-W5-07** | 数据 quality dashboard 雏形 | 小董 (主) | 我 + 老吴 (observability) | obs hook | W5 EOW | gate evaluator → quality metric pipe, JSON dump 到 obs |
| **D-W5-08** | 三源 market mapping CSV (C-15) 维护 | 我 (主, owner_supply) | 小段 + 小冯 + 老李 | mapping CSV | W5 mid | `docs/RESEARCH/data/market-mapping/v0.1.csv` (PM ↔ Goalserve ↔ Pinnacle) |

**优先级:** D-W5-01/02/03/05 是 P0 (Pinnacle + PM WSS + schema 锁是 W6 回测 critical path); D-W5-04/06/07/08 是 P1.

**自检 (ADR-005 GM 派单 5 题 — 主管版):**
- D-W5-01 / 02 / 04 没让 1 个 IC 替全员说话 ✓
- 没让 IC 看老项目 / 撤销方案 ✓
- 不越 persona 拒绝任务边界: 小段 / 小冯 / 小田 拒绝任务里都没 "schema 落地" 这类活, ok ✓
- D-W5-08 我自己接 (主管接活例外: 跨多 IC 协调 + 数据 owner 拍板, 不算违反 don't #1 主管不写代码 — 这是 spec 维护非代码)

---

## §5 跨单元接口需求 (24h ack 期望)

> ADR-005 §4.1: 主管间 24h ack, 48h 协商不下升老雷.

| 对端主管 / IC | 需求方向 | 内容 | 24h ack? | 升级阈值 |
|---|---|---|---|---|
| **老周 (A)** | A → D | WAL framework (老王 v0.2) 给 D 用 — ML data + Goalserve data 走 WAL 而不是自建 buffer | 期望 24h | 48h 不下 → 升老雷 |
| **老周 (A)** | D → A | 小田跨单元 W5+ DWH 主线启动, 申请小田 50% 时间分给 D (W3-W4 实际 20%, 不够) | 期望 24h | 48h 不下 → 升老雷 |
| **小梁 (C)** | C ↔ D | Pinnacle CSV schema 协商 (D-W5-01) + signal 数据消费 schema (老彭 CSV 字段 vs 小程 signal F-05/F-06 需要) | 期望 24h | 48h 不下 → 升老雷 |
| **老韩 (B)** | B → D | 数据时序 R-20 4 ts 字段位置 enforce — 老韩 / 老唐 audit_event 要 4 ts, D 必须保证字段顺序 + null 策略一致 | 期望 24h | 48h 不下 → 升老郭 (red-line 触发) |
| **老胡 (E)** | E → D | integration test data fixture — Sprint-2 e2e 测试要 D 提供 fixture (Goalserve + PM + Pinnacle 各一份 1h 窗口) | 期望 48h (非 P0) | 1 周不下 → 升老胡 |
| **小邓 (F ML)** | F → D | ML hook 接 Parquet schema (D-W5-04) — Parquet feature 字段命名 / 类型 / PIT 严格性必须满足 ML 训练 | 期望 24h | 48h 不下 → 升老雷 + 老周联签 |

**当前已知阻塞:**
1. 老周 WAL framework 给 D 用的对接还没正式协商, W5 周一我主动 ping 老周
2. 小田 50% 时间分给 D 需老周 ack, 否则 D-W5-04 / 06 延期
3. Pinnacle 路径 A/B/C 仍未拍板, D-W5-01 走兜底路径 C (老彭 CSV), 这是临时方案, 等 GM + CPO 拍板后我们再调整

---

## §6 主管 KPI 自评 (基于 ADR-005 §2.2 主管职责 6 条)

| 主管职责 | 目标 | W4 现状 | 自评 |
|---|---|---|---|
| 1. 接 GM 目标拆任务 | 24h ack / 48h 拆完 | W5 backlog 已自拆 (§4) | **达标** |
| 2. 单元内排队 + 优先级 | 每 wave 出 backlog vN | D-backlog v1 (本文件 §4) | **达标** |
| 3. review + 质量门禁 | 数据 schema 主管 first review | W5 起执行 | 跟踪中 |
| 4. 跨单元接口对接 | 24h ack | §5 矩阵 + 主动 ping 老周 W5 周一 | 跟踪中 |
| 5. KPI + 1:1 + 培养 | weekly 1:1, 月度 KPI | W5 周一启动 D 内 1:1 | **未启动 — 必须 W5 落地** |
| 6. 不亲力亲为 | 不写代码 (例外 hotfix < 2h) | Sprint-1 我自己抢 spec 是问题 | **告警 — 明确不再抢 IC 活** |

**自评总分:** 6/10 红 (Sprint-1 没出独立 v1 spec + 没启动 1:1) → 自评 **5.5/10**, HR pulse 7/10 黄, 我比 HR 还严. 提升计划:
- W5 EOW: 1:1 启动 + W5 backlog 全 ack
- W6 EOW: D-W5 任务完成率 ≥ 80% + 至少 2 个跨主管接口落地
- W8 EOW: 自评升到 7/10 黄 (= HR pulse), 不能更低
- W12 (Sprint-2 末): 自评升到 8/10 黄+

---

## §7 W5 GM 派单约定

> ADR-005 §3.2 GM 例外可直接派的场景之外, W5 起 GM 派 D 单元任务必经我.

**约定:**

1. **GM 派 D 任务格式:** "业务目标 + 截止 + 约束", 不指定 "谁做什么". 例如 "W5 末 Pinnacle 路径 C 跑通 + R-20 4 ts 全携带", 不要 "派小段做 Pinnacle".
2. **24h ack 硬约束:** GM 派单后我 24h 内 ack, 48h 内拆 D-backlog 回派 + 抄送 GM.
3. **例外可直接派的:**
   - 紧急 P0 (数据 red-line 触发, RM HALTED) — GM 可直接 ping 任何 IC, 同时抄我
   - 我请假 / 不响应 (24h+) — GM 可走 owner 备份: 小董 (代理主管, 因小董 8/10 工作量上限)
4. **GM 错 #8 5 题自检对 D:** 任何派单触 D 都要过 5 题, 第 5 题 "越主管直接派 IC?" 是硬约束.
5. **W5 GM 不指定 backlog:** 本 §4 D-W5 backlog 我自拆, GM ack 即可, 不再下指令.

---

## §8 HR 扩招建议 (上呈小林 + 老雷)

### 8.1 HC-06 ml-data-engineer 申请 (新 HC)

> **注意编号冲突:** 现 `docs/HIRING/backlog.md` HC-06 = devops-infra-engineer (HR pulse 2026-05-28 已建议延后到 Q4). 用户题面 "HC-06 ml-data-engineer" 与现 backlog 编号冲突. 我按用户题面提**新 HC 申请**, 编号最终由小林统一重排.

**职位:** ml-data-engineer (跨 D + F 边界, 专职 ML data + Parquet pipeline)

**申请理由 (3 条):**

1. **小邓 ML hook + Parquet pipeline 跨边界:** 小邓在 F 顾问团 (ML 训练 + ONNX 导出), 小田在 D (兼 A) 做 Parquet, 中间 "ML data pipeline" (Parquet schema → 训练样本 → PIT 严格) 是无主地带. 现状是小邓自己写一截, 小田自己写一截, 接口靠 ad-hoc 协调, 跨 wave 易翻车.
2. **D 单元数据量 ramp 后 ETL + ML pipeline 需专人:** Sprint-2 起 Pinnacle + PM WSS + Goalserve 三源数据进 DWH, ML 训练 cron 起来后, Parquet pipeline (写入 + 分区 + PIT replay) 工作量爆增, 小田兼 A 顶不住, 小邓 ML 训练顶不住跨边界.
3. **R-20 4 ts 在 ML pipeline 上的 enforce 需专人:** ML 训练用历史数据 PIT 严格, 4 ts 任一字段错乱 = 训练数据穿越未来, 模型废. 这块需要懂 ETL + 懂 ML 的人 review, 现 D / F 都没有.

**职责草案:**
- Parquet pipeline (写入 + 分区 + compaction + 读 replay)
- ML feature store (in-process + history) 接 Parquet
- PIT 严格性 review (训练 / 回测 / 推理三层共享 binary 时数据流走向)
- R-20 4 ts 在 Parquet 字段位置 enforce + ML pipeline 测试用 fixture
- 与小邓协作 ONNX 训练数据导出, 与小田协作 DWH

**优先级:** P1 (W6 起 Pinnacle 跑通 + ML 训练启动后即缺人; 不招会拖 M4.5 gate)

**入职日期:** **建议 Q3 (8 月)** — 与小吕 quant-engineer 提前到 8/1 配套, ML 训练才能启动

**评委联签:** 老周 (A, 跨 A 接口) + 我 (D, 主管) + 小邓 (F, 直接合作 IC). 老雷 + 小林文化面.

**JD 起草:** 申请小林 W5 EOW 前给 JD 模板, 我 + 小邓 W5 末交 JD v1, W6 发布.

### 8.2 HC-09 data-engineer (现 backlog HC-05) 启动时机

> 现 `docs/HIRING/backlog.md` HC-05 = data-engineer P1 Q3 2026, HR pulse 给我 1:1 后再评 staff vs mid 级别 (`HR-W4-04` 6/3 deadline).

**我的建议 (待 6/3 1:1 与小林落实):**

1. **级别建议:** **mid-level** (不是 staff). 理由: D 单元数据架构主权落我 + 小田 (DWH) + 小段 (Goalserve) + 小冯 (PM) 已覆盖架构层, 缺的是 "执行层" — pipeline 实现 + dataset 维护 + CI guard 落地. mid 即够.
2. **入职时机:**
   - 若 HC-06 ml-data-engineer (本 §8.1 申请) 批准 Q3 8 月入职 → HC-05 data-engineer 延后到 **Q3 末 9 月底 / Q4 初 10 月** (避免同月入职两人吸老周 + 我 onboarding 带宽)
   - 若 HC-06 ml-data-engineer 不批 → HC-05 data-engineer **提前到 Q3 8 月** (顶 ML pipeline 缺口, 但 mid-level 顶 ML 边界有风险, 不优选)
3. **启动条件 (P2 → P1 触发):**
   - Sprint-2 末 D 单元 IC 工作量 > 8/10 (小段 / 小冯 / 小田 任一)
   - 或 D-backlog P0 任务积压超过 5 项
   - 或 Pinnacle 路径 A (官方 API) 批准 → ingestion 量爆增

**默认:** 维持 Q3 P1, 6/3 与小林 1:1 后定级别 + 入职日.

---

## §9 完成汇报 + HC-06 申请正式提交

### 9.1 完成汇报 (上呈老雷 + 小林)

1. **主管就职宣言交付:** 本文件 v1, ≤ 300 行, ADR-005 §5 6 段全覆盖 + §7/§8 用户题面补充 2 段.
2. **D 单元 5 人清单 + 工作量自评:** 小董 8/10 黄+ / 小田 6/10 绿 (D 侧 5/10) / 小段 7/10 黄 / 小冯 5/10 (W5 升 7/10) / 我 7/10 黄 (自评 5.5/10 红).
3. **W5 D-backlog v1:** 8 任务自拆 (§4), GM 不再下指令.
4. **跨单元接口需求:** 5 接口 24h ack 期望 (§5), W5 周一主动 ping 老周.
5. **主管 KPI 自评:** 5.5/10 红, W5/W6/W8/W12 提升路径写明.
6. **GM W5 派单约定:** §7, 5 题自检对 D enforce.

### 9.2 HC-06 ml-data-engineer 申请正式提交

**收件人:** 小林 (HR) + 老雷 (GM)
**抄送:** 老周 (A 主管) + 小邓 (F ML)

**申请条目:**
- 新 HC 编号待小林重排 (现 backlog HC-06 是 devops, 用户题面 HC-06 是 ml-data, 冲突待统一)
- 职位: ml-data-engineer
- 单元归属: D (主) + F (虚线接小邓)
- 优先级: P1
- 入职建议: Q3 8 月 (与 HC-03 小吕配套)
- 联签人: 老周 + 小余 + 小邓 (三方会签)
- 文化面: 老雷 + 小林
- JD 起草: 小余 + 小邓, W5 末交 v1
- 备份方案 (不批): HC-05 data-engineer 提前 Q3 8 月入职 mid-level 兜底, 但有跨 ML 边界风险

**HC-09 (现 HC-05) 启动:** 维持 Q3 P1, 6/3 小林 1:1 后定级别 + 入职日 + 是否前后调整.

---

**最后更新:** 2026-05-28 by 小余 (D 主管 v1 首发)
**下次更新:** 2026-06-06 (W4 EOW, 待 GM ack + HR review 后定稿)
