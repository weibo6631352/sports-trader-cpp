# 私钥方案 Co-Review v1

- Co-reviewer: 老沈 (security-engineer)
- Reviewing: laosun-key-management-v1.md (Rust 版, 已删; 当前权威版本 v5.1)
- Date: 2026-05-28
- 验收人: 老雷
- 关联: S1-005 (老孙) / S1-016 (老沈威胁模型) / S1-006 (老黄红线) / S1-010 (老吴跨洋)
- 不耻下问: 链上 @老叶, 跨境合规 @老黄, KMS / IAM 细节 @老吴

---

## 1. 总评

**Conditional Accept (有条件通过)**

老孙这版 v1 我整体认可: 选型理由扎实, 跨洋延迟决策正确, 红线表 R1~R7 与我威胁模型对齐, 轮换 SOP / 灾备 / 阈值审批都给到了细节. 拒绝 AWS/GCP KMS 直签的论证我同意.

但在 **STRIDE 完备性 + 阈值审批"谁拍 / 怎么验"的工程实现 + 供应链 (Rust signer 依赖) + Shamir 跨境分片** 4 个面上有 **5 个 Blocker** 必须 v2 修掉才能进 Sprint-2 实施, 否则方案是"看上去安全, 落地时缝有洞".

补充: 老孙这个方案如果按下面 Blocker 全部修完, 我作为安全官签字; 但 IAM 边界 (Q1 推迟到老吴定后再说) 这一项必须在 Sprint-2 开始前关闭, 不能再拖.

---

## 2. STRIDE 逐项

按老孙 §3.1 的进程模型 + §6 IPC 协议 + §8 灾备做 STRIDE.

### 2.1 S — Spoofing (身份伪造)

**覆盖度: 部分覆盖**, 老孙 §6.3 没写 IPC 鉴权.

| 威胁点 | 老孙方案现状 | 老沈评估 | 处置 |
|---|---|---|---|
| 仿冒 IPC 客户端骗 signer 签名 (映射 S-05, DREAD 18) | §6.1 写了 UDS 0600, 没写 peer 身份校验 | UDS 0600 仅约束文件系统 access, 不能防 trader 主进程被入侵后仿冒 (映射 E-02 横向). 必须用 SO_PEERCRED 拿 PID/UID/GID, 校验对端 cgroup + binary hash | **Blocker B1** |
| signer 启动期取 KMS unwrap 时被 MITM | §3.1 写了"通过 IAM role 拉取" | IAM role 本身可信, 但从 signer 进程到 KMS endpoint 之间的 TLS 必须 cert pin, 不能依赖系统 CA store (映射 T-06 / I-05) | **Blocker B2** |
| 审批 bot Slack 链路仿冒 (运营冒充老韩/老雷点确认) | §7.2 写了 Slack bot + HMAC | HMAC 只能证消息没改, 不能证操作人是谁. 必须再加一层"审批人侧 hardware token 签名 (WebAuthn / YubiKey)" | **Blocker B3 (审批身份)** |
| signer binary 自身被替换 | §9.2 写了 dev/prod 隔离, 没写 binary 完整性 | 部署期必须验签, 启动期 signer 自校验自己 binary hash (映射 T-02 CI 投毒) | **Blocker B4** |

**M-K3 (我威胁模型) 已要求 PID+UID+cgroup + HMAC + nonce, 老孙 v2 必须明文写进 §6.3.**

### 2.2 T — Tampering (篡改)

**覆盖度: 中等**, 但 calldata 篡改防线只在 signer 内, 缺业务层校验.

| 威胁点 | 老孙现状 | 老沈评估 |
|---|---|---|
| age 文件被篡改 | age 自带 AEAD (chacha20-poly1305), 完整性 OK | **age 单独是够的**, 但 KMS unwrap 出来的 age key 要在内存 verify keyfile 头 fingerprint, 不能盲信文件名 |
| signer binary 完整性 | 未提 | 见 B4. 需要 sigstore / minisign 离线签名 + systemd ExecStartPre 验签 |
| 链上交易 calldata 在签名前被注入 (T-04, DREAD 19) | §6.3 的 IPC schema 只传 `message_hash`, trader 已算好 hash 才发给 signer | **这是个隐患**: signer 收到的是 32 字节 hash, 完全无法二次校验"这真的是发给 Polymarket exchange 合约的吗?". 必须让 signer 看完整 typed data, 自己重算 hash 并校验 receiver/amount 白名单. 老孙 §3.1 + §6.3 这块要重写 | **Blocker B5 (signer 二次校验)** |
| TLS cert pin 配置被偷换 (T-06) | §9.1 写了 iptables 限 KMS endpoint, 没写 cert pin | 配置文件 cert pin 改了等于没 pin, 必须 pin 编进 signer binary | 建议改 |
| 审计 WAL 被改 | §5.3 / §9.4 写了 append-only, hash chain 缺失 | hash chain (前一条 hash 拼当前条) + 异地定期备份, 与老唐 audit 对齐 | 建议改 |

### 2.3 R — Repudiation (抵赖)

**覆盖度: 较好**, 但审批日志 + 跨地区时钟需补.

| 威胁点 | 老孙现状 | 老沈评估 |
|---|---|---|
| 签名记录可对账 (R-02) | §3.1 + §9.4 写了 WAL append-only | OK, 但要扩字段: 见下 |
| 审批操作可追溯 | §7.2 提了 HMAC, 没说留档 | 审批人 WebAuthn assertion (含 challenge + signature) 必须存档, 时间戳 NTP-synced |
| 内部成员事后否认 (R-01) | 红线 R5 兜底 | OK, 但 WAL schema 老孙 v2 要明列 |

**WAL 强制字段 (老沈建议)**:

```
ts_ns_ntp_synced, request_id, intent, chain_id, domain_separator,
typed_data_full, receiver_addr, amount_usdc,
trader_pid, trader_uid, trader_binary_hash,
approver_id (if any), approver_webauthn_assertion (if any),
prev_record_hash, current_record_hash
```

老唐 audit 接的就是这张表, 字段缺一不可对账.

### 2.4 I — Information Disclosure (信息泄露)

**覆盖度: 好, 但 mlock 不够 (这是最关键的反驳老孙的点)**.

老孙 §3.1 列了 `mlock + setrlimit(RLIMIT_CORE,0) + memset`, 我我威胁模型 M-K2 要求的是 **完整 4 件套**: `mlock + MADV_DONTDUMP + PR_SET_DUMPABLE=0 + explicit_bzero`. 老孙少了两件:

| 防御项 | 老孙 v1 | 防什么 | 必要性 |
|---|---|---|---|
| `mlock` | 有 | swap 落盘 | 有 |
| `setrlimit(RLIMIT_CORE,0)` | 有 | core dump 落盘 | 有 |
| `memset` | 有 (但编译器可能优化掉!) | 退出清零 | 有, 必须改成 `explicit_bzero` 或 `secure_zero` |
| `MADV_DONTDUMP` | **缺** | 即便 RLIMIT_CORE=0, 内核 sysrq / kernel panic dump / 调试器 ptrace 可读 | **必须加** |
| `PR_SET_DUMPABLE=0` | **缺** | 防 ptrace + /proc/<pid>/mem 被读 (横向后还能不能 dump 私钥的关键) | **必须加** |
| seccomp 禁 ptrace / process_vm_readv | **缺** | 进程级反调试 | 建议加 (M-P3) |
| /proc/sys/kernel/yama/ptrace_scope=2 | 老吴侧 | 系统级反调试 | 部署清单加 |

**这块算 Blocker B6 — 内存防护四件套必须凑齐**, 因为 I-01 (DREAD 20) 是最高优先级威胁.

其他 I 类:
- swap 全禁: §9.1 写了, OK
- 私钥不进日志: 红线 R2 + signer 强制 redact wrapper, OK
- core dump 全禁: §9.1 systemd 写了 `LimitCORE=0`, OK
- 跨洋链路被嗅探 (I-05): §9.1 iptables 只放 KMS endpoint, OK, 但要补 cert pin (B2)

### 2.5 D — Denial of Service (拒服)

**覆盖度: 不够**. 老孙 §8 灾备讲了"主备双 wallet 热-热", 但**没讲 signer 进程自己的 HA**.

关键问题:

| 问题 | 老孙现状 | 老沈追问 |
|---|---|---|
| signer 进程崩了, trader 还在跑, 怎么办? | §8.1 第 1 行: "重启 signer, 走 KMS unwrap" | **重启要多久?** KMS unwrap 同 region 30ms, 但启动加载 age key + mlock + 自检 + UDS bind 估算 3~10s. 在这 3~10s 内, trader 收到的所有 fill 都没法挂 cancel/replace, 跨洋 round-trip 加上去, 错过出场窗口 **极可能** |
| 是否上 active-active signer (主备热-热)? | 未明确 | 老孙 §8.1 提了"主备双 wallet", 但**两个 wallet 是不同 signer 进程吗?** 还是同一 signer 持有两份 key? 不清楚 |
| signer 崩了的可观测信号 | §9.3 写了 "1s 粒度存活监控" | 1s 粒度太粗, signer 必须做 trader-side health check (UDS ping, 100ms 粒度), 失败立刻进 cancel-only |
| HA SLO 缺失 | 未给 | 必须给: signer 不可用 detection < 100ms, 切换 < 1s, 期间 trader cancel-only |

**处置 (Blocker B7, HA SLO)**:
- v2 必须给出 **active-active 双 signer 进程**方案 (两个 signer 持同一份 key, 不同 UDS, trader 双连; 或一主一温备)
- 如果是 active-active, 两个 signer 的 audit WAL 要合流 + nonce 不能冲突 (链上 nonce 由谁分配?), 这块跟 @老叶 onchain 必须对齐
- 给 detection / failover 数字, 进 SLA dashboard

### 2.6 E — Elevation of Privilege (权限提升)

**覆盖度: 较好**, 但 IAM 边界 + Shamir 持有人审查不够具体.

| 威胁点 | 老孙现状 | 老沈评估 |
|---|---|---|
| AWS KMS IAM 角色配置 | §10 Q1 推迟到老吴 | **必须 Sprint-2 开始前定**: signer 进程 IAM role 仅能 `kms:Decrypt` 单个 CMK, 不能 list/encrypt/describe; CMK key policy 仅允许该 role + 一个紧急恢复 role; 启用 CloudTrail + KMS grant 监控. **@老吴 这块要出 IAM JSON spec.** |
| signer 主机被入侵后横向 (E-02, DREAD 18) | §9.1 systemd hardening 一堆, OK | 加 seccomp 白名单 (M-P3) |
| Shamir 分片持有人审查 | §8.3 列了 5 人 | **3 个问题**: (1) 老孙自己持 1 份, 老孙离职就要 90 天提前 rotate; (2) "法务/外部托管" 是占位符, 必须老黄 sign-off 第三方托管商身份; (3) 见 §4 跨境合规分析 |
| Shamir 恢复时的"双人见证"够不够 | §8.3 写了"任意 3 人到场 + 视频 + 老雷书面" | 3 人到场可能撞合谋, 建议加 "3 人来自不同司法管辖区 + 视频异地同步" |
| KMS 紧急恢复 role 单点 | 未提 | break-glass account: 离线 root credential + 2FA hardware token + 老雷 + 老沈 双持有 |

---

## 3. 阈值审批实现细节 (谁拍 / 多因子)

老孙 §7 给了金额阈值表, 但 **"谁来单人审批" 和 "怎么验证身份" 没说透**. 这是合规 + 安全的交叉, 我跟老韩对齐过, 我的要求:

### 3.1 审批人身份验证 (Blocker B3 展开)

| 阈值 | 审批人 | 身份验证 (老沈强制) | SLA |
|---|---|---|---|
| < $500 | 自动 | signer 自身 + 链上白名单 | < 1ms |
| $500 ~ $2k | 自动 + Slack 通知 | Slack 通知只是知会, 非审批 | < 1ms |
| $2k ~ $5k | 老韩 或 老雷 单签 | **WebAuthn (硬件 key) 签 challenge** + **2FA TOTP** 双因子, 不能只点 Slack 按钮 | < 30s |
| > $5k | 老韩 + 老雷 双签 | 两人各自 WebAuthn (硬件 key 不能是同一把) + TOTP, 时间窗 60s 内两签都到 | < 60s |
| 日累计 > $20k | 老韩 + 老雷 双签 | 同上 + 老黄 24h 内补合规批注 (异步) | < 60s |
| Approve (USDC allowance) | 老雷 双签 (一人两次?) | **改成老雷 + 老沈 双签** (Approve 等于把代币写权交给合约, 涉安全, 我必须在场) | < 5min |

**关键约束**:

1. 审批人 hardware key 入选前必须老沈 + 老雷见证录入 fingerprint, 不允许私下增减
2. WebAuthn challenge 必须含 typed_data hash, 防 "审批人看到的金额跟实际签的不一致" (UI 替换攻击)
3. 审批 UI (小苏 frontend) 自己也算高敏感面, 必须 cert pinning + 不允许在公网 dev 机访问
4. 审批人手机端 Slack bot 只能"知会 + 看 typed data 摘要", **不能**作为唯一审批途径
5. 审批超时 (60s) 默认 reject, 不允许"轻按错过自动 pass"

### 3.2 阈值不可绕过 (老孙 §7.3 已写, 我补强)

老孙写了"烧在启动配置 + KMS-signed config", OK. 我加一条: signer 启动期校验 config hash 必须出现在 git tag 的 signed commit 里 (sigstore / minisign), 防"运维偷偷改 config + 自己 KMS-sign 一份".

---

## 4. 跨境合规对接 @老黄

这块我专门拉老黄确认, 但**我先列我看到的问题**, v2 review 时老黄拍板.

### 4.1 Shamir 分片跨境的合规面

老孙 §8.3 列的 5 个持有人, 城市 A / 城市 B / 城市 C 都是占位符. 我跟老黄红线对齐:

| 老黄红线 (R2 §2.2) | 老孙方案触点 | 老沈风险评估 |
|---|---|---|
| **R4 美国元素绝对禁** | Shamir 分片若放美国托管商保险柜 (常见做法) → **违 R4** | 必须避美国境内托管 |
| **R5 OFAC 制裁名单关联** | 第三方托管商若有 OFAC 黑名单往来 → 违 R5 | 选托管商前要扫 OFAC + Chainalysis |
| **R8 私钥明文落盘** | Shamir 分片**等于**主密钥的一部分, 放金属种子板属 "物理介质明文". 红线 R8 的"明文落盘"按字面狭义但精神上分片落地需谨慎 | 建议: 分片本身再用 BIP39 passphrase 加一层 (人脑/分人保管 passphrase), 单一分片落地不等于半个密钥 |
| **中国大陆 HIGH 风险** | 老孙 §8.3 城市 A/B 若都在国内 → 主密钥重建能力集中在境内, 任何境内监管事件可能锁死全部恢复路径 | **必须**至少 2 份分片在境外司法管辖区, 老黄过的清单 (推荐: 新加坡, 瑞士, 香港 + 一份 vault 在合规友好的离岸司法管辖区) |
| **OFAC 国绝对禁** | 异地托管商若在制裁名单国 | 一票否决 |

### 4.2 Shamir 持有人国籍/居住地的合规面

老孙没写持有人国籍. 我建议:

- 持有人中至少 1 人**不持有美国国籍 / 美国绿卡** (老黄 R4)
- 持有人中至少 2 人**不在中国境内长期居住** (灾难场景中国端断网时, 海外端能凑齐 3 个)
- "法务/外部托管" 占位符必须老黄 sign-off, 我建议**老黄自己持一份**比外部托管法律风险低 (老黄你愿意吗?)

### 4.3 KMS 区域的合规面

老孙默认 AWS us-east-1. 这是 polymarket 同区, 性能是对的, 但合规面:

- AWS us-east-1 在美国管辖, KMS CMK 受 AWS US 公司管辖, 理论上 US 法院可下令 AWS 锁定该 CMK
- 老黄 R4 是禁"用美国 IP/KYC/银行卡", **KMS 不是 KYC, 不是入金路径**, 但 US 法院冻结风险存在
- 老沈评估: 这块 **可接受**, 但必须有 **跨 AWS 账号 + 跨 region 异地 CMK 副本** (us-west-2 也行, 或者 GCP / Azure 不同 vendor), 避免单一 vendor 单一管辖区锁死
- @老黄 确认这块是否触红线; 我倾向不触, 但要写进 risk acceptance memo

### 4.4 合规对接结论

- **必须 v2**: Shamir 持有人国籍 + 居住地 + 司法管辖区清单, 老黄逐人 sign-off
- **必须 v2**: 跨 region / 跨 vendor 的 KMS 副本方案
- **建议**: 老黄持 1 份分片, 减少外部托管依赖

---

## 5. 与威胁模型对接

我把我自己 S1-016 的高危场景 (DREAD ≥ 18) 跟老孙方案做一对一映射, 看哪些缓解项他覆盖了, 哪些没覆盖.

| 威胁 (DREAD) | 我威胁模型缓解 | 老孙 v1 是否覆盖 | Gap |
|---|---|---|---|
| **I-01** 私钥落日志/dump (20) | M-K2 + M-K8 (mlock 全套) | 部分 (mlock + RLIMIT_CORE 有, MADV_DONTDUMP + PR_SET_DUMPABLE 缺) | **B6** |
| **E-01** 第三方依赖 RCE (20) | M-S1~S5 (vcpkg pin + SBOM + CVE watch) | **未覆盖 Rust signer 侧依赖** (老孙没列 cargo 依赖审查机制) | **B8 供应链** |
| **T-01** 供应链投毒 (19) | M-S1 (pin commit hash) | 同上, signer 是 Rust binary, 老孙说用 alloy-rs/k256/age/secrecy 等 crate, 没说怎么审 | **B8** |
| **T-04** calldata 签名前篡改 (19) | M-K4 链上白名单 + signer 内校验 | **未覆盖** (老孙 IPC 只传 hash, signer 没法看 calldata) | **B5** |
| **I-02** API key 入 git (19) | M-A1 secret scanner | 老孙未涉, 这是别的工程面 | 不在本 review 范围 |
| **S-05** 仿冒 IPC (18) | M-K3 PID+UID+cgroup+HMAC | **未明确** (老孙 §6.3 没写鉴权) | **B1** |
| **D-02 / D-04** 跨洋抖动 (18) | M-D1~D5 多源 + 熔断 | signer HA 缺失 | **B7** |
| **T-02** CI 投毒 (18) | M-S4 reproducible + M-S6 binary 签名 | **未涉 binary 签名验证** | **B4** |
| **T-03** RiskManager 配置 (18) | M-R1 配置签名 | 老孙 §7.3 写了阈值 config KMS-sign, 部分覆盖 | OK |
| **E-02** 横向到 signer (18) | M-K1 进程隔离 + M-P3 seccomp | systemd hardening 有, seccomp **缺** | 建议加 |
| **E-03** SSH/sudo (18) | M-P1 bastion + 最小权限 | §9.1 写了 "禁 SSH 仅 console/bastion", OK | OK |

**结论**: 老孙覆盖了 5/11, 部分覆盖 3/11, 缺 3/11 (B1, B4, B5, B6, B7, B8). 修完 v2 应该能到 10/11, 剩下 I-02 (API key) 是别的工程面.

---

## 6. 必须改 (Blocker)

v2 必须解决以下 **8 个 Blocker**, 否则不能进 Sprint-2 实施.

| # | Blocker | 严重度 | 修复方向 | Owner |
|---|---|---|---|---|
| **B1** | IPC 鉴权缺失 (UDS 0600 不够) | 高 | signer 用 SO_PEERCRED 拿 PID/UID/GID, 校验对端 cgroup + binary sha256; 加 HMAC + nonce 防 replay; trader 启动期跟 signer 做一次 challenge-response 互认 | 老孙 + 老沈 |
| **B2** | KMS endpoint TLS cert pin 缺失 | 高 | signer binary 内嵌 KMS endpoint SPKI hash; 不依赖系统 CA store | 老孙 + 老吴 |
| **B3** | 审批人身份验证只用 Slack HMAC | 极高 | WebAuthn 硬件 key + TOTP 双因子; challenge 含 typed_data hash; Slack 仅知会非审批 | 老孙 + 小苏 + 老沈 |
| **B4** | signer binary 完整性未保证 | 高 | 离线 minisign / sigstore 签名; systemd ExecStartPre 验签; signer 启动期自校验自身 sha256 | 老孙 + 老吴 |
| **B5** | signer 不二次校验 calldata (只签 hash) | 极高 | IPC schema 改成传完整 typed data, signer 重算 hash + 校验 receiver/amount 白名单 (M-K4); 拒签非白名单 receiver | 老孙 + 老叶 |
| **B6** | 内存防护四件套不全 | 极高 | 加 MADV_DONTDUMP + PR_SET_DUMPABLE=0; memset 改 explicit_bzero; 系统级 yama/ptrace_scope=2 (老吴侧) | 老孙 + 老吴 |
| **B7** | signer 单点故障 HA 缺失 | 高 | 给出 active-active / active-standby spec + detection/failover SLO + 双 signer 间 nonce 协调 (与老叶 onchain) | 老孙 + 老叶 + 老吴 |
| **B8** | Rust signer crate 供应链审查缺失 | 高 | 给出 Cargo.lock 评估 + cargo-audit / cargo-deny / cargo-vet 集成方案; 关键 crate (alloy / k256 / age / secrecy) 必须 commit hash pin + 升级走 PR + 老沈 review; 至少季度跑一次 SBOM diff | 老孙 + 老何 + 老张 |

---

## 7. 建议改 (Non-blocker)

修了更好, 不修也能进 Sprint-2, 但 Sprint-2 末必须修.

| # | 建议 | 优先级 |
|---|---|---|
| N1 | WAL hash chain 实现 + 异地备份策略 | 中 (老唐) |
| N2 | seccomp 白名单 (signer 进程仅允许 read/write/sendmsg/recvmsg/clock_gettime/exit_group + 启动期少量) | 中 |
| N3 | Shamir 分片二次加密 (BIP39 passphrase) | 中 |
| N4 | 跨 region / 跨 vendor KMS 副本 | 中 (合规) |
| N5 | 审批 UI 单独 cert pin + 独立部署 | 低 |
| N6 | signer 启动期 air-gap 自检 (校验 binary + config + key file 三签名) | 低 |
| N7 | 季度 chaos drill: 随机 kill signer, 验证 trader 进 cancel-only ≤ 100ms | 低 (Sprint-3+) |
| N8 | age key 再加一层 secret-sharing (KMS unwrap 出来后, 用 2 个 holder 内存中合成) | 低 (深度防御) |

---

## 8. 老孙 v2 改动清单 (回老孙)

老孙看这个清单做 v2, 我准备 v2 出来 24h 内 sign-off.

### 必改 (8 项 Blocker)

1. **§6.3 IPC schema 扩展** (B1, B5):
   - 加 `peer_pid / peer_uid / peer_cgroup / trader_binary_hash` 校验字段
   - 加 `challenge / nonce / hmac` (signer 启动期分发 trader HMAC key)
   - 把 `message_hash` 字段改成 `typed_data_full + claimed_hash`, signer 内重算
   - 加 receiver 白名单字段, 非白名单 (Polymarket exchange 合约地址 + funder address) 拒签

2. **§3.1 启动流程加 cert pin + binary 自校验** (B2, B4):
   - 加 "0. systemd ExecStartPre 验 signer binary 签名"
   - 加 "1.5 校验 KMS endpoint TLS cert SPKI hash"

3. **§3.1 内存防护改完整四件套 + explicit_bzero** (B6):
   - mlock + MADV_DONTDUMP + PR_SET_DUMPABLE=0 + explicit_bzero
   - 系统侧 yama/ptrace_scope=2 加到 §9.1

4. **§7 阈值审批的身份验证机制** (B3):
   - 表格加一列 "身份验证方式"
   - 写明 WebAuthn + TOTP 双因子, challenge 含 typed_data hash
   - 加 "Approve 操作改成老雷 + 老沈 双签"

5. **§8 灾备加 signer HA** (B7):
   - 新增 §8.5: signer active-active / active-standby spec
   - 给 detection / failover SLO (建议 detection < 100ms, failover < 1s)
   - 双 signer 间 nonce 协调跟老叶 onchain 对齐

6. **附录 A 核心库选型 → 加供应链审查机制** (B8):
   - 列 Cargo.lock 关键 crate
   - 加 cargo-audit / cargo-deny / cargo-vet 工作流
   - 老沈 review trigger 条件
   - 季度 SBOM diff cadence

7. **§8.3 Shamir 持有人** (跨境合规):
   - 删除占位符 "城市 A/B/C"
   - 列国籍 + 居住司法管辖区 + 托管介质
   - 必须 2 份境外, 美国境内 0 份, OFAC 国 0 份
   - 至少 1 人非美籍非中国大陆居住
   - 老黄逐人 sign-off

8. **新增 §11 IAM 边界 spec** (老吴协作):
   - signer 进程 IAM role 仅 `kms:Decrypt` 单 CMK
   - CMK key policy 白名单
   - CloudTrail + KMS grant 监控
   - break-glass 紧急恢复 role spec

### 建议改 (N1~N8)

按 §7 优先级, 老孙看着办, 不阻塞 Sprint-2 启动.

---

## 老沈 sign-off 条件

v2 满足以下条件我签字:

- [ ] B1~B8 全部修
- [ ] §4 跨境合规 老黄 sign-off
- [ ] §11 IAM 边界 老吴 sign-off
- [ ] Shamir 持有人 老黄 + 老雷 + 老沈 三签
- [ ] N1~N8 给 Sprint-2 末完成 ETA

预计 v2 出: T+3 天 (老孙 work).
预计 v2 review: T+5 天 (老沈 24h sign-off).
预计 Sprint-2 进实施: T+7 天.

---

*Co-review v1 提交: 2026-05-28*
*下一步: 老孙写 v2 + 老黄 / 老吴 / 老叶 在各自范围 sign-off.*
