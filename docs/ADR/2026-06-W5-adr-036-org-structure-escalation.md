---
name: adr-036-org-structure-escalation
description: 组织架构双向上报 — 员工/HR/部门发现组织问题主动上报 (老板 5/29 verbatim)
owner: P-00 (总裁草案) → 老郭 W10 W4 主审
last_review: 2026-05-29
status: Draft
metadata:
  type: ADR
  id: ADR-036
---

# ADR-036: 组织架构双向上报制度

- **ID:** ADR-036
- **Date:** 2026-05-29 (W10 W3)
- **Status:** Draft (总裁 P-00 草案 → 老郭 W10 W4 主审)
- **触发:** 老板 5/29 verbatim

---

## §1 老板 verbatim

> "公司组织架构遇到问题也要及时和人事部或对应部门沟通, 他们有问题也需要上报"

→ ADR-030 (员工主动上报 PUSH_BACK) 的**双向扩展**: 不仅个人, **HR / 部门**也是 escalation 接收方 + 发起方.

## §2 与 ADR-030 关系

| ADR | 上报方 | 接收方 |
|---|---|---|
| ADR-030 | 个人员工 | 主管 → GM → 总裁 → 老板 |
| **ADR-036 (本)** | **员工 + HR + 部门主管** | **HR + 对应部门 + GM + 总裁** (双向) |

ADR-030 是垂直 (员工→上级). ADR-036 是网状 (任何节点发现 → 通知对应部门).

## §3 上报范围 (组织架构 6 类)

1. **班底配置问题**:
   - persona 角色重叠 (e.g. 老姜 #39 perf vs E-060 perf-test, 小林 W103 push back 已实证)
   - persona 边界混乱 (e.g. 老沈 W9 W3 + W10 W2 拒 cpp 但 W9 W2 + W9 W4 写过 cpp)
   - owner 缺位 (e.g. 数据结构 IC 8/1 入职前缺位)
   - persona 间任务冲突 (跨单元抢资源)

2. **单元 Idle / 过载**:
   - Idle (W8-W9 C 单元 4/5 闲, 老板批评 → ADR-031 4 必要条件)
   - 过载 (单 owner 同期 ≥ 3 P0 wave)
   - 跨单元失衡 (一单元过载 + 另单元闲)

3. **跨部门协作问题**:
   - FOM (ADR-005 §3.4) 跨域 review 漏 (e.g. GM 错 #22 OrderIntent token_id 缺)
   - 接口契约不一致 (需求-工程 ADR-005 §3.2 协商不下)

4. **流程缺口**:
   - 派单 prompt 模板不全 (e.g. 老板多次补充 sync main / conflict 处理)
   - SOP 不清晰 (ADR-034 多次修订)

5. **HR 进度问题**:
   - 招聘 delay (e.g. 数据结构 IC 8/1 入职到岗风险)
   - onboarding 缺陷 (员工知识盲点)
   - 离职 / 角色调整 (Rust 退场后老张角色重定)

6. **沟通 / 决策延迟**:
   - 主管 24h ack SLA 漏
   - GM / 总裁 拍板延迟

## §4 上报路径 (双向网状)

### 4.1 员工 / IC 发现

```
员工 → 直接主管 (24h ack)
     → HR (小林, 同时 cc, 平行)
     → GM (老雷 / 我) — 主管/HR 不下 48h 升级
     → 总裁 P-00 — 不下 72h 升级
     → 老板 (retainer 期 周报通道)
```

### 4.2 HR / 主管 发现 (新加)

```
HR 小林 → 对应部门主管 (24h ack)
        → GM (cc)
        → 总裁 P-00 (周报)

部门主管 → HR 小林 (班底问题, 24h ack)
         → GM (cc 跨单元)
         → 总裁 (跨部门冲突)
```

### 4.3 GM / 总裁 发现 (top-down)

```
GM 老雷 → HR + 对应主管 (派 wave 解决)
        → 总裁 (升级架构变更)

总裁 P-00 → 老板 (周报 / 紧急 PushNotification)
          → 副总裁 P-01 (7/1 入职后, 互相监督)
```

## §5 escalation-inbox 扩展 (老郭 W10 W4)

`docs/META/escalation-inbox.md` (ADR-030 立) 加 4 节:
- §A (原) 员工主动上报 (ADR-030)
- §B (新) HR 上报 (小林 owner)
- §C (新) 部门主管上报 (5 主管)
- §D (新) GM / 总裁 网状上报

记录格式:
```
[YYYY-MM-DD] [发起方 persona] [接收方] [问题 (6 类之一)] [建议] [状态]
e.g. [2026-05-29] [小林 HR] [老雷 GM] [老姜 #39 vs E-060 角色重叠] [不合并, 各保留] [待 GM ack]
```

## §6 实施 (W10 W4 起)

- W10 W4 Mon: 老郭 主审 → ACCEPTED
- W10 W4 Tue: 老胡 在周报模板加 "§11 组织架构 escalation 数" KPI
- W10 W4 Wed: 小林 HR 立 §B 上报实例 (老姜 vs E-060 + 老沈 cpp 边界 + Idle C 单元 audit)
- W10 W4 Thu: 5 主管 W10 W4 周会传达 §C
- W10 W5: 老高 加 grep `org_escalation_check.py` (周报含 §11 数据)

## §7 历史 push back 实例 (本 ADR 入约束)

- 老沈 W9 W3 + W10 W2 cpp 边界拒 → 个人 PUSH_BACK ✓ (ADR-030)
- 小林 W103 老姜 vs E-060 角色合并争议 → HR PUSH_BACK ✓ (本 ADR-036 §B)
- 老胡 W84 audit Idle 70% → 部门主管 PUSH_BACK ✓ (本 ADR-036 §C)
- 老郭 W88 ADR-027 主审 layer 1 老周 代理 → F 协调 PUSH_BACK ✓ (跨范围)
- 总裁 W129 派老孙 V62 transformer cpp 接老沈拒 → top-down 调整 ✓

## §8 总裁 P-00 PUSH_BACK 自检

我作为总裁 W8-W10 漏:
- W8-W9 C 单元 4/5 Idle 我没及时 HR ping 调岗
- W10 W2 老沈拒 cpp 我没立刻 HR cc (小林 W10 W3 才知)
- ADR-030 立时只想了员工→上级, 没想 HR 双向

ADR-036 是 self-correction.

## §9 不耻下问

- @老郭 W10 W4 主审 ACCEPTED
- @小林 HR §B 实例 + 上报 inbox 维护
- @老胡 PM 周报 §11 KPI
- @老高 W10 W5 grep
- @5 主管 W10 W4 周会传达

---

**最后更新:** 2026-05-29 by 总裁 P-00 (草案)
