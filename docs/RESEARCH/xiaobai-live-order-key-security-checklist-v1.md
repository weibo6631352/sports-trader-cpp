---
owner: 小白 (#27, security-engineer)
last_review: 2026-05-31
sprint: MVP 实盘测试专项
status: FINAL — GM 上真实下单前必须逐项勾完
co_reviewer: 老沈 (security-engineer) — 威胁模型对齐
cite:
  laosun_spec:  docs/RESEARCH/laosun-live-order-signing-plan-v1.md §4
  laoli_spec:   docs/RESEARCH/laoli-live-submitorder-plan-v1.md §6.2
  threat_model: docs/RESEARCH/laoshen-threat-model-v2.md §3 §4 §5.1
  key_coreview: docs/RESEARCH/laoshen-key-management-coreview-v1.md §2.4
  secure_buf:   include/stcpp/crypto/ed25519.hpp (SecureBuffer<N>)
---

# 实盘下单私钥/凭证安全 Review + Checklist v1

> 红线原文 (CLAUDE.md §8, 必须原文引用，禁转述):
> 「私钥明文落盘 / 出现在日志 → 系统权限暂停」

---

## §0 Review 范围 + 小白边界

本文覆盖:
- WALLET_PRIVATE_KEY (secp256k1 32B EOA 私钥, .env 中 66 字符 "0x"+64hex)
- POLYMARKET_API_SECRET (HMAC 计算用, .env 中 44 字符 base64)
- POLY_API_PASSPHRASE / POLYMARKET_API_KEY (L2 auth header 凭证)
- keccak256 vendored 实现可信度
- 供应链: libsecp256k1 (待引入) + libsodium (已在用)
- 云上 (18.132.108.50) 私钥落地安全步骤
- 测试单 fail-safe

小白不管: libcurl 实现细节 (老陈边界) / RM cap 配置 (老韩边界) / 网络拓扑 (老吴边界)。

---

## §1 私钥读取 / 持有 — Review 结论

### §1.1 .env 文件权限 — 发现 P1

当前状态: `stat /Users/wangweibo/code/sports-trader-cpp/.env` 返回权限 **644** (世界可读)。

**风险**: 任何在同一主机以非 root 身份登录的进程 / 用户都能 `cat .env` 读到全部凭证 (威胁模型 I-03, DREAD 17)。

**必须在上线前**: `chmod 600 .env` (仅 owner 可读写)。

云端 18.132.108.50 同样要求: 上传后立即 `chmod 600 .env`, 确认文件 owner = 跑 trader 进程的用户。

### §1.2 读取方式 — 合规路径确认

老孙 spec §4.1 给的读法是合规的:

```cpp
// 合规: 通过 getenv() 读, 直接写进 SecureBuffer<32>
const char* raw = std::getenv("WALLET_PRIVATE_KEY");
crypto::SecureBuffer<32> privkey_buf;
hex_decode_to(raw + 2, privkey_buf.data(), 32);  // 跳过 "0x"
// raw 指向 environ[] 内存, 不需要 free, 不需要 memzero
// (不要 strdup/std::string 持有 raw)
```

**关键陷阱: hex decode 中间变量**。如果 hex_decode_to 内部使用了 `std::string` 临时缓冲区持有 hex 字符串, 该字符串的析构不会清零。GM 实施时确认 hex decode 函数签名为:

```
hex_decode_to(const char* hex_in, uint8_t* out, size_t out_len)
```

直接逐字节解析写进 `out` (SecureBuffer 内存), 不经过 std::string 中转。

### §1.3 std::string 持有私钥 — 硬禁

`std::string` 的析构不调 `sodium_memzero`。编译器不保证清零。这意味着私钥字节可能在 heap 上存留直到操作系统回收该内存页, 其间:
- core dump 会捕获到
- /proc/PID/mem 可被 ptrace 读出 (威胁模型 E-02)
- swap 可能将包含私钥的内存页写入磁盘 (I-01, DREAD 20)

**红线**: 私钥 32B 只能存在于 `SecureBuffer<32>` 内。签名期间 libsecp256k1 函数参数传 `const uint8_t*` 指针 (来自 SecureBuffer), 不拷贝。

### §1.4 .env 不入 git — 已确认 OK

检查结果: `.gitignore` 第 31 行已有 `.env` 规则。`git ls-files .env` 返回空 — .env 未被追踪。

---

## §2 签名过程内存安全 — Review 结论

### §2.1 libsecp256k1 ctx 生命周期

libsecp256k1 的 `secp256k1_context` 本身不存储私钥, 只是算法状态。但:
- Context 需要在使用期间有效
- 用完后调 `secp256k1_context_destroy()` 释放
- 不需要 memzero ctx 本身 (ctx 不含密钥材料)

私钥 32B bytes 只在 `secp256k1_ecdsa_sign_recoverable()` 调用期间作为参数传入, 调用结束后私钥参数在 C 函数帧上的局部拷贝 (若有) 不受我们控制, 但 libsecp256k1 内部有 `memset` 保护关键临时值 (Bitcoin Core 代码审计历史有保证)。

**要求**: 调用 secp256k1 签名函数后, privkey_buf (SecureBuffer<32>) 继续存活至 SignerV62 析构, 析构时自动 `sodium_memzero`。不要在签名完成后立即手动 memzero, 因为后续可能需要对同一订单做重试 (同一进程生命周期内)。

### §2.2 mlock — 防 swap

将包含私钥的内存页锁定, 禁止被 swap 到磁盘。需要在 SecureBuffer<32> 分配后立即 mlock:

```cpp
// 在 SecureBuffer 持有私钥的页上调用 mlock
mlock(privkey_buf.data(), 32);
// 对应在析构前 (sodium_memzero 之后) munlock — 或在 SecureBuffer 析构中加入
```

**现状 gap**: 当前 `SecureBuffer<N>` (include/stcpp/crypto/ed25519.hpp) 析构只调 `sodium_memzero`, 没有 `mlock`。对于 paper mode Ed25519 密钥这可以接受 (无真实资产), 但对于 live secp256k1 私钥**必须加 mlock**。

**解决方案 (GM 实施注意)**: 在读入 WALLET_PRIVATE_KEY 后, 在存入 SecureBuffer 之前或之后, 对该页调用:
```cpp
mlock(privkey_buf.data(), privkey_buf.size());
```
这是对现有 SecureBuffer 的使用层补充, 不需要修改头文件 (改头文件超出 GM 当前任务范围)。

老沈威胁模型 M-K2 要求完整 4 件套: `mlock + MADV_DONTDUMP + PR_SET_DUMPABLE=0 + explicit_bzero`。对于 MVP 测试单, 小白给出最低可接受要求:

| 防护项 | MVP 最低要求 | 说明 |
|---|---|---|
| `setrlimit(RLIMIT_CORE, 0)` | **必须** | 禁 core dump (老孙 §4 S-7 要求) |
| `mlock(privkey_buf, 32)` | **必须** | 禁 swap |
| `sodium_memzero` on exit | **必须** | SecureBuffer 析构已有; 确认析构在 main 返回前触发 |
| `madvise(MADV_DONTDUMP)` | 强烈建议 | 防内核 crash dump / debugger ptrace mem dump |
| `prctl(PR_SET_DUMPABLE, 0)` | 强烈建议 | 防 /proc/PID/mem 被读 |

**MVP 测试单必须满足前 3 项。后 2 项在 Sprint-1 末 (第一笔真实成交后 24h 内) 补上。**

### §2.3 RLIMIT_CORE = 0 位置

必须在 `main()` 最开头, 在任何私钥读取之前调用:

```cpp
// main() 第一件事
struct rlimit rl = {0, 0};
setrlimit(RLIMIT_CORE, &rl);
// 然后才读私钥
```

现状: signer_v62.cpp 没有 main(), 它是库。GM 在调用 SignerV62 构造的 main() 入口处加。

---

## §3 日志红线 — 禁 log 字段清单

### §3.1 绝对禁止 log 的字段 (任何级别: trace/debug/info/warn/error 全禁)

| 字段 | 存在位置 | 禁止原因 |
|---|---|---|
| `WALLET_PRIVATE_KEY` (原始 hex 或任何片段) | .env, getenv 返回值 | 红线原文: 「私钥明文...出现在日志 → 系统权限暂停」|
| `privkey_buf` 任何字节 (包括前 4 字节 "调试用") | SecureBuffer<32> 内容 | 同上 |
| `POLYMARKET_API_SECRET` (base64 原文或任何片段) | .env | 泄露后攻击者可伪造任意 HMAC 请求 |
| `POLY_PASSPHRASE` / `POLYMARKET_API_PASSPHRASE` | .env / L2 auth header | L2 auth 关键凭证 |
| `POLY_SIGNATURE` (L2 HMAC header 值) | HTTP 请求 header | 可被 replay (30s 内有效); 老李 spec §6.2 明确 "redact POLY_SIGNATURE" |
| secp256k1 私钥推导中间量 (nonce k, 临时标量) | 签名函数内部 | 理论上可从 k 反推私钥 (ECDSA nonce 泄露攻击) |

### §3.2 可以安全 log 的字段

| 字段 | 说明 |
|---|---|
| `order.signature` (EIP-712 ECDSA, "0x"+130 hex) | 公开签名, 链上可查; 老孙 §4 S-6 的"不 log 完整 signature"属于保守立场。小白确认: ECDSA r/s/v 65B 是公开输出, 不能反推私钥 (正向不可逆), 可以 log。但若觉得多余也可不 log — 不影响安全 |
| `POLYMARKET_FUNDER_ADDRESS` / EOA 地址 | 公开链上地址, 无需保密 |
| `POLYMARKET_API_KEY` (UUID 格式) | 注意: API key 本身是标识符非密钥, 泄露后攻击者仍需 secret 才能签名。但结合 secret + passphrase 三件套泄露会有问题。建议: log 前 8 字符 (UUID 的 group-1) 用于追踪, 不 log 全量 |
| `order_id` (服务端返回 UUID) | 公开 |
| HTTP 响应 body (含 status, errorMsg) | 可以 log, 不含凭证 |
| HMAC 的 `base_string` 中 timestamp + method + path 部分 | 可 log 用于调试 401; 但 **不要 log 完整 base_string** (因为 base_string 含 body, body 含 order 细节) |

### §3.3 401 调试特殊情况

遇到 401 时最常见的冲动是打出完整请求。以下是安全的 401 调试流程:

1. log `POLY_TIMESTAMP` (时间戳值) — OK
2. log HTTP method + path (`POST /order`) — OK
3. log `POLY_ADDRESS` + `POLY_API_KEY` (前 8 字符) — OK
4. log body JSON — OK, body 不含私钥 (body 只含 order.signature = 公开 ECDSA sig)
5. **不 log `POLY_SIGNATURE` header 值** — 这是 HMAC 结果, 30s 内可 replay
6. **不 log `POLY_API_SECRET` 任何形式** — HMAC 密钥

---

## §4 云上私钥落地安全步骤 (18.132.108.50)

老板授权: 私钥可上云服务器。以下是安全传输 + 落地步骤。

### §4.1 传输方式

**方式 A (推荐, GM 已用过): SSH stdin pipe 传输 .env**

```bash
# 本地执行: 通过 SSH stdin 传输, 避免 .env 出现在命令行参数或 shell history
# 注意: 命令行历史 (bash_history / zsh_history) 不会记录 stdin 内容
cat .env | ssh user@18.132.108.50 'cat > /path/to/project/.env'
# 或更安全:
ssh user@18.132.108.50 'cat > /path/to/project/.env' < .env
```

**禁止的方式**:
- `scp .env user@host:/path/.env` — SCP 本身安全, 但内容落盘后 history 留记录
- `ssh user@host "echo 'WALLET_PRIVATE_KEY=0x...' > .env"` — 私钥出现在命令行 `ps aux` 可见, 也进 shell history
- 任何经过中间服务 (Slack / GitHub issue / email) 传输 .env 内容

**方式 B: scp 传输加权限检查**

如果用 scp, 传输本身是 TLS/SSH 加密的, 可接受。但传完立即检查:
```bash
ssh user@18.132.108.50 'stat -c "%a %n" /path/to/.env'
# 必须输出 "600 /path/to/.env"
```

### §4.2 云端 .env 权限清单

传到云上后必须执行:

```bash
# 1. 权限 600
chmod 600 /path/to/project/.env

# 2. owner 必须是跑 trader 进程的用户, 不能是 root (原则: 最小权限)
# 如果 trader 用 ubuntu 用户跑:
chown ubuntu:ubuntu /path/to/project/.env

# 3. 确认目录权限 (父目录 750 或更严)
chmod 750 /path/to/project/

# 4. 验证 root 以外的其他用户不可读
# 用另一个非 root 用户尝试 cat, 应该报 Permission denied
```

### §4.3 云端 core dump + swap 检查

上线前在 18.132.108.50 执行:

```bash
# 检查 swap 是否关闭 (推荐关闭)
free -h
# 如果有 swap, 关闭:
sudo swapoff -a
# 永久关闭: 注释掉 /etc/fstab 中的 swap 行

# 检查 core dump 设置
ulimit -c
# 应该是 0. 如果不是:
ulimit -c 0
# systemd 服务文件需要加: LimitCORE=0

# 检查 /proc/sys/kernel/core_pattern
cat /proc/sys/kernel/core_pattern
# 如果指向某个目录, 需要确保 core_pattern 无效或 coredump disabled
```

### §4.4 云端 SSH 安全 (连接到 18.132.108.50)

老沈威胁模型 M-P1 要求 bastion + MFA。MVP 测试单阶段最低要求:

- SSH 使用 key 认证, 禁用密码认证 (`PasswordAuthentication no` in sshd_config)
- SSH key 本地存 `~/.ssh/id_ed25519` (或 RSA 4096), 私钥设 passphrase
- 云端 `~/.ssh/authorized_keys` 只含 GM 当前使用的 SSH 公钥
- 不在 `known_hosts` 提示时盲目 yes (先通过带外渠道确认 host fingerprint)

---

## §5 供应链可信度 Review

### §5.1 libsecp256k1 (Bitcoin Core)

**来源**: https://github.com/bitcoin-core/secp256k1

**可信度评估**: 极高。Bitcoin Core 团队维护, 该库是 Bitcoin 签名的实现, 经历全球最高强度的安全审计 (比特币安全假设的基石)。2013 年起持续维护。

**版本建议**: 使用 GitHub release tag, 通过 vcpkg 或 CMake FetchContent 引入, **pin 到具体 commit hash** (与 libsodium 同等规范)。不要从随机镜像或 fork 引入。

**验证方法**:
```bash
# 引入后验证 SHA256 (vcpkg 会自动验证 portfile 中的 sha512)
# 手动验证: 下载 tarball 后对比 sha256sum 与 GitHub release 页面

# 最简可用接口:
# secp256k1_context_create(SECP256K1_CONTEXT_SIGN)
# secp256k1_ecdsa_sign_recoverable(ctx, &sig, msg32, privkey, NULL, NULL)
# secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, r_s_64, &v, &sig)
```

**关键参数**:
- `secp256k1_ecdsa_sign_recoverable` 使用 RFC 6979 确定性 nonce (默认), 不使用随机 nonce
- RFC 6979 = ECDSA nonce 泄露风险为零 (nonce 由 privkey + message deterministically 导出)
- **这是比随机 nonce 更安全的选择**

### §5.2 vendored keccak256.h — 可信度 Review

**位置**: `experiments/laosun-laoli-live-order/keccak256.h`

**代码审查结论**:
- 实现使用标准 Keccak-f[1600] 置换 (24 轮), RC/ROT/PIL 常量已验证
- rate = 136 字节 (= 1088 bit, 与 keccak256 规范一致)
- padding: `0x01` (Ethereum keccak, 非 FIPS SHA3 的 `0x06`) — **关键区别, 已正确实现**
- 文件头注释已标明 test vector: `keccak256("") == c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470`

**test vector 验证 (GM 上线前必须跑)**:

```cpp
// 验证代码 (GM 在 main 入口 或 单元测试中加):
{
    const uint8_t empty_input[1] = {0};  // 空输入, len=0
    uint8_t out[32];
    kc::keccak256(nullptr, 0, out);  // 或传任意指针 len=0

    // 期望值: c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
    static const uint8_t expected[32] = {
        0xc5, 0xd2, 0x46, 0x01, 0x86, 0xf7, 0x23, 0x3c,
        0x92, 0x7e, 0x7d, 0xb2, 0xdc, 0xc7, 0x03, 0xc0,
        0xe5, 0x00, 0xb6, 0x53, 0xca, 0x82, 0x27, 0x3b,
        0x7b, 0xfa, 0xd8, 0x04, 0x5d, 0x85, 0xa4, 0x70
    };
    assert(memcmp(out, expected, 32) == 0);  // 必须通过
}
```

**第二个 test vector (Ethereum 常用)**:
```
keccak256("abc") == 4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45
```

**注意**: 不应用 std::string 或临时缓冲区传空字符串的 .data() 给 keccak256。空字符串时 len=0 即可, 不需要解引用指针。如果实现里 `in` 指针为 nullptr 但 `inlen=0`, 实现不会解引用 (while 循环条件 `inlen >= rate` 在 inlen=0 时立即跳过)。

### §5.3 libsodium — 已在用, 可信度 OK

`SecureBuffer<N>` 依赖 libsodium 的 `sodium_memzero`, 后者使用 `memset_s` 或 volatile 指针防编译器优化。libsodium 1.0.20 已通过老孙 W7 FetchContent 集成, 可信度充分。

### §5.4 vcpkg 供应链规范

引入 libsecp256k1 时:
- 走 vcpkg manifest (`vcpkg.json`) 声明依赖, 指定版本
- pin 到具体 portfile commit hash (与老沈 M-S1 要求一致)
- 升级走 PR + 老沈 review (威胁模型 M-S1)
- 不直接 `git clone` secp256k1 进 third_party/ 目录 (无法 hash pin)

---

## §6 GM 上真实下单前 — 逐项 Checklist

### §6.1 环境 + 凭证安全 (本地或云端)

```
[ ] C-01  .env 文件权限 = 600 (chmod 600 .env && stat .env 确认)
[ ] C-02  .env 未被 git 追踪 (git ls-files .env 返回空 = OK)
[ ] C-03  WALLET_PRIVATE_KEY 格式正确 = "0x" + 64 小写 hex 字符 (总长 66)
[ ] C-04  POLYMARKET_API_SECRET 已填入 .env (通过 DeriveApiKey 获取)
[ ] C-05  POLYMARKET_API_PASSPHRASE 已填入 .env (同上)
[ ] C-06  POLYMARKET_FUNDER_ADDRESS 已填入且格式 = "0x" + 40 hex
[ ] C-07  以上字段未出现在任何 git commit / PR diff / Slack 消息 / 日志文件中
```

### §6.2 进程级内存安全

```
[ ] C-08  main() 第一行调用 setrlimit(RLIMIT_CORE, {0,0}) (禁 core dump)
[ ] C-09  WALLET_PRIVATE_KEY 通过 getenv() 读取后直接写入 SecureBuffer<32>
[ ] C-10  hex decode 过程不经过 std::string 中间缓冲区
[ ] C-11  SecureBuffer<32> 的页面已 mlock (防 swap 落盘)
[ ] C-12  签名完成后 SecureBuffer<32> 析构链会触发 sodium_memzero (确认对象有正确生命周期)
[ ] C-13  POLYMARKET_API_SECRET (base64) 存入 SecureBuffer<44> 或等效, 计算完 HMAC 后 memzero
```

### §6.3 日志 + 代码审查

```
[ ] C-14  用 grep 全量扫描签名路径代码, 无任何 spdlog/printf/cout 含 privkey 内容
          检查命令: grep -rn "spdlog\|printf\|cout\|cerr" src/stcpp/signer/ src/stcpp/polymarket/live/
[ ] C-15  HTTP 请求发出前, 确认日志不记录 "POLY_SIGNATURE" header 值
[ ] C-16  确认 POLYMARKET_API_SECRET 未出现在 polymarket_clob_subscriber.cpp 的 log 路径
          (当前 P-09 红线已有注释, 确认注释对应的 no-log 实现有效)
[ ] C-17  live_wss_transport.hpp 的 "payload 严禁打 log" (P-09) 在实际运行中有效
```

### §6.4 keccak256 + 签名正确性验证

```
[ ] C-18  keccak256 test vector 通过: keccak256("") == c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
[ ] C-19  keccak256 第二 test vector: keccak256("abc") == 4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45
[ ] C-20  dry-run: 用同一参数执行 EIP-712 签名, 然后用 secp256k1_ecdsa_recover_pubkey 恢复公钥,
          推导地址, 确认 recovered_address == POLYMARKET_FUNDER_ADDRESS (或 EOA 地址)
          — 这是"签名可 recover 回 funder 地址"的最强保证, 防止下单后 CLOB 400 拒单
[ ] C-21  domain separator 使用 version="2" (字符串 "2", 不是整数 2)
[ ] C-22  verifyingContract 已通过 GetMarketInfo(condition_id).neg_risk 动态查, 未硬编码
[ ] C-23  EIP-712 Order struct 字段顺序 = typeHash 顺序 (salt/maker/signer/tokenId/
          makerAmount/takerAmount/side/signatureType/timestamp/metadata/builder)
[ ] C-24  signature_type = 1 (HMAC bug #2 永久 enforce)
```

### §6.5 测试单 fail-safe

```
[ ] C-25  size_pUSD_micro <= 1_000_000 ($1.00 micro) — 硬编码上限, 代码中 assert 或 if-throw
[ ] C-26  下单前调 GetBalance(), 确认 pUSD balance >= 2 × size (双倍余量检查)
[ ] C-27  orderType = "FOK" (立即成交或 Rejected, 不留悬空单) — 测试单首选
[ ] C-28  negRisk = false 的 Moneyline market (避免 negRisk verifyingContract 复杂度)
[ ] C-29  acceptingOrders = true (下单前 GET /gamma/markets 确认该字段)
[ ] C-30  先用 curl 工具验证 L2 auth credentials: GET /auth/api-keys 返回 200 且 key 在列表
[ ] C-31  有 kill 开关: 能在 1 分钟内撤销所有挂单 (CancelAll 端点或手动 CancelOrder)
```

### §6.6 云端额外检查 (18.132.108.50)

```
[ ] C-32  云端 .env 权限 = 600 (chmod 600 && stat 确认)
[ ] C-33  云端 swap 已关闭 (swapoff -a 或 free -h 确认 Swap: 0)
[ ] C-34  SSH 使用 key 认证 (PasswordAuthentication no)
[ ] C-35  .env 未出现在 bash/zsh history (已用 stdin pipe 方式传输而非命令行参数)
[ ] C-36  云端 ulimit -c = 0 (在 trader 进程启动脚本或 systemd service 中设置)
```

---

## §7 测试单风险分析 + Fail-Safe

### §7.1 真钱 = 真风险

< $0.1 或 $1 真实单 = 使用真实 pUSD。最坏情况列举:

| 风险场景 | 触发条件 | 后果 | Fail-Safe |
|---|---|---|---|
| **签名 bug: 下错订单** | EIP-712 字段顺序错 / keccak padding 错 → sig 无效 | CLOB 返回 400 BadRequest, 单不成交 | dry-run C-20 先验 recover 地址 |
| **size 填错 (数量级错误)** | size_pUSD_micro 误填 100_000_000 ($100) | 真实下单 $100 | C-25 硬上限 assert |
| **向错误 token_id 下单** | condition_id 与 token_id 不匹配 | 买到错误市场 | C-22 动态查 neg_risk + token_id 来源唯一 |
| **余额不足触发失败** | pUSD 不足 | CLOB 4xx 拒单 | C-26 下单前查余额 |
| **GTC 挂单悬空** | 选了 GTC 且市场无对手盘 | 单挂在 book 上, 需要手动撤 | C-27 使用 FOK |
| **HMAC 401 重试风暴** | HMAC 计算错误导致 401 | 重试 → 可能触发速率限制 / 账号暂停 | C-30 先 curl 验证凭证; 401 时走 endpoint-matrix-v3 §B SOP |
| **私钥泄露 (最坏)** | 错误 log 了私钥 | 攻击者可签任意订单 / 转移资产 | C-14 grep 扫描; 威胁模型 I-01 SOP |

### §7.2 dry-run 签名 recover 验证 (C-20 展开)

```cpp
// 伪代码: dry-run 验证
uint8_t digest[32];
// ... 计算 EIP-712 digest ...

secp256k1_ecdsa_recoverable_signature rsig;
secp256k1_ecdsa_sign_recoverable(ctx, &rsig, digest, privkey_buf.data(), nullptr, nullptr);

// 序列化
uint8_t rs[64];
int recid;
secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, rs, &recid, &rsig);

// 恢复公钥
secp256k1_pubkey pubkey;
secp256k1_ecdsa_recover(ctx, &pubkey, &rsig, digest);

// 序列化未压缩公钥 (65B, 0x04 prefix)
uint8_t pubkey_bytes[65];
size_t pubkey_len = 65;
secp256k1_ec_pubkey_serialize(ctx, pubkey_bytes, &pubkey_len, &pubkey, SECP256K1_EC_UNCOMPRESSED);

// keccak256 公钥后 64 字节 (跳过 0x04 prefix)
uint8_t addr_hash[32];
kc::keccak256(pubkey_bytes + 1, 64, addr_hash);

// 取后 20 字节 = 以太坊地址
// 对比 addr_hash[12..31] 的 hex == POLYMARKET_FUNDER_ADDRESS (小写, 去掉 0x)
// 如果一致: 签名正确, 可以 submit
// 如果不一致: 停止! EIP-712 参数有误
```

---

## §8 已知 gap 汇总 + 建议 sprint 归属

| Gap | 严重度 | 说明 | 建议归属 |
|---|---|---|---|
| **.env 权限 644** | P1 (上线前必须修) | 当前 644, 需改 600 | GM 立即执行: `chmod 600 .env` |
| **SecureBuffer 无 mlock** | P1 (上线前必须修) | MVP 测试单在 live_pm_client.cpp 中手动 mlock | GM 在调用点加 |
| **setrlimit(RLIMIT_CORE)** | P1 (上线前必须修) | 目前无 main 级设置 | GM 在 live binary main() 加 |
| **keccak256 test vector 未有自动化验证** | P1 (上线前必须跑) | 手工跑一次即可 | GM 加 assert |
| **dry-run signature recover** | P1 (上线前必须验) | 防签名参数错导致下错单 | GM 加 dry-run |
| **MADV_DONTDUMP + PR_SET_DUMPABLE** | P2 (Sprint-1 末) | 深度防御, 不阻 MVP 测试 | 老孙 + 老沈 Sprint-1 |
| **libsecp256k1 vcpkg pin** | P2 (引入时) | 引入依赖时随手做 | GM 引入时 |
| **凭证轮换 SOP** | P3 (Sprint-2) | 90 天 key rotation | 老沈 Sprint-2 |

---

## §9 快速引用 — 关键文件路径

- `include/stcpp/crypto/ed25519.hpp` — SecureBuffer<N> 定义
- `src/stcpp/signer/v62/signer_v62.cpp` — SignerV62 (live mode 当前为 stub)
- `experiments/laosun-laoli-live-order/keccak256.h` — vendored keccak256 实现
- `src/stcpp/polymarket/clob_wss/polymarket_clob_subscriber.cpp:16` — P-09 log 红线
- `docs/RESEARCH/laosun-live-order-signing-plan-v1.md §4` — 老孙私钥安全 spec
- `docs/RESEARCH/laoli-live-submitorder-plan-v1.md §6.2` — 老李凭证 redact spec
- `docs/RESEARCH/laoshen-threat-model-v2.md §4 §5.1` — 老沈 P0 高危场景 + 缓解措施
- `docs/RESEARCH/laoshen-key-management-coreview-v1.md §2.4` — 内存防护四件套

---

*提交: 2026-05-31 by 小白 (#27, security-engineer)*
*next review: 首笔真实成交后 retro + Sprint-1 末与老沈对齐 MADV/PR_SET_DUMPABLE 补完*
*pending: GM 逐项勾完 §6 Checklist 后, 小白签字 "安全门通过, 可上真实单"*
