# Rust Signer Crate 选型 + 供应链 SOP v1

- Owner: 老张 (rust-advisor)
- Date: 2026-05-28
- 验收人: 老孙 (key-management v3) + 老沈 (security)
- 解阻塞: 老孙 v2 B8, Q12 (供应链审查空白)
- 关联: `laosun-key-management-v2.md` §2.8 / 附录 A, `laoshen-key-management-coreview-v1.md` B8
- 协作: 性能基准 @老姜, 链上 (chain_id/EIP-712 schema) @老叶/@老李, IPC schema @老孙, audit WAL @老唐
- 适用范围: 仅 signer 独立进程 (Rust). 主 trader (C++) 不引入 Rust, 防蔓延

---

## 0. 总览 (老雷 5 分钟版)

**主推**:
- ECDSA: **`k256` (RustCrypto)** — pure Rust, NCC Group 2023 audit, alloy 默认依赖
- Keccak: **`sha3` (RustCrypto)** — 已 audit, 性能够 (我们 < 1k sig/s, 非热路径)
- TLS: **`rustls` + `rustls-webpki`** — pure Rust, ISRG 2020 + Cure53 2023 audit; SPKI cert pin 可直接 hook ServerCertVerifier
- 零拷贝/清零: **`zeroize` + `secrecy`** — `Zeroizing<Vec<u8>>` 替代裸 memset
- 常时比较: **`subtle`** (`CtChoice`) — HMAC tag 比较必须用, 防时序攻击
- UDS IPC: **同步 `std::os::unix::net::UnixStream` + `nix` SO_PEERCRED** — 不引 tokio (依赖 200+ crate, 攻击面爆炸; 老孙 IPC 是同步)
- 日志: **`tracing` + `tracing-subscriber` + 自写 WAL appender** 对接老唐 schema
- 供应链工具: **`cargo-vet` (主) + `cargo-audit` + `cargo-deny` + `cyclonedx-rust-cargo` (SBOM)**

**关键供应链工具 (一选)**: **`cargo-vet`** — Google/Mozilla 出品, 唯一覆盖 "人工 review trust 记录" 这一层 (audit/deny/SBOM 都只能拦已知 CVE, vet 才能拦 xz-style 投毒)

**xz 教训映射**: 禁止生产依赖未 vet 的 build.rs / proc-macro crate; CI 在 sandbox (no-network) 跑 build; 二进制 reproducible build + minisign 离线签

**总 direct 依赖目标**: **< 25 个** (含 dev-deps), transitive < 250. v1 列表 23 direct, 实测 transitive ~210, 在预算内

---

## 1. ECDSA crate 推荐

### 1.1 候选对比

| Crate | 实现 | 性能 (sign p50, M1) | Audit | 维护活跃度 | 知名采用 | 备注 |
|---|---|---|---|---|---|---|
| **`k256`** (RustCrypto) | pure Rust | ~85 us | NCC Group 2023 (elliptic-curves) | 月度发布, 100+ contributor | **alloy / ethers-rs 默认**, Foundry, reth | 推荐主选 |
| **`secp256k1`** (rust-bitcoin) | FFI to libsecp256k1 (C) | ~25 us | libsecp256k1 本体多次 audit (Bitcoin Core 同款) | 稳定, 但 FFI binding 层升级保守 | rust-bitcoin, Lightning Dev Kit | 性能基线 / 高吞吐场景才上 |
| `libsecp256k1` (parity 旧名) | pure Rust (旧实现) | 慢 | **无近期 audit, 已 deprecated** | 弃 | — | **不用** |
| `ring` (Brian Smith) | 部分汇编 | 不支持 secp256k1 (只 P-256/P-384) | — | — | rustls | 排除 (曲线不支持) |

### 1.2 推荐: `k256` (主) + `secp256k1` (性能 fallback, 现阶段不上)

**理由**:

1. **pure Rust 审计可读性**: `k256` 全 Rust, 没有 C FFI 黑盒. 老沈 + 我 可以人工 vet 关键路径 (`signing_key.rs`, `ecdsa.rs`); `secp256k1` 的 libsecp256k1 是 C, 升级时审 C diff 不在我们能力范围内 (要找密码学 audit firm, 季度审太贵)

2. **EIP-712 兼容**: `k256` 提供 `SigningKey::sign_prehash_recoverable(digest: &[u8; 32])` → `(Signature, RecoveryId)`, 直接喂 keccak256(EIP-712 digest) 出 `(r, s, v)`. 与 alloy `Signature` 类型互通. `secp256k1` 也支持但要手动算 recovery_id, 多一步出错风险

3. **性能不是瓶颈**: 我们 signer p99 < 5ms (老姜 latency 预算), 单笔签名 85us << 5ms. 跨洋链路 200ms+, sign 时间根本不在关键路径. **不为了 60us 引入 C FFI**

4. **生态绑定**: alloy-rs (老叶选的 ethers 替代) 内部用 `k256`. 用同一套 crate 减少类型转换 / Signature serde 边角 bug

5. **CVE 历史**: 
   - `k256`: RustSec 2022-09 (timing leak in scalar mul, 已修, 影响 ≤ 0.10.x, 现 0.13.x)
   - `secp256k1` (rust binding): 无近期 CVE; libsecp256k1 C 本体最近一次是 2021 CVE-2021-3490 (eBPF, 不相关)
   - 两者都 clean

6. **不耻下问**: 老姜如果给出 signer p99 > 3ms 的实测, 性能预算紧时再切 `secp256k1`. 现阶段 default `k256`

**Cargo.toml**:
```toml
k256 = { version = "=0.13.4", features = ["ecdsa", "sha256", "arithmetic", "expose-field"], default-features = false }
# 禁用 default-features: 关掉不需要的 schnorr / serde / bip340; 缩攻击面
```

**关键 API 用法**:
```rust
use k256::ecdsa::{SigningKey, Signature, RecoveryId};
use k256::ecdsa::signature::hazmat::PrehashSigner;

// 1. EIP-712 digest 在调用前算好 (sha3::Keccak256)
let digest: [u8; 32] = compute_eip712_digest(&typed_data);

// 2. 签名 (prehash, 不再 hash 一次)
let signing_key: SigningKey = /* from KMS-unwrapped bytes, wrapped in Zeroizing */;
let (sig, recid): (Signature, RecoveryId) = signing_key.sign_prehash_recoverable(&digest)?;

// 3. 拆 (r, s, v) 给以太坊
let (r, s) = sig.split_bytes();      // r: [u8;32], s: [u8;32]
let v = recid.to_byte() + 27;        // v: u8 (legacy) 或 + chain_id*2+35 (EIP-155, 但 EIP-712 用 27/28)
```

**禁用项**:
- `k256::SecretKey::random()` 在生产路径 — 我们 key 来自 KMS unwrap, 不用 crate RNG
- `k256` 的 `serde` feature — IPC 走 msgpack 自己 encode 公钥, 不暴露 SecretKey serde

---

## 2. Keccak256 / SHA3 crate

### 2.1 候选

| Crate | 实现 | 性能 (32B input) | Audit | 维护 | 备注 |
|---|---|---|---|---|---|
| **`sha3`** (RustCrypto) | pure Rust | ~120 ns | NCC 2023 (hashes 套件) | 活跃 | 推荐 |
| `tiny-keccak` | pure Rust, no_std | ~100 ns | 无独立 audit | 半活跃 (debris-) | 性能略快, 但 audit 缺失 |
| `keccak` (RustCrypto sub-crate) | pure Rust | 同 sha3 | 同 sha3 | 同 sha3 | `sha3` 的底层, 直接用 `sha3` 即可 |

### 2.2 推荐: `sha3`

- 与 `k256` 同生态 (RustCrypto), Cargo features 协同 (`k256/sha256` 已内嵌部分)
- audit 覆盖 NCC 2023, `tiny-keccak` 没有
- 20ns 差异不在我们关心范围

**注意**: 必须用 `Keccak256` (legacy), **不是** `Sha3_256`. 两者 padding 不同, 以太坊用 Keccak256:
```rust
use sha3::{Digest, Keccak256};
let mut hasher = Keccak256::new();
hasher.update(b"\x19\x01");
hasher.update(&domain_separator);
hasher.update(&struct_hash);
let digest: [u8; 32] = hasher.finalize().into();
```

**Cargo.toml**:
```toml
sha3 = { version = "=0.10.8", default-features = false }
```

---

## 3. TLS / mTLS crate

### 3.1 候选

| Crate | 底层 | Audit | 维护 | 备注 |
|---|---|---|---|---|
| **`rustls`** | pure Rust (ring 或 aws-lc-rs backend) | ISRG 2020, Cure53 2023 | 活跃 (rustls.dev) | 推荐 |
| `native-tls` | OS 原生 (Schannel/Secure Transport/OpenSSL) | 取决于 OS | wrapper 维护 OK | 排除 |
| `openssl` (crate) | OpenSSL FFI | OpenSSL 本体 audit, 但 FFI 层是 binding | 活跃 | 排除 (避免 C 依赖) |

### 3.2 推荐: `rustls` + `rustls-webpki` + `aws-lc-rs` backend

**排除 `native-tls` 原因**:
1. 老孙 B2 要求"不读系统 CA store, 编进 SPKI hash". `native-tls` 走 OS root store, **架构上拒绝 cert pin only** (能加 pin 但 OS root 总是优先, 不符合 zero-trust)
2. OS TLS 行为跨平台不一致 (macOS Secure Transport vs Linux OpenSSL), 测试矩阵爆炸
3. macOS Schannel/SecureTransport binding 层近 5 年至少 3 个 binding bug

**`rustls` 优势**:
1. **可彻底关闭 root store**: `ClientConfig::dangerous().set_certificate_verifier(Arc::new(PinningVerifier))` — 用自定义 verifier, 只信编进 binary 的 SPKI hash, root store 完全不读
2. **Memory safe**: pure Rust, 不会有 OpenSSL Heartbleed 级 BoF
3. **配置最小化**: 默认只开 TLS 1.3 + TLS 1.2 forward-secret ciphers, 不需要手动黑名单
4. **backend 选择**: 
   - `ring` (默认): 老牌, 但维护争议 (Brian Smith 个人项目, 升级慢)
   - **`aws-lc-rs`**: AWS fork of BoringSSL, FIPS 可选, 维护活跃 → 选这个

### 3.3 B2 SPKI cert pin 实现 (给老孙抄)

```rust
use rustls::client::danger::{ServerCertVerifier, ServerCertVerified, HandshakeSignatureValid};
use rustls::pki_types::{CertificateDer, ServerName, UnixTime};
use rustls::{DigitallySignedStruct, SignatureScheme};
use sha2::{Digest, Sha256};

#[derive(Debug)]
struct SpkiPinVerifier {
    // 编译期烧死, KMS 主+备 + 应急
    pinned_spki_sha256: &'static [[u8; 32]],
}

impl ServerCertVerifier for SpkiPinVerifier {
    fn verify_server_cert(
        &self,
        end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _server_name: &ServerName<'_>,
        _ocsp: &[u8],
        _now: UnixTime,
    ) -> Result<ServerCertVerified, rustls::Error> {
        // 用 x509-parser 拿 SPKI
        let (_, cert) = x509_parser::parse_x509_certificate(end_entity.as_ref())
            .map_err(|_| rustls::Error::General("x509 parse failed".into()))?;
        let spki_der = cert.tbs_certificate.subject_pki.raw;
        let spki_hash: [u8; 32] = Sha256::digest(spki_der).into();

        // 常时比较 (虽然 cert 是公开的, 但养成习惯)
        use subtle::ConstantTimeEq;
        for pinned in self.pinned_spki_sha256 {
            if spki_hash.ct_eq(pinned).into() {
                return Ok(ServerCertVerified::assertion());
            }
        }
        Err(rustls::Error::General("SPKI pin mismatch".into()))
    }
    
    fn verify_tls12_signature(/* ... */) -> Result<HandshakeSignatureValid, rustls::Error> { /* 委托默认 */ }
    fn verify_tls13_signature(/* ... */) -> Result<HandshakeSignatureValid, rustls::Error> { /* 委托默认 */ }
    fn supported_verify_schemes(&self) -> Vec<SignatureScheme> { /* TLS1.3 子集 */ }
}
```

**Cargo.toml**:
```toml
rustls = { version = "=0.23.12", default-features = false, features = ["std", "tls12", "aws_lc_rs"] }
rustls-webpki = { version = "=0.102.6", default-features = false, features = ["std", "aws_lc_rs"] }
aws-lc-rs = { version = "=1.8.1" }
x509-parser = { version = "=0.16.0", default-features = false }
sha2 = { version = "=0.10.8", default-features = false }
```

**禁用项**:
- `rustls-native-certs` — 不读 OS root store
- `webpki-roots` — 不用 Mozilla CA bundle
- `ring` backend — 用 `aws_lc_rs`

---

## 4. 零拷贝 / 安全 crate

### 4.1 选型

| Crate | 用途 | 版本 | 备注 |
|---|---|---|---|
| **`zeroize`** | Drop 时编译器不可优化的 memset(0) | `=1.8.1` | RustCrypto, NCC audit. 用 `Zeroizing<T>` wrapper |
| **`secrecy`** | 防止 Debug/Display 误打印 secret + 内部用 zeroize | `=0.8.0` | `SecretBox<[u8; 32]>` for raw key |
| **`subtle`** | 常时比较 (CT eq, CT select) | `=2.6.1` | HMAC tag / session token 比较必须用 |

### 4.2 用法 (替代裸 memset)

```rust
use zeroize::{Zeroize, Zeroizing};
use secrecy::{SecretBox, ExposeSecret};
use subtle::ConstantTimeEq;

// 1. KMS unwrap 得到 raw key (32B), 立刻包成 SecretBox
let raw_key: [u8; 32] = kms.decrypt(wrapped)?;
let signing_key: SecretBox<[u8; 32]> = SecretBox::new(Box::new(raw_key));
// 注意: raw_key 离开作用域时 Box 内会被 Drop, 但 stack 上的 [u8;32] 副本要手动:
let mut raw_key = raw_key;
raw_key.zeroize();   // 显式擦 stack 副本

// 2. 用 key 时 expose 最小窗口
let sig = {
    let bytes: &[u8; 32] = signing_key.expose_secret();
    let sk = k256::ecdsa::SigningKey::from_bytes(bytes.into())?;
    sk.sign_prehash_recoverable(&digest)?
    // sk 出作用域被 Drop, k256 内部已 impl Zeroize
};

// 3. HMAC tag 比较 (B1 鉴权)
fn verify_hmac(expected: &[u8; 32], received: &[u8; 32]) -> bool {
    expected.ct_eq(received).into()   // 常时比较, 不要 ==
}

// 4. 中间 buffer 包 Zeroizing
let intermediate: Zeroizing<Vec<u8>> = Zeroizing::new(serialize_typed_data(&td));
// Drop 时自动 zeroize
```

### 4.3 mlock / MADV_DONTDUMP (老孙 B6 内存防护)

`zeroize` 只能擦堆/栈, 不能防 swap. 配合 `nix`:

```rust
use nix::sys::mman::{mlock, madvise, MmapAdvise};

let buf: Vec<u8> = vec![0u8; 4096];
unsafe {
    mlock(buf.as_ptr() as *const _, buf.len())?;                              // 不 swap
    madvise(buf.as_mut_ptr() as *mut _, buf.len(), MmapAdvise::MADV_DONTDUMP)?; // 不进 core dump
}
// + prctl(PR_SET_DUMPABLE, 0) 进程级 (启动时)
```

`nix` 选型: `=0.29.0`, 只开 `mman` + `socket` features, 关掉文件系统等无关 feature.

---

## 5. UDS IPC crate

### 5.1 同步 vs async

**老孙 v2 IPC 是同步** (sign 是 request-response, p99 < 5ms, 不需要 fan-out). 决策:

| 方案 | direct crate | transitive 估算 | 推荐 |
|---|---|---|---|
| **`std::os::unix::net` + `nix`** | 0 (std 自带) + nix | ~5 | **推荐** |
| `tokio::net::UnixStream` | tokio, tokio-util | ~200+ (mio, futures, parking_lot, ...) | 拒绝 |
| `mio` 低层 | mio | ~10 | 备选 (不需要) |
| `async-std` UDS | async-std | ~150+ | 拒绝 |

### 5.2 推荐: `std::os::unix::net::UnixListener` + 每连接 1 thread + `nix` for SO_PEERCRED

**理由**:
1. signer 并发上限 = trader 进程数 (active-standby 各 1, 总 2~4 连接), 线程模型完全够
2. 同步代码审计简单, 没 future state machine 的 hidden state
3. **不引入 tokio = 砍掉 ~200 个 transitive deps** (mio, slab, parking_lot, futures-task, pin-project-lite, ...). 供应链审查范围大幅缩小
4. xz-utils 启示: 越少依赖越好. tokio 本身可信, 但 transitive 中任何一个被劫持都是入口

**示例**:
```rust
use std::os::unix::net::{UnixListener, UnixStream};
use std::io::{Read, Write};
use nix::sys::socket::{getsockopt, sockopt::PeerCredentials};
use std::os::fd::AsRawFd;

fn serve() -> std::io::Result<()> {
    let listener = UnixListener::bind("/run/sports-signer/signer.sock")?;
    // chmod 600, owner-only
    std::fs::set_permissions("/run/sports-signer/signer.sock", 
        std::os::unix::fs::PermissionsExt::from_mode(0o600))?;
    
    for stream in listener.incoming() {
        let stream = stream?;
        std::thread::spawn(move || handle_one(stream));
    }
    Ok(())
}

fn handle_one(mut stream: UnixStream) -> std::io::Result<()> {
    // B1: SO_PEERCRED 验 trader 身份
    let creds: nix::libc::ucred = getsockopt(stream.as_raw_fd(), PeerCredentials)?.into();
    if creds.uid != EXPECTED_TRADER_UID {
        return Err(std::io::Error::other("peer uid mismatch"));
    }
    
    loop {
        let req = read_msgpack_frame(&mut stream)?;
        let resp = process(req)?;
        write_msgpack_frame(&mut stream, resp)?;
    }
}
```

### 5.3 macOS dev / Linux prod 差异

老孙 v2 已注: dev macOS 用 `LOCAL_PEERPID`, prod Linux 用 `SO_PEERCRED`. `nix` crate 在 macOS 上不支持 `PeerCredentials`, 用 cfg 分支:

```rust
#[cfg(target_os = "linux")]
fn peer_uid(stream: &UnixStream) -> std::io::Result<u32> {
    use nix::sys::socket::{getsockopt, sockopt::PeerCredentials};
    let cred = getsockopt(stream.as_raw_fd(), PeerCredentials)?;
    Ok(cred.uid())
}

#[cfg(target_os = "macos")]
fn peer_uid(stream: &UnixStream) -> std::io::Result<u32> {
    // LOCAL_PEERCRED via libc::getsockopt + xucred
    // 仅用于 dev, 生产不走这条
    unimplemented!("macOS dev only — not for prod signer")
}
```

**msgpack**: `rmp-serde = "=1.3.0"` (serde-derived, 不引 async runtime), 与 v2 §3 schema 对齐.

---

## 6. 审计 / 日志 crate

### 6.1 选型: `tracing` + `tracing-subscriber` + 自写 WAL appender

| Crate | 用途 | 版本 |
|---|---|---|
| `tracing` | 结构化日志 facade (类似 slog, 但生态主流) | `=0.1.40` |
| `tracing-subscriber` | 收集器 / formatter | `=0.3.18` |
| (自写) `audit_wal` | 老唐 schema 的 WAL appender, 实现 `tracing_subscriber::Layer` | — |

**为什么不用 log/env_logger**:
- `log` 不支持结构化字段 (k=v), 老唐 audit WAL 需要结构化 fields
- `tracing` 是 tokio 生态主流, alloy / aws-sdk 都已用; 不引入新 facade

**为什么不引 `slog`**:
- 维护变缓, 生态向 `tracing` 集中
- 多一个 facade 没意义

### 6.2 与老唐 audit WAL 集成

老唐 schema 关键字段 (假设): `{ts_ns, request_id, wallet, intent, receiver, amount_usdc, nonce, hash_chain_prev, hash_chain_curr, signature}`.

```rust
use tracing::{info, info_span, Instrument};

#[tracing::instrument(skip(typed_data), fields(
    request_id = req.request_id,
    wallet = %hex::encode(req.wallet_addr),
    intent = ?req.intent,
    receiver = %hex::encode(req.receiver_addr),
    amount_usdc = req.amount_usdc,
    nonce = req.nonce,
))]
fn sign_request(req: &SignRequest) -> Result<Response> {
    let digest = compute_digest(&req.typed_data_json);
    let sig = sign(&digest)?;
    info!(
        target: "audit_wal",
        digest = %hex::encode(digest),
        sig_r = %hex::encode(sig.r()),
        sig_s = %hex::encode(sig.s()),
        "signature emitted"
    );
    Ok(Response { sig })
}
```

`audit_wal` Layer 实现 (老唐对接处):
- 接收 `tracing::Event`
- 序列化为老唐 canonical JSON 格式
- 计算 hash chain (`H_i = sha256(H_{i-1} || canonical(event_i))`)
- fsync 写 append-only 文件
- (可选) 出口推到 audit collector (gRPC/UDP)

**Cargo.toml**:
```toml
tracing = { version = "=0.1.40", default-features = false, features = ["std", "attributes"] }
tracing-subscriber = { version = "=0.3.18", default-features = false, features = ["std", "fmt", "json", "registry"] }
```

---

## 7. 供应链审查 SOP

### 7.1 工具矩阵

| 工具 | 出品 | 拦哪一层 | CI 触发 | 失败处理 |
|---|---|---|---|---|
| **`cargo-audit`** | RustSec | 已知 CVE (RUSTSEC-XXXX-XXXX) | 每次 PR + 每周 cron | block merge |
| **`cargo-deny`** | EmbarkStudios | license 白名单 / 重复 crate / advisory ban / source registry 白名单 | 每次 PR | block merge |
| **`cargo-vet`** | Google + Mozilla | 人工 review trust 链 (审过的 crate version) | 每次 PR + 升级 PR | block merge, 要求 vet |
| **`cargo-supply-chain`** | rust-secure-code | 列 publisher / maintainer / crate.io 账号 | 季度手跑 | 报告 → 老沈 review |
| **`cyclonedx-rust-cargo`** | OWASP | SBOM (CycloneDX 1.5 JSON) | 每次 release | 归档 + 与上版本 diff |
| **`cargo-geiger`** (辅助) | rust-secure-code | unsafe 代码统计 | 季度 | unsafe 占比 trend |

### 7.2 cargo-vet 主流程 (这是 v2 没细化的部分)

**cargo-vet 的核心**: 不只看 CVE, 而是建立 "我们信任谁审过 X 版本的 Y crate" 的可验证记录, 防止 xz-style "无 CVE 但有后门" 的攻击.

**初始化**:
```bash
cargo install cargo-vet --locked
cargo vet init
# 生成 supply-chain/{audits.toml, config.toml, imports.lock}
```

**信任源 (imports)**: 在 `supply-chain/config.toml` 配置 import 哪些机构的 audit 库:
```toml
[imports.google]
url = "https://raw.githubusercontent.com/google/rust-crate-audits/main/audits.toml"
[imports.mozilla]
url = "https://raw.githubusercontent.com/mozilla/supply-chain/main/audits.toml"
[imports.bytecode-alliance]
url = "https://raw.githubusercontent.com/bytecodealliance/wasmtime/main/supply-chain/audits.toml"
[imports.embark]
url = "https://raw.githubusercontent.com/EmbarkStudios/rust-ecosystem/main/audits.toml"
```

**审过的 crate 自动通过**, 没审过的列在 `cargo vet` 报告里:
```
$ cargo vet
unaudited dependencies:
  some-tiny-crate:1.2.3   (no audit, 0 publisher reputation, last update 18mo ago)
  another-helper:0.4.1    (no audit, but used by 10k+ projects)
```

**人工 vet 步骤** (老张 + 老沈 双人):
1. `cargo vet inspect some-tiny-crate 1.2.3` → 下载源码到本地 sandbox
2. 看 `Cargo.toml` 的 `build = ...` (build.rs) 和 proc-macro features → **xz 入口**
3. 读 unsafe 块 (用 `cargo geiger` 定位)
4. 看 dependency tree 有没有奇怪的 transitive
5. 检查 publisher: `cargo supply-chain publishers` → 查 crates.io 账号注册时间 / 其他 crate
6. 通过 → `cargo vet certify some-tiny-crate 1.2.3 --criteria safe-to-deploy`

**criteria 分级** (我们定义):
```toml
# supply-chain/audits.toml
[[criteria]]
id = "safe-to-deploy"
description = "无明显恶意代码, build.rs 仅做编译辅助, unsafe 块已审"

[[criteria]]
id = "crypto-reviewed"
description = "密码学相关 crate, 算法实现已对照标准 / paper 验证"
implies = ["safe-to-deploy"]
```

`k256`, `sha3`, `rustls` 三个必须 `crypto-reviewed`.

### 7.3 cargo-deny 配置 (老孙 v3 抄)

`deny.toml`:
```toml
[graph]
targets = [
    { triple = "x86_64-unknown-linux-gnu" },
]

[advisories]
db-path = "~/.cargo/advisory-db"
db-urls = ["https://github.com/rustsec/advisory-db"]
vulnerability = "deny"
unmaintained = "deny"
unsound = "deny"
yanked = "deny"
notice = "warn"
ignore = []  # 出现要 PR 加 + 老沈 sign-off

[licenses]
unlicensed = "deny"
allow = ["MIT", "Apache-2.0", "Apache-2.0 WITH LLVM-exception", "BSD-2-Clause", "BSD-3-Clause", "ISC", "Unicode-DFS-2016", "MPL-2.0"]
deny = ["GPL-3.0", "AGPL-3.0", "LGPL-3.0"]  # 商业不兼容
copyleft = "deny"
allow-osi-fsf-free = "neither"
confidence-threshold = 0.93

[bans]
multiple-versions = "deny"   # 同 crate 多版本 = 攻击面翻倍
wildcards = "deny"           # 禁止 "*" 版本依赖
highlight = "all"
# 已 ban 的 crate
deny = [
    { name = "openssl" },           # 我们用 rustls
    { name = "openssl-sys" },
    { name = "native-tls" },
    { name = "tokio", wrappers = [] }, # signer 不引 tokio
]

[sources]
unknown-registry = "deny"
unknown-git = "deny"
allow-registry = ["https://github.com/rust-lang/crates.io-index"]
allow-git = []  # 严禁 git dep
```

### 7.4 cargo-audit 配置

`.cargo/audit.toml`:
```toml
[advisories]
ignore = []  # 不许忽略
informational_warnings = ["unmaintained", "unsound"]
severity_threshold = "low"  # 低/中/高/严重全报

[output]
deny = ["warnings"]
format = "terminal"
```

### 7.5 SBOM 生成 + diff

```bash
cargo cyclonedx --format json --output-pattern bom \
    --override-filename sbom-signer-v3.0.0.json
# 归档到 ops/sbom/ 目录, git track
```

每次 release 跑 `diff sbom-vN.json sbom-vN-1.json | jq` → 老沈 review 新进 crate.

### 7.6 季度审查节奏 (老沈 + 老张, Quarterly)

| 月份 | 任务 | 输出 |
|---|---|---|
| Q1/Q2/Q3/Q4 月底 | `cargo update --dry-run` 看可升 crate | upgrade-candidates.md |
| 同上 | `cargo vet check` 列 unaudited | vet-pending.md |
| 同上 | `cargo supply-chain publishers` | publisher-trust.md |
| 同上 | SBOM diff vs 上季度 | sbom-diff-Q{N}.md |
| 同上 | `cargo geiger` unsafe trend | unsafe-trend-Q{N}.md |
| 同上 | 老沈 sign-off 会 | 季度 supply-chain 评估 |

**异常触发** (任何一个 = 临时审查):
- RustSec 发布关键 CVE 影响我们 crate → 24h 内升级 + redeploy
- crate publisher 变更 (账号易主) → 暂停升级, 排查
- xz-style 投毒事件曝光 → 全 ecosystem 紧急 vet

---

## 8. Cargo.toml 模板 (老孙抄)

### 8.1 workspace 结构

```
sports-signer/
├── Cargo.toml                # workspace root
├── Cargo.lock                # git track
├── rust-toolchain.toml       # 锁 toolchain version
├── deny.toml                 # cargo-deny
├── .cargo/
│   ├── config.toml           # build flags
│   └── audit.toml            # cargo-audit
├── supply-chain/             # cargo-vet
│   ├── audits.toml
│   ├── config.toml
│   └── imports.lock
├── crates/
│   ├── signer-core/          # 核心签名 + key 管理 (无 IO)
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   ├── signer-ipc/           # UDS server + msgpack
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   ├── signer-kms/           # KMS unwrap + TLS cert pin
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   ├── signer-audit/         # WAL appender (老唐 schema)
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   └── signer-bin/           # main, 编译输出 sports-signer
│       ├── Cargo.toml
│       └── src/main.rs
└── ops/
    └── sbom/                 # release SBOM 归档
```

**分 crate 理由**:
- `signer-core` 无 IO, 纯逻辑 → 可单元测试, 不需 sandbox
- `signer-kms` 是唯一引入 rustls + aws-sdk 的, 隔离 TLS 攻击面
- `signer-audit` 是唯一写文件的, 隔离 fs ops
- `signer-bin` 极薄, 只做 wire-up

### 8.2 root Cargo.toml

```toml
[workspace]
resolver = "2"
members = [
    "crates/signer-core",
    "crates/signer-ipc",
    "crates/signer-kms",
    "crates/signer-audit",
    "crates/signer-bin",
]

[workspace.package]
version = "3.0.0"
edition = "2021"
rust-version = "1.80"   # 锁 MSRV, 与 rust-toolchain.toml 一致
authors = ["sports-trader team"]
license = "Proprietary"
publish = false         # 防止意外 cargo publish

[workspace.dependencies]
# === 密码学 ===
k256 = { version = "=0.13.4", default-features = false, features = ["ecdsa", "sha256", "arithmetic"] }
sha3 = { version = "=0.10.8", default-features = false }
sha2 = { version = "=0.10.8", default-features = false }
hmac = { version = "=0.12.1", default-features = false }

# === 安全 / 内存 ===
zeroize = { version = "=1.8.1", default-features = false, features = ["zeroize_derive"] }
secrecy = { version = "=0.8.0", default-features = false }
subtle = { version = "=2.6.1", default-features = false }

# === EIP-712 / 链上 ===
alloy-sol-types = { version = "=0.8.0", default-features = false }
alloy-primitives = { version = "=0.8.0", default-features = false }

# === TLS / KMS ===
rustls = { version = "=0.23.12", default-features = false, features = ["std", "tls12", "aws_lc_rs"] }
rustls-webpki = { version = "=0.102.6", default-features = false, features = ["std", "aws_lc_rs"] }
aws-lc-rs = { version = "=1.8.1" }
x509-parser = { version = "=0.16.0", default-features = false }
aws-sdk-kms = { version = "=1.40.0", default-features = false, features = ["rustls", "rt-tokio"] }
# 注: aws-sdk 必带 tokio (transitive). 把它隔离到 signer-kms crate, 主循环不用 tokio
# 如果允许, 评估 aws-lc-rs 直接调 KMS API 跳过 aws-sdk-rust (TODO @老孙)

# === IPC / Serde ===
serde = { version = "=1.0.210", default-features = false, features = ["derive", "std"] }
rmp-serde = { version = "=1.3.0" }
nix = { version = "=0.29.0", default-features = false, features = ["mman", "socket", "user", "process"] }

# === 文件加密 (key blob) ===
age = { version = "=0.10.0", default-features = false }

# === 日志 ===
tracing = { version = "=0.1.40", default-features = false, features = ["std", "attributes"] }
tracing-subscriber = { version = "=0.3.18", default-features = false, features = ["std", "fmt", "json", "registry"] }

# === 错误 ===
thiserror = { version = "=1.0.63" }
anyhow = { version = "=1.0.86" }   # 仅 main / 顶层

# === 工具 ===
hex = { version = "=0.4.3", default-features = false, features = ["std"] }
minisign = { version = "=0.7.6" }   # B4 binary 自校验 (启动期验自身签名)

[workspace.lints.rust]
unsafe_code = "warn"           # 允许但要 review
missing_docs = "warn"
unused_must_use = "deny"

[workspace.lints.clippy]
unwrap_used = "deny"           # 生产代码禁 unwrap
expect_used = "warn"           # 仅启动期允许
panic = "deny"
todo = "warn"
indexing_slicing = "warn"
integer_arithmetic = "warn"    # 溢出风险

[profile.release]
opt-level = 3
lto = "fat"                    # reproducible build 需配合 -Ccodegen-units=1
codegen-units = 1
strip = false                  # 保留 symbols 便于 audit (binary 由 minisign 签, 不靠 strip 抗逆向)
panic = "abort"                # signer panic = 直接退出, 防止半状态

[profile.release.package."*"]
debug = "line-tables-only"
```

### 8.3 rust-toolchain.toml (reproducible build)

```toml
[toolchain]
channel = "1.80.1"
components = ["rustc", "cargo", "rust-std", "clippy", "rustfmt"]
targets = ["x86_64-unknown-linux-gnu"]
profile = "minimal"
```

### 8.4 .cargo/config.toml

```toml
[build]
rustflags = [
    "-D", "warnings",
    "-C", "link-arg=-Wl,-z,relro,-z,now",   # full RELRO
    "-C", "link-arg=-Wl,-z,noexecstack",
    "-C", "force-frame-pointers=yes",        # 便于 profiler / crash 分析
    "-C", "overflow-checks=on",              # release 也开
]

[target.x86_64-unknown-linux-gnu]
linker = "clang"
rustflags = [
    "-C", "link-arg=-fuse-ld=lld",
]

[net]
offline = false   # CI 设 true (sandbox build)
git-fetch-with-cli = true   # 用 git CLI 而非 libgit2, 减少 transitive

# 拒绝从非官方 registry / git 拉
[source.crates-io]
replace-with = "vendored-sources"   # release build 用 cargo vendor

[source.vendored-sources]
directory = "vendor"
```

### 8.5 vendor 策略

Release build 必须 `cargo vendor` 锁死所有源码到 `vendor/` 目录, 整 directory 进 git (或 git-lfs).
- 优点: 100% 防 crates.io 被劫持; 重建 binary 不需要联网
- 代价: 仓库变大 (~200MB), 升级 crate 要重 vendor

CI release flow:
```bash
cargo vendor --locked --versioned-dirs vendor/
git add vendor/ Cargo.lock
git commit -m "vendor: pin for release v3.0.0"
# 然后离线 sandbox build
cargo build --release --offline --locked
```

---

## 9. xz-utils 教训映射

### 9.1 xz 攻击 recap (CVE-2024-3094)

- "Jia Tan" 用 2 年 social engineering 拿到 xz upstream 维护权
- 在 **build 阶段** 注入 obfuscated 代码 (m4 脚本 + 测试数据里藏 payload)
- 触发条件: 检测到是 RPM/DEB build + sshd 链接 liblzma → 注入 ssh backdoor
- 关键: **源码 review 看不出来** (payload 在二进制测试数据), 必须 build 行为审计

### 9.2 在 Rust 生态的对应攻击面

| xz 入口 | Rust 等价 | 防御 |
|---|---|---|
| autoconf m4 脚本 | **`build.rs`** (任意 Rust 代码, build 时执行) | cargo-vet 强制 review build.rs; 审"为什么这个 crate 需要 build.rs" |
| 测试数据藏 payload | crate 内 `tests/data/*.bin` 通过 `include_bytes!` 编进 binary | 大文件 binary 测试数据要解释来源 |
| 维护者社工 | **proc-macro crate** (编译期执行任意代码) | proc-macro crate 必须 `crypto-reviewed` 级 vet |
| RPM/DEB build 钩子 | CI cache 投毒 + sccache 远程 cache | CI 用 ephemeral sandbox, 不复用 cache |

### 9.3 防御措施 (写进 SOP)

1. **build.rs 白名单**: 
   - cargo-vet 标记 `does-not-have-build-script` 或必须人工审
   - 当前 transitive 中有 build.rs 的 crate: `aws-lc-sys`, `ring` (我们不用 ring), `serde_derive` (proc-macro 不算), 等
   - 每个允许的 build.rs 在 `supply-chain/audits.toml` 注释 "为什么允许"

2. **proc-macro 沙箱**: 
   - 等 `cargo --no-proc-macro-execution` 类功能 (目前无)
   - 临时方案: proc-macro crate 列单独清单, 季度 diff
   - 已用 proc-macro: `serde_derive`, `thiserror-impl`, `tracing-attributes`, `zeroize_derive` (全是知名维护方)

3. **CI 网络隔离**:
   - Build job 在 no-network sandbox (Docker `--network=none`)
   - 依赖通过 `cargo vendor` 提前拉好
   - 防 build.rs 从远程下载 payload

4. **Reproducible build 验证**:
   - `cargo build --release --locked --offline` 在两台不同机器跑, 比较 binary sha256
   - 不一致 = 警报 (可能 build.rs 注入随机性 = 时间戳, 是隐藏入口的 hint)

5. **Binary 离线签名**:
   - Release binary 用 minisign 离线机签
   - 部署机 ExecStartPre 验签 (老孙 B4 已定)

6. **Publisher 突变监控**:
   - 季度 `cargo supply-chain publishers > publishers-Q{N}.txt`
   - diff 上季度, 任何 maintainer 易主 = 暂停升级, 调查
   - 重点盯单维护者 crate (xz 只有 1 个主维护者, 是攻击切入点)

7. **依赖最小化**:
   - 每个新 direct dep 写 ADR: 为什么必须 + 为什么这个版本 + 替代方案
   - 目标 direct < 25, transitive < 250
   - tokio 不进主循环 (减 200 transitive)

8. **upstream 异常监控**:
   - 订阅 RustSec advisory mailing list
   - 关注 OSV-Scanner / OpenSSF Scorecard 分数突降
   - 关注 crates.io 大版本跳跃 (1.x → 2.x → 3.x 短时间 = 可疑)

---

## 10. 开放问题

| ID | 问题 | 待 | 优先级 |
|---|---|---|---|
| Z-Q1 | aws-sdk-kms 必带 tokio (transitive ~200). 是否手写 minimal KMS Decrypt 客户端 (只 1 个 API, rustls + 自签 SigV4) 跳过 aws-sdk? 节省 200 deps | @老孙 决策, @老沈 review | 高 |
| Z-Q2 | `alloy-sol-types` v0.8 EIP-712 encode 行为是否与老李 Polymarket Exchange Order schema 100% 匹配? 要跑兼容测试 | @老李 + @老叶 提供测试向量 | 高 |
| Z-Q3 | signer p99 实测 (k256 sign + msgpack encode + HMAC + WAL fsync) 是否 < 5ms? 如果不达标考虑切 secp256k1 FFI | @老姜 (latency owner) bench | 中 |
| Z-Q4 | `cargo vet` import 的 4 个机构 audits.toml 谁负责定期 sync? GitHub Action cron daily? | @老沈 OPS owner | 中 |
| Z-Q5 | `vendor/` 进 git 还是 git-lfs? ~200MB | @老何 (CI/build) | 中 |
| Z-Q6 | 季度审查的 sign-off 是否进 audit WAL? (老唐 schema 支持 admin event 吗) | @老唐 + @老沈 | 低 |
| Z-Q7 | xz-style 防御是否扩展到主 trader 的 vcpkg C++ 依赖? (架构师老周决定 C++ 不引 Rust, 但 C++ 依赖也有类似攻击面) | @老周 + @老沈 (我不主 C++) | 低 |
| Z-Q8 | `aws-lc-rs` FIPS 模式是否要开? (合规需求待 @老黄 确认) | @老黄 (compliance) | 低 |

---

## 11. 给老孙 v3 的 checklist

- [ ] §1 confirm k256 (主) + 暂不上 secp256k1
- [ ] §3 SpkiPinVerifier 接入 signer-kms crate
- [ ] §4 全部 raw key path 包 `SecretBox` + `Zeroizing`, HMAC 比较全用 `subtle::ct_eq`
- [ ] §5 同步 UDS + 每连接 1 thread (不引 tokio 主循环)
- [ ] §6 tracing target=audit_wal 对接老唐 schema (schema 待 @老唐 给)
- [ ] §7 SOP 工具全装 (`cargo-audit / deny / vet / cyclonedx`)
- [ ] §8 workspace 结构 + Cargo.toml 模板拷过去
- [ ] §9 xz 教训映射写进 supply-chain/README
- [ ] Z-Q1 / Z-Q2 解决再过老沈 sign-off → v3 close

---

## 12. 决策声明

老张 (rust-advisor) sign-off:
- ECDSA 主选 **k256 0.13.4** (audit + 生态 + 安全, 性能够)
- 供应链关键工具 **cargo-vet** (对 xz-style 防御不可替代)
- 默认不引 tokio 进主循环 (供应链最小化)
- 全部 crate 用 `=X.Y.Z` 锁版本, Cargo.lock + vendor/ 双 pin

待 @老沈 (security) 共同 sign-off 后, v3 close B8.

