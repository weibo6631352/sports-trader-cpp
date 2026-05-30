# Receiver 白名单 v1 (Polymarket Polygon 合约清单)

- Owner: 老叶 (defi-onchain-advisor)
- Date: 2026-05-28
- 验收人: 老孙 (signer 调用方) + 老黄 (合规变更审批) + 老雷 (final sign-off)
- 关联: `docs/RESEARCH/laosun-key-management-v5.1.md` (Q10/B5 白名单校验, 原 v2 参考, 已删), `docs/RESEARCH/laoli-polymarket-api-spec-v1.md` (老李社区流传值)
- 截止: 2026-06-26 (Sprint-2 启动)
- 验证方法: polygonscan og:title meta 标签 + Polymarket GitHub 官方 repo addresses.json + 实测 verified contract 状态
- 验证日期: 2026-05-28 全部地址当日复核

---

## 0. TL;DR (给老孙 + 老雷)

**signer B5 校验里 typed_data.domain.verifyingContract 和 SignRequest.receiver_addr 必须命中下表白名单**, 否则一律拒签 + 高优告警.

- **核心交易合约 2 个** (v2 主用): CTF Exchange V2 + Neg Risk CTF Exchange V2
- **核心交易合约 2 个** (v1 兼容, 仅 6 个月过渡, deadline 2026-12-31 后下线): CTF Exchange + Neg Risk CTF Exchange
- **结算 / 赎回类 4 个**: Conditional Tokens, Neg Risk Adapter, UMA CTF Adapter, Neg Risk UMA CTF Adapter
- **抵押品 / 资金流 5 个**: USDC.e, USDC (native), USDT0, pUSD (PMCT v2), Neg Risk Wrapped Collateral, Neg Risk Vault, Neg Risk Operator, Neg Risk Fee Module
- **基础设施 3 个**: Proxy Wallet Factory, Safe Proxy Factory (Polymarket variant), Collateral Onramp/Offramp/Permissioned Ramp
- **自家 funder address**: 由 KMS-signed config 注入, 不写死在本文 (轮换时不动文档)

**白名单总条目数: 21** (Polygon mainnet 137; Amoy testnet 单独 §6 列, 仅 dev 用).

跨链: 当前 Polymarket 仅 Polygon 一条链, 无官方桥. 详见 §5. **多链 v2 = 暂不支持**.

---

## 1. 白名单 — Polygon Mainnet (chain_id=137)

### 1.1 主交易合约 (signer 必须命中之一才能签 Order)

| # | 名称 | 地址 | polygonscan 标签 | 用途 | EIP-712 domain |
|---|---|---|---|---|---|
| 1 | **CTFExchange V2** | `0xE111180000d2663C0091e4f400237545B87B996B` | "Polymarket: CTF Exchange V2" | v2 主交易 (普通市场) | `name="Polymarket CTF Exchange", version="2"` (待 老李 §3 重测确认) |
| 2 | **NegRiskCtfExchange V2** | `0xe2222d279d744050d28e00520010520000310F59` | "Polymarket: Neg Risk CTF Exchange V2" | v2 主交易 (负风险市场, 系列赛/outright) | 同上, neg risk 变体 |
| 3 | CTFExchange (V1, 过渡) | `0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E` | "Polymarket: CTF Exchange" | v1 兼容, 流动性逐步迁移到 V2 | `name="Polymarket CTF Exchange", version="1"` (老李实测) |
| 4 | NegRiskCtfExchange (V1, 过渡) | `0xC5d563A36AE78145C45a50134d48A1215220f80a` | "Polymarket: Neg Risk CTF Exchange" | v1 兼容 | 同 #3 neg risk 变体 |

**v1 过渡策略**: v2 已上线 (2026 Q1), 大流动性已迁移. 我们 v1 入口仅保留 6 个月做兼容, **2026-12-31 后白名单移除 #3 #4**. 详见 §4.2 升级机制.

### 1.2 结算 / 赎回 / 适配器 (signer 在 Redeem / Merge / Split 时命中)

| # | 名称 | 地址 | polygonscan 标签 | 用途 |
|---|---|---|---|---|
| 5 | Conditional Tokens Framework | `0x4D97DCd97eC945f40cF65F87097ACe5EA0476045` | "Polymarket: Conditional Tokens" | Gnosis CTF, redeemPositions / mergePositions 主入口 |
| 6 | Neg Risk Adapter | `0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296` | "Polymarket: Neg Risk Adapter" | neg risk 市场的 split/merge/convert |
| 7 | UMA CTF Adapter (v3.0.0) | `0x71392E133063CC0D16F40E1F9B60227404Bc03f7` | "Polymarket : UmaCtf Adapter" | UMA 预言机 -> CTF 结算回写 (通常我们不直接调, oracle 自动 callback, 但可能 trigger emergency) |
| 8 | Neg Risk UMA CTF Adapter | `0x2F5e3684cb1F318ec51b00Edba38d79Ac2c0aA9d` | "Polymarket: Neg Risk UMA CTF Adapter" | neg risk 变体 |
| 9 | Neg Risk Operator | `0x71523d0f655B41E805Cec45b17163f528B59B820` | (无 polygonscan label, 已 verified) | neg risk operator 角色 |

### 1.3 抵押品 + 资金流 (Approve 路径必须命中)

| # | 名称 | 地址 | polygonscan 标签 | 用途 |
|---|---|---|---|---|
| 10 | USDC.e (bridged) | `0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174` | "Circle: USDC.e Token" | 主用 collateral (v1 + v2 PMCT 上游) |
| 11 | USDC (native Circle) | `0x3c499c542cEF5E3811e1192ce70d8cC03d5c3359` | "Circle: USDC Token" | 备选 collateral, v2 支持 |
| 12 | USDT0 | `0xc2132D05D31c914a87C6611C10748AEb04B58e8F` | "USDT0: USDT0 Token" | 备选 collateral, v2 PMCT 支持 |
| 13 | pUSD (PMCT, V2 collateral proxy) | `0xC011a7E12a19f7B1f670d46F03B03f3342E82DFB` | "Polymarket: pUSD Token" | v2 wrapped collateral, Order 的 makerAmount / takerAmount 单位 |
| 14 | Neg Risk Wrapped Collateral (V1) | `0x3A3BD7bb9528E159577F7C2e685CC81A765002E2` | "Polymarket: Wrapped Collateral" | v1 neg risk 抵押 |
| 15 | Neg Risk Vault | `0x7f67327E88c258932D7d8f72950bE0d46975E11D` | (无 label, verified) | neg risk 资金池 |
| 16 | Neg Risk Fee Module | `0x78769D50Be1763ed1CA0D5E878D93f05aabff29e` | "Polymarket: Neg Risk Fee Module" | 费率收取 |
| 17 | Collateral Onramp (V2) | `0x93070a847efEf7F70739046A929D47a521F5B8ee` | "Polymarket: Permissionless Collateral Onramp" | USDC/USDT -> PMCT wrap |
| 18 | Collateral Offramp (V2) | `0x2957922Eb93258b93368531d39fAcCA3B4dC5854` | "Polymarket: Permissionless Collateral Offramp" | PMCT -> USDC/USDT unwrap |
| 19 | Permissioned Ramp (V2) | `0xebC2459Ec962869ca4c0bd1E06368272732BCb08` | "Polymarket: Permissioned Ramp" | 白名单 onramp (合规通道) |

### 1.4 基础设施 (proxy wallet 创建路径, 极低频)

| # | 名称 | 地址 | polygonscan 标签 | 用途 |
|---|---|---|---|---|
| 20 | Polymarket Proxy Wallet Factory | `0xaB45c5A4B0c941a2F231C04C3f49182e1A254052` | "Polymarket: Proxy Wallet Factory" | Magic-link 用户 proxy 创建 (signatureType=1) |
| 21 | Polymarket Safe Proxy Factory | `0xaacFeEa03eb1561C4e67d661e40682Bd20E3541b` | "Polymarket : Safe Proxy Factory" | 我们用的 1-of-1 Safe (signatureType=2) |

### 1.5 自家 Funder Address (KMS-signed config 注入)

不写死在本文档. signer 启动时从 KMS-signed config 注入, 支持轮换 (新旧 7 天 overlap):

```
funder_current  = <KMS-signed inject>
funder_previous = <KMS-signed inject, optional, 7d overlap>
```

老李 §3.3 实测 funder `0x78dE...8686BE` 是当前生产 funder, 但不入本文 (避免 PR 时手动 update).

---

## 2. 实测验证记录 (2026-05-28)

### 2.1 验证方法

对每个候选地址:
1. 调 `https://polygonscan.com/address/<addr>` 抓 `og:title` meta tag
2. 校验 title 含 "Polymarket" + 含对应功能关键词
3. 校验合约页显示 "Contract: Verified" (源码已开源)
4. 与 Polymarket 官方 GitHub repo (`Polymarket/neg-risk-ctf-adapter/addresses.json` + `Polymarket/ctf-exchange-v2/README.md`) 交叉确认

### 2.2 复现 curl 命令 (可重跑, 不含凭证)

```bash
for addr in \
  "0xE111180000d2663C0091e4f400237545B87B996B" \
  "0xe2222d279d744050d28e00520010520000310F59" \
  "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E" \
  "0xC5d563A36AE78145C45a50134d48A1215220f80a" \
  "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045" \
  "0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296" \
  "0x71392E133063CC0D16F40E1F9B60227404Bc03f7" \
  "0x2F5e3684cb1F318ec51b00Edba38d79Ac2c0aA9d" \
  "0x71523d0f655B41E805Cec45b17163f528B59B820" \
  "0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174" \
  "0x3c499c542cEF5E3811e1192ce70d8cC03d5c3359" \
  "0xc2132D05D31c914a87C6611C10748AEb04B58e8F" \
  "0xC011a7E12a19f7B1f670d46F03B03f3342E82DFB" \
  "0x3A3BD7bb9528E159577F7C2e685CC81A765002E2" \
  "0x7f67327E88c258932D7d8f72950bE0d46975E11D" \
  "0x78769D50Be1763ed1CA0D5E878D93f05aabff29e" \
  "0x93070a847efEf7F70739046A929D47a521F5B8ee" \
  "0x2957922Eb93258b93368531d39fAcCA3B4dC5854" \
  "0xebC2459Ec962869ca4c0bd1E06368272732BCb08" \
  "0xaB45c5A4B0c941a2F231C04C3f49182e1A254052" \
  "0xaacFeEa03eb1561C4e67d661e40682Bd20E3541b"; do
  curl -sS -A "Mozilla/5.0" --max-time 12 "https://polygonscan.com/address/$addr" \
    | grep -oE 'og:title" content="[^"]+"' | head -1
done
```

### 2.3 GitHub 权威源

- ctf-exchange-v2 README.md (主交易 v2 地址): https://github.com/Polymarket/ctf-exchange-v2/blob/main/README.md
- ctf-exchange README.md (v1, 已写明 "addresses updated, use V2"): https://github.com/Polymarket/ctf-exchange
- neg-risk-ctf-adapter addresses.json (neg risk 全套): https://github.com/Polymarket/neg-risk-ctf-adapter/blob/main/addresses.json
- uma-ctf-adapter releases v3.0.0: https://github.com/Polymarket/uma-ctf-adapter/releases/tag/v3.0.0

### 2.4 与老李 §3.2 流传值的差异 (重要)

老李 §6.2 写:
> verifyingContract = 0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E  # Exchange (非 negRisk)
>                   或 0xC5d563A36AE78145C45a50134d48A1215220f80a  # Exchange (negRisk)
>                      (具体地址以老叶给的链上配置为准, 我这里是社区实测值)

老李给的是 **v1 地址, 当前生产 API 仍接受** (我从 v1 README 头部确认 "deprecated, see V2"). 但 v2 上线后, Polymarket 官方 SDK 默认 sign v2 域. 我们 trader 实现必须:

1. 默认对 v2 (#1 #2) 签名
2. 兼容 v1 (#3 #4) 6 个月, 但 RiskManager 老韩 should default v2
3. v1 / v2 选择由 trader 读 gamma `events[].markets[].clobTokenIds` 时附带的 metadata 决定 (待老李 §6 与 Polymarket 对齐 API 字段)

**Open question @老李 Q-O1**: gamma / clob API 返回 market 时, 哪个字段标识 v1 还是 v2? 我猜是 `feeType` 或 `exchange` 字段, 老李 Sprint-1 末再实测一遍.

---

## 3. signer 校验逻辑 (B5 集成)

老孙 v2 §3.2 step 6/7:
```
6. domain.verifying_contract ∈ receiver_whitelist? 否则 reject
7. receiver_addr ∈ receiver_whitelist? 否则 reject
```

我这里细化:

### 3.1 按 Intent 分级校验

```rust
match req.intent {
    Intent::Order => {
        // typed_data.domain.verifying_contract ∈ {#1, #2, #3, #4}
        // (Order 只能签给主交易合约)
        // 同时校验 typed_data 里的 makerAmount unit 与 collateral 一致
        // (v2 用 PMCT #13, v1 用 USDC.e #10)
    }
    Intent::Cancel => {
        // 同上, 但 Cancel 不需要审批, 任意金额放行
    }
    Intent::Approve => {
        // verifying_contract ∈ ERC20 list {#10, #11, #12, #13}
        // spender (Approve 目标) ∈ {#1, #2, #3, #4, #17, #18, #20, #21}
        // **强制走 双签 审批** (老雷 + 老沈)
    }
    Intent::Redeem | Intent::Merge | Intent::Split => {
        // verifying_contract ∈ {#5 Conditional Tokens, #6 Neg Risk Adapter}
        // 不是直接签 typed_data, 而是签 EOA Tx, 走 EOA_Tx intent
    }
    Intent::EOA_Tx => {
        // to ∈ 全白名单
        // 严格 review, 走 单签 或 双签 (金额阈值)
    }
    _ => reject("unknown intent"),
}
```

### 3.2 amount unit 与 collateral 类型一致性

v1 Order 的 makerAmount / takerAmount 单位是 6-decimals USDC.e (老李 §6.2 实测).
v2 Order 的 makerAmount / takerAmount 单位是 6-decimals PMCT (pUSD, #13).

signer 必须根据 verifyingContract 决定预期 unit, 不一致拒签:

```
if verifyingContract in {#1 #2} (V2):
    expect makerAmount unit == pUSD (6 decimals)
elif verifyingContract in {#3 #4} (V1):
    expect makerAmount unit == USDC.e (6 decimals)
```

实务上这两个 6 decimals 一样, 但 v2 是 wrapped, 用户必须先把 USDC 走 #17 Onramp 进 PMCT, 才能挂单. trader 端在下 v2 order 前必须确保 funder 有足额 PMCT 余额, 否则单 broadcast 上链失败.

### 3.3 拒签时的错误码 (老孙 SignStatus enum 扩展)

老孙 v2 §3.1 写了 `RejectedReceiverNotWhitelisted`, 我建议拆细:

```rust
RejectedVerifyingContractUnknown,    // domain.verifyingContract 不在白名单
RejectedReceiverUnknown,             // receiver_addr 不在白名单
RejectedIntentReceiverMismatch,      // intent 是 Approve 但 receiver 不是 ERC20
RejectedCollateralUnitMismatch,      // v1/v2 unit 不一致
RejectedSpenderUnauthorized,         // Approve 的 spender 不在子白名单
RejectedV1Deprecated,                // 2026-12-31 后命中 v1 直接拒
```

---

## 4. 升级机制 (Polymarket 升级合约时如何同步)

### 4.1 触发场景

| 场景 | 频率 | 影响 | 处理 |
|---|---|---|---|
| Polymarket 升级主交易合约 (v2 → v3) | 1-2 次/年 | 高 | ADR + Risk Memo + 双签 (老黄 + 老沈) + 灰度 |
| 新增 collateral (e.g. DAI) | 不确定 | 中 | ADR + 老黄 sign-off, 不下线旧的 |
| neg risk adapter / operator 地址变更 | 极少 | 中 | 同上 |
| UMA adapter 版本升级 (v3 → v4) | 1 次/2 年 | 低 | 同上 |
| Polymarket 临时 emergency upgrade (e.g. bug fix) | 罕见 | 取决于 | 紧急流程, 老雷批 |

### 4.2 v1 -> v2 过渡示例 (当前进行时)

```
2026-Q1 (已发生): Polymarket v2 上线
2026-05-28 (今天): 我们白名单同时收 v1 #3 #4 + v2 #1 #2
2026-06-26 (Sprint-2 启动): trader 默认走 v2 但保留 v1 兼容
2026-09-30: 检查 Polymarket v1 上 24h 流动性是否 < $10k, 若是则准备下线
2026-12-31: v1 #3 #4 从白名单移除, 改为黑名单 (命中拒签, 不仅是不放行)
2027-01-15: ADR 归档 v1 退役决定
```

### 4.3 同步流程 (ADR 走法)

```
触发: Polymarket 官方公告新合约地址 (Twitter / Discord / docs.polymarket.com)
  ↓
老叶 (defi-onchain-advisor) 24h 内:
  1. polygonscan 验地址 verified
  2. github addresses.json 对账
  3. 与官方 SDK (py-clob-client / polymarket-js-sdk) 默认 domain 对账
  4. 写 ADR: "白名单变更 vX, 添加/移除 <地址>, 理由..."
  ↓
老黄 (compliance) review:
  - 新合约是否触发新 OFAC / 制裁地址
  - 是否影响合规结构 (e.g. 新合约引入美国 IP 限制)
  ↓
老沈 (security) review:
  - 合约 audit 报告是否齐 (Quantstamp / Cantina / Code4rena)
  - bug bounty 状态
  - 历史漏洞记录
  ↓
老孙 (signer) review:
  - typed_data domain 字段变化 (name / version / verifyingContract)
  - 是否需要 signer 代码改动
  ↓
老雷 final sign-off → PR merge → KMS-signed config 重签 → 热加载 (§4.4)
```

### 4.4 ADR 模板 (放在 `docs/ADR/whitelist-update-vN.md`)

```
# ADR-XXX: Receiver 白名单变更 v{N}

- 触发: <Polymarket 公告链接>
- 决策日期: <YYYY-MM-DD>
- 审批: 老叶 (proposer), 老黄 (合规), 老沈 (security), 老孙 (signer), 老雷 (final)

## 变更
- 添加: <地址> 名称 用途 polygonscan 验证日期
- 移除: <地址> 名称 原因 影响 (有无 inflight 仓位)
- 保留: ...

## 验证
- polygonscan verified: yes
- audit report: <链接>
- 与 SDK 对齐: yes
- typed_data domain 变化: [...]

## 影响评估
- 老韩 RiskManager: 是否需要重新校准
- 老孙 signer: 是否需要代码改动
- 老李 API spec: 是否需要 schema 更新

## Rollback 计划
- 灰度: 前 24h 仅允许 cancel, 不允许新开仓
- 24h 后无异常 → 全量
- 灰度期发现问题 → 直接 revert KMS-signed config
```

---

## 5. 白名单热加载 (不重启 signer)

### 5.1 设计原则

老孙 v2 §3.3 写 "白名单升级走 PR + 三签, KMS-sign config 文件烧死, 运行时不可改". 这是默认安全态. 但每次 ADR 都重启 signer 不现实 (尤其 active-standby 也得双重启 + 切流). 改进:

### 5.2 KMS-signed config 双版本机制

```
/etc/sports-signer/whitelist-v2026-05-28.kms-signed.toml    # 当前生效
/etc/sports-signer/whitelist-v2026-12-01.kms-signed.toml    # 预加载, 等切换
```

- signer 启动时加载 active config (timestamp 最大且当前 epoch 包含的)
- 通过 SIGHUP 信号触发 reload — signer 重新验签 + 替换 in-memory 白名单 + 不丢 wallet key
- reload 前后用 `nonce_mgr.observe()` 确保 inflight 全部 commit, 避免热切换中 deny 已 inflight tx

### 5.3 热加载安全性

风险: SIGHUP 被恶意进程发起 (E-class 横向).

缓解 (与老孙 B1 协议合作):
1. SIGHUP 只接受来自 init/systemd 或特定 uid (老吴 RunAs root + ExecReload signed script)
2. signer reload 时重新做完整 KMS unwrap + cert pin (B2 流程), 不是简单 reread file
3. reload 必须 audit (老唐 WAL):
   ```
   ts, reload_trigger_pid, reload_trigger_uid,
   old_config_hash, new_config_hash,
   added_addresses, removed_addresses,
   approver_signature  // KMS-signed-by 老叶+老黄+老沈+老雷 quadruple
   ```
4. reload 失败 (config 签名验证失败) → 保留旧 config, 不切换, 高优告警

### 5.4 热加载流程时序

```
t=0    老雷 sign-off ADR-XXX → CI 生成新 whitelist toml
t=10s  CI 用 KMS-signed 流程签新 toml, 上传 artifact
t=30s  老吴 ansible deploy 到 signer-A, signer-B (双机)
t=35s  老吴 systemctl reload sports-signer@A
t=36s  signer-A SIGHUP → verify new config → 替换 in-memory whitelist
       (verify 失败 → 保留旧, 告警 P0)
t=37s  signer-A 重新 listen, 业务无中断 (UDS connection 不断)
t=40s  老吴 reload signer-B
t=60s  trader 端 metric 显示新地址命中 ratio > 0%, 老叶确认成功
```

总切换 < 60s, 业务零中断 (因为 trader UDS 长连不需要重连).

### 5.5 与 active-standby 的交互

- reload 时必须 sequential (先 signer-A 再 signer-B), 不能同时
- reload 期间该 signer 短暂 (~1s) reject 新 SignRequest, trader 自动切到另一 signer
- 两 signer 都 reload 完才算完成

---

## 6. Amoy Testnet 白名单 (chain_id=80002, 仅 dev)

dev 环境用 Amoy. 地址不同, 不混入生产白名单, 单列:

| 名称 | 地址 | 来源 |
|---|---|---|
| CTFExchangeV2 (Amoy) | `0xE111180000d2663C0091e4f400237545B87B996B` | ctf-exchange-v2 README (Amoy 与 Polygon 地址相同!) |
| NegRiskCtfExchangeV2 (Amoy) | `0xe2222d279d744050d28e00520010520000310F59` | 同上 |
| CTFExchange V1 (Amoy) | `0xdFE02Eb6733538f8Ea35D585af8DE5958AD99E40` | ctf-exchange README |
| Conditional Tokens (Amoy, V1 testnet 残留 80001 Mumbai 数据) | `0x7D8610E9567d2a6C9FBf66a5A13E9Ba8bb120d43` | neg-risk-ctf-adapter addresses.json (Mumbai 80001) |
| Neg Risk Adapter (Amoy) | `0x9A6930BB811fe3dFE1c35e4502134B38EC54399C` | 同上 |
| USDC (testnet) | `0x2e8dcfe708d44ae2e406a1c02dfe2fa13012f961` | 同上 |

**注意**: Polymarket addresses.json 仍 dump 着 Mumbai (80001) 数据, Mumbai 已废弃, 应迁 Amoy (80002). 老李 Sprint-1 末再实测确认.

dev 环境必须强制 `chain_id != 137` 时禁止使用生产 wallet (B5 已校验 chain_id == 137).

---

## 7. 跨链支持 (v2 多链问题)

### 7.1 现状 (2026-05-28)

**Polymarket 现仅在 Polygon (137).** 无官方桥. 不存在跨链 trader 资金路径.

实测核对:
- docs.polymarket.com (官方): 唯一支持链 = Polygon
- ctf-exchange-v2 deployment: Polygon + Amoy testnet
- 无 Optimism / Arbitrum / Base / zkSync 部署

### 7.2 用户充值入口 (与我们 trader 无关)

Polymarket 前端给散户提供 Layerswap / Across / cBridge 等第三方桥 (USDC ETH/Arb/Op -> USDC Polygon). **这是 Polymarket UI 层集成, 与链上合约无关, 不进我们白名单**.

我们 trader 不走桥, funder 直接持 USDC.e on Polygon, 由人工 (老吴/老雷) 充值.

### 7.3 跨链 v2 规划 (若 Polymarket 扩链)

若未来 Polymarket 部署 v3 到其他链 (传闻可能 zkSync / Base):

1. 新建白名单文件 `whitelist-base-v...toml`, 与 Polygon 白名单完全隔离
2. signer 必须按 chain_id 路由到对应白名单
3. wallet 必须分链, 不允许同 EOA 跨链 (avoid replay 风险即使 EIP-155 有 chain_id 防护, 但 nonce 管理跨链复杂度高)
4. Risk Memo + ADR 全套流程, 不省

**当前态: 拒绝 chain_id != 137 的任何 SignRequest** (老孙 B5 step 5 已实现).

### 7.4 关于"Polygon CDK / AggLayer" 的预判

Polygon 2024-2026 推 AggLayer 联通各 zk chain. 如果未来 Polymarket 把 v3 部署到 AggLayer 上某个 L2, USDC 可能跨链同步. 我们届时需要:
- AggLayer bridge 合约白名单
- 跨链 nonce manager 扩展
- 与 老沈 / 老黄 重新做威胁模型

不在 Sprint-1/2 范围. 监控 Polymarket Discord 任何"multi-chain"传闻, 第一时间报老雷.

---

## 8. 验收 checklist (Sprint-2 启动前)

### 8.1 白名单完整性

- [ ] 21 个 Polygon 地址全部 polygonscan og:title 验证通过 (2026-05-28 已做, deploy 前再做一次)
- [ ] github addresses.json 对账无遗漏
- [ ] 自家 funder address 不在本文档, 由 KMS-signed config 注入
- [ ] Amoy testnet 地址单文件隔离, 不与 mainnet 混

### 8.2 signer 集成 (老孙 B5)

- [ ] signer 内部白名单数据结构 (HashSet<Address>, 启动加载, 不再触磁盘)
- [ ] domain.verifying_contract 校验 + 拒签错误码细分 (§3.3)
- [ ] receiver_addr 校验 (与 verifying_contract 不同语义都查)
- [ ] intent 与白名单子集映射 (Order/Cancel/Approve/Redeem 各自子白名单)
- [ ] amount unit 与 collateral 类型一致 (v1 USDC.e / v2 pUSD)
- [ ] chain_id != 137 直接拒
- [ ] EIP-712 byte-equal 测试 (与 py-clob-client 100+ 向量交叉, 与老李 协作)

### 8.3 热加载 (§5)

- [ ] SIGHUP reload 实现
- [ ] KMS-signed config 双版本机制
- [ ] reload audit WAL (老唐 对齐)
- [ ] reload 失败 → 保留旧, 高优告警
- [ ] active-standby sequential reload, 业务零中断 (chaos drill 验证)

### 8.4 ADR 流程 (§4)

- [ ] ADR 模板入 `docs/ADR/TEMPLATE-whitelist-update.md`
- [ ] 与老黄 合规 redline 对齐
- [ ] 与老沈 security 对齐
- [ ] 老雷 final sign-off 流程定义

### 8.5 监控 (与小郑 S1-018)

- [ ] metric: `signer_whitelist_size{kind}` gauge
- [ ] metric: `signer_reject_total{reason}` counter, reason = unknown_contract / unknown_receiver / chain_id / unit_mismatch / v1_deprecated
- [ ] metric: `signer_whitelist_reload_total{result}` counter
- [ ] metric: `signer_whitelist_config_hash` gauge (hash 切换时变化)
- [ ] alert: signer reject rate > 1% 持续 5min → P1
- [ ] alert: 命中 v1 合约 > 0% 在 2026-12-31 之后 → P0

---

## 9. 残留问题

| # | 问题 | Owner | Deadline |
|---|---|---|---|
| W1 | gamma/clob API 怎么标识 market 是 v1 还是 v2? `feeType`? `exchange`? 字段? | 老李 | Sprint-1 末 |
| W2 | v1 -> v2 流动性迁移真实进度, 何时下线 v1 才不影响业务 | 老李 + 老彭 | 2026-08 |
| W3 | Neg Risk Operator (`0x7152...`) verified 但无 polygonscan label, 是否安全 | 老沈 second review | Sprint-2 W1 |
| W4 | EIP-712 domain.version 字符串 v2 是 "1" 还是 "2"? README 没明确 | 老李 实测 | Sprint-1 末 |
| W5 | PMCT (pUSD) 上下 ramp 的 fee / 失败率 实测 | 老叶 自查 | Sprint-2 W2 |
| W6 | Polymarket Discord 是否有 "v3" / "multi-chain" 路线公告 | 老叶 + 老彭 | 每月监控 |
| W7 | UMA adapter v4 / NegRisk operator 升级 / FeeModule 重部署 在 Sprint-2 期间是否会发生 (Polymarket 升级窗口) | 老叶 | 每周扫一次 |

---

## 10. 一句话总结 (给老雷的决策)

**白名单 21 个 Polygon mainnet 地址 (v2 主用 + v1 6 月过渡 + 结算/抵押/基础设施), 全部当日 polygonscan + GitHub addresses.json 双源验证, 跨链 v2 不支持 (现状无桥, 未来扩链时另开 ADR)**.

热加载机制让升级不重启, KMS-signed config 双签流程兜底安全. 关键依赖老李 W1 W4 (Sprint-1 末实测 v2 API 字段) — 不阻塞我交付, 但阻塞 trader 端 v1/v2 路由实现.

---

*v1 提交时间: 2026-05-28*
*验证日期: 2026-05-28*
*下次复核: 2026-06-19 (Sprint-1 末, 配合老李 W1/W4 实测)*
*Sprint-2 启动前老雷 sign-off: 2026-06-26*
