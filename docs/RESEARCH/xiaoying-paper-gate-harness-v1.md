# GM-PAPER-G 门禁验收 Harness v1

- **Owner:** 小颖 (requirements-analyst, E 产品业务保障部)
- **Last review:** 2026-05-29
- **Status:** ACTIVE v1
- **任务来源:** E 主管老胡 → 小颖, 基于 ADR-039 §9【新工作流】worktree 交付
- **输入文档 (权威):**
  - `docs/OKR/laolei-2026-drive-directive-paper-profit-v1.md` §3 GM-PAPER-G v2 八条门禁
  - `docs/RESEARCH/xiaoying-acceptance-spec-v2.md` §2 G-00~G-08 criteria (SSOT)
  - `docs/RESEARCH/xiaodong-m45-gate-framework-v1.md` (小董 GateEvaluator C++ impl)
  - `docs/RESEARCH/xiaodong-stats-validation-attestation-spec-v1.md`
  - `/api/v1/gate/paper` 对接字段 (ADR-038 计划中, 本文前置定义字段契约)
- **协作边界:** 本文是验收 SSOT, 不改阈值数字, 不写 PRD, 不跟进 ticket

---

## §0 文档目的

把 GM-PAPER-G 八条门禁翻译为:

1. **可执行断言** — 每条门禁对应明确的 pass/fail 判定逻辑 (输入字段 + 阈值表达式)
2. **四方签字 checklist** — 老韩 (RM) / 小余 (数据) / 小梁 (Sharpe) / 老钱 (盘口) 否决映射
3. **API 字段契约** — 对接 `/api/v1/gate/paper` (ADR-038) 的请求/响应字段定义
4. **可运行脚本** — `tests/integration/paper_gate/` 下 bash harness, 本地可跑通

**不产出:** PRD / ticket / RM 规则文档 / 阈值修改建议

---

## §1 输入数据规范 (paper 30 日运行数据)

harness 消费以下数据文件 (paper runtime 产出, 由小董 `stats_validation_framework` 生成):

```
paper_report_30d.json          # 30 日汇总报表 (主输入)
paper_fills_30d.jsonl          # 每笔 VirtualFill 记录
paper_daily_pnl_30d.json       # 每日净 PnL 序列 (扣 fee+slippage+spread)
attestation_report.md          # 小余签字 attestation (G-08 必要前置)
```

### 1.1 paper_report_30d.json 字段规范

```json
{
  "window_start_ts_ns": 1700000000000000000,
  "window_end_ts_ns":   1702592000000000000,
  "window_days": 30,

  "pregame_moneyline": {
    "n_trades": 142,
    "net_pnl_usdc": 187.43,
    "daily_sharpe_annualized": 0.68,
    "sharpe_bootstrap_ci_lower": 0.21,
    "sharpe_bootstrap_ci_upper": 1.15,
    "sharpe_bootstrap_n": 5000,
    "ttest_pvalue_onesided": 0.047,
    "win_days": 17,
    "total_days": 30,
    "max_daily_loss_pct": 0.021,
    "equity_initial": 10000.0,
    "market_type_labeled_pct": 1.0
  },

  "risk_control": {
    "rm_reject_count": 28,
    "rm_evaluate_count": 170,
    "rm_bypass_count": 0,
    "paper_to_live_crosswrite_count": 0
  },

  "data_attestation": {
    "attestation_signed": true,
    "attestation_signer": "小余",
    "r20_violations": 0,
    "lookahead_violations": 0,
    "feature_join_orphans": 0,
    "data_silent_drops": 0
  },

  "uptime": {
    "uptime_seconds": 2572800,
    "total_window_seconds": 2592000,
    "uptime_pct": 0.9923
  },

  "inplay": {
    "n_trades": 0,
    "note": "inplay 不计入 GM-PAPER-G 核算, 仅透明度展示"
  },

  "as_of_ts_ns": 1702592001000000000
}
```

### 1.2 paper_daily_pnl_30d.json 字段规范

```json
{
  "days": [
    {
      "date_utc": "2026-11-01",
      "net_pnl_usdc": 12.3,
      "equity_eod": 10012.3,
      "n_trades": 5
    }
  ],
  "count": 30
}
```

---

## §2 八条门禁断言定义

每条门禁格式: **输入字段** → **断言表达式** → **pass/fail** → **否决人**

---

### G-00 14 日软验证前置 (gate 进入条件)

**SSOT:** `xiaoying-acceptance-spec-v2.md` §2 G-00

| 字段 | 来源 | 断言 | 否决人 |
|---|---|---|---|
| `window_days` | report | `== 30` (已进入正式窗口, G-00 已过) | — |
| `pregame_moneyline.max_daily_loss_pct` | report | 14 日窗口内 max < 0.03 (软验证时验) | 老韩 |

**断言逻辑:**
```
G00_PASS = (soft_validation_14d_net_pnl > 0.0)
           AND (soft_validation_max_daily_loss_pct < 0.03)
```

注: 软验证 14 日数据为独立前置记录, 不在 `paper_report_30d.json` 内。harness 从 `soft_validation_summary.json` 读取 (若不存在则 G-00=SKIP, 需人工确认)。

---

### G-01 30 日连续窗口

**SSOT:** `xiaoying-acceptance-spec-v2.md` §2 G-01

| 字段 | 来源 | 断言 | 否决人 |
|---|---|---|---|
| `uptime.window_days` | report | `== 30` | — |
| `uptime.uptime_pct` | report | `>= 0.990` | 老吴 (签字) |

**断言逻辑:**
```
G01_PASS = (window_days == 30)
           AND (uptime_pct >= 0.990)
```

---

### G-02 净 PnL > 0 (30 日累计)

**SSOT:** `xiaoying-acceptance-spec-v2.md` §2 G-02

| 字段 | 来源 | 断言 | 否决人 |
|---|---|---|---|
| `pregame_moneyline.net_pnl_usdc` | report | `> 0.0` | 小梁 |
| `data_attestation.attestation_signed` | report | `== true` | 小余 (否决权) |

**断言逻辑:**
```
G02_PASS = (pregame_moneyline.net_pnl_usdc > 0.0)
           AND (data_attestation.attestation_signed == true)
```

**注意:** G-02 依赖 G-08 attestation, 无 attestation → G-02 强制 FAIL (小余否决权)。

---

### G-03 样本量 + bootstrap CI

**SSOT:** `xiaoying-acceptance-spec-v2.md` §2 G-03

| 字段 | 来源 | 断言 | 否决人 |
|---|---|---|---|
| `pregame_moneyline.n_trades` | report | `>= 100` | 小梁 |
| `pregame_moneyline.sharpe_bootstrap_ci_lower` | report | `> 0.0` | 小梁 |
| `pregame_moneyline.ttest_pvalue_onesided` | report | `< 0.10` | 小梁 |
| `pregame_moneyline.sharpe_bootstrap_n` | report | `== 5000` | 小董 (方法确认) |

**断言逻辑:**
```
G03_PASS = (n_trades >= 100)
           AND (sharpe_bootstrap_ci_lower > 0.0)
           AND (ttest_pvalue_onesided < 0.10)
           AND (sharpe_bootstrap_n == 5000)
```

---

### G-04 稳定性 (正收益日 + 单日亏损上限)

**SSOT:** `xiaoying-acceptance-spec-v2.md` §2 G-04

| 字段 | 来源 | 断言 | 否决人 |
|---|---|---|---|
| `pregame_moneyline.win_days` | report | `win_days / total_days >= 0.52` | 小梁 |
| `pregame_moneyline.total_days` | report | `== 30` | — |
| `pregame_moneyline.max_daily_loss_pct` | report | `< 0.03` | 老韩 (RM 主权, 否决权) |

**断言逻辑:**
```
win_rate = win_days / total_days
G04_PASS = (win_rate >= 0.52)
           AND (max_daily_loss_pct < 0.03)
```

**GM §9 裁决 #1:** 单日亏损上限采老韩 3% (非 5%), RM kill switch 在 3% 处触发, 任一触发 → G-04=FAIL + G-ALL=FAIL。

---

### G-05 OOS Sharpe ≥ 0.5 + t-test p < 0.10

**SSOT:** `xiaoying-acceptance-spec-v2.md` §2 G-05

| 字段 | 来源 | 断言 | 否决人 |
|---|---|---|---|
| `pregame_moneyline.daily_sharpe_annualized` | report | `>= 0.5` | 小梁 (唯一签字) |
| `pregame_moneyline.ttest_pvalue_onesided` | report | `< 0.10` | 小梁 |

**断言逻辑:**
```
G05_PASS = (daily_sharpe_annualized >= 0.5)
           AND (ttest_pvalue_onesided < 0.10)
```

**注意:** G-03 和 G-05 共用同一个 t-test p 值字段, 两条 gate 必须同时满足, 不可拆分验收。

---

### G-06 盘口分桶 (pregame Moneyline 单独核算)

**SSOT:** `xiaoying-acceptance-spec-v2.md` §2 G-06

| 字段 | 来源 | 断言 | 否决人 |
|---|---|---|---|
| `pregame_moneyline.net_pnl_usdc` | report | `> 0.0` (单独分桶) | 老钱 (CPO scope 否决权) |
| `pregame_moneyline.market_type_labeled_pct` | report | `== 1.0` (100% 标签覆盖) | 老彭 (确认 inplay 未混入) |
| `inplay.n_trades` in gate calc | report | inplay 不计入上述数字 | 老钱 + 老彭 |

**断言逻辑:**
```
G06_PASS = (pregame_moneyline.net_pnl_usdc > 0.0)      # 单独分桶
           AND (market_type_labeled_pct == 1.0)         # 100% 标签
           AND (inplay_contamination == 0)              # inplay 不计入
```

**inplay_contamination 定义:** G-02/G-03/G-05 使用的数字来自 `pregame_moneyline` 分桶, 不含 `inplay` 分桶数据。harness 用 `pregame_moneyline.n_trades` != `total_n_trades` 时展示 inplay 透明度, 但不影响 gate 判定。

---

### G-07 风控零失效

**SSOT:** `xiaoying-acceptance-spec-v2.md` §2 G-07

| 字段 | 来源 | 断言 | 否决人 |
|---|---|---|---|
| `risk_control.rm_bypass_count` | report | `== 0` | 老韩 (否决权) |
| `risk_control.paper_to_live_crosswrite_count` | report | `== 0` | 老韩 (R-11) |
| `risk_control.rm_reject_count` | report | 计算拒单率 | 老韩 |
| `risk_control.rm_evaluate_count` | report | 计算拒单率 | 老韩 |

**断言逻辑:**
```
reject_rate = rm_reject_count / rm_evaluate_count
G07_PASS = (rm_bypass_count == 0)
           AND (paper_to_live_crosswrite_count == 0)
           AND (reject_rate >= 0.08)
           AND (reject_rate <= 0.20)
```

**GM §9 裁决 #5:** 拒单率区间 [8%, 20%], 老韩 RM 主权。<8% 可能策略边界试探; >20% 过度拒单, 两端均视为异常。

---

### G-08 数据 Attestation

**SSOT:** `xiaoying-acceptance-spec-v2.md` §2 G-08

| 字段 | 来源 | 断言 | 否决人 |
|---|---|---|---|
| `data_attestation.attestation_signed` | report | `== true` | 小余 (否决权, 不签=门禁不通过) |
| `data_attestation.r20_violations` | report | `== 0` | 小余 |
| `data_attestation.lookahead_violations` | report | `== 0` | 小余 |
| `data_attestation.feature_join_orphans` | report | `== 0` | 小余 |
| `data_attestation.data_silent_drops` | report | `== 0` | 小余 |

**断言逻辑:**
```
G08_PASS = (attestation_signed == true)
           AND (r20_violations == 0)
           AND (lookahead_violations == 0)
           AND (feature_join_orphans == 0)
           AND (data_silent_drops == 0)
```

**注意:** G-08 是所有 PnL 数字的前置验证。无 attestation → G-02/G-03/G-05 视为未验证, G-ALL=FAIL。

---

### G-ALL 整体通过判定

**断言逻辑:**
```
G_ALL_PASS = G00_PASS AND G01_PASS AND G02_PASS AND G03_PASS
             AND G04_PASS AND G05_PASS AND G06_PASS AND G07_PASS
             AND G08_PASS
```

**四方否决权 (任一不签 = G-ALL=FAIL):**

| 否决人 | 覆盖 Gate | 否决条件 |
|---|---|---|
| **老韩** (RM 主权) | G-04 (3% 亏损) + G-07 (零失效) | G-04 任一日 >3% OR G-07 任一违规 |
| **小余** (数据 attestation) | G-08 (attestation) | 未签字 OR 4 checklist 任一非零 |
| **小梁** (Sharpe 主权) | G-03 (bootstrap CI) + G-05 (OOS Sharpe) | CI_lower ≤ 0 OR Sharpe < 0.5 OR p ≥ 0.10 |
| **老钱** (盘口 CPO 主权) | G-06 (pregame only scope) | inplay 混入 OR 标签覆盖 < 100% |

---

## §3 /api/v1/gate/paper API 字段契约 (ADR-038 前置定义)

本节为 `/api/v1/gate/paper` endpoint 定义字段契约, 供 ADR-038 正式落地时使用。

### 3.1 GET /api/v1/gate/paper — 查询当前门禁状态

**请求:** 无 body, 可选 query param `?window_id=<30d_window_id>`

**响应字段 (JSON):**

```json
{
  "as_of_ts_ns": 1702592001000000000,
  "window_id": "2026-11-01_2026-11-30",
  "window_days": 30,
  "overall_pass": false,

  "gates": {
    "G00": {
      "pass": true,
      "label": "14d_soft_validation",
      "value_summary": "14d_net_pnl=$45.2, max_daily_loss=1.8%",
      "veto_signer": null
    },
    "G01": {
      "pass": true,
      "label": "30d_window_continuity",
      "uptime_pct": 0.9923,
      "threshold": 0.990,
      "veto_signer": null
    },
    "G02": {
      "pass": true,
      "label": "net_pnl_positive",
      "net_pnl_usdc": 187.43,
      "attestation_required": true,
      "attestation_signed": true,
      "veto_signer": "小余"
    },
    "G03": {
      "pass": true,
      "label": "sample_size_and_bootstrap_ci",
      "n_trades": 142,
      "n_trades_threshold": 100,
      "bootstrap_ci_lower": 0.21,
      "bootstrap_ci_lower_threshold": 0.0,
      "bootstrap_n": 5000,
      "ttest_pvalue": 0.047,
      "ttest_pvalue_threshold": 0.10,
      "veto_signer": "小梁"
    },
    "G04": {
      "pass": true,
      "label": "daily_stability",
      "win_rate": 0.567,
      "win_rate_threshold": 0.52,
      "max_daily_loss_pct": 0.021,
      "max_daily_loss_threshold": 0.03,
      "veto_signer": "老韩"
    },
    "G05": {
      "pass": true,
      "label": "oos_sharpe",
      "sharpe_annualized": 0.68,
      "sharpe_threshold": 0.5,
      "ttest_pvalue": 0.047,
      "ttest_pvalue_threshold": 0.10,
      "veto_signer": "小梁"
    },
    "G06": {
      "pass": true,
      "label": "pregame_bucket_isolation",
      "pregame_net_pnl_usdc": 187.43,
      "market_type_labeled_pct": 1.0,
      "inplay_n_trades_display_only": 0,
      "veto_signer": "老钱"
    },
    "G07": {
      "pass": true,
      "label": "risk_control_zero_failure",
      "rm_bypass_count": 0,
      "paper_crosswrite_count": 0,
      "reject_rate": 0.165,
      "reject_rate_range_low": 0.08,
      "reject_rate_range_high": 0.20,
      "veto_signer": "老韩"
    },
    "G08": {
      "pass": true,
      "label": "data_attestation",
      "attestation_signed": true,
      "attestation_signer": "小余",
      "r20_violations": 0,
      "lookahead_violations": 0,
      "feature_join_orphans": 0,
      "data_silent_drops": 0,
      "veto_signer": "小余"
    }
  },

  "four_party_signoff": {
    "laohan_rm": { "required": true, "signed": false, "covers": ["G04","G07"] },
    "xiaoyu_data": { "required": true, "signed": true, "covers": ["G08"] },
    "xiao_liang_sharpe": { "required": true, "signed": false, "covers": ["G03","G05"] },
    "laoqian_cpo": { "required": true, "signed": false, "covers": ["G06"] }
  },

  "failure_summary": [
    "G_ALL: waiting for four_party_signoff (老韩, 小梁, 老钱)"
  ]
}
```

### 3.2 字段不变量 (harness 验证)

| 不变量 | 表达式 |
|---|---|
| R-20 4ts 单调 | `window_start_ts_ns <= window_end_ts_ns <= as_of_ts_ns` |
| G-02 依赖 G-08 | `G08.attestation_signed == false` → `G02.pass == false` |
| G03/G05 共用 p 值 | `G03.ttest_pvalue == G05.ttest_pvalue` |
| overall_pass 逻辑 | `overall_pass == (G00.pass AND G01.pass AND ... AND G08.pass)` |
| 四方否决一致 | `overall_pass == false` if any `four_party_signoff.*.signed == false` |

---

## §4 四方签字 Checklist

```
GM-PAPER-G 四方签字 Checklist (30 日正式窗口结束后执行)
窗口: ___________  报告文件: paper_report_30d.json (sha256: _______________)
```

### 4.1 老韩 — RM 主权 (G-04 + G-07)

```
[ ] G-04: 查看 max_daily_loss_pct, 确认 < 0.03
[ ] G-04: 查看 win_rate, 确认 >= 0.52
[ ] G-07: 查看 rm_bypass_count == 0 (WAL audit 复核)
[ ] G-07: 查看 paper_to_live_crosswrite_count == 0 (R-11)
[ ] G-07: 查看 reject_rate, 确认在 [0.08, 0.20]
[ ] 签字确认: 老韩 RM 主权无否决事项
日期: ________  签字: ________
```

### 4.2 小余 — 数据 ETL 主权 (G-08)

```
[ ] G-08: 确认 attestation_report.md 已出具 (文件存在 + 本人签字)
[ ] G-08: R-20 violations == 0 (event_ts <= ds_ts <= ingestion_ts <= as_of_ts 全 30 日)
[ ] G-08: lookahead_violations == 0 (signal 在 t 仅用 ds_ts <= t 数据)
[ ] G-08: feature_join_orphans == 0 (100% feature_snapshot_id join)
[ ] G-08: data_silent_drops == 0 (Goalserve + Polymarket 数据源完整)
[ ] 签字确认: 小余数据 attestation 无否决事项
日期: ________  签字: ________
```

### 4.3 小梁 — Sharpe/Kelly 主权 (G-03 + G-05)

```
[ ] G-03: n_trades >= 100 (统计显著基础)
[ ] G-03: sharpe_bootstrap_ci_lower > 0.0 (5000 次 bootstrap)
[ ] G-03: ttest_pvalue_onesided < 0.10
[ ] G-05: daily_sharpe_annualized >= 0.5 (OOS 30 日窗口)
[ ] G-05: ttest_pvalue_onesided < 0.10 (与 G-03 同一计算)
[ ] 统计方法确认: bootstrap 5000 次 percentile method, t-test one-sample one-sided
[ ] 签字确认: 小梁 Sharpe 主权无否决事项
日期: ________  签字: ________
```

### 4.4 老钱 — CPO 盘口主权 (G-06)

```
[ ] G-06: 确认 PnL 核算基于 pregame_moneyline 分桶 (不含 inplay)
[ ] G-06: market_type_labeled_pct == 1.0 (100% fill 有 market_type 标签)
[ ] G-06: pregame_moneyline.net_pnl_usdc > 0.0 (单独分桶正 PnL)
[ ] G-06: inplay.n_trades 仅透明度展示, 不影响 gate 数字
[ ] scope 确认: 2026 = pregame Moneyline only (§8.3 一致结论)
[ ] 签字确认: 老钱 CPO 盘口主权无否决事项
日期: ________  签字: ________
```

### 4.5 GM 最终拍板 (G-ALL)

```
[ ] 四方签字全部完成 (老韩 + 小余 + 小梁 + 老钱)
[ ] harness 脚本跑通 (所有断言 PASS)
[ ] paper_report_30d.json sha256 与四方签字时一致 (防篡改)
[ ] O3 持续盈利门禁通过 → 联决通知老板
日期: ________  GM 老雷签字: ________
       CPO 老钱会签: ________
```

---

## §5 与 /api/v1/gate/paper (ADR-038) 对接说明

ADR-038 尚未创建 (截止 2026-05-29)。本节定义 harness 对接方式, 供 ADR-038 起草时参考。

### 5.1 harness 读取方式 (两种模式)

**模式 A — 文件模式 (当前可用, ADR-038 未就绪时):**
```bash
# harness 直接读 paper_report_30d.json
./tests/integration/paper_gate/run_gate_harness.sh --mode file \
  --report paper_report_30d.json \
  --daily-pnl paper_daily_pnl_30d.json
```

**模式 B — API 模式 (ADR-038 就绪后):**
```bash
# harness 调 GET /api/v1/gate/paper, 把响应字段映射到断言
./tests/integration/paper_gate/run_gate_harness.sh --mode api \
  --endpoint http://localhost:8080/api/v1/gate/paper
```

### 5.2 字段映射表 (文件 → API)

| harness 内部变量 | 文件模式字段路径 | API 响应字段路径 |
|---|---|---|
| `n_trades` | `pregame_moneyline.n_trades` | `gates.G03.n_trades` |
| `net_pnl_usdc` | `pregame_moneyline.net_pnl_usdc` | `gates.G02.net_pnl_usdc` |
| `sharpe` | `pregame_moneyline.daily_sharpe_annualized` | `gates.G05.sharpe_annualized` |
| `ci_lower` | `pregame_moneyline.sharpe_bootstrap_ci_lower` | `gates.G03.bootstrap_ci_lower` |
| `pvalue` | `pregame_moneyline.ttest_pvalue_onesided` | `gates.G03.ttest_pvalue` |
| `win_rate` | `win_days / total_days` | `gates.G04.win_rate` |
| `max_loss_pct` | `pregame_moneyline.max_daily_loss_pct` | `gates.G04.max_daily_loss_pct` |
| `reject_rate` | `rm_reject_count / rm_evaluate_count` | `gates.G07.reject_rate` |
| `rm_bypass` | `risk_control.rm_bypass_count` | `gates.G07.rm_bypass_count` |
| `crosswrite` | `risk_control.paper_to_live_crosswrite_count` | `gates.G07.paper_crosswrite_count` |
| `attestation_signed` | `data_attestation.attestation_signed` | `gates.G08.attestation_signed` |
| `market_labeled_pct` | `pregame_moneyline.market_type_labeled_pct` | `gates.G06.market_type_labeled_pct` |
| `r20_violations` | `data_attestation.r20_violations` | `gates.G08.r20_violations` |
| `uptime_pct` | `uptime.uptime_pct` | `gates.G01.uptime_pct` |

---

## §6 自检 (小颖)

- [x] 八条门禁逐条翻译为断言 (输入字段 + 阈值表达式 + pass/fail 逻辑)
- [x] 所有阈值数字来自 `laolei-2026-drive-directive-paper-profit-v1.md §3 v2`, 未自行修改
- [x] 四方否决权映射完整 (老韩/小余/小梁/老钱 各覆盖哪些 gate)
- [x] G-08 attestation 作为 G-02 前置条件明确写出
- [x] G-03 与 G-05 共用 t-test p 值明确写出 (不可拆分)
- [x] G-07 拒单率 [8%,20%] 两端均触发 FAIL (GM §9 裁决 #5)
- [x] /api/v1/gate/paper 字段契约含 R-20 4ts 字段
- [x] API 字段不变量明确 (G-02 依赖 G-08, overall_pass 逻辑)
- [x] 四方签字 checklist 可操作 (每条可逐项勾选)
- [x] harness 脚本路径定义 (tests/integration/paper_gate/)
- [x] 文件模式 + API 模式两种对接方式
- [x] 不写 PRD / 不写 RM 规则 / 不修改阈值 / 不跟进 ticket

---

**最后更新:** 2026-05-29 by 小颖 (requirements-analyst, E 产品业务保障部)
**下次 review:** ADR-038 起草后 (小卢接 /api/v1/gate/paper endpoint) + 小董 stats_validation_framework 完成后 (paper_report_30d.json schema 最终化)
**升级路径:** 冲突/缺口 → @老胡 (24h ack) → @老雷 (48h 不下升级)
