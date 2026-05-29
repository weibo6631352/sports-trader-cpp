# 观测/调试 API 安全审计 v2 — 字段级终审 (score/quote/book_pair + RmDebugSnapshot + OrderBookSnapshotHub)

- Owner: 小白 (ai-llm-advisor, F 顾问 / security 预审)
- last_review: 2026-05-29
- 视角: security 顾问 advise + 字段级终审; **不实现** (落地归老沈/老吴/小冯/模块 owner)
- 触发: GM 派单 — 观测 API allowlist/黑名单字段安全审计 (含 ADR-040 新增 score/quote/book_pair + RmDebugSnapshot + OrderBookSnapshotHub)
- 审计对象:
  - `src/stcpp/debug_api/state_provider.hpp` (全部 struct)
  - `include/stcpp/risk/rm_debug_snapshot.hpp` (RejectRow + RmDebugSnapshot)
  - `include/stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp` (OrderBookFeatures + Hub)
- 基线: 延续 `xiaobai-observability-api-security-v1.md` §1 黑名单 / §2 访问控制 / §3 R-11 / §4 脱敏
- 结论先行: **全部 struct 通过 — 无一票否决项。** 黑名单字段在三个文件物理不存在(从源头杜绝);allowlist 字段全部安全;CORS `*`+loopback 组合可接受;3 项低/中风险建议(非阻塞)。

---

## 0. 核心立场延续 v1: 详细 ≠ 泄密

观测 API 的"详细"维度 = 业务/状态/时序;**不是**密钥/凭证字节。两者正交。v2 在字段粒度复核这条边界是否守住 —— 守住了。

设计上最强的保证不是"逐字段过滤",而是 **黑名单字段在 POD 里根本不存在**(物理杜绝)。即使序列化层写错也无字段可泄。本审计据此把"物理不存在"作为第一判据。

---

## 1. 黑名单字段物理不存在确认 (逐文件 × 逐 struct)

判据: 私钥字节 / mnemonic / shamir 分片 / 签名字节 (r,s,v / sig[] / digest) / Polymarket API_KEY·SECRET·PASSPHRASE / Goalserve API_KEY / DB password / HMAC·session·signing key / nonce 原值 / timestamp_ms 原值 / order_id (CLOB) / wallet address / 待签 calldata。

### 1.1 `state_provider.hpp` — 全部 struct 字段清册

| struct | 字段 | 黑名单命中 |
|---|---|---|
| FourTs | event/data_source/ingestion/as_of_ts_ns (int64 epoch_ns) | 无。R-20 派生时间戳,非 timestamp_ms 原值;epoch_ns 为对外标准维度 |
| HoldingView | market_id / outcome / net_qty / avg_entry_price / mark_price / pnl_realized / pnl_unrealized / as_of_ts_ns | 无 |
| PnlBucket | bucket_start_ts_ns / cum_net_pnl / realized / unrealized / fee / gas / n_trades | 无 |
| PnlPerMarket | market_id / net_pnl | 无 |
| PnlAttribution | gross/fee/gas/slippage/spread/net / as_of_ts_ns / per_market[] | 无 |
| RiskRejectRow | reason_code / market_id / intent_ref / side / size / price / rejected_ts_ns | 无 (详见 §2.1) |
| PaperGate | n_trades / ratios / sharpe / p_value / dd / pass flags / window_days / has_data / as_of_ts_ns | 无 |
| MetricsSnapshot | uptime / wss_*_connected / reconnect / loop_p99 / rm_* / fill / edge / pnl / staleness / gap / drift | 无 (全聚合标量,低基数) |
| TokenInfo | token_id / outcome / price / winner | 无 (token_id = ERC1155 asset_id,链上公开,详见 §2.3) |
| MarketInfo | found / condition_id / market_id / tokens[] / tick_size / fee_rate / neg_risk / neg_risk_market_id / accepting_orders / active / closed / resolved / source / as_of_ts_ns / event_id / slug / polymarket_url | 无 (全为公开协议字段 + gamma 公开元数据) |
| EventScore | found / event_id / sport / status / period / clock_sec / home / away / home_score / away_score / ts / source | 无 (公开赛事比分,Goalserve livescore 公开面) |
| QuoteParams | found / market_id / fair_value / market_mid / edge_bps / kelly_fraction / suggested_notional / signal_strength / model_conf / as_of_ts_ns | 无密钥类。**含策略 IP — 详见 §3 风险 R2** |
| BookLevel | price / size | 无 |
| BookSnapshot | found / token_id / condition_id / outcome / market_id / best_bid / best_ask / microprice / spread / imbalance / sequence_no / gap_count / wss_state / ts / source / bids[] / asks[] | 无 (sequence_no = 本地 WSS 序列号,非 nonce/order_id) |
| BinaryMarketBookView | found / condition_id / token0 / token1 / cross_spread / ts | 无 |
| ExecMode 枚举 | paper/live/backtest | 无 (R-11 必带,合规要求) |

**state_provider.hpp 黑名单命中: 0。** 全部为 std 标准库 POD(`<cstdint>` `<string>` `<vector>`),头注释 line 31-32 显式禁 `#include` 任何热路径模块头(risk/signer/exec),从编译期阻断密钥类型混入。

### 1.2 `rm_debug_snapshot.hpp` — RejectRow

RejectRow 字段(line 78-100): `reason_code[48]` / `market_id[72]` / `intent_ref[20]` / `side[8]` / `size_usdc` / `price` / `rejected_ts_ns`。

- **黑名单命中: 0。** 私钥/签名/nonce/timestamp_ms 原值/order_id/token_id 全值 物理不在此 struct(头注释 line 29-32 + line 75-76 明列)。
- `build_reject_row` (line 208-237) 投影逻辑复核: 只读 `condition_id` / `audit_id` / `side` / `size` / `price` / `decision_ts_ns`;**不读** token_id / timestamp_ms / metadata / builder / nonce(line 205-206 注释 + 实现一致)。
- `intent_ref` = audit_id **前 8 字节** hex(`audit_id_to_hex` line 186-197,循环上限 `i < 8u`)。这是内部 audit 引用,非签名/order_id 派生,无法反推私钥或重放。安全。
- `market_id` 注释明确 = condition_id 映射(公开协议字段),非 token_id 全值。安全。
- **编译期守护已存在**: `static_assert(sizeof(RejectRow) <= 256)` (line 103) + `is_trivially_copyable` (line 106)。前者注释明写"verify no secret field was accidentally added" —— 偷加密钥字段(如 32B 私钥 + 65B 签名)极易触发 256B 上限,这是一道被动的尺寸闸门(非充分,但有价值)。
- `safe_copy_cstr` (line 169-179) 保证 dst 末尾 NUL,无缓冲越界读泄露相邻内存。

### 1.3 `orderbook_snapshot_hub.hpp` — OrderBookFeatures

OrderBookFeatures 字段(line 112-150): 4×ts_ns / bids[5] / asks[5] (OrderBookLevel: price/size_usdc) / microprice / mid / spread / imbalance / sequence_no / gap_count / wss_state / valid。

- **黑名单命中: 0。** 纯微观结构派生 + 公开 L1-L5 深度 + WSS 序列号。无密钥、无 token 字符串(line 107: token_id 用外部 key 管理,不入 POD)、无 order_id、无 nonce。
- `sequence_no` / `gap_count` 为本地 WSS feed 序列控制,非链上 nonce,泄露无重放价值。
- `static_assert(is_trivially_copyable_v<OrderBookFeatures>)` (line 152) 同样形成"无堆/无 std::string 指针字段"的被动闸门。
- 映射说明(line 276-294)逐字段列出 OrderBookFeatures → BookSnapshot 映射,**全部落在 §1.1 BookSnapshot allowlist 内**,集成时不会引入新字段。集成实现 `RealBookStateProvider` 时仍需复核外部注入的 condition_id/outcome 映射不带额外数据(见 §6 移交)。

**三文件黑名单终审: 0 命中,通过。物理杜绝成立。**

---

## 2. allowlist 终审

### 2.1 RiskRejectRow / RejectRow (reason_code / intent_ref / side / size / price)

- `side` / `size` / `price`: 订单**意图摘要**,非签名字节、非私钥。被拒订单不会成交,size/price 暴露不构成持仓泄露或可被重放的下单材料。**安全,准入。**
- `intent_ref`: audit_id 前 8B hex,内部引用句柄。安全,准入。
- `reason_code`: 见 §2.2 专项。

### 2.2 reason_code 枚举字符串脱敏评估 (专项)

全量 22 个 RejectCode 字符串(`reject_enum.hpp` + `reject_code_to_str`)逐条评估是否泄露内部敏感细节:

| 类 | code | 评估 |
|---|---|---|
| 状态机 | STATE_HALTED / STATE_DRAIN / STATE_SAFE_MODE | 安全。仅暴露"系统处于保护态",标准做市机健康语义 |
| 重复/数据 | DUPLICATE_INTENT / STALE_DATA / INVALID_INTENT | 安全。通用校验语义 |
| 仓位/资金 | EXCEED_PER_ORDER_CAP / EXCEED_CONDITION_EXPOSURE / EXCEED_PER_OUTCOME_CAP / DAILY_LOSS_HALT / CONSEC_LOSS_HALT / INSUFFICIENT_BANKROLL | **语义边界项**。暴露"存在 per-order / per-condition / per-outcome / 日亏 / 连亏 / bankroll 风控维度",但**不带阈值数值**(阈值在 audit WAL,不出 API)。攻击者知道"有上限"不等于知道"上限是多少"。可接受。 |
| 信号 | EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE / STRATEGY_DECAYED | **策略 IP 边界项**。暴露公司用 edge 置信区间 / 滑点净化 / 策略衰减检测(Bayesian kill switch)。属"我们做了正经量化"的信号,但无参数、无模型、无阈值。可接受 —— 但与 QuoteParams 合看时见 §3 R2。 |
| 市场/流动性 | MARKET_TYPE_NOT_ENABLED / MARKET_NOT_ACTIVE / LOW_FILL_RATE / EXCESSIVE_SLIPPAGE / EXCEED_BOOK_DEPTH | 安全。通用市场/流动性语义 |
| 系统 | AUDIT_WAL_BACKPRESSURE / INTERNAL_ERROR | 安全。`UNKNOWN` 兜底防枚举越界泄内存 |

**关键不变量已守住**: RejectRow **不含 `sub_reason`**(头注释 line 80;`InvalidIntentSubReason` 13-16 含 TS_V2_* / INVALID_BYTES32_FORMAT 等更细的签名/metadata 格式细节,这些留在 audit WAL 不出 API)。这是正确的脱敏分层 —— 粗粒度 code 出 API,细粒度 sub_reason 留 WAL。

**结论: reason_code 字符串无需进一步脱敏,当前枚举可全量暴露。** 仅在外部/合规交付口需注意 §3 R2 的策略 IP 聚合风险(内部调试口无问题)。

### 2.3 token_id (ERC1155 asset_id) 暴露 — 复核老周裁定

老周裁定: token_id 可暴露。**我复核: 同意,准入。** 理由:

1. token_id = CLOB asset_id = ERC-1155 链上 positionId(`state_provider.hpp` line 170 / `TokenInfo` 注释)。**它是链上公开协议事实**,任何人查 Polygon 链或 Polymarket 公开 API 都能拿到 condition_id ↔ token_id 映射。暴露它不增加任何信息泄露(攻击者本就能从公开链数据构造)。
2. 它与 `intent_ref` / `audit_id` / 私钥**无密码学派生关系**,无法用 token_id 反推任何凭证。
3. **市场指纹风险评估**: 暴露"本系统在交易哪些 token"确实暴露了**标的选择 / 关注盘口**。这是一个真实但低烈度的信息泄露 —— 攻击者可推断公司在哪些市场活跃。但:
   - 该信息只在 loopback + SSH 隧道边界内可见(§4),非公网;
   - 做市报价本身就在公开 orderbook 上可见,"在交易哪些盘口"在 Polymarket 公开 book 上已半公开;
   - 真正敏感的是**报价参数/仓位规模/edge**(QuoteParams),而非"交易哪个 token"这一事实。
   - 归类为 §3 R2 的一部分(策略指纹),靠访问控制(§4)而非字段移除来缓解。

v1.1(注: v1 文档曾误标 v1.1,实为同篇)曾建议 token_id "不直接出";RejectRow 确实遵守了(只出 condition_id)。ADR-040 在 **market/book 端点**显式准入 token_id 是 per-token orderbook 的必要主键,与 RejectRow 的策略性脱敏不冲突 —— 两处定位不同,均正确。

### 2.4 condition_id / event_id / slug / polymarket_url

- `condition_id`: bytes32 链上公开主键。准入。
- `event_id`: 内部 vendor-agnostic 事件锚。准入(无密钥派生)。
- `slug` / `polymarket_url`: gamma 公开字段,polymarket.com 公开 URL。准入。

**allowlist 终审: 全部准入,无一字段需移除或脱敏。** 唯一需运营意识的是策略 IP 聚合面(§3 R2),靠访问控制缓解,非字段问题。

---

## 3. 风险项 (非一票否决;按烈度排序)

### R1 (中) — CORS `Access-Control-Allow-Origin: *` 复核

`server.cpp` line 100-122。复核结论: **当前可接受,但建议补一道收紧。**

- 现状理由(server.cpp 注释 line 103-109)成立: ① server 已 bind `127.0.0.1`(`server.cpp` line 52 默认 / `debug_server_main.cpp` line 60);② 全端点只读 GET,无 cookie/credential,W3C 允许 `*` + 无 credential 组合;③ 远程强制 SSH 隧道。
- **真实残余风险**: `*` + loopback bind 的组合下,**本机上运行的任意网页**(用户用同一浏览器访问的恶意站点)可向 `127.0.0.1:8080` 发 GET 并读到响应(DNS-rebinding / 本地恶意 JS 跨源读取)。loopback bind 挡的是远程网络,**挡不住本机浏览器里的恶意页面跨源读取**,而 CORS `*` 恰好放行了它。这就是 `*` 在 loopback 场景仍有意义的攻击面。
- **缓解**: 当前数据非密钥类(已 §1 确认),即使被本机恶意页读到,泄露的是业务/状态(策略指纹),不是凭证。烈度由此降为中。
- **建议(非阻塞)**: 将来引入任何写端点或 credential 前,**必须**改精确 allow-list(server.cpp line 109 注释已自我承诺,核可)。短期可选加固: 加 `Vary: Origin` + 校验 `Host` 头为 `127.0.0.1/localhost`(挡 DNS-rebinding),拒非 loopback Host。此为 R-defense-in-depth,不阻塞 MVP。

### R2 (中) — QuoteParams + reason_code 策略 IP 聚合泄露

- `QuoteParams` 暴露 `fair_value` / `edge_bps` / `kelly_fraction` / `suggested_notional` / `signal_strength` / `model_conf` —— 这是**公司 alpha 的近乎完整外显**(去佣公平价 + 净 edge + Kelly 仓位 + 信号强度 + 模型置信)。配合 §2.2 的 EDGE_*/STRATEGY_DECAYED reason_code,等于把策略逻辑摊开。
- 非密钥泄露,**不触发 §8 红线一票否决**;但属公司核心 IP。
- **缓解**: 完全依赖 §4 访问控制(loopback + SSH 隧道 + 不出公网)。**建议**: QuoteParams 端点在任何"对外/合规交付口"默认脱敏或禁用(对齐 v1 §4 OQ-10 脱敏开关,待老黄+小梁定)。内部盯盘看板全量 OK。**严禁** QuoteParams 端点 bind 到 `0.0.0.0` 或经反代暴露公网 —— 一旦暴露 = 策略被竞品复刻。这条应入运维红线 checklist。

### R3 (低) — 集成期回归风险 (RealStateProvider 映射)

- 当前 Stub/Demo provider 无黑名单字段(已扫描确认: demo_state_provider.hpp 0 命中)。但 §1.3 映射说明 + Hub 集成注释指出,后续 `RealBookStateProvider` / 老韩 RM 真实接入时,需从**外部**注入 condition_id/outcome 映射、从 RmDebugSnapshot 投影。
- **风险**: 真实接入若误把上游 OrderIntent 的 token_id 全值 / timestamp_ms / metadata 带进 RejectRow,或在 RealStateProvider 里新增字段绕过 allowlist。
- **缓解**: 编译期 `static_assert` (RejectRow ≤256B / trivially_copyable) + CI grep (§5) + 集成 PR 复审。`build_reject_row` 当前实现安全,但其为 template,接入方需保证传入的 OI/RD 类型不被改造成携带敏感字段后仍被该函数读取额外字段。**建议**: 集成 PR 必经一次 §1 字段复扫(列为集成 checklist 项)。

---

## 4. 访问控制 (延续 v1 §2,复核当前实现)

- **bind 默认 `127.0.0.1`** — `server.cpp` line 52 / `server.hpp` line 46-48 / `debug_server_main.cpp` line 60+75 / CMakeLists line 120 一致,**符合 v1 §2 advise,核可**。
- **远程走 SSH 隧道默认** — `debug_server_main.cpp` line 15-19 + server.hpp line 46 注释固化。核可。
- **全端点只读 GET + OPTIONS**(server.cpp line 114 `Allow-Methods: GET, OPTIONS`),无写面。核可。
- **signer 无 HTTP 口**(v1 §2 TB-B/TB-C): 本审计三文件均不引入 signer 依赖(state_provider.hpp line 31 禁 include / Hub line 42 / rm_debug_snapshot R-12 单向消费),**横向隔离成立**。
- **运维红线建议**: `--host 0.0.0.0` 启动参数虽存在(支持灵活部署),但应在部署文档/启动脚本里默认禁用 + 加显式告警。**建议** 老吴在部署封装层对 `0.0.0.0` bind 加二次确认或环境变量门禁,防误开公网(尤其 R2 的 QuoteParams)。

---

## 5. CI grep 守护规则建议 (黑名单字段守护)

现状: 仅 `.github/workflows/claude-review.yml` line 33 有 LLM review 软提示"私钥/secret 绝不入 response/log",**无确定性静态 grep 守护**。建议补一个轻量 CI 步骤(bash + grep,符合公司语言纪律,非 Python),对 `state_provider.hpp` / `rm_debug_snapshot.hpp` / `orderbook_snapshot_hub.hpp` / `endpoint_*.cpp` / `*_state_provider.hpp` 范围扫描。任一命中 → CI fail(一票否决)。

```bash
#!/usr/bin/env bash
# scripts/ci/observability-field-blacklist-guard.sh
# 黑名单字段守护 — 任一命中即 fail (小白 v2 §5)。
set -euo pipefail

SCAN_PATHS=(
  "include/stcpp/risk/rm_debug_snapshot.hpp"
  "include/stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
  "src/stcpp/debug_api/state_provider.hpp"
  "src/stcpp/debug_api/demo_state_provider.hpp"
)
# endpoint_*.cpp + 未来 *_state_provider.hpp 也纳入
mapfile -t EXTRA < <(git ls-files 'src/stcpp/debug_api/endpoint_*.cpp' '**/*_state_provider.hpp')
SCAN_PATHS+=("${EXTRA[@]}")

# 黑名单字段名 (struct member / 序列化 key 维度)。
# 注意: 命中的是"字段名/标识符"而非注释里出现的词 — 故匹配 . 或 -> 或 " 引号 key 上下文。
# 为降误报: 先抓候选行, 再排除纯注释行 (// 或 * 开头)。
BLACKLIST_RE='(private_key|privkey|priv_key|mnemonic|seed_phrase|shamir|api_secret|api_key|apikey|passphrase|\bsecret\b|\bnonce\b|signature_bytes|sig_bytes|\bsig\[|\br_s_v\b|signing_key|hmac_key|session_key|timestamp_ms|order_id|wallet_address|private_bytes)'

fail=0
for f in "${SCAN_PATHS[@]}"; do
  [[ -f "$f" ]] || continue
  # 去掉纯注释行后再匹配, 避免注释里"黑名单: nonce/sig"误杀
  if grep -nvE '^[[:space:]]*(//|\*|/\*)' "$f" \
       | grep -iE "$BLACKLIST_RE" ; then
    echo "BLACKLIST HIT in $f (上面行) — 观测 API 禁止暴露黑名单字段 (小白 v2 §1)"
    fail=1
  fi
done

[[ $fail -eq 0 ]] && echo "OK: 观测 API 黑名单字段守护通过 (0 命中)"
exit $fail
```

设计要点:
- **白名单豁免**: `condition_id` / `token_id` / `event_id` / `sequence_no` / `market_id` 不入黑名单(已 §2 准入)。`secret` 用 `\b` 词边界避免误杀 `secrets` 之外的子串;`sig\[` 抓数组形签名字节但放过 `sigma`/`signal`(signal_strength 不命中)。
- **排除纯注释行**(`^\s*//|\*`),避免 rm_debug_snapshot.hpp line 29-32 / state_provider.hpp line 19 的"黑名单: nonce/sig"注释自我误杀。
- **保留尺寸闸门**: 现有 `static_assert(sizeof(RejectRow) <= 256)` 是编译期第二道防线,与 grep 互补,**勿删**。建议对 `OrderBookFeatures` 也加一条 size 上限 static_assert(当前只有 trivially_copyable),作为偷加字段的被动告警。
- 接入 `.github/workflows/` 作为独立 job,fail 即 block merge。落地归老吴(部署/CI)。

---

## 6. 移交

- **老沈**: RM 真实接入时,`build_reject_row` 投影保持只读 §2.1 allowlist;集成 PR 触发 §3 R3 字段复扫。
- **小冯**: `RealBookStateProvider` 集成 OrderBookSnapshotHub → BookSnapshot 映射时,确认外部注入的 condition_id/outcome 不夹带额外数据;考虑给 OrderBookFeatures 加 size 上限 static_assert。
- **老吴**: 落地 §5 CI grep 守护 job;部署封装对 `--host 0.0.0.0` 加门禁/告警(§4);R1 可选 Host 头校验。
- **老黄 + 小梁**: §3 R2 — QuoteParams + EDGE_* reason_code 的对外/合规交付脱敏开关(对齐 v1 §4 OQ-10)。内部盯盘口全量无碍。
- **老周**: token_id 暴露裁定我已复核 = 同意准入(§2.3)。

---

## 7. 终审裁定

| 审计项 | 裁定 |
|---|---|
| 黑名单字段物理不存在 (3 文件 × 全 struct) | ✅ 0 命中,通过 |
| allowlist 字段安全性 (reason_code/intent_ref/side/size/price/token_id/condition_id/event_id/slug) | ✅ 全部准入 |
| reason_code 枚举脱敏 | ✅ 无需进一步脱敏 (sub_reason 已正确留 WAL) |
| token_id (ERC1155 asset_id) 暴露 | ✅ 复核同意准入 (链上公开;指纹风险归 R2 访问控制) |
| CORS `*` + loopback | ⚠️ 可接受,建议 R1 加固 (非阻塞) |
| 策略 IP 聚合 (QuoteParams) | ⚠️ R2 访问控制缓解 + 对外脱敏 (非阻塞) |
| 集成期回归 | ⚠️ R3 集成 PR 复扫 (非阻塞) |
| **一票否决项** | **无** |

**观测 API (含 ADR-040 score/quote/book_pair + RmDebugSnapshot + OrderBookSnapshotHub) 安全审计 v2 通过。** 无泄露字段,无阻塞项,3 项非阻塞加固建议已移交。
