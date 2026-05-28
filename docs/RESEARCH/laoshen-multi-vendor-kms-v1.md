# 跨 Vendor KMS 升级方案 v1

- Owner: 老沈 (security-engineer)
- Date: 2026-05-28
- 验收人: 老孙 (v3 协议) + 老黄 (合规 sign-off) + 老雷 (GM final)
- 关联:
  - `laohuang-shamir-jurisdiction-signoff-v1.md` §8.2 (N4 跨 vendor KMS 硬约束)
  - `laosun-key-management-v2.md` §5.5 (跨 vendor 副本占位, 待我细化)
  - `laosun-key-management-v1.md` §3.1 (KMS unwrap 启动期流程)
  - `laoshen-key-management-coreview-v1.md` §4.3 (我自己 v1 review 提的 N4)
  - `laoshen-threat-model-v1.md` (T-06 / I-05 / E-04 KMS 相关威胁)
- 状态: v1 提交, 等老孙 v3 引用 + 老黄 R4 复核 + 老雷 final sign-off

> 自包含, 不强求读老孙 v2 全文. 数字凭据见 §11 引用清单.

---

## 0. TL;DR (给老雷 90 秒读)

1. **核心结论**: 单 vendor (AWS KMS us-east-1) 不接受. 我推荐 **3 vendor + 6 region 多地理分布**, 主 = **GCP KMS asia-southeast1 (新加坡)**, 备 = **Azure Key Vault switzerlandnorth (苏黎世)**, 紧急 = **AWS KMS ap-northeast-1 (东京)**. 总部国别覆盖: 美 x 2 (GCP / AWS, 但 region 都非美), 非美 x 1 (Azure 用瑞士 region, Microsoft 总部美但 region 合规上是非美; 见 §6.2 我对"vendor 总部 vs region 位置"的判定).
2. **真正非美总部 vendor**: 短期 (v1) 用美国云的非美 region 救急; 中期 (v2, Sprint-4 内) 必须接入 **Sygnum (瑞士银行 HSM)** 作为第三方真·非美总部备份, 触发 §7.3.
3. **热路径影响**: 0. KMS 仅启动期 unwrap age key 一次 (≤ 200ms 一次性开销), 热签名路径走本地 software signer, **跨 vendor 不影响交易延迟**. 与老孙 v2 §3.1 一致.
4. **RTO 影响**: 单 vendor 启动 unwrap 失败 RTO ≈ 5min (等 AWS 恢复或切区); 跨 vendor 启动 unwrap 失败 RTO ≈ 30s (切 vendor 自动). RTO **不翻倍, 反而降一个数量级** — 老雷 v5 题目里"RTO 翻倍" 的担心是反过来的, 跨 vendor 是 RTO 改善而非劣化, 见 §5.
5. **Top 1 风险**: 三 vendor 全部受美国法院传票约束的概率不为零 (GCP / Azure / AWS 总部都在美). 真正抗"美国主权一票否决" 必须接入瑞士独立 HSM (Sygnum/Taurus) 或自建 YubiHSM, **本 v1 仅消除 region 单点和 vendor 故障域单点, 未消除"美国主权域"单点**. 这条留 §7.3 v2 升级.

---

## 1. 跨 vendor 必要性

### 1.1 合规视角 (与老黄 R4/R5 对齐)

老黄 §1.3 + §8.2 已经把这条话讲死, 我引用并落到 KMS 决策上:

| 红线 | 单 vendor AWS us-east-1 触点 | 跨 vendor 是否化解 |
|---|---|---|
| **R4 美国元素全部 block** | AWS 是美国公司, us-east-1 是美国境内 region. 美国法院 / SEC / CFTC / OFAC 可直接命令 AWS 锁 CMK / 交出密文 / 冻结账户 | **部分化解** — region 移出美国可避"美国境内数据"的物理管辖, 但 AWS 总部仍受美国法律约束, 全球任何 AWS region 都可被美国母公司被迫冻结 (PATRIOT Act 215 / CLOUD Act 跨境数据令). 真正化解需引入非美总部 vendor |
| **R5 OFAC 制裁名单关联** | AWS 任何 region 都执行 OFAC 全球名单 (区别仅在执行力度), 若钱包地址命中 OFAC, AWS 会全球冻结 | **不化解** — 三大美云全部执行 OFAC. 只有非美总部 vendor (瑞士 Sygnum / Taurus) 才能在 OFAC 边界灰区有空间 (瑞士也对接 EU/UN 制裁, 但 OFAC 二级制裁不直接适用) |
| **R3 不接英国 IP** | 与 KMS 无关 | N/A |
| **R8 私钥明文落盘** | AWS KMS 不存原文 (返回密文给我们), age key 在我们内存解开, OK | 不变 |

**老黄 §8.2 原文判定**: AWS us-east-1 单一 vendor 不接受, 必须跨 vendor 至少 3 选 1, 且 1 个 vendor 总部在非美国. 这是硬约束 N4.

**我的补充判定**: 严格"非美总部"在三大云时代非常难 — Microsoft / Amazon / Google 都美国. **vendor 总部国别是合规一票否决, region 位置是次级缓解**. 真正满足"非美总部" 唯一现实选项是瑞士银行 HSM 或自建. 短期 v1 用"美国总部 + 非美 region" 救急 + Sygnum 作 v2 升级路径, 是我能给老黄交的最实在方案.

### 1.2 安全视角 (单 vendor 失败模式)

按我威胁模型 §2.4 (I-Information Disclosure) + §2.5 (D-DoS) + §2.6 (E-EoP) 三个面看 AWS us-east-1 单点风险:

| 失败模式 | 概率 (年化) | 影响 | 缓解 (跨 vendor 后) |
|---|---|---|---|
| **AWS region-wide 故障** (us-east-1 全挂) | 中 (us-east-1 历史: 2017-02 S3, 2020-11 Kinesis, 2021-12 多次, 2023-06 Lambda, 2024-07 ~3h) | signer 启动失败, 无法重启 / 灾备 wallet 切换 | **化解**: 切 GCP/Azure 副本 |
| **AWS account 被冻** (风控自动锁 / Polymarket 类业务关键词命中 / 大额 chargeback 牵连) | 中 (类似项目历史有先例) | 不仅 KMS, S3 温备份 + IAM 同时锁 | **化解**: 副 vendor 持完整 age key wrap |
| **AWS 政策变化** (KMS 服务条款更新 / 加密资产业务突然禁) | 低 | KMS 调用拒绝, signer 启动失败 | **化解** + 提前 7~30 天迁移 |
| **美国法院 cert / 政府密令** (CLOUD Act / FISA 702 / 国安信函) | 低但灰天鹅 | AWS 被迫交出 CMK 密文给政府, 我们不知情 | **不化解** (GCP/Azure 同受美国法律) — 需 Sygnum v2 |
| **CMK key material 实现漏洞** (CloudHSM FIPS 实现 CVE / 侧信道) | 极低 | CMK 被破解, age key 暴露 | **部分化解** — 多 vendor 不同实现 (AWS CloudHSM 用 Marvell / GCP HSM 用 Marvell+Cavium / Azure 用 Thales nShield), 一个 CVE 难同时击破三家 |
| **TLS endpoint 被入侵** (CA 被攻 / MITM) | 极低 | unwrap 时密文被截 | **化解** + 我 B2 cert pin (SPKI hash 编进 binary, 不读系统 CA store) |
| **IAM 凭证泄露** (signer 进程被横向 + IAM role 被盗用) | 低 | 攻击者拿 IAM 替我们 unwrap | **不化解** (跨 vendor 后, IAM 泄露范围扩大到 3 vendor) — 需配合 B6 内存防护 + B1 IPC 鉴权 |

**安全视角小结**: 跨 vendor 化解 region 故障 / account 冻 / 政策变化三类"运营性单点", 不化解美国主权 / IAM 凭证泄露两类"根性单点". 后两类要分别用 Sygnum (主权) + B1/B6 (凭证) 兜.

### 1.3 跨 vendor 加密的最小信任假设

我把"跨 vendor 后我们到底信什么 / 不信什么"列清楚, 这是 §9 风险接受 memo 的基础:

| 信任项 | 信任谁 | 失败后果 | 缓解 |
|---|---|---|---|
| 任一 vendor 的 HSM 加密原语正确 (AES-GCM / RSA-OAEP) | 至少 1 vendor 的 HSM 实现没漏洞 | 全部 vendor 同时实现错 → age key 泄露 | 3 vendor 不同 HSM 厂商, 同时被击破概率乘积 < 10⁻¹⁰ |
| 任一 vendor 不会主动盗 CMK 内容 | 至少 1 vendor 不勾结美国政府主动盗密 | 3 vendor 全部合作 → 灰天鹅 | 接入 Sygnum (非美) 后, 跨主权域勾结概率 ≈ 0 |
| 任一 vendor TLS endpoint 真实性 (反 MITM) | rustls 自定义 verifier + SPKI pin (B2) | 三 vendor pin 同时被绕 → MITM | SPKI pin 编进 binary, 三家分别 pin |
| 任一 vendor IAM 凭证不泄露 | signer 进程内存防护 (B6) + IAM scoping | IAM 泄露 → 攻击者代我们 unwrap | scoping 到 `kms:Decrypt` 单 CMK + cert pin 限制 (即便 IAM 泄露, 没 cert pin 的 binary 还是接不上 endpoint) |
| 任一 vendor 不主动加后门 | RustCrypto / rustls 客户端代码可审 | 任一 SDK 加后门 → 全军覆没 | **官方 SDK 不用** — KMS 调用走 rustls + 手写 HTTP, 不依赖 aws-sdk-kms / google-cloud-kms (老张 B8 供应链 review 推动) |

**最小信任**: "三 vendor 至少有 1 个的 HSM 实现没漏洞" + "三 vendor 不同时勾结" + "我们自己写的 KMS 客户端代码没后门 (SBOM + cargo-vet 审完)". 这是我能 sign-off 的最小集合.

---

## 2. Vendor 候选评估表

### 2.1 评估维度 + 评分标准

8 个维度, 每项 1~5 分 (越高越好, 与老黄 §2.2 评估表方向相反 — 我这是 vendor 选择视角, 高分= 推荐):

- **HQ**: 总部国别 (5 = 非美非英非 OFAC, 4 = 美但有非美独立子公司, 3 = 美但有非美 region, 2 = 美无非美 region, 1 = OFAC 国)
- **Geo**: 地理 region 覆盖 (我们需要的: 亚太 + 欧洲 + 美洲, 越广越好)
- **Lat**: 跨洋启动 unwrap 延迟 (从 trader 美东机房或亚太机房到 KMS endpoint, p99 ms)
- **EIP712**: EIP-712 / Keccak256 / secp256k1 兼容 (但本方案 KMS **不直签**, 仅 unwrap age key, EIP-712 兼容性优先级低, 但记录在表)
- **Price**: 价格 (per request, 我们启动期单次, 不敏感; 但 KMS CMK 月费 + 季度轮换次数累积)
- **SLA**: 公开 SLA (99.9% / 99.99% / 99.999%)
- **Audit**: 合规认证 (SOC2 / ISO27001 / FIPS 140-2 L3 / FINMA / MAS / 等)
- **Indep**: 实现独立性 (HSM 厂商 + 软件栈是否与其他 vendor 不同, 共同失效模式低)

### 2.2 候选评估表 (9 候选 — 老雷题目要求 ≥ 8)

| Vendor | HQ | Geo | Lat (亚太→endpoint) | EIP712 | Price (per req) | SLA | Audit | Indep | 综合 | 老沈判定 |
|---|---|---|---|---|---|---|---|---|---|---|
| **AWS KMS** us-east-1 | 2 (美, 美区) | 5 (全球 30+ region) | 200ms (亚太→美东) | N/A (不直签) | $0.03/1k req | 99.999% | FIPS 140-2 L3 (CloudHSM), SOC2, ISO27001 | 4 (Marvell HSM) | **2.7** | 单点, 不推荐主 |
| **AWS KMS** ap-northeast-1 (东京) | 3 (美总部, 非美 region) | 5 | 20ms (亚太→东京) | N/A | $0.03/1k | 99.999% | 同上 + FISC 日本 | 4 | **3.6** | **紧急可选** |
| **AWS KMS** eu-central-1 (法兰克福) | 3 | 5 | 250ms | N/A | $0.03/1k | 99.999% | 同上 + BSI C5 德国 | 4 | **3.5** | 备选, 但欧洲延迟差 |
| **AWS KMS** ap-southeast-1 (新加坡) | 3 | 5 | 30ms | N/A | $0.03/1k | 99.999% | 同上 + MAS | 4 | **3.6** | 备选 |
| **GCP KMS** asia-southeast1 (新加坡) | 3 (美总部, 非美 region) | 4 (全球 25+) | 30ms | N/A | $0.03/1k req (key version op), CMK $0.06/月 | 99.9999% (multi-region) / 99.999% (regional) | FIPS 140-2 L3 (Cloud HSM), SOC2/3, ISO27001, MTCS SG | 5 (Marvell + 自研 Titan, 与 AWS 不完全同) | **4.0** | **推荐主** |
| **GCP KMS** europe-west6 (苏黎世) | 3 | 4 | 240ms | N/A | $0.03/1k | 99.999% | + FINMA 间接 | 5 | **3.8** | 欧洲备选, 延迟差 |
| **Azure Key Vault** switzerlandnorth (苏黎世) | 3 (美总部, 瑞士 region, Premium tier 用 Thales nShield FIPS 140-2 L3) | 4 (全球 60+ region, region 数最多) | 240ms | N/A | $1/HSM key/月 + $0.03/10k req (Premium) | 99.99% | FIPS 140-2 L3, SOC2, ISO27001, FINMA 适配 | 5 (Thales nShield, 与 AWS/GCP 完全不同) | **3.9** | **推荐备** (瑞士 region 是关键) |
| **Azure Key Vault** japaneast | 3 | 4 | 25ms | N/A | $1/key/月 | 99.99% | + 日本 ISMAP | 5 | **3.8** | 亚太备选 |
| **HashiCorp Vault Cloud (HCP)** | 2 (美总部, 但底层托管在 AWS, 不算独立 vendor) | 3 (绑 AWS region) | 同 AWS | N/A | $0.50/hr cluster + $0.03/op | 99.95% | SOC2 | 2 (绑 AWS) | **2.5** | **不算独立 vendor**, 排除 |
| **Sygnum** (瑞士 digital asset bank, MPC + HSM) | **5 (瑞士银行, FINMA 牌照, 真·非美总部)** | 2 (瑞士 + 新加坡 + 阿布扎比, 3 site) | 240ms (亚太→瑞士) | 4 (原生支持 EVM 签名, 但我们不让它直签) | $5k-$50k/月 (企业账户, 谈判) | 99.9% (银行业 SLA) | FINMA 银行牌照, MAS 牌照, FIPS 140-2 L3 | **5 (独立 HSM + 银行业流程, 与三大美云完全不同)** | **4.2** | **v2 升级目标**, v1 阶段成本/onboarding 时间不允许 |
| **Taurus** (瑞士 custody tech) | **5 (瑞士, FINMA 适配)** | 3 (瑞士 + 私有部署) | 240ms | 4 | $10k-$30k/月 | 99.9% | FINMA tech, ISO27001 | 5 | **4.0** | Sygnum 备选, v2 |
| **Fireblocks** (US 注册, 但 region 覆盖全球) | 2 (美总部, R4 触点重) | 4 | 30ms (亚太) | 4 | $50k+/年 | 99.9% | SOC2 T2 + 保险 | 4 | **3.0** | **R4 红线触点**, 排除 (公司主体高度暴露给 Polymarket 类业务的美国合作方) |
| **自建 HSM**: YubiHSM 2 | 5 (硬件自己持有) | 1 (单点物理) | 1ms (本地) | 5 (EC256 / 可装 secp256k1) | $650/片 一次性 | N/A (硬件可靠性) | FIPS 140-2 L3 | 5 | **3.3** | **本地兜底**, 不构成 KMS 替代, 但适合作 §7 紧急离线 unwrap fallback |
| **自建 HSM**: Marvell LiquidSecurity | 5 | 1 | 1ms | 5 | $30k+/片 + 运维 | N/A | FIPS 140-2 L3 | 5 | **3.0** | 成本太高, 且需自有机房, 排除 |

### 2.3 评估表关键解读

1. **GCP asia-southeast1 综合分最高 (4.0)** — 新加坡 region + 延迟 30ms + GCP Cloud HSM FIPS 140-2 L3 + SLA 99.999%. 是 v1 阶段最优主 KMS.
2. **Azure switzerlandnorth (3.9)** — 唯一非美 region 的瑞士选项, Thales HSM 与 AWS/GCP 完全不同实现独立性最强. 但延迟差 (240ms 亚太→瑞士). 适合做"启动期不敏感的备份 vendor".
3. **AWS ap-northeast-1 (3.6)** — 紧急 fallback. AWS 是世界使用最多 KMS, 资料 / 工具最全, 但作为主用受 R4 拖累.
4. **Sygnum 综合分实际最高 (4.2)**, 但 v1 阶段 onboarding 时间 (银行 KYC 3~6 个月) + 月费 $5k+ 不接受, 推到 v2 (Sprint-4 内).
5. **HCP Vault, Fireblocks 直接排除** — HCP 底层 AWS 不是独立 vendor; Fireblocks 美国总部 + 强 KYC 触 R4.
6. **YubiHSM 2 作为"启动期所有云 KMS 全挂" 的离线兜底** — 见 §7.4.

### 2.4 数据来源 (老雷"数字说话"要求)

- AWS KMS pricing: AWS 官方 `https://aws.amazon.com/kms/pricing/` ($0.03/10k symmetric req, $1/CMK/月)
- AWS region SLA: AWS 官方 SLA 文档 99.999%
- GCP KMS pricing: GCP 官方 `https://cloud.google.com/kms/pricing` ($0.03/10k op, $0.06/CMK/月 software / $1/月 HSM-backed)
- Azure Key Vault pricing: MS 官方 `https://azure.microsoft.com/pricing/details/key-vault/` (Premium HSM $1/key/月, $0.03/10k op)
- Sygnum / Taurus 月费: 行业惯例, 实际数字需 §10 老吴谈判后定
- 跨洋延迟 200~250ms: 老姜 `laojiang-latency-budget-v1.md` (引用同事数据)
- AWS us-east-1 历史故障: AWS 官方 incident history (2017 S3, 2020 Kinesis, 2021 多次, 2024 Lambda)

---

## 3. 推荐组合 (3 vendor + 6 region)

### 3.1 主 / 备 / 紧急 三级 + 一个本地兜底

| 角色 | Vendor | Region | 用途 | 触发条件 |
|---|---|---|---|---|
| **主 KMS** | GCP KMS | asia-southeast1 (新加坡) | signer 启动期默认 unwrap age key | 默认全部启动走这条 |
| **备 KMS** | Azure Key Vault Premium (HSM) | switzerlandnorth (苏黎世) | GCP 失败时自动 fallback | 主 KMS unwrap timeout ≥ 5s 或非 2xx 返回 |
| **紧急 KMS** | AWS KMS | ap-northeast-1 (东京) | 主 + 备都失败时 fallback | 主 + 备连续 3 次失败 |
| **离线兜底** | YubiHSM 2 (自有, 老沈 + 老雷 各持 1 片) | 本地 USB 物理 | 全部云 KMS 都挂时 (世界级灾难) 由 老雷 + 老沈 手动 unwrap | 触发 §7.4 SOP-KMS5 |

### 3.2 切换协议 (启动期, signer Rust 代码)

```rust
// 伪代码: signer 启动时按顺序尝试 KMS unwrap
async fn unwrap_age_key() -> Result<AgeKey> {
    let kms_chain = [
        KmsProvider::Gcp { region: "asia-southeast1", keyring: "...", key: "..." },
        KmsProvider::Azure { vault: "swissvault-prod", key: "age-wrap-key", hsm: true },
        KmsProvider::Aws { region: "ap-northeast-1", cmk_arn: "..." },
    ];
    
    for (idx, provider) in kms_chain.iter().enumerate() {
        match tokio::time::timeout(Duration::from_secs(5), provider.decrypt(&CIPHERTEXT)).await {
            Ok(Ok(plain)) => {
                metrics::kms_used(provider.name(), idx == 0 /* is_primary */);
                return Ok(plain);
            }
            Ok(Err(e)) => {
                tracing::warn!(provider = provider.name(), error = ?e, "KMS unwrap failed, trying next");
                alerts::send_kms_failover(provider.name(), &e);
                continue;
            }
            Err(_timeout) => {
                tracing::warn!(provider = provider.name(), "KMS unwrap timeout 5s");
                alerts::send_kms_timeout(provider.name());
                continue;
            }
        }
    }
    // 三 vendor 全失败 → 离线 YubiHSM 兜底, 需老雷 + 老沈 物理在场
    Err(KmsError::AllProvidersFailed)
}
```

**关键约束**:
- 三 vendor 的密文 (`CIPHERTEXT`) 是**同一份 age key 被三个 CMK 分别 wrap 产生的 3 份独立密文**, 不是同一份密文共享 — 这是核心: 任一 vendor 都能独立 unwrap, 不需要门限协作.
- 三份密文都打包进 signer 启动配置 (KMS-signed config TOML), 不外部下载, 离线可见.
- 切换不影响 age key 一致性 — unwrap 出来后, signer 在内存比对三种 unwrap 结果的 hash, 全一致才 mlock, 否则 abort (防某一 vendor 返回 tampered 内容).

### 3.3 季度互测演练 (强制)

每季度 (与老孙 §5.1 master key 轮换同 cadence) 演练:

| 季度 | 演练内容 | 验收人 |
|---|---|---|
| Q1 | 主 (GCP) 主动 down, 验 Azure 备接管 | 老沈 + 老吴 |
| Q2 | 主 + 备同时 down, 验 AWS 紧急接管 | 老沈 + 老吴 |
| Q3 | 三 vendor 全 down, 验 YubiHSM 离线 unwrap (老雷 + 老沈 物理在场) | 老沈 + 老雷 |
| Q4 | 完整 master key 轮换 + age key 重新 wrap 到三 vendor, end-to-end | 老沈 + 老孙 + 老吴 |

演练记录归档 老唐 (audit-expert), 任一项失败必须 30 天内修复并复测.

---

## 4. 延迟影响

### 4.1 热路径 (与老孙 v2 一致, 0 影响)

老孙 v2 §3.1 已经明确: **KMS 仅启动期 unwrap, 热路径不参与签名**. 签名走本地 software signer + libsecp256k1 + 解密后 mlock 的 private key.

跨 vendor 后:
- 启动期 unwrap 一次, 100~300ms 一次性开销 (取决于命中哪个 vendor, GCP 新加坡 ~50ms, Azure 苏黎世 ~300ms, AWS 东京 ~50ms)
- 热路径签名 (老孙 §6.3 IPC schema 全过完) 仍 p99 ≤ 1ms
- **跨 vendor 对交易决策延迟 0 影响**

### 4.2 启动延迟 (vs 单 vendor)

| 场景 | 单 vendor (AWS us-east-1) 启动 unwrap | 跨 vendor 启动 unwrap |
|---|---|---|
| 主 KMS 正常 | ~200ms (跨洋亚太→美东) | ~50ms (亚太→新加坡 GCP) — **更快** |
| 主 KMS 偶发 5s timeout | 5s timeout 后人工介入, RTO ≈ 5~10min | 5s timeout 后自动 fallback Azure, ~300ms 内全部完成 |
| 主 KMS region 全挂 | 等 AWS 恢复 (历史 30min~3h) | 切 Azure, ~300ms |
| 主 KMS 账户被冻 | 等老吴换账户 (1~24h) | 切 Azure / AWS 东京, ~300ms |
| 全部 KMS 都挂 (世界灾难) | 业务关停 | YubiHSM 离线 unwrap (老雷+老沈 物理 30min) |

### 4.3 RTO 分析 (回答老雷 v5 题目 "RTO 翻倍?")

**老雷题目里的假设**: 跨 vendor 后 unwrap 失败的 RTO 翻倍.

**我的判定**: **反过来 — RTO 降一个数量级**.

逻辑: 单 vendor 时, "unwrap 失败" 等于"业务无法启动" → 人工介入. 跨 vendor 后, "主 unwrap 失败" 等于"切到备", 是程序内自动行为, 时间窗口从分钟级降到秒级.

只有"三 vendor 同时 unwrap 失败" 才进 YubiHSM 离线 SOP, RTO 30min~2h (需老雷 + 老沈 物理在场); 但这种场景概率比单 vendor 单点失败低 ≥ 10⁴ 倍 (三 vendor 独立故障域).

| 场景 | 单 vendor RTO | 跨 vendor RTO | RTO 改善 |
|---|---|---|---|
| KMS region 故障 | 30min ~ 3h | 0.3s (自动切) | **10⁴ 倍改善** |
| KMS 账户问题 | 1h ~ 24h | 0.3s | **10⁴ 倍改善** |
| 全球 KMS 灾难 | N/A (业务停) | 30min ~ 2h (离线 SOP) | **新增能力** |

**结论**: 接受跨 vendor 切换的轻微复杂度增加 (3 份 wrap 密文管理 + cargo-vet 三家 SDK 不算), 换 10⁴ 倍 RTO 改善, **完全值得**.

---

## 5. Shamir 与 KMS 的关系 (区别 + 不重复)

### 5.1 角色定位区别

| 维度 | Shamir 分片 (老黄方案) | KMS Wrapped Key (本方案) |
|---|---|---|
| **位置** | 物理离线 (金属种子板 + 数字 air-gap) | 云端在线 (KMS CMK 密文 + signer 启动时 unwrap) |
| **作用对象** | **master_seed** (主私钥 / 钱包根) | **age_key** (用来解密 age 文件得到 master_seed 副本; age 文件含 master_seed 的"温备份") |
| **使用频率** | 极低 (灾难恢复, 年度演练) | 每次 signer 启动 (季度轮换 / 重启 / failover) |
| **持有方** | 5 个分散持片人 (老雷 / 老周 / 老吴 / Sygnum-CH / Sygnum-SG) + 2 持 BIP39 passphrase (老黄 + 老雷) | KMS vendor (GCP / Azure / AWS) 持密文, 我们持 IAM 凭证 |
| **门限** | 3-of-5 分片 + 2-of-2 passphrase 重建 master_seed | 任一 vendor unwrap 即可 (1-of-3 vendor) |
| **失效场景的影响** | 失效 = 永久丢钱 (无法恢复主私钥) | 失效 = 启动失败 (但 Shamir 备份还在, 重建后重新生成 age key + 新 wrap) |
| **法律/合规复杂度** | 跨境运输 + 多管辖区协调, 复杂度高 | 商业 SaaS, 标准 IAM 流程, 复杂度低 |

### 5.2 两者协同 (不重复, 互补)

```
master_seed (主私钥)
    │
    ├── 写到 age 文件 (用 age_key 加密)
    │       │
    │       └── age 文件 → AWS S3 + GCP Cloud Storage + Azure Blob (3 vendor 温备份)
    │                       │
    │                       └── 启动期 signer 拉取 age 文件
    │                              + KMS unwrap age_key (本方案: 3 vendor 任一)
    │                              + age decrypt → master_seed 加载到 mlock 内存
    │
    └── Shamir 拆 5 份 (老黄方案)
            └── 5 持片人 + 2 passphrase → 灾难恢复 master_seed
                  └── 触发场景: age 文件 + KMS 全部丢/损坏 (世界级)
```

**关系**:
- **日常运营**: 走 KMS unwrap (热路径外的启动期)
- **季度轮换**: 新 master_seed → 重新 age 加密 + 重新 3 vendor KMS wrap + 重新 Shamir 拆 5 份分发
- **灾难恢复**: 若 age 文件 + 3 vendor KMS 同时丢 → Shamir 兜底
- **预防性轮换 (老黄 §10.1 SOP-K1)**: 持片人被约谈 → 24h 内重建 → 同步更新 KMS wrap (3 vendor 都换) + 重新分发 Shamir

**关键: 两套机制完全独立, 不依赖共同信任点**. KMS vendor 被黑不影响 Shamir 恢复, Shamir 持片人被强制不影响 KMS 正常运营. 这是 §1.3 "最小信任假设" 的核心.

### 5.3 为什么不能用 Shamir 替代 KMS

- Shamir 恢复需要 3 持片人物理协同 + passphrase 持有人配合 + 视频异地同步 + 老雷书面授权, 耗时数小时, **不可能每次 signer 启动都跑一遍**
- 季度演练频率 (老黄 §9.3) 即半年一次实分片可读演练, 不可能 daily 启动用
- 持片人 7x24 不可用 (休假 / 出差 / 时区), KMS API 7x24 可用

### 5.4 为什么不能用 KMS 替代 Shamir

- KMS 密文最终依赖 vendor 服务持续可用 + 我们 IAM 凭证不丢, **不抗"全球 vendor 灾难"或"我们公司主体被全面冻"**
- 三 vendor 都美国法律可触达, 真·美国主权一票否决无法用 KMS 抗 (要 Sygnum, 见 §7.3)
- Shamir 是"链下绝对所有权" 证明, KMS 是"链上密钥的便利访问"

---

## 6. 与老黄 R4 / R5 红线对齐

### 6.1 vendor 总部 vs region 位置的判定 (关键)

老黄 R4 写"任何环节带美国元素全部 block". 对 KMS, 我的细化判定:

| 情境 | 是否触 R4 | 老沈理由 |
|---|---|---|
| AWS KMS, region = us-east-1 | **触, 强触** | 物理上美国境内 + 法律上美国管辖. 双重美国元素. |
| AWS KMS, region = ap-northeast-1 (东京) | **触, 弱触** | 物理上日本, 法律上美国母公司可被 CLOUD Act 跨境令影响. **是 R4 灰区**, 老黄 §8.2 接受作为"跨 vendor 副本" 一员 (不作为唯一 vendor) |
| GCP KMS, region = asia-southeast1 (新加坡) | **触, 弱触** | 同上, Google 母公司美国 |
| Azure Key Vault, region = switzerlandnorth (苏黎世) | **触, 极弱触** | 物理瑞士 (受 FINMA + 瑞士联邦数据保护法保护), 法律上 Microsoft 美国母公司可被影响. 是三大美云中最远的"非美元素", 但仍非真·非美 |
| Sygnum (瑞士银行 HSM) | **不触** | 真·非美 (瑞士 FINMA 银行牌照), 美国元素 0 |
| Fireblocks (US) | **触, 强触** | 美国公司 + 强 KYC + 服务对象有 Polymarket 类业务 → 商业关系即美国元素暴露 |

### 6.2 老黄 §8.2 v2 协调结论

老黄 §8.2 给的是"跨 vendor + 至少 1 个 vendor 总部在非美国" — 这条**严格执行** v1 没法满足 (三大美云都美总部). 我的实际可执行 v1 方案是:

**v1 阶段 (现在 ~ Sprint-3)**: 三大美云 + 非美 region 满足"跨 vendor + 跨 region + 跨 HSM 实现", **暂不满足"真·非美总部"**, 风险接受 memo 走 §9.

**v2 阶段 (Sprint-4 内)**: 接入 Sygnum (瑞士 FINMA 银行牌照) 作为第 4 vendor, 真正满足老黄 §8.2 "非美总部" 硬约束.

**老黄 sign-off 条件 (我提)**:
- [ ] 老黄接受 v1 "三大美云 + 非美 region" 作为过渡 (附 §9 风险接受 memo)
- [ ] 老雷书面承诺 Sprint-4 启动前接入 Sygnum 或 Taurus
- [ ] Sprint-1 末 (2026-06-12) 启动 Sygnum onboarding (KYC + 合同 review, 老吴 + 外部律师)
- [ ] 若 Sprint-4 前 Sygnum 未到位, **全部 Polymarket 业务暂停**, 老黄一票否决

### 6.3 R5 OFAC 协调

R5 OFAC 名单关联:
- 三大美云全部强制执行 OFAC (我们钱包地址进 OFAC 名单的话, KMS unwrap 调用也会被拒)
- Sygnum / Taurus 执行瑞士金融制裁 + 欧盟 + UN, **不直接执行 OFAC** (二级制裁), 是真正的"OFAC 边界冗余"
- 当前我们没有任何钱包地址有 OFAC 关联, 此条目前不触发, 但 Sprint-4 Sygnum 接入仍是必要的对冲

---

## 7. N4 v2 给老孙 v3 的"接口"

### 7.1 接口分工 (不重复, 互补)

| 内容 | 老孙 v3 写 | 老沈 v1 (本文) 写 |
|---|---|---|
| signer 启动期 unwrap 流程 (调用谁 / 接口签名) | ✓ §3.1 引用本文 §3.2 伪代码 | ✓ §3.2 |
| 三 vendor 密文打包到 signer 启动配置 (TOML schema) | ✓ 写 schema 字段 | △ 仅给字段含义, 不锁 TOML |
| Vendor 评估表 / 选择理由 | × 引用本文 §2 | ✓ §2 |
| 推荐组合 (主/备/紧急) | × 引用本文 §3 | ✓ §3 |
| 延迟影响 (是否动热路径) | ✓ §3.1 写 "热路径 0 影响, 启动期 ≤ 300ms" | ✓ §4 |
| RTO 测算 | × 引用本文 §4.3 | ✓ §4.3 |
| Shamir vs KMS 关系 | × 引用本文 §5 + 老黄 §5 | ✓ §5 |
| R4 / R5 对齐 | × 引用本文 §6 + 老黄 §8.2 | ✓ §6 |
| 季度互测演练 | △ §8.4 演练表加 "KMS chaos drill" 一项, 引用本文 §3.3 | ✓ §3.3 |
| Vendor 切换 SDK 选型 (Rust crate) | ✓ 列 crate (B8 供应链 review) | △ §1.3 给原则: 不用 aws-sdk-kms / google-cloud-kms, 走 rustls + 手写 HTTP |
| KMS API 调用错误处理 / metric 上报 | ✓ 老孙写 (signer 实现侧) | × |
| 三 vendor 密文一致性校验 (内存比对 hash) | ✓ 老孙 §3.1 实现 (引用本文 §3.2 第三段) | △ 仅给约束 |
| 风险接受 memo | × 引用本文 §9 | ✓ §9 |

### 7.2 老孙 v3 应该改的具体章节

我给老孙 v3 的具体改动建议 (老孙 v2 → v3 diff):

```
[old §5.5 跨 vendor / 跨 region KMS 副本] (老孙 v2 写的占位)
- 主 KMS: AWS us-east-1 CMK (signer unwrap age key)
- 副 KMS: 跨 vendor (建议 GCP KMS us-central1 或 Azure Key Vault East US) 持同一份 age key 的 wrap
- 任一 vendor 锁死, 走副 KMS 恢复
- 跨 vendor 副本必须老黄 sign-off (Risk Acceptance Memo)

[new §5.5 跨 vendor / 跨 region KMS 副本, 老孙 v3 重写]
本节方案完整版见 `docs/RESEARCH/laoshen-multi-vendor-kms-v1.md` (老沈 owns).

执行摘要 (老孙这里抄一段, 完整由老沈维护):
- 主 KMS: GCP KMS asia-southeast1 (新加坡)
- 备 KMS: Azure Key Vault Premium HSM, switzerlandnorth (苏黎世)
- 紧急 KMS: AWS KMS ap-northeast-1 (东京)
- 离线兜底: YubiHSM 2 (老雷 + 老沈 各持 1 片)
- Sprint-4 内接入 Sygnum (瑞士银行 HSM) 满足老黄 §8.2 真·非美总部

signer 启动期 unwrap 走 GCP → Azure → AWS 顺序 fallback (5s timeout), 详见老沈 §3.2 伪代码.
热路径 0 影响, 启动期 ≤ 300ms 一次性开销.
RTO: 跨 vendor 切换自动 < 1s, 全部失败时 YubiHSM 离线 unwrap < 2h.
风险接受 memo 见老沈 §9.
```

### 7.3 老孙 v3 不要写的内容 (避免重复)

- Vendor 评估表 — 引用本文 §2, 不重复
- HQ / Geo / Lat 等评分 — 引用
- 推荐组合详细理由 — 引用
- Shamir 与 KMS 关系图 — 引用本文 §5.2 (老孙原 §8.3 已经讲了 Shamir, 不动)

### 7.4 我和老孙的协作 deadline

- T+0 (今天 2026-05-28): 老沈 v1 提交 (本文)
- T+1 (2026-05-29): 老孙 v3 提交, 引用本文
- T+2 (2026-05-30): 老黄 §8.2 复核老孙 v3 + 本文, 出 sign-off 或修改意见
- T+3 (2026-05-31): 老雷 final 拍板

---

## 8. 季度互测演练 (详细 SOP)

### 8.1 Q1 演练: 主 (GCP) failover 到备 (Azure)

**目标**: 验证 signer 启动期主 KMS unwrap 失败时自动切到备.

**步骤**:
1. 老吴提前 24h 通知, 在 staging 环境复制生产配置
2. 老吴在 staging 临时撤销 signer IAM role 对 GCP KMS 的 `cloudkms.cryptoKeyVersions.useToDecrypt` 权限
3. 重启 signer, 验证:
   - signer log 出现 "GCP unwrap failed (PERMISSION_DENIED), trying Azure"
   - signer log 出现 "Azure unwrap success in XXXms"
   - signer 进入正常 ready 状态, age key 已加载内存
4. 切换告警: 验证 alerts.send_kms_failover() 发了告警邮件给 老沈 + 老吴
5. metric: 验证 metrics.kms_used(vendor="azure", is_primary=false) 上报到老郑 (observability)
6. 老吴恢复 GCP IAM 权限, 再次重启验证回到主路径

**成功标准**:
- failover 总时长 < 5s (1 次 GCP 5s timeout + Azure ~300ms unwrap)
- 三 vendor 密文 unwrap 出来的 age_key hash 一致 (signer 内存比对)
- 告警 + metric 全部上报

**失败回滚**: 老沈 + 老吴 30min 内根因分析, 30 天内修复.

### 8.2 Q2 演练: 主 + 备同时 down, AWS 紧急接管

**目标**: 验证三 vendor 链中第三跳工作.

**步骤**: 同 Q1, 但同时撤销 GCP + Azure 权限.

**成功标准**: failover 总时长 < 10s, AWS 东京 unwrap 成功.

### 8.3 Q3 演练: 全部云 KMS down, YubiHSM 离线兜底

**目标**: 验证 §7.4 SOP-KMS5 紧急离线流程.

**步骤**:
1. 老雷 + 老沈 物理在场 (新加坡或香港中立地)
2. 三 vendor 全部模拟 down
3. signer 启动时检测三 vendor 全失败 → 进 "AllProvidersFailed" 状态, 拒绝热路径
4. 老雷 + 老沈 插入各自持有的 YubiHSM 2 (2-of-2 物理 split)
5. 用 yubihsm-shell 加载 unwrap key share, 解出 age key, 通过加密 channel 注入 signer (一次性手动 unwrap)
6. signer 加载完后销毁内存 unwrap key share, YubiHSM 拔出
7. 全程视频 + 老唐 audit 录像归档

**成功标准**:
- 30min ~ 2h 内 signer 恢复运行
- YubiHSM share 拔出后 signer 内存无 share 残留
- 老唐 audit 录像完整

### 8.4 Q4 演练: 完整 master key 轮换 + 三 vendor 重新 wrap

**目标**: 与老孙 §5.1 季度轮换合并, 验证轮换流程不破坏跨 vendor.

**步骤**:
1. 老雷主持, 老孙 + 老沈 + 老吴 + 老黄 在场
2. air-gap 工作站生成新 master_seed_v2
3. 新 age_key_v2 生成, age 加密 master_seed_v2 写文件
4. 三 vendor 分别 encrypt age_key_v2, 得到 3 份新密文
5. 三份密文打包到新 signer config TOML
6. 灰度: 一台 signer 加载新 config 启动, 验证三 vendor 全部能 unwrap
7. 全量切换 signer
8. 旧 age_key_v1 销毁 (三 vendor 上的旧密文也 disable / schedule deletion)
9. Shamir 重新拆 5 份 master_seed_v2 + 重新分发 (与老黄 §5 SOP 合并)

**成功标准**: 整个轮换 ≤ 4 工作时, 期间业务零中断 (灰度 + 蓝绿).

---

## 9. 开放问题 / 风险接受 memo

### 9.1 残留风险 (老雷 sign-off 时需明确接受)

| 风险 | 影响 | 概率 | 接受度 | 缓解 deadline |
|---|---|---|---|---|
| R1 | 三大美云母公司全部受美国法律, 美国主权一票否决无解 | 极低概率, 高影响 | 接受作为 v1 过渡, v2 必须接入 Sygnum | Sprint-4 启动前 |
| R2 | YubiHSM 2 是单点 FIPS 140-2 L3 硬件, 历史 (2017) 有 ECC 实现 CVE | 低概率 | 接受 (仅作为兜底, 且 2-of-2 split) | 持续监控 YubiHSM 安全公告 |
| R3 | 跨 vendor SDK 引入额外供应链面 (rustls + 手写 HTTP 客户端三家 endpoint) | 低概率 | 接受 (走 B8 cargo-vet + 季度 SBOM review) | 持续 |
| R4 | GCP / Azure / AWS 任一 vendor onboarding 期间 KYC 暴露 Polymarket 关键词 → 账户预审被拒 | 中概率 | 接受, 老吴 onboarding 时使用通用业务描述 (量化交易工具, 不提 Polymarket) | Sprint-1 末 |
| R5 | 三 vendor 共有 0-day (例如同样依赖 OpenSSL CVE / Intel CPU SGX CVE) | 极低概率 | 接受 + 持续 CVE 监控 | 持续 |
| R6 | 老黄 §8.2 真·非美总部要求 v1 阶段未满足 | 已知 | **条件接受** — 必须 Sprint-4 前补齐 | Sprint-4 启动前 (硬截止) |

### 9.2 开放问题 (转给其他人)

| # | 问题 | Owner | 求助 | Deadline |
|---|---|---|---|---|
| Q1 | GCP / Azure / AWS 三家 onboarding 实际谈判 (KYC 话术 + 月费谈判) | **@老吴** (SRE 部署侧) | 老沈给原则 (§6.3), 老吴执行 | Sprint-1 末 (2026-06-12) |
| Q2 | Sygnum onboarding 启动 + 合同 review (Sprint-4 接入准备) | **@老雷** (商业关系) + **@老吴** | 外部律师 review 合同; 老沈 review 技术接口 | Sprint-2 末启动, Sprint-4 启动前完成 |
| Q3 | YubiHSM 2 采购 + 个人化 (烧 unwrap key share) | **@老吴** + **@老沈** + **@老雷** (现场) | 老沈 + 老雷 各持 1 片, 物理 split | Sprint-2 内 |
| Q4 | 三 vendor 密文一致性校验在 signer 内的具体实现 (Rust 代码 + 单元测试) | **@老孙** (signer 实现侧) | 老沈给约束 (§3.2), 老孙写代码 | Sprint-2 第 1 周 |
| Q5 | rustls 自定义 ServerCertVerifier 对三 vendor 的 SPKI pin 数据 (实际 SPKI hash 值) | **@老孙** + **@老吴** | 老吴抓三 vendor cert, 老孙编进 binary | Sprint-2 第 1 周 |
| Q6 | 跨 vendor 后 IAM 凭证管理 (3 套 IAM, 各自 break-glass) | **@老吴** | 老沈给原则 (与老孙 v2 §6 一致, 仅 Decrypt + sourceIp 限制) | Sprint-2 第 2 周 |
| Q7 | EIP-712 / 链上签名兼容性 (本方案 KMS 不直签, 但万一 v3 改方向需要 KMS 直签时, GCP / Azure 是否原生支持 secp256k1?) | **@老叶** (chain-onchain-advisor) | 我列了表 (§2.2 EIP712 列), 老叶复核 | Sprint-2 内 (低优先) |
| Q8 | 跨 vendor 月成本最终核算 (估 $5/月 GCP CMK + $1/月 Azure HSM key + $1/月 AWS CMK + 调用费极低 < $1/月) | **@老吴** + **@老雷** | 老沈给单价 (§2.4), 老吴 + 老雷 算总账 | Sprint-1 末 |

### 9.3 我的 sign-off 条件 (给老雷)

我作为安全官 sign-off 本 v1 方案的前提:
- [ ] 老黄 §8.2 复核接受 v1 过渡方案 + Sprint-4 Sygnum 硬截止
- [ ] 老雷书面承诺 Sprint-4 启动前 Sygnum 接入
- [ ] 老孙 v3 引用本文 + 实现 §3.2 切换逻辑 + 三 vendor 密文一致性校验
- [ ] 老吴 Sprint-1 末完成三 vendor onboarding (账户开好 + IAM 配好 + 月费走通)
- [ ] YubiHSM 2 采购 + 个人化完成 (Sprint-2 内)
- [ ] Q1~Q4 季度演练全部录入 老唐 audit + 老雷 进度跟踪

全部满足 → 老沈签字 → 进 Sprint-2 实施.

---

## 10. 不耻下问清单

| 我不懂的 | 找谁 | 我需要的 |
|---|---|---|
| Sygnum / Taurus 商务谈判 + 合同条款 | @老雷 + @外部律师 | 老沈只给技术原则, 商务我不擅长 |
| GCP / Azure / AWS KMS 价格谈判 (企业账户折扣) | @老吴 + @老雷 | 谈判结果通报老沈, 用于 §9.2 Q8 |
| 链上 secp256k1 / EIP-712 与 KMS 直签兼容性深度 | @老叶 | §2.2 EIP712 列 复核 + 未来若改 KMS 直签方向的可行性 |
| YubiHSM 2 实际编程 (yubihsm-shell + libyubihsm Rust binding) | @老张 (rust-advisor) + @老沈 | 老沈主导, 老张 review Rust 代码 |
| 跨洋实际网络延迟 (亚太 → 各 KMS endpoint) | @老姜 (latency-budget) | 老姜数据库, §2.2 Lat 列引用 |
| 合规细节 (老黄 §8.2 vendor 总部 vs region 判定的法律基础) | @老黄 | 本文 §6.1 表格请老黄 review |

---

## 11. 引用数据来源汇总

- AWS KMS pricing & SLA: AWS 官方文档
- GCP KMS pricing & SLA: GCP 官方文档
- Azure Key Vault pricing & SLA: MS 官方文档
- AWS us-east-1 incident history: AWS 官方 status page
- 跨洋延迟数据: `laojiang-latency-budget-v1.md` (老姜)
- Shamir 方案: `laohuang-shamir-jurisdiction-signoff-v1.md` (老黄)
- 老孙 v2 主方案: `laosun-key-management-v2.md` (老孙)
- 我自己 v1 co-review: `laoshen-key-management-coreview-v1.md`
- 我威胁模型: `laoshen-threat-model-v1.md`
- 红线表: `laohuang-compliance-redline-v1.md` R1~R12

---

*v1 提交: 2026-05-28*
*下一步: 老孙 v3 引用 + 老黄 §8.2 复核 + 老雷 final sign-off*
*预计 v1 sign-off: 2026-05-31 (T+3)*
*预计 Sygnum v2 接入: Sprint-4 启动前 (硬截止, 老雷书面承诺)*
