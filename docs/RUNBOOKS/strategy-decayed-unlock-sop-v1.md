---
owner: 老沈 (B 单元 IC, security)
reviewer: 老韩 (B 主管, Smell #4 owner) → 老郭 (architecture) → 老高 (PR v1.2) → GM ack
last_review: 2026-05-28
deadline: M2 2026-08-06
relates_to:
  - RejectCode::STRATEGY_DECAYED (reject_enum.hpp R-19)
  - AuditEventType::Unlock (audit_record.hpp AET #11)
  - laohan-riskmanager-design-v0.3.1.md §Smell-4
  - docs/ADR/2026-05-28-r07-r08-liquidity-vs-position-cap-priority.md (ADR-004)
---

# STRATEGY_DECAYED 触发后三签解锁操作手册 v1

> 本手册是 Smell #4 的操作实施。spec 由老韩出, SOP 由老沈出, M2 前必交。
> 任何与 spec 矛盾以老韩 Smell #4 spec 为准, 本文更新走 PR + 老韩 first review。

---

## §1 触发场景识别

### 1.1 触发条件

STRATEGY_DECAYED (RejectCode #19, R-19) 由 RM 自动触发, 不需要人工干预:

```
触发判断: P(μ < 0 | data) > 0.3 持续 >= 2 周 (Bayesian 后验, paper PnL 时序)
代码路径: RiskGateway::check_strategy_decayed_() → set_state(RmState::HALTED)
```

触发后 RM 立即进入 HALTED 状态: 所有新单拒绝 (RejectCode::STATE_HALTED), 仅放行平仓 (DRAIN 路径,
is_close=true 订单可通过)。

### 1.2 operator 如何感知触发

**方式 A — RiskGateway.state() 轮询 (主路径)**

```
if (gw.state() == RmState::HALTED) {
    // 查 audit WAL 最近 1 条 StrategyDecayed AET (#10)
    // 确认 reject_code == STRATEGY_DECAYED
}
```

**方式 B — audit WAL AET #10 告警**

audit chain 写入 `AuditEventType::StrategyDecayed` (AET #10) 条目。
监控系统扫描 WAL tail, AET==10 触发 PagerDuty/飞书通知。

**方式 C — operator 浏览器 notification + 邮件**

Grafana alert rule: `rm_state{mode="paper"|"live"} == 2` (HALTED=2) 持续 > 60s
→ 触发 notification channel (配置由小郑 W6 观测栈负责, SRE 部署层实施)。

### 1.3 触发上下文记录 (必须在 24h 内收集)

operator 在拉群前必须准备以下数据 (从 audit WAL + position WAL 提取):

| 项目 | 数据来源 | 说明 |
|---|---|---|
| 触发时 P(μ<0) 后验值 | audit WAL StrategyDecayed 条目 payload | 必须 > 0.3 才合法触发 |
| 持续开始日期 | audit WAL 最早连续 StrategyDecayed 条目时间戳 | 确认 >= 2 周 |
| 最近 4 周每日 PnL | position WAL / paper_audit.wal | 提供给老韩 + 老唐独立分析 |
| 触发时 open position | position WAL 当前快照 | 确认 exposure 归零进度 |
| audit_id (触发条目) | WAL 第一条 AET #10 记录的 audit_id | 作为三签 context_hash 基础 |

---

## §2 三签人员

三签顺序不可颠倒: 老韩先于老唐, 老唐先于 GM。审计链要求三条独立的 audit_id。

| 签序 | 签字人 | 角色 | 审查视角 | 拒绝权 |
|---|---|---|---|---|
| 第 1 签 | **老韩 (E-009, B 主管)** | RM owner / 风控 | Bayesian 后验有效性 + paper PnL 时序 + 信号 alpha decay | 单独否决 (CLAUDE.md §6 风控红线) |
| 第 2 签 | **老唐 (audit-expert, B 单元)** | audit chain owner | audit chain 完整性 + BLAKE3 链接有效 + 4 ts R-20 合规 | 单独否决 |
| 第 3 签 | **老雷 (GM)** | 战略 | 业务影响 + 时机判断 + 整体风险可接受 | 最终拍板 |

**风控原则**: 老韩 / 老郭 / 老黄 任一可单独叫停 (CLAUDE.md §6), 叫停后进入 re-analysis 流程,
重新满足三签前提条件后方可再走流程。

---

## §3 解锁前置条件 (C1-C3 全部满足才可进入三签流程)

### C1 paper PnL 时序复盘 (老韩责任)

复盘维度 (最近 4 周周级):

| 指标 | 健康基准 | 行动阈值 |
|---|---|---|
| 年化 Sharpe | >= 1.5 (北极星) | < 0 → 拒绝解锁 |
| 最大回撤 | <= 15% | > 30% → 拒绝解锁 |
| 每周笔数 | 与历史均值差异 < 50% | 笔数骤降 → 信号问题 |
| Win rate | > 50% (方向性策略) | < 40% 持续 4 周 → 拒绝解锁 |

复盘结果落文档: `docs/INCIDENTS/strategy-decayed-<date>-laohan-analysis.md`

### C2 信号 alpha decay 分析 (老韩 + 老唐协作)

各信号 (P0-01 / P0-02 / ...) 独立检查:

- 过去 2 周信号触发率是否异常下降 (> 50% 降幅为 P0)
- 信号 EV 分布是否发生均值漂移 (Bayesian posterior shift)
- 是否存在数据源问题 (Goalserve / Polymarket 数据质量下降)

信号分析报告: `docs/INCIDENTS/strategy-decayed-<date>-signal-decay-analysis.md`

### C3 风险敞口归零 (老唐验证)

```
exposure_pct < 0.1%  (所有 open position 已平仓)
```

验证方式: position WAL 快照读取, 全部 market 的 `exposure_usdc / bankroll_usdc < 0.001`。
老唐在 audit WAL 确认没有 DRAIN 状态下未平仓的遗留 fill。

**C3 未满足时严禁进入三签流程**: 解锁后立刻开单会放大已有亏损敞口。

---

## §4 三签流程 (Step 1-5)

### Step 1: 触发后 24h 内拉群

责任人: operator (值班人员)

行动:
1. 确认 audit WAL 存在 AET #10 记录, 提取触发 audit_id
2. 收集 §1.3 触发上下文数据
3. 建立即时通讯群: 老韩 + 老唐 + 老雷 + 老胡 (PM, 协调) + 老沈 (安全, 本 SOP owner)
4. 在群中发布: 触发时间 / P(μ<0) 值 / 持续天数 / 当前 open position 数量

### Step 2: 老韩 + 老唐 各自独立分析 (48h 窗口, 不允许 group think)

**关键约束**: 老韩和老唐必须在互相不看对方分析结论的情况下独立完成文档。
老胡 PM 控制信息隔离 (各自文档发布到群后才互相阅读)。

老韩分析内容: C1 PnL 复盘 + C2 信号 decay
老唐分析内容: C3 exposure 验证 + audit chain 完整性校验

### Step 3: GM 集成 + 拍板 ack (24h 响应)

老雷阅读老韩 + 老唐独立分析报告 (两份都需阅读), 做出战略判断:
- 是否同意 C1/C2/C3 分析结论
- 是否批准解锁
- 是否需要额外前置条件 (如: 先做 2 周 paper shadow 再解锁)

GM ack 落文档: `docs/INCIDENTS/strategy-decayed-<date>-gm-ack.md`

### Step 4: 三方签字 (CLI 工具执行, 落 3 audit_id + BLAKE3 链)

使用 `stcpp-strategy-unlock` CLI 工具 (§5 详述):

```bash
# 第 1 签: 老韩 (生成签名离线, 注入 CLI)
# Ed25519 签名内容: "<strategy_id>|STRATEGY_DECAYED_UNLOCK|<trigger_audit_id>|<timestamp_ns>"
stcpp-strategy-unlock \
  --strategy-id=<id> \
  --trigger-audit-id=<hex_audit_id> \
  --laohan-sig=<base64_ed25519_sig> \
  --laotang-sig=<base64_ed25519_sig> \
  --laolei-sig=<base64_ed25519_sig> \
  --exec-mode=paper|live

# CLI 输出: 3 audit_id (AET #11 Unlock × 3) + 解锁状态确认
```

CLI 强制验证 (任一失败则拒绝):
1. 三个签名均有效 (Ed25519 验签通过, 公钥来自 .env `RM_UNLOCK_PUBKEY_*`)
2. 签名 timestamp 与当前时间差 <= 1h (防重放)
3. strategy_id + trigger_audit_id 与签名 payload 一致
4. exec-mode 与当前运行进程 PID file 的 mode 一致

CLI 成功后:
- emit 3 条 AuditEventType::Unlock (AET #11) 记录到 audit WAL
- 每条 Unlock 记录携带签字人 operator_id + BLAKE3 链接
- RM state 从 HALTED 切回 RUNNING

### Step 5: RM state 从 STRATEGY_DECAYED 切回 RUNNING

CLI 通过 RPC/IPC 调用 RiskGateway::set_state(RmState::RUNNING)。
**不允许直接重启进程**: 重启会丢失 audit chain 上下文, 违反 R-1。

---

## §5 不允许的解锁路径 (CLI 强制 enforce)

| 违禁路径 | CLI 响应 | 原因 |
|---|---|---|
| operator 直接重启 RM 进程 | 不适用 (重启走 SAFE_MODE, 非 RUNNING) | 绕过审计 + 状态不一致 |
| 单签 (1 人) | CLI 返回 exit 1, 不改 state | 不满足三签合规要求 |
| 两签 (2 人, 缺任一) | CLI 返回 exit 1, 不改 state | 同上 |
| 公钥 mismatch (错签) | CLI 返回 exit 1, Ed25519 验签失败 | 签字人身份无法确认 |
| 过期签 (timestamp > 1h) | CLI 返回 exit 1, timestamp drift 拒绝 | 防签名重放 |
| 跳过 audit chain | CLI 强制 emit 3 audit_id 后才调 set_state | R-1: 解锁必须留可追溯记录 |
| 直接修改 RM state enum | 禁止 (代码层: set_state() 是唯一接口) | R-1 红线 |

---

## §6 解锁后 28 天观察期

### 6.1 自动 re-trigger 规则

解锁后 28 天内处于强化监控期:

```
任意新单被 STRATEGY_DECAYED (R-19) 拒绝
→ RM 立刻自动 re-trigger STRATEGY_DECAYED (无需等 2 周)
→ 重新进入本 SOP §1 流程
```

观察期结束条件: 解锁后 28 个自然日内无 STRATEGY_DECAYED reject 记录。

### 6.2 观察期强化指标

每周 (周一站会) 汇报:
- paper PnL 日环比
- 信号触发次数 vs 历史均值
- STRATEGY_DECAYED reject 次数 (观察期内应为 0)

### 6.3 观察期满处理

老韩在观察期满后 5 个工作日内出 `docs/INCIDENTS/strategy-decayed-<date>-recovery-report.md`,
确认策略稳定恢复。该报告作为后续 M4.5 gate 输入材料之一。

---

## §7 紧急 override (GM 单签)

### 7.1 适用场景 (极度受限)

仅限以下两种场景:
1. **用户 / 投资人紧急要求**: 外部强制要求 24h 内恢复交易, 且老韩 / 老唐 有一人不可达
2. **技术性误触发**: 有明确证据表明 STRATEGY_DECAYED 触发是数据管道故障 (非真实策略衰减)
   且老韩书面确认误触发 + 老唐确认 audit chain 无篡改 (此情况实为"2 签 + GM 确认", 非真单签)

### 7.2 操作

```bash
# 紧急模式: 仅 GM 签名 + override flag
stcpp-strategy-unlock \
  --strategy-id=<id> \
  --trigger-audit-id=<hex> \
  --laolei-sig=<base64_ed25519_sig> \
  --emergency-override \
  --exec-mode=paper|live
```

CLI 行为:
- 接受单签 + `--emergency-override` flag
- 自动 emit audit record: AET #11 Unlock, operator_id="GM_EMERGENCY_OVERRIDE"
- 在 audit payload 的 note 字段写入 "EMERGENCY_SINGLE_SIGN: normal 3-sig bypassed"
- RM state 切 RUNNING

### 7.3 事后强制义务

| 义务 | deadline | 责任人 |
|---|---|---|
| W7 retro 复盘 (全员) | 下一个 sprint retro | 老胡 (主持) + 老韩 (风控总结) |
| Incident post-mortem | 72h 内 | 老沈 (author) |
| 补签老韩 + 老唐 ack | 72h 内 | 老韩 + 老唐 各出书面确认 |
| audit record 补注 | 72h 内 | 老唐 (补 emit Unlock × 2) |

**紧急 override 不是例外豁免, 是负债**: W7 retro 必须输出是否需要修改 SOP 的决议。

---

## §8 安全约束 (老沈 ADR-009 v2 enforce)

- Ed25519 私钥: 离线生成, 不入 git, 不入日志, 存放路径 `.env` (`.gitignore` 已覆盖)
- 公钥: 写入 `.env` 的 `RM_UNLOCK_PUBKEY_LAOHAN` / `RM_UNLOCK_PUBKEY_LAOTANG` / `RM_UNLOCK_PUBKEY_LAOLEI`
- 签名有效期: 1h (CLI timestamp drift 硬拒)
- paper / live 使用不同签名载体 (exec-mode 参数隔离, R-11 / R-7)
- CLI 不打印私钥 / 签名原文到 stdout/stderr
- audit WAL 物理隔离: paper 写 PaperAudit (/var/lib/stcpp/paper/), live 写 RiskAudit (/var/lib/stcpp/audit/) — R-11

---

**END v1.** 老韩 first review → 老郭 architecture review → 老高 PR v1.2 → GM ack. M2 (8/6) 前交付.
