# Sprint-01 总结归档 (final)

- **Owner**: 小米 (doc-curator) 归档
- **GM 签**: 老雷
- **周期**: 2026-05-28 (collapse 启动) → 2026-05-28 (Retro 散会, 单日全量交付)
- **状态**: CLOSED (归档完成)
- **Last review**: 2026-05-28
- **关联**:
  - Backlog: `docs/SPRINTS/sprint-01.md`
  - Retro 主纪要: `docs/MEETINGS/2026-05-28-sprint1-retro-all-hands.md`
  - GM 决议总表: `docs/ADR/2026-05-28-gm-signoff-sprint1-retro.md`
  - Sprint-2: `docs/SPRINTS/sprint-02.md`

> 给老板的话: Sprint-1 不是写代码, 是把"我们到底要做什么"全公司 56 个人对齐, 把不可知的东西变成清单. 26 个调研任务 + 16 份真实发言 + 18 GM 决议 + 13 红线, 全部落档. 公司 4 价值观从 PPT 变成可点名的行为模板.

---

## 0. 业务视角 (老板能看懂)

| 维度 | 出发点 | 到 Sprint-1 末 |
|---|---|---|
| 班底 | 0 | 58 agent (45 类 + 10 IC + 1 HR + 1 UX + 1 dogfood) 全员认岗 |
| 文档 SSOT | 0 | 128 份 (含 78 RESEARCH + 13 ADR + 3 MEETING + Sprint backlog) |
| 关键决策 | 0 锁定 | 18 GM 决议 + 16 共识 + 6 妥协 全员公开 |
| 红线 | CLAUDE.md §8 5 条 | 20 条 (老韩 R-1..R-13 + 老黄 R1-R12 + R-14..R-20) |
| MVP scope | 模糊 | 7 hard gate (M4.5) + 8 拒绝清单 + 双轨甘特 (9/12 实战 / 10/29 OKR) |
| API 实测 | 未连 | Polymarket + Goalserve + Polygon RPC 三源全部联通 + 实测数据落档 |
| 风控核心参数 | 拍脑袋 | KELLY 0.25 / fill_rate 0.50 / PER_ORDER $5K (HARD)/$2K (SOFT) 数据背书锁定 |

**最核心成果**: 老钱拒绝清单 + 小董 7 gate + 小蒋 P0-02 fee 算账 → **MVP 上线门槛全员可量化**, 不再有"感觉差不多了"的灰色地带.

---

## 1. 26 ticket 完成盘点 (S1-001 ~ S1-026)

全部 26 ticket 验收通过. 单元拆分:

| 单元 | ticket | 主要交付 |
|---|---|---|
| A 系统工程 (老周) | S1-001/002/010/011/018/021/025/026 | 架构 v0.4 / Polymarket API 实测 / 跨洋部署 / 延迟预算 / chrome-devtools MCP 装机 / 跨洋带宽实测 v2 |
| B 风控合规 (老韩) | S1-004/005/006/016 | RM v0.3 (5 档 STALE + 4 字段 4 reject) / 老孙 v5 私钥简化版 / 老黄合规红线 v2 / 老沈威胁模型 v2 |
| C 量化研究 (小梁) | S1-007/012/017 | 体育市场结构 v1 / 体育博彩行业分析 / 信号 catalog v1 (12 信号假设) |
| D 数据基础设施 (小余) | S1-003 | Goalserve API 实测 + 字段清单 v3 + 小段 11 sport probe |
| E 跨域 (老胡) | S1-013/014/015/019/020/022/023/024 | HC-01 老冀 + HC-02 小秦 JD / 全局甘特 / docs 体系 / 老徐 RACI / 小白 LLM 规范 / 小尤 UX framework / 小宫 dogfood / 老徐工具盘点 |
| F 顾问 (老雷直属) | S1-008/009 | 老钱 8 维度拒绝清单 / 老叶 Polygon RPC 选型 |

**完成率**: 26/26 = 100%. 残留尾巴 (S1-021 跨洋实测 v2) 进 Sprint-2 P0.

---

## 2. 16 份真实发言 (Sprint-1 Retro 模范)

老雷 Retro 拒绝"PM 编 16 份发言", 改为各 owner **独立真实表态 + GM 收口** 范式. 现场发言归档于 `docs/MEETINGS/sprint1-retro/`:

- **老周** (架构): v0.4 落实 R-12 + 6 misalignment 自查 + 3 处自认错
- **老韩** (RM): v0.2 ADR-001 13 项整改全过, 3 项待 GM 拍
- **老胡** (PM): M4.5 双轨 (9/12 实战 + 10/29 OKR)
- **老黄** (合规): R4 美国元素 → 老孙 v4 §6.3 AWS us-east-1 → GCP SG
- **老李** (协议): HMAC 4 bug **当面公开认** + 4 项补救 (test vector 14 / 401 SOP / endpoint matrix v3 / 月度 SDK diff)
- **老叶** (链上): RPC 单 vendor 反对, 保留双 vendor + dRPC
- **老郭** (评审): ADR-001 升 Accepted, 自查"漏看老周生命周期 §2.A.2"
- **老钱** (CPO): P0-02 进 M2 hard gate + 8 条 Sprint-2 预防性拒绝
- **小程** (信号): P0-01 阈值 3¢ → 5¢
- **小董** (stats): G8 不进 hard gate (观察项) / FWER 拒 1 yellow
- **小蒋** (backtest): PIT CI 主笔接 / R-21 paper 联调时间不够 自标 Top 1
- **小袁** (微观): 264 双边样本 / hot 再切一档 INPLAY_HOT_CRIT 200/800ms
- **小梁** (financial): PER_ORDER $5K/$2K 当场交付
- **小宋** (test): 17+1 test case + R-12 4 场景 fixture + R-21 顶 30%
- **小肖** (Kelly): NaN 检测补充 + 命名碰撞警告 (paper-mode vs slippage-mode)

模范表扬 (Retro §0.2):
- **老李 + 小袁 + 小蒋**: 数字说话 + 公开失败 双价值观模范
- **老韩 + 老郭**: 整改速度 + 跨域自查模范

---

## 3. 18 GM 决议 (D-01 ~ D-18)

详见 `docs/ADR/2026-05-28-gm-signoff-sprint1-retro.md`. 此处摘 12 条最关键:

| # | 决议 | 影响域 |
|---|---|---|
| D-01 | KELLY 0.25 / fill_rate 0.50 / PER_ORDER $5K/$2K | RM 核心参数锁死 |
| D-02 | P0-01 阈值 5¢ (从 3¢ 升) | 信号 catalog 直改 |
| D-03 | P0-02 进 M2 hard gate (OOS Sharpe ≥ 0.8 含 fee) | MVP 不首发 |
| D-04 | M4.5 G1-G7 全 hard, 1 yellow 拒 | 上线门槛 |
| D-05 | Polymarket WSS 2 conn 分离 (market + user) | 架构 v0.4 |
| D-06 | STALE 5 档 (含 INPLAY_HOT_CRIT 200/800ms) | RM v0.3 |
| D-07 | vCPU0 4-5 conn 单 reactor (老李精细方案), 压测 W3 验 | 架构 + 性能 |
| D-10 | 命名统一: paper-mode {Sim/Hybrid/Real} vs slippage-mode {Linear/Sqrt/CLOB} | 全员避免歧义 |
| D-12 | paper/live ULID 命名空间方案 A (独立 generator + mode 字段) | R-11 落实 |
| D-13 | 老孙 v4 §6.3 AWS us-east-1 → GCP asia-southeast1 (老黄 R4 美国元素) | 合规 |
| D-15 | 美国 entity Escalated → GM 待外部律师 confirm | jurisdictional-deferral |
| D-18 | ADR 模板加"听取确认清单"章节 | 跨域听取义务制度化 |

---

## 4. 13 红线 (R-1 ~ R-20 全收口)

老韩 RM 红线 (R-1..R-13) + 老黄合规红线 R1-R12 + Retro 新增 6 条 + 老雷 R-20 新立 = **共 20+ 条**.

Sprint-1 Retro 新增 6 条 (R-14 ~ R-19):

| # | 红线 | Owner |
|---|---|---|
| R-14 | paper Mode A 套 fill_rate ~ Bernoulli(0.50-0.65), 禁 fill_rate=1.0 | 小蒋 paper engine v0.3 |
| R-15 | quote_age_ms > 100 && market_state ∈ {INPLAY_HOT, INPLAY_HOT_CRIT} → size *= 0.5 | 老韩 RM v0.3 |
| R-16 | 命名碰撞统一 (paper-mode vs slippage-mode 不许字母 A/B/C 复用) | GM 联签 |
| R-17 | HMAC test vector 14 条强制 CI, 任一行复现失败 = pipeline 红 | 老李 + 老练 |
| R-18 | PIT CI 主笔小蒋, future-leak row = 0 (R-3 镜像) | 小蒋 + 老练 |
| R-19 | 阈值参数必须标"数据依据 = 谁的实测 + 文档引用 + 实测日期" | 全员 + 老郭 ADR 模板 |

**R-20 (2026-05-28 老雷新立, 影响最大)**:

> 所有数据源使用必须标记时间信息 (4 时间戳契约: `event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts`), 优先用上游 ts, 禁本地 `now()` 替代. 违者 P0.

落地 owner: 老韩 / 老唐 / 小蒋 / 小邓 / 老高 / 老孙 / 老李 / 小段 / 小余 / 小米 / 老郭 (11 个 owner Sprint-2 内执行). 详见 `docs/ADR/2026-05-28-gm-redline-data-source-timestamping.md`.

---

## 5. 与 OKR 对照 (M1-M5 + M4.5 gate)

| KR | Sprint-1 末状态 | gap |
|---|---|---|
| KR-C-1 MVP 2026-11-30 实盘上线 | OKR 不动 (10/29) + 实战双轨 9/12 | 双轨缓冲 6 周 |
| KR-C-2 首笔成交 72h 零崩溃 | 设计就绪 (RM v0.3 + paper engine) | 待实施 (Sprint-2 起) |
| KR-C-3 风控失效 = 0 | 13 红线 + RM 4 字段 4 reject 全锁 | M4.5 G3 验 |
| KR-C-4 回测 Sharpe > 1.0 | 小蒋 backtest v0.2 skeleton 就绪 | M3 Sprint backtest v2 (7/30) |
| KR-C-5 老冀 + 小秦入职 | HC-01 + HC-02 JD 起草, Sprint-2 W1 发布 | 6/30 老冀 / 小秦未入 → R-07 启动甘特兜底 |

**M4.5 gate (7 hard gate, 老钱 + 小梁 + 小董 联签, FWER 拒 1 yellow)**:
- G1 OOS Sharpe ≥ 0.8 含 3% taker fee
- G2 bootstrap CI 下界 > 0.3
- G3 RM 误拒率 ≤ 5% (观察项 G8 加 v1.1)
- G4 ≥ 500 场样本 (M2)
- G5 paper engine 72h 零崩溃
- G6 ≥ 50 笔触发 (5¢ 阈值后边际, 21 天窗口可能)
- G7 实盘前 30 天 paper 联调

---

## 6. 公司 4 价值观落地证据

| 价值观 | Sprint-1 落地 |
|---|---|
| **不耻下问** | 老李 HMAC 4 bug 当面认 + 4 项补救; 老雷 D-15 美国 entity 标"GM 待外部律师 confirm" 不假装全知; 老郭自查"漏看老周 §2.A.2"; 小米 v1 健康度报告找出"老郑 = 小米" 错署 |
| **数字说话** | 小袁 264 双边样本 / 小蒋 P0-02 fee 算账 / 小董 FWER 拒 1 yellow / R-19 阈值必须标数据依据 + 实测日期; 老雷 R-20 立 4 时间戳契约 |
| **公开失败** | 老周 6 misalignment 自查; 老韩 3 项 misalignment 待 GM 拍; 老雷 §0.3 自己承认 2 个错 (草率 ack 老李 + 险些让 PM 编 retro) |
| **实盘优先** | M4.5 7 hard gate 不允许 1 yellow; P0-02 不进首发; paper engine 共享生产 binary (R-11); GCP SG 主 wrap (合规优先, 不为速度妥协) |

---

## 7. Sprint-1 痛点 + Sprint-2 续接

| 痛点 | Sprint-2 续接 |
|---|---|
| Rust → C++ 重写 (老孙 v1/v2/v3 + 老张 stack 沉淀) | Sprint-2 S2-010 老孙 v4 C++ 实现 |
| ADR-001 整改 13 项 (本 sprint 设计层过, 实施层未) | Sprint-2 S2-001/002 老周 v0.4 + 生命周期 v1.1 |
| 跨洋网络实测 v2 仍未跑全 | Sprint-2 P0 续接 |
| HMAC test vector / 401 SOP / 月度 SDK diff (老李 4 项) | Sprint-2 W1 W2 老李交付 |
| paper engine R-21 联调时间不够 | Sprint-2 小蒋三道闸 (W6 skeleton 并行 + placeholder microstructure + W7 末早期联调切片) |
| PIT CI v0.1 (D-16, 小蒋主笔) | Sprint-2 W3 末 |
| US entity D-15 Escalated | 等老雷决定启动 + 外部律师 RFP |

---

## 8. 文档归档动作 (小米执行)

- INDEX.md v2 同步 (本次)
- 13 ADR 入索引 + 时间线
- Superseded 文档 (架构 v0.1-v0.3 / RM v0.1-v0.2 / 私钥 v1-v4 等) 在 INDEX 标注 → 当前 active 版本
- DEPRECATED 文档 (Rust 两份) 单独分组标注
- R-20 不合规 RESEARCH 文档清单 → v2 健康度报告 §3

---

## 9. Retro 散会条件 (8 条已落 7, 1 条悬空)

详见 Retro §9.2:

- 1-5: GM 决议 ack / ADR 模板 / BIP39 sign / 老孙 v4.1 patch / 老吴代理产权确认 — 6/4 截止, 已启动
- 6-7: 老雷 Sygnum 主体 + Goalserve odds 商务 — 6/11 截止
- 8: Sprint-2 Planning 6/13 (Mon) 9:00 — 已锁日期

唯一悬空: **D-15 美国 entity 启动决策** — 老雷不锁日期, 维持 jurisdictional-deferral.

---

## 10. GM 收口 (老雷一句话)

> "Sprint-1 不是写代码的 sprint, 是把 56 个人对齐到同一张地图上的 sprint. 18 决议 + 20 红线 + 7 hard gate 把不可知的东西变成清单. Sprint-2 起开始 ship 代码, 拿数字说话."

---

**散会签字**:
- 归档主笔: 小米 (doc-curator)
- GM 批: 老雷

**End of Sprint-01.**
