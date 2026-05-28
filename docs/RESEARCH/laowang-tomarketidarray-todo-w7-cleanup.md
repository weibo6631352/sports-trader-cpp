# ToMarketIdArray dead code 删 + wal_writer 3 TODO W4 转 Sprint backlog

- **owner**: 老王 (persistence-layer IC, 系统工程部)
- **last_review**: 2026-06-28
- **触发**: 老高 D-01 + D-03 审查发现 → GM W6 Wave 32 派回
- **关联**: ADR-009 v2 (Sonnet), GM 错 #11, GM 错 #13

---

## Part 1: ToMarketIdArray dead code 自评

### 1.1 问题溯源

| 时间线 | 事件 |
|---|---|
| W6 Wave 28 | 老王 Position WAL v0.1 落代码, 加 `ToMarketIdArray` helper. 当时 VirtualFill 无 `market_id` 字段, helper 作为 placeholder 供后续使用 |
| W6 Wave 29 | 小蒋在 VirtualFill 加 `market_id: std::array<char,32>` 字段, `_build_record` 改为 `rec.market_id = fill.market_id` 直接透传, `ToMarketIdArray` 从此无调用方 |
| W6 Wave 29 (GM 错 #11) | GM 越权代修: 给 `ToMarketIdArray` 加 `[[maybe_unused]]` 掩盖 dead code warning, 未派老王 cleanup. 正确做法是派 ownership 方 (老王) 删除. |

### 1.2 现状 (老高 D-01 verbatim 确认)

```cpp
// position_ledger.cpp L58-63
[[maybe_unused]] [[nodiscard]] std::array<char, 32>
ToMarketIdArray(std::string_view id) noexcept {
    std::array<char, 32> arr{};
    const std::size_t copy_len = std::min(id.size(), static_cast<std::size_t>(32));
    std::memcpy(arr.data(), id.data(), copy_len);
    return arr;
}
```

- 函数体完整实现但零调用方
- `[[maybe_unused]]` 是 dead code 的语法掩盖而非设计意图
- 测试中如存在 `static_cast<void>(ToMarketIdArray(...))` 形式的 no-op 调用, 系维持覆盖率的假阳性, 一并删除

### 1.3 W7 W1 PR 删除范围

- `position_ledger.cpp` 匿名 namespace 内 `ToMarketIdArray` 函数定义 (L58-63) 整块删
- 扫描 `tests/` 下任何 `static_cast<void>(ToMarketIdArray(...))`  no-op 调用, 一并删
- 扫描整个 `src/` 确认无其他 TU 引用该函数

**预期 diff**: 删除约 7 行 cpp, 其余 0 新增. 不改任何业务逻辑.

---

## Part 2: wal_writer.cpp 3 处 TODO W4 拆解

### 2.1 3 处 TODO W4 实际内容 (逐行确认)

经读取 `/Users/wangweibo/code/sports-trader-cpp/src/stcpp/infra/wal/wal_writer.cpp`:

**TODO W4 #1 — L57, `Open()` 内**

```
// TODO W4: 开 segment fd / 启动 bg jthread / pin cfg.bg_cpu_core / 启 SPSC ring.
// 当前 skeleton: 构造空对象, API 闭环, 让 R-11 / R-20 / API 表面可测.
```

真实工作: segment file open + O_DIRECT flag, bg jthread 启动, CPU core affinity pin (cfg.bg_cpu_core), rigtorp SPSC ring 初始化 (@小石 ADR-017).

**TODO W4 #2 — L83-86, `Append()` 内**

```
// TODO W4: build_frame (header + payload + CRC32C) → ring.try_push.
//  ring 满 → return WalError::Backpressure (老韩 v0.3 #18 AUDIT_WAL_BACKPRESSURE).
//  PerRecord (position) 等 bg fsync 完成才返回.
// 当前 skeleton: 视作 ring 永有空, 直接进 group commit watermark.
```

真实工作: flatbuffers/手写 serialize_into, CRC32C 尾部附加, ring.try_push (SPSC), Backpressure 路径, PerRecord fdatasync 等待 (condvar).

**TODO W4 #3 — L101, `~WalWriter()` 析构注释**

```
// W4: drain ring + 最终 fsync + close fd, 此处 stub.
```

真实工作: jthread join-or-request_stop, ring drain 到空, fdatasync + close(fd).

### 2.2 为何 3 处不应 W7 EOW 完成

- TODO W4 #1 依赖: rigtorp SPSC ring framework (@小石, ADR-017 Sprint-3 W9 才真上线)
- TODO W4 #2 依赖: serialize_into 真实实现 (@老陈 序列化), SPSC ring (#1 先就绪), fdatasync condvar 设计
- TODO W4 #3 依赖: #1 jthread 就绪才有 drain 对象

这 3 处是 WAL group commit + fsync 真实实现的核心骨干, 属于 Sprint-3 纸上实盘数据 ramp 阶段的基础设施. W7 仅 EOW 2 天无法安全完成且无联调对象.

### 2.3 Sprint backlog 拆解

| 编号 | 内容 | 依赖 | 目标 Sprint/Week | 负责人 |
|---|---|---|---|---|
| WAL-B01 | `Open()` 真实实现: segment fd (O_DIRECT), bg jthread, CPU affinity, SPSC ring 初始化 | ADR-017 小石 SPSC framework Sprint-3 W9 就绪 | Sprint-3 W9 | 老王 + 小石联调 |
| WAL-B02 | `Append()` 真实实现: serialize_into + CRC32C + ring.try_push + Backpressure + PerRecord fdatasync condvar | WAL-B01 就绪, 老陈 serialize_into 接口锁定 | Sprint-3 W10 | 老王 |
| WAL-B03 | `~WalWriter()` 真实实现: jthread join/request_stop + ring drain + fdatasync + close(fd) | WAL-B01 jthread 就绪 | Sprint-3 W11 | 老王 |

### 2.4 W7 W1 PR 中 TODO 注释更新

3 处 TODO 注释末尾追加引用行:

```
// Tracked: Sprint-3 W9 WAL-B01 (见 docs/SPRINTS/sprint-03-backlog.md)
// Tracked: Sprint-3 W10 WAL-B02 (见 docs/SPRINTS/sprint-03-backlog.md)
// Tracked: Sprint-3 W11 WAL-B03 (见 docs/SPRINTS/sprint-03-backlog.md)
```

不改功能代码, 纯注释 traceability 补全. diff: 3 行新增注释.

---

## Part 3: W7 实施计划

### 3.1 W7 W1 (7/1) — 老王 PR

| 变更 | 文件 | 类型 | 预期 diff |
|---|---|---|---|
| 删 `ToMarketIdArray` 函数定义 | `position_ledger.cpp` | 删除 | -7 行 |
| 删 no-op 测试调用 (若有) | `tests/unit/test_position_ledger.cpp` | 删除 | -N 行 (扫描确认) |
| TODO W4 #1 注释加 Tracked 引用 | `wal_writer.cpp` L57 区域 | 注释新增 | +1 行 |
| TODO W4 #2 注释加 Tracked 引用 | `wal_writer.cpp` L83-86 区域 | 注释新增 | +1 行 |
| TODO W4 #3 注释加 Tracked 引用 | `wal_writer.cpp` L101 区域 | 注释新增 | +1 行 |

**PR 标题**: `cleanup(wal): delete ToMarketIdArray dead code + track 3 TODO-W4 → Sprint-3 backlog`

**验收 checklist**:
- `grep -rn "ToMarketIdArray" src/ tests/` 零结果
- `grep -rn "maybe_unused.*ToMarket\|ToMarket.*maybe_unused" src/` 零结果
- 3 处 TODO 注释含 `sprint-03-backlog.md` 引用
- `ctest --output-on-failure` 440/440 pass (不破测试)

### 3.2 W7 W2 — 老高 PR v1.4 review

- 老高 `adr010_wno_check.py` grep: 确认无 `[[maybe_unused]]` 掩盖 dead code 残留
- ADR-005 §3.4 文件 ownership: PR commit author = 老王, 非 GM

### 3.3 W7 W3 — GM ack

- 老雷确认 GM 错 #11 已闭环: `[[maybe_unused]]` 掩盖路径已删, 教训入 §4

### 3.4 W7 末 — Sprint-03 backlog 同步

`docs/SPRINTS/sprint-03-backlog.md` 加入 WAL-B01/B02/B03 三条 entry (老胡 Sprint Planning 确认排期).

---

## Part 4: GM 错 #13 配套教训

### 4.1 责任定界

| 事项 | 结论 |
|---|---|
| ToMarketIdArray dead code 产生 | 不是老王错. 原因: W6 Wave 29 小蒋改 VirtualFill 加 `market_id` 字段属 ABI cascade 变更, 应触发下游 dead code audit, 但未执行 |
| `[[maybe_unused]]` 掩盖 | GM 错 #11: GM 越权代修加 `[[maybe_unused]]`, 本应派 ownership 方 (老王) 执行 cleanup |
| W6→W7 跨 wave 未清理 | Dead code 拖了 3 wave (W29→W32), 触发 D-01 P1 |

### 4.2 流程固化 (ABI cascade audit 规则)

当任意 struct 字段新增/删除/改类型 (ABI 变更事件) 时:
1. 变更 owner (本次: 小蒋 VirtualFill +market_id) 必须在 PR description 列出 "下游需 audit" 清单
2. 下游 owner (本次: 老王 position_ledger) 在同 sprint 内完成 dead code 扫描 + 清理
3. 老高 D-01 类问题若出现, 属 cascade audit 流程缺失, 非纯 ownership 失职

### 4.3 ADR-005 §3.4 ownership 确认

- 老王 ACK: `position_ledger.cpp` 文件 ownership 归老王, 任何 cleanup 由老王 PR
- GM 不直接 hotfix ownership 方文件 (例外需提前 ack)
- 老高 PR v1.4 review: 检查 commit author 字段确认不是 GM 代写

---

## Part 5: 不耻下问协作

| 协作方 | 事项 | 优先级 |
|---|---|---|
| 小蒋 | W6 Wave 29 VirtualFill +market_id cascade: 确认是否还有其他下游 dead code 未清理, 协助全量 audit | W7 W1 前 |
| 老高 | PR v1.4 review + `adr010_wno_check.py` grep 协作, 确认 no dead code 掩盖残留 | W7 W2 |
| 小石 | WAL-B01 Sprint-3 W9 SPSC ring framework 接口确认 (ADR-017), 联调时间窗口对齐 | Sprint-3 计划会前 |
| 老胡 | Sprint-03 backlog WAL-B01/B02/B03 排期确认 (Sprint Planning) | W7 末 |
| 老雷 | GM ack: GM 错 #11 闭环确认 + GM 错 #13 教训归档 | W7 W3 |

---

## Part 6: 硬约束确认 (GM 错 #11 验证项)

| 约束 | 状态 |
|---|---|
| 本文档 W7 W1 前为 spec-only, diff 0 cpp | PASS — 本 spec 不含代码修改, 仅规划 |
| ctest 440/440 不破 | 待 W7 W1 PR 后验证, PR checklist 已列 |
| 回汇带 ctest 摘要 + diff 行计 | W7 W1 PR 合并后回汇老高 + 老雷 |
| PR commit author = 老王 (非 GM) | W7 W1 PR 时执行, ADR-005 §3.4 |

---

## 完成汇报摘要

**ToMarketIdArray + 3 TODO W7 cleanup spec + 3 TODO Sprint-3 W9/W10/W11 拆解 + GM 错 #13 教训 + diff 0 cpp**

- `ToMarketIdArray`: dead code 成因已溯源 (W6 Wave 29 小蒋 VirtualFill ABI 变更 cascade 未 audit + GM 错 #11 掩盖), W7 W1 PR 删除约 7 行, 零业务逻辑改动
- `wal_writer.cpp` 3 TODO W4: 已逐一确认内容 (Open fd+ring, Append serialize+fsync, Destructor drain), 拆解为 WAL-B01/B02/B03 进入 Sprint-3 W9/W10/W11, W7 仅加 Tracked 注释引用 (+3 行)
- GM 错 #13 教训: ABI cascade audit 流程规则已提出, 小蒋变更时应列下游清单, 老王同 sprint 内 audit
- diff 0 cpp (本 spec 不动代码)
