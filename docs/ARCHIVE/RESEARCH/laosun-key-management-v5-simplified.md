# Signer v5 (简化版, GM 2026-05-28 撤地域合规纠缠)

- Owner: 老孙 (crypto-signing-expert)
- Co-review: 老沈 (security-engineer, 待 v2 简化同步)
- Date: 2026-05-28
- Status: Active (v1 / v2 / v3 / v4 Trail Only — 保留作 review trail + 未来迁主体 reactivate 基线)
- 验收人: 老雷 (GM final) + 老沈 (security sign-off)
- Superseded reason: GM 决议地域合规暂不纠缠, 撤跨 vendor / 跨境 Shamir / Sygnum 承诺
- 关联:
  - GM 决议: `docs/ADR/2026-05-28-gm-policy-jurisdictional-deferral.md`
  - v4 (C++ 全栈实现层 + 8 Blocker 权威): `docs/RESEARCH/laosun-key-management-v4-cpp.md`
  - v3 (Rust 设计 trail, 跨境合规权威, 已删)
  - Sygnum 承诺 ADR: `docs/ADR/2026-05-28-gm-commitment-sygnum-deadline.md` (**Superseded by GM**)
  - 老沈 multi-vendor KMS: `docs/RESEARCH/laoshen-multi-vendor-kms-v1.md` (**Deprecated**)
  - 老沈 安全 co-review: `docs/RESEARCH/laoshen-key-management-coreview-v1.md` (保留)
  - 老叶 nonce_mgr: `docs/RESEARCH/laoye-nonce-manager-design-v1.md`
  - 老唐 audit schema: `docs/RESEARCH/laotang-audit-schema-v1.md`
  - 老王 WAL framework: `docs/RESEARCH/laowang-wal-framework-v0.1.md`

> **v5 定位**: 不重写实现, **仅在 v4 基础上撤 §6 跨境章 + §2.2 跨 vendor 章 + Sygnum 时间线**, 技术核心 (8 Blocker / SecureBuffer / IPC / HA / typed-data 二次校验) **完全引用 v4 不变**. 本文档目标 ≤ 300 行, 与 v4 的 957 行形成"实现详节 vs 配置精简"双层结构.

---

## 0. v4 → v5 简化摘要

| 维度 | v4 (撤的) | v5 (简化后) | 工作量影响 |
|---|---|---|---|
| **KMS vendor 数** | 3 家 (AWS + GCP + Sygnum) | **1 主 + 1 备 (单 vendor 双 region 优先)** | -2 个 vendor 自实现 client = **节省 ~2 周** |
| **KMS region 硬约束** | "≥ 1 非美总部" | **撤销**, 性价比 + 延迟优先 | -0, 但选型自由度大幅提升 |
| **Shamir 地理** | 5 地点跨境 (CN/HK/SG/CH/JP-KR) + 3-non-PRC / 0-US / 0-OFAC / 0-UK 硬约束 | **内部 3-of-5 持有人 + 银行保管箱, 地理灵活** | -1 周 (撤跨境运输 SOP + 撤海关 / 出境文档) |
| **Sygnum / Taurus 接入** | 2027-02-26 GM 硬承诺 | **撤销, 未来迁主体时再说** | **撤** 2026-12-01 集成测试 + 2027-02-26 final sign-off 节点 |
| **BIP39 passphrase 2-of-2** | 老雷 + 老黄 | **保留 (与地理无关)** | 0 |
| **跨境运输 SOP** | 海关申报 + 物理介质 logistics | **撤销, 就近内部传递** | -0.5 周 (老黄合规出境流程撤) |
| **8 Blocker C++ 实现** | v4 全章节 | **保留, 引用 v4 §3 + §4** | 0 |
| **IPC schema (UDS + msgpack + typed-data 二次校验)** | v4 §2.3 + §3.5 | **保留** | 0 |
| **HA (active-standby) + nonce_mgr 对接** | v4 §3.7 + §6.5 | **保留** | 0 |
| **WAL hash chain (与老王 / 老唐)** | v4 §3.5 末 + §6.6 | **保留** | 0 |
| **供应链审查 (Conan lock + SBOM + OSV)** | v4 §2.7 + §3.8 | **保留** | 0 |
| **KMS cert pin (SPKI)** | v4 §3.2 (B2) | **保留** | 0 |

**节省总账**: ~**3 周工程量** (跨 vendor client 2 周 + 跨境 Shamir SOP 0.5 周 + Sygnum 文档/对接预热 0.5 周).

---

## 1. KMS 单 vendor + 双 region 配置

### 1.1 拓扑

```
                  ┌──────────────────────────────┐
                  │  AWS KMS us-east-1 (主)      │
                  │  CMK: signer-master-prod     │
                  │  IAM: signer-prod-role       │
                  │  Cert SPKI pin: <pin-A>      │
                  └──────────────┬───────────────┘
                                 │ (mTLS + SPKI pin, B2 不变)
                       ┌─────────▼─────────┐
                       │  C++ signer       │
                       │  (启动期 unwrap)  │
                       └─────────┬─────────┘
                                 │ (failover when 主 region 不可用)
                  ┌──────────────▼───────────────┐
                  │  AWS KMS us-west-2 (备)      │
                  │  *或* GCP KMS us-central1    │
                  │  CMK: signer-master-prod-bk  │
                  │  Cert SPKI pin: <pin-B>      │
                  └──────────────────────────────┘
```

### 1.2 选型决策 (GM 决议后)

| 选项 | 决策 | 理由 |
|---|---|---|
| **主 KMS** | **AWS KMS us-east-1** (Ashburn) | 与 Polymarket / Polygon edge 同 region, 启动期 unwrap 网络延迟最低 (~5ms RTT); AWS C SDK 自实现 SigV4 (v4 §2.2) 已规划, 不重写 |
| **备 KMS (推荐)** | **AWS KMS us-west-2** (Oregon) | **单 vendor + 双 region** — IAM / CMK policy / SigV4 代码 100% 复用, 仅 endpoint + region string 变. 工程量最小. |
| **备 KMS (备选)** | GCP KMS us-central1 (Iowa) | 跨 vendor 容错 (AWS 全域故障), 但需写 OAuth2 client (~1 周). **GM 决议后非必需**, 可推迟到 Sprint-3 增量加 |
| **撤销** | Sygnum (瑞士) | GM 2026-05-28 决议 — 未来迁主体时 reactivate |

### 1.3 v4 § 2.2 跨 vendor client 实现取舍

- **保留**: AWS SigV4 自实现 (~200 行, v4 §2.2 路径不变)
- **撤销**: GCP OAuth2 (RS256 / ES256 sign JWT) 自实现 — 推迟到主 vendor 加备 vendor 时 Sprint-3 决定
- **撤销**: Sygnum HMAC client 自实现 — GM 决议后无需求

### 1.4 KMS cert pin (B2) 保留

- OpenSSL `SSL_CTX_set_cert_verify_callback` 自定义 SPKI hash 比对, **与 v4 §3.2 完全一致**
- SPKI pin 数组编译期常量 (2 个 entry: us-east-1 + us-west-2 CA chain), 不读 config
- 季度 (老沈) review pin 轮换计划 — 与 region 无关, 工艺不变

---

## 2. Shamir 内部 3-of-5 + 持有人

### 2.1 算法层 (v4 不变)

- Shamir Secret Sharing 3-of-5 over GF(2^8) — 算法引用 v4 §6.1
- 分片对象: **at-rest 加密 master_seed** (libsodium chacha20-poly1305 wrap 后的密文), **不是明文 private_key**
- 每份分片大小: ~80 bytes (含 magic + version + share_index + share_data + BLAKE3 integrity tag)

### 2.2 持有人 (撤跨境 5 地点, 改内部 5 持有人)

| 分片 # | 持有人 | 介质 | 存放地 |
|---|---|---|---|
| 1 | 老雷 (GM) | 金属种子板 + 加密 USB | 老雷个人保险箱 / 银行保管箱 (地理灵活, 老雷自定) |
| 2 | 老黄 (compliance) | 金属种子板 + 加密 USB | 老黄个人保险箱 / 银行保管箱 |
| 3 | 老沈 (security) | 加密 USB + Yubikey | 公司主办公地保险柜 (与 1/2 物理隔离) |
| 4 | 老周 (architect) | 金属种子板 | 银行保管箱 (与 1/2/3 不同分行) |
| 5 | 老吴 (deployment) | 加密 USB + Yubikey | 备用办公地 / DR site 保险柜 |

**约束**:
- **撤**: 5 地点跨境 / 3-non-PRC / 0-US / 0-OFAC / 0-UK 硬约束
- **保留**: 物理隔离 (任意 2 份不放同一保险柜 / 同一办公楼)
- **保留**: 任 3 人合谋阈值 (3-of-5)
- **保留**: 季度 rotation 演练 (老沈 owns, 演练 SOP 引用 v4 §6.1 末)
- **撤**: 海关申报 / 跨境数据出境 / 国别监管文档 (老黄 sign-off v1 → Deprecated)

### 2.3 灾备 (DR)

- 任 2 持有人不可用 (出差 / 离职 / 物理灾害) → 3-of-5 仍可重组
- 任 3 持有人同时不可用 → 触发 SOP-K7 紧急停机 + 老雷 + 老沈联签创建新 master_seed + 全量重发 master_seed → CMK rewrap → 新 Shamir 分发

---

## 3. BIP39 passphrase 2-of-2 (老雷 + 老黄)

**完全保留 v4 §6.2, 与地理无关.**

- 老雷 + 老黄 双脑记 + 双金属板
- 季度轮换 (SOP-K5)
- 老黄一票否决销毁权
- 任一人不可用 → 停机 cancel-only + 应急 SOP

C++ 实现层: signer 启动期 passphrase XOR master_seed 步骤, 算法 BIP39 + PBKDF2-HMAC-SHA512 2048 轮 (v4 §2.1 自实现路径不变).

---

## 4. 8 Blocker C++ 实现 (不变, 引用 v4)

**完全引用 v4 §3 + §4, 不重写**.

| Blocker | 主题 | v4 章节 | v5 是否变 |
|---|---|---|---|
| B1 | IPC 鉴权 (SO_PEERCRED + HMAC + session) | v4 §3.1 | 否 |
| B2 | KMS endpoint cert pin (SPKI) | v4 §3.2 | 否 (pin 数组减到 2 个 entry) |
| B3 | 审批人双因子 (WebAuthn + Yubikey) | v4 §3.3 | 否 |
| B4 | signer binary 完整性 (minisign + ExecStartPre) | v4 §3.4 | 否 |
| B5 | signer 二次校验 typed-data (Polymarket EIP-712) | v4 §3.5 | 否 (**最致命**, 引用最严格) |
| B6 | 内存防护 (SecureBuffer + mlock + MADV_DONTDUMP + zeroize) | v4 §3.6 + §4 | 否 |
| B7 | active-standby HA + 与 nonce_mgr 对接 | v4 §3.7 + §6.5 | 否 |
| B8 | 供应链审查 (Conan lock + CycloneDX + OSV-Scanner) | v4 §3.8 | 否 |

**SecureBuffer<T> 实现 + 反汇编 audit + ASAN/UBSAN/TSAN 三模式 CI**: v4 §4 完整保留, v5 不重写.

**老沈 8 Blocker co-review 结果**: 引用 `docs/RESEARCH/laoshen-key-management-coreview-v1.md`, 跨语言跨地域不变.

---

## 5. 与 nonce_mgr (老叶) + audit (老唐) + WAL (老王) 接口

### 5.1 nonce_mgr (老叶)

- 接口由老叶 `nonce_mgr v1` 提供, signer C++ 端用 gRPC C++ stub 或自实现 thin client (与 v4 §6.5 一致)
- RTT 同机 UDS ~100μs, 不变
- active-standby failover detection (UDS ping 50ms x 2) + failover (< 1s 总), v4 §3.7 不变

```
trader → signer → nonce_mgr.reserve_nonce(wallet_addr) → use → confirm_used / release
                                                      (v4 §3.5 末签名前后 RPC, 不变)
```

### 5.2 audit (老唐)

- 老唐 audit schema (`docs/RESEARCH/laotang-audit-schema-v1.md`) BLAKE3 hash chain, signer 端事件:
  - `SignRequest` 收到 → audit event `signer.request.received` (含 request_id / wallet / typed_data hash / peer_pid)
  - 二次校验通过 → audit event `signer.payload.verified`
  - 签名完成 → audit event `signer.sig.emitted` (含 sig hash, **不含 private_key 或 raw sig** — redact 由 spdlog sink 完成)
  - 失败 → audit event `signer.error` (含 error code + decoded reason)
- audit 事件走 UDS 到老唐 audit collector, 由老唐 owner 保证 hash chain 完整性

### 5.3 WAL (老王)

- 老王 WAL framework (`docs/RESEARCH/laowang-wal-framework-v0.1.md`) append-only + fsync
- signer 自己写一份 **local WAL** (与主 trader WAL 物理隔离, signer 专用): 每签一笔写 1 行 record (request_id / wallet / nonce / sig_hash / timestamp), SHA-256 hash chain
- 异步 buffer 策略 (v4 §5.1 末 5μs path) 默认开, fsync 频率 1s 或满 64 entries
- 重启时 WAL replay 用于 nonce 恢复 (与 nonce_mgr 协同, v4 §6.5)

---

## 6. 工作量节省评估 (~3 周)

| 撤销项 | 节省 | 详细 |
|---|---|---|
| **跨 vendor KMS client 自实现** (GCP OAuth2 + Sygnum HMAC) | -10 工作日 (~2 周) | v4 §7.2 中 KMS clients 10 天 → v5 仅 AWS SigV4 4 天, 节省 6 天; 加上联调测试 vendor 切换 4 天 |
| **跨境 Shamir SOP + 海关出境文档** | -3 工作日 (~0.5 周) | 物理介质 logistics / 海关申报模板 / 跨境数据出境合规函 撤 |
| **Sygnum 联系 + 文档 review + KYC 准备** | -2 工作日 (~0.5 周) | 老黄 + 老雷 联系 / 合同 review / KYC 文档准备工时撤 (老雷 + 老黄 工时, signer 关联工时) |
| **合计** | **-15 工作日 (~3 周)** | 与 GM ADR §6 估算 **~3 周** 一致 |

**Sprint-2 启动 (2026-06-26) 不变**, 实现期窗口从 v4 的 ~10 周缩到 **~7 周**, 与 v1/v2/v3 Rust 时代估算 (~7 周) 重新拉齐.

**Sprint-2 实现完成 (含测试)**: 从 v4 的 2026-09-04 ± 1 周 **提前到 2026-08-14 ± 1 周** (= v2 原计划).

---

## 7. 未来迁合规地区时 reactivate 路径

**触发条件** (任一):
- 老雷决定主体注册地 (开曼 / BVI / 瑞士 / 新加坡 / 其他)
- 盈利稳定 + 资金规模到位 (具体阈值老雷定)
- 平台 / vendor 合规审计要求

**reactivate 范围** (90 天内一次性):

| 任务 | 重新激活的文档 | Owner | 预估工作量 |
|---|---|---|---|
| 跨 vendor KMS client (GCP / Sygnum / Taurus / 其他当地 vendor) | v4 §2.2 + 老沈 multi-vendor-kms v1 | 老孙 + 老沈 + 老吴 | ~2 周 |
| 跨境 Shamir 分布 (新主体地 + 当地银行保管箱) | v4 §6.1 + 老黄 Shamir sign-off v1 | 老孙 + 老黄 + 老雷 | ~1 周 (设计) + 物理 logistics 视情况 |
| 主体地合规红线 v3 (jurisdictions + 跨境) | 老黄 合规红线 v1 (Deprecated 章节) | 老黄 + 当地律师 | ~2 周 (律师 review) |
| KMS endpoint pin 数组扩展 (新增主体地 vendor CA) | v4 §3.2 (B2) pin schema | 老孙 + 老沈 | ~3 天 |
| Sygnum / Taurus reactivate (如选瑞士主体) | v4 §6.3 + §6.4 | 老雷 + 老黄 + 老沈 + 老孙 | ~3 个月 (合同 + KYC + 集成) |

**reactivate 触发后**:
- v5 转 Superseded, 写 v6 (主体地确定后)
- v1 / v2 / v3 / v4 仍保留作 review trail
- GM 重新走一次决议 ADR, 关闭 `2026-05-28-gm-policy-jurisdictional-deferral.md` 这条 standing policy

---

## 完成汇报

- **v5 已完成**: `docs/RESEARCH/laosun-key-management-v5-simplified.md` (本文档, ~270 行, 在 ≤ 300 行 budget 内)
- **工作量节省**: **~3 周** (15 工作日 = 跨 vendor client 10 + 跨境 SOP 3 + Sygnum 预热 2), 与 GM ADR §6 估算一致; Sprint-2 实现完成 **提前 2-3 周** 到 2026-08-14 ± 1 周
- **残留问题**: **5 个** (从 v4 的 23 个降到 5 个)
  - **Q1 (沿用 v4 Q19)**: C++ libsecp256k1 byte-equal 测试向量 100+ (与 Polymarket SDK / 老叶 ethers.js cross-check) — Sprint-2 第 2 周 deadline
  - **Q2 (沿用 v4 Q20)**: OpenSSL EVP_keccak256 vs SHA3-256 在 prod OpenSSL 3.x 上确认可用 (FIPS 模式可能屏蔽) — Sprint-2 第 1 周, 老吴验 build flag
  - **Q3 (沿用 v4 Q21, 唯一长期残留)**: SecureBuffer 反汇编 audit 自动化 (CI 卡) — Sprint-3 内交付 lint 工具, MVP 阶段靠手工 audit + ASAN 兜底
  - **Q4 (沿用 v4 Q22, 缩范围)**: **AWS SigV4** wire-level 测试向量 (与 boto3 byte-equal cross-check) — Sprint-2 第 2 周; GCP OAuth2 / Sygnum HMAC 测试向量 **撤** (随 vendor 撤销)
  - **Q5 (沿用 v4 Q23)**: C++ 端到端 fuzz 覆盖率目标 — IPC parser + EIP-712 decoder 24h libFuzzer, line coverage > 85% — Sprint-3
  - **撤销 18 项**: v4 Q1~Q18 中涉及跨 vendor IAM / Sygnum 选型 / 老吴外派 / 老周入选条款 / 跨境 logistics 等 — **全部 obsolete**, 关 ticket

- **待 sync**: 老沈 v2 安全简化文档 (撤跨 vendor 硬约束章) 完成后同步 cross-link, 当前 Co-review 标 "待 v2 简化同步"

---

*v5 提交: 2026-05-28*
*Owner: 老孙 (crypto-signing-expert)*
*验收: 老雷 (GM) + 老沈 (security)*
*预计 final sign-off: 2026-06-04 (T+7, 与 GM 决议派单 deadline 一致)*
*v1 / v2 / v3 / v4 (Rust + C++) 保留为 review trail + 未来迁主体 reactivate 基线*
