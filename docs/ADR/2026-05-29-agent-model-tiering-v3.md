# ADR-009 v3: Agent Model 分级 — 管理层+顾问 Opus 4.8 / IC Sonnet 4.6

- **ID:** ADR-009 (v3, supersedes v2)
- **Date:** 2026-05-29
- **Owner:** 老雷 (GM) 拍板, 老板直接指令
- **Status:** Accepted
- **前序:** [`2026-05-28-agent-model-tiering.md`](2026-05-28-agent-model-tiering.md) (v1/v2)
- **触发 (老板 verbatim 2026-05-29):** "所有管理层以上员工用 claude 4.8 模型, 以下员工用 claude 4.7。"

---

## 1. 背景

v2 (2026-05-28) 立场是"全员默认 Sonnet 4.6, Opus 仅紧急例外", 核心诉求是省 claude token。
老板 2026-05-29 直接推翻该省 token 立场, 改为按层级分档:

> "所有管理层以上员工用 claude 4.8 模型, 以下员工用 claude 4.7。"

随后两点口头校正 (本会话 confirm):
1. **IC 档 4.7 → 4.6**: 老板"还是 4.6 吧"（工具侧无 4.7 别名, IC 落 Sonnet 4.6）
2. **顾问层归档**: 老板定"顾问算管理层 (4.8)"（v1/v2 曾把顾问与 IC 归一档, v3 上调顾问到 Opus 档）

## 2. 决策 (v3)

### 2.1 管理层档 → Opus 4.8 (`model: opus`) — 16 persona

**9 管理层:**

| persona file | 名 | 角色 |
|---|---|---|
| 45-professional-manager | 老雷 | GM |
| 15-cpo-product-strategy | 老钱 | CPO |
| 46-hr-talent-manager | 小林 | HR Owner |
| 01-cpp-chief-architect | 老周 | A 主管 (架构主权) |
| 09-risk-engineer | 老韩 | B 主管 (RM 主权) |
| 18-financial-expert | 小梁 | C 主管 (Sharpe/Kelly 主权) |
| 22-data-etl | 小余 | D 主管 (ETL 主权) |
| 26-pm-project-manager | 老胡 | E 主管 (PM milestone 主权) |
| 16-chief-architecture-reviewer | 老郭 | F 协调 (架构否决权) |

**7 顾问 (老板 2026-05-29 上调至 4.8):**

| persona file | 名 | 领域 |
|---|---|---|
| 13-rust-advisor | 老张 | Rust 顾问 (Inactive) |
| 14-modern-cpp-advisor | 老何 | 现代 C++ |
| 17-code-quality-reviewer | 老高 | 代码质量评审 |
| 31-ml-engineer | 小邓 | ML 训练 |
| 32-defi-onchain-advisor | 老叶 | 链上 DeFi |
| 33-ai-ops-collaboration | 老徐 | AI Ops |
| 44-ai-llm-advisor | 小白 | AI/LLM 顾问 |

### 2.2 IC 档 → Sonnet 4.6 (`model: sonnet`) — 32 persona

其余全部 IC（含 10 IC pool 小卢 #35），落 Sonnet 4.6。
（02/03/04/05/06/07/08/10/11/12/19/20/21/23/24/25/27/28/29/30/34/35/36/37/38/39/40/41/42/43/47/48）

## 3. 落地 (本 ADR 已执行)

- [x] 48 个 persona file frontmatter 写死 `model:` 字段 (16 opus / 32 sonnet)
- [x] CLAUDE.md §10 Model 分级改写为 v3 + 老板 verbatim
- [x] 本 ADR (v3) 立, 标 supersede v2
- [ ] employee-registry.md "Default Model" 列同步 v3 (小林 next)
- [ ] 老高 PR review grep: v2 的"IC 严禁 default model"规则作废, 改 grep frontmatter `model:` 字段完整性

## 3.5 IC 复杂工作酌情升 4.8 (老板 2026-05-29 三次补充)

> **老板 verbatim:** "如果非管理人员接收到的是复杂工作, 可看情况给他使用 claude4.8 模型。"

IC 默认 Sonnet 4.6 (frontmatter), 但派单方 (GM / 主管) 判断 task **确属复杂**时可酌情临时升 Opus 4.8:

**典型复杂场景 (示例, 非穷举):**
- 跨模块设计 / ABI 字段冻结契约 (e.g. 老沈 RM v0.5 field-freeze)
- 数值算法 / 概率统计 spec (e.g. 小程 alpha v2 + bootstrap CI / 小董 attestation 功效分析)
- 深度代码 audit / security 预审 (e.g. 小白 supply chain + 私钥边界)
- 架构敏感的热路径实现 (e.g. 老陈 zero-alloc 出站 + 背压 + R-12)

**关键变更 — 推翻 v2 禁令:**
- v2 §3 把 "这个 task 复杂" 列为**不允许**的 Opus 理由 (省 token 立场)
- ✅ v3 老板三次补充: **task 复杂是 IC 升 4.8 的合法理由**, 判断权在派单的 GM / 主管
- 不再"从严收口"省 token (老板 2026-05-29: "不用过度给我省 token, 我给你们定了模型就是在我的预算之内")

**派单写法:** prompt 第一行 `model: opus (例外: IC 复杂工作 — <具体复杂点>)`, 老胡周报 §6 仍记录 (供统计, 非为压制)。

## 4. 派单 enforce (v3)

GM 召唤 sub-agent **默认随 persona frontmatter `model:` 字段**, 无需每次显式传:

```python
# v3 默认: 直接召唤, model 跟 frontmatter
Agent(subagent_type="cpp-hot-path-engineer", prompt=...)   # → sonnet 4.6 (frontmatter)
Agent(subagent_type="professional-manager", prompt=...)    # → opus 4.8 (frontmatter)

# 临时覆盖 (升/降档例外), prompt 第一行标理由
Agent(subagent_type="cpp-hot-path-engineer", model="opus",
      prompt="model: opus (例外: 紧急 P0 < 2h + 跨多模块)\n\n...")
```

## 5. 与 v2 的差异 (变更摘要)

| 维度 | v2 (2026-05-28) | v3 (2026-05-29) |
|---|---|---|
| 核心立场 | 省 token, 全员 Sonnet | 按层分档 |
| 管理层 (9) | Sonnet 4.6 | **Opus 4.8** |
| 顾问 (7) | Sonnet 4.6 | **Opus 4.8** |
| IC (32) | Sonnet 4.6 | Sonnet 4.6 (不变) |
| Opus 触发 | 仅紧急例外 | 默认 frontmatter, 例外是临时覆盖 |

## 6. 不变量 (沿用 v2 §7)

- 任何 sub-agent 拒接越界派单的能力**不受模型影响**
- 任何 P0 红线 (RM / 私钥 / R-11 / R-12 / R-20) **不受模型影响**
- 模型档位跟 persona, HC 新员工入职由 HR (小林) 在 registry 定档, 不允许"自动晋升"

## 7. 用户原话 verbatim 入约束

> "所有管理层以上员工用 claude 4.8 模型, 以下员工用 claude 4.7。" (2026-05-29)
> （IC 档随即口头校正 4.7 → "还是 4.6 吧"; 顾问"算管理层 (4.8)"）

---

**最后更新:** 2026-05-29 by 老雷 (GM)
