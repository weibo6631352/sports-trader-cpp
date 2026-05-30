# JD: onchain-ops-engineer (persona: 老冀)

- Owner: 小林
- Date: 2026-05-28
- Status: Active recruiting (Sprint-2 W2 起)
- 截止入职: 2026-06-30 (Q2)
- 评委: 见 docs/HIRING/backlog.md §3 (HC-01)
- 关联决策: MVP 不上链 (GM 2026-05-28 决议, 原 gm-decision-defer-onchain-until-profitable.md 已删; M4.5 hard gate 通过后解锁)

## 阶段透明告知

公司当前处于 **paper trading 阶段**, 实盘上链解锁需 M4.5 7 hard gate (PnL / Sharpe / DD / 在线率 / n_trades 等) 连续 2 周通过. **MVP 期间你不会真签链上交易**, 而是负责 PaperSigner mock + 未来切 LiveSigner 的零重构架构. 接受这一阶段性安排是上岗前提.

## 1. 岗位职责

- **MVP 阶段 (Q2-Q3 2026)**:
  - 维护 PaperSigner mock (virtual_nonce / virtual_gas / virtual_confirm 三 stub) 与 paper engine 联调 (与小蒋协作)
  - Polygon RPC / nonce_mgr / receiver whitelist 设计文档保鲜 (已归档但需季度 review, 等待 reactivate)
  - 监控 Polymarket on-chain 状态 (gamma metadata + 链上 settle 时序) 即便 paper 不触发, 也要保证数据准确
- **解锁后 (post M4.5 gate)**:
  - LiveSigner 落地 + KMS unwrap (老孙 v5 设计)
  - nonce_mgr 真实 Redis/SQLite/RPC observe 上线
  - gas 监控 + 跨链桥接 (Sygnum / Fireblocks 任一) 运维
  - 链上交易广播 SLA owner

## 2. 任职要求 (技术 @老周 / 风控 @老韩)

- 3+ 年生产环境链上交易系统经验 (DEX / CEX bridge / Polygon / Ethereum 任一)
- 熟悉 EIP-1559 gas 机制 / nonce 管理 / RPC 高可用切换
- 能读懂 Solidity, 不要求写 (我们不部署合约)
- C++17/20 工程能力 (主交易系统是 C++, 至少要能改 signer 接口)
- 接受 paper-first 文化, 不在 paper 阶段偷偷启用真链路

## 3. 加分项

- Polymarket CTF / UMA oracle 实操经验
- 有过链上交易事故复盘公开记录 (postmortem)
- KMS / HSM / Sygnum 接入经验
- 跨洋部署 (us-east-1 ↔ 国内) 网络延迟调优经验

## 4. 公司价值观要求 (4 条)

- **实盘优先**: 接受 paper 阶段不上链, 但理解 paper 是为实盘做准备, 不是终点
- **纪律**: paper mode 触发真实链上交易 = P0 事故 = 不可逾越的红线
- **数字说话**: 解锁上链与否, 看 M4.5 gate 数字, 不看个人判断
- **不耻下问**: 链上事故必须公开复盘, 不能藏

## 5. 面试流程 (SOP v1)

- D10 初面 40min — 老周 (签名/nonce/gas 三件套基本功)
- D14 深度面 90min — 老周 + 老韩 (链上风控边界 + R-11 红线场景题)
- D17 文化面 30min — 小林 + 老雷 (paper-first 接受度)
- D21 GM 终面 — 老雷 (P1 必过)
- D24 评分汇总, 录用线综合 ≥ 7.5

## 6. 入职 buddy

- **老周直带** (架构 owner, paper signer 接口设计者)
- Day 1-7: 跟老周过 architecture v0.4 §18 (ExecutionMode.Paper mock 接口)
- Week 2: 与小蒋配对调 paper engine virtual_nonce / virtual_gas stub
- Week 3-4: 第一个真任务 — PaperSigner mock 边界 case 测试 (>= 50 个 case)

## 7. 反向问候: 候选人可问什么

- M4.5 gate 当前进度? 何时可能解锁上链? (答: 看 paper PnL, 不承诺时间表)
- 上链解锁前我做什么? 不会闲着吗? (答: PaperSigner / 监控 / 文档保鲜 / 未来切换准备, 工作量 0.7-0.8 人)
- 公司 entity 在哪? 我个人是否承担链上合规风险? (答: entity 待定, 见老黄 30-90 天 timeline, 上链前必有明确法律主体)
- 失败如何复盘? (答: 公开 postmortem, 不背锅文化)

## 8. 发布渠道

- LinkedIn (英文 JD, target Polymarket / Polygon / DeFi 经验候选人)
- CryptoJobsList / web3.career
- 内推奖金 2 万 RMB (Sprint-2 临时上调, 关键岗)
- 国内: V2EX 求职 / 区块链工程师社群
- 校友群 (老周 + 老叶 networks)
- **避开**: Twitter 公开发, 避免被竞品 Polymarket 注意到
