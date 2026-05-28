# GM Sign-off — RM v0.2 待会签项

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** Approved
- **关联:** `docs/RESEARCH/laohan-riskmanager-design-v0.2.md`

---

## 1. RM 参数 GM 拍板

| 参数 | 老韩 v0.2 提案 | GM 决议 | 备注 |
|---|---|---|---|
| `KELLY_FRACTION` | 0.25 (1/4 Kelly) | **批 0.25** | 与小肖 Wave 4 Kelly-slippage 模型一致；M5 后再回看 |
| `DAILY_LOSS_PCT` | 3% | **批 3%** | 起步保守，M5 后回看 |
| `PER_ORDER_CAP` | 待小梁会签 | **小梁 6/4 前给值**，未给前默认 $200/笔硬上限 | 跟小梁市场结构容量评估对齐 |
| 双人 ack 具体是谁 (Q13) | 待定 | **老雷 + 老沈 互为 backup**；任一在线即可单 ack；超额 (>=$5k 单笔 / >=$20k 日累计) 强制双 ack | 与老孙 v2 阈值审批一致 |
| 影子模式时长 (Q12) | 待定 | **2 周**（M4 → M5 之间） | 不达 KR-C-4 立即回炉 |

## 2. 红线再申明

- D-06 30s 是 HALT 不是 WARNING，老韩 v0.2 已锁，未来 ADR 不得弱化
- SAFE_MODE 重启默认（W-3）老周 v0.2 + 老韩 v0.2 双方落地确认
- RM PER_ORDER_CAP 在小梁会签前默认 $200/笔 hardcoded，任何代码绕过 = P0

## 3. 派单

- **小梁** → 6/4 前给 PER_ORDER_CAP 数值（结合 Polymarket 体育市场单笔容量评估）
- **老韩** → 收到本 sign-off 后即可推进 v1.0（实现版），不必等小梁，先用 $200 default
- **小颖** → 把这 5 个参数纳入 MVP 验收标准矩阵
- **老沈** → 双人 ack backup 角色，准备 24/7 标准化响应

---

**Approved by 老雷, 2026-05-28**
