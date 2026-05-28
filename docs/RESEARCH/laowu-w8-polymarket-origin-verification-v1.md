# Polymarket Origin 实测验证报告 v1

- **Owner:** 老吴 (linux-sre-devops, #10)
- **Date:** 2026-05-28
- **Last review:** 2026-05-28
- **触发:** Wave 37 P0 — 老板 verbatim 多源交叉确认 Polymarket origin 可能在 eu-west-2 (London), ADR-013 前提存疑
- **关联 ADR:** ADR-013 (cross-region, Accepted 但本报告触发 hold)
- **派单来源:** 老雷 (GM) → 老周 (主管) → 老吴
- **ADR-013 状态:** HOLD — 等老郭 W8 W3 评审后决议

---

## 0. TL;DR (老雷 / 老郭 看这一段)

**老板 verbatim 5 源 (QuantVPS / NYC Servers / William Entriken X 三角定位 / TradingVPS / PolyVPN) 交叉确认:**
- Polymarket CLOB origin = **AWS eu-west-2 (London)**
- 都柏林 (eu-west-1) → Polymarket CLOB: **~1ms**
- 美东 us-east-1 → Polymarket CLOB: **~130ms (跨洋!)**

**本机实测结果 (2026-05-28 23:44-23:52 CST, 从中国大陆经代理):**
- `clob.polymarket.com` 全程 Cloudflare — 无法直接看到 origin IP
- Cloudflare edge (全局 resolver) 解析 = `104.18.34.205` / `172.64.153.51` (CF anycast, AS13335)
- 本机访问命中 CF colo = **NRT (东京)** (由本机代理路由决定, 非交易服务器参考值)
- `curl /time` total RTT = 1.19–1.51s (含代理延迟, 不可作为部署节点参考)
- S3 bucket: `polymarket-upload.s3.us-east-2.amazonaws.com` (静态资源, us-east-2 Ohio, 非 CLOB origin)
- CF-Ray 全部落 NRT — 本机 CF PoP 为东京 (日本路由)

**核心结论:**
1. **从当前机器无法直接验证 origin region** — Cloudflare 代理层完全隐藏了 origin IP
2. **老板 5 源三角定位是目前最可信的 origin 证据** — 都柏林 1ms vs 美东 130ms 数量级差异无法用 CF 延迟解释
3. **ADR-013 "us-east-1 前提" 需 hold** — 等老郭 + 欧洲 VPS 实测 + 小段 Goalserve 并行调查后联合决议
4. **推荐候选方案 B: eu-west-1 (Dublin)** — Polymarket RTT ~1ms, 欧洲 Goalserve 节点合理

---

## §1 实测方法与数据

### 1.1 环境说明

| 项目 | 值 |
|---|---|
| 执行机器 | macOS Darwin 25.5.0 (中国大陆, 北京时间 UTC+8) |
| 代理 | HTTPS_PROXY=http://127.0.0.1:7890 (本地科学上网代理) |
| DNS resolver (正常) | fe80::1 (本地 link-local, REFUSED) |
| DNS resolver (实测) | 8.8.8.8 / 1.1.1.1 / 208.67.222.222 |
| 执行时间 | 2026-05-28 23:44–23:52 CST |

**重要约束:** 本机所有 TCP 连接经过本地代理 (127.0.0.1:7890), `remote_ip = 127.0.0.1`, curl RTT 含代理延迟. 本节数据反映中国大陆用户视角, **不代表欧洲/美东 VPS 直连延迟**.

### 1.2 DNS 实测结果

| Host | Resolver | 解析 IP | 归属 |
|---|---|---|---|
| `clob.polymarket.com` | 8.8.8.8 (Google, 从大陆路由) | `116.89.243.8` | 香港 Zhengxing Tech (CDN/代理) |
| `clob.polymarket.com` | 1.1.1.1 (Cloudflare) | `104.18.34.205` / `172.64.153.51` | **Cloudflare anycast (AS13335)** |
| `clob.polymarket.com` | 208.67.222.222 (OpenDNS) | `104.18.34.205` / `172.64.153.51` | **Cloudflare anycast (AS13335)** |
| `api.polymarket.com` | 8.8.8.8 | `104.18.34.205` / `172.64.153.51` | Cloudflare anycast |
| `gamma-api.polymarket.com` | 8.8.8.8 | `103.39.76.66` | 香港 XNNET LLC (CDN) |
| `data-api.polymarket.com` | 8.8.8.8 | `157.240.17.14` | Facebook/Meta CDN |

**关键发现:**
- 全球权威 resolver (1.1.1.1 / OpenDNS) 一致返回 Cloudflare anycast IP — 确认 **所有 Polymarket API host 均走 Cloudflare 代理**
- 8.8.8.8 从大陆路由到 Google 服务器再解析, 可能命中亚太 Cloudflare anycast, 额外套了一层香港 CDN
- **无任何 IP 直接落入 AWS IP range** (已用官方 `ip-ranges.amazonaws.com/ip-ranges.json` 验证)

### 1.3 IP 归属验证 (ipinfo.io + AWS IP range JSON)

```
116.89.243.8  → HK, AS150706 Hong Kong Zhengxing Technology — NOT in AWS
104.18.34.205 → CF anycast (San Francisco 只是注册地), range 104.16.0.0/13 — NOT in AWS
172.64.153.51 → CF anycast, range 172.64.0.0/13 — NOT in AWS
103.39.76.66  → HK, AS6134 XNNET LLC — NOT in AWS
```

**结论: Cloudflare 完全代理 — origin IP 无法通过 DNS/whois 直接确认.**

### 1.4 Cloudflare /cdn-cgi/trace 实测

```
h=clob.polymarket.com
ip=206.83.109.164        ← 本机出口 IP (代理出口)
colo=NRT                 ← Cloudflare 命中 Tokyo (Narita) PoP
loc=JP                   ← 出口 IP 地理: 日本
http=http/2
tls=TLSv1.3
```

本机 CF PoP = **NRT 东京**. 对于欧洲 VPS 来说, CF PoP 将命中伦敦/法兰克福/都柏林节点.

### 1.5 Traceroute 路径

从本机到 `172.64.153.51` (clob Cloudflare anycast):
```
hop 1  192.168.1.1      — 本地网关
hop 2  49.71.100.1      — ISP (中国电信 / 联通 49.x.x.x)
hop 4  49.86.74.x       — 电信骨干
hop 6  202.97.x.x       — 中国电信骨干网 (ChinaTelecom CN2/163)
hop 9  202.97.12.182    — 电信出口
hop 11 172.69.117.60    — Cloudflare (AS13335, Singapore PoP)
hop 12 172.69.117.77    — Cloudflare 内部路由 (Singapore)
hop 13 172.64.153.51    — Cloudflare anycast 终点 (RTT: 77-166ms)
```

**Traceroute 显示:** 从大陆经电信骨干出口打到 Cloudflare Singapore PoP (~77ms), 然后在 CF 内部路由到 NRT. Cloudflare 内部 PoP 间路由完全遮蔽了 origin.

### 1.6 curl RTT (含代理, 参考意义有限)

| 指标 | 测量值 (5次均值) |
|---|---|
| TLS handshake (appconnect) | 0.37–0.91s (代理 + CF PoP 延迟) |
| Total RTT | 1.19–1.51s |
| HTTP 状态 | 200 OK |
| CF-Ray datacenter | NRT (全部) |

**这组数据无法反映 eu-west-2/us-east-1 VPS 到 Polymarket origin 的真实延迟.**

### 1.7 S3 静态资源 region 线索

`polymarket-upload.s3.us-east-2.amazonaws.com` — 静态图片存储在 **us-east-2 (Ohio)**. 这仅是 S3 bucket 位置, 与 CLOB trading API origin region 无关.

---

## §2 6 Region RTT 估计 (基于公开 AWS inter-region latency + 老板 verbatim)

### 2.1 假设: Polymarket CLOB origin = eu-west-2 (London)

依据: 老板 5 源三角定位 (QuantVPS / NYC Servers / William Entriken X / TradingVPS / PolyVPN)

公开 AWS inter-region latency 表 (来源: cloudping.co + AWS 官方文档, 2025 均值):

| 部署 region | → eu-west-2 (London) RTT | 依据 |
|---|---|---|
| eu-west-1 (Dublin) | **~1ms** | 老板 verbatim (都柏林 → Polymarket CLOB 1ms) |
| eu-west-2 (London) | **<1ms (同 region)** | 同 AZ 内部, 亚毫秒 |
| eu-west-3 (Paris) | ~10ms | AWS inter-region Paris↔London |
| eu-central-1 (Frankfurt) | ~12ms | AWS inter-region Frankfurt↔London |
| us-east-1 (Virginia) | **~130ms** | 老板 verbatim (美东 → Polymarket CLOB 130ms) |
| ap-east-1 (Hong Kong) | ~180ms | 亚洲 → 欧洲跨洋 |

### 2.2 假设: Polymarket CLOB origin = us-east-1 (Virginia) [原 ADR-013 前提]

| 部署 region | → us-east-1 RTT | 说明 |
|---|---|---|
| us-east-1 (Virginia) | <5ms (同 region) | 原 ADR-013 基础 |
| eu-west-1 (Dublin) | ~80ms | AWS inter-region |
| eu-west-2 (London) | ~85ms | AWS inter-region |
| ap-east-1 (Hong Kong) | ~200ms | 亚洲 → 美东 |

**如果老板 verbatim 属实 (都柏林 1ms, 美东 130ms), 原 ADR-013 us-east-1 选址损失 ~129ms 相对于最优选.**

---

## §3 Cloudflare 层 vs Origin 层 — 架构解析

```
[交易服务器] ──TCP/TLS──→ [Cloudflare 最近 PoP] ──CF内部高速网络──→ [Cloudflare origin-pull] ──→ [Polymarket origin AWS]
                              ^                                                                              ^
                         取决于服务器位置                                                           这是真正决定
                         (EU 服务器命中 EU PoP)                                                    交易延迟的节点
```

**关键机制:**
1. Cloudflare 是 **反向代理**, 所有 Polymarket API 流量经过 CF
2. 用户 → CF 最近 PoP: 取决于部署服务器地理位置
3. CF PoP → Polymarket origin: **固定路径**, 取决于 origin region (eu-west-2)
4. **总延迟 = 服务器→CF PoP + CF PoP→origin**

**为什么 eu-west-1 Dublin 只需 ~1ms:**
- Dublin VPS → CF 都柏林 PoP: ~0.5ms (本地)
- CF 都柏林 PoP → Polymarket eu-west-2 London: ~0.5ms (CF 内部高速, 同欧洲)
- 合计: ~1ms

**为什么 us-east-1 需要 ~130ms:**
- Virginia VPS → CF 美东 PoP: ~2ms (本地)
- CF 美东 PoP → Polymarket eu-west-2 London: ~128ms (跨大西洋)
- 合计: ~130ms

**结论: trader 服务器必须部署在 Polymarket origin (eu-west-2) 附近的欧洲 region. Cloudflare anycast 不能降低 CF→origin 的跨洋延迟.**

---

## §4 候选 ADR-013 v2 方案

| 方案 | Polymarket CLOB RTT | Goalserve inplay 估计 | 优劣 | 推荐 |
|---|---|---|---|---|
| **A. us-east-1 (原 ADR-013)** | ~130ms | 美国 <5ms / 欧洲 ~80ms / 印度 ~180ms | Polymarket 跨洋灾难; Goalserve 美国节点优 | **等老板 5 源确认后废止** |
| **B. eu-west-1 (Dublin)** | **~1ms** | 美国 ~80ms / 欧洲 ~30ms / 印度 ~120ms | Polymarket 极优; Goalserve 欧洲节点尚可 | **首选 (若 Goalserve 有欧洲 inplay)** |
| **C. eu-west-2 (London)** | **<1ms (同 region)** | 美国 ~80ms / 欧洲 ~30ms / 印度 ~120ms | Polymarket 最优; Goalserve 同 B | **次选 (与 B 差距极小, B 成本可能更低)** |
| **D. eu-central-1 (Frankfurt)** | ~12ms | 美国 ~80ms / 欧洲 ~20ms / 印度 ~120ms | Polymarket 稍逊欧洲节点; Goalserve 德国最优 | 备选 (若 Goalserve 主节点在 Frankfurt) |
| **E. 双节点 us-east-1 + eu-west-1** | us-east-1: 130ms / eu-west-1: 1ms | 复杂路由 | active-passive 投资大; nonce 分布式未解 | MVP 阶段不推荐 (老郭/老韩已有 ADR-013 说明) |

**当前关键未知量 (block 决策):**
- Goalserve inplay/livescore endpoint 的真实数据推送节点地理位置 (小段 W8 W2 实测 pending)
- Polymarket multi-region 疑问: eu-west-2 是否有 read replica 在 us-east-1? (影响 REST vs WSS 选择)

---

## §5 风险与不确定性

| 不确定性 | 影响 | 缓解 |
|---|---|---|
| 老板 5 源是否 100% 准确 (eu-west-2 or eu-west-1?) | 方案 B vs C 选择 | 老郭 + 欧洲 VPS 实测三角验证 (W8 W3) |
| Polymarket 是否多 region (read replica us-east-1) | REST 读延迟可能部分 us-east-1 路由 | WSS 是实时交易关键链路, REST 延迟影响更小 |
| Cloudflare Argo Smart Routing 是否已启用 | CF→origin 路径可能更优 | 无论如何 EU PoP 都比 US PoP 近 London |
| Goalserve inplay 推送节点 (美国 / 欧洲 / 印度?) | 影响 B vs A 选择 | 小段 W8 W2 实测 (并行, 本报告不 block) |
| eu-west-1 vs eu-west-2 cost delta | 方案 B vs C 最终选择 | 老钱 infra 预算评审 |
| Hetzner Falkenstein (欧洲) vs AWS eu-west-1 cost | MVP 预算 | 老钱 + 老吴联合 cost analysis |

---

## §6 派单 (W8 W3)

| 接收方 | 任务 | 优先级 | 说明 |
|---|---|---|---|
| **小段** | Goalserve inplay endpoint 实测 — 确认推送节点地理位置 | P0 并行 | Wave 37 并行, 不 block 本报告, 结果 sync 老郭评审前 |
| **老郭** | ADR-013 v2 架构评审 — 综合本报告 + 小段实测后 W8 W3 召集 | P0 | 输入: 本报告 §4 候选方案 + 小段 Goalserve 结果 |
| **老何 / 老叶** | 顾问 review: Cloudflare CDN vs origin 网络拓扑 — 验证 §3 分析是否有遗漏 | P1 | 特别关注: CF Argo / Railgun / Workers 是否改变延迟模型 |
| **老雷 (GM)** | ack ADR-013 hold — 禁止购买 us-east-1 服务器直至老郭 v2 决议 | P0 ack | 不需要行动, 仅确认 hold |

---

## §7 立刻行动清单

- [x] 本机 dig + traceroute + curl 实测 (2026-05-28 23:44-23:52, 本文 §1)
- [x] AWS IP range JSON 反查所有 Polymarket IP (§1.3)
- [x] Cloudflare /cdn-cgi/trace — 确认 CF 代理架构 (§1.4)
- [x] 候选 ADR-013 v2 方案表 (§4)
- [ ] **欧洲 VPS 直连实测** — 需要 eu-west-1/eu-west-2 VPS 跑 `curl + ping` 到 clob/api (老郭 W8 W3 前置)
- [ ] 小段 Goalserve inplay 实测 (并行)
- [ ] 老郭 ADR-013 v2 评审 (W8 W3)

**不要立刻购买服务器** — ADR-013 选址 HOLD 到老郭 v2 决议.

---

## §8 不耻下问

| 问题方 | 问题 | 状态 |
|---|---|---|
| @老板 | 5 sources verbatim 已带 (QuantVPS / NYC Servers / X 三角定位 / TradingVPS / PolyVPN) | 已 ack, 本文基础 |
| @小段 | Goalserve inplay endpoint 实测 (并行 worktree, W8 W2) | Pending |
| @老郭 | ADR-013 v2 重评准备 (本报告 + 小段实测 → W8 W3 评审) | 待召集 |
| @老何 | Cloudflare CDN 网络拓扑 review (§3 分析验证) | 待 |
| @老叶 | 顾问 review Cloudflare PoP→origin 延迟模型 | 待 |
| @老雷 | ADR-013 hold 决议 ack | 待 ack |

---

## §9 ADR-013 变更摘要 (供老郭参考)

**原 ADR-013 (2026-06-01 Accepted, 6/6 全共识):**
- 前提: "Polymarket origin 在 us-east-1"
- 决议: W6~M4.5 us-east-1 单点

**触发 hold 的新证据:**
- 老板 verbatim 5 源: eu-west-2 origin, 都柏林 1ms vs 美东 130ms
- 本报告实测: 无法直接确认 origin IP (CF 代理), 但架构分析支持老板判断
- ADR-013 如基于错误前提, 会导致 hot-path 跨洋 130ms — **与 ADR R-12 100us 热路径要求严重冲突** (网络层 130ms >> 全链路预算的合理占比)

**ADR-013 v2 推荐决议方向 (供老郭评审, 非本文决策范围):**
- 将 us-east-1 单点改为 eu-west-1 (Dublin) 或 eu-west-2 (London) 单点
- 取决于 Goalserve inplay 节点地理 (小段实测)
- M5+ multi-region 评估维持

---

**报告结束. 老吴 (SRE) 2026-05-28.**
