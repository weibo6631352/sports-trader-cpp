# Security Audit 预审 — G4 上线前必关闭 Gap 清单 (Paper 阶段前置)

- **Owner:** 小白 (#F 顾问团, AI/LLM + security 预审)
- **Last review:** 2026-05-29
- **Status:** v1 DRAFT — 待老郭 (F 协调) review + 老韩 (RM 主权) / 老孙 (crypto-signing) 背书
- **派单来源:** GM 推进指令 §8.1 (老郭转派, P1, 6 月内交付)
- **顾问边界:** advise + review, 产 gap 清单不产实现; 实现归 IC (老孙 signer / 老沈 安全 spec / 老唐 audit / 老韩 RM)
- **审计基线 commit:** d97e952 (SignerV62 v6.2, OrderIntent v0.6) + .env 现状 + CMake 依赖现状

---

## §0 审计范围与方法

四维扫描 (R-33 流程红线对齐):
1. 一手源码: `signer_v62.{hpp,cpp}` / `signer_iface.hpp` / `crypto/ed25519.hpp` (SecureBuffer) / `paper/paper_signer.cpp` / `src/stcpp/crypto/CMakeLists.txt` / `.env` + `.gitignore`
2. SSOT cross-ref: GM 推进指令 §3 GM-PAPER-G 8 门禁 + §9 老郭架构红线 4 条 + CLAUDE.md §8 红线
3. 实测: `.env` git 跟踪状态 (`git ls-files` 确认未入 git) + 日志面 grep (无 sk_/secret 打印)
4. 同行基线: EIP-712 ECDSA / secp256k1 私钥管理通行实践 (M5+ live 切换面)

**G4 锚点:** 本清单的"上线前"= G4 (paper → 准实盘的 live 切换闸门, secp256k1 真私钥首次进场)。
GM-PAPER-G (§3) 是 paper 持续盈利门禁 (12 月)，G4 在其之后。**security gap 须在 paper 阶段就前置关闭**，
不能等 live 切换才补 — 因为 R-11 paper 零污染、私钥落盘红线、supply chain 在 paper 期已经全部活跃。

**映射表例规:** 每条 gap → 映射到具体 GM-PAPER-G 门禁条 (§3 八条) 或 §9 GM 架构红线条 或 CLAUDE.md §8 红线。
老郭防走过场要求: 写不出映射 = 打回。下方每条均带 `映射:` 字段。

---

## §1 G4 上线前必关闭 Security Gap 清单 (按严重度)

### CRITICAL (C 级 — 任一未关闭 = G4 否决)

#### C-1 — live secp256k1 真私钥来源/落盘路径未定义
- **gap:** 当前 live signer 是 stub (`signer_v62.cpp:189` → `InternalError "mode_not_paper"`)。M5+ 真切 secp256k1 时，真私钥从哪来 (`.env WALLET_PRIVATE_KEY` 明文 vs KMS/HSM vs 加密文件) **尚无设计**。`.env` 现含 `WALLET_PRIVATE_KEY=<明文>`。
- **风险:** 明文私钥落盘 = CLAUDE.md §8 红线 (系统权限暂停)。一旦 live signer 直接 `std::getenv("WALLET_PRIVATE_KEY")` 读明文进内存，进程 core dump / `/proc/<pid>/environ` / `ps e` 均可能泄露。
- **影响面:** 全资金安全。私钥泄露 = 钱包被清空，不可逆。
- **关闭标准:** ① live 私钥加载走 ADR 决策 (KMS / OS keyring / sodium-protected 加密文件三选一，禁明文 env 直读)；② 私钥进程内仅以 `SecureBuffer` 持有 (已有 wrapper)，禁拷贝到 `std::string`/日志/audit；③ 加载路径有单测验证内存离开作用域后清零。
- **owner 建议:** 老孙 (signer 实现) + 老沈 (安全 spec) + 老韩 (RM review)；ADR 走老郭评审。
- **映射:** CLAUDE.md §8「私钥明文落盘 / 出现在日志 → 系统权限暂停」+ §9 GM 架构红线 (红线不可妥协)。

#### C-2 — `.env` 明文私钥的运行期暴露面 (paper 期已活跃)
- **gap:** `.env` 已含 `WALLET_PRIVATE_KEY` / `POLYMARKET_API_SECRET` / `POLYMARKET_API_PASSPHRASE` / `DATABASE_URL` 明文。已确认未入 git (`.gitignore` 13-16 行覆盖 `.env`，`git ls-files` 无匹配 — 这一点 PASS)。但 paper runtime (Frankfurt server) 上 `.env` 仍以明文存在磁盘 + 注入环境变量。
- **风险:** paper 期虽用 mock keypair (R-11)，但 `.env` 里真 `WALLET_PRIVATE_KEY` 已在生产服务器明文落盘。任何进程能读环境变量 / 服务器被入侵 / 日志误打 `env` dump = 泄露。**这是 paper 阶段就存在的红线暴露，不是 live 才有。**
- **影响面:** 全资金 (真钱包私钥) + Polymarket API 凭证。
- **关闭标准:** ① paper 阶段服务器上 `.env` 不放真 `WALLET_PRIVATE_KEY` (paper 用 mock，真私钥不需要在 paper 服务器)；② 文件权限 `chmod 600` + owner-only；③ CI/部署脚本扫描禁止 `env`/`printenv` 全量 dump 进日志；④ 真私钥仅在 G4 live 切换时按 C-1 方案进场，不提前落 paper 服务器。
- **owner 建议:** 老吴 (Frankfurt 部署) + 老高 (CI) + 老沈 (安全 spec)。
- **映射:** CLAUDE.md §8「私钥明文落盘」+ R-11 paper 隔离 (§3 风控零失效条「paper 零污染真账本」延伸 — 真凭证不该出现在 paper 环境)。

#### C-3 — 日志/audit/异常路径的私钥与签名泄露面 (回归守卫缺失)
- **gap:** 当前实测 grep 无私钥打印 (PASS)，`audit_record.hpp` 不含 signature/secret 字段 (PASS)。但**没有自动化守卫防止未来回归**。`signer_v62.cpp` 的 `reject_reason` 字符串、core dump、`SecureBuffer` 之外的临时拷贝都是潜在泄露面。
- **风险:** 一次粗心 `LOG(sk_)` / 把 `signature` 全量打进 audit / panic 时 stack 含密钥 = 红线。无 grep 守卫则靠人工 review，必漏。
- **影响面:** 私钥 + 签名材料泄露。
- **关闭标准:** ① CI 加 grep 守卫 (类似已有 abi_lock)：禁止 `log/cout/print` 同行出现 `sk_/secret_key/private_key/WALLET_PRIVATE`；② audit_record schema 静态断言不含私钥/全量签名字段 (仅留 sig 哈希前缀或截断)；③ release build 禁 core dump 或 core dump 落加密盘。
- **owner 建议:** 老高 (CI grep 守卫) + 老唐 (audit schema) + 老沈。
- **映射:** CLAUDE.md §8「私钥出现在日志 → 系统权限暂停」+ §6 可追溯红线 (audit log 不得含敏感字段)。

### HIGH (H 级 — G4 前关闭，可 paper 期并行)

#### H-1 — supply chain: 依赖库版本 pin + 完整性校验不完整
- **gap:** libsodium PASS (URL_HASH SHA256 已 pin `ebb65ef6...`, 静态链接, `crypto/CMakeLists.txt:47`)。但 SSOT (CLAUDE.md) 提到的 simdjson / glaze / libpqxx / libcurl / libwebsockets / libsecp256k1 **当前主 CMake 未声明** (仅 libsodium + gtest 实际接入)。M5+ 接入这些库时若无 SHA256/tag pin + 来源校验，存在供应链投毒风险。
- **风险:** 未 pin 的依赖 (FetchContent GIT_TAG 用 branch 而非 commit / 无 URL_HASH) → 上游被篡改或 typosquat 进生产 binary。simdjson/glaze 解析不可信 REST/WSS 输入，是攻击面前沿。
- **影响面:** 整个生产 binary 完整性。
- **关闭标准:** ① 所有第三方依赖 FetchContent 用 `GIT_TAG <40-char commit sha>` 或 `URL + URL_HASH SHA256` (禁 branch/latest)；② 建依赖清单 SSOT (库名 + 版本 + sha + 用途 + owner)；③ CI 加 hash 校验 gate；④ libsecp256k1 (C-1 依赖) 接入时同标准。
- **owner 建议:** 老吴 (toolstack/构建) + 老张 (C++20 ABI/crate 选型顾问) advise + 老高 (CI gate)。
- **映射:** §9 GM 架构红线 (ABI 校验未过不得启 paper runtime — 供应链完整性是 ABI 信任根) + CLAUDE.md §8「跳过架构评审上重大变更」(引新依赖须评审)。

#### H-2 — simdjson/glaze 解析不可信输入的健壮性边界未审
- **gap:** Polymarket/Goalserve REST+WSS 是外部不可信输入。simdjson (小赵收口中) / glaze 解析的输入若构造恶意 payload (超长字符串 / 深度嵌套 / 数值溢出)，解析层是否有大小/深度上限、是否在 WSS event loop 同步解析 (撞 R-12 100us)，未审。
- **风险:** 恶意/异常 payload → DoS (event loop 阻塞 > 100us = R-12 P0) 或解析越界。`signer_v62.cpp` 的 `token_id` 接受 ~77 char uint256 字符串无上限校验 (仅非空)。
- **影响面:** event loop 可用性 (R-12) + 解析层内存安全。
- **关闭标准:** ① 所有外部输入解析有显式 size/depth 上限 + 拒绝超限；② `token_id`/`condition_id`/`metadata`/`builder` 入 signer 前长度上界校验 (condition_id/metadata/builder 已校 66 char bytes32 — PASS；token_id 仅非空，需补 ≤78 上限)；③ 解析不在 WSS event loop 同步执行 (R-12)。
- **owner 建议:** 小赵 (simdjson) + 老马 (hot path latency 守卫 R-12) + 老沈 spec。
- **映射:** §9 GM 架构红线 R-12 (event loop 阻塞 > 100us P0) + GM-PAPER-G 「风控零失效」(畸形输入不得绕过校验)。

#### H-3 — R-11 paper/真账本隔离的 security 边界 (见 §3 详述)
- **gap:** 详见 §3。摘要: paper signer 走 mock keypair + `WalKindForMode` 硬填 PaperAudit (`signer_v62.cpp:116-126` PASS)，但 mode 由构造期 `ExecutionMode` 决定，无运行期 tamper 防护，且 live/paper binary 物理隔离 (R-7) 的 CI 守卫强度未审。
- **风险:** paper 数据若因 mode 误配/binary 串味写入真 position/pnl_ledger/nonce_ledger = R-11 P0。
- **影响面:** 真账本完整性 + GM-PAPER-G 盈利数字可信度。
- **关闭标准:** 见 §3 关闭标准。
- **owner 建议:** 老韩 (R-11 主权) + 老唐 (audit replay verify) + 老沈。
- **映射:** §3 GM-PAPER-G 「风控零失效」条「paper 零污染真账本 (R-11)」+ §9 GM 红线 R-11。

### MEDIUM (M 级 — G4 前 best-effort，不阻塞 paper)

#### M-1 — DATABASE_URL 含凭证，DB 连接面未审
- **gap:** `.env DATABASE_URL` 含库凭证 (libpqxx 连 Postgres)。连接串明文 + 是否走 TLS + 注入面未审。
- **风险:** DB 凭证泄露 / 未加密连接 / SQL 注入 (若有动态拼接)。
- **影响面:** audit/ledger 数据完整性。
- **关闭标准:** ① DATABASE_URL 同 C-2 文件权限治理；② libpqxx 全参数化查询 (禁字符串拼接)；③ 跨洋链路 DB 连接强制 TLS (`sslmode=require`)。
- **owner 建议:** 小董 (数据) + 老唐 (audit DB)。
- **映射:** CLAUDE.md §8 可追溯红线 (audit log 完整性) + §6 留 audit log。

#### M-2 — Polymarket API 凭证 (key/secret/passphrase) 的 HMAC 签名面
- **gap:** `.env` 含 `POLYMARKET_API_{KEY,SECRET,PASSPHRASE}` (CLOB L2 HMAC 鉴权)。HMAC bug 4 教训已在 signer 注释固化 (`signer_v62.hpp:103-106`)，但 API secret 的内存持有/日志面同 C-3。
- **风险:** API secret 泄露 → 他人可代下单。HMAC 误用 (bug#2 sigType) 已有守卫 (PASS)。
- **影响面:** Polymarket 账户下单权限。
- **关闭标准:** ① API secret 同私钥级别治理 (SecureBuffer 持有，不入日志/audit)；② HMAC 计算路径单测 + 反模式 grep 守卫 (复用 C-3 守卫)。
- **owner 建议:** 老孙 + 老沈。
- **映射:** CLAUDE.md §8「私钥/secret 出现在日志」延伸 + 可追溯红线。

#### M-3 — Goalserve proxy 凭证 + ToS 速率合规
- **gap:** `.env GOALSERVE_PROXY` / `GOALSERVE_API_KEY` 明文。ToS 速率/反操纵红线 (CLAUDE.md §8) 与 security 交叉。
- **风险:** 凭证泄露 + 速率超限触 vendor ToS 红线 (P0 回滚)。
- **关闭标准:** 凭证同 C-2 治理 + 速率限流在数据层 enforce。
- **owner 建议:** 小段 (goalserve) + 老黄 (合规，G4 前激活，§9 裁决#7)。
- **映射:** CLAUDE.md §8 ToS 红线 + §3 数据 attestation 条 (数据源合规)。

---

## §2 私钥管理预审 (secp256k1 / Ed25519 — 存储 / 使用 / 日志面)

**当前状态 (paper 阶段，commit d97e952):**

| 面 | 现状 | 结论 |
|---|---|---|
| paper 私钥来源 | `Ed25519::generate_keypair` 随机生成 (`signer_v62.cpp:130-134`)，非真私钥 (R-11) | PASS — paper 不碰真私钥 |
| 内存持有 | `SecureBuffer<64> sk_`，析构/move 自动 `sodium_memzero` (`ed25519.hpp:76-121`) | PASS — 析构清零，禁拷贝，move 清源 |
| 拷贝防护 | `SignerV62` 禁拷贝 (`=delete`)，move 清源 (`signer_v62.cpp:165-169`) | PASS |
| 日志面 | grep 无 `sk_/secret/private` 打印；audit_record 无私钥/全量签名字段 | PASS (但无回归守卫 → C-3) |
| live 真私钥 | stub，**来源未定义** | **GAP C-1** |
| `.env` 真私钥 | `WALLET_PRIVATE_KEY` 明文落盘 (gitignore 已挡入 git，但运行期落盘) | **GAP C-2** |
| 回归守卫 | 无 CI grep 守卫防未来误打日志 | **GAP C-3** |

**关键判断:**
- paper 阶段私钥管理本体设计良好 (SecureBuffer + memzero + 禁拷贝 + mock keypair)，老孙/老沈底子扎实。
- **真正风险在 (a) live 切换私钥来源未设计 C-1；(b) `.env` 真私钥已在生产服务器明文落盘 C-2；(c) 无回归守卫 C-3。**
- secp256k1 (EIP-712 ECDSA) M5+ 接入时，私钥不应复用 Ed25519 的随机生成路径 — 真私钥须按 C-1 ADR 方案从受保护来源加载，且全程 SecureBuffer 持有。
- **强烈建议:** live signer 禁止 `std::getenv("WALLET_PRIVATE_KEY")` 明文直读模式 (现 `strategy_unlock_cli.cpp:78` 已有 getenv 模式，勿复用于私钥)。

---

## §3 R-11 Paper 零污染的 Security 边界 (与老韩 PAPER_LEDGER_GUARD 衔接)

**现有隔离机制 (实测):**
- `WalKindForMode` 按 `ExecutionMode` 硬填 audit_wal_kind (`signer_v62.cpp:116-126`): Paper→PaperAudit / Live→RiskAudit / Backtest→ShadowAudit。
- paper signer 用 mock keypair，签名非密码学有效 (`paper_signer.cpp` FillMockSignature 仅 trace 哈希)。
- R-7 三 binary 物理隔离 (CMake target 分离 paper/live/backtest)。

**Security 视角的 gap (映射到 H-3):**

| 边界 | gap | 关闭标准 |
|---|---|---|
| mode 完整性 | `ExecutionMode` 构造期决定，无运行期 tamper 检测；若误注入 Live mode 到 paper runtime 配置 → 真账本写入风险 | 启动期 mode attestation (binary 编译期 `STCPP_EXEC_MODE_*` 宏 + 运行期断言一致)；mismatch → 拒启动 |
| WalKind 强制 | 硬填 PASS，但下游 ledger 写入端是否**二次校验** WalKind 才允许写真 ledger 未审 | 老韩 PAPER_LEDGER_GUARD 在 position/pnl/nonce ledger 写入端断言 `WalKind != PaperAudit` 才落真账本；PaperAudit → 强制走 paper 影子库 |
| binary 串味 | R-7 CMake 隔离，CI abi_lock grep 拦 V1 signer，但是否拦 paper/live symbol 串链未审 | CI 守卫: paper binary 不得链接 live signer symbol (nm/grep 校验)；live stub 不得出现在 paper binary |
| audit replay | paper audit 与真 audit 同库混存风险 | 老唐 audit replay verify: PaperAudit 记录 replay 不得触发真 ledger 副作用 |

**衔接动作 (建议老韩主导):** 把上述 4 条写进 PAPER_LEDGER_GUARD spec，作为 GM-PAPER-G「风控零失效 / paper 零污染」条的 security 子项；老唐 audit replay 覆盖 PaperAudit 路径的零副作用验证。
- **映射:** §3 GM-PAPER-G 「风控零失效」条「paper 零污染真账本 (R-11)」+ §9 GM 红线 R-11 (paper 污染真账本 P0)。

---

## §4 Supply Chain 审查建议

| 库 | 当前状态 | 建议 |
|---|---|---|
| **libsodium 1.0.20** | URL_HASH SHA256 pin `ebb65ef6...` + 静态链接 + ABI static_assert (`ed25519.hpp:59-64`) | **PASS — 标杆**。其余依赖按此标准对齐 |
| **gtest** | 测试用，已接入 | 测试依赖，pin tag；不进生产 binary |
| **simdjson** | 主 CMake 未声明 (小赵收口中) | 接入时 URL_HASH/commit-sha pin；解析上限校验 (H-2) |
| **glaze** | 未声明 | 接入时 pin + header-only 来源校验 |
| **libpqxx** | 未声明 (M5+) | pin + DB 连接 TLS (M-1) |
| **libcurl / libwebsockets** | 未声明 (M5+) | pin + TLS cert 校验 (禁 `CURLOPT_SSL_VERIFYPEER=0`) |
| **libsecp256k1** | 未声明 (M5+ live signer 依赖) | C-1 依赖；pin + 来源校验 (bitcoin-core 官方) + 编译期常量审计 |

**通用 supply chain 关闭标准 (映射 H-1):**
1. 依赖清单 SSOT (`docs/RESEARCH/<owner>-dependency-manifest.md`): 库 + 版本 + sha + 用途 + owner + last_review。
2. FetchContent 一律 `GIT_TAG <40-char sha>` 或 `URL + URL_HASH SHA256`，禁 branch/latest/master。
3. CI hash 校验 gate (依赖下载 hash 不符 → 构建 fail)。
4. 引新依赖走架构评审 (老郭) — CLAUDE.md §8「跳过架构评审上重大变更」。
- **映射:** §9 GM 架构红线 (ABI 校验未过不得启 paper runtime — 供应链是信任根) + CLAUDE.md §8 架构评审红线。

---

## §5 Gap → GM-PAPER-G / 架构红线 映射汇总 (老郭防走过场)

| Gap | 严重度 | 映射门禁/红线 |
|---|---|---|
| C-1 live 私钥来源未定义 | CRITICAL | CLAUDE.md §8 私钥明文落盘 + §9 GM 红线 |
| C-2 `.env` 真私钥运行期落盘 | CRITICAL | CLAUDE.md §8 私钥明文落盘 + §3 paper 零污染延伸 |
| C-3 日志/audit 泄露回归守卫缺失 | CRITICAL | CLAUDE.md §8 私钥出现在日志 + §6 可追溯 |
| H-1 supply chain pin 不完整 | HIGH | §9 ABI 信任根 + CLAUDE.md §8 架构评审 |
| H-2 解析不可信输入健壮性 | HIGH | §9 R-12 event loop + §3 风控零失效 |
| H-3 R-11 paper 隔离 security 边界 | HIGH | §3 风控零失效 (paper 零污染 R-11) + §9 R-11 |
| M-1 DATABASE_URL/DB 连接面 | MEDIUM | CLAUDE.md §8 可追溯 + §6 audit log |
| M-2 Polymarket API HMAC secret 面 | MEDIUM | CLAUDE.md §8 secret 日志 + 可追溯 |
| M-3 Goalserve 凭证 + ToS 速率 | MEDIUM | CLAUDE.md §8 ToS 红线 + §3 数据 attestation |

**全部 9 条均有映射 — 无走过场条目。**

---

## §6 交付建议与移交

- **G4 否决门:** C-1 / C-2 / C-3 任一未关闭 = G4 不通过 (老韩 RM 主权 + 老郭架构否决权任一可叫停)。
- **paper 阶段必做 (前置):** C-2 (生产服务器 `.env` 不放真私钥) + C-3 (CI grep 守卫) + H-3 (PAPER_LEDGER_GUARD security 子项)，因为这些在 paper runtime 启动 (9-30) 就已活跃。
- **顾问移交:** 本清单 = advise + review 产物，不含实现。实现归 owner 建议列的 IC，ADR (C-1 私钥来源 / H-1 依赖清单) 走老郭评审入 ADR SSOT。
- **后续 review 触点:** ① 老韩 PAPER_LEDGER_GUARD spec 出后小白复核 §3 衔接；② M5+ live signer secp256k1 接入前小白复审 C-1 ADR；③ 各新依赖接入时小白 review supply chain pin。

---

*小白 (#F 顾问团), 2026-05-29 — G4 上线前 security 预审 gap 清单 v1。advise + review，不抢 IC 活。*
