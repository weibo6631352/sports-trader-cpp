# 实盘准备计划 v1 (2026-06-12)

owner: 老雷 (GM) | last_review: 2026-06-12 | 状态: 准备期 (**开闸授权 = 老板一句话同意**, 2026-06-12 老板定: 不搞会签流程; §8.1 人签门即此)

## 一、晋升标准 (paper → live, 数据门槛)
| 维度 | 门槛 | 现状 |
|---|---|---|
| 引擎结算样本 | 该引擎 ≥100 笔结算 | sharp 51 / flb 0 / dip 0 |
| 胜率置信 | 95% CI 下沿 > 盈亏平衡胜率 +5pp | sharp: 82%@51, CI下沿 ~71% vs BE ~68% (差 2pp, 未达) |
| 日净趋势 | 连续 3 个自然日净 >0 ([daily-close] 行) | 第 1 天进行中 (+100) |
| 稳定性 | 7 天零 P0 (崩溃/账错) | 2026-06-11 两起已根治, 计时重启 |
| 首发引擎 | **sharp** (唯一有实盘结算证据) | FLB 等自己的 100 结算 |

## 二、实盘第一阶段设计 (满足门槛后)
- **微注影子期 2 周**: $1-2/单 (CLOB min $1), 与 paper 同信号并跑, 对比成交价/滑点/费 → 校准 VirtualMatcher
- live RM 档 (与 paper 完全分离): per_order $2 / 日损硬熔断 $10 / consec-loss 5 / 总敞口 $50
- kill switch 三层: LiveOrderGate.Disarm() (秒级) / 进程停 / 钱包划走

## 三、基建缺口清单 (全部可 disarmed 先建, 不需会签)
| # | 缺口 | 状态 | 优先级 |
|---|---|---|---|
| 1 | live 账本与 paper 物理隔离 (R-11) | 缺 — live FillEvent → 独立 PositionLedger + WAL | P0 |
| 2 | 钱包余额/allowance 预检 | 缺 — CLOB /balance-allowance 只读接入 | P0 (本轮做) |
| 3 | 成交对账 (CLOB positions vs 账本) | 缺 — data-api /positions 定时核对 | P1 |
| 4 | user-channel WSS (订单状态推送) | 缺 — 现 ExecReport 仅 FOK 同步回执 | P1 |
| 5 | LiveExecutorAdapter 接线 (executor_ 注入缝) | 缝在 (G-1), adapter 未接 | P1 |
| 6 | live RM 档代码化 | 本轮做 (named profile, 不激活) | P0 (本轮做) |
| 7 | 开闸 runbook (Arm 流程/回滚/监控) | 缺 | P2 |

## 四、风险红线重申
- paper 期关闭的日损熔断/连亏 halt **在 live 档全部重开且更紧**
- Arm() 仅在老板明确同意后由 GM 执行 (单参数切换); 任何自动/未授权 Arm = P0
- live 首日仅 sharp 引擎 + 白名单 5 盘 + 总敞口 $50

## 五、切换设计 (2026-06-12 老板定: 「真钱和虚拟盘就一个参数切换的事」)
- 目标形态: `start_paper.sh` ↔ `start_live.sh` 单脚本切换 — 同一套信号/引擎代码, 差异仅:
  ① 编译模式 binary (live build, R-7/R-11 编译期锁保留 — paper binary 物理无闸是地基不拆)
  ② RM 档: LiveShadowRiskProfile (已建)
  ③ executor: VirtualExecutor → LiveExecutorAdapter (缺口#5, 待接线)
  ④ 账本: live PositionLedger + WAL (缺口#1)
  ⑤ LIVE_ARMED=1 环境变量 → Arm() (仅老板同意后设置)
- 下一步工程: 接线 #5 LiveExecutorAdapter + #1 live 账本 → 凑齐「一个参数」的全部内涵
