# GM 政策 — API 监控是长期规划，不一次性收集全数据

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** Standing Policy（标准化）
- **关联:** Wave 10 + Wave 10 v2 全 endpoint 测试、`docs/RESEARCH/laoli-polymarket-endpoint-matrix-v2.md`、`docs/RESEARCH/xiaoduan-goalserve-endpoint-matrix-v2.md`（在跑）、`docs/RESEARCH/laoye-polygon-rpc-endpoint-matrix-v1.md`（在跑）

---

## 1. 用户原话

> "告诉他们不要等数据，只做我们当前时间能拿到的就行，这个时间点并不是所有都开赛的，这是个长期规划，数据不太可能一次收集全部。"

## 2. 政策

**API endpoint 监控是长期规划，不要求一次跑完。**

具体落地：
- 当下时间能拿到的数据立刻写文档（off-season 的 sport 标记 "no data this window, retest in season"）
- 不允许为了"全覆盖"而等数据（NBA 季后赛刚结束 / NFL 9 月开 / NHL 10 月开 / etc.）
- 文档里明确写"测试窗口 + 当下覆盖率 + 待补测列表"
- 长期补测节奏：每月一次 sweep，覆盖当月在赛 sport 的所有 endpoint

## 3. 当下时间快照（2026-05-28，给团队参考）

| Sport | 季节状态 | 数据可获取性 |
|---|---|---|
| NBA | 季后赛刚结束（24/25 season） | 历史完整可拿，inplay 暂无 |
| WNBA | 在赛季 | ✅ inplay + livescore |
| MLB | 正赛季 | ✅ 完整 |
| NHL | 季后赛刚结束 | 历史完整，inplay 暂无 |
| NFL | Off-season（9 月开） | 仅 schedule + offseason news |
| NCAA | Off-season | 仅 schedule |
| Soccer (EPL/La Liga 等欧洲) | Off-season（5-8 月） | 仅 schedule + 转会期 |
| Soccer (MLS) | 正赛季 | ✅ inplay + livescore |
| Soccer (世界杯预选 / 友谊赛) | 散在 | 部分日有 |
| Tennis | Roland Garros 进行中 | ✅ inplay |
| Cricket | IPL 收尾 | ✅ 部分 |
| Golf | PGA 进行中 | ✅ 部分 |
| F1 | 正赛季 | ✅ 完整 |
| MMA/UFC | 周末有 event | 散在 |
| Boxing | 散在 | 散在 |
| Esports (LoL/Dota/CS:GO/Valorant) | 多联赛并行 | ✅ 部分 |

**注：** Goalserve odds endpoint 全 0 字节是另一个问题（权限缺失，老黄商务跟进，与季节性无关）。

## 4. 长期 sweep 节奏（标准化）

**月度 API endpoint 健康度 sweep**

- **Owner：** 小段 (Goalserve) + 老李 (Polymarket) + 老叶 (Polygon RPC) + 小冯 (general API watch)
- **频率：** 每月最后一周
- **输出：** `docs/RESEARCH/api-health-<YYYY-MM>.md`（追加，不覆盖 v1/v2 矩阵）
- **覆盖：** 当月在赛 sport 的全 endpoint + 已发现的限流变化 + 字段变更 + 复用率重测
- **触发额外 sweep：** 上游官方公告 API 变更、入赛季首周（如 NFL 9 月）、合约升级

## 5. 当下任务调整（in-flight agents）

针对小段 v2 + 老叶 v1 在跑：
- **不阻塞**：当下能测的写完，off-season sport 标 "OUT-OF-SEASON, retest YYYY-MM"
- **不重跑全 sport** — 当下数据 = 当下报告，下一轮 sweep 自然补
- **报告里加 "Coverage Window" 章节**：明确测试时间 + 当下季节性快照

## 6. 派单

- **小段** v2 → 标 off-season sport，不重跑
- **老叶** v1 → Polygon 链上数据与季节无关，但 vendor RPC 可能限流时段波动，标 "测试时段"
- **小冯** → 接管月度 sweep 协调（与 API 变更监控合并到他原本 KPI）
- **老胡** → 月度 sweep 进甘特图（每月末固定 1 天）
- **小米** → docs/INDEX 加月度 sweep 归档区

---

**Standing Policy by 老雷, 2026-05-28**
