# xiaosong-wno-cleanup-extension-w7-plan.md — ADR-010 §4 -Wno- 清理扩展 W7 计划

- **owner:** 小宋 (#36, test-replay-engineer, E 单元)
- **last_review:** 2026-05-28
- **派单来源:** 老高 H-10 P1 (W6 Wave 32)
- **deadline:** W7 EOW (2026-07-04)
- **关联:** ADR-010 §4 / xiaosong-grandfather-cleanup-v1.md / laogao-pr-review-v1.3.md §2

---

## Part 1: 漏网原因自评

W6 W2 清理扫描范围仅覆盖 `tests/` 全子树，`src/` 只扫了 ADR-010 §4 表格明确列名的两行
(`microstructure` / `ml`)。

| target | 落入时间 | W6 W2 时是否存在 | 漏网原因 |
|---|---|---|---|
| `stcpp_polymarket_wss` | W5 Wave 24 (小冯) | 存在 | ADR-010 §3.2 明确"W6 末确认, W7 处理"——W6 W2 不动是合规 |
| `stcpp_cli_three_sig` | W6 Wave 29 (老沈) | **不存在** | W6 W2 后才加，W6 W3 落地时复制模板带入禁止 flag |
| `stcpp_cli_strategy_unlock` | W6 Wave 29 (老沈) | **不存在** | 同上 |

**结论:** `stcpp_polymarket_wss` 是合规延迟处理；CLI 两个 target 是 W6 W3 新 lib 落地时
未 follow ADR-010 §4 enforcement，且当时无 `adr010_wno_check.py` CI 自动拦截，
属 CI enforcement 空白导致的漏网，不是 W6 W2 清理失职。

**附注 — observability 不在本次范围:** `src/stcpp/observability/CMakeLists.txt` L57-59 的
`-Wno-*` 施加于 `stcpp_blake3` (BLAKE3 官方 C 源码三方库)，符合 ADR-010 §3.2
"三方库引起 → 允许，文档注明来源"，老高 H-10 未列入，不做改动。

---

## Part 2: W7 EOW 清理计划

### 2.1 精确删除点

**`src/stcpp/polymarket/wss/CMakeLists.txt` L22-23:**
```
删: -Wno-sign-conversion -Wno-conversion (L22 后两项)
删: -Wno-shadow (L23 首项)
留: -Wno-double-promotion -Wno-old-style-cast -Wno-cast-align
```

**`src/stcpp/bin/CMakeLists.txt` L57-59 (stcpp_cli_three_sig):**
```
删: -Wno-sign-conversion -Wno-conversion (L58 后两项)
删: -Wno-shadow (L59 首项)
留: -Wno-double-promotion -Wno-old-style-cast -Wno-cast-align
```

**`src/stcpp/bin/CMakeLists.txt` L77-79 (stcpp_cli_strategy_unlock):**
```
删: 同上 (L78 后两项 + L79 首项)
留: 同上
```

保留依据: ADR-010 §2.2 grandfather 白名单:
`-Wno-double-promotion` / `-Wno-old-style-cast` / `-Wno-cast-align`

### 2.2 W7 周计划

| 周 | 日期 | 动作 | 负责人 |
|---|---|---|---|
| W7 W1 | 7/1 | 小宋出 PR: 删 3 处禁止 -Wno-，diff 0 cpp | 小宋 |
| W7 W1 EOD | 7/1 | 问小冯确认 `pm_wss_subscriber.cpp` sign-conversion 真实例；问老沈确认 CLI cpp 同 | 小宋 |
| W7 W2 | 7/2 | 若小冯/老沈反馈有真实 warning → 由其出 source cpp 修 PR；小宋协助 | 小冯/老沈 |
| W7 W2 EOD | 7/2 | 小宋 + 老高协作写 `tests/ci_grep/adr010_wno_check.py` 初稿 | 小宋 + 老高 |
| W7 W3 | 7/3 | 老高 PR v1.4 review: grandfather 4 项保留、禁止项全删、CI job 纳入 | 老高 |
| W7 W4 | 7/4 (EOW) | GM 老雷 ack；回汇附 ctest 摘要 + diff 0 cpp 确认 | 老雷 ack |

---

## Part 3: adr010_wno_check.py CI grep 设计

落地路径: `tests/ci_grep/adr010_wno_check.py`

扫所有 CMakeLists.txt，规则:
- **禁止项 (FAIL):** `-Wno-sign-conversion` / `-Wno-shadow` / `-Wno-conversion` / `-Wno-character-conversion`
- **grandfather 白名单 (PASS):** `-Wno-double-promotion` / `-Wno-old-style-cast` / `-Wno-cast-align` / `-Wno-invalid-offsetof`
- **超出白名单其他 -Wno-\* → WARN** (需走 ADR-010 §5 例外申请)
- 注释行 (`#` 开头) 不计入

CI 集成 (老高 PR v1.4 加 job `ci-grep-adr010-wno`):
```yaml
- run: python3 tests/ci_grep/adr010_wno_check.py --repo .
```

v1.3 已有 10 个 grep 脚本；v1.4 新增此脚本后升为 11 个。

---

## Part 4: GM 错 #11 配套分析

GM 错 #11 根因: 派单未含 cmake+ctest 验证条款，导致老沈 Wave 29 加新 lib 时
未跑 full build verify，复制模板带入禁止 flag 无 CI 拦截。

W7 起双重防护:
1. **派单层:** `build_verification.py` (v1.3 已上) 确保代码任务派单必含 cmake+ctest 条款
2. **CI 层:** `adr010_wno_check.py` (v1.4 待上) 确保任何新增禁止 -Wno-* 直接 FAIL

---

## Part 5: 不耻下问联系人

| 问题 | 找谁 |
|---|---|
| `pm_wss_subscriber.cpp` sign-conversion 真实行 | 小冯 (#34, api-watch-general) |
| `three_signature.cpp` / `strategy_unlock_cli.cpp` shadow/conversion 真实例 | 老沈 (risk-engineer) |
| ADR-010 §2.2 grandfather 解释 + adr010_wno_check.py review | 老高 (#17, code-quality-reviewer) |
| W7 PR v1.4 架构争议仲裁 | 老郭 (#16, arch-review) |
| W7 EOW GM ack | 老雷 (GM) |

---

## Part 6: 交付验收标准 (W7 W4 回汇必带)

1. **diff 0 cpp:** PR 仅改 CMakeLists.txt，source cpp 修复由小冯/老沈独立 PR
2. **ctest 摘要:** 本地全过，0 regression (count ≥ 440)
3. **grep 验证:** `grep -rn "Wno-sign-conversion\|Wno-shadow\|Wno-conversion" src/` → 仅存 observability 三方库行 (豁免)，wss + bin 0 命中
4. **adr010_wno_check.py 初稿:** 与老高协作完成，纳入老高 PR v1.4

---

— 小宋 (#36, test-replay-engineer), 2026-05-28 (W6 Wave 32, ADR-010 §4 扩展 W7)
