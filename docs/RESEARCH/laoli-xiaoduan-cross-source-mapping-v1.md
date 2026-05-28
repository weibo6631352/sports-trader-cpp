# 跨源 Mapping v1 — Goalserve ↔ Polymarket

- **Owner**: 老李 (#07, Polymarket 协议) + 小段 (#37, Goalserve SSOT, cross-check 联动)
- **Date**: 2026-05-29
- **Last review**: 2026-05-29
- **Sprint**: W9 Wave 63 P1 (小米 audit §4 缺失 doc 补交)
- **ADR-027 cite**:
  ```
  cite:
    - polymarket_ssot: docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md (§2, §3)
    - goalserve_ssot:  docs/RESEARCH/xiaoduan-goalserve-data-structure-ssot-v1.md   (§7)
  ```
- **关联文档**:
  - `docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md` §2 (三层结构)
  - `docs/RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md` §7 (与 Polymarket mapping)
  - `docs/RESEARCH/laopeng-w9-inplay-edge-gross-net-confirm.md` (老彭 inplay alpha v2 edge 确认)
  - `docs/RESEARCH/laopeng-w8-oq-p02-3-inplay-single-source-ack.md` (bet365 单源 ack)

**派单小段 W9 W2 cross-check ack**: 本文 §4 mapping 逻辑需小段在 W9 W2 前确认 inplay-pregame mapping 5 sport 双向索引覆盖情况, 及 Goalserve game_id ↔ Polymarket event.gameId 实测状态.

---

## §1 Goalserve event_id ↔ Polymarket condition_id lookup

### §1.1 ID 体系

| 层 | Goalserve 字段 | Polymarket 字段 | 格式 |
|---|---|---|---|
| 赛事/比赛 | `pregame_match_id` (6 位 int, e.g. `310218`) | `event.gameId` (string, 可能含前导 0) | 两者通过 event.gameId 关联 |
| 赛事/比赛 (实时) | `inplay_match_id` (134xxxxxxx, 9 位) | — (无直接对应) | 须经 inplay-pregame mapping 转换 |
| 条件/盘口 | — | `condition_id` (bytes32 hex, 0x + 64char) | Polymarket 独有, Goalserve 无对应 |

### §1.2 两步 lookup 流程

```
Step 1 — inplay_match_id → pregame_match_id
  (仅需 inplay 数据时)
  URL: www.goalserve.com/getfeed/<KEY>/soccernew/inplay-mapping?json=1
  response: { @pregame_match_id, @inplay_match_id, @pregame_team1_id, @inplay_team1_id (name!) }
  注: inplay team id 是字符串名称, pregame team id 是数字, 需名称 fuzzy match 补偿

Step 2 — pregame_match_id → Polymarket condition_id
  join key: Polymarket event.gameId == Goalserve pregame_match_id (string 比较)
  数据来源: gamma /events?tag_id=1&... → event.gameId 字段
  结果: 找到 event → 遍历 event.markets[] → 取对应盘口 condition_id
```

**已知陷阱**:
- `event.gameId` 在 Polymarket 侧不保证 100% 填充; 部分 event 此字段为空 → 需 fallback 到球队名 + 开赛时间 fuzzy match
- 小段 W9 W2 需实测 gameId 填充率 (目标 ≥ 90%, 低于则触发 fallback)

### §1.3 5 sport mapping endpoint (小段 SSOT §7.1)

| Sport | inplay-pregame mapping URL |
|---|---|
| Soccer | `getfeed/<KEY>/soccernew/inplay-mapping?json=1` |
| Tennis | `getfeed/<KEY>/tennis_scores/inplay-mapping?json=1` |
| Baseball | `getfeed/<KEY>/baseball/inplay-mapping?json=1` |
| Esports | `getfeed/<KEY>/esports/inplay-mapping?json=1` |
| Basketball | `getfeed/<KEY>/bsktbl/inplay-mapping?json=1` (basketball/ 路径实测 500, 用 bsktbl/) |

MVP 阶段 (W9) 优先实现 Soccer + Basketball. 其余 3 sport W10+ 补.

---

## §2 Goalserve outcome ↔ Polymarket outcome ↔ token_id 三层 lookup

```
Goalserve odds                    Mapping Layer                  Polymarket CLOB
--------------                    -------------                  ---------------
inplay_match_id (134180558)
  + market_id (e.g. 1 = 1X2)
  + participant name ("Home")
         |
         | Step 1: inplay_match_id → pregame_match_id (§1.2)
         v
  pregame_match_id (310218)
         |
         | Step 2: pregame_match_id → event.gameId → event → markets[]
         v
  condition_id (0xa9db...)          [market 级, bytes32 hex]
         |
         | Step 3: outcome name → token index
         v
  clobTokenIds[i] → token_id       [outcome 级, uint256 decimal string]
         |
         v
  Polymarket CLOB /book?token_id=<token_id>
  或 WSS market channel assets_ids: ["<token_id>"]
```

**Step 3 detail — outcome name → token index**:

| Goalserve outcome | Polymarket clobTokenIds index | 规则 |
|---|---|---|
| `"Home"` / `"Player1"` / `"Team A"` / `"Yes"` | `clobTokenIds[0]` (outcomes[0]) | **outcomeIndex 对齐规则** (SSOT §2.4): outcomes[i] ↔ clobTokenIds[i], 不可用字符串硬匹配 |
| `"Away"` / `"Player2"` / `"Team B"` / `"No"` | `clobTokenIds[1]` (outcomes[1]) | 同上 |
| `"Draw"` | 独立 condition — 见 §4 | Soccer 3-way 特殊 |

**outcomeIndex 对齐警示**: Polymarket outcomes[0] 不保证是 "Yes" 或 "Home". 字面值取决于 market 类型. 必须以 index 对齐, 不能以字符串做硬匹配. 见 SSOT §2.4.

---

## §3 价格单位转换

| 来源 | 字段 | 单位 | 转换公式 |
|---|---|---|---|
| Goalserve inplay | `value_eu` (string, e.g. `"8.5"`) | 欧赔 (decimal odds, 含 bookmaker margin) | `implied_p = 1.0 / value_eu` |
| Goalserve pregame | `bookmaker[@value]` (string, 9 家) | 同上 | 同上; 取 9 家均值去 vig |
| Polymarket CLOB | `price` (float 0~1) | 概率 (已 de-vig 由市场) | 直接用 |
| Polymarket WSS | `mid_bps` (uint32) | bps (basis points, 1/10000) | `mid_price = mid_bps / 10000.0` |

**de-vig (去 bookmaker 边际) 公式**:

```
binary (basketball / tennis / MLB / hockey 60min):
  implied_p_home = 1 / odds_home
  implied_p_away = 1 / odds_away
  fair_p_home = implied_p_home / (implied_p_home + implied_p_away)
  fair_p_away = 1 - fair_p_home

3-way multiplicative (soccer 1x2, ADR-008):
  overround = implied_p_home + implied_p_draw + implied_p_away
  fair_p_home = implied_p_home / overround
  fair_p_draw = implied_p_draw / overround
  fair_p_away = implied_p_away / overround
  constraint: fair_p_home + fair_p_draw + fair_p_away = 1.0
```

**edge 计算**:
```
edge = fair_p - polymarket_mid_price
signal_triggered = abs(edge) > C2_threshold
```

**老彭 alpha v2 确认 (OQ-P02-3, 2026-05-29)**:
- inplay 单源 bet365 de-vig: 1.5-2.5% gross edge (扣 bet365 bias 后, **未扣 Polymarket 3% taker fee**)
- net edge = gross edge - 3% fee - ~0.3% slippage = 可能为负
- C2 门槛: ≥ 6¢ (等效 gross edge > 4%) 才有正净期望
- 见 `laopeng-w9-inplay-edge-gross-net-confirm.md` §2 for G3 KR 调整建议

---

## §4 3-way Soccer — Polymarket binary condition mapping

**问题**: Goalserve soccer 1x2 = 3 outcomes (Home/Draw/Away). Polymarket 以**独立 binary condition** 表达每个 outcome.

**标准拆法** (老李 实测确认, Polymarket 体育 soccer 主流结构):

| Goalserve outcome | Polymarket condition | Polymarket token |
|---|---|---|
| `"Home"` (odds_home) | `"Will [Home Team] win [Match]?"` | YES token (`clobTokenIds[0]`) |
| `"Draw"` (odds_draw) | `"Will [Match] end in a draw?"` | YES token (`clobTokenIds[0]`) |
| `"Away"` (odds_away) | `"Will [Away Team] win [Match]?"` | YES token (`clobTokenIds[0]`) |

三个 condition 各自独立. 每个都是 2-token (YES/NO) binary market. 映射层需:
1. 用球队名 + 赛事信息匹配 3 个 condition_id (各自独立 lookup)
2. 对每个 condition 单独维护 orderbook + signal

**约束** (小段 SSOT §4.2): `fair_p_home + fair_p_draw + fair_p_away ≈ 1.0` de-vig 后. 若 Polymarket 只开了 Home/Away 两个 condition (不开 Draw), 则:
```
fair_p_home_binary = p_home / (p_home + p_away)
fair_p_away_binary = p_away / (p_home + p_away)
```

**工程 FairValue ABI 要求** (SSOT §6 gap): FairValue struct 必须支持 per-outcome 三值 (Home/Draw/Away 各一个 fair_p), 单值 scalar 语义不清. 老周 W8 W5 ABI 修订.

---

## §5 老彭 inplay 单 bet365 alpha v2 mapping

**老彭 W8 OQ-P02-3 ack + W9 gross/net confirm 要点**:

| 维度 | 规格 | 来源 |
|---|---|---|
| inplay 数据源 | bet365 单源 (Goalserve inplay feed `bm="bet365"`, 结构性单源) | 小段 SSOT §3.2 |
| de-vig 方法 | 单家 multiplicative + C2 门槛上调 (ADR-008 §5 例外条款) | 老彭 OQ-P02-3 §3.2 |
| gross edge | 1.5-2.5% (扣 bet365 bias 0.7-1.2pp 后) | 老彭 W9 confirm §1 |
| net edge | gross - 3% fee - 0.3% slippage ≈ -1.8% to -0.8% 中位 | 同上 §1 |
| C2 门槛 | ≥ 6¢ → gross edge > 4% → net > 1% 有正期望 | 同上 §2.2 方案二 |
| G3 KR 建议 | hit rate 升至 56-58% 或加 C2 ≥ 6¢ 门禁 | 同上 §2.1/§2.2 |

**Mapping 对应**: bet365 inplay feed → `value_eu` → `implied_p` → 单家 multiplicative de-vig → `fair_p` → 与 Polymarket CLOB mid_price 比较 → edge. 路径同 §3, 但 inplay 不能用多源 de-vig (结构性单源).

---

## §6 不耻下问 / 待确认

| 问谁 | 问题 | 截止 |
|---|---|---|
| @小段 | Goalserve `event.gameId` ↔ Polymarket `pregame_match_id` 实测填充率是多少? Basketball bsktbl/inplay-mapping?json=1 现在 OK 了吗? | W9 W2 |
| @小段 | 5 sport inplay-pregame mapping 端点双向索引完成度? | W9 W2 |
| @老周 | FairValue struct 是否已支持 per-outcome 三值 (soccer 3-way)? W8 W5 ABI 修订状态? | W9 W1 ack |
| @老彭 | C2 门槛 ≥ 6¢ 与 G3 KR hit 56% 两方案老钱选哪个? 影响本 mapping 中的 edge 过滤逻辑 | W9 W2 |

---

**最后更新**: 2026-05-29 by 老李 (W9 Wave 63 P1, 小米 audit §4 补交) | 联动小段 cross-check W9 W2 ack
