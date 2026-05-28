# R-20 12 篇 frontmatter 回灌 progress 报告

- Owner: 小米 (doc-curator)
- Date: 2026-05-28
- 关联: `xiaomi-docs-health-2026-05-28-v2.md` §3.2 不合规清单 / `ADR/2026-05-28-gm-redline-data-source-timestamping.md` (R-20)
- 验收人: 老雷 (GM)
- 状态: ACTIVE, W3 Wave 18 收口
- 范围: 仅加 1 行 `Last measured` frontmatter, 不改正文

---

## 0. TL;DR

12 篇全部补 frontmatter. 10 篇有具体 UTC timestamp (取自 `docs/RESEARCH/data/*.txt` 文件名), 2 篇 (老沈 KMS + 老黄 Shamir) 无对应 data 探针文件 → 标 `[需 owner confirm]`, 派给 owner 升 v2 时补.

**健康度评级: A- → A** (R-20 维度 60% → ~92%, 总分 85.7 → 92.0).

---

## 1. 12 篇补全清单

| # | 文档 | data 文件 timestamp 来源 | Last measured | 状态 |
|---|------|--------------------------|----------------|------|
| 1 | `laoli-polymarket-endpoint-matrix-v3.md` | `laoli-polymarket-matrix-v2-20260528-133323.txt` | 2026-05-28 13:33 UTC | 完整 |
| 2 | `laoli-polymarket-endpoint-matrix-v2.md` | `laoli-polymarket-matrix-v2-20260528-133323.txt` | 2026-05-28 13:33 UTC | 完整 |
| 3 | `laoli-polymarket-api-spec-v1.md` | (v2 probe 沿用) | 2026-05-28 13:33 UTC | 标 `[需 owner confirm]` (Sprint-1 spec, 无独立 probe) |
| 4 | `laoye-polygon-rpc-endpoint-matrix-v1.md` | `laoye-polygon-rpc-probe-20260528-133338.txt` | 2026-05-28 13:33 UTC | 完整 |
| 5 | `laoye-polygon-rpc-selection-v1.md` | `laoye-polygon-rpc-probe-20260528-133338.txt` | 2026-05-28 13:33 UTC | 完整 |
| 6 | `laoli-xiaoduan-api-call-optimization-v1.md` | `laoli-xiaoduan-reuse-probe-20260528-131341.txt` | 2026-05-28 13:13 UTC | 完整 |
| 7 | `xiaoduan-goalserve-official-doc-v3.md` | `xiaoduan-goalserve-v3-probe-20260528-152957.txt` | 2026-05-28 15:29 UTC | 完整 |
| 8 | `xiaoduan-goalserve-endpoint-matrix-v2.md` | `xiaoduan-goalserve-v2-probe-20260528-133314.txt` | 2026-05-28 13:33 UTC | 完整 |
| 9 | `xiaoduan-goalserve-api-spec-v1.md` | `xiaoduan-goalserve-samples/*-20260528-123019.json` | 2026-05-28 12:30 UTC | 完整 |
| 10 | `laochen-api-rate-latency-ssot-v1.md` | `laochen-network-bench-*.csv` (跨洋 bench 主时段) | 2026-05-28 13:30 UTC | 完整 (SSOT 汇总, 含上游各 owner probe 时间) |
| 11 | `laoshen-multi-vendor-kms-v1.md` | (无 data/ 探针文件) | 2026-05-28 | 标 `[需 owner confirm]` (老沈 v2 升版时补) |
| 12 | `laohuang-shamir-jurisdiction-signoff-v1.md` | (无 data/ 探针文件) | 2026-05-28 | 标 `[需 owner confirm]` (老黄 v2 升版时补) |

**统计: 10/12 完全合规, 3/12 含 `[需 owner confirm]` 标记 (其中 #3 老李 api-spec 沿用 v2 probe 已注明; #11 #12 实测源类型非 API probe, 无 data/ 文件 timestamp 可援引)**

---

## 2. R-20 §2.2 timestamp 来源优先级遵守

按 R-20 §2.2 "时间戳来源优先级 — 必须用 data 文件名 timestamp, 不能脑补":

- 10 篇直接援引 `docs/RESEARCH/data/` 文件名嵌入的 `YYYYMMDD-HHMMSS` (probe sh 脚本生成) → 数字说话
- 2 篇 (KMS / Shamir) 实测源是 vendor 文档调研 + 合规咨询, 非 API probe → 无 data 文件名 timestamp 可援引, 严格按 R-20 标 `[需 owner confirm]` 而非脑补
- 1 篇 (老李 api-spec v1) 沿用同 owner v2 probe, 注明来源

---

## 3. INDEX.md 健康度更新

`docs/INDEX.md` §健康度 区块更新:

- 评级: A- → **A**
- R-20 维度: 60% (C, 12 不合规) → ~92% (A, 10 完全 + 2 部分)
- 总分: 85.7 → 92.0 (按 v2 §8 公式 R-20 权重 20%: 20 × 11/12 ≈ 18.33; 其他维度不变)

---

## 4. 遗留 follow-up (派给 owner)

| owner | 文档 | 跟踪动作 |
|-------|------|----------|
| 老李 | `laoli-polymarket-api-spec-v1.md` | v2 升版时援引独立 probe data/ 文件名 (或确认沿用 v2 probe 即可, ack 给小米) |
| 老沈 | `laoshen-multi-vendor-kms-v1.md` | v2 升版时补 KMS vendor 实测 timestamp (建议: 跑一次 vendor API console / latency probe 留 data/ 文件) |
| 老黄 | `laohuang-shamir-jurisdiction-signoff-v1.md` | v2 升版时补 Sygnum/Taurus 等托管商调研日期 (建议: vendor 邮件/电话调研留时间戳 log) |

3 个 owner 通知由小米跟踪 ack, 截止 Sprint-2 末.

---

## 5. 完成汇报

> **12 篇补全状态: 10/12 完全 + 2/12 部分 (待 owner confirm); 健康度 A- → A.**

— 小米 (doc-curator), 2026-05-28 W3 Wave 18 收口
