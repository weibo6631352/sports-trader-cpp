# 风险登记 v1

- Owner: 老胡 (pm-project-manager)
- Last review: 2026-05-28
- 验收人: 老雷
- 关联 ticket: S1-014
- 姊妹文档: `docs/RESEARCH/laohu-master-gantt-v1.md` (甘特 + 依赖)
- 来源整合: Wave 1-3 各 owner 报告中提到的风险, ADR-001 整改清单, Sprint-1 backlog 风险段

---

## 0. TL;DR

- **20 条风险**, 其中 **Top 5 是 H × H 双高** (Risk-01 / 02 / 03 / 04 / 09), 必须 GM 周报每周露脸
- 风险评分: 影响 (1-5) × 概率 (1-5), Score ≥ 15 = 红, 10-14 = 黄, < 10 = 绿
- 当前 **红色 5 条 (Top 5), 黄色 9 条, 绿色 6 条**
- 评审节奏: 每 Sprint retro 复盘 + 月度老郭 + 老雷 联合评审
- **Top 1: R-01 跨洋链路抖动放大 RM HALT 频率** — 老郭 ADR §3.2 已收紧 STALE 阈值 (WSS 2s/10s), 老吴 S1-021 网络实测在 Sprint-2 末出报告校准, 但实测如果 p99.9 > 2s, RM 假阳性 HALT 会高频触发, **可能直接拖垮 M2 (无法持续生成有效信号) / M3 (testnet 频繁停摆)**

---

## 1. 风险表 (20 条)

> 影响 (I) / 概率 (P) 各 1-5; 评分 = I × P; 状态 = 新 / 缓解中 / 关闭 / 触发

| 编号 | 描述 | 类型 | I | P | 评分 | 缓解 | Owner | 来源 | 状态 |
|---|---|---|---|---|---|---|---|---|---|
| R-01 | 跨洋链路抖动 (us-east 实测 p99.9 > 2s) 触发 RM STALE 假阳性, RM 频繁 HALT 拖垮 M2/M3 | 性能/RM | 5 | 4 | **20** | 老吴 S1-021 实测先行; 老郭 ADR §3.2 阈值需根据实测校准; Sprint-2 retro 重定阈值 | 老吴 + 老韩 | ADR-001 §3.2; 老吴 §3 | 缓解中 |
| R-02 | RiskGateway 绕过 (link 阻断 + CI 扫描 + runtime trip-wire 三层任一未落地) | 安全/合规 | 5 | 4 | **20** | ADR-001 W-2 拍板; 老韩 + 老郭 双签 PR; @老练 CI 静态扫描落地; @小郑 runtime trip-wire 落地 | 老韩 + 老郭 | ADR-001 §2.2 W-2 | 缓解中 |
| R-03 | 私钥泄露 / signer 被 RCE 接管 (依赖 CVE / supply chain 投毒) | 安全 | 5 | 3 | **15** | 老沈 威胁模型 E-01 已识别; vcpkg pin commit; OSV watch; signer 进程隔离 + UDS + 不出网 | 老沈 + 老孙 + 老吴 | 老沈 §3 E-01; 老孙 §2.1 | 缓解中 |
| R-04 | 资金对账失败 / 链上 nonce 不一致, 导致重复广播或漏单 | 资金/链上 | 5 | 3 | **15** | nonce manager WAL fsync (老周 §9.4); SAFE_MODE 启动对账 5 分钟才解锁 (ADR W-3); 老叶 多 RPC 交叉验证 | 老叶 + 老周 + 老韩 | 老叶 S1-009; ADR §2.3 边界 4 | 缓解中 |
| R-05 | Polymarket negRisk 合约 verifyingContract 选错 → 签名拒收, 影响 M3 下单端到端 | 协议/集成 | 4 | 4 | **16** | 老李 §8.1 风险 6 已识别; 序列化层强制 propagate `negRisk` 字段; 单测覆盖两个 verifyingContract 分支 | 老李 + 老周 | 老李 §8.1 风险 6 | 新 |
| R-06 | Polymarket 官方 `seconds_delay` (1-3s 反套利) 吃光 PnL, 信号不达预期 | 量化/性能 | 4 | 4 | **16** | 老李 §8.1 风险 2; 小程 信号设计已纳入 (xiaocheng §4.5); 回测必含 seconds_delay 模拟 | 小程 + 小蒋 | 老李 §8.1 风险 2 | 新 |
| R-07 | HC-01 老冀 / HC-02 小秦 招聘失败或延期, M2/M3 关键岗位空缺 | 人事 | 4 | 4 | **16** | 多渠道 (LinkedIn + 内推 + 行业论坛); 老叶 + 老周 兜底 (本文档 §3); M3 顺延 2 周可吸收 | 小林 + 老胡 | S1 风险登记; 甘特 §4.4 | 缓解中 |
| R-08 | 老周 自研 WAL (架构 F-4) 实现不完整, 崩溃恢复留下脏状态导致 M5 后续事故 | 架构/数据 | 5 | 3 | **15** | ADR-001 C-Z4 强制 Sprint-1 末出最小可证明设计; @小段 联合设计; chaos test (fsync hang / WAL 满) | 老周 + 小段 + 小宋 | ADR §2.4 F-4 | 缓解中 |
| R-09 | Sharpe > 1 目标不达 (回测样本 < 500 场 或 P0-01 信号 IS/OOS 不一致), M2 失败 | 量化 | 5 | 3 | **15** | 小程 信号备选 12 条; 小余 历史回填 2 年; 小蒋 OOS 验证; 必要时降阈值至 0.8 (老雷批) | 小梁 + 小程 + 小蒋 | 小程 §9.1 OQ-1; OKR KR-C-3 | 新 |
| R-10 | Goalserve API 限流 / IP whitelist 失效 影响数据接入联调 (M1) | 数据 | 4 | 3 | 12 | S1 风险表已列; GOALSERVE_PROXY 已配; 老吴 us-east-1 直连方案 (退掉国内 proxy); 速率监控 (小郑) | 小段 + 老吴 | S1 风险表; 老吴 §2.2 | 缓解中 |
| R-11 | Polygon RPC 节点单点 (Alchemy / QuickNode) 故障或返回伪数据 | 链上 | 4 | 3 | 12 | 老叶 多 RPC 选型 (S1-009); 关键调用交叉验证; nonce / block height sanity check | 老叶 + 老周 | 老沈 §3 S-02; 老叶 S1-009 | 缓解中 |
| R-12 | audit WAL group commit 实现 bug 导致 audit 漏写, 违反 G2 合规 (任意拒单可查) | 风控/合规 | 5 | 2 | 10 | ADR §3.3 设计已成熟 (Oracle/MySQL 模式); @老姜 加 throughput/fsync latency 压测; @小宋 chaos (fsync 线程 hang) | 老韩 + 老姜 + 小宋 | ADR §3.3; 老韩 §10.3 R-3 | 缓解中 |
| R-13 | sqlite idempotency 表崩溃恢复窗口 (≤ 60s) 同 key 二次放行 | 风控 | 3 | 4 | 12 | ADR F-7; nonce manager 兜底 (nonce 不复用); v0.2 加 PRAGMA NORMAL + WAL mode + 60s checkpoint | 老韩 | ADR §3.4 F-7 | 缓解中 |
| R-14 | c6i.large 容量不够 (老周 §8 列 8 core, 老吴选 c6i.large 实际 2 vCPU) | 部署/性能 | 3 | 4 | 12 | ADR F-3 W-6 老雷待批升级 c6i.xlarge (+$100/月); 或老周 §8 重画 core 分配接受 10% 退步 | 老雷 + 老周 + 老吴 + 老钱 | ADR §2.4 F-3 W-6 | 待 GM 决议 |
| R-15 | Goalserve livescore vs Polymarket 结算口径不一致 (UMA 仲裁延迟 / 比分差异) | 数据/合规 | 4 | 3 | 12 | 小余 + 小冯 字段口径对比报告 (M1 前); 比分差异 > 阈值触发 RM HALT (D-06) | 小余 + 小冯 + 老韩 | OKR KR-D-6; 老沈 §6 场景 D | 新 |
| R-16 | 跨进程 SHM ring 选型未定 (老周 §6.2) 影响 M3 主交易进程 + 旁路进程通信 | 架构 | 3 | 3 | 9 | ADR C-Z5 派 @小石 Sprint-2 补; 起步推荐 boost::interprocess + 自研 SPSC over SHM | 小石 + 老周 | ADR §2.4 F-5 | 新 |
| R-17 | Kelly 系数错配 / drawdown 参数不当导致纸面/实盘单笔超限 (M4 / M5) | 风控/资金 | 4 | 2 | 8 | KR-B-4 小梁 + 老韩 双签; KELLY_FRACTION_WARNING = 0.5 × 在 WARNING 状态自动减半 (老韩 §7); audit 全量 | 小梁 + 老韩 | 老韩 §7; ADR F-8 | 新 |
| R-18 | Polymarket maker rebate 规则不明 (老李 OQ-9), 做市策略 PnL 估计偏差 | 量化 | 3 | 3 | 9 | 不在 S1 范围; M2 后小程 + 老彭 跟进; MVP 不依赖 maker rebate, 仅作 upside | 小程 + 老彭 | 老李 §8.2 OQ-9 | 监控 |
| R-19 | 实盘上线时 dependency CVE 紧急爆出, 老沈 / 老吴 来不及打补丁 | 安全/运维 | 4 | 2 | 8 | OSV watch 7/24; SBOM 月度 diff (老沈 §7); SAFE_MODE 启动 + kill switch 兜底 | 老沈 + 老吴 + 老何 | 老沈 §3 E-01; 老沈 §7 | 监控 |
| R-20 | 老胡 / 老周 / 老韩 任一关键单点请假 / 离职, M3-M5 节点失主 | 人事/组织 | 4 | 2 | 8 | 各部门 buddy 制 (HC backlog 评委表); 关键文档归档 (小米); 周报双备份 (老雷 + 老郭 抄送) | 老雷 + 老胡 + 小林 | HC backlog §评委 | 监控 |

### 1.1 评分图谱 (I × P 热力)

```
              P=1     P=2     P=3     P=4     P=5
I=5 (致命)    --     R-12    R-03    R-01   --
                    R-19?   R-04    R-02
                            R-08    R-09(P=3)
                            R-09
I=4 (严重)    --     R-17    R-10    R-05   --
                    R-19    R-11    R-06
                    R-20    R-15    R-07
I=3 (中等)    --     --      R-16    R-13   --
                            R-18    R-14
I=2 (轻微)    --     --      --      --     --
I=1 (微小)    --     --      --      --     --
```

### 1.2 状态分布

| 状态 | 数量 | 编号 |
|---|---|---|
| 新 | 6 | R-05 / R-06 / R-09 / R-15 / R-16 / R-17 |
| 缓解中 | 11 | R-01 / R-02 / R-03 / R-04 / R-07 / R-08 / R-10 / R-11 / R-12 / R-13 / R-14 |
| 监控 | 3 | R-18 / R-19 / R-20 |
| 关闭 | 0 | -- |
| 触发 | 0 | -- |

---

## 2. Top 5 风险特别关注

> 评分 ≥ 15 = 红色, 必须 GM 周报每周露脸, 老雷 + 老郭 月度评审单独点名

### Top 1: R-01 跨洋链路抖动放大 RM HALT 频率 (评分 20)

**为什么是 Top 1:**

老郭 ADR §3.2 已经把 RM STALE 阈值从老韩 v0.1 的 30s/60s 收紧到 WSS 2s/10s — 这是基于"主节点在 us-east-1 同区, p99 RTT < 10ms"的假设. 但**这个假设到 Sprint-2 末才有实测数据 (老陈 + 老吴 + 小段 S1-021)**. 如果实测 p99.9 出现 > 2s 的抖动 (跨洋链路有这个可能, 老吴 §3.1 抽样数据 p99 已经 280ms, p99.9 不排除秒级):

- WSS STALE 阈值 2s 会被频繁触发 → RM 进 DEFERRED → 信号无法落地下单
- 连续 DEFERRED 累积超过 10s → RM 进 HALT → 整个交易暂停
- M2 (W10) 信号 v1 灌真数据时, 如果阈值频繁假阳性, 回测 Sharpe 失真; 更糟的是实时跑的话, 实盘根本下不出单

**缓解动作 (按时间排序):**
1. **Sprint-2 末 (W4)**: 老陈 + 老吴 + 小段 S1-021 实测报告必须含 p50/p95/p99/p99.9/max 五档数字, 老郭基于实测重定 RM 阈值 (ADR §3.2 配套要求)
2. **W6 (M1 前)**: 老韩 v0.2 阈值实现 + 配 DEFERRED 频率 metric (小郑); 实际跑联调 48h, DEFERRED 频率 < 5%/h 才算 M1 通过
3. **M2 / M3 落地后**: 持续观察 1 个月 DEFERRED 频率, Sprint-6 retro 校准 (若实测 p99.9 长期可控, 阈值可适度放宽)

**触发条件 (转 "触发" 状态):**
- Sprint-2 末实测 p99.9 > 2s → 必须升级老雷, 重审 RM 阈值方案 (可能要为跨洋抖动加二级阈值: "短抖动" → 标记不 HALT, 累积超阈 → HALT)

**Owner:** 老吴 (实测) + 老韩 (RM 实现) + 老郭 (阈值决议)

### Top 2: R-02 RiskGateway 绕过 (评分 20)

**为什么:**

ADR-001 W-2 老雷已会签的硬决议: build 编译期 link 阻断 + CI 静态扫描 + 运行时 trip-wire 三层防御. 任一层未落地, RM 红线 D-02 ("零风控失效事件") 就失守. 老郭 ADR §2.2 明示这是"安全模块的防御纵深, 缺一不可".

**缓解动作:**
1. **Sprint-2 (W2-W4)**: 老周 v0.2 落地 CMake link 阻断 (`risk_gateway` 唯一对外 target, `risk_internal` 不暴露)
2. **Sprint-2 (W4 前)**: @老练 testing-coach 落地 CI grep 扫描脚本, signer/clob/router 任何函数调用签名 API 前必须有前序 evaluate() 调用, 不通过 = block merge
3. **Sprint-3 (W6 前)**: @小郑 metrics 落地 runtime trip-wire (last_audit_id 1:1 匹配, 不匹配 abort)
4. **永久**: 老郭 + 老韩 双签 PR (任何修改 risk/ 目录 public 接口)

**触发条件:**
- 任一层在 Sprint-3 末仍未上线 = M3 hard block, 整个 milestone 顺延

**Owner:** 老周 (link 阻断) + 老练 (CI) + 小郑 (runtime) + 老韩 (双签)

### Top 3: R-03 私钥泄露 / signer RCE (评分 15)

**为什么:**

老沈 威胁模型 E-01 评分最高 (20 分). 第三方依赖 CVE → RCE → 拿到主机 shell → 直接动用私钥转账, 这是公司层级灭顶事件 (CLAUDE.md 红线).

**缓解动作:**
1. vcpkg pin commit hash (老沈 M-S1) — Sprint-2 末必须落地
2. OSV watch 7/24 接入飞书告警 (老沈 + 老吴) — Sprint-2
3. signer 进程隔离 + UDS + 不出网 + cgroup 限流 (老孙 §2.1 方案 G + 老周 D1) — M3 前
4. 月度 SBOM diff + 升级 review (老沈 + 老何) — M1 起执行
5. M5 前: 红蓝对抗演练 (老沈 + 老吴 模拟 RCE)

**触发条件:**
- OSV 告警出现 critical CVE 影响本项目依赖 → 4h 内老沈给出评估 + 缓解
- 任一 signer 接收非授权 signature 请求 (runtime trip-wire 触发) → 立即 abort + 告警 + 老雷 page

**Owner:** 老沈 (主) + 老孙 (signer) + 老吴 (运维) + 老何 (依赖)

### Top 4: R-05 negRisk 合约 verifyingContract 选错 (评分 16)

**为什么:**

老李 §8.1 风险 6 + ADR-001 跨文档一致性检查都点到这一条. negRisk 市场和普通市场用不同的 verifyingContract (`0x4bFb...` vs `0xC5d5...`), 选错就是签名拒收, M3 下单端到端跑不通. 这不是低级 bug 而是协议结构性问题 (Polymarket 把 negRisk 系列赛专门拆了一个 Exchange 合约).

**缓解动作:**
1. **Sprint-2**: 老李 + 老叶 在协议规范 v1.1 里明示 verifyingContract 选择决策树 (检查 `market.negRisk` 字段 → 选对应合约)
2. **Sprint-3**: 老周 序列化层强制 propagate `negRisk` 字段到下单路径, 缺该字段 = 编译期拒绝
3. **M3 前**: 单测覆盖两个 verifyingContract 分支, testnet 各跑 ≥ 10 笔
4. **永久**: 老叶 OQ 1 (Exchange / NegRiskExchange / NegRiskAdapter 三个合约链上权威地址确认 by etherscan `getImplementation()`)

**触发条件:**
- testnet 下单出现 "Signature verification failed" 错误 → 立刻停下单 + 老李 + 老叶 协同排查 4h SLA

**Owner:** 老李 (协议) + 老叶 (合约确认) + 老周 (序列化)

### Top 5: R-07 HC-01 / HC-02 招聘失败 (评分 16)

**为什么:**

老冀 (链上运维) + 小秦 (信号→执行) 是 Q2 P1 招聘, 直接挂 M2 + M3. 候选池薄 (国内 C++ + 链上 + 量化 三重背景 + 跨洋部署经验同时具备的, 招聘市场少). 现在 (5/28) JD 还没发, 6/4 出, 6/10 开始面, 6/30 入职目标本身就紧.

**缓解动作:**
1. **本周 (W1)**: 小林 JD 优先发布 (6/4 deadline), 渠道 LinkedIn + 内推 + 行业论坛三管齐下
2. **Sprint-1 末 (W2)**: 候选池 ≥ 5 人/岗, 否则升级老雷扩渠道 (猎头 / 海外远程)
3. **W4**: 若 6/30 入职目标失守, 启动兜底 (老叶 兼链上运维 + 老周 兼信号-执行, 老雷批准 M3 顺延 2 周)
4. **永久**: 入职 buddy 制 (老周 直带老冀和小秦), ramp-up 加速

**触发条件:**
- 6/15 前候选池 < 3 人/岗 → 黄牌
- 6/22 前未发 offer → 红牌, 启动兜底
- 7/15 前未实际入职 → 触发 M3 顺延决议

**Owner:** 小林 (招聘) + 老胡 (跟进) + 老雷 (决议)

---

## 3. 风险评审节奏

### 3.1 评审日历

| 频率 | 评审 | 参与方 | 输出 |
|---|---|---|---|
| 每日 | Standup blocker 同步 | 老胡 + 当事 owner | Standup 阻塞清单 |
| 每周 | GM 周报 §4 含 Top 1 风险状态 | 老胡 → 老雷 | 周报文档 |
| 每 Sprint (双周) | Sprint retro 风险登记更新 | 老胡 + 5 单元 owner | 本文档 vN+1 |
| 每月 | 老雷 + 老郭 联合风险评审 | 老雷 + 老郭 + 老胡 + 红色 Owner | 风险升降级决议 |
| Milestone 节点 | M1/M2/M3/M4/M5 评审含风险关闭 + 新增 | 老胡 + 老郭 + 老韩 + 老雷 | DoD 验收单 |

### 3.2 风险升级路径

```
绿 → 黄: owner 缓解动作不到位, 评分上升 5+ → 老胡 列入下周 GM 周报
黄 → 红: 触发条件命中, 或缓解动作 1 周内无进展 → 老胡 直接 page 老雷 + 老郭
红 → 触发: 风险已实际发生 → 启动应急 SOP (CLAUDE.md §8 风控红线条目 → page 老雷 + 老沈 + 老韩)
```

### 3.3 新风险加入流程

1. 任何 owner 在 owner 文档 / Sprint retro / standup 提出新风险
2. 老胡 1 个工作日内评估 I × P 评分, 加入本文档 vN+1
3. 评分 ≥ 15 = 红色, 立即上 GM 周报 + 通知老雷
4. 评分 10-14 = 黄色, 下次 Sprint retro 评审
5. 评分 < 10 = 绿色, 月度评审时一并复盘

### 3.4 风险关闭流程

风险可关闭的条件 (满足任一):
- 缓解措施全部落地 + 触发条件长期 (≥ 2 个月) 未命中, owner + 老胡 共同签字关闭
- Milestone 通过后, 与该 milestone 强绑定的风险 (例如 R-05 在 M3 通过后) 自动关闭
- 老雷 决议关闭 (例如 scope 调整, 该风险所属功能被剔除)

关闭记录保留在本文档历史版本, 不删除.

### 3.5 风险编号规则

- 风险编号 R-NN 顺序递增, 不复用
- 关闭后编号保留, 状态改 "关闭"
- 触发后编号保留, 状态改 "触发", 并在 INCIDENTS/ 开事故复盘文档

---

## 4. 风险源溯 (各 owner 文档 -> 风险表)

| owner 文档 | 提到的风险 | 对应本表编号 |
|---|---|---|
| 老李 S1-002 §8.1 | 跨洋 880ms / seconds_delay / 3% taker fee / Cloudflare UA / API key 绑死 / negRisk 合约 | R-01 / R-06 / -- / R-10 / -- / R-05 |
| 老韩 RM v0.1 §10 + ADR §3 | STALE 阈值 / audit fsync / sqlite idempotency | R-01 / R-12 / R-13 |
| 老周 架构 v0.1 + ADR §2 | 自研 WAL / link 阻断 / c6i 容量 / fail-fast 边界 | R-08 / R-02 / R-14 / -- |
| 老沈 威胁模型 §3 | E-01 RCE / S-02 假 RPC / T-03 RM 配置篡改 | R-03 / R-11 / R-02 |
| 老孙 S1-005 §2 | 私钥本地 vs HSM | R-03 |
| 老吴 S1-010 §3 | 跨洋链路 RTT 200ms+ | R-01 |
| 老姜 S1-011 §6 | 内环延迟 vs 外环跨洋 | R-01 |
| 老叶 S1-009 | Polygon RPC 单点 / nonce 一致性 | R-11 / R-04 |
| 小程 S1-017 §4.5 + §9.1 | 模型外推 / seconds_delay / Sharpe 不达 | R-09 / R-06 |
| 小梁 S1-007 | 二元拆分 vig 不适用 | R-09 |
| 老彭 S1-012 | 3% taker fee 行业可比 | R-06 |
| 小余/小段 S1-003 | Goalserve 限流 / 字段 mapping | R-10 / R-15 |
| 老黄 S1-006 | 合规红线 D-02 (零绕过) / D-06 (30s HALT) | R-02 / R-01 |
| 小林 HC backlog | Q2 招聘候选池薄 | R-07 |
| 老钱 S1-008 | MVP scope 减项空间 (兜底) | R-07 / R-09 |
| Sprint-1 风险表 | Goalserve 限流 / Polymarket 鉴权 / 跨洋稳定性 / HC 候选 | R-10 / -- (鉴权已通) / R-01 / R-07 |

---

## 5. 老胡附言

- 20 条不是上限, 是当前 (2026-05-28) 我从各 owner 报告 + ADR + Sprint-1 backlog 能整出来的, 实测和实践中一定会新增. Sprint-2 retro 我会再加一轮.
- Top 5 里 R-01 和 R-02 各占 20 分, 我把 R-01 排第一, 因为 R-02 的缓解路径在 ADR 里已经被老郭设计死了 (老周老韩按图施工, 老练老郭 review), 落地概率高; R-01 的实测数据老吴还没拿到, 这是真的 "未知数".
- R-09 (Sharpe 不达) 我没排进 Top 5 不是因为概率低, 是因为这条**真到了 M2 验收点才会暴露**, 现在没有可行动的缓解动作 (除了等小程 IS 跑出来). 我把这条单独标注: M2 验收前 2 周 (W8) 老雷需要决定 "Sharpe 阈值要不要降到 0.8"; W9-W10 我会持续追小蒋的 IS/OOS 数字进周报.
- R-14 (c6i 容量) 不大也不小, 评分 12 (黄色), 但**这是 GM 唯一需要本周决策的事项** (老雷批 +$100/月 升级 c6i.xlarge, 或老周重画 core 分配). 我会在下周 GM 周报里点你, 决议前会先发老钱过预算.
- 风险登记 v1 不是"最终版", 是"启动版". 老雷月度评审一定有新洞察, 我会按 §3.3 加新条目.

— 老胡, 2026-05-28
