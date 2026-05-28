# 扩招 Backlog v3 (老板 2026-05-28 拍板, 5 主管 W4 提请)

- **Owner：** 小林（hr-talent-manager）
- **会签：** 老雷
- **Last review：** 2026-05-28 (v3, 老板 ack 4 P1 新增)
- **下次评审：** 隔周三 HR 同步
- **v2 → v3 变更:** HC-04/05/06 编号语义改 (原 risk-quant / data-engineer / devops 推后, 编号让位给 5 主管 W4 W4 提请的 4 P1)

---

## HC 表（19 人, Q2-Q3 加速到 7 人）

### Q2-Q3 优先 (P0+, 老板 ack)

| HC | 岗位 | Persona | 部门 | 主管提请 | 季度 | 招聘理由 |
|---|---|---|---|---|---|---|
| HC-01 | onchain-ops-engineer | 老冀 | A. 系统工程 | 老周 | Q2 2026 | 链上交易广播 + gas 监控运维专人，MVP 关键路径 |
| HC-02 | strategy-execution-engineer | 小秦 | A. 系统工程 | 老周 | Q2 2026 | 信号→执行中间层主责，避免老周单点 |
| HC-03 | quant-engineer | 小吕 | C. 量化研究 | 小梁 | **Q3 提前到 8/1** | 量化研究→生产代码翻译，HR 提案 + 小梁 ack, M4.5 倒推 |
| HC-04 | **wal-storage-engineer** (新) | (待命名 E-052) | A. 系统工程 | **老周 W4 提请, 老板 5/28 ack** | Q3 2026 | 老王 #05 一人扛 4 wal kind 维护 (risk_audit / paper_audit / shadow_audit / position) P1 红 |
| HC-05 | **observability-2** (新) | (待命名 E-053) | A. 系统工程 | **老周 + 老吴 W4 提请, 老板 5/28 ack** | Q3 2026 | 小郑 #11 一人扛 76 metrics + 4 stack (Prometheus/Grafana/Loki/Tempo) P1 红 |
| HC-06 | **ml-data-engineer** (新) | (待命名 E-054) | D. 数据 | **小余 + 小邓 W4 提请, 老板 5/28 ack** | Q3 2026 | 小邓 #31 ML hook + Parquet pipeline 跨边界 (A 单元 WAL + D 单元 DWH + F ML), 1 人专职 |
| HC-07 | **qa-integration-engineer** (新) | (待命名 E-055) | E. 产品 | **老胡 + 老周 W4 提请, 老板 5/28 ack** | Q3 2026 | 小宋 #28 一人扛 256 unit + 4 sim + chaos + replay + M4.5 后 UAT, P1 红 |

### Q3 P1 (原 v2, 推后编号)

| HC | 岗位 | Persona | 部门 | 主管 | 季度 | 招聘理由 |
|---|---|---|---|---|---|---|
| HC-08 | risk-quant-engineer | (待命名) | B. 风控合规 | 老韩 | Q3 末-Q4 2026 | 实时风控模型 + 规则引擎专人 (老韩 7/10 黄, 多 signal 后需要分担) |
| HC-09 | data-engineer | (待命名) | D. 数据 | 小余 | Q3 末-Q4 2026 | 数据摄入/清洗/存储管道专职 (与 HC-06 ml-data 互补) |
| HC-10 | devops-infra-engineer | (待命名) | A. 系统工程 | 老周 | Q4 2026 (**AWS 解锁后**) | 跨洋部署 + CI/CD + 监控告警基础设施 (上链 deferred 一并 deferred) |
### Q3-Q4 P2 (原 v2 HC-07~13, 编号重排)

| HC | 岗位 | Persona | 部门 | 主管 | 季度 | 招聘理由 |
|---|---|---|---|---|---|---|
| HC-11 | market-data-qa-engineer | 小方 | D. 数据 | 小余 | Q4 2026 | 市场数据边缘 case 专项 QA |
| HC-12 | data-scientist | (待命名) | C. 量化研究 | 小梁 | Q4 2026 | 赔率定价建模专项 |
| HC-13 | sports-data-analyst | (待命名) | C. 量化研究 | 小梁 | Q4 2026 | Goalserve 体育数据校准专才 |
| HC-14 | platform-sre | (待命名) | D. 数据 | 小余 | Q4 2026 | 数据平台 latency SLA + 跨洋故障 RTO |
| HC-15 | product-ops-specialist | (待命名) | E. 产品 | 老胡 | Q4 2026 | 盘口配置 + 市场上下线运营 |
| HC-16 | cpp-network-engineer (扩编) | (待命名) | A. 系统工程 | 老周 | Q4 2026 | 高性能网络层专项优化 |
| HC-17 | bi-analyst | 小范 | E. 产品 | 老胡 | Q4 2026 | 实盘 PnL 归因 + 经营报告 |
| HC-18 | compliance-analyst | (待命名) | B. 风控合规 | 老韩 | Q1 2027 | 链上合规边界跟踪 |
| HC-19 | partner-liaison | (待命名) | E. 产品 | 老胡 | Q1 2027 | Polymarket/Goalserve 对接专人 |

**节奏 v3 (老板 5/28 加速):** Q2 招 2 (HC-01/02) / Q3 招 5 (HC-03 提前 8/1 + HC-04/05/06/07 W4 W4 ack) / Q4 招 7 / Q1 2027 招 2 / **2026 净增 14 人 (vs v2 的 12), 2027 Q1 再加 2 人, 总 16 人扩招**.

---

## 评委分配

| HC | 初面 | 深度面 | 文化面 | 入职 buddy |
|---|---|---|---|---|
| HC-01 老冀 | 老周 | 老周+老韩 | 小林+老雷 | 老周直带 |
| HC-02 小秦 | 老周 | 老周+小梁 | 小林+老雷 | 老周指定 |
| HC-03 小吕 | 小梁 | 小梁+老周 | 小林+老雷 | 小梁直带 |
| HC-08 risk-quant (原 HC-04) | 老韩 | 老韩+小梁 | 小林+老雷 | 老韩直带 |
| HC-09 data-engineer (原 HC-05) | 小余 | 小余+老周 | 小林+老雷 | 小余直带 |
| HC-10 devops (原 HC-06) | 老周 | 老周+小余 | 小林+老雷 | 老吴 buddy |
| HC-11 小方 (原 HC-07) | 小余 | 小余+小梁 | 小林+老雷 | 小宋 buddy |
| HC-12 data-scientist (原 HC-08) | 小梁 | 小梁+小余 | 小林+老雷 | 小程 buddy |
| HC-13 sports-data (原 HC-09) | 小梁 | 小梁+老胡 | 小林+老雷 | 老彭 buddy |
| HC-14 platform-sre (原 HC-10) | 小余 | 小余+老周 | 小林+老雷 | 老吴 buddy |
| HC-15 product-ops (原 HC-11) | 老胡 | 老胡+老韩 | 小林+老雷 | 小颖 buddy |
| HC-16 cpp-network (原 HC-12) | 老周 | 老周+小卢 | 小林+老雷 | 老陈 buddy |
| HC-17 小范 (原 HC-13) | 老胡 | 老胡+小余 | 小林+老雷 | 小董 buddy |
| HC-18 compliance (原 HC-14) | 老韩 | 老韩+老雷 | 小林+老雷 | 老黄 buddy |
| HC-19 partner-liaison (原 HC-15) | 老胡 | 老胡+老雷 | 小林+老雷 | 老胡直带 |

---

## Q2 在招追踪

| HC | 状态 | 关键日期 |
|---|---|---|
| HC-01 老冀 | JD 起草 | JD → 6/4；发布 → 6/10；面试启动 → 6/11+ |
| HC-02 小秦 | JD 起草 | JD → 6/4；发布 → 6/10；面试启动 → 6/11+ |

## Q3 在招追踪 (老板 5/28 加速 4 + HR 提案 1)

| HC | 状态 | 关键日期 |
|---|---|---|
| HC-03 小吕 | **HR 提案 W4 起草 (提前自 Q3)** | JD → 6/10；发布 → 6/20；面试启动 → 7/1+; 入职 → 8/1; 兜底 → 9/1 |
| HC-04 wal-storage | **W4 EOW (6/06) 起草, 老板 5/28 ack** | JD → 6/15；发布 → 6/30；面试启动 → 7/15+; 入职 → 8/15; 兜底 → 9/15 |
| HC-05 observability-2 | **W4 EOW 起草, 老板 5/28 ack** | JD → 6/15；发布 → 6/30；面试启动 → 7/15+; 入职 → 8/15; 兜底 → 9/15 |
| HC-06 ml-data | **W4 EOW 起草, 老板 5/28 ack** | JD → 6/15；发布 → 6/30；面试启动 → 7/15+; 入职 → 8/15; 兜底 → 9/15 |
| HC-07 qa-integration | **W4 EOW 起草, 老板 5/28 ack** | JD → 6/15；发布 → 6/30；面试启动 → 7/15+; 入职 → 8/15; 兜底 → 9/15 |

## 评委分配 (新 4 HC, 主管联签)

| HC | 初面 | 深度面 | 文化面 | 入职 buddy |
|---|---|---|---|---|
| HC-04 wal-storage | 老王 | 老王+老周 | 小林+老雷 | 老王直带 |
| HC-05 observability-2 | 小郑 | 小郑+老周 | 小林+老雷 | 老吴 buddy |
| HC-06 ml-data | 小邓 | 小邓+小余+小田 | 小林+老雷 | 小邓直带 |
| HC-07 qa-integration | 小宋 | 小宋+老胡 | 小林+老雷 | 小宋直带 |
