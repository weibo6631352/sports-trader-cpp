# 私钥管理方案 v1

- Owner: 老孙 (crypto-signing-expert)
- Co-review: 老沈 (security-engineer)
- Co-consult: 老叶 (defi-onchain-advisor), 老吴 (linux-sre-devops), 老黄 (compliance-legal)
- Last review: 2026-05-28
- 验收人: 老雷
- 状态: v1 提交, 待 Sprint-1 review

> 关联 ticket: S1-005
> 关联文档:
>   - S1-002 (Polymarket CLOB API 实测) — 老李, 决定 EIP-712 schema
>   - S1-009 (Polygon RPC 选型) — 老叶, 决定 onchain 调用模式
>   - S1-010 (跨洋部署方案) — 老吴, 决定 signer 物理位置
>   - S1-016 (安全威胁模型 v1) — 老沈, 输入红线
>   - 合规红线 (S1-006) — 老黄, 私钥合规要求

---

## 0. TL;DR (给老雷)

- **推荐**: **本地软件 signer (独立进程, mlock + 内存清零) + age-encrypted at-rest + 启动期 KMS unwrap**, 部署在美东 colocation 节点 (与 trader 同机房 / 同机).
- **备选**: HashiCorp Vault Transit (远程签名) — 仅当合规审计强制要求"私钥不出 KMS"时启用, 接受 +30~80ms 签名延迟代价.
- **拒绝**: AWS/GCP KMS 直接签名 — 不支持 secp256k1 + Keccak256 (EIP-712), 仅能存裸字节, 价值有限.
- **核心机制**:
  - signer 独立 unix domain socket 进程, 主进程 zero-knowledge
  - 季度强制轮换 + 双密钥 7 天重叠窗口
  - 单笔 >$2k / 日累计 >$20k 走人工审批队列
  - HA: 主备双 wallet (热-热), 灾备私钥分片 Shamir 3-of-5 异地保管
- **关键风险 (1 条)**: 跨洋部署模式下, 若 signer 与 trader 不同机房, 任何远程签名 (KMS/Vault) 都会让 us 级热路径破产, 必须强制 colocation, 这一点必须在 S1-010 跨洋方案中钉死.

---

## 1. 红线 + 约束

### 1.1 不可越过的红线 (违者权限暂停 / 解雇)

| # | 红线 | 检测手段 | Owner |
|---|---|---|---|
| R1 | 私钥永不明文落盘 (含 swap / core dump / tmp) | `mlock` + `setrlimit(RLIMIT_CORE,0)` + filesystem audit | 老孙 |
| R2 | 私钥永不出现在任何日志 (含 stack trace / panic) | 日志 grep CI + signer 强制 redact wrapper | 老孙 + 小郑 |
| R3 | 私钥永不通过环境变量传递 (除 dev/staging 一次性 bootstrap) | 生产启动脚本 grep + 进程 env audit | 老吴 |
| R4 | 私钥永不出现在 git 历史 / docker layer / build artifact | pre-commit hook + trivy scan + image layer scan | 老沈 |
| R5 | 签名操作必须可审计 (who/when/what/amount) | signer 审计日志 append-only + WAL | 老孙 + 老唐 |
| R6 | 生产私钥与 dev/staging 私钥物理隔离, 永不混用 | 部署 checklist + key tag prefix | 老吴 |
| R7 | 私钥持有人 >= 2 (双人解封), 单人不可独自恢复主密钥 | Shamir 3-of-5 + 多签人员名单 | 老沈 + 老雷 |

### 1.2 业务约束

- **签名频率**: 高峰期 100~500 ops/sec (cancel/replace 风暴), 平均 10~50 ops/sec
- **签名延迟预算**: 端到端订单延迟 < 200ms (跨洋), 签名本身必须 < 1ms p99
- **跨洋部署**: trader 节点在美东 (与 Polymarket 同区), 中国端只跑 BI / monitor
- **EIP-712**: Polymarket CLOB Order 结构 + Polygon chainId=137, 每笔订单一次签名
- **合规**: 私钥所属 wallet 需 KYC, 不可与非交易资金混用 (老黄红线)

---

## 2. 选型对比表

### 2.1 候选方案 (8 个, 覆盖云 / 硬件 / 软件 三大类)

| # | 方案 | 类型 | secp256k1 原生? | Keccak256? | 跨洋友好 | 成本/月 | 签名延迟 | 灾备 |
|---|---|---|---|---|---|---|---|---|
| A | AWS KMS (asymmetric) | 云 SaaS | 是 (ECDSA_SECP_K1) | 否 (要本地 hash) | 差 (us-east-1 到亚太 ~180ms) | $1/key + $0.03/10k req | 30~100ms RTT | KMS 多 region 复制 |
| B | GCP KMS | 云 SaaS | 是 (EC_SIGN_SECP256K1_SHA256) | 否 (强制 SHA256, 不能换 Keccak) | 差 | $0.06/月/key + $0.03/10k req | 30~80ms RTT | multi-region key |
| C | Azure Key Vault Managed HSM | 云 SaaS | 是 (P-256K) | 否 (要本地 hash) | 中 (美东节点近) | $3.5/HSM 实例/小时 (~$2500/月) | 20~60ms | geo-replication |
| D | HashiCorp Vault Transit (自部署) | 自托管 SaaS | 是 (ecdsa-p256k1) | 否 (但可自定义 hash) | 好 (与 trader 同机房) | $0 (OSS) 或 $1.5k+ (Enterprise) | 1~5ms (同 VPC) | Raft cluster + snapshot |
| E | YubiHSM 2 / Ledger Nano S | 硬件 | 是 | 否 | 中 (要物理插在节点) | $650~$50 一次性 | 50~200ms USB 慢 | 备机预置 + Shamir |
| F | AWS CloudHSM | 云 HSM | 是 | 否 | 中 (仅美区) | ~$1500/月/HSM | 5~15ms (同 VPC) | cluster + backup |
| G | **本地 software signer + age-encrypted at-rest** | 软件 | 是 (libsecp256k1) | 是 (本地全栈) | 优 (同进程/同机) | $0 | **30~80μs** | KMS unwrap + Shamir 离线备份 |
| H | TEE (AWS Nitro Enclave / Intel SGX) | 硬件隔离 | 是 (本地) | 是 (本地) | 中 (依赖云) | ~$200/月 | 100~500μs | enclave attestation + KMS sealed |

### 2.2 维度详细评估

#### 签名延迟 (最关键, 跨洋部署放大效应)

| 方案 | 延迟来源 | 实测/估算 p99 | 备注 |
|---|---|---|---|
| 本地 signer (G) | libsecp256k1 + Keccak | **50μs** | reference: ethers-rs benchmark, M1 mac |
| TEE (H) | enclave 进出 + 本地计算 | 300μs | Nitro enclave vsock 开销 |
| Vault Transit 同 VPC (D) | TCP RTT + HTTP + Vault internal | 2ms | 取决于 Vault 节点位置 |
| CloudHSM 同 VPC (F) | PKCS#11 + 网络 | 10ms | AWS 官方数据 |
| 云 KMS 同 region (A/B) | HTTPS + KMS 内部 + 网络 | 30~80ms | AWS KMS p99 实测 |
| 云 KMS 跨洋 (A/B) | 跨洋 RTT + KMS | 200~400ms | 不可接受 |

> 在 100 ops/sec 突发场景, 远程方案会变成队列瓶颈; 本地 signer 单核可撑 ~20k ops/sec.

#### 跨洋部署友好度

- **优 (本地 / Vault 同机房)**: signer 跟 trader 在美东 colocation, 物理距离 <1ms
- **中 (AWS CloudHSM, 同 region)**: 必须 trader 也在 AWS us-east, 锁定厂商
- **差 (云 KMS 跨 region / Azure)**: 任何跨洋调用都不可接受

#### 灾难恢复 (DR) 维度

- **A/B/C 云 KMS**: 厂商负责 region 复制, 但厂商账号丢失 = 私钥永久丢失 (无法导出)
- **D Vault**: 自己掌控 unseal key + Raft snapshot, 但运维负担重
- **E YubiHSM**: 物理设备丢失 = 私钥丢失, 必须有备机 + Shamir 离线种子
- **G 本地 + age**: 加密文件可任意备份, Shamir 3-of-5 异地保管, **可控性最高**
- **H TEE**: enclave attestation 失效 = 重新 sealed, 依赖 KMS unwrap key

#### 合规友好度 (老黄输入)

| 方案 | 审计就绪 | 不可导出私钥保证 | KYC wallet 兼容 |
|---|---|---|---|
| AWS KMS | SOC2/PCI/HIPAA | 是 (no export by default) | 是 |
| GCP KMS | 同上 | 是 | 是 |
| Vault Enterprise | SOC2 | 是 (transit no export) | 是 |
| 本地 signer | 自审计 | 否 (文件可拷) — 靠 OPS 流程 | 是 |
| YubiHSM | FIPS 140-2 L3 | 是 (硬件 sealed) | 是 |

---

## 3. 推荐方案 + 理由

### 3.1 主方案: **本地 software signer (G) + KMS unwrap bootstrap**

```
启动流程:
  1. signer 进程启动, 通过 IAM role 拉取 KMS-encrypted age key
  2. KMS unwrap age key (一次性, 启动期, 走 us-east-1 同 region < 30ms)
  3. age 解密 private_key.age 文件 → 内存
  4. mlock + setrlimit(RLIMIT_CORE,0) + memset 原 buffer
  5. unix domain socket listen, 等待 trader 进程 IPC

运行流程:
  1. trader 通过 UDS 发 EIP-712 typed data hash
  2. signer 用 libsecp256k1 签名 (50μs)
  3. 返回 (r, s, v), trader 拼装 order
  4. signer 写审计 WAL (append-only, fsync)
```

### 3.2 为什么不直接用 AWS/GCP KMS

1. **GCP KMS 强制 SHA256**, EIP-712 必须 Keccak256 → 不能用 (硬性技术约束)
2. **AWS KMS 支持 secp256k1 raw sign**, 但每次签名 30~80ms p99, 100 ops/sec 突发会触发 throttle (KMS quota 默认 5000 req/sec, 但单调用延迟不变)
3. **跨洋部署放大效应**: trader 必须在美东; signer 进 KMS 走 us-east 同 region 还行, 但若 monitor/ops 需要在中国端做应急签名 (不应该, 但运维场景会出现), 立刻破产
4. **成本**: 100k 笔/月只要 $0.3, 不是成本驱动决策; 决策驱动是**延迟 + 控制力**

### 3.3 为什么不用 HashiCorp Vault Transit (主)

- 备选不主选, 因为:
  - Vault Transit 同 VPC 签名 1~5ms, 仍然比本地 signer 慢 100x
  - Vault 集群运维负担大 (Raft + unseal + 监控), Sprint-1 阶段不值得
  - Vault Enterprise 才有完整审计 / namespace, OSS 版功能有限
- 但**保留为合规升级路径**: 若 Sprint-N 合规审计要求"私钥不出 KMS", 切换到 Vault Transit, 改动只在 signer 进程内部

### 3.4 为什么不用 YubiHSM / CloudHSM (硬件)

- YubiHSM USB 总线延迟 50~200ms, 不适合高频
- CloudHSM 锁 AWS 且 $1500/月起步, MVP 阶段成本不合理
- 硬件方案保留为**冷钱包 / Shamir 备份介质**用途, 不参与热路径签名

### 3.5 备选 (Plan B): Vault Transit (D)

触发条件 (任一即切):
- 合规审计 (老黄) 给出"私钥必须不可导出"硬要求
- 安全事件: signer 进程被怀疑泄露
- 团队规模扩大, 需要多 wallet 隔离 + namespace 管理

---

## 4. 签名延迟实测预估

### 4.1 本地 signer (推荐方案)

| 阶段 | 延迟 | 备注 |
|---|---|---|
| trader → signer UDS send | 5μs | unix domain socket |
| signer 解析 EIP-712 hash | 2μs | 已 typed data 序列化 |
| Keccak256 (32B) | 1μs | sha3 / xkcp |
| secp256k1 sign | 40μs | libsecp256k1, M1 ~30μs, x86 ~50μs |
| 写审计 WAL (异步) | 0μs 热路径 | fsync 不在路径上 |
| signer → trader 回 (r,s,v) | 5μs | |
| **总 p50** | **~55μs** | |
| **总 p99** | **~150μs** | mlock 抖动 |

### 4.2 Vault Transit 同 VPC

| 阶段 | 延迟 |
|---|---|
| trader → Vault HTTPS | 0.5ms RTT |
| Vault internal (cache + sign) | 1~3ms |
| 返回 | 0.5ms |
| **总 p99** | **3~5ms** |

### 4.3 AWS KMS asymmetric sign

| 阶段 | 延迟 |
|---|---|
| trader → KMS endpoint | 1ms (同 region) |
| KMS internal | 30~80ms p99 |
| 返回 | 1ms |
| **总 p99** | **30~80ms** |

> 结论: 本地 signer 比 KMS 快 ~500 倍, 比 Vault 快 ~30 倍. 跨洋部署下任何远程方案都不可接受.

### 4.4 实测 todo (Sprint-2 验证)

- [ ] benchmark: ethers-rs vs libsecp256k1 vs noble-curves wasm 三选一
- [ ] mlock 后 p99 抖动测量 (是否需要 RT 调度)
- [ ] 100 ops/sec 持续 1h 内存不增长测试
- [ ] EIP-712 typed data hash 与 Polymarket SDK byte-equal 对比 (老李协作)

---

## 5. 密钥轮换 SOP

### 5.1 季度强制轮换 (每 90 天)

| 阶段 | T+0 (轮换日) | T+1 ~ T+7 (双密钥窗口) | T+8 |
|---|---|---|---|
| 生成 | 新 wallet 离线生成 (air-gap 笔记本 + libsecp256k1 keygen) | | |
| 注资 | Polymarket funder address 转入 | 旧 wallet 余额逐步迁移 | |
| 部署 | 新私钥 age-encrypted + 推送到 signer | signer 同时持有新旧, 新单走新 wallet | |
| 验证 | 测试单 (最小金额) byte-equal 验证 | 监控旧 wallet 残余持仓平仓 | |
| 失效 | | T+7 旧 wallet 余额清零 | 旧 age 文件销毁 + 公开备份归档 |

### 5.2 紧急轮换 (怀疑泄露)

- T+0 立刻: signer 拒签所有新单, trader 进入 cancel-only 模式
- T+0~1h: 老沈 + 老雷 + 老韩 三人会签启动应急
- T+1h~6h: 平掉旧 wallet 所有持仓
- T+6h~24h: 部署新 wallet, 走完 5.1 流程
- T+24h: 复盘 + 事件报告 (老唐审计)

### 5.3 轮换 checklist

```
[ ] 新私钥离线生成, 双人见证 (老孙 + 老沈)
[ ] 新 wallet 地址 KYC (Polymarket funder 关联)
[ ] age 加密文件生成, 备份到 3 个介质 (热盘 + 冷盘 + Shamir)
[ ] KMS 包装 age key 上传 (AWS KMS encrypt)
[ ] signer 配置双密钥 (key_id_v1 + key_id_v2)
[ ] 测试单签名 byte-equal 验证 (老李 ref impl 对比)
[ ] 监控旧 wallet 余额, 平仓完成后销毁
[ ] 审计日志记录 (老唐)
[ ] 合规备案 (老黄)
```

---

## 6. C++ 集成方式

### 6.1 进程模型

```
┌──────────────┐                ┌─────────────────┐
│  trader      │  UDS msgpack   │  signer         │
│  (C++)       │ ───────────────│  (Rust binary)  │
│              │  EIP-712 hash  │                 │
│  老周/小马   │  ◄─── (r,s,v)  │  老孙           │
└──────────────┘                └─────────────────┘
                                         │
                                         │ mlock
                                  ┌──────▼─────┐
                                  │ key (mem)  │
                                  └────────────┘
                                         │ at-rest
                                  ┌──────▼─────┐
                                  │ age file   │
                                  │ + KMS wrap │
                                  └────────────┘
```

### 6.2 为什么 signer 是 Rust 而非 C++ (与老张 rust-advisor 对齐)

- ethers-rs / alloy-rs 是事实标准, EIP-712 实现成熟, 比 C++ 手搓风险低
- libsecp256k1 C 库 Rust binding 更稳定
- 内存安全, signer 进程攻击面小
- C++ trader 通过 IPC 调用, 不引入 Rust 到主进程

### 6.3 IPC 协议 (与老李 + 小赵 对齐)

```
// 老孙定的最小 schema, 二进制 msgpack
SignRequest {
  request_id: u64,
  chain_id: u64,         // 137 = polygon
  domain_separator: [u8; 32],
  message_hash: [u8; 32], // keccak256(typed_data) 已在 trader 端算好
  amount_usdc: u64,       // 用于审批阈值判断 (单位: 1e6)
  intent: enum { Order, Cancel, Approve, EOA_Tx },
  timestamp_ns: u64,
}

SignResponse {
  request_id: u64,
  status: enum { Ok, AmountExceeded, RateLimit, Rejected },
  r: [u8; 32],
  s: [u8; 32],
  v: u8,
  signer_pubkey: [u8; 20], // 给 trader 校验
}
```

### 6.4 与老周 / 小马 对接点

- trader 通过 thin client `Signer*` interface, 隐藏 IPC 细节
- 非热路径 (启动 / shutdown / 轮换) 用阻塞调用
- 热路径 (高频签名) 用 io_uring + UDS, 单笔 5μs IPC 开销
- mock signer 给 testbed 用 (小宋 test-replay 协作)

### 6.5 byte-equal 测试 (老孙交付)

```
test_eip712_byte_equal:
  1. 加载 Polymarket SDK (TypeScript) 生成的 reference signature
  2. 用 signer 对同样 typed data 签名
  3. 比对 r/s/v 字节完全相等
  4. CI 跑 100+ 测试向量 (覆盖 Order/Cancel/Approve)
```

测试向量从 Polymarket testnet 取, 老李 S1-002 输出.

---

## 7. 签名审批阈值

### 7.1 阈值机制 (与老韩 RiskManager + 老雷 商定)

| 类型 | 阈值 | 处理 | SLA |
|---|---|---|---|
| 单笔 < $500 | 自动签 | 无人工 | < 1ms |
| 单笔 $500 ~ $2000 | 自动签 + Slack 通知 | 异步通知 | < 1ms |
| 单笔 $2000 ~ $5000 | 进入审批队列 | 老韩/老雷 任一确认 | < 30s |
| 单笔 > $5000 | 进入审批队列 | 老韩 + 老雷 双签 | < 60s |
| 日累计 > $20k | 当日剩余进入审批 | 老韩 + 老雷 双签 | < 60s |
| Cancel | 自动签 (任何金额) | 仅记录 | < 1ms |
| Approve (USDC allowance) | 进入审批队列 | 老雷 双签 | < 5min |

### 7.2 审批队列实现

- signer 进程内置 pending_queue (UDS 通道 1) 给审批人
- web UI (小苏 frontend) 提供审批界面, 显示 typed data 摘要 (人类可读)
- 审批人手机端 Slack bot, 5s 内可点击确认 (HMAC 校验)
- 超时 (60s) 默认拒签, 触发 alert

### 7.3 阈值不可绕过 (硬性)

- 阈值参数烧在 signer 进程启动配置 (KMS-signed config), 运行时不可改
- 修改阈值必须 redeploy + 双人审批
- 老唐 audit-expert 季度审计阈值生效记录

---

## 8. 灾备方案

### 8.1 主密钥丢失场景

| 场景 | 概率 | 应对 |
|---|---|---|
| signer 进程崩溃, age 文件完好 | 高 | 重启 signer, 走 KMS unwrap |
| 主机硬盘损坏, age 文件丢失 | 中 | 切热备 wallet (5.2 紧急轮换) |
| KMS 账号锁定 (AWS 风控) | 低 | Shamir 3-of-5 离线恢复 |
| 物理机房失火 / 区域不可用 | 低 | 切跨 region 备 signer, 备 wallet 启用 |
| 团队人员变动, 持有人离职 | 中 | 离职前 90 天 Shamir 分片重分发 |

### 8.2 备份策略 (3-2-1 规则升级版)

```
3 份独立备份:
  - 热备份: signer 节点本地 age 文件 (运行时使用)
  - 温备份: S3 + KMS 加密 (跨 region, 仅紧急)
  - 冷备份: Shamir 3-of-5 离线分片

2 种介质:
  - 数字 (age 加密文件)
  - 物理 (Shamir 分片 → 金属种子板 / 纸质保险柜)

1 份异地:
  - 至少 1 份分片在不同物理城市 / 国家
```

### 8.3 Shamir 3-of-5 持有人 (示意, 实际由老雷 + 老沈 确定)

| 分片 | 持有人 | 保管方式 |
|---|---|---|
| 1 | 老雷 (CEO) | 私人保险柜 (城市 A) |
| 2 | 老沈 (security) | 公司保险柜 (城市 A) |
| 3 | 老孙 (crypto-signing) | 私人保险柜 (城市 B) |
| 4 | 法务 / 外部托管 | 第三方银行保险柜 (城市 C) |
| 5 | 离线冷钱包硬件 | 主办公地保险柜 |

恢复门槛: 任意 3 人到场 + 视频记录 + 老雷书面授权.

### 8.4 演练 (每半年一次)

- [ ] 模拟 signer 崩溃, 走标准重启流程, < 5min 恢复
- [ ] 模拟 KMS 锁定, 用温备份 + 备 KMS region 恢复, < 1h
- [ ] 模拟 Shamir 恢复, 召集 3 持有人, < 4h 重建主密钥
- [ ] 演练结果归档老唐 audit

---

## 9. 实盘部署 checklist

### 9.1 基础设施 (老吴 owns)

- [ ] signer 进程独立部署在 trader 同机房 (美东 colocation)
- [ ] signer 主机系统 hardening: 禁 swap, 禁 core dump, selinux enforcing
- [ ] UDS 文件权限 0600, 仅 trader user 可读
- [ ] systemd unit: `LimitCORE=0`, `LockPersonality=true`, `MemoryDenyWriteExecute=true`, `NoNewPrivileges=true`, `PrivateTmp=true`, `ProtectHome=true`, `ProtectSystem=strict`
- [ ] signer 主机不允许 SSH (仅 console 或 bastion)
- [ ] iptables: signer 进程仅出向 KMS endpoint, 拒绝所有其他出向

### 9.2 密钥生命周期 (老孙 owns)

- [ ] 生产私钥离线生成 (air-gap 笔记本), 双人见证
- [ ] age 加密, KMS 包装 age key
- [ ] Shamir 3-of-5 分片完成, 持有人签收
- [ ] 测试单 byte-equal 通过 (CI + 手动各 1 次)
- [ ] dev / staging / prod 三套独立 wallet, 永不混用

### 9.3 监控告警 (小郑 owns)

- [ ] signer 进程存活监控 (1s 粒度)
- [ ] 签名延迟 p50/p99 dashboard
- [ ] 签名失败率 alert (>1% 5min 内触发)
- [ ] 审批队列积压 alert (>5 笔触发)
- [ ] 异常签名模式检测 (突发金额 / 频率)
- [ ] mlock 失败 alert (内存换页 = 红线)

### 9.4 审计 (老唐 + 老黄)

- [ ] signer 审计 WAL 双备份 (热 + 冷)
- [ ] 季度签名总量对账 (链上 vs WAL)
- [ ] 阈值生效记录归档
- [ ] 合规备案 (老黄 法务 review)

### 9.5 应急 runbook (老吴 + 老孙 共同)

- [ ] signer 崩溃重启 SOP (写到 docs/INCIDENTS/runbook-signer.md)
- [ ] 紧急轮换 SOP
- [ ] Shamir 恢复 SOP
- [ ] 私钥泄露事件响应 SOP (含通报老黄 / 暂停交易)

### 9.6 验收 (老雷)

- [ ] 老沈 安全 review 通过
- [ ] 老黄 合规 review 通过
- [ ] 老唐 审计设计 review 通过
- [ ] 老吴 部署 dry-run 通过
- [ ] 老李 byte-equal 测试通过
- [ ] 老雷 final sign-off

---

## 10. 开放问题 (待 Sprint-2 解决)

| # | 问题 | 阻塞? | Owner | 求助 |
|---|---|---|---|---|
| Q1 | KMS 选 AWS 还是自建 Vault 做 age key wrap? | 否 (默认 AWS KMS) | 老孙 | @老吴 跨洋部署后再定 |
| Q2 | EIP-712 Polymarket 最新 schema 有无变化 (CTF Exchange v2)? | **是** | 老李 | @老李 S1-002 ASAP |
| Q3 | Polygon EIP-1559 fee 签名是否纳入 signer 还是 trader 算? | 否 | 老孙 + 老叶 | @老叶 S1-009 |
| Q4 | 多 wallet (主备热-热) 仓位分配策略? | 否 | 老韩 | @老韩 RiskManager 集成 |
| Q5 | signer 进程是否需要 Nitro Enclave 加固 (Plan C)? | 否 | 老孙 | Sprint-N 评估 |
| Q6 | Slack 审批 bot 与 Polymarket session token 关联机制? | 否 | 小苏 | frontend 设计阶段 |
| Q7 | 季度轮换是否影响 Polymarket API key (与私钥关联?) | **是** | 老李 | @老李 确认 |
| Q8 | Shamir 分片合规 (持有人涉及法务托管, 跨境是否受限)? | 否 | 老黄 | 合规 review |

---

## 附录 A. 核心库选型

| 用途 | 选型 | 备选 |
|---|---|---|
| secp256k1 签名 | libsecp256k1 (bitcoin-core, C) | k256 (Rust pure) |
| EIP-712 typed data | alloy-sol-types (Rust) | ethers-rs (deprecated) |
| Keccak256 | sha3 crate / xkcp | tiny-keccak |
| age 加密 | age crate (rage) | sops |
| IPC | unix domain socket + msgpack | gRPC unix |
| 内存 mlock | libc mlock + memlockall | secrecy crate |
| 审计 WAL | append-only file + fsync | sqlite WAL |

## 附录 B. 参考

- EIP-712: https://eips.ethereum.org/EIPS/eip-712
- Polymarket CLOB SDK: https://github.com/Polymarket/clob-client (待 S1-002 实测确认)
- AWS KMS secp256k1 doc: https://docs.aws.amazon.com/kms/latest/developerguide/asymmetric-key-specs.html
- HashiCorp Vault Transit: https://developer.hashicorp.com/vault/docs/secrets/transit
- libsecp256k1: https://github.com/bitcoin-core/secp256k1
- age: https://github.com/FiloSottile/age

---

*v1 提交时间: 2026-05-28*
*下次 review: Sprint-1 Retro (2026-06-12)*
