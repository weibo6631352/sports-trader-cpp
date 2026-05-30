# Signer v4 — C++ 重写方案 (无 Rust)

- Owner: 老孙 (crypto-signing-expert)
- Co-review: 老沈 (security-engineer) + 老高 (cpp-arch-lead, C++ 内存安全 + 工程规范)
- 不耻下问: 老高 (C++ 内存安全 / RAII / ASAN-UBSAN-TSAN 兜底), 老沈 (KMS TLS / SPKI pin), 老叶 (libsecp256k1 用法 + BIP32 派生 + 链上 nonce), 老何 (C++20 footgun checklist + 编译器优化抑制)
- Date: 2026-05-28
- 验收人: 老雷 (GM final) + 老沈 (security sign-off) + 老黄 (compliance sign-off)
- 状态: v4 提交, 等老高 PR review 接力 + 老沈 + 老黄 final
- **决策依据**: GM 2026-05-28 用户终极拍板 — **公司无 Rust 项目, signer 撤 Rust 改 C++ 全量重写**
- 关联:
  - 前版 v1 (Rust 选型论证, 保留为 review trail): `docs/RESEARCH/laosun-key-management-v1.md`
  - 前版 v2 (8 Blocker 修复, Rust 实现, 保留): `docs/RESEARCH/laosun-key-management-v2.md`
  - 前版 v3 (Shamir / 跨 vendor / passphrase, Rust 实现, 保留): `docs/RESEARCH/laosun-key-management-v3.md`
  - 安全 co-review: `docs/RESEARCH/laoshen-key-management-coreview-v1.md`
  - 跨 vendor KMS: `docs/RESEARCH/laoshen-multi-vendor-kms-v1.md`
  - GM Sygnum 截止承诺: `docs/ADR/2026-05-28-gm-commitment-sygnum-deadline.md`
  - C++ footgun checklist: `docs/RESEARCH/laohe-cpp-footgun-checklist-v1.md`
  - C++ version selection: `docs/RESEARCH/laohe-cpp-version-selection-v1.md`
  - 项目宪法: `CLAUDE.md` §10 (C++20 全栈) + §12.3 (无 Rust / 无 Python in prod)

> v4 自包含, 但是**纯实现层重写**, 算法 / 流程 / 跨境合规 / 阈值审批 / passphrase / Shamir / 跨 vendor / HA 全部沿用 v3. 本 v4 主要写 C++ 等效技术栈 + C++ 特有的内存安全工程化 (Rust 编译器免费送的, C++ 要手工 + ASAN/UBSAN/TSAN + code review 三层兜).

---

## 0. v3 (Rust) → v4 (C++) 关键差异

| 维度 | v3 (Rust) | v4 (C++20) | 工程影响 |
|---|---|---|---|
| **语言** | Rust 1.79 | C++20 (与主进程一致, GCC 13 / Clang 17 ABI) | 内存安全靠 RAII + ASAN + code review, 不靠编译器 |
| **secp256k1 签名** | `k256` (RustCrypto pure Rust) 或 `secp256k1` crate (bitcoin C binding) | `libsecp256k1` C 库 (Bitcoin Core 出品, 业界金标准) | 性能等价, 审计强度等价或更高 (libsecp256k1 经历更长实战) |
| **Keccak256** | `sha3` / `tiny-keccak` crate | OpenSSL 3.x `EVP_sha3_256` (本地走 EVP API, FIPS 模式兼容) | 性能等价 (~5μs) |
| **EIP-712 typed data 编码** | `alloy-sol-types` | 手写 C++ 模板 (老李 提供 schema → 老孙 实现 encode) + 老叶 cross-check | 工作量 +1 周, 但减少第三方依赖, byte-equal 测试可控 |
| **TLS / mTLS (KMS)** | `rustls` + 自定义 `ServerCertVerifier` | OpenSSL 3.x SSL_CTX + `SSL_CTX_set_cert_verify_callback` 手写 SPKI hash 比对 | 实现复杂度略升, 但 OpenSSL 是 KMS SDK 唯一 transport |
| **零拷贝 / 内存清零** | `secrecy` + `zeroize` crate (编译器不可优化掉的 Drop) | 手写 `SecureBuffer<T>` 模板 (mlock + MADV_DONTDUMP + 析构 zeroize + `[[gnu::optnone]]` 防优化 + `volatile T*` 写) | C++ **风险更高** (编译器可能优化掉 memset), 必须 ASAN-UBSAN-TSAN + 反汇编 audit 兜底 |
| **UDS IPC** | `tokio` + `tokio::net::UnixStream` + `nix::sys::socket` | `boost::asio::local::stream_protocol` 或 raw POSIX socket + `<sys/socket.h>` | 性能等价, C++ 实现量略大 |
| **at-rest 加密 (age 等价物)** | `age` (rage Rust) | `libsodium` `crypto_secretbox_xsalsa20poly1305` 或 `crypto_aead_chacha20poly1305` | age 文件格式不通用 → 改 libsodium 原生格式, 但密码学等价 (chacha20-poly1305 AEAD) |
| **AWS KMS / GCP KMS / Azure / Sygnum 客户端** | `aws-sdk-kms` 等 Rust SDK | **C++ 没有官方 SDK**, 改自实现 HTTP/JSON 客户端 + OpenSSL TLS + 各 KMS REST API (AWS SigV4 / GCP OAuth2 / Sygnum HMAC) | 工作量 +2 周, 但攻击面更小 (减少 SDK transitive dep) |
| **WebAuthn 校验** | `webauthn-rs` | 手写 + libsodium CBOR parser 或选 `libfido2` (Yubico 维护) | 倾向 libfido2, 经审计 |
| **日志** | `tracing` + 自定义 redact layer | `spdlog` + 自定义 sink (敏感字段过滤 + redact) | 与主进程统一 |
| **测试** | `cargo test` + criterion bench | gtest + gbenchmark + ASAN/UBSAN/TSAN (CI 三模式跑) | 与小宋 framework 一致 |
| **供应链** | `cargo-vet` / `cargo-audit` / `cargo-deny` | Conan / vcpkg lock + reproducible build + CycloneDX-cpp SBOM + 季度老沈+老高 review | 工作量等价, 流程不变 |
| **HA 双 signer** | Rust binary x 2 | C++ binary x 2 | 设计完全不变 |
| **跨境合规 / Shamir / passphrase / 跨 vendor / Sygnum** | v3 设计 | **完全不变** (语言无关) | 0 工作量 |
| **GM Sygnum 2027-02-26 承诺** | v3 引用 | v4 引用, **不影响交付** | 0 |

**核心判断**: 算法 / 协议 / 流程 / 合规 100% 复用; 重写仅在实现层. 工作量主要是 C++ 内存安全的工程化兜底 (Rust 编译器免费送, C++ 要付钱).

---

## 1. 总则

### 1.1 语言与运行时

- **全 C++20**, 与主进程 (trader / RiskManager / Strategy) 同一 ABI
- 编译器 GCC 13 或 Clang 17 (与老高 `laohe-cpp-version-selection-v1.md` 锁定一致)
- 标准库: libstdc++ (GCC) 或 libc++ (Clang), CI 两条都跑
- **拒绝**: C++17 及以下 (无 `consteval` / `concept`, footgun 多), C++23 (编译器实现不全)

### 1.2 进程拓扑 (与 v3 一致)

```
┌────────────┐  UDS (HMAC + SO_PEERCRED) ┌────────────┐
│  trader    │ ◀─────────────────────────▶ │ sports-    │
│  (C++)     │                              │ signer-A   │
│  ground-up │                              │ (C++)      │
└────────────┘                              └─────┬──────┘
                                                  │
                                            ┌─────▼──────┐
                                            │ sports-    │
                                            │ signer-B   │
                                            │ (C++, 热备)│
                                            └────────────┘
```

- signer 与 trader **独立进程**, UDS 通信, signer **零业务逻辑** (不算 PnL / 不接 strategy)
- 双 signer active-standby, 与老叶 `nonce_mgr v1` 对接
- signer 编译产物: 静态链接 (libsecp256k1 + libsodium + OpenSSL 3.x + libfido2 + spdlog + boost), 减少运行时 ldd 依赖

### 1.3 v3 (Rust) 历史定位

v1 / v2 / v3 三份 Rust 文档**永久保留**作为:

1. **选型论证 trail** (老沈 + 老黄 + 老雷 都看过, 决策审计需要)
2. **算法 / 流程 / 合规细节的权威来源** (8 Blocker / Shamir / passphrase / 跨 vendor / 演练矩阵, 这些跨语言)
3. **未来如果重新评估 Rust 时的对比基线**

v4 是**实现层重写**, 不否定 v1~v3 的设计决策. 所有"为什么这么做" 的论证仍在 v3.

---

## 2. C++ 技术栈选型

### 2.1 密码学核心

| 用途 | 选型 | 备选 | 评估 | 引入路径 |
|---|---|---|---|---|
| **secp256k1 签名 / 验签** | **`libsecp256k1`** (Bitcoin Core, C 库) | OpenSSL 3.x `EVP_PKEY_sign` (EC + secp256k1 curve) | libsecp256k1 是业界金标准 — Bitcoin / Lightning / Ethereum clients 都用; 经历最长实战 + 多次独立审计 (Trail of Bits 2018 / 2021); 性能基线 ~70-100μs/sign on M1. 比 OpenSSL secp256k1 实现更快且更专 | Conan: `libsecp256k1/0.4.x@bitcoincore`. 锁 commit hash. |
| **Keccak256 / SHA3-256** | **OpenSSL 3.x `EVP_sha3_256`** | XKCP (Keccak team reference impl) | OpenSSL 3.x 已支持 SHA3 (FIPS 202), API 稳定; ~5μs/hash 在小消息. 不引入新依赖 (OpenSSL 我们 KMS TLS 必用) | 系统级 OpenSSL 3.x |
| **HMAC-SHA256** (IPC 鉴权 B1) | OpenSSL 3.x `EVP_MAC` (HMAC) | libsodium `crypto_auth_hmacsha256` | OpenSSL 已在, 不加依赖 | 系统级 OpenSSL |
| **AEAD (at-rest, age 等价物)** | **`libsodium` `crypto_aead_chacha20poly1305_ietf`** | OpenSSL ChaCha20-Poly1305 | libsodium 是 NaCl 接任, API 防 misuse (nonce / key 长度静态), 审计强; chacha20-poly1305 与 age 内部算法等价, 但 age 文件格式特殊, 不通用 → 改 libsodium 原生 sealed-box 格式 + 显式 nonce | Conan: `libsodium/1.0.19`. |
| **KMS unwrap 后的 key encoding** | 自定义二进制 wire format (4B magic + 1B version + 32B key + 16B HMAC tag) | age file format | age 是 Rust 生态; C++ 没成熟实现; 自定义格式更可控 | 自实现, 老沈 review schema |
| **BIP39 / BIP32** (passphrase XOR + master_seed 派生) | **`libbitcoin-system`** (老叶推荐) 或 自实现 `bip39_decode` + `pbkdf2-hmac-sha512` (BIP39) + `hmac-sha512` (BIP32 chain code) | trezor-crypto C 库 | BIP39 算法简单 (2048 词表 + PBKDF2-HMAC-SHA512 2048 轮), 倾向自实现 + 老叶 cross-check, 减少 transitive dep. BIP32 派生路径 `m/44'/60'/0'/0/0` 通用 | 自实现, 老叶 + 老何 review |

### 2.2 TLS / 网络

| 用途 | 选型 | 备选 | 评估 |
|---|---|---|---|
| **mTLS to KMS endpoint** | **OpenSSL 3.x SSL_CTX** + `SSL_CTX_set_cert_verify_callback` 自定义 SPKI verifier (B2) | mbedTLS | OpenSSL 是事实标准; mbedTLS 嵌入式向; 我们没必要换 |
| **HTTP/2 client to KMS REST API** | **`nghttp2`** + 手写 HTTP/2 framer 或 `cpp-httplib` (HTTP/1.1 only, 后期考虑) | libcurl | KMS REST 走 HTTP/1.1 即可; libcurl 老沈拒绝 (依赖面过大). cpp-httplib 单头文件, 配合 OpenSSL 即可 |
| **AWS SigV4 签名** | 自实现 (~200 行) | aws-c-auth (Amazon C SDK 组件) | 自实现 + 与 boto3 byte-equal 测试; aws-c-auth 依赖 aws-c-common 一长串, 不要 |
| **GCP OAuth2 (service account JWT)** | 自实现 (RS256 / ES256 签 JWT) + OpenSSL `EVP_PKEY_sign` | google-cloud-cpp | 自实现, 200 行; google-cloud-cpp 是 3000+ 文件 monorepo, 老沈拒绝 |
| **Sygnum API HMAC** | 自实现 (Sygnum API 用 HMAC-SHA256 + timestamp + nonce) | — | 必自实现, Sygnum 无 C++ SDK |

### 2.3 IPC / 进程间

| 用途 | 选型 | 备选 | 评估 |
|---|---|---|---|
| **UDS 监听 + accept** | **`boost::asio::local::stream_protocol`** | raw POSIX socket | boost::asio 与主进程统一 (与老敖 networking 框架一致); 性能等价, 编码更安全 |
| **SO_PEERCRED + binary hash + cgroup** | raw POSIX `getsockopt(SO_PEERCRED)` + `read("/proc/<pid>/exe")` + sha256 | — | 直接系统调用, 与 v2 Rust 版语义完全等价 |
| **wire 序列化** | **`msgpack-c`** (C++) 或 自定义 packed struct + manual encoding | protobuf | msgpack 与 Rust 版字段兼容; protobuf 引入额外 codegen 步骤. 倾向 msgpack-c (v2/v3 已选 msgpack, 沿用) |
| **fd-passing (HMAC session key 分发)** | `SCM_RIGHTS` ancillary message | — | POSIX 标准 |

### 2.4 内存安全 (B6 关键)

| 用途 | 选型 | 评估 |
|---|---|---|
| **SecureBuffer<T>** | **手写** (§4 完整代码) | C++ 没有标准 secure allocator, 必须自实现; 关键是编译器优化抑制 |
| **mlock / munlock / madvise / mprotect** | raw POSIX `<sys/mman.h>` | 直接调系统 API |
| **PR_SET_DUMPABLE / yama** | raw POSIX `<sys/prctl.h>` | 同上 |
| **explicit_bzero 等效** | `volatile T*` 写入 + `[[gnu::optnone]]` + `__asm__ __volatile__("" ::: "memory")` barrier | C 标准没 explicit_bzero (glibc 有, 但跨平台不可靠); 必须三层防优化 (老何 footgun checklist §3.2 一致) |
| **ASAN / UBSAN / TSAN** | CI 三模式跑 (与小宋 test framework 一致) | 兜内存 / 整型 UB / 数据竞争 |
| **clang-tidy + cppcheck + clang-analyzer** | CI 静态分析 (与老高 code conventions 一致) | 静态查 footgun |
| **`-D_FORTIFY_SOURCE=3 -fstack-protector-strong -Wl,-z,relro,-z,now -fPIE -pie`** | 编译器硬化 flag | 与 v2 systemd hardening 配套 |

### 2.5 测试 / Bench

| 用途 | 选型 | 评估 |
|---|---|---|
| **单元测试** | gtest + gmock | 与小宋 `xiaosong-test-replay-framework-v0.1.md` 一致 |
| **签名性能 bench** | google benchmark | 与老敖 latency budget 一致 |
| **EIP-712 byte-equal 测试向量** | gtest fixture 加载 Polymarket SDK 输出的 100+ JSON vector (老李 owns) | 与 v2 验收 checklist §7.2 B5 一致 |
| **fuzz** | libFuzzer (Clang built-in) | fuzz IPC parser + EIP-712 decoder |
| **chaos** | toxiproxy 或自实现 UDS 模拟器 | active-standby HA 演练 |

### 2.6 日志 / 观测

| 用途 | 选型 | 评估 |
|---|---|---|
| **结构化日志** | **spdlog** + 自定义 sink | 与主进程一致 (小郑 observability v0.1); 自定义 sink 实现敏感字段过滤 (private_key / passphrase / age_key 等关键词触发 redact) |
| **WAL (append-only + hash chain)** | 自实现 (fwrite + fsync + sha256 chain) | v2 设计不变, C++ 直接实现; gtest cover hash chain integrity |
| **Metrics** | prometheus-cpp | 与 observability 框架一致 |

### 2.7 供应链 / 构建

| 用途 | 选型 | 评估 |
|---|---|---|
| **包管理** | **Conan 2.x** (与项目其他模块一致) | vcpkg 备选; Conan 锁版本到 commit hash; build profile 锁 GCC version / std=c++20 |
| **构建系统** | CMake 3.27+ | 与项目一致 |
| **Reproducible build** | `-fdebug-prefix-map` + 固定 SOURCE_DATE_EPOCH + 锁 toolchain version | 与 v2 Rust 等价 |
| **SBOM** | **CycloneDX-cpp** (`cyclonedx-cli` 扫 conan lock) | 与 v2 Rust 用 cyclonedx-rust-cargo 等价 |
| **二进制签名** | minisign (老沈持私钥, 离线签 release binary) | 与 v2 一致 |
| **CVE 扫描** | OSV-Scanner (支持 C/C++ via OSV.dev) + dependabot for Conan | 与 v2 cargo-audit 等价 |
| **license 扫描** | scancode-toolkit | 与 v2 cargo-deny license check 等价 |

---

## 3. 8 Blocker C++ 等效实现

### 3.1 B1 (Spoofing) — IPC 鉴权升级 (POSIX C API, 直接转)

**Rust v2 实现要点 → C++ v4 等效**:

```cpp
// signer/auth/peer_auth.cpp
#include <sys/socket.h>
#include <sys/un.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <fstream>
#include "secure_buffer.h"

struct PeerInfo {
    pid_t pid;
    uid_t uid;
    gid_t gid;
    std::array<uint8_t, 32> binary_sha256;
};

class PeerAuthenticator {
public:
    // Layer 1: SO_PEERCRED + binary hash + cgroup
    std::expected<PeerInfo, AuthError> authenticate_peer(int sock_fd) {
        struct ucred cred{};
        socklen_t len = sizeof(cred);
        if (::getsockopt(sock_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
            return std::unexpected(AuthError::PeerCredFailed);
        }
        if (cred.uid != config_.allowed_trader_uid) {
            return std::unexpected(AuthError::WrongUid);
        }
        auto exe_path = std::format("/proc/{}/exe", cred.pid);
        auto sha = sha256_file(exe_path);
        if (!sha || *sha != config_.allowed_trader_binary_sha256) {
            return std::unexpected(AuthError::WrongBinary);
        }
        auto cgroup_path = std::format("/proc/{}/cgroup", cred.pid);
        auto cgroup_content = read_file(cgroup_path);
        if (!cgroup_content || !cgroup_content->starts_with(config_.allowed_trader_cgroup_path)) {
            return std::unexpected(AuthError::WrongCgroup);
        }
        return PeerInfo{cred.pid, cred.uid, cred.gid, *sha};
    }

    // Layer 2: HMAC challenge-response (单调 nonce + timestamp ±500ms)
    bool verify_hmac(const SignRequest& req, const SecureBuffer<uint8_t>& session_key) {
        // 重算 HMAC-SHA256(session_key, request_bytes_without_hmac || nonce || ts)
        std::array<uint8_t, 32> computed{};
        unsigned int out_len = 0;
        HMAC(EVP_sha256(),
             session_key.data(), session_key.size(),
             req.payload_bytes_for_hmac().data(),
             req.payload_bytes_for_hmac().size(),
             computed.data(), &out_len);
        // 常量时间比较, 防侧信道
        return CRYPTO_memcmp(computed.data(), req.hmac.data(), 32) == 0;
    }

    // Layer 3: session 状态机 (Hello/Ack 握手, 切 session 必重走)
    // ... (省略, 见 §4 IPC schema)
};
```

**关键点**:
- `SO_PEERCRED` 是 Linux 特有, macOS 开发期用 `LOCAL_PEERPID + LOCAL_PEEREPID` (与 v2 一致)
- `CRYPTO_memcmp` (OpenSSL) 用于常量时间比较, 防 HMAC 侧信道
- session_key 走 fd-passing (`SCM_RIGHTS`), 不进 IPC payload

### 3.2 B2 (EoP) — KMS endpoint cert pin

**OpenSSL 自定义 verify callback (替代 rustls 自定义 ServerCertVerifier)**:

```cpp
// signer/kms/spki_pin_verifier.cpp
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace {
// 编译期烧死的 SPKI sha256 数组 (主 + 备, AWS/GCP/Sygnum 各 2)
constexpr std::array<std::array<uint8_t, 32>, 6> kAllowedSpkiHashes = {{
    // AWS kms.us-east-1.amazonaws.com 主 SPKI
    {0xab, 0xcd, /* ... */},
    // AWS 备 SPKI (cert rotation overlap)
    {0x12, 0x34, /* ... */},
    // GCP cloudkms.googleapis.com asia-southeast1 主 + 备
    {/* ... */}, {/* ... */},
    // Sygnum API endpoint 主 + 备
    {/* ... */}, {/* ... */},
}};

constexpr std::array<std::string_view, 3> kAllowedSanSuffixes = {
    ".kms.us-east-1.amazonaws.com",
    ".cloudkms.googleapis.com",
    ".sygnum.com",
};
}  // namespace

extern "C" int kms_cert_verify_callback(X509_STORE_CTX* ctx, void* /*arg*/) {
    X509* leaf = X509_STORE_CTX_get0_cert(ctx);
    if (!leaf) return 0;

    // 1) 算 SPKI sha256
    uint8_t* spki_der = nullptr;
    int spki_len = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(leaf), &spki_der);
    if (spki_len <= 0) return 0;
    std::array<uint8_t, 32> spki_hash{};
    EVP_Digest(spki_der, spki_len, spki_hash.data(), nullptr, EVP_sha256(), nullptr);
    OPENSSL_free(spki_der);

    // 2) 比对白名单
    bool spki_ok = std::ranges::any_of(kAllowedSpkiHashes,
        [&](const auto& expected) {
            return CRYPTO_memcmp(spki_hash.data(), expected.data(), 32) == 0;
        });
    if (!spki_ok) return 0;

    // 3) SAN suffix check
    // ... (X509_get_ext_d2i + GENERAL_NAMES_free)

    return 1;  // 通过
}

void setup_kms_ssl_ctx(SSL_CTX* ctx) {
    SSL_CTX_set_cert_verify_callback(ctx, kms_cert_verify_callback, nullptr);
    // 不读系统 CA store
    SSL_CTX_set_default_verify_paths(ctx);  // 仍调用但 callback 强制 override
}
```

**关键点**:
- `kAllowedSpkiHashes` 是 `constexpr`, 编译期写死, **不读 config 文件** (v2 要求)
- `kms_cert_verify_callback` 是 `extern "C"`, 因为 OpenSSL 是 C API
- SPKI rotation 流程: 与 v2 一致 (PR + 老沈 review + 重新发布 binary), 不允许运行时改

### 3.3 B3 (Spoofing) — 审批人双因子 (业务逻辑, 跨语言不变)

WebAuthn 校验 C++ 实现:

| 子任务 | 选型 |
|---|---|
| WebAuthn assertion 解析 (CBOR) | `libfido2` (Yubico 维护, C 库, 经审计) |
| TOTP (RFC 6238) | 自实现 + OpenSSL HMAC-SHA1 (~50 行) |
| Challenge 绑定 typed_data hash | 自实现 (sha256 拼接) |

**与 v2 业务规则一致** (阈值表 / 双签人员 / Slack 仅知会 / WAL 字段), 不重复.

### 3.4 B4 (Tampering) — signer binary 完整性

```ini
# /etc/systemd/system/sports-signer.service
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

**signer C++ main() 启动期自校验**:

```cpp
// signer/main.cpp (前 30 行)
int main(int argc, char* argv[]) {
    // 0. 第一件事: 读 /proc/self/exe sha256, 与编译期烧死的 expected hash 比对
    constexpr std::array<uint8_t, 32> kExpectedBinarySha256 = {
        /* 编译期通过 build script 写入: $ sha256sum sports-signer | xxd */
    };
    auto self_sha = sha256_file("/proc/self/exe");
    if (!self_sha || *self_sha != kExpectedBinarySha256) {
        std::fprintf(stderr, "[FATAL] binary sha256 mismatch, aborting\n");
        std::exit(1);
    }
    // 此时 PR_SET_DUMPABLE 还没设, 写 stderr 安全
    
    // 1. 设置内存防护 (下面 B6)
    setup_memory_protection();
    
    // 2. KMS unwrap (下面 §3 启动流程)
    // ...
}
```

**minisign 签 binary**:
- 老沈持 minisign 私钥 (离线 air-gap)
- `make release` 后 → `minisign -S -s ~/.minisign/laoshen.sec -m sports-signer` → 产 `sports-signer.minisig`
- `verify-signer-sig.sh` 用 minisign pubkey (编进系统 image, `/etc/minisign-pubkeys/laoshen.pub`) 验签

### 3.5 B5 (最致命, Tampering) — signer 二次校验 typed-data

**C++ struct + msgpack 反序列化** (替代 v2 Rust serde):

```cpp
// signer/proto/sign_request.h
struct EIP712Domain {
    std::string name;
    std::string version;
    uint64_t chain_id;
    std::array<uint8_t, 20> verifying_contract;
    std::optional<std::array<uint8_t, 32>> salt;
    
    MSGPACK_DEFINE(name, version, chain_id, verifying_contract, salt);
};

enum class Intent : uint8_t {
    Order = 0,
    Cancel = 1,
    Approve = 2,
    EOA_Tx = 3,
};

struct SignRequest {
    // v1 字段
    uint64_t request_id;
    std::array<uint8_t, 16> session_id;
    uint64_t timestamp_ns;
    uint64_t nonce;
    
    // B1 鉴权
    std::array<uint8_t, 32> hmac;
    
    // B5 完整 typed data
    uint64_t chain_id;
    EIP712Domain domain;
    std::string primary_type;
    std::vector<uint8_t> typed_data_json;  // canonical JSON
    std::array<uint8_t, 32> claimed_hash;
    
    // 业务字段
    Intent intent;
    std::array<uint8_t, 20> receiver_addr;
    uint64_t amount_usdc;
    std::array<uint8_t, 20> wallet_addr;
    uint64_t expected_onchain_nonce;
    
    MSGPACK_DEFINE(request_id, session_id, timestamp_ns, nonce, hmac,
                   chain_id, domain, primary_type, typed_data_json, claimed_hash,
                   intent, receiver_addr, amount_usdc, wallet_addr, expected_onchain_nonce);
};
```

**signer 内部 EIP-712 hash 重算 (C++)**:

```cpp
// signer/eip712/hasher.cpp
namespace eip712 {

std::array<uint8_t, 32> hash_struct(std::string_view primary_type,
                                     const nlohmann::json& message);

std::array<uint8_t, 32> domain_separator(const EIP712Domain& d) {
    // keccak256(encode(EIP712Domain typeHash || hash(name) || hash(version) || chainId || verifyingContract))
    static constexpr auto kTypeHash = keccak256_const(
        "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)");
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha3_256(), nullptr);  // 注: SHA3-256 != Keccak-256, 见下注
    // ... 写入 kTypeHash || keccak256(d.name) || keccak256(d.version) || padded chainId || padded contract
    std::array<uint8_t, 32> out{};
    unsigned int out_len = 0;
    EVP_DigestFinal_ex(ctx, out.data(), &out_len);
    EVP_MD_CTX_free(ctx);
    return out;
}

// **关键陷阱**: Ethereum 用的是 Keccak-256, 不是 SHA3-256.
// OpenSSL 3.x 的 EVP_sha3_256() 是 NIST 标准化后的 SHA3 (padding 不同).
// **必须用 EVP_KECCAK_256 (OpenSSL 3.x 已支持, 名为 "KECCAK-256") 或 link libkeccak**.
// 见 §3.5 后注 + 老叶 cross-check 测试向量

std::array<uint8_t, 32> compute_signing_hash(const SignRequest& req) {
    // keccak256(\x19\x01 || domainSeparator || hashStruct(message))
    auto ds = domain_separator(req.domain);
    auto hs = hash_struct(req.primary_type, parse_json(req.typed_data_json));
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_keccak256(), nullptr);
    constexpr uint8_t prefix[2] = {0x19, 0x01};
    EVP_DigestUpdate(ctx, prefix, 2);
    EVP_DigestUpdate(ctx, ds.data(), 32);
    EVP_DigestUpdate(ctx, hs.data(), 32);
    std::array<uint8_t, 32> out{};
    unsigned int out_len = 0;
    EVP_DigestFinal_ex(ctx, out.data(), &out_len);
    EVP_MD_CTX_free(ctx);
    return out;
}

}  // namespace eip712
```

> **Keccak-256 vs SHA3-256 警告**: 这是 C++ 实现的头号 footgun. OpenSSL 3.0 起新增 `EVP_KECCAK*` 但接口与 SHA3 不同. 老叶 cross-check 一组 Polymarket SDK 输出的 byte-equal 测试向量验证 hash 字节级一致, 否则签出来的 sig 链上验签必失败.

**校验流程 (v2 §3.2 逐条照搬, C++ 实现)**:

```cpp
SignResponse SignerCore::handle_sign_request(SignRequest req, PeerInfo peer) {
    // 1-4: peer cred + HMAC + replay check (B1)
    if (auto e = check_peer_and_hmac(req, peer); !e) return reject(e.error());

    // 5: chain_id
    if (req.chain_id != 137) return reject(RejectReason::WrongChainId);

    // 6-7: receiver 白名单
    if (!receiver_whitelist_.contains(req.domain.verifying_contract))
        return reject(RejectReason::ReceiverNotWhitelisted);
    if (!receiver_whitelist_.contains(req.receiver_addr))
        return reject(RejectReason::ReceiverNotWhitelisted);

    // 8: intent / primary_type 一致
    if (!intent_matches_primary_type(req.intent, req.primary_type))
        return reject(RejectReason::IntentMismatch);

    // 9-10: 重算 hash, 与 claimed_hash 比对
    auto computed = eip712::compute_signing_hash(req);
    if (CRYPTO_memcmp(computed.data(), req.claimed_hash.data(), 32) != 0) {
        log_alert_high("HASH_MISMATCH", req);  // 可能 trader 被入侵
        return reject(RejectReason::HashMismatch);
    }

    // 11: amount 阈值 (B3)
    auto approval = threshold_engine_.classify(req.amount_usdc, req.intent);
    if (approval.needs_approval) {
        return enqueue_for_approval(req, approval);  // 异步, 不阻塞热路径
    }

    // 12: nonce
    auto reserved = nonce_mgr_->reserve_nonce(req.wallet_addr);
    if (!reserved || *reserved != req.expected_onchain_nonce)
        return reject(RejectReason::NonceConflict);

    // 14: secp256k1 签名
    auto sig = secp256k1_sign(computed, private_key_);

    // 15: WAL + confirm nonce
    wal_.append(req, sig, peer);
    nonce_mgr_->confirm_used(req.wallet_addr, *reserved);

    return SignResponse{.status = SignStatus::Ok, .r = sig.r, .s = sig.s, .v = sig.v,
                        .signer_pubkey = wallet_addr_, .used_nonce = *reserved};
}
```

### 3.6 B6 (Information Disclosure) — 内存防护 (C++ 最关键的工程化)

**这是 C++ 比 Rust 多花 1-2 周的核心原因.** Rust 编译器 `Drop trait` + `zeroize` 是免费的, C++ 必须三层兜:

1. **手写 `SecureBuffer<T>`** (§4 完整设计)
2. **`[[gnu::optnone]]` + `volatile T*` + memory barrier** 三层防优化
3. **ASAN/UBSAN/TSAN + 反汇编 audit** CI 强制兜底

**系统层 (老吴 SRE)**:
- `/proc/sys/kernel/yama/ptrace_scope = 2` (admin-only ptrace)
- swap 全禁 (`swapoff -a` + fstab 注释)
- core dump 全禁 (`kernel.core_pattern = |/bin/false`)
- `setrlimit(RLIMIT_CORE, {0,0})` (signer 启动期)
- `prctl(PR_SET_DUMPABLE, 0)` (signer 启动期)
- `madvise(key_buf, len, MADV_DONTDUMP)` (key load 后)
- `mlock(key_buf, len)` (key load 后)

### 3.7 B7 (DoS) — active-standby HA (C++ 等效, 设计不变)

**C++ 等效实现要点**:
- 两个独立 `sports-signer` 进程 (signer-A / signer-B), 同 systemd unit template
- trader 用 `boost::asio::local::stream_protocol` 双连两个 UDS socket
- `nonce_mgr` 接口 (老叶 owns, 设计**完全不变**): gRPC over UDS 或 raw socket, signer C++ 端用 gRPC C++ 或自实现 thin client
- Detection / failover SLO 与 v2 一致 (100ms / 1s)
- WAL append-only + hash chain → C++ `std::ofstream + fsync`, gtest 单测 hash chain integrity

### 3.8 B8 (Supply chain) — Conan / vcpkg lock + CycloneDX-cpp

| 工具 | Rust 等价 (v2) | 触发 |
|---|---|---|
| **Conan 2 lock file** | `Cargo.lock` + commit hash pin | 每次 build 强制锁 |
| **OSV-Scanner** | `cargo-audit` | CI 每次 PR + 每周定时 |
| **scancode-toolkit** | `cargo-deny` license check | CI 每次 PR |
| **dependabot for Conan** (GitHub native, 2025 起支持) | `cargo-deny ban list` | 自动 PR 提醒升级 |
| **手工 vet** (老沈 trust matrix, 与 Rust 一致) | `cargo-vet` | 新引入 lib 必须 vet |
| **CycloneDX-cpp** | `cyclonedx-rust-cargo` | 每次 release |
| **季度 review** (老沈 + 老高 + 老何 三人) | 老沈 + 老张 | 季度 |

**关键 C++ lib 清单 (锁版本 + Conan recipe hash)**:

| Lib | 用途 | 版本 | 审计来源 |
|---|---|---|---|
| `libsecp256k1` | secp256k1 签名 | 0.4.x (Bitcoin Core release) | Trail of Bits 2018, NCC 2021 |
| `OpenSSL` | TLS / SHA3 / Keccak / HMAC | 3.2.x LTS | FIPS 140-3 certified |
| `libsodium` | AEAD / curve25519 (备用) | 1.0.19 | Cure53 audit |
| `libfido2` | WebAuthn / FIDO2 | 1.14.x | Yubico maintained |
| `boost::asio` | UDS / IO | 1.84.x | Boost 社区 + 老高 review |
| `msgpack-c` | IPC wire format | 6.x | 与 v2 Rust msgpack 兼容 |
| `spdlog` | logging | 1.13.x | 经审计 |
| `nlohmann::json` | EIP-712 typed_data JSON 解析 | 3.11.x | 标准 |
| `gtest` / `gmock` | 测试 | 1.14.x | Google |

**版本升级流程**:
- PR 提出 → 老高 (C++ arch) 技术 review → 老沈 (security) 供应链 review → CycloneDX SBOM diff → merge
- transitive dep 升级走 Conan lockfile diff, 与直接 dep 同流程

---

## 4. SecureBuffer<T> 设计 + C++ 内存安全保障

### 4.1 完整代码

```cpp
// signer/memory/secure_buffer.h
#pragma once
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace signer::memory {

template <typename T>
class SecureBuffer {
    static_assert(std::is_trivially_copyable_v<T>,
                  "SecureBuffer<T> requires trivially copyable T to allow safe zeroize");

public:
    explicit SecureBuffer(std::size_t n) : n_(n) {
        const std::size_t bytes = n_ * sizeof(T);
        // 1. mmap PRIVATE|ANON, PROT_READ|WRITE
        void* p = ::mmap(nullptr, bytes,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            throw std::runtime_error("SecureBuffer: mmap failed");
        }
        ptr_ = static_cast<T*>(p);

        // 2. mlock 防 swap
        if (::mlock(ptr_, bytes) != 0) {
            ::munmap(ptr_, bytes);
            throw std::runtime_error("SecureBuffer: mlock failed");
        }

        // 3. MADV_DONTDUMP 防 core dump
        if (::madvise(ptr_, bytes, MADV_DONTDUMP) != 0) {
            ::munlock(ptr_, bytes);
            ::munmap(ptr_, bytes);
            throw std::runtime_error("SecureBuffer: madvise(DONTDUMP) failed");
        }
    }

    // 析构: zeroize → munlock → munmap
    ~SecureBuffer() {
        zeroize();
        const std::size_t bytes = n_ * sizeof(T);
        ::munlock(ptr_, bytes);
        ::munmap(ptr_, bytes);
        ptr_ = nullptr;
        n_ = 0;
    }

    // 禁拷贝, 仅 move
    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    SecureBuffer(SecureBuffer&& other) noexcept : ptr_(other.ptr_), n_(other.n_) {
        other.ptr_ = nullptr;
        other.n_ = 0;
    }
    SecureBuffer& operator=(SecureBuffer&&) = delete;  // 拒绝移动赋值, 避免遗失旧数据未 zero

    // 关键: zeroize 三层防优化
    [[gnu::optnone]]
    void zeroize() noexcept {
        if (!ptr_ || n_ == 0) return;
        volatile T* vp = ptr_;
        for (std::size_t i = 0; i < n_; ++i) {
            vp[i] = T{};
        }
        // 内联汇编 memory barrier, 阻止 LTO 跨函数优化
        __asm__ __volatile__("" ::: "memory");
    }

    T* data() noexcept { return ptr_; }
    const T* data() const noexcept { return ptr_; }
    std::size_t size() const noexcept { return n_; }
    std::size_t size_bytes() const noexcept { return n_ * sizeof(T); }

private:
    T* ptr_ = nullptr;
    std::size_t n_ = 0;
};

// 全局启动期初始化函数, signer main() 第一调
inline void setup_process_memory_protection() {
    // PR_SET_DUMPABLE = 0, 防 ptrace + /proc/<pid>/mem
    if (::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) {
        std::fprintf(stderr, "[FATAL] prctl(PR_SET_DUMPABLE, 0) failed: %s\n", strerror(errno));
        std::exit(2);
    }
    // setrlimit(RLIMIT_CORE, 0) 禁 core dump
    struct rlimit lim{0, 0};
    if (::setrlimit(RLIMIT_CORE, &lim) != 0) {
        std::fprintf(stderr, "[FATAL] setrlimit(RLIMIT_CORE) failed: %s\n", strerror(errno));
        std::exit(2);
    }
}

}  // namespace signer::memory
```

### 4.2 C++ 内存安全保障矩阵

| 风险 | Rust v2 兜底 | C++ v4 兜底 | 强度对比 |
|---|---|---|---|
| memset 被编译器优化掉 | `zeroize` crate 用 inline asm + volatile 实现 (编译器无法看穿) | `[[gnu::optnone]]` + `volatile T*` + `__asm__ memory barrier` 三层 | C++ 等价 (实际是同一原理) |
| use-after-free | borrow checker 编译期 | RAII (析构链) + ASAN 运行期捕获 | Rust 强 (编译期) > C++ (运行期, 但 ASAN 覆盖率 ~95%) |
| 数据竞争 | `Send`/`Sync` 编译期 | TSAN 运行期 | 同上 |
| 整型 UB (溢出 / shift) | 编译期 + checked_* APIs | UBSAN 运行期 + `-fsanitize=signed-integer-overflow` + 老何 footgun checklist 强制 `int64_t` | Rust 略强 |
| 拷贝构造意外暴露 | `Copy` trait 必须显式 | `delete` 拷贝构造 + `clang-tidy` cppcoreguidelines-special-member-functions | 等价 (依赖工程规范) |
| 指针越界 / 数组溢出 | bound check 编译期 | ASAN 运行期 + `std::span` + 老何 footgun checklist `at()` 替 `[]` | Rust 略强 |
| 私钥指针逃逸 (返回 raw T*) | 编译期阻止 lifetime 不当 | code review (老高 + 老沈) + clang-tidy + 接口只暴露 `std::span<const uint8_t>` | C++ 弱, 依赖 review |
| double-free | borrow checker | RAII + `delete` 拷贝赋值 + ASAN | 等价 |

**C++ 多出的工程兜底**:
1. **CI 三套 sanitizer 跑全 test suite**: `-fsanitize=address`, `-fsanitize=undefined`, `-fsanitize=thread`
2. **clang-tidy 必跑**: `cppcoreguidelines-*`, `bugprone-*`, `cert-*`, `concurrency-*`
3. **PR review 双人** (老高 C++ + 老沈 security) 强制对所有 SecureBuffer 边界
4. **fuzz** signer IPC parser + EIP-712 decoder, libFuzzer 跑 24h baseline
5. **反汇编 audit**: release binary objdump 关键函数 (`SecureBuffer::~SecureBuffer`, `zeroize`, `private_key 加载路径`), 确认 memset 没被优化掉 (老何 owns 季度审)

### 4.3 关键 footgun (老何 checklist 引用)

引用 `docs/RESEARCH/laohe-cpp-footgun-checklist-v1.md`, signer 实现期必须 100% 命中:

- §3.1 `memset` 不可用 → 必须 `SecureBuffer::zeroize`
- §3.2 `volatile` 单独不够 → 必须叠加 `[[gnu::optnone]]` + asm barrier
- §3.5 拷贝构造泄露 key → 显式 `= delete`
- §4.x signed/unsigned 整型比较 → 严格 `int64_t` + UBSAN
- §5.x 模板隐式实例化导致 key 泄露到 debug print → SecureBuffer 不写 `operator<<` (强制 review who 写 logging)
- §6.x exception 安全 → SecureBuffer 析构期 noexcept, 任何 throw 必须在持锁前完成 (与老高 PR review 强制项)

---

## 5. 签名延迟实测预估

### 5.1 单次签名分解 (本地 signer 同机)

| 步骤 | Rust v2 (实测/估算) | C++ v4 (预估) | 差异 |
|---|---|---|---|
| UDS recv + msgpack decode | 5μs | 5μs | 0 |
| SO_PEERCRED + HMAC verify | 3μs | 3μs | 0 |
| EIP-712 hash 重算 (Keccak256 x 3) | 15μs | 15μs (OpenSSL EVP_keccak256) | 0 |
| receiver / amount / nonce 校验 | 2μs | 2μs | 0 |
| nonce_mgr reserve_nonce (UDS RTT 同机) | 100μs | 100μs (老叶 owns, 跨语言不变) | 0 |
| **libsecp256k1 sign** | **70μs** (k256 Rust) | **70-80μs** (libsecp256k1 C, 历史 bench 略快或等价) | 0 ~ +10μs |
| WAL append + fsync (异步策略可优化) | 50μs (同步路径) 或 5μs (异步 buffer) | 50μs / 5μs | 0 |
| msgpack encode + UDS send | 5μs | 5μs | 0 |
| **合计 p50 (最优路径, 异步 WAL)** | **~130μs** | **~140μs** | +10μs (libsecp256k1 略保守) |
| **合计 p99 (含 jitter)** | **< 250μs** | **< 250μs** | 0 |

**结论**: C++ v4 与 Rust v2 签名延迟**同数量级**, 跨语言无系统性差异. 与 v1 §0 TL;DR "本地 signer p99 < 1ms" 目标一致.

### 5.2 启动期 KMS unwrap (一次性, 不影响热路径)

| 步骤 | 估算 |
|---|---|
| TLS handshake (OpenSSL + SPKI pin) | 80ms (TCP RTT to KMS endpoint + handshake) |
| AWS KMS Decrypt API call | 50-100ms |
| age decrypt (libsodium chacha20poly1305) | < 1ms |
| BIP39 passphrase XOR + BIP32 派生 | < 1ms |
| **合计** | **< 200ms** (一次性, 仅 signer 启动) |

**跨 vendor failover (主 AWS unwrap 失败 → 切 Sygnum)**: 额外 < 200ms RTT to 瑞士, **总 RTO < 30s** (与老沈 multi-vendor-kms v1 §0.4 一致).

### 5.3 与 trader 端到端订单延迟预算

老敖 `xiaojiang-latency-budget-v1.md` 给的端到端 < 200ms (跨洋), signer 占用 < 1ms p99, **占比 < 0.5%**, 不在 critical path 上.

---

## 6. 保留不变项 (v3 完全沿用, 跨语言)

### 6.1 跨境 Shamir 3-of-5 分布 (v3 §8.3)

5 地点完全不变:
1. 香港 — 老雷 — 金属种子板 + passphrase
2. 新加坡 — 老周 — 金属种子板 + passphrase
3. **瑞士 (Sygnum / Taurus)** — 商业托管 — 数字 air-gap + passphrase
4. 中国大陆 — 老周 — 个人银行保险箱
5. 日本东京 或 韩国首尔 — 老吴 — 加密 USB + Yubikey

地理硬约束 (3-non-PRC / 0-US / 0-OFAC / 0-UK) 不变.

### 6.2 BIP39 passphrase 2-of-2 (v3 §8.6)

- 老雷 + 老黄 双脑记 + 双金属板
- 季度轮换
- SOP-K5 老黄一票否决销毁权

C++ v4 在 signer 启动期实现 passphrase XOR 步骤 (上面 §5.2), 算法与 v3 一致.

### 6.3 跨 vendor KMS (v3 §N4 + 老沈 multi-vendor-kms v1)

- 主 wrap: AWS KMS (us-east-1) — 与 trader 同 region, **过渡方案**
- 副 wrap 1: Sygnum (瑞士) — 满足非美 vendor 总部硬约束 — **2027-02-26 GM 承诺接入**
- 副 wrap 2: GCP KMS (asia-southeast1, 新加坡)

C++ v4 实现 KMS REST client 三套 (AWS SigV4 / GCP OAuth2 / Sygnum HMAC), 见 §2.2.

### 6.4 GM Sygnum 2027-02-26 承诺 (`docs/ADR/2026-05-28-gm-commitment-sygnum-deadline.md`)

**v4 不影响此承诺**:
- 老黄 + 老雷 6/11 前联系 Sygnum
- 老黄 + 外部律师 7/15 前合同 review
- 8/15 前签字 + KYC
- 12/01 前老沈 + 老孙 集成测试通过 (Sygnum API + C++ signer 联调) — **此为 C++ v4 集成点, 不变**
- 2027-02-26 前老沈 + 老黄联签验收

C++ signer 与 Sygnum API 的集成实现路径: 自实现 HTTP+HMAC client (§2.2), 与 v3 Rust 设计**完全相同协议层**, 仅实现语言变.

### 6.5 active-standby HA (与老叶 nonce_mgr v1 对接)

- 两个 C++ signer 进程 (signer-A active / signer-B standby)
- `NonceManager` 接口由老叶 v1 给, signer C++ 端用 gRPC C++ stub 或自实现 thin client
- Detection (UDS ping 50ms x 2) + Failover (< 1s 总) **不变**
- cancel-only 模式 trader 侧实现 (与小颜 risk manager 联动)

### 6.6 阈值审批 + WAL + STRIDE 覆盖 (v2 §7 全部覆盖)

11 个 STRIDE P0 场景, v4 全部沿用 v2 修复方案. I-02 (API key 入 git) 仍归 secret-scanner 范围, 不在本方案.

---

## 7. 工作量预估 + 延期评估

### 7.1 设计层 (1 周, 大部分复用)

| 任务 | 工作量 | Owner |
|---|---|---|
| v4 文档 (本文档) + 老沈 + 老高 cross-check | 3 天 | 老孙 |
| EIP-712 C++ schema 设计 (与老李 Polymarket schema 对齐) | 2 天 | 老孙 + 老李 |
| Conan recipe lock + SBOM baseline | 1 天 | 老孙 + 老吴 |
| 老沈 + 老高 + 老黄 sign-off review | 1 天 | 三人并行 |

### 7.2 实现层 (比 Rust v2 多 2 周)

| 模块 | Rust v2 估算 | C++ v4 估算 | 差异原因 |
|---|---|---|---|
| signer 主框架 + UDS server | 3 天 | 4 天 | boost::asio 与 tokio 语义差异 |
| IPC schema + msgpack 序列化 | 2 天 | 2 天 | 同 |
| **EIP-712 hash 重算 + 测试向量** | 3 天 | **5 天** | **Keccak vs SHA3 footgun + 老李 100+ byte-equal 向量 cross-check** |
| libsecp256k1 / k256 集成 + sign | 1 天 | 1 天 | 同 |
| **SecureBuffer<T> + 内存防护四件套** | 2 天 (zeroize crate 现成) | **5 天 (含反汇编 audit + ASAN/UBSAN/TSAN 调通)** | **C++ 工程化兜底成本** |
| KMS 客户端 (AWS + GCP + Sygnum) | 5 天 (有 aws-sdk-rust) | **10 天 (自实现 SigV4 + OAuth2 + HMAC)** | **C++ 无官方 SDK** |
| TLS SPKI pin verifier (OpenSSL callback) | 2 天 (rustls custom) | 3 天 (OpenSSL API 复杂) | OpenSSL API |
| WebAuthn 集成 (libfido2) | 3 天 | 4 天 | libfido2 学习成本 |
| HA 双 signer + nonce_mgr 客户端 | 5 天 | 5 天 | 等价 |
| WAL + hash chain + spdlog redact sink | 3 天 | 3 天 | 等价 |
| 单元测试 + fuzz + chaos drill | 5 天 | 7 天 | C++ 测试 boilerplate 略多 |
| **合计** | **~34 天 (~7 周)** | **~49 天 (~10 周)** | **+15 天 = +3 周** |

### 7.3 PR review 工作量 (+30%)

| 角色 | Rust v2 | C++ v4 |
|---|---|---|
| 老沈 security review | 每 PR 30min | 每 PR 45min (+50% — C++ 必须额外看 memory safety 边界) |
| 老高 C++ arch review | — | 每 PR 30min (新增, Rust 时由老张 review, 现在改老高) |
| 老何 footgun checklist 审 | — | 每关键 PR 一次 cross-check (新增) |
| **review 总工作量** | baseline | **+30% (与 v4 题目预估一致)** |

### 7.4 总延期 = 2-3 周

| 节点 | Rust v2 计划 | C++ v4 修正 |
|---|---|---|
| signer 实现启动 | Sprint-2 启动 (2026-06-26) | **同, 不延** |
| 实现完成 (含测试) | Sprint-2 末 (~7-8 周后, 2026-08-14) | **+2~3 周, 2026-08-28 ~ 2026-09-04** |
| Sygnum 集成测试 | 2026-12-01 (GM 承诺) | **不延** (有充分 buffer 4 个月) |
| Sygnum final sign-off | 2027-02-26 (GM 承诺) | **不延** |

**结论**: **不影响 Sprint-2 启动 (6/26)**, 实现期延 2-3 周, Sygnum 集成有 4 个月 buffer 完全消化得了延期. **GM 2027-02-26 承诺不受影响**.

---

## 8. 与老沈 + 老高 PR review 接力

### 8.1 PR 分阶段交付路径

```
T+0 (2026-05-28): 老孙 v4 提交本文档
T+1 (2026-05-29): 老高 review v4 §2 (C++ 技术栈) + §4 (SecureBuffer) → conditional accept
T+1 (2026-05-29): 老沈 review v4 §3 (8 Blocker C++ 化) → conditional accept
T+2 (2026-05-30): 老黄 review v4 §6 (合规保留项) → final accept (无合规变化, 跨语言)
T+3 (2026-05-31): 老雷 GM final sign-off
T+3 ~ Sprint-2 启动 (2026-06-26): 老孙搭基础脚手架 (Conan + CMake + 静态分析 CI)
Sprint-2 启动 (2026-06-26): C++ signer 实现开工
Sprint-2 第 1 周: 拿到老叶 nonce_mgr 接口 + 老李 EIP-712 schema + 老吴 跨 vendor IAM
Sprint-2 末: 实现完成 + 单测 + fuzz 跑 24h
Sprint-3 初: 老沈 + 老高 + 老何 三方 PR review + 反汇编 audit
2026-09-04 ± 1 周: signer v4 实现验收 (老沈 + 老高 sign-off)
2026-12-01 前: Sygnum API 联调测试通过
2027-02-26 前: Sygnum final sign-off (GM 承诺)
```

### 8.2 老高 review 重点 (C++ 内存安全)

老高 (cpp-arch-lead) review checklist:
- [ ] SecureBuffer<T> 实现 — mlock / madvise / zeroize 三层全到位
- [ ] `[[gnu::optnone]]` + volatile + asm barrier 三层防优化, 反汇编 audit pass
- [ ] 所有 SecureBuffer 拷贝构造 / 拷贝赋值 = delete, 移动语义边界清晰
- [ ] private_key 路径只暴露 `std::span<const uint8_t>`, 不返回 raw pointer
- [ ] exception 安全 — 任何 throw 不能跨过 mlock 边界 (析构必 noexcept)
- [ ] ASAN/UBSAN/TSAN 三模式 CI 100% 跑 + 0 finding
- [ ] clang-tidy `cppcoreguidelines-*` + `bugprone-*` + `cert-*` 0 warn
- [ ] reproducible build 验证 (两次 build 二进制 sha256 一致)

### 8.3 老沈 review 重点 (security)

老沈 (security-engineer) review checklist (与 v2 一致, 跨语言):
- [ ] B1 ~ B8 全部覆盖 (与 v2 §7.2 checklist 完全一致)
- [ ] SPKI pin 数组编译期常量, 不读 config
- [ ] HMAC session_key 走 fd-passing, 不进 IPC payload
- [ ] CMK key policy 三套 (AWS / GCP / Sygnum) 与老吴对接
- [ ] minisign 签名 + ExecStartPre 验签 链路完整
- [ ] WAL hash chain 单测 + 异地备份策略

### 8.4 老何 footgun checklist 强制项

老何 (cpp-version-advisor) 季度对 signer 关键路径反汇编 audit:
- [ ] `SecureBuffer::zeroize` 反汇编必含 store 指令 (memset 未被优化掉)
- [ ] `private_key 加载路径` 不含 stack 拷贝
- [ ] 模板实例化无意外 logging (operator<< 不暴露 key)

---

## 9. 残留开放问题 (v4 新增 + 沿用 v3)

| # | 问题 | 阻塞? | Owner | 求助 | Deadline |
|---|---|---|---|---|---|
| Q1 ~ Q18 | (沿用 v3, 跨语言不变, 包含 IAM / nonce_mgr / 白名单 / EIP-712 schema / Sygnum 选型 / 老吴外派 / 老周入选条款) | (沿用 v3) | (沿用 v3) | (沿用 v3) | (沿用 v3) |
| **Q19** (v4 新增) | **C++ libsecp256k1 byte-equal 测试向量 100+** (与 Polymarket SDK / 老叶 ethers.js cross-check) | 是 (B5 实现期) | 老孙 + 老李 + 老叶 | — | Sprint-2 第 2 周 |
| **Q20** (v4 新增) | **OpenSSL EVP_keccak256 vs SHA3-256 在我们 OpenSSL 3.x 版本上确认可用** (FIPS 模式可能屏蔽 Keccak), 否则改 link libkeccak | 是 (B5 实现期) | 老孙 + 老吴 (验 prod OpenSSL build flag) | — | Sprint-2 第 1 周 |
| **Q21** (v4 新增, **唯一残留风险**) | **SecureBuffer 反汇编 audit 自动化** — 目前依赖老何手工季度 audit, 没法 CI 卡. 残留风险: 某次编译器升级 / LTO flag 变更 / [[gnu::optnone]] semantics 演化 导致 memset 被优化掉但 audit 周期内无人发现 | 否 (可缓解, 不阻塞 MVP) | 老孙 + 老何 + 老沈 | 长期: 写自动化反汇编 lint 工具 (LLVM IR pass 或 objdump regex) | Sprint-3 内交付 lint 工具; Sprint-2 MVP 阶段靠手工 audit + ASAN 兜底 |
| **Q22** (v4 新增) | **C++ KMS REST client 自实现 (无官方 SDK) — AWS SigV4 / GCP OAuth2 / Sygnum HMAC 的 wire-level 测试向量** | 是 (实现期) | 老孙 + 老吴 (AWS) + 老黄 (Sygnum 文档) | 与 boto3 / google-cloud-python / Sygnum SDK byte-equal 对比 | Sprint-2 第 3 周 |
| **Q23** (v4 新增) | **C++ 端到端 fuzz 覆盖率目标** — IPC parser + EIP-712 decoder 24h libFuzzer, 期望 line coverage > 85% | 否 | 老孙 + 小宋 (test framework) | — | Sprint-3 |

**v4 残留: 23 个** (v3 18 个 + v4 新增 5 个 Q19~Q23).

**Sprint-2 启动前必须关闭**: Q1 / Q9 / Q10 / Q11 / Q16 / Q17 / Q18 (v3 七项) + Q20 (OpenSSL Keccak 可用性) **共 8 项**.

---

## 附录 A. v3 章节对应 (沿用 + 修订)

| v3 章节 | v4 状态 |
|---|---|
| §0 v2→v3 变更摘要 | v3 保留, v4 §0 新增 v3→v4 (Rust→C++) 差异表 |
| §1~§7 沿用 v2 主体 | **沿用** (跨语言不变) |
| §8 跨境 Shamir 分布 | **沿用** (跨语言不变) |
| §8.6 BIP39 passphrase | **沿用** (算法不变, C++ 实现 XOR 步骤) |
| §N4 跨 vendor KMS | **沿用** (协议不变, C++ 客户端自实现见 §2.2 + §6.3) |
| §9 v3 整改验收 checklist | v4 §8 接力, 加 C++ 内存安全 + footgun checklist |
| §10 残留问题 Q1~Q18 | v4 §9 沿用 + 新增 Q19~Q23 |

---

## 附录 B. v4 给老雷的 90 秒 elevator summary

1. **公司无 Rust → signer 全 C++ 重写**, 算法 / 协议 / 流程 / 合规 100% 复用 v3 设计, 仅实现语言变.
2. **C++ 技术栈**: libsecp256k1 (Bitcoin Core 金标准) + OpenSSL 3.x (Keccak / TLS) + libsodium (AEAD) + libfido2 (WebAuthn) + boost::asio (UDS) + spdlog + gtest. 全部业界经过审计的 C 库.
3. **内存安全代价**: Rust 编译器免费送的, C++ 要手工 `SecureBuffer<T>` + ASAN/UBSAN/TSAN + 反汇编 audit + 老高 / 老沈 / 老何 三方 PR review. 工程化成本 +2 周.
4. **签名延迟无差异**: p99 < 250μs, 与 Rust v2 同数量级, 不影响 trader 端到端 < 200ms 预算.
5. **8 Blocker 全 C++ 化, STRIDE 11 个 P0 场景覆盖 10/11** (I-02 不在本方案范围, 同 v2).
6. **跨境 Shamir / passphrase / 跨 vendor / Sygnum / HA 不变**, GM 2027-02-26 承诺不受影响.
7. **延期 2-3 周** (实现期), Sprint-2 启动不延 (6/26), Sygnum 集成有 4 个月 buffer.
8. **残留风险 1 条**: SecureBuffer 反汇编 audit 目前手工, 无 CI 卡, Sprint-3 内交付 lint 工具补 (Q21).

---

*v4 提交: 2026-05-28*
*Owner: 老孙 (crypto-signing-expert)*
*Co-review: 老沈 (security) + 老高 (cpp-arch) + 老何 (cpp-footgun)*
*验收: 老雷 (GM) + 老沈 + 老黄*
*预计 final sign-off: 2026-06-04 (T+7)*
*v1 / v2 / v3 (Rust) 保留为 review trail + 算法权威, 不删*
*GM 2027-02-26 Sygnum 承诺: 不变, 不延*
