# xiaosong-adr010-wno-check-grep-spec.md
#
# owner: 小宋 (test-replay, #E-IC)
# last_review: 2026-05-28 (W7 Wave 33)
# 协作: 老高 (pr-reviewer) — 本文是 adr010_wno_check.py v1.4 grep 规则草稿
#        老高 first review 后决定是否并入 PR v1.4 框架

---

## 0. 背景

ADR-010 §4 禁止在任何新 target 的 `target_compile_options` / `add_compile_options`
中新增 `-Wno-*` 抑制。

W7 Wave 33 小宋已删 3 个违规项（wss / cli_three_sig / cli_strategy_unlock）。
老高 W7 W3 PR v1.4 主框架；本文给老高 **grep 规则草稿**，供 `adr010_wno_check.py` 参考。

---

## 1. 扫描目标

```
扫描路径: **/CMakeLists.txt (递归, 从项目根)
排除路径: build/   # build 目录三方库 CMakeLists 不在约束范围
```

---

## 2. 需要找的模式

### 2.1 target_compile_options 里的 -Wno-*

```
正则 (grep -E):
    -Wno-[a-zA-Z0-9_-]+

上下文: 出现在 target_compile_options(...) 或 add_compile_options(...) 块内
每个 target 完整提取: target 名称 + 所在文件 + 行号 + 抑制项列表
```

### 2.2 add_compile_options 全局抑制

```
正则:
    add_compile_options\s*\([^)]*-Wno-[^)]*\)
```

---

## 3. 分类规则 (adr010_wno_check.py 判定逻辑)

### 3.1 ADR-010 §2.2 grandfather 白名单 (PASS)

以下 4 项允许在**第一方 target** 中出现，无需注释：

| 抑制项 | 允许理由 |
|---|---|
| `-Wno-double-promotion` | float/double 混用场景，grandfather |
| `-Wno-old-style-cast` | C 风格 cast，grandfather |
| `-Wno-cast-align` | 平台对齐，grandfather |
| `-Wno-invalid-offsetof` | POD offsetof，grandfather |

判定条件: **仅** 这 4 项，无其他 -Wno-*。

### 3.2 三方库豁免 (PASS with comment check)

以下三方库 CMakeLists.txt 豁免：
- `blake3_official` (BLAKE3)
- `rigtorp_spsc` / `rigtorp_mpmc` (rigtorp SPSCQueue/MPMCQueue)
- `libsodium` (任何 sodium/libsodium FetchContent target)
- `monocypher`
- `googletest` / `gtest`

豁免条件: 文件路径匹配以下任一：
```
**/build/_deps/**
**/third_party/**
**/vendor/**
**/extern/**
```

若三方库 CMakeLists 在项目内（非 FetchContent），则**必须**有注释：
```
# 三方库豁免 (ADR-010 §3.2)
```
否则 FAIL。

### 3.3 任何其他 -Wno-* 项 (FAIL)

不在 grandfather 白名单、不在三方库豁免路径、没有 §3.2 注释的 -Wno-* → FAIL。

---

## 4. 输出格式

### 4.1 JSON (机器可读，供 CI 消费)

```json
{
  "pass": true/false,
  "violations": [
    {
      "file": "src/stcpp/foo/CMakeLists.txt",
      "line": 22,
      "target": "stcpp_foo",
      "wno_flags": ["-Wno-sign-conversion", "-Wno-shadow"],
      "reason": "not in grandfather whitelist, not a third-party target"
    }
  ],
  "grandfather_ok": [
    {
      "file": "src/stcpp/polymarket/wss/CMakeLists.txt",
      "target": "stcpp_polymarket_wss",
      "wno_flags": ["-Wno-double-promotion", "-Wno-old-style-cast", "-Wno-cast-align"]
    }
  ],
  "thirdparty_exempted": [
    {
      "path": "build/_deps/blake3_official-src/CMakeLists.txt",
      "reason": "FetchContent third-party path"
    }
  ]
}
```

### 4.2 Console summary (人类可读)

```
adr010_wno_check.py — ADR-010 §4 -Wno-* audit
================================================
Scanned: 42 CMakeLists.txt files (excluded: 18 in build/_deps/)

PASS  grandfather: 6 targets (double-promotion/old-style-cast/cast-align/invalid-offsetof only)
PASS  third-party exempted: 5 targets
FAIL  violations: 0

Overall: PASS
```

---

## 5. grep 命令参考 (供 Python 脚本内嵌或手工验证)

```bash
# 找所有含 -Wno- 的 CMakeLists.txt (排除 build/)
grep -rn "\-Wno-" --include="CMakeLists.txt" . \
    --exclude-dir=build \
    --exclude-dir=.git

# 验证 wss 已干净 (W7 Wave 33 删后应只剩 3 grandfather)
grep -n "\-Wno-" src/stcpp/polymarket/wss/CMakeLists.txt

# 验证 bin CLI guard 已干净
grep -n "\-Wno-" src/stcpp/bin/CMakeLists.txt
```

---

## 6. 已知现状 (W7 Wave 33 小宋 snapshot, 2026-05-28)

| target | 文件 | 删除的违规项 | 保留 (grandfather) |
|---|---|---|---|
| `stcpp_polymarket_wss` | `src/stcpp/polymarket/wss/CMakeLists.txt` | sign-conversion / conversion / shadow | double-promotion / old-style-cast / cast-align |
| `stcpp_cli_three_sig` | `src/stcpp/bin/CMakeLists.txt` | sign-conversion / conversion / shadow | double-promotion / old-style-cast / cast-align |
| `stcpp_cli_strategy_unlock` | `src/stcpp/bin/CMakeLists.txt` | sign-conversion / conversion / shadow | double-promotion / old-style-cast / cast-align |

wss source (`pm_wss_subscriber.cpp`) 验证：删掉 3 项后 0 warning，source 干净，无需派回小冯。
CLI source (`cli/three_signature.cpp` / `cli/strategy_unlock_cli.cpp`) 在 `STCPP_BUILD_CLI=OFF` 下不参与默认 build，warning 状态待老沈 W7 W2 独立 PR 开 CLI 时确认。

---

## 7. 给老高的协作约定

- 本文是 **grep 规则草稿**，老高 first review 后决定是否并入 `adr010_wno_check.py` v1.4
- §3.1 白名单 4 项是否准确，请老高与 ADR-010 §2.2 原文对齐
- §3.2 三方库路径匹配规则，老高可根据 FetchContent 实际路径调整 glob
- §4 JSON schema 仅建议，老高可按 CI 框架习惯改
- 本文不进 CI 直接使用，只是输入文档；实现在老高的 `.py` 里

---

**last_review:** 2026-05-28 by 小宋 (W7 Wave 33)
