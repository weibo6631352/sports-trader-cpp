# 里程碑进度评估 W7 末

- **Owner:** 老胡 (pm-project-manager, E-026)
- **评估日:** 2026-06-W3 末 (Sprint-2 W7 冷静周, Wave 34)
- **评估基准:** 小颖 acceptance spec v1 M1 38 条 + M4.5 17 条 + M5 13 条
- **关联:**
  - `docs/RESEARCH/xiaoying-acceptance-spec-v1.md` (验收 SSOT)
  - `docs/SPRINTS/sprint-03-backlog.md` (WAL-B01/02/03)
  - `docs/ADR/2026-06-W3-adr-020-ic-tester-separation.md`
  - `docs/ADR/2026-06-W3-adr-021-worktree-isolation.md`
- **时间消耗:** T+0 起 25% (W7 末 / T+6 月目标)

---

## 1. M1 MVP 进度 (T+6 月, 2026-11, acceptance 38 条)

**当前: 24/38 = 63%**

| 分组 | 条数 | 完成 | 进度 | 关键缺口 |
|---|---|---|---|---|
| **M1-A 风控有效性** | 5 | **5** | 100% ✓ | — |
| **M1-B 数据接入** | 5 | **4** | 80% | M1-B05 PM book vs Goalserve 时序对齐 (ADR-012 paper 1×8 暂保留) |
| **M1-C 信号生成** | 5 | **4** | 80% | M1-C01 P0-02 cpp (小卢 W8 W2 实施) |
| **M1-D 订单执行 paper** | 5 | **3** | 60% | M1-D03 PM CLOB 真 submit (live mode M5+); M1-D01 paper e2e p99 <50ms 未测 |
| **M1-E 复盘能力** | 3 | **3** | 100% ✓ | — |
| **M1-F 系统健康** | 4 | **2** | 50% | M1-F02 WAL fsync p99 <1ms (WAL-B01/B02 Sprint-3 W9/W10); M1-F04 R-12 违例 0 (Sim 未跑) |
| **M1-G 紧急操作** | 3 | **3** | 100% ✓ | — |
| **M1-H PnL 看板** | 8 | **0** | 0% | UI 全未启动; 小苏 + 小宫 Sprint-3 启动 |

**合计: 24/38 = 63%**

### M1 关键路径 (W7 → M1 达标)

1. **P0-02 cpp** — 小卢 W8 W2 (M1-C01 前置)
2. **WAL-B01/B02/B03** — 老王 Sprint-3 W9/W10/W11 (M1-F02 + paper runtime)
3. **paper runtime 真启动** — Sprint-3 W11 (M1-D01/D02/D03/D04)
4. **PnL 看板 M1-H** — Sprint-3 启动 + Sprint-4 (0/8, 最大欠账)

---

## 2. M2 Sharpe Gate 进度 (T+8 月, 8/6 OOS Sharpe gate) — ~40%

| 条件 | 状态 | 说明 |
|---|---|---|
| P0-01 信号框架 ✓ | 完成 | PinnacleNoVig spec 落码, de-vig 老彭校准 ✓ |
| ADR-008 de-vig 算法 ✓ | 完成 | W5 末拍板 Goalserve fair value 路径 |
| 老彭 8 bookmaker 历史回填 ✓ | 完成 | ~750 万行 offline (Wave 29) |
| P0-02 cpp 实施 | 未完成 | 小卢 W8 W2 (OQ-P02-3 ack 后) |
| paper runtime 数据 (OOS Sharpe) | 未完成 | 需 paper runtime 真跑 ≥2 周; Sprint-3 W11+ |
| 小蒋 backtest framework cpp | 部分 | v0.2 cpp skeleton 已落, 需 WAL-B 接入 |

**估算 40%: 基础框架 + 算法定好, 缺实测数据.**

---

## 3. M4.5 paper 2 周 7 gate 进度 (~10%)

| 条件 | 状态 |
|---|---|
| gate evaluator framework (小董 W6) | 完成 (m45_gate_evaluator.py + stats validation) |
| ML shadow hook (小邓 ADR-014) | 完成 (32 feature hook 框架) |
| paper runtime 0 跑 | 未完成 (Sprint-3 W11 目标首跑) |
| 7 gate 通过 (G1-G7) | 未完成 (需连续 2 周 paper 跑) |
| 在线率 ≥99.5% 验证 | 未完成 |

**估算 10%: framework 全就绪, 0 paper runtime 真跑.**

---

## 4. M5 live 首笔成交 (~5%)

| 条件 | 状态 |
|---|---|
| live stub 框架 | 完成 (ADR-011 paper/live binary, 老周 v0.7) |
| libsodium signer 基础 | 老孙 W8 W1 落 (FetchContent) |
| PM CLOB 真 submit | 未完成 (M5+ 路径, live mode) |
| M4.5 7 gate 通过 | 未完成 (M5 硬前置) |

**估算 5%: live stub 只在, 签名 W8 落, CLOB 真下单路径远.**

---

## 5. 北极星 (T+36 月, 单策略年化 PnL ≥$5M, Sharpe ≥1.5, DD ≤15%) — 0%

**尚未进入实盘阶段, 0% 属正常.**

---

## 6. 关键观察

| 观察 | 数据 | 结论 |
|---|---|---|
| 时间消耗 vs 工作进度 | 时间 25% / 工作 M1 63% | 进度领先 ~38pp — 超前 ✓ |
| 最大欠账模块 | M1-H PnL 看板 0/8 | Sprint-3 必须启动 UI 路径 |
| paper runtime 是 gate | M2/M4.5/M5 全依赖 | WAL-B01/B02/B03 是关键路径 |
| ADR-020 Tester 影响 | W8 起 IC 不自测 | Tester 产能 (小宋 + HC-08) 成瓶颈; HC-08 8/15 入职时机敏感 |
| libsodium W8 | 老孙 W8 W1 | M5 路径前置; W9 前必须 ctest 通过 |

---

## 7. Sprint-3 关键 milestone

| Sprint-3 Week | 里程碑 | 影响 M 段 |
|---|---|---|
| W9 (6/29) | WAL-B01 merge; Sprint Planning 完成 | M1-F02 前置 |
| W10 (7/06) | WAL-B02 merge; 端到端联调起步 | M1-D01/D04 前置 |
| W11 (7/13) | WAL-B03 + paper runtime 真启动 | M1-D 全段 + M2 gate 数据起步 |

**Sprint-3 W11 = paper runtime 首跑, 是 M2/M4.5/M5 的真正起点.**

---

**最后更新:** 2026-06-W3 by 老胡 (E-026, PM, Wave 34)
