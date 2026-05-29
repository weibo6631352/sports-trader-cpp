# 外部数据路径安全审计 — Live Polymarket WSS + Goalserve Feed (v1)

- **owner:** 小白 (F 顾问 / 安全)
- **last_review:** 2026-05-29
- **scope:** 接真实 Polymarket WSS + Goalserve feed 后的入站数据安全 / ToS 合规 / 凭证 / TLS
- **派单:** GM (全员开工 + 真实外部数据接入)
- **状态:** 审计结论 + 落地建议。**不 commit / push**(GM 统一合并)。
- **协作边界:** 小白只给安全建议;C++ 实施落地由 owner 执行 —
  - 解析层加固 → 小冯 #34 (clob/PM WSS) + 小段 #37 (Goalserve inplay parser)
  - CI grep 守护 → 老高 (CI owner)
  - TLS / boost.beast transport → 老周 #02 / 老李 #07
  - 凭证流转复核 → 老韩 (RM 主权) + 老唐 (audit schema)

---

## 0. TL;DR (红线优先)

| # | 风险 | 现状 | 级别 | owner |
|---|---|---|---|---|
| 1 | int 解析无溢出防护 (`v = v*10 + d` 无 cap) | clob §63/§140, inplay 走 `from_chars` 安全 | **P1** | 小冯 |
| 2 | JSON 字段提取无消息体大小上限 / 无深度上限 | 手写扫描, 无 max_frame 限制 | **P1** | 小冯/小段 |
| 3 | 源码无 secret 字段黑名单 CI grep | 17 grep job 无一条查 `WALLET_PRIVATE_KEY` 等 | **P1** (合规缺口) | 老高 |
| 4 | Goalserve inplay 多 host **仅 HTTP (明文)** | `inplay.goalserve.com` cert 错配回退 http | **接受 (有条件)** | 老周 |
| 5 | TLS verify / host 白名单未在 transport 固化 | boost.beast transport W6+ 才接, 现 mock | **P0 拦截项** (真接前必做) | 老周/老李 |

观测 API 结构体 (EventScore / MarketInfo / BookSnapshot) 复核结论:**黑名单字段 (私钥 / 签名字节 / API secret) 物理上不存在,源头杜绝合规** — 见 §3.2。

---

## 1. 入站数据信任边界

### 1.1 信任模型

真实 Polymarket WSS (`ws-subscriptions-clob` market/user + `sports-api` 第 5 host) 与 Goalserve
inplay/livescore feed **全部是外部不可信输入**。即使来自"官方 host",也必须假设:

- 字节可能被中间盒篡改 (跨洋链路, 多跳)
- 上游可能下发畸形 / 超大 / 恶意构造 payload (压测、bug、攻击)
- 数值字段 (price / size / timestamp) 可能溢出、负数、`NaN`、超长

**铁律:解析层是信任边界的唯一闸门。** 任何越过解析层进入 `WssEvent` / `GameScoreRecord`
的数据,下游 (RM / 策略) 默认信任 —— 所以防御必须 100% 收在解析层。

### 1.2 当前解析实现盘点

两套手写 JSON 字段扫描器 (W10 才升 simdjson):

- `src/stcpp/polymarket/clob_wss/polymarket_clob_subscriber.cpp` (小冯) — `FindKey` + `Extract*`
- `src/stcpp/data/inplay_score_parser.cpp` (小段) — `ExtractStringValue` / `ExtractInt64Value` + 花括号深度计数

### 1.3 防御要点 (按攻击面)

#### (A) 数值溢出 — **P1, 已确认 bug**

`polymarket_clob_subscriber.cpp` 的整数提取:

```cpp
// L76-79  ExtractInt64OrQuotedInt64
while (... std::isdigit ...) { v = v * 10 + (body[pos]-'0'); ++pos; }  // 无 overflow check
// L149     ExtractUint64 同样
```

`timestamp` / `size` / `sequence_no` 走这条路径。上游发 30 位数字字符串 → **signed int64 溢出 = UB**。
`size` 溢出后 `static_cast<uint64_t>` 进 `t.size_micro`,污染 RM 的仓位 / fill 计算。

**修复 (小冯):**
1. 优先改用 `std::from_chars` (与 `inplay_score_parser.cpp` 已用的安全路径一致) —— `from_chars` 原生检测 `result_out_of_range`,返回 `errc` 即 drop frame。
2. 若保留手写循环:加位数上限 (timestamp ≤ 13 位 ms, size ≤ 18 位) + 每步 `if (v > (INT64_MAX - d)/10) return false;`。
3. `ExtractDecimalStringAsBps` 已做 `from_chars`-style `strtod` + clamp [0,1] + 32B buf 上限,**这条 OK**(但 `strtod` 受 locale 影响,建议改 `std::from_chars(double)` 彻底去 locale 依赖 —— 现注释写"P-02 must use from_chars"但实际用了 `strtod`,**注释与实现不符,需对齐**)。

`inplay_score_parser.cpp` 整数全走 `from_chars` → **溢出安全,无需改**。

#### (B) 超大消息 / 资源耗尽 — **P1**

两套 parser 均 **无消息体大小上限**。手写 `body.find()` 在 MB 级 payload 上每个字段 O(n) 扫描,
N 字段 = O(N·n);`inplay` 的 `EnumerateEvents` 对每个 event 再做 substr + 深度扫描 = 在 vCPU0 上
**违反 R-12 (≤100us)** 的隐患。攻击者发一个 50MB 畸形 inplay.gz → 解析阻塞 event loop = P0。

**修复 (小冯/小段 + transport owner 老周):**
1. **transport 层硬上限**:WSS 单帧 `max_frame_bytes`(建议 market ≤ 256KB / user ≤ 64KB / sports ≤ 256KB);Goalserve HTTP body `max_body_bytes`(inplay.gz 解压后 ≤ 8MB,实证最大约 2-3MB,留 buffer)。超限直接 drop + metric,不进 parser。
2. **gzip 解压炸弹防护**:Goalserve inplay 走 gz。解压必须设 `max_output_bytes` 上限(zlib `inflate` 流式 + 输出计数,超限即 abort),防 1KB→1GB 解压炸弹。**这条目前代码里没有(W5 才接真 HTTP+gz),接入前必须先有上限。**
3. **解析超时熔断**:parser 入口记 `ingestion_ts`,出口若 `> 100us` → metric `parse_slow_total`,连续超阈 → 该帧降级到 vCPU3 慢路径(不阻 vCPU0)。

#### (C) 畸形 JSON / 注入 — **P2**

手写扫描的固有风险:

- `inplay_score_parser.cpp` 的花括号深度计数 (`ExtractEventsBlock` / `ParseEventInfo`) 对**未闭合 / 深嵌套** JSON:未闭合时返回空(已处理,OK);但**无最大嵌套深度限制** → 攻击者发 `{{{{...×10万` 触发深度计数循环耗 CPU。加 `max_depth`(建议 32)。
- `ExtractStringValue` 的转义处理 `if (json[end]=='\\') ++end;` 在字符串**末尾恰好是 `\`** 时 `++end` 越界一位 —— 因后续 `while (end < json.size())` 保护未崩,但会吃掉闭合引号导致 key/value 错位。建议显式 `if (end+1 < size)` 守护。
- **注入**:本系统不把外部字符串拼进 SQL / shell / 日志格式串(已确认无 `fprintf(log, body)` 模式),注入面低。**但** `result.parse_errors.push_back("event " + entry.id + ...)` 把外部 `entry.id` 拼进错误串 → 若错误串进结构化日志且未转义,有 log injection 面。建议错误串里的外部 id 截断 ≤ 64 字符 + 剥非 `[A-Za-z0-9_-]`。

#### (D) 连接数 / 消息速率耗尽 — **P2**

- 出站连接数固定(market hot/cold + user + sports = 4-5 conn,老周 v0.6 §17),非攻击面。
- 入站速率:上游异常高频(price_change 风暴)→ SPSC ring 满 → 现有 `TryPush` 非阻塞 drop + `frames_dropped_total` metric,**这条设计正确**(R-12 合规)。建议加 `frames_dropped_total` 速率告警(小郑 prom),持续 drop = 上游异常或我方处理不过来,需 RM STALE 介入。

### 1.4 simdjson 升级 (W10) 防御要点

W10 升 simdjson 时:

1. **用 `ondemand` + `padded_string`** —— simdjson 要求输入尾部 padding,**直接喂 WSS frame buffer 会读越界**。必须 `simdjson::pad()` 或预留 `SIMDJSON_PADDING` 字节。
2. **`max_capacity`**:`ondemand::parser parser(max_doc_bytes)` 显式设上限(= §1.3-B 的 max_frame_bytes),超限 simdjson 自身报 `CAPACITY` 错而非崩。
3. **数值用 `.get_int64()` / `.get_double()`**,simdjson 原生检测溢出 / `NUMBER_OUT_OF_RANGE`,**天然修掉 §1.3-A 的手写溢出 bug** —— 这是升 simdjson 的核心安全收益,建议把 §1.3-A 的手写 fix 视为 W10 前的临时补丁。
4. **不信任 `simdjson` 不等于不验证语义**:price ∈ [0,1]、size ≥ 0、timestamp 在合理窗口(已有 5s future guard,好)等业务约束仍要显式 check。

---

## 2. ToS / 合规 (红线: 违反 Polymarket/Goalserve ToS = 立即回滚)

### 2.1 只读 / 不下单边界

真实 feed 接入阶段 **只读公开 book + 比分,不下单**:

- Polymarket market channel (token_id 粒度):公开 book / price_change / trade —— **只读公开数据,合规**。
- Polymarket user channel (condition_id + auth):订阅**自己账户**的 trade/order 状态 —— 是自己的数据,合规;但 user channel 携带凭证,见 §3。
- Goalserve inplay/livescore/pregame:付费授权 key 拉取比分 / odds —— 授权范围内只读,合规。
- **下单链路在真接 feed 阶段不启用**(paper mode / 不发单)。任何下单必经 RiskManager(CLAUDE.md 红线),feed 接入不碰下单。

### 2.2 速率 / 订阅规模建议

**Polymarket WSS:**
- 订阅 token 规模:market hot reactor 300-500 token + cold 2000-3500 token(老周 v0.6 §17 拓扑)。**建议单连接订阅上限保守设 ≤ 2000 asset_ids/conn**,超出拆连接,避免单帧订阅 payload 过大被服务端拒。
- 重连退避:已有 exp backoff 1s→cap 30s ×2(`reconnect_initial/cap/multiplier`)—— **合规且防自我 DoS**。务必保留,真接时不要为"快速恢复"调成固定短间隔(会被判攻击)。
- 心跳:10s PING / 30s timeout —— 符合 Polymarket 文档,保留。
- **不要并发狂建连接**:重连风暴时所有 conn 同时退避,建议加 ±20% jitter 防 thundering herd(老周 transport 实现时加)。

**Goalserve:**
- inplay 增量协议:用 `FetchWithTsDelta` 的 `ts` 增量(压缩 83x)—— **务必走增量,不要每轮全量拉**,既省带宽(跨洋链路紧)又尊重速率。
- 轮询频率:inplay 建议 1-2s 一轮增量(按 Goalserve 套餐速率上限,**需老段确认套餐 rate limit**,不要拍脑袋调到亚秒);livescore/pregame 低频(分钟级)。
- key 是带速率配额的 —— 超额可能被封 key。建议加客户端侧 token-bucket 限速(C++ 实现,老段),硬上限保护。

### 2.3 不 reselling

- 原始 feed 数据**不对外暴露 / 不转售**。观测 API (debug_api) 仅供内部盯盘,**不可公网暴露**(绑 localhost / 内网 + 鉴权,老高/小郑确认部署绑定)。
- WAL 落盘的 raw ingest(`ingest_raw_writer`)是内部审计用,**不得分发**。

---

## 3. 凭证 (Polymarket / Goalserve / Wallet)

### 3.1 凭证清单 (.env 字段名, 不打印值)

`WALLET_PRIVATE_KEY` / `POLYMARKET_API_KEY` / `POLYMARKET_API_SECRET` /
`POLYMARKET_API_PASSPHRASE` / `GOALSERVE_API_KEY` / `POLYMARKET_FUNDER_ADDRESS` / `DATABASE_URL` / `GOALSERVE_PROXY`。

### 3.2 三道防线复核

**(1) 不落日志:**
- `PolymarketCLOBSubscriber::MakeUserSubscribeFrame` (clob §620-644) 把 `api_key/secret/passphrase` 拼进 subscribe frame —— 代码标注 `P-09: auth payload 严禁落日志`,**实现里此函数只构造不 log,合规**。
- **风险点**:这个 frame 字符串若被 transport 层在 debug 模式下打印(boost.beast 接入时常见 `BOOST_LOG << send_payload`)→ secret 泄露。**TLS/transport owner (老周) 实现时:user channel 的 send payload 严禁进任何日志,或发送前对 auth 块做掩码。** 已列入 §5 CI grep 防护。
- Goalserve key 在 URL query(`?key=...`)—— URL **不得整条进日志**(access log / error log)。`GoalserveClient` 真接时,日志记 endpoint 类型 + host,**不记完整 URL**。

**(2) 不进前端 / 不出观测 API — 复核 EventScore / MarketInfo / BookSnapshot:**

逐字段复核 `src/stcpp/debug_api/state_provider.hpp`:
- `EventScore`:event_id / sport / status / period / clock / home / away / score / ts / source —— **无任何 secret,合规**。
- `MarketInfo`:condition_id / token_id / outcome / price / tick / fee / flags / slug / url / ts —— **全是公开协议事实(token_id = ERC-1155 链上 positionId),无 secret,合规**。
- `BookSnapshot` / `BinaryMarketBookView`:token_id / 报价 / spread / imbalance / seq / ts —— **全公开,无 secret,合规**。
- `RiskRejectRow`:注释明确 `intent_ref` 是内部引用**非签名/私钥**,`side` allowlist —— **合规**。
- **结论:观测 API POD 里黑名单字段(私钥 / 签名字节 / API secret)物理上不存在**,符合 state_provider.hpp 头注 L19 的设计意图(源头杜绝)。**这是正确的安全架构 —— allowlist(只放安全字段)而非 blacklist(事后过滤)。** 维持此原则:任何新增观测字段必须 allowlist 审查。

**(3) WALLET_PRIVATE_KEY 隔离:**
- 私钥仅 signer (C++) 使用,**绝不进 feed / 观测 / 前端任何路径**。feed 接入不碰 signer。
- 红线(CLAUDE.md):私钥明文落盘 / 出现在日志 → 系统权限暂停。**真接前确认 signer 日志无 raw key**(老孙 signer owner)。

### 3.3 凭证轮换 / 最小权限

- Polymarket API key 应为**只读 / 交易分离**(若 Polymarket 支持 scoped key);feed 阶段用最小权限 key。
- 凭证只从环境变量读(`getenv`),**不硬编码、不进 git**。`.env` 必须在 `.gitignore`(已确认 settings.local 模式)。

---

## 4. WSS TLS

### 4.1 红线 (真接 boost.beast transport 前 P0 拦截项)

当前 transport 是 mock(`IWssTransport` 抽象),真 boost.beast transport W6+ 才接。**接入前以下必须固化,否则 P0:**

1. **证书验证开:** `ssl::context` 设 `verify_peer`,`set_verify_callback` 用 `ssl::host_name_verification(host)`。**严禁 `verify_none` / 任何 `--insecure` 等价物。**
2. **SNI 必设:** `SSL_set_tlsext_host_name(ssl, host)` —— 不设 SNI 在多租户 CDN 后会连错证书 / 握手失败。
3. **CA bundle:** 显式 `load_verify_file` / `set_default_verify_paths`,不信任空 CA store。
4. **TLS ≥ 1.2:** `ssl::context::tlsv12_client` 或更高,禁 SSLv3/TLS1.0/1.1。
5. **Host 白名单(硬编码,不可运行时改):**
   - `ws-subscriptions-clob.polymarket.com` (market + user)
   - `sports-api.polymarket.com` (第 5 host)
   - 连接前校验目标 host ∈ 白名单,否则拒连。`PMWssSubscriberConfig.url` / `PolymarketCLOBSubscriberConfig.market_url/user_url` 默认值已是官方 host,但 **config 可被覆盖** → transport 层加白名单 assert,防配置错误 / 注入指向钓鱼 host。

### 4.2 Goalserve HTTP 明文问题 — **接受 (有条件)**

`goalserve_client.hpp` HostBaseStatic (§283):`inplay.goalserve.com` 强制 `http://`(注释:TLS cert 域名错配)。

- **风险**:inplay 比分 / odds 走明文,跨洋链路可被窃听 / 篡改。比分数据本身非机密,**但 URL query 带 `GOALSERVE_API_KEY` → 明文传输 = key 暴露在链路上**。
- **缓解:**
  1. **走 `GOALSERVE_PROXY`**(.env 已有此字段)—— 通过我方控制的 HTTPS 代理出口,client→proxy 走 TLS,proxy→goalserve 走 http,key 不暴露在公网跨洋段。**强烈建议 inplay 必走 proxy。**
  2. 若直连 http:接受比分明文,但 **§1.3 入站防御(篡改防护:future guard / 单调性 / 范围 check)是唯一防线**,必须严格。
  3. www / oddsfeed / livescore 默认 `prefer_https=true` → **这些 host 强制 https,不要退 http**。
- **决议建议**:inplay 直连 http 仅在 proxy 不可用时降级,且记 metric `goalserve_insecure_fetch_total` 告警。最终由老周 + 老段定。

---

## 5. CI grep 守护建议 (外部数据字段黑名单)

**现状缺口:** pr.yml 17 个 grep job 覆盖 R-20/R-12/R-33/HMAC/ABI 等,**无一条查 secret 字段进观测/日志路径**。这是合规缺口(P1)。建议老高新增 grep job:

### 5.1 secret 字段进观测 API 黑名单 (FAIL)

观测层(`src/stcpp/debug_api/`)源码出现 secret 字段名即 FAIL:

```bash
# ci-grep-secret-in-debug-api
if git grep -nE '\b(api_secret|apiSecret|passphrase|private_key|privateKey|WALLET_PRIVATE_KEY|secret_key)\b' \
   -- 'src/stcpp/debug_api/**/*.hpp' 'src/stcpp/debug_api/**/*.cpp'; then
  echo "::error::secret 字段不得出现在观测 API 层 (state_provider allowlist 原则)"
  exit 1
fi
```

### 5.2 secret 进日志 / 错误串黑名单 (FAIL)

全源码扫 secret 变量被拼进 log / cout / 异常串:

```bash
# ci-grep-secret-in-log
if git grep -nE '(LOG|log|cout|cerr|fprintf|throw .*runtime_error)\s*[(<].*\b(api_secret|api_key|passphrase|private_key|WALLET_PRIVATE_KEY|\.key\b|auth.*payload)\b' \
   -- 'src/**/*.cpp' 'src/**/*.hpp'; then
  echo "::error::疑似 secret 进日志/异常串 (P-09)"
  exit 1
fi
```

### 5.3 完整 subscribe frame / URL 进日志 (WARN→FAIL)

user channel frame(含 auth)和 Goalserve URL(含 key)整体进日志:

```bash
# ci-grep-credentialed-payload-log  (WARN 起, W+1 FAIL)
git grep -nE '(LOG|cout|cerr)\s*<<.*(MakeUserSubscribeFrame|user.*frame|BuildUrl|full_url|request_url)' \
   -- 'src/**/*.cpp' 'src/**/*.hpp'
```

### 5.4 TLS 反模式黑名单 (FAIL, transport 接入后启用)

```bash
# ci-grep-tls-insecure
if git grep -nE 'verify_none|VERIFY_NONE|--insecure|CURLOPT_SSL_VERIFYPEER\s*,\s*0|set_verify_mode\s*\(\s*[^)]*none' \
   -- 'src/**/*.cpp' 'src/**/*.hpp'; then
  echo "::error::TLS verify 关闭 / insecure 模式 (§4.1 红线)"
  exit 1
fi
```

### 5.5 .env / secret 值进 git (FAIL)

```bash
# ci-grep-no-secret-value
if git grep -nE '(WALLET_PRIVATE_KEY|POLYMARKET_API_SECRET|GOALSERVE_API_KEY)\s*=\s*["\x27]?[0-9a-fA-Fx]{16,}' \
   -- ':!*.example' ':!docs/**'; then
  echo "::error::secret 明文值疑似进 git"
  exit 1
fi
```

(实现细节交老高,落 `tests/ci_grep/secret_blacklist.py` 与现有 17 job 同风格,排除 `tests/ci_grep/**` 自身。)

---

## 6. 交接 / follow-up

| 项 | owner | 优先级 |
|---|---|---|
| clob parser int 溢出 → `from_chars` / 加 cap | 小冯 #34 | P1, W5 前 |
| max_frame_bytes / max_body_bytes + gzip 解压上限 | 小冯/小段 + 老周 | P1, 真接前 |
| 花括号 max_depth + 转义末尾守护 + 错误串外部 id 截断 | 小段 #37 | P2 |
| boost.beast TLS verify+SNI+CA+host 白名单 | 老周 #02 / 老李 #07 | **P0, 真接 WSS 前拦截** |
| Goalserve inplay 走 GOALSERVE_PROXY (key 不明文跨洋) | 老段 #37 + 老周 | P1 |
| 5 条 CI grep 守护落地 | 老高 | P1 |
| Goalserve 套餐 rate limit 确认 + token-bucket 限速 | 老段 #37 | P2 |
| simdjson 升级时 padded_string + max_capacity + 原生溢出检测 | 小冯 (W10) | P2 (届时) |

**审计无下单链路改动,无 RM 绕过,符合只读 feed 接入边界。本文档不 commit,GM 统一合并。**
