# Sprint-N WeekN 周报模板 v2

- **Owner:** 老胡 (E-026, pm-project-manager, E 单元主管)
- **Date:** 2026-06-01 (W5 末 Wave 25)
- **触发:** GM 错 #9 (跨 wave 引用过时信息, Pinnacle 凭空决议) — R-41 永久 enforcement 4 条配套, ADR-005 §4.2 主管周同步首次落地
- **关联:**
  - `docs/INCIDENTS/gm-self-mistakes-log.md` 错 #9 永久 enforcement 第 3 条 ("老胡 PM 周报加 '本周 SSOT 版本演进' 段")
  - `docs/ADR/2026-05-28-department-manager-mandate.md` ADR-005 §4.2 主管周同步议程
  - `docs/MEETINGS/2026-06-01-manager-sync-w5-v1.md` 6/01 主管周同步 v1 决议 D-06 (周报模板升 v2)
  - `docs/RESEARCH/laohu-risk-registry-v2.2.md` → v2.3 (R-41 配套, 周报段落入风险登记)
- **生效:** 2026-06-01 起每周五 EOD PM 周报必套用 v2 模板
- **首次套用:** W5 五 周报 (Sprint-2 W5 progress, 2026-08-01 暂定)
- **变更摘要 (v1 → v2):** 加 §4 "本周 SSOT 版本演进" 段 (GM 错 #9 + R-41 永久 enforcement), 加 §5 主管派单 KPI 段 (ADR-005 §8 W5 试点 W6 硬约束追踪)

---

## 周报标题命名

`docs/SPRINTS/sprint-NN-wN-progress.md` (例: `sprint-02-w5-progress.md`)

---

## §1 本周完成

- 列本周交付的 ticket (Agreed / Compromised / Escalated 三态明示)
- 按主管 / 单元分组 (ADR-005 后, 不再按 owner 个人, 按 5 单元)
- 每条带 commit hash / 行数 / 测试数 / 红线 enforce 状态
- **格式示例:**
  ```
  ### A 系统工程部 (老周)
  - W5-A-01 老李 PolymarketClient v0.1 (1050 行, 28 unit test ✓, commit 3ab5dfb, R-7/R-11/R-20/R-33 enforce)
  - W5-A-02 小冯 PM WSS subscriber (1044 行, ✓, R-12 p99 3.9us)
  ...
  ```

---

## §2 本周阻塞

- blocker 升级链: IC → 主管 (4h ack) → 老胡协调 (48h) → 老雷 (P0 2h)
- 列每条 blocker 当前在哪层 + ack 状态 + ETA
- **格式示例:**
  ```
  - BL-W5-01: 老周 (A) 与小余 (D) 小田归属仲裁 — 6/2 EOD 升老雷拍板 (主管协商 48h 不下)
  - BL-W5-02: 老钱 PRD v2 cross-ref 待 6/1 EOD 老钱 dial-in 直答 (CPO 平级, 不走主管)
  ```

---

## §3 风险登记 update

- 引用 `docs/RESEARCH/laohu-risk-registry-vN.md` 最新版本号
- 列本周 update 的风险 (新增 / 升级 / 降级 / 关闭)
- Top 5 风险快照 (I × P)
- **格式示例:**
  ```
  v2.2 → v2.3: 新增 R-41 (跨 wave 引用过时信息, 12 = 4×3) + R-21 降级 9 → 6 (W5 e2e 跑通)
  Top 5 (v2.3): R-02 / R-06 / R-07 / R-09 / R-31
  ```

---

## §4 本周 SSOT 版本演进 (R-41 + GM 错 #9 配套, 永久 enforcement 第 3 条)

> **强制约束** (GM 错 #9 永久 enforcement 第 3 条): 数据源 owner (小段 / 老李 / 老彭 等) 每出 v(N+1) **必须**在本节点名"推翻了 vN 的 X 结论", 让 GM / CPO / 全主管不漏读最新版.

### §4.1 本周 vN → v(N+1) 推翻清单

| owner | 单元 | SSOT 文档 | vN | v(N+1) | 推翻结论 | 影响下游 (谁要重新对齐) |
|---|---|---|---|---|---|---|
| (示例) 小段 | D | xiaoduan-goalserve-official-doc | v2.1 | v3 | "Goalserve 单源就够 fair value 锚源, 不再需要单独接 Pinnacle / Betfair" (inplay JSON 含 bet365 value_eu + getodds?cat=<sport>_10 含 8-9 家 bookmaker) | 小梁 (C 接 de-vig 算法选型), 老彭 (Pinnacle CSV 工作量撤回 → Goalserve 顾问), 老胡 (撤回 Pinnacle 决议入会议纪要), 全 5 主管 |
| ... | ... | ... | ... | ... | ... | ... |

### §4.2 跨 wave 决议追溯 (本周 GM / 主管/ CPO 引用了哪些 vN, 是否最新)

- 列本周会议 / ADR / 派单 prompt 中引用的 SSOT 文档 + 版本号
- 标 "**最新**" 或 "**已被 v(N+1) 推翻**" 双态
- 若发现 GM / 主管引用过时 SSOT → 本节当周点名 + 当周升级至 GM ack

### §4.3 数据源 owner 版本变更 watch list

- 哪些 owner 在路上 (W+1 / W+2 即将出 v(N+1)) → 提前 watch
- 谁需要 GM / CPO 提前预审 → flag 在本节

### §4.4 GM 错 #9 enforcement 第 4 条 — Pinnacle 决议撤回追踪

> **首次落地 (W5 末 6/01 主管周同步 ADR-008 撤回):** Pinnacle 路径 A 官方 vs C 老彭手工 → **正式撤回**, 改为 Goalserve fair value de-vig 算法选型 (小梁 W6 起建模).
> 后续若有类似"GM 凭空发明的决议"被 owner v(N+1) 推翻 → 本节追加.

---

## §5 主管派单 KPI (ADR-005 §8 W5 试点 W6 硬约束追踪)

> **强制约束** (ADR-005 §8 推行时间线): W5 试点期间软约束 ≥ 70%, W6 起硬约束 100%. 老胡 PM 周报必带本段直至 W7 复评.

### §5.1 主管派单覆盖率 (核心指标)

- **公式:** 战斗单元 IC 任务 (主管派 spec + 派单) ÷ 全部战斗单元 IC 任务 × 100%
- **目标:** W5 试点 ≥ 70% (软约束) / W6 起 = 100% (硬约束)
- **本周实测:** N%

### §5.2 主管 SLA + GM SLA

| KPI | 目标 (ADR-005) | 本周实测 | 状态 |
|---|---|---|---|
| 主管 SLA (24h ack GM 派单) | > 90% | TBD | TBD |
| GM SLA (24h 拍板主管升级议题) | > 95% | TBD | TBD |
| 主管 48h 拆 IC 任务 | 100% | TBD | TBD |

### §5.3 协商会次数 (老胡主持每周二 30min, R-39 配套)

- **目标:** 每周 1 次 (老胡就职宣言 §4.2 E-W5-M01)
- **本周实测:** N 次
- **会议纪要 link:** docs/MEETINGS/YYYY-MM-DD-negotiation.md

### §5.4 escalate 次数 (R-39 配套)

- **sub-agent 拒接次数:** N (其中合理 N% / 误伤 N%)
- **IC 越主管找 GM 次数:** N (W6 起硬约束 = 0, W5 试点软约束 ≤ 例外)
- **GM 越主管派 IC 次数:** N (含 ADR-005 §3.2 例外: 顾问团 / P0 < 2h / 主管本人 / 跨多单元)

---

## §6 下周 backlog

- 列下周 (W+1) 派单, 按 5 单元 + 主管分组
- 标 P0 / P1 / P2 + Agreed / Compromised / Escalated
- 跨单元接口 ASK 列表 (24h ack 期望)
- **格式示例:**
  ```
  ### W6 派单 (2026-07-13 ~ 2026-07-17)
  #### A (老周)
  - W6-A-01 老李 polymarket-client live HTTP build (cpp-httplib FetchContent), P0
  - W6-A-02 小冯 BoostBeastTransport 真接 (替换 MockWssTransport), P0
  ...
  ```

---

## §7 待 GM 拍板议题

- 列协商不下需要 GM 后续 24h 决议的事项
- 含主管之间协商 48h 不下 + 跨单元红线触发
- 标 deadline + 影响下游
- **格式示例:**
  ```
  - E-01 小田归属仲裁 (老周 + 小余 不下) — 6/2 EOD 升老雷
  - E-02 GM 自检 5 题升 6 题 (加 SSOT vN 查询) — 6/06 EOW 评估
  ```

---

## §8 上周 GM 错 (CLAUDE.md §3 公开失败 + GM 错 #9 enforcement)

> **强制约束** (CLAUDE.md §3 "公开失败" 铁律 + INCIDENTS log 后续机制): 每周 GM 周报必带"本周 GM 错"一节, 强制 GM 自检.

- 引用 `docs/INCIDENTS/gm-self-mistakes-log.md` 本周新增的错
- 若本周无新增 → 写 "本周 GM 0 错 (沿用 #N 共性教训不重复)"
- 若有 → 列错号 + 一句话教训 + 永久 enforcement 落地状态

---

## §9 主管月度轮值 GM 助理 status (ADR-005 §4.3)

- 当月轮值主管 (6 月老周 → 7 月老韩 → 8 月小梁 → 9 月小余 → 10 月老胡)
- 当月 dial-in 老雷次数 + 学到的 GM 视角摘要
- 月末交接 ack

---

## §10 完成汇报

- 本周关键交付 1-3 句话摘要
- W6 启动 / 下次主管周同步预告
- 周报 owner 签字 (老胡) + 归档 (小米)

---

## 模板使用约束

1. **强制段:** §1 / §2 / §3 / **§4 (SSOT 版本演进 — GM 错 #9 enforcement 第 3 条)** / **§5 (主管派单 KPI — ADR-005 §8)** / §6 / §7 / §8 / §10
2. **可选段:** §9 (仅当月有轮值主管时填)
3. **每周五 EOD:** PM 老胡交周报 → 周日 EOD 5 主管 + 老郭 24h ack → 周一上午主管周同步引用
4. **GM 错 #9 enforcement 追踪:** §4 至少要有 1 条 "本周 SSOT 版本演进" 或显式 "本周无 owner 出 v(N+1)" — 不允许跳过
5. **W7 复评:** v2 模板首次套用后 4 周 (Sprint-3 W3 末) 复评有效性, 是否升 v3

---

**最后更新:** 2026-06-01 by 老胡 (E-026, PM)
**生效:** 2026-06-01 (W5 末 6/01 主管周同步 v1 决议 D-06)
**下次 review:** Sprint-3 W3 末复评 (W7 复评目标主管 KPI 24/30 同时复评模板有效性)
