---
owner: 老孙 (#06, 加密签名专家, A 系统工程部 IC)
last_review: 2026-05-29
status: DRAFT — 待老郭 (F 协调, 架构评审) + 老韩 (B 主管, RM 主权) 联合背书
trigger: GM 老雷直派 (红线 P0, ADR-005 跨单元安全红线例外) — 小白 security 预审 C-2 关闭
cite:
  xiaobai_gap_list: docs/RESEARCH/xiaobai-security-preaudit-gap-list-v1.md
  laoshen_spec:     docs/RESEARCH/laoshen-rm-v0.5-field-freeze-spec-v1.md §3 §4
  signer_impl:      src/stcpp/signer/v62/signer_v62.cpp (SignerV62 v6.2)
  ed25519_wrapper:  include/stcpp/crypto/ed25519.hpp (SecureBuffer<N>)
  paper_signer:     src/stcpp/signer/paper/paper_signer.cpp
  signer_cmake:     src/stcpp/signer/v62/CMakeLists.txt
  audit_baseline:   d97e952 (SignerV62 v6.2, OrderIntent v0.6)
---

# Paper Mock Keypair 隔离 + C-2/C-3 关闭方案 v1

> 本文是老孙对小白 C-2/C-3 gap 的 signer 层关闭方案。
> 覆盖: (1) paper mock keypair 隔离现状确认 + 强化点；
>        (2) C-2 Frankfurt paper server 真私钥剔除立即动作；
>        (3) C-3 CI grep 守卫设计；
>        (4) C-1 ADR 触发点 (推 pre-G4，本文标 owner+时点)。
>
> 实施边界: 老孙负责 signer 层设计 + IPC 协议 + byte-equal 测试。
> `.env` 文件权限 / Frankfurt 部署脚本由老吴执行; CI grep 守卫由老高接线。
> 私钥存储方案由老沈 + 老韩主导 ADR，老孙不越界。

---

## §1 Paper Mock Keypair 隔离现状审查

### §1.1 隔离机制层次 (commit d97e952 审查结论)

审查基准: `signer_v62.cpp` + `ed25519.hpp` + `paper_signer.cpp` + CMakeLists.txt 两级。

**层 L1 — build-time 宏隔离 (现已实现)**

`src/stcpp/signer/v62/CMakeLists.txt:32-48`:

```cmake
if(STCPP_EXEC_MODE STREQUAL "paper")
    target_compile_definitions(stcpp_signer_v62_paper
        PUBLIC STCPP_EXEC_MODE_paper=1
        PUBLIC STCPP_CLOB_V2=1
    )
```

- paper binary 编译时定义 `STCPP_EXEC_MODE_paper=1`。
- live signer 当前为 stub (未链接真私钥路径) — live CMake block 被注释，M5+ 才启用。
- 物理隔离: paper binary 不链接 `stcpp_signer_v62_live`，live binary 不链接 `stcpp_signer_v62_paper`。
- 结论: **PASS — build-time 物理隔离已实现**。

**层 L2 — 构造期 keypair 生成路径 (现已实现)**

`signer_v62.cpp:140-153` (SignerV62 constructor):

```cpp
if (mode_ != execution::ExecutionMode::Paper) {
    return;  // live/backtest → stub, 不生成 keypair
}
// paper: 随机生成 Ed25519 mock keypair
if (!GeneratePaperKeypair(pk_, sk_)) { return; }
keypair_valid_ = true;
```

- paper mode: 调 `crypto::Ed25519::generate_keypair` — libsodium `crypto_sign_ed25519_keypair` 随机生成。
- 生成的 sk_ 是全随机 32B seed 对应的 Ed25519 私钥，与 `.env WALLET_PRIVATE_KEY` (secp256k1 真私钥) 在算法和来源上完全隔离。
- **不读 `std::getenv("WALLET_PRIVATE_KEY")`**，不读 `.env` 任何字段。
- 结论: **PASS — paper 真私钥物理不进 paper 构造路径**。

**层 L3 — SecureBuffer 内存安全 (现已实现)**

`ed25519.hpp:76-121` (SecureBuffer<N>):

- 禁止拷贝 (`=delete`)。
- move 时清零 source (`sodium_memzero(other.buf_.data(), N)`)。
- 析构时 `sodium_memzero(buf_.data(), N)` — 编译器优化安全。
- 结论: **PASS — 私钥内存生命周期受控**。

**层 L4 — WalKind 硬填 R-11 (现已实现)**

`signer_v62.cpp:116-126` (WalKindForMode):

```cpp
case execution::ExecutionMode::Paper:
    return infra::wal::WalKind::PaperAudit;
```

- paper 模式签名响应硬填 `PaperAudit`，物理不可能路由到真账本。
- 结论: **PASS — 账本隔离已实现**。

### §1.2 现有隔离的两个强化点 (与老沈 R-11 spec §3.2 对齐)

小白 H-3 + 老沈 §3.2 指出的 gap，老孙 signer 层需补以下两点:

**强化点 A — 编译期 mode 自检 static_assert**

当前 paper binary 靠 CMake 注入 `STCPP_EXEC_MODE_paper=1`，但 signer_v62.cpp 内没有
编译期断言确认宏已定义。若 CMake 配置漂移（如手动 `-DSTCPP_EXEC_MODE=live` 但
未触发 CMake 重新 configure），可能导致宏不一致而运行期误判。

建议在 `signer_v62.cpp` 顶部补:

```cpp
// 编译期 mode 一致性断言 (R-7 + 老沈 §3.2 mode attestation)
// paper binary 必须 STCPP_EXEC_MODE_paper=1
// live binary 必须 STCPP_EXEC_MODE_live=1 (M5+ 启用时加)
#if defined(STCPP_EXEC_MODE_paper) && defined(STCPP_EXEC_MODE_live)
static_assert(false, "STCPP_EXEC_MODE_paper 和 STCPP_EXEC_MODE_live 不可同时定义 (R-7)");
#endif
```

**强化点 B — 运行期 mode/keypair 一致性断言**

`SignerV62::Sign()` 在 paper 模式下调用时，应断言 `keypair_valid_ == true`（已有判断
但返回 LibsodiumInitFail，可补一行 assert 日志，不含私钥内容）。

这两个强化点是 P2 优化（现有设计已足够安全），不阻塞 C-2 关闭。

---

## §2 C-2 立即关闭方案: Frankfurt Paper Server 真私钥剔除

### §2.1 问题定性

当前 `.env` 第 2 行:

```
WALLET_PRIVATE_KEY=0xe447ed3c2b7e05fa85c4928c8c9538574e8cacbc684cbaaabfc8683873e0b068
```

**这是真 secp256k1 私钥明文。** paper binary (signer_v62_paper) 在代码路径上完全不读
这个环境变量（已审查，无任何 `getenv("WALLET_PRIVATE_KEY")` 调用链进入 signer），
但它存在于 Frankfurt server `.env` 文件和进程环境变量中，满足"明文落盘"红线定义。

小白 C-2 判定正确: **paper 阶段此暴露已活跃，与 live 切换无关**。

### §2.2 关闭动作 (paper 部署前)

**动作 1 (立即，old吴执行): Frankfurt paper server .env 剔除真私钥**

paper server 的 `.env` 文件只保留 paper 运行所需的凭证:

```bash
# paper server .env (Frankfurt) — 真私钥不在此文件
# WALLET_PRIVATE_KEY 行整行删除 (paper binary 不需要真私钥)
DATABASE_URL=postgresql+asyncpg://...
POLYMARKET_FUNDER_ADDRESS=0x78dE3c8264C546Fffed8D9A1396cddEf7c8686BE
POLYMARKET_API_KEY=...
POLYMARKET_API_SECRET=...
POLYMARKET_API_PASSPHRASE=...
GOALSERVE_API_KEY=...
GOALSERVE_PROXY=...
```

`POLYMARKET_FUNDER_ADDRESS` 保留（是公开地址，不是私钥）。
`WALLET_PRIVATE_KEY` 整行删除。

**动作 2 (老吴执行): 文件权限收紧**

```bash
chmod 600 /path/to/.env
chown <deploy-user>:<deploy-user> /path/to/.env
```

仅部署用户可读。防止 `ps e`、`/proc/<pid>/environ` 侧信道（文件权限本身不能防
`/proc` 泄露，但减少直接文件读取面，配合动作 1 才彻底关闭）。

**动作 3 (老高执行): 部署脚本禁止 env dump 进日志**

CI/CD 部署脚本（如 Dockerfile entrypoint、systemd service）禁止以下模式:

```bash
# 禁止模式 (老高 CI 脚本 grep 守卫)
env
printenv
export -p
cat /proc/$$/environ
set (bash 全量 dump)
```

这些命令输出会把整个环境变量写入 stdout/stderr，若有日志收集就泄露。

**动作 4 (signer 层确认, 老孙): paper binary 不接受 WALLET_PRIVATE_KEY 输入**

老孙确认: `signer_v62.cpp` + `paper_signer.cpp` 中不存在任何读取 `WALLET_PRIVATE_KEY`
环境变量的代码路径。paper mock keypair 由 `Ed25519::generate_keypair()` 在进程内随机
生成，与 `.env` 完全无关。

若后续代码变更引入 `getenv` 调用，C-3 CI grep 守卫（见 §3）会拦截。

### §2.3 C-2 关闭判断

**C-2 能否在 paper 部署前关闭: 是，可以立即关闭。**

关闭前提:
- 动作 1 (剔除 `.env` 真私钥) — 无需代码变更，运维操作，老吴 < 1h 可完成。
- 动作 2 (文件权限) — 配套运维，< 30min。
- 动作 3 (部署脚本 env dump 禁止) — 老高 CI 配套，< 1 sprint。
- 动作 4 (signer 层确认) — 老孙已确认，本文即交付物。

**关闭验证步骤** (老吴部署后，老孙 / 小白复核):

```bash
# 1. 确认 .env 无真私钥行
grep "WALLET_PRIVATE_KEY" /path/to/.env
# 期望: 无输出

# 2. 确认进程环境变量无真私钥
# 启动 paper binary 后:
grep "WALLET_PRIVATE_KEY" /proc/<pid>/environ 2>/dev/null || echo "CLEAN"
# 期望: CLEAN (因 .env 不再含该行，进程不会继承)

# 3. 文件权限确认
stat /path/to/.env | grep Access
# 期望: Access: (0600/-rw-------)
```

---

## §3 C-3 CI 回归守卫: 私钥/签名误入 Log 防护

### §3.1 守卫目标

防止未来代码变更（任何贡献者，包括老孙自己）意外将私钥材料、原始签名字节打印到:
- `std::cout` / `std::cerr`
- `LOG_*` / `SPDLOG_*` / `spdlog::` 调用
- `audit_record` 字段（签名全量字节）
- `reject_reason` 字符串（不得含私钥十六进制）

### §3.2 grep 守卫设计 (老高接线)

在 `tests/ci_grep/` 新增 `privkey_log_guard.py`，与现有 `abi_lock.py` 同级。

**规则 G1 — 禁止私钥关键词与 log/print 同行出现**

扫描全量 `.cpp` / `.hpp` diff 新增行，拒绝同行同时含:

```python
PRIVKEY_TOKENS = {
    "WALLET_PRIVATE_KEY",
    "sk_",            # SecureBuffer 私钥成员名
    "secret_key",
    "private_key",
    "getenv(\"WALLET",
    "getenv('WALLET",
}
LOG_TOKENS = {
    "LOG_", "SPDLOG_", "spdlog::", "std::cout", "std::cerr",
    "printf", "fprintf", "puts(",
    "reject_reason =",   # 不得把私钥塞进 reject_reason 字符串
}
```

同行同时含 PRIVKEY_TOKEN + LOG_TOKEN → CI FAIL。

**豁免模式**: 含 `// CI-EXEMPT: <理由>` 的行（需老郭 24h 仲裁，与 abi_lock 同标准）。

**规则 G2 — audit_record schema 静态检查**

`include/stcpp/observability/audit_record.hpp` 中不得新增以下字段名:

```python
AUDIT_BANNED_FIELDS = {
    "signature",           # 完整签名 64B 禁止入 audit record
    "secret_key",
    "private_key",
    "sk_",
    "wallet_private",
}
```

扫描 diff 新增行中 `audit_record.hpp` 的 struct 字段定义，含上述名称 → CI FAIL。

老唐 (audit schema owner) review 此规则豁免列表（`sig_hash_prefix` 之类的截断字段允许）。

**规则 G3 — signer reject_reason 内容限制**

`signer_v62.cpp` 中 `resp.reject_reason = ` 赋值行，右侧字符串字面量不得含:
- 十六进制私钥格式 pattern (`[0-9a-f]{32,}`)
- 上述 PRIVKEY_TOKENS

这条规则通过编译期 static_assert 也可部分覆盖（如对 reject_reason 字符串长度上界），
但 grep 守卫更直接。

### §3.3 CI 接线 (老高)

在 `.github/workflows/pr.yml` 新增 job:

```yaml
privkey-log-guard:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
      with: { fetch-depth: 2 }
    - run: python3 tests/ci_grep/privkey_log_guard.py
      env:
        PR_BODY: ${{ github.event.pull_request.body }}
```

与 `ci-grep-abi-lock` 并行运行（不依赖）。

### §3.4 与现有守卫的边界划分

| 守卫 | 文件 | 覆盖面 | owner |
|---|---|---|---|
| `abi_lock.py` | 现有 | ABI 结构变更 + CMake crypto target 变更 | 老高 |
| `privkey_log_guard.py` | **新增 (C-3)** | 私钥/签名误入 log/audit 防护 | 老高 + 老孙 (规则设计) |

两个守卫均为 PR 级 CI gate，`PR_BODY` 未注入时静默 pass（非 PR 触发），与 abi_lock 一致。

---

## §4 给老沈/老郭的 ADR 触发点

### §4.1 C-1: live 真私钥来源 ADR (pre-G4 必交付)

**议题:** secp256k1 真私钥 (live signer M5+ secp256k1 ECDSA) 从哪里加载？

**三个备选方向** (老沈 + 老韩主导评估，老孙实施):

| 方向 | 描述 | 老孙 signer 层影响 |
|---|---|---|
| A — OS keyring (Keychain/libsecret) | macOS Keychain / Linux libsecret 按需解锁 | `SecureBuffer` 接收，禁 std::string 中转 |
| B — 加密文件 + passphrase (libsodium secretbox) | `.env` 只存 passphrase，私钥以 libsodium secretbox 加密落盘 | 启动期 unlock，SecureBuffer 持有，passphrase 内存即清零 |
| C — KMS (AWS KMS / HashiCorp Vault) | 私钥不出 KMS，signing API 调用 KMS | signer 层改为 RPC 调用，latency 可接受性需评估 |

**裁决 owner**: 老沈 (安全 spec) + 老韩 (RM 主权) + 老郭 (架构评审)。
**老孙角色**: 提供 signer 实施约束 (SecureBuffer 接口 + 内存清零 + IPC 协议设计)。

**ADR 触发时点**: M5 (paper 持续盈利 12 月后, G4 live 切换前 ≥ 3 个月)。
建议 M3 阶段启动评估 (G4 闸门约束，见小白 §0)。

**禁止事项** (老孙红线，不论 ADR 选哪个方案):
- live signer 中出现 `std::getenv("WALLET_PRIVATE_KEY")` 明文直读模式。
- 私钥进程内以 `std::string` 持有（只允许 `SecureBuffer<64>`）。
- 私钥字节进入任何 log / audit 字段（C-3 守卫覆盖）。

**ADR owner 建议**: 老沈 (主笔) + 老韩 (co-author) + 老郭 (架构评审 chair)。

### §4.2 C-1 ADR 前的 signer stub 保护

当前 `signer_v62.cpp:189` live mode 返回 `SignV62Error::InternalError "mode_not_paper"`。
这个 stub 是安全的——它拒绝任何 live 签名请求，不会意外使用真私钥。

M5+ 实施 live signer 时，stub 替换为真实实现，必须同步完成 C-1 ADR 决议。
**禁止在 C-1 ADR 决议前替换 stub**（即使 secp256k1 库已接入）。

---

## §5 A↔B↔F 协作矩阵

| 动作 | 责任人 | 依赖 | 时限 |
|---|---|---|---|
| 动作 1: Frankfurt .env 删 WALLET_PRIVATE_KEY | 老吴 (A 部署) | 本文 §2.2 | paper 部署前，立即 |
| 动作 2: .env chmod 600 | 老吴 | 动作 1 | 同步 |
| 动作 3: 部署脚本禁 env dump | 老高 (CI) | 老吴确认部署流 | 1 sprint 内 |
| 动作 4: signer 层确认 (本文) | 老孙 | 代码审查完成 | 已完成 (本文) |
| C-3 grep 守卫: privkey_log_guard.py | 老高 + 老孙 (规则) | §3 设计 | 1 sprint 内 |
| §1.2 强化点 A/B static_assert | 老孙 | 本文 | P2, 不阻塞 C-2 |
| C-1 ADR: live 私钥来源 | 老沈 (主) + 老韩 + 老郭 | M3 阶段启动 | pre-G4 ≥ 3 月前 |
| H-3 PAPER_LEDGER_GUARD CI 守卫 (symbol check) | 老高 + 老沈 | 老沈 §3.3 D7 | P0, 1 sprint |

---

## §6 关键结论汇总

**C-2 能否 paper 部署前关闭: 是。** 关闭路径纯运维 (删 `.env` 一行 + chmod 600)，
无需代码变更，无需 ADR，老吴 < 2h 可完成。老孙 signer 层已确认 paper binary
物理不读 `WALLET_PRIVATE_KEY`。

**signer 层隔离现状评级: 良好。** build-time 宏隔离 + 构造期随机 keypair + SecureBuffer
memzero + WalKind 硬填四层均已实现，commit d97e952 代码质量符合安全基线。
两个强化点 (static_assert + 日志 assert) 是 P2 防御深度补充，不影响当前安全性。

**C-3 守卫: 需新增 CI job。** 当前无自动化回归守卫，靠人工 review 必漏。
`privkey_log_guard.py` 设计见 §3，老高接线，老孙 co-review 规则。

**C-1 ADR: 明确标 owner+时点，不在本阶段解决。** live signer secp256k1 真私钥来源
是 pre-G4 P0 门禁，M3 阶段启动 ADR 评估，老沈+老韩+老郭主导，老孙实施。
当前 live stub 拒签保护有效，无需提前动。

**CLAUDE.md §8 红线评估:** C-2 关闭后，paper 阶段私钥明文落盘红线解除。
C-3 守卫部署后，私钥误入日志回归风险得到自动化覆盖。
C-1 (live 私钥来源) 仍为 G4 否决门禁，pre-G4 前必须有 ADR 决议。

---

*老孙 (#06, A 系统工程部 IC, 加密签名专家), 2026-05-29*
*派单: GM 老雷 (跨单元安全红线直派, ADR-005 例外)*
*合规 co-review: 老韩 (B 主管, RM 主权) + 老郭 (F 协调, 架构否决权)*
