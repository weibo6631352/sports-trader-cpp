# 安全威胁模型 v2

- Owner: 老沈 (security-engineer)
- Date: 2026-05-28
- Status: Active (v1 Deprecated jurisdictional 部分)
- Superseded reason: GM 2026-05-28 撤地域合规纠缠, 威胁模型只保留技术层
- 关联: `docs/ADR/2026-05-28-gm-policy-jurisdictional-deferral.md`
- 验收人: 老雷 (GM) + 老孙 (crypto-signing-expert)
- 方法论: STRIDE + DREAD (1-5 分)
- 范围: sports-trader-cpp 源码到生产部署全链路 (跨洋链路 + 链上交互 + 第三方 API + 班底操作面)

> v2 撤销内容: 跨境数据出境威胁, 监管/法院密令威胁, 多国法律管辖耦合, 与 R4 / R5 / 合规联签条款 → 由 GM 转 future 立项处理. 本文只保留**技术安全**段.

---

## 1. 资产清单

| ID | 资产 | 等级 | CIA | 存放 | Owner |
|---|---|---|---|---|---|
| AS-01 | WALLET_PRIVATE_KEY 主交易钱包私钥 | **S** | C/I | KMS unwrap + signer mlock 内存 | 老孙 |
| AS-02 | POLYMARKET_API_KEY/SECRET/PASSPHRASE | **S** | C | .env + secret store | 老沈 + 老吴 |
| AS-03 | GOALSERVE_API_KEY | A | C/A | .env + secret store | 老沈 + 老吴 |
| AS-04 | Polygon 链上资金 (USDC) | **S** | C/I | 链上 EOA | 老孙 + 老叶 |
| AS-05 | DATABASE_URL (含 DB password) | A | C/I | .env + secret store | 老吴 + 老王 |
| AS-06 | 历史成交 + 持仓 + PnL | A | I/A | PostgreSQL + WAL | 老王 |
| AS-07 | 策略代码 + 信号 IP | A | C/I | git repo + 构建产物 | 老周 + 小梁 |
| AS-08 | RiskManager 红线配置 | **S** | I | 代码 + ops config | 老韩 |
| AS-09 | 实时盘口 + WSS 订阅状态 | B | I/A | 内存 cache | 老李 |
| AS-10 | 审计日志 + incident 记录 | A | I | append-only log | 老唐 |
| AS-11 | Prometheus / Grafana metrics | B | A | observability stack | 小郑 |
| AS-12 | 构建工具链 (vcpkg / CMake / 编译器) | A | I | 构建主机 | 老吴 |
| AS-13 | git 远端 + CI/CD 凭证 | A | C/I | GitHub + Actions secret | 老吴 |
| AS-14 | TLS CA 信任锚 + cert pin 配置 | A | I | 二进制内嵌 + ops config | 老沈 |
| AS-15 | 跨洋代理 / VPN 凭证 | A | C | secret store | 老吴 |

---

## 2. 信任边界 (5 条)

- TB-A 互联网 ⇄ 主机: 所有 egress 走 TLS + cert pinning; ingress 默认 deny
- TB-B trader ⇄ signer: 进程隔离 + Unix domain socket, signer 不出网, 签名请求带 RiskManager checksum
- TB-C signer ⇄ 密钥存储: 密钥仅 signer 内存 (mlock + MADV_DONTDUMP), 启动期 KMS unwrap, 退出清零
- TB-D 运维域 ⇄ 生产主机: bastion + MFA + 录屏, 禁直连
- TB-E 链下 ⇄ 链上: signer + RiskManager 二次校验 (金额 / receiver / 频率), 白名单

---

## 3. STRIDE 威胁清单

### S — Spoofing
| ID | 威胁 | 资产 | 边界 | DREAD |
|---|---|---|---|---|
| S-01 | 伪造 Polymarket WSS server (DNS 劫持 / 证书伪造) 灌假盘口 | AS-09, AS-04 | TB-A | **17** |
| S-02 | 伪造 Polygon RPC 节点返回假 nonce / 假状态 | AS-04 | TB-A/E | **16** |
| S-03 | 班底 agent 冒用他人身份提交 PR / 触发部署 | AS-07, AS-13 | TB-D | **16** |
| S-04 | 伪造 Goalserve 数据源喂错赛果 | AS-09 | TB-A | **15** |
| S-05 | 仿冒 internal IPC 客户端骗 signer 签名 | AS-01, AS-04 | TB-B | **18** |

### T — Tampering
| ID | 威胁 | 资产 | 边界 | DREAD |
|---|---|---|---|---|
| T-01 | 供应链投毒 (vcpkg / 上游包恶意代码) | AS-07, AS-12 | TB-A | **19** |
| T-02 | 构建产物在 CI/CD 被植入后门 | AS-07, AS-13 | TB-D | **18** |
| T-03 | 篡改 RiskManager 红线配置绕过下单 | AS-08 | TB-D | **18** |
| T-04 | 篡改链上交易 calldata (签名前注入) | AS-04 | TB-B | **19** |
| T-05 | 数据库历史成交记录被改 (掩盖损失) | AS-06, AS-10 | TB-D | **15** |
| T-06 | TLS cert pin 配置被偷换 | AS-14 | TB-D | **16** |

### R — Repudiation
| ID | 威胁 | 资产 | 边界 | DREAD |
|---|---|---|---|---|
| R-01 | 内部成员事后否认下单 / 配置变更 | AS-08, AS-10 | TB-D | **14** |
| R-02 | 签名记录缺失, 无法对账链上交易归属 | AS-04, AS-10 | TB-E | **14** |
| R-03 | agent 误操作后日志被自动 rotate 覆盖 | AS-10 | TB-D | **13** |

### I — Information Disclosure
| ID | 威胁 | 资产 | 边界 | DREAD |
|---|---|---|---|---|
| I-01 | 私钥落日志 / core dump / 异常堆栈 | AS-01 | TB-C | **20** |
| I-02 | API key 写进 git commit / 截图泄露 | AS-02, AS-03 | TB-D | **19** |
| I-03 | .env 误打包进容器镜像 / 二进制 | AS-02, AS-05 | TB-D | **17** |
| I-04 | metrics 标签泄露策略参数 / 持仓 | AS-07, AS-06 | TB-A | **15** |
| I-05 | 跨洋链路被嗅探 (BGP 劫持 / ISP 中间盒) | AS-02, AS-09 | TB-A | **16** |
| I-06 | dev 笔记本被入侵泄露 git/凭证 | AS-02, AS-07 | TB-D | **16** |
| I-07 | LLM agent prompt 注入泄露上下文凭证 | AS-02 | TB-D | **17** |

### D — Denial of Service
| ID | 威胁 | 资产 | 边界 | DREAD |
|---|---|---|---|---|
| D-01 | Goalserve 限流 / 封号 | AS-09 | TB-A | **16** |
| D-02 | Polymarket WSS 主动断连 / 反爬 | AS-09 | TB-A | **18** |
| D-03 | Polygon RPC 单点拥堵导致下单超时 | AS-04 | TB-A | **17** |
| D-04 | 跨洋链路抖动 / 丢包错过出场 | AS-04 | TB-A | **18** |
| D-05 | 主机资源耗尽 (内存泄漏 / fd 泄漏) | 整机 | TB-B | **15** |
| D-06 | 恶意盘口数据触发解析路径死循环 | AS-09 | TB-A | **15** |

### E — Elevation of Privilege
| ID | 威胁 | 资产 | 边界 | DREAD |
|---|---|---|---|---|
| E-01 | RCE: 第三方依赖 CVE 拿主机 shell | AS-01, AS-04 | TB-A | **20** |
| E-02 | trader 进程被入侵后横向到 signer | AS-01 | TB-B | **18** |
| E-03 | sudo / root 滥用 / SSH key 泄露 | 整机 | TB-D | **18** |
| E-04 | container escape | 整机 | TB-B | **18** |
| E-05 | 班底 agent 越权 | AS-07, AS-08 | TB-D | **16** |

---

## 4. P0 高危场景 (DREAD ≥ 18, 共 11 个)

Sprint-1 末必须出可验证缓解 spec.

| Rank | ID | 描述 | DREAD | Owner |
|---|---|---|---|---|
| 1 | **I-01** | 私钥写进日志 / core dump / 异常堆栈 | 20 | 老沈 + 老孙 |
| 2 | **E-01** | 依赖 RCE → 攻击者直动用私钥转账 | 20 | 老沈 + 老吴 |
| 3 | **T-01** | 供应链投毒 (vcpkg / 上游包) | 19 | 老沈 + 老何 + 老张 |
| 4 | **T-04** | 链上交易 calldata 签名前篡改 (改 receiver / amount) | 19 | 老孙 + 老叶 |
| 5 | **I-02** | API key 进 git / 截图 | 19 | 老沈 + 全员 |
| 6 | **S-05** | 仿冒 IPC 客户端骗 signer 签名 | 18 | 老孙 + 老沈 |
| 7 | **D-02** | Polymarket WSS 断连错过出场 | 18 | 老李 + 老陈 |
| 8 | **D-04** | 跨洋链路抖动错过止损 | 18 | 老吴 + 老陈 |
| 9 | **T-02** | CI/CD 构建产物被植入后门 | 18 | 老吴 + 老沈 |
| 10 | **T-03** | RiskManager 红线配置被改 | 18 | 老韩 + 老沈 |
| 11 | **E-02** | trader 入侵后横向到 signer | 18 | 老沈 + 老吴 |

> 11 个 P0 高危场景全部保留 (与 v1 一致, 均为技术层威胁, 不涉地域合规).

---

## 5. 缓解措施

### 5.1 私钥 (映射 I-01 / E-02 / S-05 / T-04 / R-02)
- **M-K1 独立 signer 进程**: 与 trader 分进程, signer 不开 egress (iptables OUTPUT drop), Unix socket 收签名请求
- **M-K2 内存保护**: `mlock` + `madvise(MADV_DONTDUMP)` + `prctl(PR_SET_DUMPABLE, 0)`; 退出前 `explicit_bzero`
- **M-K3 IPC 鉴权**: 校验对端 PID + UID + cgroup, 签名请求带 HMAC + nonce + RiskManager checksum, 防 replay
- **M-K4 链上白名单**: signer 内置 receiver 白名单, 非白名单拒签
- **M-K5 签名速率限制**: per-minute / per-hour 上限, 触顶报警 + 暂停
- **M-K6 启动期 KMS unwrap**: 私钥不落盘, 启动时从 KMS 解封 (见 vendor 选型 v2)
- **M-K7 audit log**: 每次签名 append-only (timestamp + tx hash + calldata hash + caller checksum)
- **M-K8 core dump 全禁**: `ulimit -c 0` + systemd `LimitCORE=0`

### 5.2 API key (映射 I-02 / I-03 / I-07)
- **M-A1 secret scanner**: gitleaks / trufflehog pre-commit + CI 再扫
- **M-A2 .env 不入镜像**: 运行时通过 KMS / instance metadata 注入
- **M-A3 LLM agent 隔离**: agent 不直接读 .env; prompt 含凭证关键词时拒绝
- **M-A4 凭证轮换**: Polymarket / Goalserve key 90 天, DB password 180 天
- **M-A5 最小作用域**: 一份 key 一个用途
- **M-A6 SBOM + 镜像扫描**: trivy / grype

### 5.3 跨洋 MITM (映射 S-01 / S-04 / I-05 / T-06)
- **M-N1 TLS cert pinning**: leaf cert / SPKI hash 内嵌二进制
- **M-N2 多 RPC 多源**: Polygon RPC ≥ 3 家, nonce / chainTip 比对
- **M-N3 WSS 双向校验**: 心跳 / sequence number 监控
- **M-N4 DNSSEC + DoH**: 防本地 DNS 投毒
- **M-N5 链路加密**: 跨洋除 TLS 外加 wireguard
- **M-N6 BGP 监控**: routeviews / RIPE atlas

### 5.4 供应链 (映射 T-01 / T-02 / T-06)
- **M-S1 vcpkg pin**: 所有依赖 pin commit hash, 升级走 PR + 老沈 review
- **M-S2 SBOM**: 每次构建 CycloneDX / SPDX
- **M-S3 CVE watch**: OSV / GHSA 订阅, CVSS ≥ 7 的 48h 内出修复
- **M-S4 reproducible build**: 关键模块双机交叉验证 hash
- **M-S5 CI 隔离**: Actions runner 不持生产凭证
- **M-S6 二进制签名**: 生产只接受签名过的二进制
- **M-S7 上游 untrusted**: 解析层严格 schema + range + outlier 校验

### 5.5 RiskManager 配置 (映射 T-03 / R-01)
- **M-R1 配置签名**: 红线配置 GPG 签名, 启动期验签
- **M-R2 double review**: 红线变更老韩 + 老郭 双签
- **M-R3 运行时 immutable**: 加载后 read-only, SIGHUP + 验签
- **M-R4 audit chain**: 变更记录 + 操作者 + diff append-only

### 5.6 拒服 / 灾难恢复 (映射 D-01 ~ D-06)
- **M-D1 多源 + fallback**: 数据源 / RPC ≥ 双源
- **M-D2 熔断器**: 数据延迟 / RPC 失败率超阈值 → 只平不开
- **M-D3 资源限制**: cgroup memory / cpu / fd 限制
- **M-D4 fuzz**: JSON / WSS libfuzzer, CI 每次 30min
- **M-D5 失联 SOP**: 数据源失联 > N 秒 → 撤挂单 → 暂停下单

### 5.7 权限 (映射 E-03 / E-05 / S-03)
- **M-P1 bastion + MFA**: 生产不开公网 SSH
- **M-P2 最小权限**: trader / signer 分账户, signer nologin, 不能 sudo
- **M-P3 seccomp + AppArmor**: signer 白名单系统调用, 拒 execve / ptrace
- **M-P4 agent 工具白名单**: 每个 sub-agent persona 限定 tools
- **M-P5 操作签名**: 部署 / 配置变更 / 凭证轮换走 sigstore keyless + 审计

---

## 6. 应急响应 SOP (技术事故)

### 6.1 角色 (IC)
| 角色 | 默认人 | 备份 | 职责 |
|---|---|---|---|
| IC | 老雷 | 老郭 | 拍板 / 升级 |
| Security Lead | 老沈 | 老孙 | 取证 / 修复 / CVE 响应 |
| Risk Lead | 老韩 | 老梁 | 资金止血 / 仓位评估 |
| Ops Lead | 老吴 | 老陈 | 隔离 / 重建 / 凭证轮换 |
| On-chain Lead | 老叶 | 老孙 | 链上追踪 / 资金转移 |

> 涉私钥 / 资金事件, **任何人** 可直接 page 老雷 + 老沈 + 老韩.

### 6.2 P0 SOP

**场景 A 私钥泄露**: T+0 page + `kill-switch.sh` 撤挂单 + signer SIGTERM; T+5min 老叶 sweep USDC 冷备; T+15min 老韩评估敞口; T+30min 老沈取证 (内存 dump + 日志 + 抓包异地, **不重启**); T+2h timeline; T+24h post-mortem; T+48h 新私钥 + 全链路凭证轮换.

**场景 B 主机失陷 (RCE / SSH)**: T+0 page; T+2min 老吴 bastion 切断 egress **不关机**; T+5min 视私钥位置 sweep 资金; T+15min 干净主机重建, 老沈做镜像; T+1h 定位入侵路径; T+24h 全栈凭证轮换.

**场景 C 资金被盗 (链上 tx)**: T+0 监控检测非白名单 receiver outgoing tx 自动 page; T+1min 老叶尝试链上拦截 (高 gas 抢 nonce); T+10min 全量 sweep 剩余到冷备; T+30min 老雷决定是否通报 Polymarket; T+24h 链上追踪 (etherscan / Chainalysis).

**场景 D 供应链投毒 / CVE**: T+0 老沈 OSV watch 触发, SBOM 比对评估影响; T+1h 锁版本 + CI 阻断升级; T+4h 评估生产是否已构建受影响版本 (是则按场景 B); T+24h 修复版本上线.

### 6.3 演练
- Q3 2026 私钥泄露 tabletop (老沈)
- Q4 2026 主机失陷红蓝对抗 (老吴 + 老沈)
- MVP 上线前 kill-switch 实战 (生产, dummy 资金)

---

## 7. 内部审计 + audit log 完整性

| 频率 | 内容 | Owner |
|---|---|---|
| 每周一 | CVE 扫描 (vcpkg deps + base image) | 老沈 |
| 每周五 | secret scan (git history + image + log) | 老沈 + 小郑 |
| 每月 | 依赖 SBOM diff + 升级 review | 老沈 + 老何 |
| 每月 | audit log 完整性校验 (BLAKE3 hash chain) | 老唐 + 老沈 |
| 每季度 | 凭证轮换 (API key / DB password) | 老吴 + 老沈 |
| 每季度 | 内部红队 pen test | 老沈 |
| 每季度 | 应急响应演练 | 老沈 + 老雷 |
| 持续 | SIEM 告警 (异常签名 / RPC / SSH) | 小郑 + 老沈 |

**audit log 完整性硬要求**:
- append-only + BLAKE3 hash chain (老唐 schema), 每条链接前一条 hash
- 异地备份 (与生产隔离) ≥ 180 天
- 月度校验 hash chain 连续性, 断链立即 page 老沈 + 老唐
- log rotation 保留 hash continuity, 禁 rotate 丢链

---

## 8. 开放问题

| # | 问题 | 阻塞方 | 期望解决 |
|---|---|---|---|
| O-01 | KMS 选型最终落地 (见 vendor 选型 v2) | 老孙 + 老沈 | Sprint-1 末 |
| O-02 | 冷备私钥多签 threshold (2/3 vs 3/5) | 老孙 + 老雷 | Sprint-2 |
| O-03 | Polygon RPC 私有节点是否自建 | 老叶 + 老吴 | Sprint-2 |
| O-04 | 是否引入 TEE (SGX / Nitro Enclave) 跑 signer | 老沈 + 老孙 | Sprint-3 |
| O-05 | 跨洋 wireguard 是否够 / 上专线 | 老吴 | Sprint-2 |
| O-06 | LLM agent 安全沙箱 | 老沈 + 小白 | Sprint-2 |
| O-07 | RiskManager 配置签名密钥存放 | 老韩 + 老沈 | Sprint-2 |
| O-08 | kill-switch 是否硬件按钮 | 老吴 + 老沈 | Sprint-3 |
| O-09 | EIP-712 typed-data schema 校验 (signer 内完整 vs hash) | 老孙 | Sprint-1 末 |
| O-10 | 凭证轮换 zero-downtime (Polymarket key 重叠期) | 老沈 + 老李 | Sprint-2 |
| O-11 | 班底 agent 在 commit / 部署链路的身份认证 | 老沈 + 老徐 + 老吴 | Sprint-2 |

---

## 9. v1 → v2 撤销条数

撤销条款 (源 v1, 由 GM 2026-05-28 决议触发):
1. v1 §1 提到的"跨境数据出境"风险评估 → 撤
2. v1 §3 R-类涉跨境取证 / 跨国传票 / 数据主权讨论 → 撤
3. v1 §6.2 场景 C "T+24h 法务跟进 (老黄, 跨部门移交)" 段 → 撤
4. v1 §6.2 场景 C "T+30min 老雷决定是否对外监管通报" → 改为仅通报 Polymarket
5. v1 §7 "每年第三方安全审计 (HSM/KMS + 智能合约) 由老雷 (采购)" 跨境部分 → 简化
6. v1 §8 O-08 "审计日志异地备份目的地 (合规角度, 老黄)" → 改为纯技术异地, 不涉合规地点
7. v1 与 R4 / R5 / 老黄合规联签的所有条款 → 撤
8. v1 与"非美总部 KMS 硬约束"的间接耦合 → 撤

**保留**: 11 个 P0 高危场景 (全部技术层) 一个不撤; STRIDE 全表 (撤跨境段后) 保留; 缓解措施 5.1 ~ 5.7 全保留; 应急 SOP 场景 A/B/C/D 保留 (D 简化掉跨国法务环节); 内部审计 + audit log hash chain 强化.

---

*v2 提交: 2026-05-28*
*Supersedes v1 jurisdictional 段; STRIDE 技术段保留*
*下次 review: Sprint-1 末 (与 KMS 选型 v2 联动)*
