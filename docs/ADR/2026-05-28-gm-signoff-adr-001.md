# GM Sign-off — ADR-001 (架构 v0.1 + RM v0.1 评审)

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** Approved with W-6 资金批
- **关联:** `2026-05-28-arch-and-rm-v0.1-review.md`

---

## 1. GM 决议

**老郭 ADR-001 全文接受。** 整改清单（老周 C-Z1..C-Z7 / 老韩 C-H1..C-H6）按计划 Sprint-1 末出 v0.2，hard block（W-2 / W-3 / C-H1）不到位不允许进 Sprint-2。

## 2. W-1..W-8 拍板

| 项 | 决议 | 备注 |
|---|---|---|
| W-1 ~ W-5, W-7, W-8 | **Accept (知情确认)** | 老郭论证充分，无异议 |
| **W-6** 实例规格 c6i.large → c6i.xlarge (+$100/月) | **批** | 公司"无限资金"已批，老吴 S1-025 deploy 直接走 xlarge；月度成本表入老胡甘特图 |

## 3. 行动派单

- **老周** → v0.2 修 C-Z1..C-Z7（最迟 Sprint-1 末）
- **老韩** → v0.2 修 C-H1..C-H6，重点 STALE 阈值（WSS 2s/10s, Goalserve 5s/15s, 对账 10s/30s）
- **老吴** → S1-010 部署方案改成 c6i.xlarge，重发 v0.2
- **小郑** → S1-018 SLO 表对齐 STALE 新阈值
- **小米** → docs/INDEX 加 ADR-001 + 本签收链接

## 4. 红线再申明

- W-3 SAFE_MODE 重启：进程重启后默认进入只读 / 不下单状态，运维显式 unlock 才恢复交易。这是公司红线，任何下次 ADR 不得弱化。
- W-2 RiskGateway link 阻断：策略层禁止链接 `exec/*.o`，CI 静态扫描强制。

---

**Approved by 老雷, 2026-05-28**
