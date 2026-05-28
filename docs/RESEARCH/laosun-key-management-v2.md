# 私钥管理方案 v2 (修 8 Blocker)

- Owner: 老孙 (crypto-signing-expert)
- Co-review: 老沈 (security-engineer) — v1 co-review sign-off-pending
- Co-consult: 老叶 (defi-onchain-advisor), 老吴 (linux-sre-devops), 老黄 (compliance-legal), 老李 (polymarket-api-expert), 老张 (rust-advisor), 老何 (cpp-version-advisor)
- Last review: 2026-05-28
- 验收人: 老雷 + 老沈 (24h 内 sign-off)
- 状态: v2 提交, 等老沈 sign-off
- v1 保留: `docs/RESEARCH/laosun-key-management-v1.md` (review trail, 不动)
- Co-review 原文: `docs/RESEARCH/laoshen-key-management-coreview-v1.md` (B1~B8 出处)

> 关联 ticket: S1-005
> 本 v2 自包含 (不强求 v1 已读), 但保留 v1 章节号 §1~§10, 新增 §11 IAM, §12 v2 变更摘要 + 验收 checklist + 残留问题.

---

## 0. TL;DR (给老雷, v2 update)

v1 主选型 (本地软件 signer + age-encrypted + KMS unwrap + 美东 colocation) 老沈整体认可, **选型不变**. v2 全部工作集中在工程实现层补 8 个 Blocker:

- **B1** UDS 鉴权升级到 SO_PEERCRED + binary hash + HMAC challenge/response
- **B2** KMS endpoint TLS cert SPKI hash 编进 signer binary, 不读系统 CA store
- **B3** 审批人改 WebAuthn + TOTP 双因子, challenge bind typed_data hash, Slack 仅知会
- **B4** signer binary 离线 minisign 签名, systemd ExecStartPre 验签 + 启动期自校验自身 sha256
- **B5** (最致命) IPC schema 改成传完整 EIP-712 typed data, signer 内部重算 hash + 校验 receiver 白名单 + amount 阈值表 + nonce 合理性, 然后才签
- **B6** 内存防护四件套补齐: mlock + MADV_DONTDUMP + PR_SET_DUMPABLE=0 + explicit_bzero (用 zeroize crate, 不裸 memset)
- **B7** signer active-standby 双进程 HA, detection < 100ms, failover < 1s, 双 signer nonce 由老叶设计的 nonce manager 分配
- **B8** Rust crate 供应链审查: cargo-vet + cargo-audit + cargo-deny + SBOM + commit hash pin + 季度第三方 review

修完后 STRIDE 11 个 P0 场景覆盖 10/11 (剩 I-02 API key 入 git 不在本方案范围, 归 secret-scanner 工程面).

---

## 1. v1 → v2 变更摘要 (与 v1 diff 一览)

| 章节 | v1 | v2 变更 | 关联 Blocker |
|---|---|---|---|
| §3.1 启动流程 | 5 步 | 加 "0. systemd 验 binary 签名" + "1.5 KMS endpoint cert pin 校验" + "4 改 explicit_bzero + MADV_DONTDUMP + PR_SET_DUMPABLE=0" | B2, B4, B6 |
| §6.3 IPC schema | 只传 `message_hash` | **重写**: 传完整 typed data + peer cred + HMAC + challenge/nonce; signer 内重算 hash + 校验 receiver 白名单 + amount + nonce | B1, B5 |
| §7 阈值审批 | Slack HMAC | 加 "身份验证方式"列, WebAuthn + TOTP 双因子, challenge bind typed_data hash; Approve 改老雷+老沈 双签 | B3 |
| §8 灾备 | 单 signer 重启 | 新增 §8.5 active-standby HA spec + nonce manager 接口 (与老叶对接) | B7 |
| §8.3 Shamir 持有人 | 占位符 "城市 A/B/C" | 列国籍 + 居住司法管辖区 + 托管介质; 老黄持 1 份; 必须 2 份境外, 0 份美国, 0 份 OFAC 国 | 跨境合规 |
| §9.1 部署 | systemd hardening | 加 yama/ptrace_scope=2 + seccomp 白名单 (N2 提前) | B6 |
| 附录 A 选型 | 列库名 | 加供应链审查工作流 (cargo-vet/audit/deny + SBOM + pin) | B8 |
| **新增 §11** | — | AWS IAM JSON spec 锁定时间 + Owner @老吴 | E-class |
| **新增 §12** | — | v2 验收 checklist + 残留开放问题 | — |

v1 §1~§5, §10 不变 (仅补字段, 未颠覆).

---

## 2. B1~B8 逐条修复方案

### 2.1 B1 (Spoofing) — IPC 鉴权升级

**问题**: UDS 0600 仅约束文件系统 access, 不能防 trader 主进程被入侵后仿冒 (E-02 横向).

**修复**: 三层防御

1. **OS 层 (peer credentials)**:
   - signer accept connection 后立刻 `getsockopt(SO_PEERCRED)` 拿 `{pid, uid, gid}`
   - uid/gid 必须 == 启动配置里 `allowed_trader_uid` (烧死, KMS-signed config)
   - 拿 pid → 读 `/proc/<pid>/exe` → 读 binary → sha256, 必须 == 启动配置里 `allowed_trader_binary_sha256`
   - 拿 pid → 读 `/proc/<pid>/cgroup`, 必须在 `allowed_trader_cgroup_path` 下 (防容器逃逸到别的 cgroup)

2. **应用层 (challenge-response 互认)**:
   - signer 启动时生成 32B HMAC key, 通过 KMS unwrap 同样路径分发给 trader (或 sealed file fd-passing)
   - trader 每次 SignRequest 必须带 `hmac = HMAC(key, request_bytes || nonce || ts)`
   - nonce 单调递增 (per-trader-per-signer-session), signer 维护 last_nonce, 拒绝重放
   - timestamp 偏差 > 500ms 拒签 (防 replay)

3. **签名层 (启动握手)**:
   - 第一次连接 trader 发 `HelloRequest{pubkey, binary_hash, claimed_pid}`, signer 校验通过后回 `HelloAck{session_id, hmac_key_handle}`
   - 后续所有 SignRequest 必须带 `session_id`, 切 session 必须重走 Hello

**代码草图** (Rust signer 侧):

```rust
use nix::sys::socket::{getsockopt, sockopt::PeerCredentials};

fn authenticate_peer(stream: &UnixStream) -> Result<PeerInfo> {
    let cred = getsockopt(stream.as_raw_fd(), PeerCredentials)?;
    if cred.uid() != CONFIG.allowed_trader_uid {
        return Err(AuthError::WrongUid);
    }
    let exe_path = format!("/proc/{}/exe", cred.pid());
    let binary_sha = sha256_file(&exe_path)?;
    if binary_sha != CONFIG.allowed_trader_binary_sha256 {
        return Err(AuthError::WrongBinary);
    }
    let cgroup = read_to_string(format!("/proc/{}/cgroup", cred.pid()))?;
    if !cgroup.starts_with(&CONFIG.allowed_trader_cgroup_path) {
        return Err(AuthError::WrongCgroup);
    }
    Ok(PeerInfo { pid: cred.pid(), uid: cred.uid(), binary_sha })
}
```

macOS 注: 开发期用 `LOCAL_PEERPID` + `LOCAL_PEEREPID`, 生产 Linux 用 `SO_PEERCRED`. signer 只生产部署在 Linux.

### 2.2 B2 (EoP) — KMS endpoint cert pin

**问题**: KMS unwrap 链路依赖系统 CA store, 任何 CA 被入侵 / 系统 trust store 被篡改即 MITM (T-06 / I-05).

**修复**:

1. signer binary 编译期内嵌 KMS endpoint (e.g. `kms.us-east-1.amazonaws.com`) 的 **SPKI (Subject Public Key Info) sha256 fingerprint 数组**, 至少 2 个 (主 + 备, 防 cert rotation)
2. TLS handshake 用 rustls + custom `ServerCertVerifier`, 不读系统 CA store
3. SubjectName 白名单: CN/SAN 必须 match `*.amazonaws.com` 且属 AWS KMS service 子域
4. SPKI pin 升级走 PR + 老沈 review, 不允许运维侧改 config

**代码草图**:

```rust
struct KmsPinVerifier {
    allowed_spki_sha256: Vec<[u8; 32]>,  // 编译期常量
    allowed_san_suffix: &'static str,    // ".kms.us-east-1.amazonaws.com"
}

impl ServerCertVerifier for KmsPinVerifier {
    fn verify_server_cert(...) -> Result<ServerCertVerified, Error> {
        let leaf_spki_hash = sha256(end_entity.subject_public_key_info());
        if !self.allowed_spki_sha256.contains(&leaf_spki_hash) {
            return Err(Error::General("SPKI pin mismatch".into()));
        }
        // SAN suffix check
        let sans = extract_sans(end_entity)?;
        if !sans.iter().any(|s| s.ends_with(self.allowed_san_suffix)) {
            return Err(Error::General("SAN whitelist mismatch".into()));
        }
        Ok(ServerCertVerified::assertion())
    }
}
```

SPKI pin rotation 流程: AWS 提前 30 天宣布 cert 变更 → 老孙 PR 加新 pin (保留旧) → 老沈 review → 发布 → cert 真正切换后 30 天再 PR 删旧 pin.

### 2.3 B3 (Spoofing) — 审批人身份双因子

**问题**: Slack HMAC 只能证消息没改, 不能证操作人是谁. UI 替换攻击下审批人看到 $500 实际签 $5000.

**修复**: WebAuthn 硬件 key + TOTP 双因子, challenge bind typed_data hash.

**审批人身份验证表** (v2 重写 §7.1):

| 阈值 | 审批人 | 身份验证方式 | SLA |
|---|---|---|---|
| < $500 | 自动 | signer 自身 + receiver 白名单 | < 1ms |
| $500 ~ $2k | 自动 + Slack 知会 | Slack 仅通知, 非审批 | < 1ms |
| $2k ~ $5k | 老韩 或 老雷 单签 | **WebAuthn (硬件 key) + TOTP** 双因子, challenge 含 typed_data hash | < 30s |
| > $5k | 老韩 + 老雷 双签 | 两人各自 WebAuthn 硬件 key (不能同把) + TOTP, 60s 内两签到 | < 60s |
| 日累计 > $20k | 老韩 + 老雷 双签 | 同上 + 老黄 24h 内补合规批注 (异步) | < 60s |
| Cancel | 自动签 | 仅记录 | < 1ms |
| **Approve (USDC allowance)** | **老雷 + 老沈 双签** (v2 改) | 双 WebAuthn + TOTP | < 5min |

**关键约束**:

1. WebAuthn challenge 必须 = `sha256(typed_data_full || amount_usdc || receiver_addr || nonce)`, 由 signer 生成, signer 校验回包 assertion
2. 审批人 hardware key fingerprint 入选前 老沈 + 老雷 当面录入, audit 留档
3. 审批 UI (小苏 frontend) cert pinning + 不允许公网 dev 机访问
4. Slack bot **只能知会 + 显示 typed_data 摘要 (人类可读)**, 不能作为审批途径
5. 审批超时 (60s) 默认 reject

**审批 WAL 字段** (老沈强制, 与老唐 audit 对齐):

```
ts_ns_ntp_synced, request_id, intent, chain_id, domain_separator,
typed_data_full, receiver_addr, amount_usdc,
trader_pid, trader_uid, trader_binary_hash,
approver_id (if any),
approver_webauthn_credential_id,
approver_webauthn_assertion_bytes,
approver_totp_window_used,
prev_record_hash, current_record_hash
```

### 2.4 B4 (Tampering) — signer binary 完整性

**问题**: T-02 CI 投毒 / 部署期 binary 替换, signer v1 没验签.

**修复**: 离线签名 + 多层验签.

1. **构建期 (老何 build-eng + 老张 rust-adv 协作)**:
   - signer Rust binary reproducible build (固定 toolchain version, RUSTFLAGS, target)
   - 老沈持 minisign / sigstore 离线私钥, 对 release binary 签名
   - 签名 + binary 同时发布到内部 artifact repo

2. **部署期 (老吴)**:
   - systemd unit 加 `ExecStartPre=/usr/local/bin/verify-signer-sig.sh`
   - 该脚本用 minisign pubkey (编进系统 image, 不可改) 验签 signer binary
   - 验签失败 systemd 拒绝启动

3. **启动期 (signer 自校验)**:
   - signer main() 第一件事: `read("/proc/self/exe") → sha256`, 与编译期烧死的 expected hash 比对
   - 不一致立刻 abort + 写 stderr (注: 此时还没 mlock, 写日志安全)
   - 防 ld_preload / 内存 patch (虽然内存 patch 防不了, 但提高门槛)

4. **运行期**:
   - 配合 B6 PR_SET_DUMPABLE=0 + yama/ptrace_scope=2, 防 ptrace 注入

**systemd unit 片段** (老吴 owns):

```ini
[Service]
ExecStartPre=/usr/local/bin/verify-signer-sig.sh /usr/local/bin/sports-signer
ExecStart=/usr/local/bin/sports-signer --config /etc/sports-signer/config.kms-signed.toml
LimitCORE=0
LockPersonality=yes
MemoryDenyWriteExecute=yes
NoNewPrivileges=yes
PrivateTmp=yes
ProtectHome=yes
ProtectSystem=strict
SystemCallFilter=@system-service ~@privileged ~@debug ~@mount ~@reboot
SystemCallArchitectures=native
```

### 2.5 B5 (Tampering, 最致命) — signer 二次校验完整 typed data

**问题**: v1 IPC 只传 32 字节 hash, signer 完全无法验证"这真的是给 Polymarket exchange 合约的吗", 等于"hash 盲签机". T-04 calldata 注入直接绕过 signer.

**修复**: **重写 IPC schema** (见 §3 完整 schema), signer 收完整 typed data 后内部:

1. **重算 hash**: keccak256(`\x19\x01` || domainSeparator || hashStruct(message))
2. **校验 claimed_hash == 重算 hash**, 不等拒签 + 告警
3. **校验 receiver 白名单**:
   - Polymarket CTF Exchange 合约地址 (老叶提供, mainnet/polygon)
   - Polymarket Neg Risk CTF Exchange 合约地址 (老叶提供)
   - 自家 funder address (轮换时新旧两地址)
   - 任何不在白名单的 receiver / verifyingContract 拒签
4. **校验 amount 在阈值表**: 单笔 + 日累计, 超阈值进审批队列 (走 B3)
5. **校验 nonce 合理**:
   - signer 维护本地 nonce manager (per-wallet)
   - 收到的 nonce 必须 == local_nonce 或 local_nonce + 1 (允许预签下一个)
   - 跨 active-standby 双 signer 的 nonce 协调走老叶的 nonce manager (§4)
6. **校验 intent / chain_id 一致性**: chain_id 必须 == 137 (Polygon mainnet); intent 字段必须匹配 typed data 的 primaryType (Order / Cancel / Approve / EOA_Tx)
7. **以上全过才调 libsecp256k1.sign()**

**关键: 不再有"客户端算 hash, signer 盲签"路径**. trader 端可以预算 hash 用于本地 dedup / cache, 但 signer 内部以重算结果为准, claimed_hash 仅用于 sanity check.

### 2.6 B6 (Information Disclosure) — 内存防护四件套

**问题**: v1 只有 `mlock + setrlimit(RLIMIT_CORE,0) + memset`, 少 `MADV_DONTDUMP` 和 `PR_SET_DUMPABLE=0`, 且 `memset` 可能被编译器优化掉.

**修复**:

1. **mlock** (v1 已有): `mlock(key_ptr, key_len)` 防 swap
2. **setrlimit(RLIMIT_CORE, 0)** (v1 已有): 禁 core dump
3. **MADV_DONTDUMP** (v2 新增): `madvise(key_ptr, key_len, MADV_DONTDUMP)`, 即便 RLIMIT_CORE 非 0 也不进 dump; 防 kernel panic dump / sysrq
4. **PR_SET_DUMPABLE=0** (v2 新增): `prctl(PR_SET_DUMPABLE, 0)`, 防 ptrace + /proc/<pid>/mem 读 (横向后还能不能 dump 私钥的关键)
5. **explicit_bzero / zeroize crate** (v2 修): 用 `secrecy` + `zeroize` crate 的 `Zeroizing<Vec<u8>>` 包裹, Drop 时编译器不可优化掉的清零; 绝不裸 `memset`
6. **系统侧** (老吴部署清单加):
   - `/proc/sys/kernel/yama/ptrace_scope = 2` (admin-only ptrace)
   - swap 全禁 (`swapoff -a` + fstab 注释)
   - core dump 全禁 (`kernel.core_pattern = |/bin/false`)

**代码草图**:

```rust
use secrecy::{Secret, ExposeSecret};
use zeroize::Zeroize;
use nix::sys::mman::{mlock, madvise, MmapAdvise};
use nix::sys::prctl;

struct PrivateKey(Secret<Zeroizing<Vec<u8>>>);

impl PrivateKey {
    fn load_from_age(age_file: &Path, age_key: &Secret<Vec<u8>>) -> Result<Self> {
        prctl::set_dumpable(false)?;  // PR_SET_DUMPABLE=0
        let mut key_bytes = age_decrypt(age_file, age_key.expose_secret())?;
        let ptr = key_bytes.as_ptr() as *mut c_void;
        let len = key_bytes.len();
        unsafe {
            mlock(ptr, len)?;
            madvise(ptr, len, MmapAdvise::MADV_DONTDUMP)?;
        }
        Ok(Self(Secret::new(Zeroizing::new(key_bytes))))
    }
    // Drop → zeroize::Zeroize → explicit_bzero
}
```

启动期断言: 若任一 syscall 失败立刻 abort, 不进降级模式.

### 2.7 B7 (DoS) — signer active-standby HA

**问题**: v1 单 signer, 崩了重启 3~10s, 跨洋叠加错过出场窗口.

**修复**: active-standby 双 signer 进程 + nonce manager (老叶设计).

**架构**:

```
┌──────────────┐
│   trader     │
│   (C++)      │
│              │
│   多路连接   │
└──┬────────┬──┘
   │ UDS-A  │ UDS-B
   │        │
┌──▼──┐  ┌──▼──┐
│sgnA │  │sgnB │  两 signer 进程, 同机 / 同机房
│活跃 │  │待命 │  持同一份 wallet key (age 文件双加载)
└──┬──┘  └──┬──┘
   │        │
   └────┬───┘
        │ gRPC (本机 UDS, 100μs RTT)
   ┌────▼─────┐
   │nonce mgr │  老叶设计, 链上 nonce 仲裁
   │(老叶 owns)│  分配 nonce range, 防 active-standby 同时签同一 nonce
   └──────────┘
```

**Detection** (trader 侧):
- trader 维护 `health_ping` 通道, 每 50ms 发一次 UDS ping
- 连续 2 次失败 (100ms) → 标记 signer-A unhealthy → 切到 signer-B
- 切换原子操作: trader 内部 `current_signer_handle` atomic swap

**Failover** (signer 侧):
- signer-B 常驻热备, key 已加载在内存 (mlock), UDS 已 listen
- signer-A 崩溃 → signer-B 收到 nonce manager 的 "you are now primary" 信号 (老叶定义)
- signer-B 从 nonce manager 拉最新 nonce, 开始接管签名
- 总 failover < 1s (100ms detection + 100ms nonce sync + <500ms warmup)

**Nonce 协调** (老叶 owns 接口, 老孙调用):

```rust
trait NonceManager {
    fn reserve_nonce(&self, wallet: Address) -> Result<u64>;
    fn confirm_used(&self, wallet: Address, nonce: u64) -> Result<()>;
    fn current_chain_nonce(&self, wallet: Address) -> Result<u64>;
}
```

signer 收到 SignRequest 后:
1. `let nonce = nonce_mgr.reserve_nonce(wallet)?;`
2. 校验 typed_data 里的 nonce == reserved
3. 签名 → 返回 trader
4. trader 上链成功后 → `nonce_mgr.confirm_used(wallet, nonce)`
5. 若 trader 上链失败 → trader 显式 `release_nonce` (老叶定义)

**WAL 合流**: active-standby 两 signer 各写本地 WAL, 由 audit 侧 (老唐) 周期性合并 + 按 ts 排序 + verify hash chain.

**SLO**:
- Detection: p99 < 100ms
- Failover: p99 < 1s
- 期间 trader 进 cancel-only 模式 (cancel 不阻塞, 因为 cancel 是 receiver=exchange 的特殊 intent, 仍可走 standby)

### 2.8 B8 (Supply chain) — Rust crate 审查

**问题**: signer 用 alloy / k256 / age / secrecy / zeroize / nix / rustls 等 crate, v1 完全没说怎么审.

**修复**: 工具链 + 流程双管.

**工具链**:

| 工具 | 用途 | 触发 |
|---|---|---|
| `cargo-audit` | RustSec advisory DB CVE 扫 | CI 每次 PR + 每周定时 |
| `cargo-deny` | license / 重复 crate / 已知 ban 列表 | CI 每次 PR |
| `cargo-vet` | 人工 review trust 记录 (Mozilla / Google 标准) | 新增 crate / 升版本必须 vet |
| `cargo-supply-chain` | 看 crate 维护者 + publisher | 季度 review |
| SBOM (cyclonedx-rust-cargo) | 输出 SBOM JSON | 每次 release |

**流程**:

1. **关键 crate 清单 + 锁版本 (commit hash pin in Cargo.toml)**:

| Crate | 用途 | 版本策略 |
|---|---|---|
| `k256` | secp256k1 签名 (RustCrypto, pure Rust) | commit hash pin, 升级走 PR + 老沈 review |
| `secp256k1` (bitcoin-core binding) | 备选 / 性能基线 | 同上 |
| `alloy-sol-types` | EIP-712 typed data 编码 | 同上, @老李 校 schema |
| `sha3` 或 `tiny-keccak` | Keccak256 | 同上 |
| `age` (rage) | 文件加密 | 同上 |
| `secrecy` + `zeroize` | 内存清零 | 同上 |
| `nix` | mlock / madvise / prctl / SO_PEERCRED | 同上 |
| `rustls` + `webpki` | KMS TLS | 同上, B2 cert pin 依赖 |
| `aws-sdk-kms` | KMS unwrap | 同上 |
| `serde` + `rmp-serde` | IPC msgpack | 同上 |

2. **新增 crate 流程**:
   - PR 提出 → 老张 (rust-adv) 技术 review → 老沈 (security) 供应链 review → cargo-vet 标记 → merge
   - 任何 transitive dep 升级也走同样流程 (cargo-deny ban unaudited)

3. **季度 review (老沈 + 老张)**:
   - SBOM diff 跟上季度对比
   - 新进 crate 必须 vet
   - 弃用 crate 必须移除
   - 老沈 sign-off + audit 留档

4. **CI 集成**:

```yaml
# .github/workflows/signer-supply-chain.yml
- name: cargo-audit
  run: cargo audit --deny warnings
- name: cargo-deny
  run: cargo deny check
- name: cargo-vet
  run: cargo vet --locked
- name: SBOM
  run: cargo cyclonedx --format json > sbom.json
- name: SBOM diff
  run: diff sbom.json baseline-sbom.json || echo "SBOM changed, requires review"
```

5. **第三方审查 (季度)**: 关键 crate 至少有一个独立机构审计记录 (Trail of Bits / OSTIF / Open Source Security Foundation) 或近期社区 audit. 老沈维护 trust 矩阵.

---

## 3. 新 IPC schema (B5 重点 — typed-data 传输 + 白名单校验)

### 3.1 v2 IPC schema (msgpack binary, 取代 v1 §6.3)

```rust
// trader → signer
struct SignRequest {
    // === v1 字段 (保留 + 部分语义改) ===
    request_id: u64,
    session_id: [u8; 16],              // B1 Hello 握手分配
    timestamp_ns: u64,                  // NTP-synced, signer 校 ±500ms
    nonce: u64,                         // 单调递增 per-session, signer 拒重放

    // === B1 鉴权字段 (新增) ===
    hmac: [u8; 32],                     // HMAC-SHA256(session_key, all_above_bytes)

    // === B5 完整 typed data (新增, 取代 v1 message_hash) ===
    chain_id: u64,                      // 必须 == 137
    domain: EIP712Domain,               // name, version, chainId, verifyingContract
    primary_type: String,               // "Order" / "Cancel" / "Approve" / "EOA_Tx"
    typed_data_json: Vec<u8>,           // 完整 EIP-712 message (canonical JSON 序列化)
    claimed_hash: [u8; 32],             // trader 算的 hash, signer sanity check
    
    // === 业务字段 (signer 校验白名单/阈值用) ===
    intent: Intent,                     // Order / Cancel / Approve / EOA_Tx
    receiver_addr: [u8; 20],            // signer 校 white list
    amount_usdc: u64,                   // 1e6 单位, signer 校阈值
    wallet_addr: [u8; 20],              // 哪个 wallet 签
    expected_onchain_nonce: u64,        // signer 与 nonce_mgr 对齐
}

struct EIP712Domain {
    name: String,
    version: String,
    chain_id: u64,
    verifying_contract: [u8; 20],       // 必须 in receiver whitelist
    salt: Option<[u8; 32]>,
}

enum Intent {
    Order,           // 下单, receiver 必须是 CTF Exchange / Neg Risk CTF Exchange
    Cancel,          // 撤单, receiver 同上 (任意金额免审批)
    Approve,         // USDC allowance, receiver 必须 == USDC contract, 走 B3 双签
    EOA_Tx,          // 通用以太坊交易, 严格白名单 + 双签
}

// signer → trader
struct SignResponse {
    request_id: u64,
    session_id: [u8; 16],
    status: SignStatus,
    
    // Ok 时填
    r: [u8; 32],
    s: [u8; 32],
    v: u8,
    signer_pubkey: [u8; 20],            // trader 校验返回是预期 wallet
    used_nonce: u64,                    // signer 实际写入 typed data 的 nonce
    
    // Reject 时填
    reject_reason: Option<RejectReason>,
}

enum SignStatus {
    Ok,
    NeedsApproval,                       // 入队列, trader 收到后 polling 或异步回调
    RejectedReceiverNotWhitelisted,
    RejectedAmountExceeded,
    RejectedHashMismatch,
    RejectedHmacInvalid,
    RejectedNonceConflict,
    RejectedReplay,
    RejectedSessionExpired,
    InternalError,
}
```

### 3.2 signer 校验流程 (B5 关键)

```
1. peer cred check (SO_PEERCRED, B1)
2. HMAC verify (B1)
3. timestamp / nonce replay check (B1)
4. session valid check (B1)

5. chain_id == 137? 否则 reject
6. domain.verifying_contract ∈ receiver_whitelist? 否则 reject
7. receiver_addr ∈ receiver_whitelist? 否则 reject (注: receiver_addr 与 verifying_contract 业务上语义不同, 都查)
8. intent 与 primary_type 一致? 否则 reject

9. 重算 hash:
   computed_hash = keccak256(b"\x19\x01" || domain_separator(domain) || hashStruct(primary_type, typed_data_json))
10. computed_hash == claimed_hash? 否则 reject + 高优先级告警 (可能 trader 被入侵)

11. amount 阈值检查:
    - amount <= $500 → 直接放行
    - $500 < amount <= $2k → 放行 + Slack 知会
    - $2k < amount <= $5k → 入审批队列, 单签
    - amount > $5k → 入审批队列, 双签
    - 日累计 > $20k → 强制双签
    - intent == Approve → 强制 老雷+老沈 双签

12. nonce 校验:
    reserved = nonce_mgr.reserve_nonce(wallet_addr)?
    若 expected_onchain_nonce != reserved → reject (trader 与链上不同步)

13. (如果需要审批) 等待 WebAuthn assertion (异步, 不阻塞热路径)

14. 调 libsecp256k1.sign(computed_hash, private_key) → (r, s, v)

15. 写 WAL (append-only + hash chain)
    nonce_mgr.confirm_used(wallet_addr, used_nonce)

16. 返回 SignResponse
```

### 3.3 Receiver 白名单 (老叶 owns 清单, 老孙调用)

```rust
// 老叶 S1-002 + 老李 S1-002 联合 sign-off
const RECEIVER_WHITELIST: &[(&str, [u8; 20])] = &[
    ("Polymarket CTF Exchange (mainnet)",        addr!("0x4bFb...")),  // 待 @老叶 填
    ("Polymarket Neg Risk CTF Exchange",         addr!("0xC5d5..."));  // 待 @老叶 填
    ("USDC.e (Polygon)",                         addr!("0x2791..."));  // 已知
    ("Funder address (current rotation)",        addr!("0x____..."));  // 启动配置注入, KMS-signed
    ("Funder address (previous, 7d overlap)",    addr!("0x____..."));  // 同上
];
```

白名单升级走 PR + 老沈 + 老叶 + 老黄 三签, KMS-sign config 文件烧死, 运行时不可改.

### 3.4 与 v1 的差异 (一图看清)

| 字段/检查 | v1 | v2 |
|---|---|---|
| 传 `message_hash` | 有 | **改为** `claimed_hash` + 完整 typed data |
| 传完整 typed_data | 无 | **新增** |
| signer 重算 hash | 无 | **新增** |
| receiver 白名单 | 无 | **新增** |
| amount 阈值 (signer 内) | 在 trader 算 | **下沉到 signer** |
| nonce manager 集成 | 无 | **新增** |
| peer cred check | 0600 文件权限 | **+ SO_PEERCRED + binary hash** |
| HMAC + nonce replay | 无 | **新增** |
| session 概念 | 无 | **新增** (Hello/Ack 握手) |

---

## 4. signer active-standby HA 设计 (B7, 与老叶 nonce 设计接口)

(详见 §2.7, 此处补部署 + 故障矩阵.)

### 4.1 部署拓扑

- **同机房 active-standby** (推荐): signer-A 和 signer-B 同物理机房, 不同物理机, 共享 nonce_mgr (老叶 owns, 可放第三台机器或 etcd / consul cluster)
- **跨机房 active-standby** (灾备态): signer-B 在备机房, 平时不接流量, 只 sync 状态; 主机房断电时切

### 4.2 故障矩阵

| 场景 | Detection | 处理 | RTO |
|---|---|---|---|
| signer-A 进程崩溃 | trader UDS ping 100ms 失败 | trader 切 signer-B; signer-A 由 systemd restart | < 1s 业务可用 |
| signer-A 主机断电 | 同上 | 同上 | < 1s |
| nonce_mgr 不可用 | signer 调 reserve_nonce timeout | 进 cancel-only (cancel 不消耗 nonce 顺序) | < 100ms (取决于 nonce_mgr SLO) |
| 双 signer 都崩 | trader 两路 UDS 都失败 | trader 进 cancel-only + 紧急告警 (老沈 + 老雷 + 老韩) | 业务降级, 等手动恢复 |
| 主机房断电 | 跨机房健康检查 | 跨机房切 (DNS + nonce_mgr 仲裁) | < 30s |
| age 文件损坏 | 启动期校验 fingerprint 失败 | 启动 fail, 走灾备 wallet (§8 紧急轮换) | < 1h |

### 4.3 与老叶 nonce manager 接口 (老叶 owns 实现, 老孙 owns 调用)

**老叶需要交付**:

1. `NonceManager` trait 实现 (gRPC over UDS, 同机部署)
2. 链上 nonce 同步频率 (建议 1s)
3. reserve_nonce 的 SLO (建议 p99 < 10ms)
4. 主备 signer 切换时的 nonce range 转交协议
5. 异常 (链上 nonce 跳跃 / reorg) 时的回退策略

**老孙这边的依赖时间窗**: Sprint-2 第 1 周必须拿到 nonce_mgr 的 mock + 接口定义, 第 2 周拿到生产实现.

---

## 5. 跨境合规 Shamir 分布 (与老黄会签)

(v1 §8.3 重写, 老沈 §4 + 老黄红线对齐.)

### 5.1 Shamir 3-of-5 持有人 (v2 锁定)

| # | 持有人 | 国籍 | 居住司法管辖区 | 托管介质 | 合规属性 |
|---|---|---|---|---|---|
| 1 | 老雷 (CEO) | 中国 | 新加坡 | 个人保险柜 + 钢板 | 非美籍, 境外 |
| 2 | 老沈 (security) | 中国 | 香港 | 公司保险柜 + 钢板 | 非美籍, 境外 |
| 3 | 老黄 (compliance) | 中国 | 中国大陆 | 律所保险柜 (老黄关联) + 钢板 | 法务持有, 境内 1 份 |
| 4 | 老孙 (crypto-signing) | 中国 | 新加坡 | 个人保险柜 + 钢板 | 非美籍, 境外 (跟老雷不同保险柜) |
| 5 | 离线冷钱包 (无持有人) | — | 瑞士第三方银行保险柜 | 钢板 + 加密 BIP39 passphrase | 跨 vendor 冷备 |

**合规检查**:
- 美国境内 0 份 (老黄 R4 美国元素禁)
- OFAC 国 0 份 (老黄 R5)
- 至少 2 份境外 (新加坡 x2 + 香港 + 瑞士 = 4 份境外, 中国大陆 1 份) — 符合
- 至少 1 人非美籍非中国大陆居住 (老雷/老沈/老孙 居住境外) — 符合
- 跨司法管辖区 (新加坡/香港/中国/瑞士) — 4 区, 任一管辖区单点失效不致丢

**关键决策 (老沈 §4.2 建议, 老孙采纳)**: 老黄持 1 份, 替换 v1 "外部托管" 占位符. 法律风险低 + 老黄本就是合规 owner.

### 5.2 Shamir 分片二次加密 (N3 提前到 v2, 老沈建议)

每个分片再用 BIP39 passphrase 加一层:
- passphrase 由分片持有人 + 老雷 二人脑子记 (split knowledge)
- 单一分片落地 (钢板) 不等于半个密钥, 攻击者需同时拿钢板 + passphrase
- passphrase 季度轮换, 持有人离职走 90 天 rotate

### 5.3 恢复门槛 (v2 强化)

- 任意 3 人到场, 必须**来自不同司法管辖区** (老沈 §4.2 强制)
- 视频异地同步 (老雷主持)
- 老雷书面授权 + 老黄合规授权 双签
- 老唐 audit 全程录像归档

### 5.4 演练 (v2 加 chaos drill)

- 每半年一次 Shamir 恢复演练 (v1 已有)
- 每季度一次 signer chaos drill (随机 kill signer, 验证 active-standby + cancel-only) — N7 提前到 v2

### 5.5 跨 vendor / 跨 region KMS 副本 (老沈 §4.3 必须)

- 主 KMS: AWS us-east-1 CMK (signer unwrap age key)
- 副 KMS: 跨 vendor (建议 GCP KMS us-central1 或 Azure Key Vault East US) 持同一份 age key 的 wrap
- 任一 vendor 锁死, 走副 KMS 恢复
- 跨 vendor 副本必须老黄 sign-off (Risk Acceptance Memo)

---

## 6. AWS IAM JSON spec 锁定时间 + 责任 @老吴

(v1 §10 Q1 推迟项, v2 必须给定锁定时间, 老沈强制.)

### 6.1 锁定时间

| 阶段 | Deadline | Owner | 状态 |
|---|---|---|---|
| 老吴 跨洋部署方案 (S1-010) 定稿 | Sprint-1 末 (2026-06-12) | 老吴 | 待 |
| 老吴 出 IAM JSON spec 初稿 | 2026-06-19 (Sprint-1 末 +7d) | 老吴 | **未启动 — 本 v2 督办** |
| 老沈 + 老孙 IAM spec review | 2026-06-22 | 老沈 + 老孙 | 待 |
| **Sprint-2 启动前 (2026-06-26) IAM spec sign-off** | **2026-06-26** | **老雷 final** | **硬截止, 不再推迟** |

老孙的承诺: **Sprint-2 启动前 IAM spec 不到位, 老孙不开始 signer 实现**. 此 deadline 直接对老雷负责.

### 6.2 IAM Spec 要求 (老沈 §2.6 强制, 老孙列大纲, 老吴填 JSON)

**signer 进程 IAM role**:
- 仅 `kms:Decrypt` 权限
- 仅对单一 CMK ARN (age key wrap CMK)
- 不允许 `kms:Encrypt` / `kms:ListKeys` / `kms:DescribeKey` / `kms:GenerateDataKey`
- Condition: `aws:SourceIp` 限 signer 主机 IP (静态)
- Condition: `aws:RequestedRegion` 限 us-east-1

**CMK key policy**:
- 仅 signer role 可 Decrypt
- 仅 老沈 + 老雷 双签可 Encrypt (新 age key 轮换时)
- 紧急恢复 role (break-glass) 仅 老雷 + 老沈 联合启用, 写入 CloudTrail 高优告警

**监控**:
- CloudTrail 所有 KMS 调用记录
- KMS grant 任何变更告警 (小郑 监控)
- 异常 Decrypt 频率告警 (>10/min 或非工作时段)

**Break-glass account**:
- 离线 root credential (USB + 加密)
- 2FA hardware token
- 老雷 + 老沈 各持一半 (split knowledge)
- 使用必须 audit + 24h 内复盘

### 6.3 老孙不再拖延的承诺

v1 §10 Q1 "Q1 KMS 选 AWS 还是自建 Vault" 老孙打算"老吴跨洋部署后再定". 老沈批评"不能再拖". v2 承诺:

- 默认选 AWS KMS (us-east-1), 与 trader 同 region, 性能可控
- Vault Transit 仅作为合规升级路径备选 (v1 §3.3 已写, 不变)
- 老吴 6/19 出 IAM spec, 6/22 老沈 + 老孙 review, 6/26 sign-off, 6/26 Sprint-2 启动
- 若老吴 6/19 未交付, 老孙升级到老雷直接介入

---

## 7. 验收 checklist (对应威胁模型 11 个 P0 场景)

### 7.1 STRIDE P0 场景覆盖 (老沈 §5 映射)

| 威胁 (DREAD) | v1 覆盖 | v2 覆盖 | Blocker |
|---|---|---|---|
| **I-01** 私钥落日志/dump (20) | 部分 | 全 (B6 四件套 + zeroize) | B6 |
| **E-01** 第三方依赖 RCE (20) | 未 | 全 (B8 工作流 + 季度 SBOM) | B8 |
| **T-01** 供应链投毒 (19) | 未 | 全 (B8 commit hash pin + cargo-vet) | B8 |
| **T-04** calldata 签名前篡改 (19) | 未 | 全 (B5 signer 重算 + 白名单) | B5 |
| **I-02** API key 入 git (19) | — | 不在本方案范围 (归 secret-scanner) | — |
| **S-05** 仿冒 IPC (18) | 部分 (UDS 0600) | 全 (B1 SO_PEERCRED + HMAC + binary hash) | B1 |
| **D-02 / D-04** 跨洋抖动 (18) | 部分 (主备 wallet) | 全 (B7 active-standby + < 1s failover) | B7 |
| **T-02** CI 投毒 (18) | 部分 | 全 (B4 minisign + ExecStartPre + 自校验) | B4 |
| **T-03** RiskManager 配置 (18) | 部分 (KMS-sign config) | 全 (同 + git tag sigstore 校验) | T-03 |
| **E-02** 横向到 signer (18) | 部分 (systemd) | 全 (+ seccomp + yama/ptrace_scope=2 + B6) | E-02 |
| **E-03** SSH/sudo (18) | 全 (禁 SSH + bastion) | 全 (不变) | E-03 |

**覆盖统计**: 10/11 (I-02 不在本方案范围). 符合老沈 sign-off 条件.

### 7.2 v2 verify checklist (老雷 + 老沈 sign-off 用)

#### B1 IPC 鉴权
- [ ] signer 实现 SO_PEERCRED + uid + binary sha256 + cgroup 校验
- [ ] HMAC session_key 分发流程 (Hello/Ack) 实现
- [ ] nonce 单调递增 + timestamp ±500ms 校验
- [ ] replay attack 单元测试 (重放历史 SignRequest 必拒)
- [ ] 错误 binary 仿冒测试 (改 trader binary 一字节, signer 必拒)

#### B2 KMS cert pin
- [ ] SPKI hash 数组编进 signer binary
- [ ] rustls 自定义 ServerCertVerifier 实现
- [ ] 系统 CA store 不被引用 (CI grep 校验)
- [ ] cert rotation runbook (老吴 + 老孙)

#### B3 审批人双因子
- [ ] WebAuthn 集成 (硬件 key) + TOTP 校验
- [ ] challenge bind typed_data hash 实现
- [ ] hardware key 入选 SOP (老沈 + 老雷 当面)
- [ ] Approve 改 老雷 + 老沈 双签
- [ ] UI 替换攻击测试 (审批 UI 显示 $500, 实签 $5000 必被 challenge bind 阻止)

#### B4 binary 完整性
- [ ] minisign 离线签名流程 (老沈 owns pubkey)
- [ ] systemd ExecStartPre 验签
- [ ] signer 启动期自校验 sha256
- [ ] reproducible build 验证 (老何 + 老张)

#### B5 signer 二次校验
- [ ] IPC schema v2 实现 (完整 typed data)
- [ ] signer 内重算 hash (sanity check claimed_hash)
- [ ] receiver 白名单 (老叶 提供地址) + 拒签测试
- [ ] amount 阈值表 + 审批队列集成
- [ ] nonce 校验 + nonce_mgr 集成 (老叶)
- [ ] EIP-712 byte-equal 测试 (与 Polymarket SDK 100+ 向量, 老李 协作)

#### B6 内存防护四件套
- [ ] mlock + setrlimit(RLIMIT_CORE, 0) 已有
- [ ] MADV_DONTDUMP + PR_SET_DUMPABLE=0 新增
- [ ] secrecy + zeroize crate 替换裸 memset
- [ ] yama/ptrace_scope=2 + swapoff + core_pattern (老吴 部署清单)
- [ ] ptrace attempt 测试 (gdb attach 必失败)

#### B7 signer HA
- [ ] active-standby 双 signer 进程实现
- [ ] trader UDS 双连 + atomic swap
- [ ] nonce_mgr 接口 (老叶) + 集成
- [ ] detection < 100ms 实测
- [ ] failover < 1s 实测
- [ ] chaos drill: 随机 kill signer, 业务无中断 (cancel-only OK)

#### B8 供应链
- [ ] Cargo.toml commit hash pin (关键 crate)
- [ ] cargo-audit / cargo-deny / cargo-vet CI 集成
- [ ] SBOM 生成 + baseline diff
- [ ] 关键 crate 第三方审计记录归档 (老沈 trust 矩阵)
- [ ] 季度 SBOM review SOP (老沈 + 老张)

#### 跨境合规
- [ ] Shamir 5 人持有人确定 (老雷/老沈/老黄/老孙/瑞士 vault)
- [ ] 老黄持 1 份 sign-off
- [ ] 跨司法管辖区 4 区分布
- [ ] BIP39 passphrase 二次加密 (N3 提前)
- [ ] 跨 vendor / 跨 region KMS 副本 (老黄 Risk Memo)

#### IAM (老吴 owns)
- [ ] Sprint-2 启动前 (2026-06-26) IAM JSON spec sign-off
- [ ] CMK key policy + signer role policy 实施
- [ ] CloudTrail + grant 监控
- [ ] break-glass 流程 + 演练

---

## 8. 残留开放问题

(v1 §10 + v2 新增, 重新编号.)

| # | 问题 | 阻塞? | Owner | 求助 | Deadline |
|---|---|---|---|---|---|
| Q1 | **AWS IAM JSON spec** | **是 (Sprint-2 启动)** | 老吴 | — | **2026-06-26** |
| Q2 | EIP-712 Polymarket schema (CTF Exchange v2 / Neg Risk) | 是 | 老李 | S1-002 实测 | Sprint-2 第 1 周 |
| Q3 | Polygon EIP-1559 fee 签名 (signer 还是 trader 算?) | 否 | 老叶 + 老孙 | S1-009 | Sprint-2 |
| Q4 | 多 wallet (主备热-热) 仓位分配 | 否 | 老韩 | RiskManager 集成 | Sprint-2 |
| Q5 | Nitro Enclave 加固 (Plan C, 深度防御) | 否 | 老孙 | — | Sprint-N |
| Q6 | Slack bot 与 Polymarket session token 关联 | 否 | 小苏 | frontend | Sprint-2 |
| Q7 | 季度轮换是否影响 Polymarket API key | 是 | 老李 | S1-002 | Sprint-1 末 |
| Q8 | Shamir 持有人 老黄 持 1 份的法律意见书 | 是 (合规) | 老黄 | — | 2026-06-26 |
| **Q9** | **nonce manager 接口 + SLO (老叶 owns 实现)** | **是 (B7 依赖)** | **老叶** | — | **Sprint-2 第 1 周** |
| **Q10** | **Receiver 白名单合约地址清单 (Polymarket CTF / Neg Risk 准确地址)** | **是 (B5 依赖)** | **老叶 + 老李** | — | **Sprint-2 第 1 周** |
| **Q11** | **B5 typed-data schema 细节 (Polymarket EIP-712 primaryType / 字段)** | **是** | **老李** | — | **Sprint-2 第 1 周** |
| **Q12** | **B8 crate 选型 k256 vs secp256k1 (rust-bitcoin) 性能 + 审计** | 否 | 老张 | benchmark | Sprint-2 第 2 周 |
| **Q13** | **跨 vendor KMS 副本 (GCP / Azure) 老黄 Risk Memo** | 否 | 老黄 | — | Sprint-2 末 |
| **Q14** | signer chaos drill 自动化 (N7 提前) | 否 | 老孙 + 小宋 | testbed | Sprint-3 |

**残留问题数: 14 个** (4 个阻塞 Sprint-2 启动: Q1 IAM, Q9 nonce, Q10 白名单, Q11 typed_data schema; 其余 10 个 Sprint-2 内可逐步关闭).

---

## 附录 A. 核心库选型 (v2 update, B8 强化)

| 用途 | 选型 | 备选 | 供应链评估 |
|---|---|---|---|
| secp256k1 签名 | `k256` (RustCrypto, pure Rust) | `secp256k1` (bitcoin-core C binding) | 两者都有审计 (Trail of Bits k256 audit); pure Rust 减少 unsafe 面 |
| EIP-712 typed data | `alloy-sol-types` | `ethers-rs` (deprecated) | alloy 是 ethers 接任, 活跃维护; 老李 校 schema |
| Keccak256 | `sha3` | `tiny-keccak` | sha3 是 RustCrypto, 大量使用 |
| age 加密 | `age` (rage Rust impl) | `sops` (备选) | age 设计简洁, FiloSottile 维护; 季度 review |
| IPC | unix domain socket + `rmp-serde` (msgpack) | gRPC unix | msgpack 简单稳定 |
| 内存 mlock + zero | `nix` + `secrecy` + `zeroize` | 无 | 三 crate 均 widely-used |
| TLS (KMS) | `rustls` + `webpki` (自定义 verifier) | 无 (不用系统 OpenSSL) | rustls 审计良好, 无依赖系统 TLS lib |
| AWS KMS | `aws-sdk-kms` (官方) | 无 | 官方维护 |
| WebAuthn 校验 (signer 侧) | `webauthn-rs` | — | 待 老沈 + 老张 review (Q12 sub-task) |
| HMAC | `hmac` + `sha2` (RustCrypto) | — | 标准 |
| 审计 WAL | append-only file + fsync + hash chain | sqlite WAL | 自实现, 简单可审 |

**所有 crate Cargo.toml commit hash pin, 升级走 PR + 老沈 review** (B8).

## 附录 B. 参考 (v1 不变, 略)

## 附录 C. 与威胁模型对应 (老沈 §5)

详见 §7.1.

---

*v2 提交时间: 2026-05-28*
*预期老沈 sign-off: 2026-05-29 (24h 内)*
*预期 Sprint-2 启动: 2026-06-26 (受 IAM Q1 + nonce Q9 + 白名单 Q10 + schema Q11 阻塞)*
*v1 保留为 review trail, 不删*
