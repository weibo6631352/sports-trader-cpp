# 安全威胁模型 v1

- Owner: 老沈 (security-engineer)
- Co-review: 老孙 (crypto-signing-expert)
- Last review: 2026-05-28
- 验收人: 老韩 (risk-engineer) + 老雷 (GM)
- 关联 ticket: S1-016 (本 ticket) / S1-005 (老孙私钥方案) / S1-009 (老叶 Polygon RPC) / S1-010 (老吴跨洋部署) / S1-006 (老黄合规)
- 方法论: STRIDE 分类 + DREAD 评分 (1-5 分, 总分 5-25)
- 范围: sports-trader-cpp 从源码到生产部署的全链路, 含跨洋链路 + 链上交互 + 第三方 API + 班底 agent 操作面

---

## 1. 资产清单

资产按 **机密性 / 完整性 / 可用性 (CIA)** 三维分级, S 级 = 失守即破产, A 级 = 失守即重大损失, B 级 = 失守需 24h 内响应.

| 资产 ID | 资产名 | 类型 | 等级 | CIA 重点 | 存放位置 | Owner |
|---|---|---|---|---|---|---|
| AS-01 | WALLET_PRIVATE_KEY (主交易钱包私钥) | 密钥 | **S** | C/I | TBD (S1-005 输出) | 老孙 |
| AS-02 | POLYMARKET_API_KEY/SECRET/PASSPHRASE | API 凭证 | **S** | C | .env + secret store | 老沈 + 老吴 |
| AS-03 | GOALSERVE_API_KEY | API 凭证 | A | C/A | .env + secret store | 老沈 + 老吴 |
| AS-04 | Polygon 链上资金 (USDC) | 资金 | **S** | C/I | 链上 EOA / proxy | 老孙 + 老叶 |
| AS-05 | DATABASE_URL (含 DB password) | 凭证 | A | C/I | .env + secret store | 老吴 + 老王 |
| AS-06 | 历史成交 + 持仓 + PnL 数据 | 业务数据 | A | I/A | PostgreSQL + WAL | 老王 |
| AS-07 | 策略代码 + 信号 IP | 代码/算法 | A | C/I | git repo + 构建产物 | 老周 + 小梁 |
| AS-08 | RiskManager 红线配置 | 配置 | **S** | I | 代码 + ops config | 老韩 |
| AS-09 | 实时盘口 + WSS 订阅状态 | 业务数据 | B | I/A | 内存 + Redis-like cache | 老李 |
| AS-10 | 审计日志 + incident 记录 | 取证数据 | A | I | append-only log | 老唐 |
| AS-11 | Prometheus / Grafana metrics | 监控数据 | B | A | observability stack | 小郑 |
| AS-12 | 构建工具链 (vcpkg / CMake / 编译器) | 工具链 | A | I | 构建主机 | 老吴 |
| AS-13 | git 远端仓库 + CI/CD 凭证 | 元数据 | A | C/I | GitHub + Actions secret | 老吴 |
| AS-14 | TLS CA 信任锚 + cert pin 配置 | 信任配置 | A | I | 二进制内嵌 + ops config | 老沈 |
| AS-15 | 跨洋代理 / VPN 凭证 (GOALSERVE_PROXY 等) | 网络凭证 | A | C | secret store | 老吴 |

---

## 2. 信任边界

```
                  +-------------------------------------------------+
                  |                  互联网 (untrusted)              |
                  |  Polymarket CLOB / Gamma / Data REST + WSS      |
                  |  Polygon RPC nodes (多家)                       |
                  |  Goalserve REST + 代理 IP 白名单                 |
                  |  GitHub / vcpkg registry / 包源                  |
                  +------------------+------------------------------+
                                     |  TLS (TB-A 跨洋边界)
                                     v
+----------------------------------------------------------------+
|  生产主机 (就近数据中心, semi-trusted, 班底 + SRE 可达)         |
|                                                                |
|  +----------------------------+   +-------------------------+  |
|  |  trader 主进程 (C++)        |   |  signer 子进程 (隔离)    |  |
|  |  - WSS / REST 客户端        |<->|  - 持有私钥 (mlock)     |  |
|  |  - RiskManager              |IPC|  - 仅签名, 不出网         |  |
|  |  - 订单状态机                |   |  - 内存清零              |  |
|  +----------------------------+   +-------------------------+  |
|                ^                              ^                |
|                |                              |                |
|  TB-B 进程边界 |                              | TB-C 密钥边界  |
|                |                              |                |
|  +-----------------------------+   +-------------------------+ |
|  | 监控/日志/审计              |   |  secret store / KMS     | |
|  | Prometheus / WAL / audit    |   |  (S1-005 选型)          | |
|  +-----------------------------+   +-------------------------+ |
+--------------------+-------------------------------------------+
                     | TB-D 运维边界
                     v
+----------------------------------------------------------------+
|  班底 + 运维域 (老吴 / on-call / agent 操作)                    |
|  - SSH bastion / MFA                                          |
|  - CI/CD runner                                               |
|  - dev 笔记本                                                  |
+----------------------------------------------------------------+
```

**5 条信任边界**:

- **TB-A (互联网 ⇄ 主机)**: 所有 egress 走 TLS + cert pinning; ingress 默认 deny, 仅放 SSH bastion + Prom scrape.
- **TB-B (trader 主进程 ⇄ signer 子进程)**: 进程隔离 + Unix domain socket, signer 不出网, 签名请求需带 RiskManager checksum.
- **TB-C (signer ⇄ 密钥存储)**: 密钥仅在 signer 内存 (mlock + MADV_DONTDUMP), 启动期从 KMS/HSM 解封, 进程退出立即清零.
- **TB-D (运维域 ⇄ 生产主机)**: bastion + MFA + 审计录屏, 不允许直连生产主机.
- **TB-E (链下 ⇄ 链上)**: 所有上链交易经 signer 签名 + RiskManager 二次校验 (金额 / 接收地址 / 频率), 链上地址白名单.

---

## 3. STRIDE 威胁清单

### S — Spoofing (身份伪造)

| ID | 威胁 | 资产 | 边界 | DREAD | 备注 |
|---|---|---|---|---|---|
| S-01 | 伪造 Polymarket WSS server (DNS 劫持 / 证书伪造) 灌假盘口 | AS-09, AS-04 | TB-A | 4+4+3+3+3=**17** | 跨洋路径长, 中间设备多 |
| S-02 | 伪造 Polygon RPC 节点返回假 nonce / 假状态 | AS-04 | TB-A/E | 4+4+3+2+3=**16** | 单 RPC 单点信任风险 |
| S-03 | 班底 agent 冒用他人身份提交 PR / 触发部署 | AS-07, AS-13 | TB-D | 3+4+3+2+4=**16** | 协同规范 S1-019 配套 |
| S-04 | 伪造 Goalserve 数据源 (DNS / 代理劫持) 喂错赛果 | AS-09 | TB-A | 3+3+3+3+3=**15** | 影响信号但不直接资金 |
| S-05 | 仿冒 internal IPC 客户端骗 signer 签名 | AS-01, AS-04 | TB-B | 4+5+3+2+4=**18** | signer 必须验对端 |

### T — Tampering (篡改)

| ID | 威胁 | 资产 | 边界 | DREAD | 备注 |
|---|---|---|---|---|---|
| T-01 | 供应链投毒 (vcpkg / 上游包被植入恶意代码) | AS-07, AS-12 | TB-A | 5+5+3+2+4=**19** | xz-utils 教训 |
| T-02 | 构建产物在 CI/CD 被植入后门 | AS-07, AS-13 | TB-D | 4+5+3+3+3=**18** | reproducible build 缓解 |
| T-03 | 篡改 RiskManager 红线配置绕过下单 | AS-08 | TB-D | 4+5+3+2+4=**18** | 配置签名 + double review |
| T-04 | 篡改链上交易 calldata (在签名前注入) | AS-04 | TB-B | 5+5+3+2+4=**19** | 业务层 + signer 双校验 |
| T-05 | 数据库历史成交记录被改 (掩盖损失) | AS-06, AS-10 | TB-D | 3+5+2+2+3=**15** | append-only + hash chain |
| T-06 | TLS cert pin 配置被偷换 | AS-14 | TB-D | 4+5+2+2+3=**16** | 二进制内嵌优先 |

### R — Repudiation (抵赖)

| ID | 威胁 | 资产 | 边界 | DREAD | 备注 |
|---|---|---|---|---|---|
| R-01 | 内部成员事后否认下单 / 配置变更 | AS-08, AS-10 | TB-D | 3+4+2+2+3=**14** | 全链路 audit log + 操作签名 |
| R-02 | 签名记录缺失, 无法对账链上交易归属 | AS-04, AS-10 | TB-E | 3+4+2+2+3=**14** | signer 出 audit log |
| R-03 | agent 误操作后日志被自动 rotate 覆盖 | AS-10 | TB-D | 3+3+2+2+3=**13** | log 留存 ≥ 180 天 + 异地备份 |

### I — Information Disclosure (信息泄露)

| ID | 威胁 | 资产 | 边界 | DREAD | 备注 |
|---|---|---|---|---|---|
| I-01 | 私钥落日志 / core dump / 异常堆栈 | AS-01 | TB-C | 5+5+3+3+4=**20** | **最高优先级**, 红线 §8 |
| I-02 | API key 写进 git commit / Slack 截图 | AS-02, AS-03 | TB-D | 4+5+4+3+3=**19** | pre-commit hook + secret scanner |
| I-03 | .env 误打包进容器镜像 / 二进制 | AS-02, AS-05 | TB-D | 4+5+3+2+3=**17** | 镜像 SBOM + 扫描 |
| I-04 | metrics 标签泄露策略参数 / 持仓 | AS-07, AS-06 | TB-A | 3+3+3+3+3=**15** | metrics 白名单 |
| I-05 | 跨洋链路被嗅探 (BGP 劫持 / ISP 中间盒) | AS-02, AS-09 | TB-A | 4+3+3+3+3=**16** | TLS + cert pin |
| I-06 | dev 笔记本被入侵泄露 git/凭证 | AS-02, AS-07 | TB-D | 3+4+3+3+3=**16** | dev 机不放生产凭证 |
| I-07 | LLM agent prompt 注入泄露上下文凭证 | AS-02 | TB-D | 3+4+4+3+3=**17** | agent 不直接读 .env, S1-020 联动 |

### D — Denial of Service (拒服)

| ID | 威胁 | 资产 | 边界 | DREAD | 备注 |
|---|---|---|---|---|---|
| D-01 | Goalserve 限流 / 封号 → 数据流断 | AS-09 | TB-A | 3+3+3+4+3=**16** | 多源 fallback + 代理池 |
| D-02 | Polymarket WSS 主动断连 / 反爬限流 | AS-09 | TB-A | 4+3+4+4+3=**18** | 多 endpoint + 重连退避 |
| D-03 | Polygon RPC 单点拥堵导致下单超时 | AS-04 | TB-A | 4+3+3+4+3=**17** | 多 RPC + 私有节点 fallback |
| D-04 | 跨洋链路抖动 / 丢包 → 错过出场 | AS-04 | TB-A | 4+4+3+4+3=**18** | S1-010 部署方案配套 |
| D-05 | 主机资源耗尽 (内存泄漏 / fd 泄漏) | 整机 | TB-B | 3+3+3+3+3=**15** | resource limit + cgroup |
| D-06 | 恶意构造盘口数据触发解析路径死循环 | AS-09 | TB-A | 3+3+3+3+3=**15** | fuzz + 超时熔断 |

### E — Elevation of Privilege (权限提升)

| ID | 威胁 | 资产 | 边界 | DREAD | 备注 |
|---|---|---|---|---|---|
| E-01 | RCE: 第三方依赖 CVE → 攻击者拿到主机 shell | AS-01, AS-04 | TB-A | 5+5+3+3+4=**20** | **最高优先级**, CVE watch |
| E-02 | trader 进程被入侵后横向到 signer | AS-01 | TB-B | 5+5+2+2+4=**18** | 进程隔离 + seccomp |
| E-03 | sudo / root 滥用 / SSH key 泄露 | 整机 | TB-D | 4+5+3+3+3=**18** | bastion + 最小权限 |
| E-04 | container escape (若用容器) | 整机 | TB-B | 4+5+2+3+4=**18** | gVisor / Kata 可选 |
| E-05 | 班底 agent 越权 (拿到不属于自己的资源) | AS-07, AS-08 | TB-D | 3+4+3+3+3=**16** | RACI + 工具白名单 |

---

## 4. 高危场景 (按可能性 × 影响排序)

DREAD ≥ 18 的为 **P0 高危场景**, 必须 Sprint-1 末出缓解 spec.

| Rank | 场景 ID | 描述 | DREAD | 类型 | Owner |
|---|---|---|---|---|---|
| 1 | **I-01** | 私钥写进日志 / core dump / 异常堆栈 | 20 | 信息泄露 | 老沈 + 老孙 |
| 2 | **E-01** | 第三方依赖 RCE 拿下主机, 攻击者直接动用私钥转账 | 20 | 权限提升 | 老沈 + 老吴 |
| 3 | **T-01** | 供应链投毒 (vcpkg 或上游包恶意更新) | 19 | 篡改 | 老沈 + 老何 + 老张 |
| 4 | **T-04** | 链上交易 calldata 在签名前被篡改 (改 receiver / amount) | 19 | 篡改 | 老孙 + 老叶 |
| 5 | **I-02** | API key 提交进 git / 被截图泄露 | 19 | 信息泄露 | 老沈 + 全员 |
| 6 | **S-05** | 仿冒 IPC 客户端骗 signer 签名 | 18 | 身份伪造 | 老孙 + 老沈 |
| 7 | **D-02** | Polymarket WSS 主动断连导致错过出场窗口 | 18 | 拒服 | 老李 + 老陈 |
| 8 | **D-04** | 跨洋链路抖动错过止损 | 18 | 拒服 | 老吴 + 老陈 |
| 9 | **T-02** | CI/CD 构建产物被植入后门 | 18 | 篡改 | 老吴 + 老沈 |
| 10 | **T-03** | RiskManager 红线配置被改 | 18 | 篡改 | 老韩 + 老沈 |
| 11 | **E-02** | trader 进程入侵后横向到 signer | 18 | 权限提升 | 老沈 + 老吴 |
| 12 | **E-03** | SSH key 或 sudo 滥用 | 18 | 权限提升 | 老吴 + 老沈 |
| 13 | **E-04** | container escape | 18 | 权限提升 | 老吴 |

---

## 5. 缓解措施 (按威胁映射)

### 5.1 私钥相关 (映射 I-01 / E-02 / S-05 / T-04 / R-02)

- **M-K1 独立 signer 进程**: signer 与 trader 分进程, signer 不开 egress (iptables OUTPUT drop), 仅通过 Unix domain socket 收签名请求. (S1-005 输出实施 spec)
- **M-K2 内存保护**: 私钥所在页 `mlock` + `madvise(MADV_DONTDUMP)` + `prctl(PR_SET_DUMPABLE, 0)`; 进程退出前 `explicit_bzero` 清零.
- **M-K3 IPC 鉴权**: signer 校验对端 PID + UID + cgroup, 签名请求需带 HMAC + nonce + RiskManager checksum, 防 replay.
- **M-K4 链上白名单**: signer 内置 receiver 白名单 (Polymarket exchange contract 等), 非白名单地址直接拒签.
- **M-K5 签名速率限制**: signer 内置 per-minute / per-hour 签名次数上限, 触顶报警 + 暂停.
- **M-K6 启动期解封**: 私钥不落盘, 启动时从 KMS/HSM 解封 (S1-005 选型), 解封需 ops + risk 双人审批.
- **M-K7 audit log**: 每次签名出 append-only 记录 (timestamp + tx hash + calldata hash + caller checksum), 异地备份.
- **M-K8 core dump 全禁**: 生产 ulimit `-c 0` + systemd `LimitCORE=0`, 调试用专用副本 (无真私钥).

### 5.2 API key (映射 I-02 / I-03 / I-07)

- **M-A1 pre-commit secret scanner**: gitleaks / trufflehog 在 commit 前扫描, CI 也再扫一次.
- **M-A2 .env 不入镜像**: 镜像构建期不挂载 .env; secret 运行时通过 KMS / instance metadata 注入.
- **M-A3 LLM agent 隔离**: agent 进程不直接读 .env, 凭证通过 wrapper 临时 export; prompt 中含凭证关键词时拒绝执行.
- **M-A4 凭证轮换**: Polymarket API key / Goalserve key 每 90 天轮换, DATABASE password 每 180 天轮换, 写进 SOP.
- **M-A5 最小作用域**: 一份 key 一个用途, Polymarket key 不共享给数据回灌脚本.
- **M-A6 SBOM + 镜像扫描**: trivy / grype 扫镜像, 触发 .env / private_key 这类敏感字符串报警.

### 5.3 跨洋 MITM (映射 S-01 / S-04 / I-05 / T-06)

- **M-N1 TLS cert pinning**: Polymarket / Goalserve / Polygon RPC 的 leaf cert 或 SPKI hash 内嵌二进制, mismatch 直接拒.
- **M-N2 多 RPC 多源**: Polygon RPC 至少 3 家 (官方 + alchemy/infura + 自建 archive 节点), nonce / chainTip 多源比对, 异常报警 + 切换.
- **M-N3 双向校验**: WSS 心跳 / sequence number 监控, 数据跳变报警.
- **M-N4 DNSSEC + DoH**: DNS 走 DoH 防本地 DNS 投毒.
- **M-N5 链路加密**: 跨洋链路除 TLS 外加 wireguard, 防 ISP 中间盒.
- **M-N6 BGP 监控**: routeviews / RIPE atlas 监控目的 IP 的 BGP 公告异常 (老吴 owner).

### 5.4 供应链 (映射 T-01 / T-02 / T-06)

- **M-S1 vcpkg pin**: 所有依赖 pin 到 commit hash, 升级走 PR + 老沈 review, 不允许 `latest` / `master`.
- **M-S2 SBOM**: 每次构建生成 SBOM (CycloneDX 或 SPDX), 入库.
- **M-S3 CVE watch**: OSV / GHSA 订阅, 高危 CVE (CVSS ≥ 7) 48h 内出修复 plan.
- **M-S4 reproducible build**: 至少关键模块支持 reproducible build, 双机交叉验证产物 hash.
- **M-S5 CI 隔离**: GitHub Actions runner 不持有生产凭证, 构建产物推 artifact registry, 部署用独立 deploy key.
- **M-S6 二进制签名**: 生产部署只接受签名过的二进制, 验签失败拒绝启动.
- **M-S7 Polymarket / Goalserve 供应链**: 视作 untrusted 外部源, 解析层严格校验 (schema + range + outlier), 不假设上游永远诚实.

### 5.5 RiskManager 配置 (映射 T-03 / R-01)

- **M-R1 配置签名**: 红线配置文件 GPG 签名, 启动期验签, mismatch 拒绝启动.
- **M-R2 double review**: 红线变更需 老韩 + 老郭 双签 (合 §8 红线 §3 不可越界).
- **M-R3 运行时 immutable**: 配置加载后置 read-only, 运行期变更必须 SIGHUP + 验签流程.
- **M-R4 audit chain**: 配置变更记录 + 操作者 + diff, append-only.

### 5.6 拒服 / 灾难恢复 (映射 D-01 ~ D-06)

- **M-D1 多源 + fallback**: 数据源 / RPC 至少双源, 自动切换.
- **M-D2 熔断器**: 数据延迟 / RPC 失败率超阈值, 自动进入只平仓不开仓模式.
- **M-D3 资源限制**: cgroup memory / cpu / fd 限制, OOM 前主动 kill 并安全退出.
- **M-D4 fuzz**: 解析层 (JSON / WSS message) 走 libfuzzer, CI 每次跑 30min.
- **M-D5 失联 SOP**: 数据源失联 > N 秒 → 撤所有挂单 → 暂停下单 (由 RiskManager 强制, 不依赖人工).

### 5.7 权限 / 越权 (映射 E-03 / E-05 / S-03)

- **M-P1 bastion + MFA**: 生产主机不开公网 SSH, 走 bastion + 硬件 MFA.
- **M-P2 最小权限**: trader / signer 分账户运行, signer 账户 nologin, 不能 sudo.
- **M-P3 seccomp + AppArmor**: signer 进程 seccomp 白名单系统调用 (read/write/sendmsg/recvmsg/clock_gettime), 拒绝 execve / ptrace.
- **M-P4 agent 工具白名单**: 每个 sub-agent persona 限定 tools, 不允许跨域执行.
- **M-P5 操作签名**: 关键操作 (部署 / 配置变更 / 凭证轮换) 走 sigstore 风格的 keyless 签名 + 审计.

---

## 6. 应急响应 SOP

### 6.1 角色 (Incident Command)

| 角色 | 默认人 | 备份 | 职责 |
|---|---|---|---|
| Incident Commander (IC) | **老雷** | 老郭 | 拍板 / 升级 / 对外口径 |
| Security Lead | **老沈** | 老孙 | 取证 / 修复方案 / CVE 响应 |
| Risk Lead | **老韩** | 老黄 | 资金止血 / 仓位评估 |
| Ops Lead | **老吴** | 老陈 | 隔离 / 重建 / 凭证轮换 |
| On-chain Lead | **老叶** | 老孙 | 链上追踪 / 资金转移 |
| Comms / 内部 | 老胡 | 小米 | timeline / 通报 / post-mortem 跟进 |

**红线**: 涉私钥 / 资金事件, **任何人** 都可直接 page 老雷 + 老沈 + 老韩 (CLAUDE.md §8 风控红线).

### 6.2 P0 灾难场景与 SOP

#### 场景 A: 私钥泄露 / 疑似泄露

1. **T+0 (任意人触发)**: page 老沈 + 老韩 + 老雷, 调用 `kill-switch.sh` (老吴出脚本, S1-010) 立即:
   - 撤销所有 Polymarket 挂单 (API cancel-all)
   - signer 进程 SIGTERM
2. **T+5min (老叶)**: 链上把 USDC 余额 sweep 到冷备地址 (预先生成, 多签).
3. **T+15min (老韩)**: 评估剩余敞口, 决定是否手动平仓持有的 Polymarket position (链上).
4. **T+30min (老沈)**: 取证 — 主机内存 dump + 全量日志 + 网络抓包归档异地, 隔离主机但**不重启**.
5. **T+2h (老沈 + 老雷)**: 第一版 timeline 出, 通报全员.
6. **T+24h (老沈)**: post-mortem draft, RCA + 修复 plan.
7. **T+48h (老雷)**: 公开 incident report (内部), 启动新私钥生成 + 全链路凭证轮换.

#### 场景 B: 主机失陷 (RCE / SSH 入侵)

1. **T+0**: page 老沈 + 老吴 + 老雷.
2. **T+2min**: 老吴在 bastion 切断主机 egress (iptables drop), **不要关机** (留内存取证).
3. **T+5min**: 老叶按场景 A 步骤 sweep 链上资金, 视私钥是否同主机决定.
4. **T+15min**: 老吴在干净主机重建服务, 老沈做主机镜像.
5. **T+1h**: 老沈定位入侵路径 (CVE? 凭证? 配置错误?), 出临时缓解.
6. **T+24h**: 全栈凭证轮换 (私钥 / API key / DB / SSH).

#### 场景 C: 资金被盗 (链上交易确认)

1. **T+0**: 自动监控 (老叶 + 小郑) 检测到非白名单 receiver 的 outgoing tx → 自动 page.
2. **T+1min**: 老叶尝试链上拦截 (高 gas 抢 nonce, 视时机).
3. **T+10min**: 全量 sweep 剩余资金到冷备.
4. **T+30min**: 老雷决定是否对外 (Polymarket / 监管) 通报.
5. **T+24h**: 链上追踪 (etherscan / Chainalysis) + 法务跟进 (老黄, 跨部门移交).

#### 场景 D: 供应链投毒 (依赖被植入 / CVE)

1. **T+0 (老沈 OSV watch)**: 触发 → 评估是否影响本项目 (依赖图 + SBOM 比对).
2. **T+1h**: 如确认影响, 锁版本 + CI 阻断升级.
3. **T+4h**: 评估是否已经构建过受影响版本到生产 → 是则按场景 B 处理.
4. **T+24h**: 修复版本上线, 全员通报.

### 6.3 演练计划

- **Q3 2026 一次**: 私钥泄露 tabletop 演练 (老沈主持, 全员参与)
- **Q4 2026 一次**: 主机失陷红蓝对抗 (老吴 + 老沈)
- **MVP 上线前**: kill-switch 实战演练 (生产环境, 用 dummy 资金)

---

## 7. 安全审计计划

| 频率 | 内容 | Owner | 输出 |
|---|---|---|---|
| 每周一 | CVE 扫描 (vcpkg deps + base image) | 老沈 | weekly CVE report |
| 每周五 | secret scan (git history + image + log) | 老沈 + 小郑 | secret-scan report |
| 每月 | 依赖 SBOM diff + 升级 review | 老沈 + 老何 | SBOM-monthly.md |
| 每月 | audit log 完整性校验 (hash chain) | 老唐 + 老沈 | audit-integrity report |
| 每季度 | 凭证轮换 (API key / DB password) | 老吴 + 老沈 | rotation log |
| 每季度 | pen test (内部红队 by 老沈, 外部 1 年 1 次) | 老沈 | pen-test report |
| 每季度 | 应急响应演练 | 老沈 + 老雷 | drill report |
| 每年 | 第三方安全审计 (HSM/KMS + 智能合约交互) | 老雷 (采购) + 老沈 (技术对接) | 外部审计报告 |
| 持续 | SIEM 告警 (异常签名 / 异常 RPC / 异常 SSH) | 小郑 + 老沈 | Grafana alert |

---

## 8. 开放问题

| # | 问题 | 阻塞方 | 期望解决 sprint |
|---|---|---|---|
| O-01 | HSM vs Cloud KMS vs 软件 keystore 最终选型 (S1-005) | 老孙 | Sprint-1 末 |
| O-02 | 冷备私钥的多签 threshold (2/3? 3/5?) 及备份保管 | 老孙 + 老雷 | Sprint-2 |
| O-03 | Polygon RPC 私有节点是否自建 (成本 vs 信任) | 老叶 + 老吴 | Sprint-2 |
| O-04 | 是否引入 TEE (SGX / Nitro Enclave) 跑 signer | 老沈 + 老孙 | Sprint-3 评估 |
| O-05 | 跨洋链路 wireguard 是否够用, 是否上专线 | 老吴 | Sprint-2 |
| O-06 | LLM agent (本项目大量使用 Claude sub-agent) 是否需要单独的安全沙箱 | 老沈 + 小白 | Sprint-2, 与 S1-020 联动 |
| O-07 | RiskManager 配置签名的密钥本身存哪里 (不能跟交易私钥同位置) | 老韩 + 老沈 | Sprint-2 |
| O-08 | 审计日志异地备份的目的地 (合规角度, 老黄) | 老沈 + 老黄 | Sprint-2 |
| O-09 | kill-switch 是否需要硬件按钮 (物理隔离主机的最快手段) | 老吴 + 老沈 | Sprint-3 评估 |
| O-10 | 智能合约交互的 EIP-712 typed-data 校验是否在 signer 内做完整 schema 校验 (而非只验 hash) | 老孙 | Sprint-1 末 |
| O-11 | 凭证轮换期间的 zero-downtime 方案 (Polymarket key 重叠期) | 老沈 + 老李 | Sprint-2 |
| O-12 | 对老叶 Polygon 选型方案的安全 review (RPC 多源 + nonce 一致性) | 老沈 → 老叶 | Sprint-1 末 (与 S1-009 协作) |
| O-13 | 班底 agent 在 commit / 部署链路上的身份认证机制 (S-03 关联) | 老沈 + 老徐 + 老吴 | Sprint-2 |

---

## 附录: 与其他 Sprint-1 ticket 的接口

- **S1-005 (老孙)**: 本文档的 §5.1 私钥缓解需要老孙的 HSM/KMS 选型落地, 老孙的 spec 必须满足 M-K1 ~ M-K8.
- **S1-009 (老叶)**: 本文档 §5.3 / §6.2 场景 C 依赖老叶的多 RPC 方案 + 链上监控.
- **S1-010 (老吴)**: 本文档 §5.6 跨洋抖动 + §6.2 主机失陷依赖老吴部署方案的 bastion / kill-switch / 监控.
- **S1-018 (小郑)**: 本文档 §7 审计依赖小郑的 Prometheus / Grafana alert 通道.
- **S1-006 (老黄)**: 本文档 §6 应急响应中对外通报 / 监管走老黄合规线.
- **S1-019 (老徐)**: 本文档 S-03 / E-05 班底越权依赖老徐的协同规范 RACI.
- **S1-020 (小白)**: 本文档 I-07 LLM prompt 注入风险与小白的 LLM 规范联动.

---

**v1 状态**: 待 Sprint-1 末与老孙 S1-005 终稿 cross-review 后定 v1.0; 任何 P0 高危场景在 MVP 上线前必须有可验证缓解.
