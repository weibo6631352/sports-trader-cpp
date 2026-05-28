---
owner: 老胡 (pm-project-manager, E-026)
last_review: 2026-05-29
category: RUNBOOKS
relates_to:
  - docs/INCIDENTS/gm-self-mistakes-log.md (GM 错 #22 复盘范例)
  - CLAUDE.md §7 (公开失败铁律)
  - CLAUDE.md §8 (红线 + 可追溯)
  - docs/SPRINTS/sprint-03-w9-abi-fix-timeline.md (W9 ABI 修复 — R-001 活跃风险)
usage: 每次 P0/P1 事故发生后 4h 内填写基本信息, 24h 内完成 5-Why + 永久 enforcement
---

# Incident Post-mortem Template v1

> 本 template 是所有 P0/P1/P2 事故的标准复盘结构。
> 参考范例: GM 错 #22 (OrderIntent ABI 漏洞, ~4 周未发现).
> 每次事故必须公开, 不捂不等报告写完才说 (CLAUDE.md §7 铁律 #1).
> 维护: 小米 (doc-curator) 归档监督; 事故 owner 填写; 老胡 (PM) 跟进关闭.

---

## 使用方法

1. 事故触发后 **4h 内**: 填写 §1-§3 (基本信息 + 影响 + 初步 timeline)
2. **24h 内**: 完成 §4 (5-Why 根因) + §5 (责任认定)
3. **48h 内**: 完成 §6 (永久 enforcement) + §7 (类似事故避免)
4. **1 周内**: §8 (验证关闭) 确认所有 enforcement 已落地
5. 文件落 `docs/INCIDENTS/<YYYY-MM-DD>-<incident-id>-<brief>.md`, owner + last_review 必填

---

## §1 事故基本信息

```
事故 ID:        INC-XXXX  (格式: INC-<年>-<序号>, e.g. INC-2026-001)
触发日:         YYYY-MM-DD HH:MM (UTC+8)
发现方式:       [ ] 监控自动告警  [ ] 人工发现  [ ] 用户反馈  [ ] 上线后回归
严重度:         [ ] P0 (系统停摆 / 资金安全 / 红线越界)
                [ ] P1 (核心功能降级 / M1 关键路径 delay > 1 周)
                [ ] P2 (非核心降级 / 流程缺口)
状态:           [ ] 检测中  [ ] 响应中  [ ] 缓解中  [ ] 已恢复  [ ] 复盘中  [ ] 已关闭
报告人:         <name> (<role>)
owner:          <name> (<role>)  — 负责推进复盘 + enforcement 落地
```

---

## §2 影响范围

### 2.1 用户 / 业务影响

```
受影响用户/系统:  <描述>
业务中断时间:     <起始> → <恢复>  (Duration: Xh Ym)
资金影响:         [ ] 无  [ ] 有 (金额: $X 或 "待估算")
数据完整性:       [ ] 无损  [ ] 部分损失 (描述: ...)  [ ] 数据污染
合规 / ToS 风险:  [ ] 无  [ ] 有 (描述: ...)
```

### 2.2 系统影响

```
受影响模块:       <模块列表, e.g. RiskManager / OrderIntent / signer_v52>
ABI / schema 影响: [ ] 无  [ ] 有 (字段: ...)
下游依赖污染:     [ ] 无  [ ] 有 (依赖方: ...)
ctest 回归:       <触发前 ctest 数> → <事故后 ctest 数>  (delta: ±N)
```

### 2.3 数据影响

```
4 时间戳契约 (R-20):  [ ] 未违反  [ ] 违反 (描述: ...)
audit log 完整性:      [ ] 完整  [ ] 有缺口 (描述: ...)
WAL 数据:              [ ] 完整  [ ] 有丢失 (量: N 条)
```

---

## §3 事故 Timeline

> 时间精度到分钟. 每行格式: `YYYY-MM-DD HH:MM | 阶段 | 行为描述 | 操作人`

| 时间 (UTC+8) | 阶段 | 描述 | 操作人 |
|---|---|---|---|
| YYYY-MM-DD HH:MM | 发生 | 事故实际发生 (若可回溯) | — |
| YYYY-MM-DD HH:MM | 检测 | 首次发现事故 / 告警触发 | <name> |
| YYYY-MM-DD HH:MM | 响应 | 事故 owner 确认 + incident 发出 | <name> |
| YYYY-MM-DD HH:MM | 缓解 | 临时措施生效 (功能降级 / 回滚 / 隔离) | <name> |
| YYYY-MM-DD HH:MM | 恢复 | 系统全面恢复正常 | <name> |
| YYYY-MM-DD HH:MM | 复盘开始 | 本 post-mortem 文件建立 | <name> |

**检测延迟:** 发生 → 检测 = <X>h  (> 1h 需额外说明原因)
**响应延迟:** 检测 → 响应 = <X>min  (P0 目标: < 2h; P1 目标: < 4h)
**恢复时长:** 响应 → 恢复 = <X>h

---

## §4 根因分析 (5-Why)

> 每个 Why 必须有支撑证据 (commit hash / 文件路径 / log 截图).
> 不允许停在 "人的失误" — 必须追到 **流程 / 工具 / 可见性缺口**.

### 4.1 直接原因 (What happened)

```
现象: <一句话描述 observable failure>
直接原因: <技术层面直接触发的原因>
支撑证据: <commit hash / 文件路径 / log>
```

### 4.2 5-Why 链

```
Why 1: 为什么发生 <直接原因>?
  答: ...
  证据: ...

Why 2: 为什么 <Why 1 答案> 没有被阻止?
  答: ...
  证据: ...

Why 3: 为什么 <Why 2 答案> 存在?
  答: ...
  证据: ...

Why 4: 为什么 <Why 3 答案> 没有被更早发现?
  答: ...
  证据: ...

Why 5: 根本原因 — 流程 / 工具 / 可见性的哪个系统性缺口导致了整个链条?
  答: ...
  对应 CLAUDE.md 条款: §X / ADR-XXX / 红线条款
```

### 4.3 范例 (GM 错 #22 参考)

```
现象: OrderIntent struct 缺少 token_id / outcome / Side::Sell ~4 周未被发现
直接原因: ABI spec 落地时未做 struct 字段全量 review
Why 1: 为什么字段缺失?  → ABI spec 仅口头确认, 无 CI 字段存在检查
Why 2: 为什么无 CI 检查? → abi_lock CI script 在 ABI 问题发现后才立项 (ADR-027 事后)
Why 3: 为什么事后才立项? → GM 派单 prompt 未要求 IC 提交 ABI diff checklist
Why 4: 为什么 4 周未发现? → 无跨单元 ABI 审查机制; FOM 4 人 approve 流程未建立
Why 5: 根本原因 → ADR-027 立项前无"核心数据结构 SSOT enforce"流程
```

---

## §5 责任认定 (不护短原则)

> CLAUDE.md §7 铁律 #1: 公开失败, 不藏问题. 责任认定不是追责, 是找系统缺口.
> 三类责任 (可复选):

### 5.1 流程缺口 (FOM — Failure of Method)

```
缺口描述: <流程 / 规范 / checklist 的哪个环节缺失或失效>
关联条款: CLAUDE.md §X / ADR-XXX / 红线条款
流程 owner: <部门 / 主管>
```

### 5.2 个人判断失误

```
当事人: <name> (<role>)
失误描述: <具体行为, 避免主观评价, 只描述行为和后果>
上下文: <为什么当时做了这个判断 — 用于理解而非辩护>
```

### 5.3 工具 / 可见性缺口

```
缺口描述: <监控 / CI / lint 的哪个覆盖盲区>
是否触发红线: [ ] 否  [ ] 是 (红线条款: ...)
工具 owner: <负责修复的 IC / 顾问>
```

---

## §6 永久 Enforcement

> 每项 enforcement 必须有: 类型 + 负责人 + 完成 ETA + 验证方式.
> 三类 enforcement:

### 6.1 流程固化

| 编号 | 内容 | 负责人 | ETA | 验证方式 |
|---|---|---|---|---|
| ENF-01 | <流程改进描述> | <name> | <YYYY-MM-DD> | <如何验证已执行> |
| ENF-02 | ... | ... | ... | ... |

### 6.2 CI / 工具 Enforcement (grep / lint / hook)

| 编号 | 内容 | 负责人 (CI) | ETA | grep 规则 |
|---|---|---|---|---|
| CI-01 | <CI 脚本 / grep 规则描述> | <name> | <YYYY-MM-DD> | `grep -r "<pattern>" src/` |
| CI-02 | ... | ... | ... | ... |

> CI enforcement 范例 (GM 错 #22):
> - `scripts/abi_lock_v1.7.sh`: grep 检查 `token_id` / `outcome` / `Side::Sell` 在 OrderIntent 定义中
> - cmake custom_target `abi_lock_check` (默认 ON)

### 6.3 KPI 跟踪 (老胡周报永久 §)

| KPI 项目 | 目标值 | 跟踪频率 | 降级条件 (转 Watch) |
|---|---|---|---|
| <KPI 描述> | <目标, e.g. = 0> | 周报 §X | 连续 3 sprint 达标 |

---

## §7 类似事故避免

### 7.1 跨域 Review 触发

> 本次事故如果有下列人员 review 会早发现:

| 角色 | review 内容 | 触发时机 |
|---|---|---|
| <name> (<单元>) | <他们应该 review 什么> | <什么时机应触发> |

### 7.2 SSOT Cross-reference

> 本次事故涉及哪些 SSOT 文件需要同步更新:

| SSOT 文件 | 更新内容 | 负责人 | ETA |
|---|---|---|---|
| <file path> | <更新描述> | <name> | <YYYY-MM-DD> |

### 7.3 ADR 引用

> 本次事故触发或应关联的 ADR:

| ADR | 关联方式 | 状态 |
|---|---|---|
| ADR-XXX | <新立 / 已有需更新 / 应引用> | <active / 待立> |

### 7.4 类似事故 checklist (给未来派单参考)

> 以下 checklist 由本次复盘产出, 加入相关 IC 的派单 prompt 模板:

```
- [ ] <检查项 1>
- [ ] <检查项 2>
- [ ] ...
```

---

## §8 验证关闭

> 事故 1 周后, owner 逐项确认 enforcement 落地.

| 编号 | enforcement | 状态 | 验证时间 | 验证人 |
|---|---|---|---|---|
| ENF-01 | <描述> | [ ] 待完成 / [ ] 已完成 | YYYY-MM-DD | <name> |
| CI-01 | <描述> | [ ] 待完成 / [ ] 已完成 | YYYY-MM-DD | <name> |

**关闭条件:** 所有 enforcement 状态 = 已完成, 且 CI 绿灯确认.

**关闭确认:**
```
关闭人:    <name>
关闭时间:  YYYY-MM-DD
关闭 note: <简短说明>
```

---

## §9 附: 事故严重度定义

| 级别 | 触发条件 | 响应时限 | 必须通知 |
|---|---|---|---|
| P0 | 任何红线 (CLAUDE.md §8) 越界 / 系统停摆 / 资金安全 | 2h 内老雷介入 | 老雷 + 老韩 + 老郭 (任一可单独叫停) |
| P1 | M1 关键路径 delay > 1 周 / 核心功能降级 / ADR 撤回率触发 | 4h 内老胡协调 | 老胡 + 对应单元主管 |
| P2 | 非核心功能降级 / 流程缺口 (无直接业务影响) | 48h 内处理 | 对应单元主管 |

**P0 升级路径 (CLAUDE.md §6):**
当事人 → 单元 owner (4h) → 老胡协调 (48h) → 老雷 (P0 2h 内介入)

**风控叫停权 (P0 即时生效, 无需二次确认):**
老韩 / 老黄 / 老郭 任一均可单独叫停.

---

## §10 相关文档模板路径

| 文档 | 路径 | 说明 |
|---|---|---|
| 事故 log | `docs/INCIDENTS/<date>-<id>-<brief>.md` | 每次事故独立文件 |
| GM 错 log | `docs/INCIDENTS/gm-self-mistakes-log.md` | GM 自承认错累计 |
| 风险 registry | `docs/RESEARCH/laohu-risk-registry-v2.4.md` | 活跃风险跟踪 |
| ADR | `docs/ADR/<date>-adr-<NNN>-<title>.md` | 架构决策记录 |
| 本 template | `docs/RUNBOOKS/incident-postmortem-template-v1.md` | 本文件 |

---

*老胡, 2026-05-29 (Wave 62, post-mortem template v1 首次建立)*
*参考范例: GM 错 #22 复盘 (docs/INCIDENTS/gm-self-mistakes-log.md), 老胡 W8 W4 sprint-02-w8-w5-progress.md §5*
