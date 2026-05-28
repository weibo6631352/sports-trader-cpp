# ADR-009: Agent Model 分级 — 管理层 Opus / IC + 顾问 Sonnet

- **ID:** ADR-009
- **Date:** 2026-05-28 (W5 末)
- **Owner:** 老雷 (GM) 拍板
- **Status:** Accepted
- **触发:** 老板 verbatim "开发人员一般情况使用 Sonnet only 模型就够了, 只有管理层以上才默认更高的模型, 避免浪费我的 claude token"

---

## 1. 背景

W3 起 GM 派 50+ sub-agent 全用 Anthropic API default 模型 (Opus 4.7). Token 消耗加速, 老板 W5 末提出成本控制. 这是合理诉求 — IC 写代码 / 写 spec 不需要顶级模型, Sonnet 已足够 (Sonnet 4.6 智能编程能力已达 production grade).

## 2. 决策

按职位分两级:

### 2.1 Opus 4.7 (管理层 + 战略层)

| 角色 | Persona | 工号 | 理由 |
|---|---|---|---|
| GM | 老雷 | E-045 | 公司战略 + 跨域统筹 + 错误兜底 |
| CPO | 老钱 | E-015 | 产品方向 + 业务能力 + 大原则 |
| HR Owner | 小林 | E-046 | 招聘 SOP + 文化健康度 + 跨部门 |
| A 主管 | 老周 | E-001 | 架构主权 + 25 人单元统筹 |
| B 主管 | 老韩 | E-009 | RM 主权 + 红线 enforce |
| C 主管 | 小梁 | E-018 | 量化战略 + Kelly / Sharpe / VaR |
| D 主管 | 小余 | E-022 | 数据基建 + ETL spec |
| E 主管 | 老胡 | E-026 | PM + Sprint + 协商主持 |
| F 协调 | 老郭 | E-016 | 架构评审 + ADR 仲裁 + 跨顾问协调 |

共 **9 persona** 默认 Opus.

### 2.2 Sonnet only (IC + 顾问)

其余 **48 persona** (包括 10 IC pool 小卢) 默认 Sonnet 4.6:

- A 单元 IC: 小马 #02 / 老陈 #03 / 小赵 #04 / 老王 #05 / 老孙 #06 / 老李 #07 / 小田 #08 / 老吴 #10 / 小郑 #11 / 老姜 #39 / 小石 #41 / 小肖 #42 / 小颜 #43 / 小卢 × 10 #35
- B 单元 IC: 老沈 #27 / 老黄 #29 / 老唐 #38
- C 单元 IC: 小程 #19 / 小蒋 #20 / 小袁 #21 / 老彭 #30
- D 单元 IC: 小董 #23 / 小田 #24 (兼 A) / 小段 #37 / 小冯 #34
- E 单元 IC: 小颖 #25 / 小杜 #36 / 小宋 #28 / 小苏 #12 / 小米 #40 / 小尤 #47 / 小宫 #48
- F 顾问: 老张 #13 / 老何 #14 / 老高 #17 / 小邓 #31 / 老叶 #32 / 老徐 #33 / 小白 #44

**为什么顾问也 Sonnet?**
- 顾问 = advisor, 不是 line manager (ADR-005 §2.2 老郭就职宣言区分)
- 顾问职责是"提建议 + 评审", 不是统筹团队
- 老高 PR review / 老徐 R-39 escalate 这类工作 Sonnet 完全胜任

## 3. 例外 (临时升级)

允许临时升 Opus, 但派单 prompt **显式标注** + 理由:

```
派单 prompt 第一行: "model: opus (例外: <理由>)"

允许的理由:
- "紧急 P0 < 2h 响应" (e.g. BUG-W5-001, 修复 audit_id UB)
- "复杂战略 ADR 撰写" (e.g. ADR-005 主管 mandate)
- "跨多单元复杂仲裁" (e.g. ADR-004 liquidity vs cap)
- "GM 错系列复盘 + enforcement 设计" (e.g. 错 #4-9 任一)

不允许的理由:
- "我觉得 Sonnet 不够好" (无具体证据)
- "这个 task 复杂" (没量化复杂度)
- "防错升 Opus" (派单设计问题, 不是模型问题)
```

## 4. 派单 enforce

GM 派 sub-agent 时 Agent tool 必传 `model` 参数:

```python
# IC 派单 (Sonnet)
Agent(
    subagent_type="polymarket-protocol-expert",  # 老李 IC
    model="sonnet",  # ← 必传
    prompt=...
)

# 主管派单 (Opus, 默认 OK 但建议显式)
Agent(
    subagent_type="cpp-chief-architect",  # 老周 主管
    model="opus",  # ← 显式
    prompt=...
)

# 临时升 (例外)
Agent(
    subagent_type="risk-engineer",  # 紧急 P0 升 Opus
    model="opus",  # ← 显式
    prompt="model: opus (例外: 紧急 P0 < 2h, BUG-W5-001 audit_id UB)\n\n...",
)
```

GM 漏传 model 参数 → fall-back Sonnet (而非 default Opus), 避免无意识用 Opus.

## 5. CI grep enforce (老高 PR review v1.2)

老高 W6 在 `.github/workflows/pr.yml` 加 grep:
- 派单 prompt grep `model: opus` 必须紧跟"例外:" 段
- 派单 prompt grep IC persona name (e.g. polymarket-protocol-expert) 严禁直接 default model

## 6. KPI 监控

老胡周报新增段 (与错 #9 SSOT 版本演进段并列):

**§6 Model 分级 KPI**
- 本周 IC 派单 Sonnet 覆盖率 (目标 100%, 例外 < 5%)
- 本周 Opus 例外申请次数 + 理由
- 月度 Anthropic token 消耗对比 (vs 前月)

## 7. 不变量

- 任何 sub-agent 拒接派单的能力**不受模型影响** (Sonnet IC 仍可拒接越界, ADR-005 §3.3 #5)
- 任何 P0 红线 (RM / 私钥 / R-11 / R-12 / R-20) **不受模型影响** (Sonnet 同样 enforce)
- 9 管理层 Opus persona 是 list 上限, 后续不允许"自动晋升" (HC 新主管走 HR 评估)

## 8. 落地动作

- [x] ADR-009 立 (本文件)
- [x] CLAUDE.md §10 加 model 分级条款 + 用户原话 verbatim
- [ ] 老高 W6 PR review v1.2 加 model grep
- [ ] 老胡周报 §6 Model 分级 KPI 模板 (W5 末交)
- [ ] employee-registry.md 加 "Default Model" 列 (W5 末小林 update)

## 9. 用户原话 verbatim 入约束

> "开发人员一般情况使用 Sonnet only 模型就够了, 只有管理层以上才默认更高的模型, 避免浪费我的 claude token"

任何 GM 派单违反本 ADR (无理由用 Opus 派 IC) = GM 错累计 +1 (入 INCIDENTS log).

---

**最后更新:** 2026-05-28 by 老雷 (GM)
