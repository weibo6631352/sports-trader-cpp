---
owner: 小尤 (ux-experience-evaluator, E-047)
last_review: 2026-05-29
sprint: Sprint-3 W10
relates_to:
  - docs/RESEARCH/xiaoyou-ux-framework-v1.md
  - docs/RESEARCH/xiaosu-ui-wireframe-v0.1.md
  - docs/RESEARCH/xiaogong-dogfood-playbook-v1.md
  - docs/SPRINTS/sprint-03-w10-plan.md
---

# UX Framework Spec W10-W1 + Dogfood Plan

- **Owner:** 小尤 (ux-experience-evaluator, E-047)
- **撰写日:** 2026-05-29
- **汇报:** 老胡 (E-026)
- **协同:** 小宫 (dogfood-tester, E-048) / 小苏 (frontend, E-012) / 小郑 (observability, E-011)

---

## §1 UX 评估 Framework — 应用到 M1-H PnL 看板 8 页

本节把 ux-framework-v1 的 7 维评估方法具体落到小苏 Wave 95 spec 的 8 个页面
(D1 交易大盘 / D2 数据健康 / D3 风控大盘 / D4 链上状态 / D5 信号监控 /
Operator Console 主屏 / Audit Trail / CLI stcpp-ctl status).

### §1.1 4 个核心 UX 维度 (重申 + 应用)

**D1 心流 (Flow):** 操作员 8~12h 盯屏, 1h 内非必要打断 ≤ 1 次.
M1-H 最容易破心流: D3 REJECT HEATMAP 颜色频繁变化 / Console ALERT BANNER P2 反复抢焦 /
CLI 每次要记子命令. 评估口径: 操作员主观打分 + 每天非必要打断事件计数.

**D2 直觉触发点 (Intuitive Action):** 看到信息 → 知道下一步, 不需思考.
需特别审查: RM STATE 变黄后操作员走三跳 (D1-panel1 → D1-panel6 → D2) 才找到源头,
需要 tooltip 直接给跳链接; HALT 按钮灰色无 tooltip 解释原因; CLI status ALERTS 行后无下一步.
评估口径: 盲操测试 (新操作员 30s 内找到下一步) 通过率.

**D3 信息密度 (Info Density):** 甜区一屏 12-18 核心数字, 不横向滚动.
小苏 Wave 95 D1/D3 panel 数各为 12 (顶满) — 顶满不是坏事, 但每次新增 metric 需先砍一个,
这是准入卡 D3 维度把守的门.

M1-H 8 页信息密度速查:

| 页面 | panel 数 | 核心数字估算 | 密度评级 |
|---|---|---|---|
| D1 交易大盘 | 12 | ~40 个数字 | 偏满, 需实测 |
| D2 数据健康 | 10 | ~28 个数字 | 合理 |
| D3 风控大盘 | 12 | ~45 个数字 | 偏满 |
| D4 链上状态 | 9 | ~22 个数字 | 宽松 |
| D5 信号监控 | 11 | ~35 个数字 | 合理 |
| Operator Console | 10 区块 | ~30 个数字 | 合理 |
| Audit Trail | 列表页 | 可变 | 依赖分页 |
| CLI status | 固定宽度输出 | ~25 行 | 合理 |

D1 和 D3 偏满, W11 dogfood 时实测 chrome-devtools digit_cell_count, 超过 50 需砍.

**D4 错误恢复 (Error Recovery):** 出了事, 操作员 3s 内知道"做什么".
M1-H 路径最长的两个场景:
1. goalserve stale → RM WARNING: D1-panel1 黄 → 跳 D2 → panel4 时序图 → panel6 STALE → 决策,
   约 4 步 + Grafana 两 tab 切换. 改进: D1-panel1 tooltip 直给 "goalserve stale 6.2s, see D2" + 跳链接.
2. nonce gap → P0 HALT: `audit --tail` → NONCE_GAP 行 → 权限确认 → `nonce --resync`,
   需要权限不足时直接显示"找老孙/老叶", 不让操作员猜.

### §1.2 8 页 UX 准入卡模板 (W11 上线前逐页填)

每页在 W11 paper runtime 启动后 1 周内出评分卡:

```
页面: ____________   评审日: ____________   reviewer: 小尤 + 小宫 dogfood 同步

D1-flow        心流 (打断次数)         [  /10]
D2-intuition   直觉触发 (盲操通过率)   [  /10]
D3-density     信息密度 (数字计数)     [  /10]
D4-recovery    错误恢复 (步骤数/时长)  [  /10]

附加项 (涉及告警模块才填):
D4-alert-noise 报警噪音 (真阳率)       [  /10]   < 7 一票驳回

总分: __/40   准入: [ ] 通过  [ ] 有条件  [ ] 驳回
```

准入卡阈值 (与 ux-framework-v1 §3.2 对齐):
- 任何单维 < 5 → 驳回
- 报警噪音 < 7 → 驳回 (一票)
- 平均 ≥ 7 → 通过
- 平均 6~7 → 有条件通过 (1 sprint 内补改)

### §1.3 8 页各自的 UX 风险点速查

| 页面 | 最高 UX 风险 | 影响维度 | 建议处理 |
|---|---|---|---|
| D1 交易大盘 | 12 panel 顶满, 后续塞不下 | D3 密度 | 砍一个 panel 留余量 |
| D2 数据健康 | 三色灯无 tooltip 说明阈值 | D2 直觉 | hover 显示 "warn=2s halt=10s" |
| D3 风控大盘 | REJECT HEATMAP 颜色无规律时看不懂 | D2/D3 | 加行/列排序 + 点击说明 |
| D4 链上状态 | nonce resync 权限门槛不直觉 | D4 错误恢复 | 权限不足时直接显示"找老孙/老叶" |
| D5 信号监控 | edge bps 直方图轴单位不直觉 | D3 | 加 bps 单位注释 + x 轴标签 |
| Operator Console | ALERT BANNER P2 噪音高 | D1 心流 / D4-alert | P2 改侧栏, 不占顶部 banner |
| Audit Trail | trace_id 16 hex 对非工程人员无感 | D2 直觉 | 加"人话摘要"列 |
| CLI status | ALERTS P2:3 后无下一步提示 | D4 错误恢复 | 在 status 输出末尾加 next_action |

---

## §2 Dogfood Plan — W11 Paper Runtime 与小宫联动

W11 paper runtime 启动后, 小宫跑流程, 我 (小尤) 评体感.
分工原则: **小宫跑, 我看**.

### §2.1 W11 Dogfood 启动条件

参考 sprint-03-w10-plan §7 checklist, 以下全绿才能启动 W11 paper runtime,
小尤评估也依赖这些条件:

- RM v0.5 整合完成 (老韩 + 老沈)
- audit chain 写入可用 (老唐)
- REST 接真 state 可用 (小卢)
- CI main 干净
- Frankfurt server 就绪 (老吴)
- D2 数据健康 dashboard 上线 (第一个上的 dashboard, 小苏 + 小郑)

若 D2 未上线, 小尤无法给数据健康页出评分卡, W11 dogfood 只能覆盖 CLI 和 Operator Console.

### §2.2 小宫跑 / 小尤看 分工表

| 场景 | 小宫做什么 | 小尤评什么 |
|---|---|---|
| 正常交易日启动 | `stcpp-ctl status` 确认 RUNNING, 扫 D2 三色灯 | CLI status 输出 30s 内能否读懂 |
| goalserve stale 注入 | `chaos --inject goalserve-down --duration 30` | WARNING 触发到操作员"知道下一步"几步? |
| RM HALT + RESUME-ACK | HALT → 双人 RESUME-ACK 流程 | 双人确认 modal 认知负担 (理解 vs 习惯) |
| P1 告警触发 | WSS 断 30s, 看 Slack 告警 | 告警 subject 一眼读懂率 + body 结构分 |
| 新人 onboarding 模拟 | 首次看 Operator Console, 不看文档 | 5 min 内找到 HALT 按钮 + 理解倒计时 |
| 值班交接 handoff | `stcpp-ctl handoff --from a --to b` | 交接 5 个数字够不够, 漏没漏 |
| audit grep | `stcpp-ctl audit --grep STALE_DATA` | 结果格式可读性 + 有没有人话摘要 |

### §2.3 评估节奏

- W11 第 1 天: 小宫完成"正常交易日启动"场景, 小尤出 D2 + CLI status 评分卡 (各 1 张)
- W11 第 2 天: chaos 注入场景, 小尤出 D1 + Operator Console 评分卡
- W11 第 3 天: P1 告警 + HALT/RESUME-ACK, 小尤出 D3 + 告警可读性评分
- W11 第 4 天: 新人 onboarding 模拟, 小尤出"直觉触发点"专项报告
- W11 第 5 天: 小尤整合本周 8 页评分卡, 出 W11 UX 体感日志 → 提交老胡

### §2.4 主观感受 Reporting 链路

小宫 → 小尤 → 老胡 → 总裁/老板, 每级提炼一次:

**小宫输出 (raw):**
每场景跑完填一张表:
```
场景: ___________   日期: ___________   评分 1-10: ___
卡点描述 (一句话): ___________________________________________
复现步骤: 1. ... 2. ... 3. ...
建议归口: @小苏 / @小郑 / @owner
```

**小尤输出 (体感日志, W11 末汇总):**
- 痛点 top 3 (含维度 + 严重度)
- 流畅点 top 3 (值得复用)
- 8 页评分卡汇总表
- 建议 backlog (给小苏 / 小郑 / 老胡)

**老胡输出 (周报 §6):**
- UX 均分 (目标 ≥ 7/10)
- 需立改 item 数 (目标 0 个驳回项)
- 告警真阳率 (目标 ≥ 80%)

---

## §3 老板 Verbatim "用户体验专家满意" 落地标准

老板原话目标: "用户体验专家满意". 拆开三层:

### §3.1 总裁满意 (我自己)

我 (小尤) 满意 = 以下 5 条全达到:

1. **8 页评分卡全部 ≥ 7 分均值, 0 个驳回维度**
   具体: D1~D5 + Console + Audit + CLI 各出 1 张评分卡, 无单维 < 5, 无 D4-alert-noise < 7

2. **操作员在凌晨 3 点 NBA 末节能做对**
   具体: 新人 onboarding 测试 — 首次看 Operator Console, 5 min 内独立完成
   "系统状态确认 + 找到 HALT 按钮 + 理解倒计时" 三步. 通过率 ≥ 80% (≥ 4/5 人测)

3. **P0/P1 告警主观读懂率 ≥ 90%**
   具体: 抽 10 条真实 P0/P1 告警 subject, 操作员盲读 5s 内说出"是什么/影响什么/下一步",
   答对率 ≥ 9/10

4. **错误恢复路径 ≤ 3 步**
   具体: 主要 5 个异常场景 (goalserve stale / WSS 断 / nonce gap / RM WARNING / 拒单率高)
   每个场景操作员平均步骤 ≤ 3 步才到"知道下一步做什么"

5. **心流打断 ≤ 1 次/小时**
   具体: 操作员工作日自评, 连续 3 个交易日主观打分 ≥ 7/10 (心流维度)

### §3.2 老板满意 (退出期 + 周报通道)

老板 = 项目投资人视角, 满意标准 = 系统可运营 + 操作员不需要老板介入:

1. **周报里 UX 均分趋势上升或稳定 ≥ 7**
   渠道: 老胡周报 §6 写明 UX 均分 (小尤提供数据), 老板通过周报看到趋势

2. **连续 4 周无 "UX 导致的 P0/P1 事故"**
   定义: 告警格式错误 / 误按按钮 / 读错数字 导致的实盘事故 = 0
   如果有, 小尤出 post-mortem + UX root cause 分析

3. **操作员 onboarding 时间 ≤ 2h**
   具体: 新操作员从零开始, 跟着 `stcpp-ctl --help` + 看 Operator Console, 2h 内独立完成
   一次完整交易日值班. 这是退出期最重要的可替换性指标

4. **UX 改进 backlog 持续收缩**
   具体: 每 sprint backlog 新增项 ≤ 解决项. 若 backlog 只涨不消, 说明 UX 债在累积

### §3.3 小宋 + 小尤 + 小宫 三方都满意

三方满意 = UX 评审闭环跑通:

**小宋满意 (QA 视角):**
- UX 评分卡不增加小宋的 bug 负担: 体验问题归小尤, bug 归小宋, 边界清晰
- 小尤发现的"操作员误按"场景, 明确标注: 若是 bug (按了没反应) → 小宋; 若是 UX 问题
  (按了反应不对但没有崩溃) → 小苏 + 小尤

**小尤满意 (UX 视角):**
- 提的 backlog 有人接: 给小苏的 FE-01~FE-07, 给小郑的 OBS-01~OBS-07, 下 sprint 有 owner
- 评分卡准入机制生效: 有条件通过的 item 在 1 sprint 内确实被改
- 不做设计实现, 但反馈被小苏认可引用

**小宫满意 (dogfood 视角):**
- dogfood 剧本跑得顺: 场景不卡, 命令不报奇怪错误, 环境稳定
- 小宫的卡点反馈有 SLA: 提了 → 1 个工作日内小尤确认归口 → 3 个工作日内 owner ack
- W11 每天跑完能出一张简表, 不需要写长报告 (小宫不写文档, 小尤帮整理)

---

## ADR-029 + ADR-032 流程说明

本文档按 ADR-029 (worktree push PR flow) + ADR-032 (push 后不等 CI) 提交:
- 文件落 `docs/RESEARCH/xiaoyou-w10-w1-ux-framework-spec.md`
- commit + push 后不等 CI, CI 结果由 owner 自查
- 如 CI 有格式 lint 失败, 小尤 owner 负责修

---

**文档状态:** v1.0 ready for review
**下一步:** W11 paper runtime 启动后, 小尤 + 小宫 按 §2.3 节奏逐日出评分卡, W11 末汇总 → 老胡
