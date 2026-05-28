# GM Decision — Goalserve Odds Gap 应对

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** Decided
- **关联:** `docs/RESEARCH/xiaoduan-goalserve-api-spec-v1.md` §3 odds gap, P0-01 Pinnacle no-vig 信号

---

## 1. 背景

小段实测 Goalserve 7 个 `/getodds/*` endpoint 全部返回 200 + 0 字节，但 `baseball/usa` 字段内含 `@oddsid` 引用 — 说明 Goalserve 内部有数据，**当前 API key 未开 odds 权限**。

无 vendor odds = 无 fair-value anchor。这是 P0。

## 2. GM 决议（双线并进）

### 线 A：商务升级 odds 权限
- **Owner：** 老黄（compliance/商务双视角）+ 老胡（项目跟进）
- **截止：** 2026-06-11
- **动作：**
  1. 联系 Goalserve sales，索取 odds 权限报价 + 路径文档
  2. RPS 上限商务确认（小段实测 1 RPS 安全，正式商务限上限）
  3. 代理 SLA：`127.0.0.1:7890` 产权归属 → 老吴在 6/4 前确认
- **回报：** 6/11 给老雷书面方案 + 价格

### 线 B：ETL 按"无 odds"假设并行推进（不阻塞 Sprint）
- **Owner：** 小余 + 小段
- **回退方案：**
  - inplay 实时 odds **fallback to Polymarket midprice**（不依赖 Goalserve odds）
  - 体育事件流（比分 / 时间 / 球员）仍走 Goalserve（这部分 200 OK）
  - 公平价值 anchor 改走 **Pinnacle no-vig 信号 (P0-01)**，小梁路径 A/B/C 已规划
- **截止：** Sprint-1 末（6/12）ETL 骨架不依赖 odds 字段也能跑通

### 线 C：Pinnacle 数据路径同步推进（解 P0-01 命脉）
- **Owner：** 老李 + 老彭
- **截止：** 6/12 路径决议
- 路径 A：Pinnacle 官方 API（首选）
- 路径 B：The Odds API（$500/月）
- 路径 C：老彭手工 CSV 兜底
- 与 Goalserve odds 互为冗余，不放在同一个篮子

## 3. 不耻下问派单（小段 ETL 必做项 8 条）

| # | 内容 | Owner | 截止 |
|---|---|---|---|
| ETL-1 | 单元素 dict / 多元素 array 兼容 | 小余 | M1 |
| ETL-2 | `@prefix` 字段脱壳 | 小余 | M1 |
| ETL-3 | 拼写硬编码（`shedule`）| 小余 | M1 |
| ETL-4 | 200+0byte 三态区分 | 小余 | M1 |
| ETL-5 | 数值 string → safe int | 小余 | M2 前 |
| ETL-6 | 时区统一 UTC | 小余 | M1 |
| ETL-7 | status 枚举 catch-all | 小余 | M2 前 |
| ETL-8 | 客户端 diff（Goalserve 无 incremental）| 小余 + 老周 | M1 |

## 4. 红线

- Goalserve sales 谈判失败 → 不影响 MVP（Pinnacle 路径 + Polymarket midprice 已足够）
- 但 odds 长期没接入 → 大盘扩展（Spreads / Totals 等多盘口）会受限，进 Sprint-2+ backlog

---

**Decided by 老雷, 2026-05-28**
