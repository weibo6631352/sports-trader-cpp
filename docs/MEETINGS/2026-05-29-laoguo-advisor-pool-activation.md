# 顾问团激活 Framework — Wave 53

**owner:** 老郭 (#46, F 协调人)
**last_review:** 2026-05-29
**依据:** GM 授权 verbatim 2026-05-29 ("顾问们也别闲着, 你全权代表我管理所有部门")
**关联:** Stage-Gate framework §4.4 (老胡 2026-05-29); CLAUDE.md §4 F 顾问团

---

## §1 顾问团 9 人名单

| # | 姓名 | Persona | 主专业 | 角色定位 |
|---|---|---|---|---|
| #01 | 老张 | 架构兼容顾问 | 原 Rust advisor, 项目 C++ only 后调整为架构兼容性独立审查 | ADR cross-check |
| #44 | 老何 | AI/LLM advisor | AI/LLM 在量化系统中的 leverage 点 | signal augmentation / anomaly detection |
| #15 | 老钱 | CPO + strategy advisor | 双角色: CPO 主线产品方向 + 顾问团战略输入 | 产品路线 + 赛道扩展 |
| #46 | **老郭** | **F 协调人 (本文 owner)** | 架构 second opinion, 跨 agent 仲裁, 顾问团协调 | 协调 routing + 架构否决 |
| #16 | 老高 | CI/code quality advisor | CI 流程质量, 多 repo grep/lint 优化 | ADR-027/ADR-024 review |
| #31 | 小邓 | ML advisor | ML signal pipeline, 离线训练→ONNX | signal pipeline 与 PositionManager 衔接 |
| #18 | 老叶 | 金融 advisor | 金融产品, 做市逻辑, KR 验收金融视角 | G3/G4/G5 Stage-Gate 金融审查 |
| #42 | 老徐 | 外部工具栈 advisor | 工具选型, DuckDB/Parquet/websocat | 工具升级建议 |
| #43 | 小白 | security audit advisor | 密钥管理, API 安全, 红线审查 | G4 上线前 security audit |

协调人 (本人老郭) 不亲自设计, 不写代码. 负责 routing / 聚合 / 仲裁升级.

---

## §2 激活 Framework — 三层节奏

### 2.1 每周 (W9 W1 起, 即 Sprint-3 第 1 周)

**周五意见箱** (老郭收集):
- 每位顾问填 1-3 条: 需求 / 风险预警 / 改进意见
- 格式: `[顾问姓名] [类别: 需求/风险/改进] [一句话内容] [优先级: P0/P1/P2]`
- 截止: 每周五 EOD
- 老郭周五收完 → 周一上午 aggregate 成**顾问周报** → 送主管周同步 (老胡 PM 主持)

**顾问周报内容:**
1. 本周意见条目 (按优先级排序)
2. 已 routing 给哪个主管的条目状态
3. 待 GM 拍板事项 (若有)

### 2.2 每月 (Sprint-2 W4 起, 约 6/26 或对齐月末)

**月度顾问→老雷 GM 1:1 长会** (60 min, 老郭主持):
- 议题固定 4 块:
  1. 战略方向确认 (老钱 CPO 输入)
  2. 技术选型重大决策 (老张/老何/老高 输入)
  3. 风险预警 (小白/老叶 输入)
  4. 班底升级 / 工具栈升级 建议 (老徐/小邓 输入)
- 产出: 月度顾问纪要 → 落 `docs/MEETINGS/YYYY-MM-laoguo-advisor-monthly.md`
- 老郭提前 72h 收集各顾问议题提案, 合并议程后发给 GM

### 2.3 重大决议 (临时触发, 24h ack)

触发条件: ADR 新建 / 班底变更 / 红线事故 / 跨 ≥3 单元架构变动

流程:
1. 触发方 (任意 agent / 主管) → @老郭
2. 老郭 routing: 确定相关顾问子集 (≥2 人) → 发议题 brief
3. 相关顾问 24h 内 ack + 给出意见
4. 老郭 aggregate → 出仲裁意见 / 架构评审报告
5. 结论入 ADR / 事故报告 / 班底变更决议

---

## §3 Stage-Gate 接入 (对接 老胡 §4.4)

每 G 阶段 90% 触发时, 老胡通知老郭 → 老郭协调顾问 cross-review:

| Gate | 当前进度 | 顾问重点 | 老郭协调动作 |
|---|---|---|---|
| G3 (M4.5 paper 2 周) | 0% (Sprint-3 W11+) | 老叶 (金融逻辑验证) + 老何 (AI signal 质量) | 提前 1 周发 review brief, 收意见 aggregate |
| G4 (M5 live 首笔成交) | 0% (G3 通过后) | 小白 (security audit) + 老叶 (金融产品合规) | security checklist + 金融 KR 验收 |
| G5 (M6 盈利达标) | 0% (North Star) | 全 9 顾问 review | 月度长会升级为 G5 全员评审会 (120 min) |

顾问 review 产出: 格式化意见 → 老郭 aggregate → 进 Stage-Gate 会议正式议程.
G3/G4/G5 顾问意见为 Stage-Gate 会议必要输入 (老胡 §4.2 议程 step).

---

## §4 W9 W1 启动动作 (2026-06-29 Sprint-3 周)

| 动作 | 截止 | 负责人 | 状态 |
|---|---|---|---|
| 周五 W9 W1 Fri 发第 1 期意见箱 ping (9 顾问各 1 条引子) | 2026-07-04 Fri | 老郭 | 待启动 |
| 顾问 onboarding: 每位顾问读 docs/INDEX.md + CLAUDE.md + W8 W5 周报 | W9 W1 | 老郭协调 + 小林 HR 配合 | 待排期 |
| 月度 1:1 排期: W9 W4 (2026-07-25 附近) | W9 W1 确认 | 老郭 + @老雷 | 待确认 |
| 主管周同步加顾问 input 议程项 | W9 W1 Mon | 老郭 → @老胡 | 待协商 |
| 顾问角色 doc onboarding | W9 W1 | @小林 HR 签字 | 待派 |

---

## §5 W9 W1 第 1 周意见箱引子 (老郭各 ping 1 条)

老郭将在 W9 W1 Fri 向每位顾问发以下引子, 触发第 1 期意见箱:

| 顾问 | 引子问题 |
|---|---|
| **老张 (#01)** | Rust 已退场 (GM 2026-05-28 终版), 你的角色调整为架构兼容性独立审查. 第 1 周 input: 当前 C++20 架构有哪 1-2 处你认为兼容性风险最高的决定? |
| **老何 (#44)** | AI/LLM 在 sports-trader-cpp 系统中哪里有最高 leverage 点? 优先考虑 anomaly detection / signal augmentation / 数据质量监控三个方向, 给 1 个最值得 Sprint-3 开始 PoC 的点. |
| **老钱 (#15)** | CPO 视角: T+12 月以后体育盘口扩展优先级 (Moneyline 之后下一个应该是什么?), 给 1 个有数字支撑的判断. |
| **老高 (#16)** | ADR-027 + ADR-024 的 CI multi-grep 流程目前有几处重复检查. 给 1 条合并优化建议, 附估算节省 CI 时间. |
| **小邓 (#31)** | ML signal pipeline (你负责离线训练→ONNX) 与 C++ PositionManager 的衔接接口目前是否明确? 给 1 条接口 gap 或风险. |
| **老叶 (#18)** | G3/G4/G5 KR 金融视角 background check: 当前 KR-C-1 (实盘首笔 PnL > 0 USDC) 的验收标准是否足够严格? 给 1 条补充建议. |
| **老徐 (#42)** | 本周已交付工具栈 inventory. 下一步: DuckDB / Parquet 在 ETL pipeline 中的集成深度建议, 给 1 条 Sprint-3 可执行的升级建议. |
| **小白 (#43)** | Security audit 优先级: 私钥管理 / API 认证 / 网络暴露面三块哪个风险最高? 给 1 条 G4 上线前必须关闭的 security gap. |
| **老郭 (#46, 本人)** | 跨单元架构风险自评: 当前 A 系统工程部 与 D 数据基础设施部 的数据 schema 变更通知机制 (CLAUDE.md 红线 §8) 是否有执行盲区? 自评 1 条 + routing 给老周/小余. |

---

## §6 意见 → 项目 Backlog 流程

```
顾问意见 (周五意见箱 / 重大决议)
    │
    ▼
老郭 routing 判断
    ├─ 单元内技术问题 → @对应主管 (老周/老韩/小梁/小余/老胡)
    ├─ 跨单元 / 架构争议 → 老郭架构评审 → 结论入 ADR
    ├─ 产品方向 → @老钱 CPO + @老雷 GM 联决
    └─ 红线 / P0 → 老郭/老韩/老黄 任一可单独叫停 (CLAUDE.md §6)
    │
    ▼
主管 ack (24h) → 接 / 拒 / 升老雷
    │
    ▼
入 Sprint backlog (老胡 PM 排期)
```

意见条目追踪: 每条意见赋 ID `ADV-YYYYMM-NN`, 老郭在顾问周报中维护状态列表.

---

## §7 协作接口 (不耻下问)

| 接口对象 | 内容 | 频率 |
|---|---|---|
| **@老胡 PM** | 顾问周报送主管周同步; Stage-Gate 触发通知; Sprint backlog 排期 | 每周一 |
| **@老雷 GM** | 月度 1:1 排期; 重大决议 ack; P0 事故升级 | 月度 + 临时 |
| **@9 顾问** | 每周 ping 意见箱; 重大决议 brief; Stage-Gate review brief | 每周五 + 临时 |
| **@小林 HR** | 顾问角色 onboarding doc 注册 (CLAUDE.md §7 HR 注册前置) | W9 W1 一次 |
| **@老周 (A 主管)** | 架构类意见 routing; ADR 协同 | 按需 |
| **@老韩 (B 主管)** | 风险预警 routing; 红线 ack | 按需 |

---

## §8 约束与边界

1. **老郭不亲自设计架构, 不写代码** — 仅 review / 仲裁 / routing / 协调
2. **顾问不越位主管** — 顾问意见经老郭 routing 给主管, 不直接派 IC 任务 (ADR-005)
3. **架构否决权** — 老郭对所有单元有架构否决权 (CLAUDE.md §4), 否决须出具 ADR 评审报告
4. **GM 授权边界** — 全权代理期间老郭代 GM 协调管理, 但**红线决策 (下单绕 RiskManager / 私钥 / 数据 schema 静默变更 等) 仍需老雷 GM 本人确认或老韩/老黄 RM 叫停**
5. **Sonnet 模型** — 本 framework 文档及顾问激活全程 ADR-009 v2 Sonnet (无 Opus 例外条件)

---

**下一动作:** W9 W1 (Sprint-3 启动周 2026-06-29) 老郭发第 1 期意见箱 ping. 月度 1:1 排 W9 W4 (2026-07-25 附近) 待 @老雷 GM 确认排期.
