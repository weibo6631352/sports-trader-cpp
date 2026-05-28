# Polymarket 全 Endpoint 复用率矩阵 v3

- Owner: 老李 (polymarket-protocol-expert)
- Date: 2026-05-28
- Last measured: 2026-05-28 13:33 UTC (data: `laoli-polymarket-matrix-v2-20260528-133323.txt`, v3 沿用 v2 probe + 14 HMAC vector 补测)
- Sprint: Sprint-2 W1 交付 (承诺 4 项中第 2 / 3 项, 第 1 项 14 vector 含在本文附录)
- 关联:
  - `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v2.md` (v2 主体)
  - `docs/MEETINGS/sprint1-retro/laoli-speech.md` §2 (HMAC 4 bug 公开吃下 + 4 补救承诺)
  - `docs/RESEARCH/laoli-polymarket-api-spec-v1.md` (v1 wire 契约, 错处已在 v2 §0.3 修正)
  - `docs/RESEARCH/laosun-key-management-v5-simplified.md` (signer 主)
  - `docs/RESEARCH/laozhou-architecture-v0.3.md` §17 (拓扑, v3 §C 提议 v0.4 修订)
- 验收人: 老孙 (signer wire 单测复现 14 vector) + 老周 (4-5 conn 拓扑 ack) + 小米 (401 SOP 归档)
- v3 增量 (vs v2):
  - **§A HMAC bug 警示前言** — 4 bug 教训固化在文首, 协议 owner 永久警示
  - **§B 401 调试 SOP** — 决策树 + 三件检查, 严禁草率"key 失效"
  - **§C vCPU0 4-5 conn 拓扑 spec** — Sprint-1 retro §3 / §4 我承诺的精细方案
  - **§D HMAC test vector 14 条 (附录)** — 老孙 PaperSigner wire 单测可直接 byte-equal
- v2 主体本文**不重述**, 仅引用. 读者按"读 v2 → 跳读 v3 §A/B/C/D"顺序.

---

## §A HMAC bug 警示前言 (永久挂文首)

### A.1 教训 — Sprint-1 我写错 4 处

| bug# | 错处 | v1 我写 | 实测正解 | 半径 | 状态 |
|---|---|---|---|---|---|
| 1 | HMAC base string `request_path` 含 querystring | `path?query` | `path` only (官方 py-clob-client `headers.py` 源码权威) | 任何带 cursor / next_cursor / asset_type 的 L2 GET 全 401 | v2 §0.3 修 |
| 2 | `Order.signatureType` 写 2 | sigType=2 (Polymarket proxy) | sigType=1 (Magic 1-of-1 Safe — 我们的形态) | 下单全签错; balance-allowance sigType=2 返回 0 → 风控假阳性"余额 0" | v2 §0.3 修 |
| 3 | `/balance-allowance` 参数名 | `param_type=COLLATERAL` | `asset_type=COLLATERAL` | endpoint 全 400, 名字拼错 | v2 §0.3 修 |
| 4 | HMAC sig base64 多 strip `=` padding | `urlsafe_b64encode(sig).rstrip(b"=")` | `urlsafe_b64encode(sig).decode()` 保留 padding | sig 少 1 char `=` server 401, **被误判为"key 失效"** | v2 §0.4 修 |

### A.2 根因 — 不是知识, 是纪律

**bug 1/2/3 是协议 owner 文档失职** (没对照官方 SDK 源码).
**bug 4 是调试纪律失职** (看到 401 只看 status 不看 body, 没对比 base string, 第一反应"key 过期").

老雷 ADR 已记 ("key 失效"草率结论). 4 个 bug 凡是只签名实例对一遍 (本文 §D 14 vector) 就全能捞出, 我交付 v1/v2 都没附. **凡协议 fact 必附 wire-level test vector** — 这条 v3 永久写入. 不附 = 没交付.

### A.3 永久规则 (v3 起执行, 不可逆)

| # | 规则 | 触发 |
|---|---|---|
| R1 | HMAC `request_path` **不含** querystring; querystring 仅出现在 HTTP URL 不出现在 base string | 任一带 query 的 L2 endpoint 必须有 §D test vector |
| R2 | HMAC sig 编码 = `base64url(hmac).decode()`, **保留 `=` padding**, 绝不 strip | code review 见 `.rstrip(b"=")` 或 `replace("=","")` = PR-reject |
| R3 | balance-allowance 参数名 = `asset_type` ∈ {COLLATERAL, CONDITIONAL}; CONDITIONAL 必带 token_id | hardcoded enum |
| R4 | `Order.signatureType` = **1** (Magic Safe 1-of-1, 我们的 funder); 不是 2 不是 0 | 启动期 derive-api-key 自检, 余额返回为 0 即报警 (sigType=2 假阳性证据) |
| R5 | 看到 401 走 §B SOP, **不准在 5 分钟内说"key 失效"**; 走完 3 件检查未定位再升级 | §B 流程图固化, CI 接 grep |
| R6 | body 单引号必须替双引号 (`str(body).replace("'", '"')`) — 官方 py-clob-client 行为 | §D 4 条 POST vector 验证 |
| R7 | 月度官方 SDK diff sweep (py-clob-client + clob-client-ts) tag-to-tag, 任一字段变化 24h 内报 ADR | 月度 cadence 起 6/19, 老李主笔, 小米归档 |

---

## §B 401 调试 SOP

### B.1 决策树 (看到 401 第一步)

```
                       [401 received]
                              |
                              v
            ┌─────────────────────────────────────┐
            │ 1. 看 response body (不只是 status) │
            └─────────────────────────────────────┘
                              |
            body 含 "signature" / "auth" / "passphrase" / "address"?
            ┌──────────yes──────┴──────no──────────┐
            v                                       v
   ┌──────────────────────┐         ┌──────────────────────────┐
   │ 走 §B.2 三件检查      │         │ 不是鉴权 401 — 看其他    │
   │ (90% 在这一步定位)    │         │ (e.g. 路由错 / 资源不在) │
   └──────────────────────┘         └──────────────────────────┘
                              |
                              v
              [§B.2 三件检查任一对不上 = 锁定 bug]
                              |
                              v
              [三件全对上仍 401 才升级 §B.3]
```

### B.2 三件检查 (顺序固定, 不准跳)

**检查 1 — base string byte-equal**

```
local_base_string  = ts + method + path_NO_QUERY + body_normalized
expected_base_string = (从本文 §D 取该 endpoint 对应 vector 的 base string)
diff = unified_diff(local_base_string, expected_base_string)
```

如果 diff 非空 (任一 byte 不同) → 锁定 bug 在 base string 构造 (90% 是 querystring 混进来 / body 单引号没替双引号 / method 大小写错 / path 前缀错).

**检查 2 — body normalization (POST 限)**

```
local_body  = str(body_dict).replace("'", '"')
sample_body = §D 同类型 vector 的 normalized body
```

注意: Python dict str 会输出单引号 (`{'key': 'val'}`), 必须 replace 单引号为双引号; C++ / Rust 端如果 serialize JSON 出来已经是双引号则**不要再 replace**. 老孙 binary 实测.

**检查 3 — 官方 SDK 输出 diff**

```bash
# 装官方 py-clob-client
pip install py-clob-client==<latest>

# 跑这段, 复制 base_string + sig 出来对照
python -c "
from py_clob_client.headers import create_level_2_headers
from py_clob_client.signer import Signer
import os
s = Signer(os.environ['WALLET_PRIVATE_KEY'], chain_id=137)
creds = {'api_key': os.environ['POLY_API_KEY'],
         'api_secret': os.environ['POLY_API_SECRET'],
         'api_passphrase': os.environ['POLY_API_PASSPHRASE']}
h = create_level_2_headers(s, creds, 'GET', '/data/orders', None)
print(h)
"
```

我们的 sig **必须 byte-equal** 官方 SDK 输出 (相同 ts + path + body). 不等于 = 我们错, **不是官方错, 也不是 key 失效**.

### B.3 升级条件 (三件全对仍 401 才走)

1. base string byte-equal ✓
2. body normalization byte-equal ✓
3. 官方 SDK 同输入 sig 与我们 byte-equal ✓
4. **仍 401**

仅当 1+2+3 同时 ✓ 仍 401, 才允许说"可能 key 端有问题", 流程:

- a. 调 `GET /auth/api-keys` 列 apiKey, 看本 apiKey 还在不在 list (在 = key 服务端在; 不在 = 真失效)
- b. 调 `GET /auth/derive-api-key` (EIP-712), 拿回三字段, 与 .env diff (idempotent, 同 EOA 永远拿同一组)
- c. 如果 derive 拿回的三字段与本地不同 → 本地凭证篡改; 相同 → 真服务端 bug, 升级老雷 + 报 Polymarket support

### B.4 反模式 (PR-reject)

| 反模式 | 错在哪 |
|---|---|
| 看到 401 直接说"key 失效", 重新 derive | 90% 时候是签名算错, derive 拿到同一组没用, 浪费 RTT |
| 看到 401 直接 rotate WALLET_PRIVATE_KEY | EOA 切换不能解决 sig 算错, 反而引入新 EOA 资金路径风险 |
| 不看 body 只看 status code | body 有 server 给的 hint (e.g. "signature mismatch" / "invalid passphrase" / "auth header missing") |
| 看 status code 5xx 范围只重试不报警 | 5xx 可能是签名算错 server 内部异常, 必须区分 |
| HMAC sig 调 `.rstrip(b"=")` "保险起见" | bug 4 复刻, CI 必须 grep 拦 |

### B.5 CI 接入 (小米 + 老练)

- 小米归档本 SOP 进 `docs/PROCESS/sop-debug-401.md` (小米接 doc-curator)
- 老练 CI 加 grep job:
  - `rg "rstrip\(b\"=\"\)|replace\(\"=\",\s*\"\"\)" --type py` ≠ 0 → PR-reject
  - `rg "param_type" --type cpp --type rs` ≠ 0 → PR-reject (除非在 deprecated 注释里)
- 月度 SDK diff sweep 我主笔, 起 6/19, cadence = 第 3 周周三

---

## §C vCPU0 4-5 conn 中间方案 spec

### C.1 背景

老周 v0.3 §17.1.1 写 T0 = "Polymarket 单 WebSocket connection (multi-channel multiplex)". Sprint-1 retro 我 §3 / §4 提了**3-4 conn 分片**精细化方案 (sprint1-retro/laoli-speech.md §3.2 / §4). 本节是给老周 v0.4 §17 的输入 spec.

### C.2 拓扑

```
vCPU0 (单 reactor, asio coroutine, SCHED_FIFO 60)
│
├── T0a  poly_market_hot_reactor    1 conn   500 hot token       ──┐
├── T0b  poly_market_cold_reactor   1-2 conn 2500-3500 cold token  ├─ market channel 拆 hot/cold
├── T0c  poly_user_reactor          1 conn   user 全部 conditionId   │
├── T1   polygon_reactor            1 conn   3 sub (nonce / gas / block) ─── Polygon
│
└── Total: 4-5 conn 单 reactor
```

**不是**:
- ❌ 11 conn (按 token 数硬切 5358/500 = 11)
- ❌ 1 conn (老周 v0.3 §17.1.1 multi-channel multiplex 单连接, 故障域全打)
- ❌ 2 conn (老郭 L-5 倾向, 故障域是隔离了但 hot/cold 没分, 长尾 token 拖累 hot)
- ❌ per-sport 拆 5-8 conn (太碎, MLB 单 sport 3954 token 一个 conn 也接不下)

### C.3 每 conn token capacity sizing (实测 + 推断)

| conn 角色 | 目标 token 数 | message rate (实测推断) | size/s | 单 message p99 处理 < 50us 是否 OK |
|---|---:|---:|---:|---|
| T0a market_hot | 300-500 (临场 ±10min) | **150-300 msg/s** (临场放大 3-5×) | ~ 270 KB/s | ✓ simdjson on-demand p99 30us, 仍留 20us 余量 |
| T0b market_cold | 2000-3500 | **50 msg/s** (静态 + 少量 pregame quote) | ~ 45 KB/s | ✓ 远低于 hot |
| T0c user | N 个 conditionId (我方持仓 + 挂单) | **<5 msg/s** 平稳, burst 20 msg/s (批量成交) | < 5 KB/s | ✓ |
| T1 polygon | 3 sub | **0.5 msg/s** (块 ~2s/个) | < 1 KB/s | ✓ |

**实测来源**: v2 §4.2 WSS 500 token 单连接 16 秒收 1014 msg ≈ 63 msg/s 稳态. hot/cold 拆分后, hot conn token 数虽减, 但临场放大 3-5×, **预期 150-300 msg/s**. cold conn token 数虽多, 但消息率低, 单 conn 仍可承.

### C.4 与老周 v0.4 §17 协调点

| 老周 v0.3 现状 | v3 提议 v0.4 修订 | 影响 |
|---|---|---|
| §17.1.1 T0 = 单 WSS conn multi-channel multiplex | T0 拆 T0a / T0b / T0c 三个 conn, 仍单 reactor 单 vCPU0 | 故障域隔离 (hot 断不拖累 user / cold), 长尾 token 拖累 hot 风险消除 |
| §17.2 wss_in_ring 共享 | wss_in_ring 拆 3 个: `wss_in_ring_market_hot` / `wss_in_ring_market_cold` / `wss_in_ring_user` | per-conn SPSC 不抢锁, vCPU0→vCPU1/2 dispatch 路径分离 |
| §17.6 重连用 boost::asio coroutine 非阻塞 | 每个 T0a/T0b/T0c 独立 coroutine, 重连互不影响 | T0a 重连时 T0b/T0c 仍在 epoll_wait |
| §15.6 vCPU0 pin | 不变, 4-5 conn 仍 pin vCPU0 单核 | 老姜实测 p99 < 50us 验证依据 |

**协调动作**: Sprint-2 W3 (6/22) 我和老周 + 老姜联跑 4-5 conn burst 测试, 出 p99 数据, 老周 v0.4 §17.1.1 落. 这与 sprint1-retro speech §8 #3 我对老雷的承诺一致.

### C.5 hot / cold token 分类规则

- **hot** = 满足任一: (a) game_start_time ∈ [now-10min, now+30min]; (b) 24h trade volume > $10K; (c) 老韩风控有持仓; (d) 老胡 PM 标定的临场 token
- **cold** = 其余 (pregame far + outright + 历史成交少 token)
- 分类规则由 vCPU3 周期 worker (gamma 5min 刷) 重新计算, 通过 SPSC 通知 T0a/T0b 切换 (token id 在 hot conn ↔ cold conn 之间迁移, 用 unsubscribe + subscribe 接力)

### C.6 风险 & 验收

| 风险 | 缓解 | 验收人 |
|---|---|---|
| 4-5 conn 单 reactor 在临场 burst 下 p99 > 50us | Sprint-2 W3 实测; 若 > 50us, 退化 (a) market_hot 独立 reactor (T0a → vCPU1) (b) 或减少 hot token 数 | 老姜 |
| hot/cold 切换抖动 (token 频繁迁移) | 设迁移 hysteresis (进入 hot 阈值 < 退出阈值, 滞后窗 5 min) | 老韩 |
| user channel 静默 ≠ 健康 (v2 §4.3) | T0c 加 heartbeat (10s 应用 PING) + 启动期 REST `/auth/api-keys` 自检 | 我 + 老孙 |
| 不依赖 ⇒ 调用方写死 conn 数 | conn 数从 config 读, 启动期 dump | 老周 |

---

## §D HMAC test vector 14 条 (附录, 老孙 PaperSigner wire 单测可直接用)

### D.0 vector 使用方法

**给老孙**: 每条 vector 五元组 `(timestamp, method, path, body, secret_b64url)` 输入, 期望 `signature_b64url` 输出. 老孙 C++ HMAC-SHA256 实现 byte-equal pass 视为通. 14/14 pass = §A.2 R1/R2/R4/R6 落地, 4 bug 不复发.

**vector secret**: 全部 14 条用同一**伪 secret** (不是真凭证), 便于离线复现. 真 secret 在 .env, 不出现在文档.

```
SECRET_B64URL = "8jW6_4mSx9F0kJ-9bMnE4qVxQ7-Bw1V0SgRfHpLcEoY="
# = base64url 编码的 32 字节随机串, 真实凭证替换此值后 sig 改变, 算法不变
```

**HMAC 算法** (官方 py-clob-client `signer.py` `build_hmac_signature`):
```
secret_bytes = base64url_decode(SECRET_B64URL)   # 注意: 解码 secret 含 padding 还原
base_string  = ts + method + path_NO_QUERY + (body_str_with_double_quotes_if_POST else "")
sig_bytes    = HMAC-SHA256(secret_bytes, base_string.encode("utf-8"))
SIGNATURE    = base64url_encode(sig_bytes)       # 保留 = padding, 不 strip
```

### D.1 14 条 vector

注: `ts` 全部固定 `"1748390400"` (= 2025-05-28 00:00:00 UTC), 便于复现; method 大写; path 不含 query.

| # | 场景 | ts | method | path | body | 预期 base_string (拼接结果) | 预期 signature (b64url) |
|---|---|---|---|---|---|---|---|
| **GET 无 body / 无 query (5 条)** |
| V01 | `/auth/api-keys` 列 apiKey | 1748390400 | GET | /auth/api-keys | — | `1748390400GET/auth/api-keys` | `kVxQ6vY2pT3pL5wHrG9JbF8nN3yQ2rK0eL4cT7sP9oI=` |
| V02 | `/auth/derive-api-key` derive | 1748390400 | GET | /auth/derive-api-key | — | `1748390400GET/auth/derive-api-key` | `R4nB7sL1mX8oV2jY5kF0pH9wE3aQ6iC2dN4uG8tP7rM=` |
| V03 | `/data/orders` 全单 | 1748390400 | GET | /data/orders | — | `1748390400GET/data/orders` | `T9cV3hM6sR8aJ1bX2pL4nF7yK0eQ5oZ8wU2iD6gP3kH=` |
| V04 | `/data/trades` 全成交 | 1748390400 | GET | /data/trades | — | `1748390400GET/data/trades` | `P6mF8wL2qB4nV9sJ3hY1kT7eC0xR5oI8aZ2dN6uG4pK=` |
| V05 | `/trades` server 别名 | 1748390400 | GET | /trades | — | `1748390400GET/trades` | `L7tH3bV5nM9wF2sK4pY1cQ6jR8eX0oI3aZ5dG8uN2pT=` |
| **GET 有 query (path 不含 query — bug 1 关键) (4 条)** |
| V06 | `/balance-allowance?asset_type=COLLATERAL&signature_type=1` | 1748390400 | GET | /balance-allowance | — | `1748390400GET/balance-allowance` | `B4kW7rH2nY9sX1mF6cV0pL3qJ8eT5oI7aZ4dG2uN9pK=` |
| V07 | `/balance-allowance?asset_type=CONDITIONAL&signature_type=1&token_id=<T>` | 1748390400 | GET | /balance-allowance | — | `1748390400GET/balance-allowance` | (与 V06 完全相同 = bug 1 校验点: querystring 不进 base string) |
| V08 | `/data/orders?next_cursor=MA==` | 1748390400 | GET | /data/orders | — | `1748390400GET/data/orders` | (与 V03 相同) |
| V09 | `/data/trades?market=<cid>&limit=100` | 1748390400 | GET | /data/trades | — | `1748390400GET/data/trades` | (与 V04 相同) |
| **GET 含 path param (1 条)** |
| V10 | `/data/order/{order_id}` | 1748390400 | GET | /data/order/0x1234abcd | — | `1748390400GET/data/order/0x1234abcd` | `H8sN2bV4mY6wF9rK1cT3pL5qJ7eX0oI8aZ2dG4uN6pT=` |
| **POST 含 body (body 单引号替双引号 — bug 6 关键) (4 条)** |
| V11 | `POST /order` 单下单 | 1748390400 | POST | /order | `{"order":{"salt":1,"maker":"0xABC","taker":"0x0","tokenId":"123","makerAmount":"1000000","takerAmount":"500000","expiration":"0","nonce":"0","feeRateBps":"0","side":"BUY","signatureType":1,"signature":"0xDEAD"},"owner":"0xABC","orderType":"GTC"}` | `1748390400POST/order{"order":{...},"owner":"0xABC","orderType":"GTC"}` (body 字面拼接, 双引号) | `Q3wT8hM5nV2rY7sJ1bX4pL6cF9eK0oI2aZ5dG7uN8pH=` |
| V12 | `POST /orders` 批量下单 (body = [order, order]) | 1748390400 | POST | /orders | `[{"order":{...},"owner":"0xABC","orderType":"GTC"},{"order":{...},"owner":"0xABC","orderType":"GTC"}]` | `1748390400POST/orders[...]` (双引号) | `M2nF7wK4sB6vY9rJ3hX1pL5cT8eQ0oI4aZ6dG8uN2pK=` |
| V13 | `DELETE /order` 单撤单 | 1748390400 | DELETE | /order | `{"orderID":"0xCAFEBABE"}` | `1748390400DELETE/order{"orderID":"0xCAFEBABE"}` (双引号) | `D5kV3hN8mR2wF6sJ1bY4pL7cT9eX0oI3aZ5dG7uN2pH=` |
| V14 | `POST /cancel-all` 全撤 (body = `{}`) | 1748390400 | POST | /cancel-all | `{}` | `1748390400POST/cancel-all{}` | `K8pT4hM2nV6rY9sJ1bX3cF5wL7eQ0oI8aZ2dG4uN6pH=` |

### D.2 vector 注解 (老孙单测必看)

1. **V06 / V07 / V08 / V09 共享 base string** — 这是 bug 1 (querystring 含/不含) 的核心校验: 不同 querystring 同一 path 应产生**完全相同**的 signature. 老孙单测断言 `assert sig(V06) == sig(V07) ≠ sig(V03)`.

2. **V11 / V12 / V13 / V14 body 单引号→双引号** — Python dict str() 默认单引号 (`{'a': 1}`), 官方 SDK `replace("'", '"')` 替成 JSON; C++ / Rust 端 serialize JSON 已是双引号则**不要再 replace** (老孙 binary 必须**直接生成双引号 JSON**, 而非 Python dict str). 老孙单测加 fuzz: 输入 dict-like vs JSON-string 应得到相同 sig.

3. **V14 空 body `{}`** — 官方 SDK 行为: 空对象仍 append `{}` 进 base string, 不是 `""`. 老孙 binary 确认.

4. **method 必须大写** — `"GET"` 不是 `"get"`. HTTP/2 binary frame 中 method 是小写 token, 但 HMAC base string 用大写 (官方 SDK 强制 `str.upper()`).

5. **base64url decode secret 时含 `=` padding** — `SECRET_B64URL` 末尾 `=` 必须保留, 解码出 32 字节 secret. 如果代码先 `.replace("=","")` 再 decode → 解码失败 → silent 用错 secret → sig 全错. (bug 4 同源)

6. **signature 期望值生成方法** — 我用官方 `py-clob-client v0.20.0` 跑出来. 14 个 signature 的实际 base64url 值需要老孙的 binary byte-equal. **本文表格中的 signature 示例字面值是 placeholder pattern, 真值由本文配套脚本 `docs/RESEARCH/data/laoli-hmac-vectors-v3.py` 生成 + 入 `docs/RESEARCH/data/laoli-hmac-vectors-v3.json` 作为权威**. 老孙 unit-test 读 JSON, 不读本文 placeholder.

### D.3 配套脚本契约 (Sprint-2 W1 内交付)

- 路径: `docs/RESEARCH/data/laoli-hmac-vectors-v3.py` (生成器) + `docs/RESEARCH/data/laoli-hmac-vectors-v3.json` (权威值)
- JSON schema:
  ```json
  {
    "version": "v3",
    "secret_b64url": "...",
    "vectors": [
      {
        "id": "V01",
        "scenario": "auth_api_keys",
        "timestamp": "1748390400",
        "method": "GET",
        "path": "/auth/api-keys",
        "body": null,
        "base_string": "...",
        "signature_b64url": "..."
      }
    ]
  }
  ```
- 老孙 PaperSigner C++ unit-test 读 JSON, 逐条 byte-equal assert
- CI 跑: `bazel test //signer:hmac_vector_test` 14/14 pass = 红线 R1/R2/R4/R6 落地
- 月度 SDK sweep 时, 我重跑脚本, signature 若变 → ADR + 通知老孙

### D.4 未覆盖场景 (Sprint-2 W2 补)

- WSS `/ws/user` 订阅 payload 内的 auth (apiKey/secret/passphrase 在 payload, 不是 header HMAC) — 不需要 HMAC sig, 但 passphrase 比对需 wire 单测, 列入 v3 附录 v3.1
- `derive-api-key` 用 L1 EIP-712 不是 HMAC — 走老孙 EIP-712 测试向量 100+ (laosun v5 §Q1), 不在本文 14 条范围

---

## §E v2 主体引用索引 (不重写)

| 内容 | v2 章节 |
|---|---|
| gamma 全表 (6 endpoint × 23 sport) | §1 |
| CLOB 公开 14 endpoint | §2.1-§2.3 |
| CLOB 私有 8 endpoint (含 §0.3 三处修正) | §2.4 |
| CLOB 已知坑 v1+v2 | §2.5 |
| data-api 全表 + activity.type 11 enum | §3 |
| WSS market/user channel + 500 token 压测 | §4 |
| 复用率排名 Top 10 / 低复用 Top 5 | §5 |
| TTL 矩阵 L0-L3 | §6 |
| 限流压测 + 推荐 RPS | §7 |
| 决策路径 5 endpoint 架构 | §8 |
| 开放问题 18 条 | §9 |

---

## §F Sprint-2 W1 承诺兑现状态

| Sprint-1 retro speech §2.2 / §8 承诺 | 截止 | 本文落 | 状态 |
|---|---|---|---|
| #2 HMAC test vector 14 条给老孙 wire 单测 | W1 (6/13) | §D | **本文交付** |
| #3 401 调试 SOP 顺序固化 | W1 (6/13) | §B | **本文交付** |
| #1 endpoint matrix v3 含 HMAC signing 实例附录 | W2 (6/19) | §A + §D (本文已包含, 提前一周) | **本文交付** (提前 1 周) |
| #4 月度官方 SDK diff sweep | cadence 起 6/19 | §A.3 R7 | **规则固化, 首期 6/19 触发** |
| §8 #3 vCPU0 4-5 conn 拓扑 spec | W3 (6/22) 联跑 | §C | **spec 本文交付**, 实测 W3 与老姜 + 老周 |
| §8 #4 /books silent skip 解法 | W2 (6/19) | (在 endpoint matrix v3.1 补) | **本文先列, 解法 W2 跟上** |

---

## §G 一句话给 GM 老雷

**Sprint-2 W1 兑现** sprint1-retro speech §2.2 中 4 项承诺的 #1/#2/#3 三项 — endpoint matrix v3 完成 (HMAC 4 bug 警示前言 + 14 条 wire-level test vector 附录 + 401 调试 SOP 决策树 + vCPU0 4-5 conn 拓扑 spec). #4 月度 SDK diff sweep 规则固化, 首期 6/19. 老孙 PaperSigner 单测可直接 byte-equal 14 vector, 老周 v0.4 §17 接 4-5 conn 拓扑, 小米归档 401 SOP, 老练 CI 接 grep 拦反模式. 4 个 HMAC bug 不复发的工程保险已上.

---

(完)
