# 2026-06-01 主管周同步 input — B 风控合规部 status v1

- **Owner:** 老韩 (E-009, risk-engineer, B 单元 Manager)
- **会议:** 6/01 (Mon) 主管周同步 (老雷主持, 5 主管 + 老钱 + 老郭)
- **本档:** B 单元 W5 末 status + W6 启动决议 + ADR 候选表态 + HC 进度
- **关联:** `laohan-manager-mandate-v1.md` (我就职宣言) / W5 Wave 24 commit `3ab5dfb` / ADR-004 / BUG-W5-001 closeout / `sprint-02.md` W5 派单
- **Last review:** 2026-05-28

---

## §1 B 单元 W5 末 status

| Ticket | IC | 状态 | 备注 |
|---|---|---|---|
| W5-B-01 ADR-004 patch RM cpp (position_caps 前移 liquidity) | 老沈 | ✓ 落地 commit `3ab5dfb` | 82 行 + 1 regression test (`EvaluatePriority_PositionCapBeforeLiquidity`) |
| W5-B-02 audit emitter 真 BLAKE3 + replay verifier | 老唐 | TBD W5 末 → W6 | 老唐 W4 audit emitter v0.1 XOR stub 已就位, W5 末撤 stub 接真 BLAKE3 + 写 replay verifier invariant |
| W5-B-03 R-7 build-time switch 联调 (paper / live 路径分流验证) | 老沈 + 老唐 | TBD W5 末 | 老李 W5 Wave 24 已物理隔离 paper / live build target, 等 audit ts_t 双路径 e2e |
| W5-B-04 INVALID_INTENT enum 17 终版表 (21 enum + 9 sub_reason 统一 patch) | 我 (老韩) | TBD W6 | enum spec 不是 cpp, 我自己出 (mandate N1/N4 不冲突) |
| **紧急 BUG-W5-001 P0 patch** (next_audit_id UB shift 72) | 老沈 | ✓ 落地 commit `3ab5dfb` | 97 行 + CI build-release-ubsan job + 14 RM release 模式恢复 |

**W5 末 B 单元产出:** 老沈 179 行 cpp (82 ADR-004 + 97 BUG-W5-001) + 2 PR + 14 RM release 模式 pre-existing fail 全部恢复. **老唐 W5-B-02 / W5-B-03 进度待老唐 1:1 input (我不替他承诺时间).**

---

## §2 first review 结果 (我 B 主管 first review, 当日 ack)

### 2.1 老沈 ADR-004 patch first review

- ✓ `check_position_caps_` 前移至 `check_liquidity_` 之前, 顺序按 ADR-004 B 选项落地
- ✓ ABI 0 改动 (RiskDecision struct + RiskGateway public 签名)
- ✓ enum 数值不动 (21 RejectCode + 9 sub_reason)
- ✓ 5 文件 patch + 1 regression test (`EvaluatePriority_PositionCapBeforeLiquidity`)
- ✓ R-1 / R-7 / R-11 / R-20 全保留 (paper / live build-time switch 不破)
- ✓ p50 -30~100ns 优化 (position cap 命中场景短路前移), p99 不变
- **B 主管 ack: PASS, 上 PR, 升老郭 + 老高 二审会签**

### 2.2 老沈 BUG-W5-001 P0 patch first review

- ✓ `next_audit_id` seq 10B → 6B big-endian (shift 0~40 < 48, 远离 64-bit UB)
- ✓ `out[12..15]` 4 byte randomness 占位 (M5 老孙 ULID rand 接)
- ✓ shift 表达式加 precedence 括号 (`(seq >> n) & 0xFF`, 防 `>> n & 0xFF` 误读)
- ✓ 加 `AuditId.NonZero_O2` 1000 次循环测试 (非零 + 唯一性 + 时序单调三 invariant)
- ✓ CI 加 `build-release-ubsan` job (Release -O2 + UBSAN `halt_on_error=1`)
- ✓ 撤销小宋 W5 integration test 内 TODO marker (release 模式 14 fail 全恢复, 312/312 PASS)
- **B 主管 ack: PASS, 上 PR, P0 走加急通道升老郭 + 老高 + GM 4 会签**

---

## §3 跨主管 ASK 进度 (W5 mandate §5 — 6/01 EOD deadline)

| ASK | 对方主管 | 状态 | 备注 |
|---|---|---|---|
| 老周 (A): RM v0.6 PREGAME_FAR 15000 取齐 + 3 接口 (SlippageModel / WALWriter / IntakeQueue) 稳定 | 老周 | **待老周 input** | 6/01 同步会上当面 ack |
| 小梁 (C): signal output schema (signal_id / strategy_id / signal_ts / confidence / source_market) 字段终版 | 小梁 | **待小梁 input** | RM ingestion 端依赖, W6 前必须 closeout |
| 老胡 (E): integration test fixture 60s + chain verify (replay verifier invariant) | 老胡 | ✓ 小宋 v0.1 (14/14 PASS, e2e p99 3.9us) | 闭环, 感谢小宋 |
| 老郭 (F): ADR-004 + ADR-003 final closeout | 老郭 | ✓ ADR-004 老沈 patch 落地 + ADR-003 closeout 老郭已签 | 闭环 |

**4 ASK 进度 2 ack / 2 在途, 6/01 同步会当面催老周 + 小梁.**

---

## §4 W5 自评 update (mandate §6 KPI)

**核心承诺: W5 起 cpp lines = 0 严格守住 ✓** — ADR-004 + BUG-W5-001 全派老沈实施, 我 0 行 cpp.

| KPI 项 | W4 | W5 | 备注 |
|---|---|---|---|
| 拆任务 (W5 派单完整度) | 4/5 | **5/5** | W5-B-01~04 + BUG-W5-001 全拆全派 |
| 排队 (优先级管理) | 4/5 | **5/5** | P0 BUG-W5-001 当日插队老沈, ADR-004 不延后 |
| review (first review 当日 ack) | 4/5 | **5/5** | 2 first review 当日 PASS |
| 跨单元 (4 ASK ack) | 3/5 | **4/5** | 4 ASK 中 2 ack, 2 在途 |
| 1:1 (老沈 / 老唐 bi-weekly) | 3/5 | **4/5** | 周一 1:1 启动 (老沈 W5-B-01/05, 老唐 W5-B-02) |
| 不亲力亲为 (cpp lines) | 3/5 (B+, W4 还有 evaluate spec 倾向写代码) | **5/5 (W5 cpp = 0)** | **关键提升, mandate N4 第 6 条死守** |

**综合自评:** W4 B+ → **W5 A-** (cpp = 0 一项升 1 级).

---

## §5 待 GM 拍板事项

### 5.1 closeout 4 会签待签

- **ADR-004 closeout 4 会签** (老韩 ✓ / 老郭 ✓ / 老高 / GM)
- **BUG-W5-001 closeout 4 会签** (老韩 ✓ / 老郭 / 老高 / GM)
- **INVALID_INTENT enum 17 vs 19 终版表 W6 patch 上线时机** — 我建议 W6 中段 (与小梁 signal output schema 同步上线, 一次 ABI 不破 patch)

### 5.2 W5 candidate ADR 3 个 — 我 (B 主管) 表态

| ADR 候选 | 我 (B) 表态 | 理由 |
|---|---|---|
| **ADR-006 HTTP client** | **不动产 (B 不直接用 HTTP, 老周 A 拍)** | B 单元 RM 不直接发起 HTTP, signer (老孙) / clob client (老周单元) 才需要. 我不越权 |
| **ADR-007 VirtualMatcher 切 Mode A** | **建议 paper E2E 跑通后再切 (与老郭一致)** | 当前 Mode B 已稳, 切 Mode A 前必须 paper 24h 联调 + audit chain verify. 强切风险 R-11 paper / live 串污 |
| **~~ADR-008 Pinnacle~~ → 改 Goalserve de-vig** | **建议 multiplicative de-vig (~30 行简化版), 不上 Shin** | 风控视角不希望 Shin 150 行黑盒 (M4.5 后老沈 / 老唐 audit 复杂度爆). multiplicative 一阶近似 RM 可 inline review, Shin 留作 v2 升级路径. **此为我建议, 由小梁 (C) + 老郭 (F) 最终拍** |

### 5.3 mandate §1 不替别人说话

- 老沈 W5-B-02 / W5-B-03 计划由老沈 6/01 1:1 当面 ack
- 老唐 W6 真 BLAKE3 落地节奏由老唐 6/01 1:1 当面 ack
- 老黄 M4.5 后激活时间 (Sygnum / Taurus 联系) 由老黄当面 input

---

## §6 W6 启动决议 (B 单元内部, 我已拆)

| W6 ticket | Owner | 交付 | 截止 |
|---|---|---|---|
| W5-B-02 续 (audit emitter 真 BLAKE3 + replay verifier) | 老唐 | XOR stub → BLAKE3 + replay invariant cpp 落地 | W6 中 (待 1:1 ack) |
| W5-B-03 续 (R-7 build-time switch 端到端联调) | 老沈 + 老唐 | paper / live build target × audit ts_t 双路径 e2e 联调报告 | W6 末 (待 1:1 ack) |
| W5-B-04 INVALID_INTENT enum 终版表 (我自己, enum spec 不是 cpp, 不破 N4) | 我 (老韩) | 21 enum + 9 sub_reason 终版表 + W6 patch 上线时机 spec | W6 中 |
| 老黄 W5 持续 3/10 低载, M4.5 后激活 | 老黄 | (M4.5 后再起 Sygnum / Taurus 联系) | 不强加 |

---

## §7 HC 进度 (mandate §8 + HR 联动)

- **HC-08 risk-quant 节奏:** Q3 末 / Q4 初 (M4.5 倒推, multi-signal 上线后 RM enforce 复杂度 + de-vig audit + STRATEGY_DECAYED 实施需要 risk-quant 分担, 我个人 7/10 黄会升 8+ 红)
- **老黄 W5 仍 3/10 低载, 不申请 P2 compliance 提前** — M4.5 后 vendor ToS 系统化梳理 (Polymarket / Goalserve / Polygon / vendor KMS) 再 evaluate
- **小林 6/03 我 1:1 议程:** 同步 HC-08 risk-quant **提前到 Q3 末** vs **维持 Q3 中** (取决于 M4.5 paper engine 9/12 首判窗口结果, 若延后则 HC-08 同步推)

---

## §8 不耻下问 (CLAUDE.md §3 第 4 条铁律)

- **@老周 (A):** RM v0.6 PREGAME_FAR 15000 取齐 + 3 接口稳定 6/01 EOD 是否还能赶上? 老沈 W5-B-03 联调依赖
- **@小梁 (C):** signal output schema (signal_id / strategy_id / signal_ts / confidence / source_market) 字段终版 W6 前能否 closeout? RM ingestion 端依赖, INVALID_INTENT enum 终版表也要同步引用
- **@老郭 (F):** ADR-008 Goalserve de-vig multiplicative vs Shin, 你架构评审视角倾向? (我建议 multiplicative, 但你 F 顾问团有否决权)
- **@老雷 (GM):** ADR-004 + BUG-W5-001 closeout 4 会签 GM 一签何时? P0 BUG 建议加急走
- **@小林 (HR):** HC-08 risk-quant Q3 末提前 evaluate, 6/03 1:1 我带 M4.5 倒推材料过来

---

## §9 完成汇报

**B 单元 W5 末 status + 2 first review PASS + cpp = 0 守住 KPI 升 A- + ADR-008 我表态 multiplicative de-vig.**

- W5 末 B 单元代码 179 行 (全老沈实施) + 2 patch closeout 在途
- 我 W5 cpp = 0 严格守住, 主管 KPI 自评 W4 B+ → W5 A-
- 跨主管 4 ASK: 2 ack (老胡 + 老郭), 2 在途 (老周 + 小梁), 6/01 同步会当面催
- W6 启动: 老唐 audit 真 BLAKE3 + 老沈 R-7 联调 + 我 INVALID_INTENT enum 终版表 (老黄 M4.5 后)
- ADR 候选表态: ADR-006 不动产 / ADR-007 paper E2E 后再切 / ADR-008 multiplicative de-vig
- HC: HC-08 risk-quant Q3 末提前 evaluate, 6/03 小林 1:1 议程

**完。**
