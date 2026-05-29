# escalation-inbox.md — 员工主动上报收件箱

- **owner:** 老郭 (F 协调人, 顾问团)
- **co-维护:** 老胡 (PM, 周报 §3 数据源) + 小米 (doc-curator, 归档监督)
- **last_review:** 2026-05-29
- **触发:** ADR-030 §5 (Wave 83 P0, 老板 verbatim 2026-05-29)
- **关联:**
  - `docs/ADR/2026-06-W4-adr-030-bottom-up-escalation.md` (本文件治理规则来源)
  - `docs/META/escalate-decision-log.md` (sub-agent 拒接决议, 并行不替代)

---

## 填报规则 (ADR-030 §5)

1. **Append-only** — 只加行, 不删不改历史记录 (小米监督)
2. **三项必填** — 问题描述 + 影响 + 建议 (缺任一项老胡打回)
3. **主管 24h ack SLA** — 超时视为默认同意, 自动升 GM
4. **上报人责任** — 填报后不等 ack 就继续执行 = 仍需在 PR body 写 PUSH_BACK 区段
5. **状态字段说明:**
   - `open` — 已上报, 等主管 ack
   - `ack-主管` — 主管已 ack, 等处理
   - `ack-GM` — 升 GM, 等 GM ack
   - `ack-总裁` — 升总裁
   - `backlog` — 已入 Sprint backlog (老胡 PM 跟进)
   - `ADR` — 已触发 ADR 修订
   - `closed-accepted` — 问题已处理, 关闭
   - `closed-rejected` — 经书面说明, 无需处理, 关闭

---

## 上报表格

| ID | 日期 | Wave | 上报人 | 类别 | 问题描述 | 影响 | 建议 | 状态 | 主管 ack (24h) | GM ack (48h) |
|----|------|------|--------|------|----------|------|------|------|----------------|--------------|
| E-001 | 2026-05-29 | W9 W3 | 老沈 | 派单 prompt 错 | cpp wave 越 persona 语言纪律边界, 要求风控模块写 C++ 以外语言 | 若执行将违反语言纪律红线, 产出物不可上生产 | 转老孙 (系统工程 A 单元) 处理 | closed-accepted | 老周 ack | N/A |
| E-002 | 2026-05-29 | W9 W3 | 小段 | 制度问题 (依赖未就绪) | Goalserve 队名 mapping + delta_ts 字段 + 小冯下游依赖三项未就绪, 跨源 mapping 无法执行 | 强行执行产出废弃代码, 浪费 sprint 资源 | 先梳理依赖链, 调整排期到依赖就绪后 | closed-accepted | 小余 ack | N/A |
| E-003 | 2026-05-29 | W9 W3 | 老郭 | 逻辑问题 (接口契约不一致) | ADR-027 主审发现 FOM 代理方案与老周 layer 1 架构存在接口契约冲突 | 若不处理将导致 SSOT enforce 流程执行时出现歧义, ADR-027 无法落地 | 老周补充 layer 1 接口约束说明后再确认 ADR-027 | ADR | 老周 ack | N/A |
| E-004 | 2026-05-29 | W9 W2 | 老孙 | 逻辑问题 (spec 矛盾) | sigType=1 与 v5.1 spec 填写冲突, HMAC bug #2 根因 | 若不修正 signer 实盘下单将持续失败 | CI grep 加固 sigType 检查 + ADR 修订 spec 说明 | closed-accepted | 老周 ack | 老雷 ack |
| E-005 | 2026-05-29 | W9 W4 | 老沈 | 制度问题 (owner 边界) | PR #3 follow-up CI fail 责任归属不清, 被要求揽责他人遗留问题 | owner 边界模糊将导致后续 PR 验收无人负责 | 明确 PR fail 归属规则: 遗留 fail 由原提交人负责 | closed-accepted | 老韩 ack | N/A |

---

## KPI 追踪 (老胡 PM 每周更新)

| 指标 | 本周 | 累计 | 目标 |
|------|------|------|------|
| K-B1: 上报总数 | 5 (历史导入) | 5 | — |
| K-B2: 主管 24h ack 达成率 | 100% | 100% | ≥ 95% |
| K-B3: GM 48h ack 达成率 | 100% | 100% | ≥ 95% |
| K-B4: 入 backlog / ADR 转化率 | 40% (2/5) | 40% | ≥ 30% |
| K-B5: PUSH_BACK 区段覆盖率 | N/A (W9 W5 前宽限) | N/A | W10 W4 后 ≥ 100% |

_上次更新: 2026-05-29 by 老郭 (初始化, 历史案例导入)_
