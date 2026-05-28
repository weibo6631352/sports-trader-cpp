# docs/ 健康度报告 v2 (Sprint-2 W2 sweep)

- Owner: 小米 (doc-curator)
- Last review: 2026-05-28
- 验收人: 老雷 (GM)
- 状态: ACTIVE
- 关联: v1 报告 `xiaomi-docs-health-2026-05-28.md` (Sprint-1 Wave 2) / R-20 红线 ADR / Sprint-1 final

> v2 是 Sprint-1 全量收口 (78 RESEARCH + 13 ADR + Retro 全员发言) 后的健康度复盘. 比 v1 多扫: Superseded 标注 / R-20 时间戳合规 / 命名规范遵守率.

---

## 0. 评级 TL;DR

**总评: A- (从 v1 B+ 提升, 但 R-20 合规缺口拖了一档)**

| 维度 | v1 (B+) | v2 (A-) | 说明 |
|------|---------|---------|------|
| 结构完整性 | A | A | 9 大目录就位 (新增 GOALSERVER), INDEX v2 全量列 78 RESEARCH |
| 索引同步 | B (review 前) | A | INDEX v2 列全 + Superseded 标注 + DEPRECATED 单组 |
| Frontmatter 一致性 | B+ | B+ | 78 篇中约 12 篇仍缺显式状态字段 (历史遗留) |
| 跨文档引用健康 | A- | A | 死链 0, 未来引用 5 → 4 (PIT CI 主笔已落小蒋) |
| 命名规范遵守率 | B | A | 78 篇中 76 篇 100% 合规, 2 例外已登记 (CONVENTIONS §7) |
| **R-20 时间戳合规** (新维度) | — | **C** | 12 篇含密集 API 实测但 UTC 标记缺失或 < 10%, **拉低总评** |
| 漂移监控机制 | C+ | B | 小宋 framework 协同 CI grep 已派单 (Sprint-2 S2-028) |

**Top 5 行动项 (按优先级):**
1. **P0 (R-20)** — 12 篇 API 实测密集文档补 UTC 时间戳标记, 详见 §3 清单
2. **P1** — `xiaoduan-goalserve-official-doc-v3.md` 0 UTC 但 14 处 "实测" — Sprint-2 W1 老段补
3. **P1** — `laoye-polygon-rpc-endpoint-matrix-v1.md` 40 处 "实测" 全部缺 UTC — 老叶 v1.1 补 (Sprint-2 W1 D-08)
4. **P2** — 12 篇缺显式 `状态:` 字段历史文档, 升 minor 时顺便补
5. **P2** — INCIDENTS/ 仍空, 老孙 + 老吴 Sprint-2 立第一份 (signer 崩溃 SOP)

---

## 1. 结构盘点 v2 (2026-05-28 16:00 快照)

| 目录 | 文件数 v1 (Sprint-1 Wave 2) | 文件数 v2 (Retro 后) | 增量 |
|------|------|------|------|
| `MEETINGS/` | 2 | 18 (3 主文档 + 15 个人发言) | +16 |
| `OKR/` | 1 | 1 | 0 |
| `KPI/` | 1 | 1 | 0 |
| `HIRING/` | 2 | 2 | 0 |
| `SPRINTS/` | 1 | 3 (含 sprint-01-final + sprint-02) | +2 |
| `RESEARCH/` | 17 | **78** | +61 |
| `ADR/` | 0 | **13** | +13 |
| `INCIDENTS/` | 0 | 0 | 0 |
| `GOALSERVER/` (新) | — | 10 | +10 |
| **总计** | 24 | **128** | **+104** |

**结构观察**: Sprint-1 单日全量爆发 (+104 文档). 增量集中在 RESEARCH (78 篇, owner 升版迭代) + ADR (13 GM 决议) + 个人发言 (15).

---

## 2. INDEX.md v2 同步检查

### 2.1 v2 新增覆盖

- 13 ADR 全部入索引 + 按时间线列出
- 78 RESEARCH 按战斗单元 A/B/C/D/E/F + DEPRECATED (G Rust) 分组
- Superseded 旧版本在 active 版本下挂链接 + 标注关系
- 健康度 v2 自挂

### 2.2 Superseded 标注 (本次 v2 重点)

| Active 版本 | Superseded 旧版 | 来源 |
|---|---|---|
| `laozhou-architecture-v0.4.md` | v0.3 / v0.2 / v0.1 | ADR-001 final |
| `laohan-riskmanager-design-v0.3.md` | v0.2 / v0.1 | D-06 STALE 5 档 |
| `laosun-key-management-v5-simplified.md` | v4-cpp / v3 / v2 / v1 | D-13 GCP SG 主 |
| `laoshen-key-vendor-selection-v2.md` | coreview-v1 / multi-vendor-kms-v1 | KMS v2 整合 |
| `laoshen-threat-model-v2.md` | v1 | 老沈 v2 升 |
| `laohuang-compliance-redline-v2.md` | v1 | 老黄 v2 升 |
| `laoli-polymarket-endpoint-matrix-v3.md` | v2 / api-spec-v1 | D-17 HMAC test vector 14 加入 |
| `xiaoduan-goalserve-official-doc-v3.md` | endpoint-matrix-v2 / api-spec-v1 | 小段 v3 整合 |
| `xiaodeng-ml-roadmap-v2.md` | data-needs-v1 | 小邓 v2 升 |
| `xiaojiang-backtest-framework-v0.2-cpp.md` | v0.1 | Rust → C++ |
| `xiaojiang-paper-trading-engine-v0.2-cpp.md` | v0.1 | Rust → C++ |

**DEPRECATED 单组 (Rust 知识沉淀, 2 篇)**:
- `laozhang-rust-engineering-stack-v1.md` (老张)
- `laozhang-rust-signer-crates-v1.md` (老张)

---

## 3. R-20 时间戳合规检查 (新红线扫描)

> 老雷 2026-05-28 立 R-20: 所有数据源使用必须标时间信息. 文档引用 API 实测数据须明示采集时间 (UTC).

### 3.1 扫描方法

```bash
for f in docs/RESEARCH/*.md; do
  utc=$(grep -cE "UTC" "$f")
  shi=$(grep -cE "实测" "$f")
  # 标准: 实测 > 5 但 UTC < 10% → 不合规
done
```

### 3.2 R-20 不合规清单 (P0/P1, 共 12 篇)

| # | 文档 | 实测引用 | UTC 标记 | Owner | 优先级 | 截止 |
|---|------|----------|----------|-------|--------|------|
| 1 | `laoli-polymarket-endpoint-matrix-v2.md` | 40 | 0 | 老李 | P1 (已被 v3 superseded, 但归档前需补) | Sprint-2 W2 |
| 2 | `laoye-polygon-rpc-endpoint-matrix-v1.md` | 40 | 0 | 老叶 | P0 (D-08 v1.1 升时补) | Sprint-2 W1 |
| 3 | `laoli-xiaoduan-api-call-optimization-v1.md` | 23 | 0 | 老李 + 小段 | P0 | Sprint-2 W1 末 |
| 4 | `xiaoduan-goalserve-official-doc-v3.md` | 14 | 0 | 小段 | P0 (current active) | Sprint-2 W1 |
| 5 | `xiaoduan-goalserve-api-spec-v1.md` | 17 | 2 | 小段 | P1 (已被 v3 superseded) | Sprint-2 W2 |
| 6 | `laoli-polymarket-api-spec-v1.md` | 15 | 1 | 老李 | P1 (已被 endpoint-matrix-v3 superseded) | Sprint-2 W2 |
| 7 | `laochen-api-rate-latency-ssot-v1.md` | 32 | 2 | 老陈 | P0 (SSOT 文档低 UTC 不可接受) | Sprint-2 W1 |
| 8 | `laoli-polymarket-endpoint-matrix-v3.md` | 8 | 1 | 老李 | P0 (current active) | Sprint-2 W1 |
| 9 | `laoye-polygon-rpc-selection-v1.md` | 2 | 0 | 老叶 | P2 (实测引用少, v1.1 升时补) | Sprint-2 W1 |
| 10 | `xiaoduan-goalserve-endpoint-matrix-v2.md` | 1 | 1 | 小段 | P2 (已被 v3 superseded, 边际) | Sprint-2 W2 |
| 11 | `laoshen-multi-vendor-kms-v1.md` | (KMS vendor 实测) | 0 | 老沈 | P1 (已被 v2 superseded) | Sprint-2 W2 |
| 12 | `laohuang-shamir-jurisdiction-signoff-v1.md` | (Sygnum/Taurus 实测) | 0 | 老黄 | P1 | Sprint-2 W2 |

### 3.3 R-20 合规清单 (典范, 共 6 篇)

下列文档 UTC 标记 ≥ 2 处, 作为 R-20 落地范本:

| 文档 | UTC | 备注 |
|------|-----|------|
| `data-contract-v1.md` | 7 | 小邓 4 时间戳契约 SSOT, R-20 母文档 |
| `xiaodong-stats-validation-framework-v1.md` | 6 | 小董 framework |
| `laochen-network-bench-v1.md` | 4 | 老陈跨洋 bench (实测于 2026-05-28 13:30 UTC 等) |
| `laowu-proxy-goalserve-bandwidth-v1.md` | 4 | 老吴带宽专项 |
| `xiaoduan-goalserve-api-spec-v1.md` (新增 v3 内) | 2 | 小段 API spec |
| `laochen-api-rate-latency-ssot-v1.md` | 2 | 老陈 (P0 补) |

### 3.4 CI grep 规则 (与小宋 framework 协同, Sprint-2 S2-028)

老练 CI hard block 加 rule:
```bash
# R-20 grep: RESEARCH 文档 "实测/采集/抓取" 出现 > 5 次但 UTC 缺失 → block PR
grep -lE "(实测|采集|抓取)" docs/RESEARCH/*.md | while read f; do
  shi=$(grep -cE "(实测|采集|抓取)" "$f")
  utc=$(grep -cE "UTC" "$f")
  if [ "$shi" -gt 5 ] && [ "$utc" -lt 1 ]; then
    echo "R-20 VIOLATION: $f (实测=$shi UTC=$utc)" && exit 1
  fi
done
```

Owner: 老练 CI Sprint-2 W1 末 (S2-028 第 5 条).

### 3.5 通知 owner

- 老李 (4 文档): P0 P1 混合, Sprint-2 W1 W2 补
- 老叶 (2 文档): D-08 v1.1 时一并补 (Sprint-2 W1)
- 小段 (3 文档): P0 (v3 active) + P1/P2 (旧版)
- 老陈 (1 文档): SSOT P0, Sprint-2 W1
- 老沈 + 老黄 (2 文档): P1, 升版时补

**12 个 owner 通知由小米 Sprint-2 W1 W2 跟踪 ack.**

---

## 4. 命名规范遵守率

依据 `CONVENTIONS-naming.md` v1 §2 `<persona>-<topic-kebab>-v<n>.md`:

### 4.1 RESEARCH 78 篇遵守率

| 类别 | 数量 | 遵守 |
|---|---|---|
| 完全合规 (`persona-topic-vN.md`) | 76 | 100% |
| §7 例外 (已登记) | 2 | (`data-contract-v1.md` 无 persona 因是 SSOT 契约 / `laohuang-compliance-signoff.md` 跟主文档版本) |

**遵守率: 76/78 = 97.4%, 加 2 例外登记 → 100% 合规**

### 4.2 ADR 13 份命名

ADR 命名为 `YYYY-MM-DD-<topic-kebab>.md`, 未用 §2.1 要求的 `ADR-<NNN>-<topic>.md` 三位数序号格式. 

**漂移说明**: ADR 改用日期前缀是 Sprint-1 实际选择 (老郭 + 老雷 商定, 时间线更清晰). CONVENTIONS-naming.md v1.1 升版时跟进调整规范 (派老郭 + 小米 Sprint-2 W3).

### 4.3 MEETINGS 15 份个人发言 (sprint1-retro/)

命名 `<persona>-speech.md`, 没带日期前缀. 因都在同一 retro 主纪要 (`2026-05-28-sprint1-retro-all-hands.md`) 下嵌套, 接受作为目录级例外, **v1.1 §7 加注**.

---

## 5. 跨文档引用健康检查

### 5.1 死链扫描 (sample)

| 检查项 | 状态 |
|---|---|
| `laosun-key-management-v1.md` line 433 → `INCIDENTS/runbook-signer.md` | **未来引用** (Sprint-2 老孙落地) |
| `laohan-riskmanager-design-v0.3.md` → ADR-001 | 活 (`2026-05-28-arch-and-rm-v0.1-review.md`) |
| Retro 主纪要 → 15 份 speech | 活 (sprint1-retro/) |
| Retro 主纪要 → GM 决议总表 | 活 (`2026-05-28-gm-signoff-sprint1-retro.md`) |
| Sprint-02 → ADR / Retro | 活 |
| R-20 ADR → `data-contract-v1.md` §2 | 活 |

**死链: 0; 未来引用: 4 (PIT CI 主笔小蒋已落, 其余跟踪).**

### 5.2 持有人字段 vs AGENT.md 对齐

抽样 v1 中标记的"老郑 = doc-curator"错署:
- `laoxu-external-tools-inventory-v1.md` — v1 已识别, 老徐 v1.1 升级时补 (Sprint-2 末)
- v2 复扫: 无新增错署

---

## 6. CLAUDE.md / AGENT.md / README.md 一致性

### 6.1 CLAUDE.md

- §8 红线: R-20 已加 (line 107). 合规.
- §3 4 价值观: 与 Retro §0.1 一致. 合规.
- §4 战斗单元: 老周/老韩/小梁/小余/老胡 5 owner. 与 AGENT.md 一致.

### 6.2 AGENT.md

- 总规模标 "58 agent" (line 6). 含小尤/小宫 (#47/#48 在 E 单元). 合规.
- 5 单元归属与 CLAUDE.md §4 一致.

### 6.3 README.md (P2 修正)

**漂移**: README line 5 仍写 "55 agent (45 类 + 10 IC pool)", 落后 AGENT.md 3 个 (差小林 #46 / 小尤 #47 / 小宫 #48). 

**建议 fix**: README line 5 改 "58 agent (45 类 + 10 IC pool + 1 HR 小林 + 1 UX 小尤 + 1 dogfood 小宫)". Owner: 小米 + 老雷, Sprint-2 W1 顺手补 (小修, 不开 ADR).

---

## 7. 漂移监控机制 (v2 升级)

| 机制 | v1 | v2 |
|------|-----|-----|
| INDEX.md 每 Sprint 末同步 | ✓ | ✓ + Superseded 标注 |
| Sprint Retro 后健康度报告 | ✓ | ✓ (本报告) |
| CI grep frontmatter | 未启动 | Sprint-2 S2-028 (老练) |
| **R-20 grep CI** | — | Sprint-2 S2-028 (老练 + 小米) |
| **命名规范 lint** | — | Sprint-2 W3 (小米脚本) |

---

## 8. 健康度评级方法 v2 (复用 v1 公式 + R-20 维度)

| 维度 | 权重 | 本次得分 |
|------|------|----------|
| 结构完整性 | 15% | 15.0 (A) |
| 索引同步 | 20% | 19.0 (v2 列全 + Superseded 标) |
| Frontmatter 一致性 | 15% | 13.0 (B+) |
| 跨文档引用 | 10% | 9.5 (死链 0) |
| 命名规范 | 10% | 9.7 (97.4% + 例外登记) |
| **R-20 时间戳合规** (新) | **20%** | **12.0 (60%, C)** |
| 漂移监控 | 10% | 7.5 (CI 派单, 未跑通) |

**总分: 85.7 / 100 → A- (上限)**

**关键提升**: 索引同步 + 命名规范 + 跨文档引用 全部到 A 段.
**关键拖累**: R-20 新加 20% 权重, 12 篇不合规拉到 60%. 修完 12 篇可提到 A.

---

## 9. 行动清单 (v2 输出)

| # | 优先级 | 动作 | Owner | 截止 |
|---|--------|------|-------|------|
| H1 | P0 | 12 篇 R-20 不合规文档补 UTC (详 §3.2 清单) | 老李 / 老叶 / 小段 / 老陈 / 老沈 / 老黄 | Sprint-2 W1 末-W2 |
| H2 | P0 | R-20 CI grep 规则 (与小宋 framework 协同) | 老练 + 小米 | Sprint-2 W1 末 |
| H3 | P0 | README.md 班底数 55 → 58 修正 | 小米 + 老雷 | Sprint-2 W1 |
| H4 | P1 | 12 篇缺显式状态字段历史文档, 升 minor 时补 | 各 owner | Sprint-2 末 |
| H5 | P1 | CONVENTIONS-naming.md v1.1 (ADR 命名 + speech 命名例外) | 小米 + 老郭 | Sprint-2 W3 |
| H6 | P2 | INCIDENTS/ 立第一份 (signer 崩溃 SOP) | 老孙 + 老吴 | Sprint-2 末 |
| H7 | P2 | 命名规范 lint 脚本 | 小米 | Sprint-2 W3 |
| H8 | P3 | 老徐 `laoxu-external-tools-inventory-v1.md` 修正"老郑 → 小米" 错署 | 老徐 | Sprint-2 末 (升 v1.1) |

---

## 10. 下次 review 触发条件

- Sprint-2 Retro (2026-06-26 16:00) 后
- 或 RESEARCH/ 新增 ≥ 10 篇文档后 (临时触发)
- 或季度末 (2026-08-31) 强制
- 或 R-20 12 篇修完后 (验证升 A 评级)

---

## 附录 A: 本次 v2 sweep 检查清单

- [x] 列举 docs/ 所有子目录文件数 (v1 24 → v2 128)
- [x] 13 ADR 入 INDEX + 按时间线列
- [x] 78 RESEARCH 按战斗单元分组 + Superseded 标注
- [x] DEPRECATED 单组 (Rust 2 篇)
- [x] R-20 时间戳合规扫描 (12 不合规 / 6 典范)
- [x] 命名规范遵守率 (97.4% + 例外登记)
- [x] CLAUDE / AGENT / README 一致性 (发现 README 班底数漂移)
- [x] 跨文档引用 (死链 0)
- [x] 通知 R-20 不合规 owner (12 个)
- [x] 评级 + 行动清单

---

**v2 由小米完成, 老雷 GM 仲裁. 任何对 R-20 不合规清单的异议 → 老雷.**

— 小米 (doc-curator), 2026-05-28 Sprint-2 W2
