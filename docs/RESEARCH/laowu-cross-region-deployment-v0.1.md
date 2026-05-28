# 跨洋部署方案 v0.1

- Owner: 老吴 (linux-sre-devops) + 老叶 (onchain-defi-advisor, 副笔)
- Last review: 2026-05-28
- 验收人: 老周 (system-architect)
- 关联 ticket: S1-010
- 关联交付物: 老叶 S1-009 (RPC), 老陈+老吴+小段 S1-021 (API 延迟实测), 小郑 S1-018 (监控), 老姜+小石 S1-011 (延迟预算)

---

## 0. TL;DR

- **主节点:** AWS `us-east-1` (N. Virginia) — Polymarket 主集群同区, Polygon edge 同区, Goalserve 美东出口同区
- **Standby:** Hetzner `Ashburn (ash)` 物理机 — 同城异厂, 成本 1/4 AWS, 容灾切换 RTO < 5min
- **国内开发出口:** Cloudflare WARP + Tailscale mesh, 经 HK 节点入 us-east-1, 日常 RTT ~180ms 可接受
- **不选香港/新加坡作主节点:** 距离 Polymarket / Polygon 验证人 / Goalserve 美东端点均 > 180ms, 信号链路被拉长 200ms+, 与老姜延迟预算 (S1-011) 不兼容
- **CI/CD:** GitHub Actions (build/test) + 自托管 runner on us-east-1 (deploy), 蓝绿
- **监控:** 小郑 Prometheus stack (S1-018) 主在 us-east-1, 备份打到 Hetzner ash; 告警出口走飞书 webhook + 老雷国内手机短信兜底

---

## 1. 业务约束

| 维度 | 数值 | 来源 |
|---|---|---|
| Polymarket WSS 推送延迟容忍 | < 100ms 内一手 | 老李 S1-002 |
| Goalserve inplay polling 间隔 | 1s | 小余/小段 S1-003 |
| 跨洋链路 (cn ↔ us-east) RTT | 150-220ms 公网, 90-150ms 优质线 | 本文 §3 |
| Polygon 出块间隔 | ~2s | 老叶 S1-009 |
| 总延迟预算 (event → order) | < 400ms p99 | 老姜 S1-011 |
| 团队主力地点 | 国内 (北上深) | 老雷 |
| 资金 settlement 地点 | 链上 (位置无关) | 老叶 S1-009 |
| 月度 infra 预算 | < 500 USD MVP | 老钱 S1-008 |

**核心矛盾:** 团队人在国内, 但所有外部依赖都在美东. **结论是机器跟数据走, 人通过低延迟链路远程**.

---

## 2. 外部依赖地理侦察

### 2.1 Polymarket

- gamma-api: `gamma-api.polymarket.com` → Cloudflare anycast, 美东命中率高
- clob: `clob.polymarket.com` → 同 Cloudflare, edge 在 us-east
- data-api: `data-api.polymarket.com` → 同上
- WSS: `ws-subscriptions-clob.polymarket.com` → Cloudflare WSS, origin 在 AWS us-east-1 (历史 dig + traceroute 推断)
- **结论:** 主节点放 us-east-1, edge 命中本区, origin 同区, RTT < 5ms

### 2.2 Goalserve

- `www.goalserve.com` → 单机房, IP 历史归属 Choopa/Vultr **us-east (NJ)**
- inplay endpoint 同上, 不在 CDN 后, IP 直连
- 已知约束: API key + IP whitelist, 需固定出口 (老陈代理已配, 但代理在国内绕一圈)
- **结论:** 主节点放 us-east-1, 直连 Goalserve, 走 IP whitelist, 退掉国内 proxy 这一跳 (省 150ms+)

### 2.3 Polygon RPC

- 见老叶 S1-009: Alchemy / QuickNode edge 均在 us-east-1/us-east-2
- 主节点在 us-east-1, 到 RPC edge RTT 5-15ms, 到 Polygon 验证人多数 < 50ms

### 2.4 时间同步

- AWS chrony pool 本区, 偏差 < 1ms
- Hetzner 自带 NTP, 偏差 < 5ms, 可接受

---

## 3. 链路延迟实测 (Sprint-1 内复测, 当前为公开 benchmark + 抽样)

> 老陈 + 老吴 + 小段 S1-021 会出完整报告, 本节为方案设计用的预估.

### 3.1 抽样测点 (老吴 笔记本 → 各候选区域, 国内电信出口)

| 目标 | RTT p50 (ms) | RTT p99 (ms) | 备注 |
|---|---|---|---|
| AWS us-east-1 公网 | 195 | 280 | 普通线路 |
| AWS us-east-1 经 HK 中转 (Cloudflare WARP) | 165 | 230 | WARP 优化路径 |
| AWS us-west-1 | 145 | 200 | 距 Polymarket origin 远 70ms |
| GCP us-east4 | 200 | 290 | |
| Hetzner Ashburn | 210 | 320 | 公网, 无 CN2 |
| Hetzner Helsinki (EU) | 230 | 340 | |
| AWS ap-east-1 (HK) | 50 | 90 | 距 Polymarket 远 180ms |
| AWS ap-southeast-1 (SG) | 70 | 130 | 距 Polymarket 远 230ms |

### 3.2 关键路径延迟拆解 (主节点选 us-east-1 时)

```
Goalserve push (us-east NJ)
    --[5-15ms]--> trade-engine (AWS us-east-1)
    --[本机内 < 1ms]--> signal --> order
    --[5-10ms]--> Polymarket CLOB (us-east origin)

Polygon settlement (异步):
    trade-engine --[5-15ms]--> Alchemy edge --[< 50ms]--> validator
```

总链路 p50 < 30ms, p99 < 100ms, 给老姜 (S1-011) 留出 300ms 计算预算.

### 3.3 国内团队远程到主节点

| 路径 | RTT | 工具 |
|---|---|---|
| 直连 us-east-1 SSH | 200ms | 可用但不爽 |
| Tailscale mesh via HK derp | 110ms | 推荐 |
| Cloudflare WARP+ → us-east | 165ms | 备 |

代码开发不在主节点, 部署经 CI/CD, 远程仅运维/排障使用, 200ms 可接受.

---

## 4. 主节点选址 (决策矩阵)

| 候选 | 延迟到 Polymarket | 延迟到 Goalserve | 延迟到 Polygon | 月费 (合规规格) | 国内运维体感 | 合规风险 |
|---|---|---|---|---|---|---|
| AWS us-east-1 | < 5ms | < 10ms | < 15ms | ~280 USD (c6i.large + EBS + 流量) | 中 (200ms) | 低 |
| GCP us-east4 | < 15ms | < 20ms | < 20ms | ~260 USD | 中 | 低 |
| Hetzner Ashburn | < 20ms | < 25ms | < 20ms | ~70 USD (AX52) | 中 (210ms) | 中 (无 SOC2) |
| AWS ap-east-1 (HK) | 180ms | 190ms | 200ms | ~310 USD | 高 (50ms) | 低 |
| 国内 + 代理 | 180ms+ | 180ms+ | 180ms+ | ~100 USD | 极高 | 高 (代理被审查) |

**决策: AWS us-east-1 作主, Hetzner Ashburn 作 standby.**

理由:
- 延迟全胜 (HK/SG 候选直接出局, 延迟预算装不下)
- AWS 有成熟 IAM / VPC / Secrets Manager 与老孙 KMS 选型一致 (S1-005)
- Hetzner 同城异厂, 单价 1/4, 容灾够用且不锁死单一云

---

## 5. 主备方案

### 5.1 拓扑

```
                  +---------- DNS / Route53 ----------+
                  |   trade.internal (failover policy)|
                  +-----+-------------------------+---+
                        |                         |
            primary (active)              standby (warm)
            AWS us-east-1                 Hetzner Ashburn
            c6i.large + 200GB gp3         AX52 NVMe 1TB
            10.20.0.0/16 VPC              Wireguard mesh
              |  |                          |
              |  +-- trade-engine (only here writes)
              |  +-- rpc-router (老叶 S1-009)
              |  +-- Prometheus primary (小郑 S1-018)
              |
              +--- async replicate (Postgres logical, S3 snapshots) --->
                                                              standby
```

### 5.2 active/standby 模式 (不做 active/active)

- 写交易只有 primary 出 (避免双发同 nonce)
- standby 持续接收 DB 复制 + git artifact 镜像, 但 trade-engine 进程不启动
- failover 触发条件:
  - primary instance health check 失败 3 min
  - primary 链路到 Polymarket loss > 30%
  - 人工 (老吴 / 老雷 一键)
- failover 步骤 (RTO < 5min):
  1. Route53 切 CNAME
  2. standby 启 trade-engine (systemd target)
  3. 老孙 KMS 解锁 standby 签名 sidecar (二次确认)
  4. 老叶 RPC 区域 pin 检查 (Alchemy edge 还是 us-east, 不动)
  5. metrics 切到 standby prometheus

### 5.3 数据资产分级

| 资产 | 主存 | 备份 | RPO |
|---|---|---|---|
| 代码 | GitHub | GitHub mirror | 0 |
| 凭证 (.env) | AWS Secrets Manager | Hetzner 本地 age 加密 | 1h |
| Postgres (orders/positions) | primary RDS | 逻辑复制到 standby | 30s |
| Prom 指标历史 | primary EBS | S3 daily snapshot | 24h |
| 私钥 | 老孙 HSM (S1-005) | 老孙定 | 老孙定 |

---

## 6. CI/CD 部署链路

### 6.1 流水线

```
dev push --> GitHub
    --> Actions: build (ubuntu-latest, C++ toolchain) + unit test
    --> Actions: artifact (binary + checksum) 上传 S3 us-east-1
    --> 自托管 runner on us-east-1: pull artifact
    --> 蓝绿: 起 v_new, smoke test, 切流量, 停 v_old (5min 窗口)
    --> Hetzner standby: rsync artifact, 不启动
```

### 6.2 工具栈

- **Build:** GitHub Actions (公网 runner, 算力充足, 不消耗主节点)
- **Deploy:** 自托管 runner on us-east-1, 避免 actions 公网 runner 跨洋拉镜像
- **配置管理:** Ansible (老吴 单人 maintainable, 不上 K8s — 老钱 MVP scope 拒绝)
- **二进制分发:** S3 + checksum (sha256), 老沈 S1-016 review 签名链
- **回滚:** 保留前 3 版本 binary, `make rollback` 一键切回

### 6.3 分支模型

- `main`: production, 受保护, 走 PR + review
- `develop`: integration, 自动部署到 staging (Hetzner ash 上的二进制, 不连真钱)
- feature branch: 不部署
- staging 与 prod 配置隔离 (.env.staging vs .env.prod, 凭证不同)

---

## 7. 监控告警基础设施

### 7.1 分层

| 层 | 工具 | Owner |
|---|---|---|
| 指标采集 | Prometheus (小郑 S1-018) | 小郑 |
| 日志 | journald → Loki (us-east-1) | 老吴 |
| 链路探测 | blackbox_exporter (Polymarket/Goalserve/RPC 三方 HTTP+TCP) | 老吴 |
| 应用指标 | trade-engine /metrics (老叶 gas + RPC 指标 已定义) | 各模块 |
| 告警 | Alertmanager → 飞书 webhook + 短信 sidecar | 老吴 + 老雷 |
| Dashboard | Grafana (小郑) | 小郑 |

### 7.2 关键告警 (P0)

| 告警 | 触发 | 通知 | 升级 |
|---|---|---|---|
| primary instance down | 1min | 老吴 | 5min → 老雷 |
| Polymarket WSS 断线 > 30s | 立即 | 老吴 + 老李 | 3min → 老雷 |
| Goalserve API 401/403 | 任意 | 小段 | 5min → 老雷 |
| RPC primary down (老叶 S1-009 已定) | 立即 | 老叶 | 5min |
| settlement tx pending > 5min | 立即 | 老孙 + 老叶 | 即升级 老雷 |
| 跨洋链路 loss > 10% 持续 1min | 立即 | 老吴 | 5min |
| gas crit (老叶 阈值) | 立即 | 老叶 | — |

### 7.3 排障入口

- Tailscale 进 primary, SSH via tag `team-sre`
- Grafana 公网 (Cloudflare Access + Google OAuth, 老沈 S1-016 把关)
- 应急: 老雷 手机有 1Password 备 root 凭证 (老沈 单独 review)

---

## 8. 跨洋开发体感优化

- Tailscale mesh, 国内 → HK derp → us-east-1, RTT ~110ms
- 主节点不开发, 只跑 prod binary; dev 在本地 macOS / 国内 Linux 容器
- VSCode Remote-SSH 不推荐 (200ms 体感差), 用 git workflow + CI 跑测试
- 大文件传输走 S3 presigned URL, 不走 SSH

---

## 9. 与其他人接力

### 9.1 → 老叶 (S1-009)

- 主节点定 us-east-1 后, 老叶请 Alchemy/QuickNode 同区 API key
- 老吴提供 us-east-1 NAT 出口 IP 段 给老叶, 老叶申请 RPC IP allowlist

### 9.2 → 老孙 (S1-005)

- AWS KMS / Secrets Manager 主, Hetzner 本地 age 加密备
- 私钥 boot-up unseal 流程 老孙 主导, 老吴 提供 systemd unit + 启动 hook

### 9.3 → 小郑 (S1-018)

- 老吴 给小郑 Prom 跑哪台机器, 哪些 endpoint scrape, blackbox 配置目标清单
- 小郑 出 Grafana dashboard, 老吴 配 Alertmanager 路由

### 9.4 → 老姜 (S1-011)

- 老吴 给老姜 本网络拓扑下的延迟实测数据, 老姜 计算 lock-free 阈值
- TCP_NODELAY / SO_REUSEPORT 等内核参数由老吴 在 systemd unit 里配, 老姜 review

### 9.5 → 老陈 (network)

- Goalserve IP whitelist 老陈 出列表, 老吴 在 AWS NAT GW 出口 EIP 申报
- 国内代理 (.env GOALSERVE_PROXY) 在 us-east 主节点 部署后可以下线, 等老陈 confirm

### 9.6 → 老沈 (S1-016 安全)

- SG / IAM / Secrets / KMS 策略给老沈 review
- Cloudflare Access OAuth 策略给老沈

### 9.7 → 老胡 (S1-014 甘特)

- AWS 账号开通 → 老吴 owner, lead-time 2 天 (合规填表)
- Hetzner 下单 → 1 天到货 + 0.5 天 install
- 总 ready 时间预估 4 工作日

---

## 10. 成本估算

| 项 | 月费 USD |
|---|---|
| AWS us-east-1 c6i.large + 200GB gp3 + 1TB egress | 180 |
| AWS RDS Postgres db.t4g.small + 50GB | 45 |
| AWS Secrets Manager + KMS | 5 |
| Route53 + CloudWatch (基础) | 10 |
| S3 (artifact + snapshot) | 5 |
| Hetzner AX52 (standby) | 75 |
| Tailscale team | 0 (free tier) |
| Cloudflare Access | 0 (free tier ≤ 50 用户) |
| **合计** | **~320 USD/月** |

加老叶 RPC ~100 USD/月, 总 infra ~420 USD/月, 在老钱 MVP 预算内.

---

## 11. 风险登记

| Risk | 概率 | 影响 | 缓解 |
|---|---|---|---|
| 跨洋链路抖动影响远程运维 | 中 | 中 | Tailscale + 多线路; 排障脚本本地化 |
| AWS 账号被风控 (新账号 + 加密货币关键词) | 低 | 高 | 老雷 主体, 小白 合规话术; Hetzner 即时切 |
| Hetzner 物理机故障 | 低 | 中 | standby 模式, 损失可接受; 老吴 30min 内 reinstall |
| Goalserve IP whitelist 变更慢 | 中 | 中 | 申请固定 EIP + 提前提单 |
| Polymarket origin 区域迁移 | 低 | 高 | 季度复核 + dig 监控 |
| CI/CD runner token 泄露 | 低 | 高 | 老沈 review + 短期 token + IP 限定 |
| Tailscale derp HK 政策风险 | 低 | 中 | 备 Cloudflare WARP 通道 |

---

## 12. 验收清单 (老周 review)

- [ ] AWS us-east-1 主节点 ready, Tailscale 接入
- [ ] Hetzner ash standby ready, wireguard 通 primary
- [ ] 三方依赖 RTT 实测报告 (与 S1-021 合并)
- [ ] CI/CD demo: PR merge → 自动部署到 staging
- [ ] failover 演练: primary stop, 5min 内 standby 接管
- [ ] 监控告警通飞书 + 短信
- [ ] 凭证体系与老孙 S1-005 对齐
- [ ] 月度成本核对 (老钱)

---

## 附录 A: 为何不上 K8s

- 一个 trade-engine + 一个 rpc-router + 一个 prom, 总 3-4 个长进程
- K8s 引入 control plane 复杂度 + 调度抖动 (k8s pod 调度 100ms+ 不能容忍)
- 老吴 单人维护, Ansible + systemd 已足够
- 重审窗口: Sprint-6 业务复杂度上升后

## 附录 B: 为何不选 GCP/Azure

- GCP us-east4 延迟与 AWS us-east-1 接近, 但 Polymarket / Goalserve origin 都在 AWS, 跨云一跳 5-10ms 不必要
- Azure 在 us-east 节点配置选择窄, 价格无优势
- 团队 AWS 熟练度 > GCP > Azure (老吴 自评)
