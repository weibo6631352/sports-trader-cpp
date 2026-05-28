# ADR-013 v2: 部署选址 — 撤回 us-east-1, 推荐 eu-central-1 Frankfurt

- **ID:** ADR-013 v2
- **Date:** 2026-06-W4 (2026-05-29 老郭评审起草)
- **Status:** Draft — 待 W9 W2 实测数据确认后老雷 GM ack 转 Accepted
- **Owner:** 老郭 (架构评审 + F 顾问团协调)
- **撤回:** ADR-013 v1 (2026-06-01 Accepted, us-east-1 单点) — 前提失效, 见 §1
- **输入文档:**
  - `docs/RESEARCH/laowu-w8-polymarket-origin-verification-v1.md` (老吴, 2026-05-28)
  - `docs/RESEARCH/xiaoduan-w8-goalserve-inplay-node-verification-v1.md` (小段, 2026-05-28)
  - `docs/RESEARCH/laopeng-w8-oq-p02-3-inplay-single-source-ack.md` (老彭, 2026-05-28)
- **投票:** 待 W9 W2 实测后发起

---

## §1 老板 verbatim + 撤回 ADR-013 v1

### 1.1 老板 5/28 原话 (verbatim, 不改写)

> "Polymarket CLOB origin 在 AWS eu-west-2 (London), 不是 us-east-1. 都柏林 1ms vs 美东 130ms. 派人验证."

### 1.2 ADR-013 v1 撤回决议

ADR-013 v1 (2026-06-01 Accepted, 6/6 全共识) 基于以下两个前提:

1. Polymarket CLOB origin 在 us-east-1 (Virginia)
2. Goalserve 数据节点在 us-east (NJ/Vultr)

**两个前提均已失效** (老吴 W8 W2 + 小段 W8 W2 实证):

- 前提 1 失效: 老板 5 源三角定位 (QuantVPS / NYC Servers / William Entriken X / TradingVPS / PolyVPN) 交叉确认 Polymarket CLOB origin = eu-west-2 (London). 都柏林 ~1ms vs 美东 ~130ms 数量级差异无法用 CF 路由解释 (老吴 §3 架构分析).
- 前提 2 失效: 小段实测 `inplay.goalserve.com` = 91.206.228.73, Sofia Bulgaria (AS58294 Cloud Wall Ltd.), traceroute 明确终止于 `sof.bg.retn.net`. 非美国, 非印度.

**ADR-013 v1 即日起标记为 Superseded. us-east-1 部署决策 HOLD. 禁止购买 us-east-1 服务器.**

### 1.3 撤回影响

- 老吴: W6 "us-east-1 维持" 指令作废, 改等本 ADR v2 GM ack 后再行动
- 老叶 + 老韩: M5+ nonce 分布式 spec 暂不启动, 等 ADR-013 v2 主节点确认
- Sprint-3 paper runtime 启动地: 由本 ADR v2 §6 联动决定

---

## §2 实证综述

### 2.1 Polymarket CLOB — 大概率 eu-west-2 London

**来源:** 老吴 `laowu-w8-polymarket-origin-verification-v1.md` (2026-05-28)

老吴实测 (2026-05-28 23:44-23:52 CST) 关键发现:

- `clob.polymarket.com` 全程 Cloudflare 反向代理 (anycast IP 104.18.34.205 / 172.64.153.51, AS13335), origin IP 无法通过 DNS/whois 直接确认
- Cloudflare 完全隐藏 origin — "从当前机器无法直接验证 origin region"
- AWS IP range JSON 反查: **无任何 Polymarket IP 落入 AWS 前缀**, 全为 CF anycast 或第三方 CDN
- 老板 5 源三角定位是目前最可信的 origin 证据: 都柏林 1ms vs 美东 130ms 数量级差异无法用 CF 延迟解释

**置信度评估 (老郭):** 无法 100% confirm (CF 全代理屏蔽 origin), 但 5 源 + RTT 数量级差异支持 eu-west-2 判断. W9 W1 欧洲 VPS 实测将提供直接验证 (见 §5).

Cloudflare 架构分析 (老吴 §3):
- 总延迟 = 服务器→CF 最近 PoP + CF PoP→origin (固定路径)
- eu-west-1 Dublin VPS: Dublin→CF Dublin PoP (~0.5ms) + CF Dublin→PM London (~0.5ms) = ~1ms
- us-east-1 Virginia VPS: Virginia→CF 美东 PoP (~2ms) + CF 美东→PM London (~128ms) = ~130ms

### 2.2 Goalserve inplay — Sofia Bulgaria (AS58294 Cloud Wall) 实证

**来源:** 小段 `xiaoduan-w8-goalserve-inplay-node-verification-v1.md` (2026-05-28)

dig 实测结果:

| Endpoint | IP | 地点 | ISP | ASN |
|---|---|---|---|---|
| `inplay.goalserve.com` | 91.206.228.73 | Sofia, BG | Cloud Wall Ltd. | AS58294 |
| `live.goalserve.com` | 91.206.228.78 | Sofia, BG | Cloud Wall Ltd. | AS58294 |

traceroute 实证: hop 11 = `ae0-6.rt.s3c.sof.bg.retn.net` (Sofia Bulgaria), 经 RETN 骨干连接 CloudWall AS58294. 明确无 CDN 层 (非 CF/Akamai 前缀, HTTP 响应无 `cf-ray` 头).

**Goalserve 节点实际分布 (2 节点, 非官方声称 3 节点):**

| 节点 | 域名 | 地点 | 用途 |
|---|---|---|---|
| 节点 1 (EU) | `inplay.*` / `live.*` | Sofia BG (AS58294 CloudWall) | inplay odds (1s HTTP polling) |
| 节点 2 (US) | `www.*` / `livescore.*` / `oddsfeed.*` | Phoenix AZ (AS18501 Codero) | pregame scores / livescore / settlement odds |

**老板 3 节点假设 verify:**
- 美国 Codero: 确认 (Phoenix AZ, 非 NJ)
- 欧洲 意大利/乌克兰: 部分确认 — 欧洲节点存在, 实际在保加利亚 Sofia (东欧), 非南欧/东欧北部
- 印度: 推翻 — in/india/asia/ap/in1/in2 全 NXDOMAIN

### 2.3 Goalserve pregame — US Phoenix Arizona (AS18501 Codero) 实证

**来源:** 小段同上

`www.goalserve.com` / `oddsfeed.goalserve.com` / `livescore.goalserve.com` 全部解析到 69.64.x.x 段, Phoenix AZ, CyberCloud Professionals LLC (Codero), AS18501.

pregame getodds 刷新周期: 30s 增量拉取. 延迟不敏感路径.

---

## §3 候选 4 方案 RTT 矩阵

RTT 数字来源: 老板 verbatim (都柏林 1ms / 美东 130ms) + 老吴 AWS inter-region latency 估算 + 小段 dig 实测节点地理推算. W9 W1 欧洲 VPS 实测将校准 (见 §5).

| 部署方案 | PM CLOB (eu-west-2 London) | Goalserve inplay (Sofia BG) | Goalserve pregame (Phoenix AZ) | 综合评分 |
|---|---|---|---|---|
| **A. us-east-1 Virginia (v1 已撤)** | ~130ms (跨大西洋) | ~145ms (跨大西洋) | ~5ms (美国内陆) | 已撤 — PM + inplay 双跨洋 |
| **B. eu-west-1 Dublin** | ~1ms (老板 verbatim) | ~30ms (都柏林→索菲亚, 欧洲内陆 2200km) | ~100ms (欧洲→美国) | 优 — PM 极致, inplay 良好 |
| **C. eu-west-2 London** | <1ms (同 region) | ~25ms (伦敦→索菲亚, 欧洲内陆 2000km) | ~100ms (欧洲→美国) | 优 — PM 最优, inplay 同 B |
| **D. eu-central-1 Frankfurt** | ~15ms (法兰克福→伦敦) | ~20ms (法兰克福→索菲亚, 欧洲内陆 1500km) | ~100ms (欧洲→美国) | 最优 — inplay 最短, PM 可接受 |

**pregame 100ms 评估:** pregame getodds 为 30s 增量拉取, 非事件驱动, 100ms 网络延迟远小于刷新周期. 可接受.

---

## §4 推荐决议: D eu-central-1 Frankfurt

### 4.1 推荐理由 (老郭)

**理由一: inplay 是延迟最敏感的数据源 — Frankfurt 到 Sofia 最近**

老彭 OQ-P02-3 (`laopeng-w8-oq-p02-3-inplay-single-source-ack.md`) 明确:
- inplay 信号 decay tau = 25s (PM 做市机器人调价窗口)
- 有效信号窗口 5-15s
- 部署在 us-east-1 时, Goalserve inplay (Sofia) → 生产节点 (Virginia) RTT ~145ms, 总延迟 = Goalserve 1s 刷新 + 145ms RTT ≈ 1.145s
- 部署在 Frankfurt 时, Sofia → Frankfurt RTT ~20ms, 总延迟 ≈ 1.020s
- 差值 ~125ms, 占 5-15s 有效窗口的 1-2.5% — 不可忽视的竞争优势

**理由二: PM CLOB 15ms 比 us-east-1 130ms 数量级优**

15ms 对比 130ms, 差 115ms. 下单链路 (惩罚 us-east-1) 在同等 edge 条件下被竞争者抢先 115ms. 这在 decay tau = 25s 的信号窗口中占比显著.

**理由三: Frankfurt 是欧洲两端距离最短的中转点**

London (PM) ←1500km→ Frankfurt ←1500km→ Sofia (Goalserve inplay)

Frankfurt 是 London–Sofia 连线的近似地理中点, 单点部署同时最小化两个最关键数据源的 RTT.

**理由四: Goalserve pregame 100ms 可接受**

pregame getodds 是 30s 周期增量拉取 (小段实证). 100ms 网络延迟占 30s 周期 < 0.34%, 不影响 pregame 信号质量.

**理由五: AWS eu-central-1 Frankfurt 基础设施成熟**

- 3 个可用区 (AZ): eu-central-1a / 1b / 1c
- 成熟 region (2014 年启动), AWS 在欧洲最大 region 之一
- 价格合理: 相比 eu-west-1/2 成本差异在合理范围
- 符合 MVP 单点 + M5+ multi-region 升级路径

### 4.2 方案 B/C 为何非首选

- **B eu-west-1 Dublin**: PM CLOB 极优 (~1ms), 但 Dublin→Sofia inplay ~30ms 比 Frankfurt→Sofia ~20ms 多 10ms. 如果 Goalserve Sofia 是主要竞争瓶颈, B 方案劣于 D.
- **C eu-west-2 London**: PM CLOB 最优 (<1ms), inplay ~25ms 介于 B/D 之间. 与 Frankfurt 差异在于: PM CLOB 优势 15ms vs inplay 劣势 5ms. 给定 inplay 是主信号驱动器 (decay tau 25s, 1s 刷新), Frankfurt 的 inplay 优势权重更高.

**结论: 在 inplay-driven 策略框架下, Frankfurt 的 inplay RTT 最小化优先于 London 的 PM CLOB 亚毫秒优势. 推荐 D.**

---

## §5 实测 verify 计划 (购买前)

### 5.1 W9 W1 — 老吴 3 Region 实测

老吴 spin up 3 个 AWS t3.micro 临时 instance:
- eu-central-1 (Frankfurt)
- eu-west-1 (Dublin)
- eu-west-2 (London)

每个 instance 跑 24h 连续 RTT 测量:

```bash
# PM CLOB RTT (每分钟一次, 24h)
ping -c 60 clob.polymarket.com 2>&1 | tail -1   # ICMP (CF 可能 block, 备用 curl)
curl -s -o /dev/null -w "%{time_connect}\n" \
  --noproxy '*' https://clob.polymarket.com/      # TCP+TLS handshake RTT

# Goalserve inplay RTT (每分钟一次, 24h)
nc -zw3 inplay.goalserve.com 80                  # TCP SYN RTT

# WSS handshake + 第 1 个 event 延迟 (每 5 分钟一次)
# polymarket WSS (sports-api/ws) — 从握手完成到收到第 1 个 event 的时间戳差
```

记录: P50 / P95 / P99 RTT + 抖动 (std). 输出格式: CSV, 落 `docs/RESEARCH/laowu-w9-rtt-3region-<date>.md`.

### 5.2 W9 W2 — ADR-013 v2 final

- 老吴实测数据回来
- 老郭 review 实测数字 vs 本 ADR §3 估算差异
- 若数字支持 Frankfurt → ADR-013 v2 status 改 Accepted
- 若数字显示 London/Dublin 更优 (例如 PM CLOB 15ms 估算偏低实际 <5ms, 且 inplay 差异可忽略) → 老郭修正推荐, 发起投票
- 老雷 GM ack 后购买 Frankfurt 节点

---

## §6 与 paper runtime W11 联动

### 6.1 Sprint-3 W11 paper runtime 启动地

**推荐: eu-central-1 Frankfurt**

paper runtime (小蒋 #20 + 老吴 SRE) 是第一个真实网络环境下的延迟验证场景. 在 Frankfurt 启动 paper runtime 可以:
1. 验证 inplay 信号链路延迟是否符合 §3 估算
2. 验证 PM CLOB WSS 连接稳定性
3. 为 live runtime 热切换 Frankfurt→Frankfurt (无需迁移) 提供保障

### 6.2 不阻塞 W9-W10 spec 工作

W9-W10 各单元 spec 工作 (老周/小梁/小余/老韩) 不依赖部署节点选定, 在本地继续 paper test 无碍. ADR-013 v2 final 在 W9 W2, 不 block W9-W10 主线.

### 6.3 本地 paper test 继续

W9 W2 前: 所有 paper test 继续在本地 (macOS / CI) 运行. 不购买任何 AWS 节点. 老吴 W9 W1 使用 t3.micro spot instance (临时, 24h 实测后释放, 成本可控).

---

## §7 风险

| 风险 | 描述 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| PM origin 非 eu-west-2 (CF 全代理无法 100% confirm) | 若 Polymarket 实际 origin 在 eu-west-1 Dublin 而非 eu-west-2 London | 低 (5 源三角定位一致) | Frankfurt 仍是最优中转, 切 London 影响 <5ms | W9 W1 实测校准; Frankfurt 备 London 切换代价极低 (同区域切换) |
| Goalserve Sofia 节点 IP 变更或迁移 | CloudWall AS58294 RIPE 记录 2022 年建, 较新 | 中 (3-5 年 infra 可能变) | inplay RTT 模型需重新评估 | 季度 nslookup audit + RTT 监控告警 |
| Frankfurt 20ms inplay RTT 实测偏差 | 公开 inter-region latency 估算与实测可能有 ±5ms 误差 | 中 | 若实测 Frankfurt→Sofia 为 30ms 而 Dublin→Sofia 仍 25ms, 方案 B 重新竞争 | W9 W1 实测结果拍板, 不提前购买 |
| AWS eu-central-1 可用性 | AWS region 级故障 (P999 事件) | 极低 | 单点故障 | MVP 阶段可接受 (ADR-013 v1 已明确 M5+ 再评 multi-region); 设计上预留 eu-west-2 failover 配置 |
| 合规地理 | 跨洋链路法律/监管问题 | 参见 GM 2026-05-28 决议 | — | 已 GM 决议暂不纠缠, 详见 `2026-05-28-gm-policy-jurisdictional-deferral.md` |

---

## §8 不耻下问

| 接收方 | 问题 / 任务 | 截止 | 状态 |
|---|---|---|---|
| **@老吴 (A 单元 SRE)** | W9 W1 spin up 3 region (Frankfurt / Dublin / London) t3.micro, 跑 24h RTT 实测: ping clob.polymarket.com / TCP inplay.goalserve.com / WSS handshake 第 1 event | W9 W1 EOW | 待派 |
| **@老雷 GM** | W9 W2 购买决议 ack — 实测数字回来后老郭确认方案, 老雷拍板购买 Frankfurt 节点 | W9 W2 | 待 ack |
| **@老钱 CPO** | 部署预算确认 — eu-central-1 Frankfurt 单 instance (t3.medium 或 c5.large) 月度成本 ~$60-150, 是否在 Sprint-3 预算内 | W9 W2 | 待确认 |
| **@老周 (A 单元主管)** | 架构联动 — paper runtime W11 启动地确认 Frankfurt; 老吴 W9 W1 任务排入 A 单元 Sprint | W9 W1 排入 | 待确认 |
| **@老胡 (E 单元主管)** | Sprint-3 排期同步 — paper runtime W11 与本 ADR v2 final W9 W2 联动, 确保 Sprint-3 planning 包含 Frankfurt node 购买 + paper runtime 启动 | Sprint-3 Planning | 待同步 |

---

## §9 附: ADR-013 v1 撤回摘要

| 项目 | v1 (撤回) | v2 (本 ADR) |
|---|---|---|
| 前提 1 | Polymarket CLOB = us-east-1 | Polymarket CLOB = eu-west-2 London (5 源确认) |
| 前提 2 | Goalserve = us-east NJ (Vultr) | Goalserve inplay = Sofia BG (CloudWall), pregame = Phoenix AZ (Codero) |
| 决议 | W6-M4.5 us-east-1 单点 | W9 W2 ack 后 eu-central-1 Frankfurt 单点 |
| 状态 | **Superseded** | Draft → W9 W2 Accepted |
| 原投票 | 6/6 全共识 (基于错误前提) | 待 W9 W2 实测后发起 |

---

**老郭 (F 协调人 / 架构评审), 2026-05-29**
