# ADR-009: Agent Model 分级 — 管理层 Opus / IC + 顾问 Sonnet

- **ID:** ADR-009
- **Date:** 2026-05-28 (W5 末)
- **Owner:** 老雷 (GM) 拍板
- **Status:** Accepted
- **触发:** 老板 verbatim "开发人员一般情况使用 Sonnet only 模型就够了, 只有管理层以上才默认更高的模型, 避免浪费我的 claude token"

---

## 1. 背景

W3 起 GM 派 50+ sub-agent 全用 Anthropic API default 模型 (Opus 4.7). Token 消耗加速, 老板 W5 末提出成本控制. 这是合理诉求 — IC 写代码 / 写 spec 不需要顶级模型, Sonnet 已足够 (Sonnet 4.6 智能编程能力已达 production grade).

## 2. 决策 (v2, 2026-05-28 老板二次校正)

**v1 错: GM 把"管理层以上默认更高"误解为"管理层默认 Opus". 老板第二次校正:**

> "管理层以上默认 4.7 就够了, 除非很有必要, 一般没必要用 Opus 浪费 token"

**v2 正确: 全员默认 Sonnet (4.6), Opus 仅紧急例外**.

### 2.1 全员默认 Sonnet 4.6

**所有 57 persona** 默认 Sonnet 4.6, 包括:

- GM 老雷 (E-045) — 战略 + 跨域统筹, Sonnet 4.6 完全胜任
- CPO 老钱 (E-015) — 产品方向
- HR Owner 小林 (E-046) — 招聘 + 文化
- 5 主管 (老周/老韩/小梁/小余/老胡)
- F 协调 老郭 (E-016)
- 7 顾问 (老何/老高/小邓/老叶/老徐/小白 + 老张 Inactive)
- 48 IC (含 10 IC pool 小卢)

**v1 → v2 关键变更:**
- ~~9 管理层 Opus / 48 IC+顾问 Sonnet~~
- ✅ **57 全员 Sonnet, Opus 仅例外**

### 2.2 ~~Opus 4.7 (管理层 + 战略层)~~ 撤销

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

## 3. 例外 (临时升级 Opus, 严格收口)

**v2 严格化**: "很有必要" 才升 Opus, 默认从严, 老胡周报 §6 监控.

```
派单 prompt 第一行: "model: opus (例外: <理由>)"

允许的理由 (高阶判断, 不轻易触发):
- "紧急 P0 < 2h 响应 + 涉及多模块协同" (e.g. BUG-W5-001 audit_id UB)
- "重大架构 ADR (跨 ≥ 3 单元 + 不可逆决策)" (e.g. ADR-005 主管 mandate)
- "重大事故指挥 (人/钱/信誉损失)"

不允许的理由 (从严):
- "管理层身份" (v1 错的根因)
- "我觉得 Sonnet 不够好" (无具体证据)
- "这个 task 复杂" (没量化复杂度)
- "防错升 Opus" (派单设计问题, 不是模型问题)
- "撰写 ADR" (除非 ADR-005 量级, 一般 ADR Sonnet 够)
- "主管周同步纪要" (Sonnet 够)
- "代码 review" (Sonnet 够)
```

**老板原话 (verbatim, 二次校正):**
> "管理层以上默认 4.7 就够了, 除非很有必要, 一般没必要用 Opus 浪费 token"

## 4. 派单 enforce (v2)

GM 派 sub-agent 默认全 Sonnet, Opus 严格收口:

```python
# v2 默认派单 (全员 Sonnet)
Agent(
    subagent_type="<任意 persona>",
    model="sonnet",  # ← 默认, 含 GM/CPO/HR/5 主管/F 协调/顾问/IC
    prompt=...
)

# 例外升 Opus (严格审批)
Agent(
    subagent_type="risk-engineer",  # 紧急 P0 才升
    model="opus",
    prompt="model: opus (例外: 紧急 P0 < 2h + 跨多模块, BUG-W5-001 audit_id UB)\n\n...",
)
```

GM 漏传 model → fall-back Sonnet (绝不 default Opus).

**W5 commit `0a9c374` 已派的 Opus sub-agent (Wave 25/26 共 ~10 次)** 是 v1 错的产物, 不撤销 (已花 token), 但 v2 之后停止默认 Opus.

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
