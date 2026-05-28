# 逆向 polymarket.com/sports/live XHR 实证 v1

- Owner: 老李
- Date: 2026-05-28 08:49 UTC (R-20 时间戳)
- 抓包窗口: 2026-05-28 08:43:16Z ~ 08:44:17Z
- 用户问题: 官方 `/sports/live` 用什么参数过滤 live
- 工具: Playwright headless (匿名) + curl (拿 server-rendered HTML) + gamma probe
- 验收人: 老雷 + 小段 (双源对齐)
- 抓包资产 (供老雷复核):
  - /Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data/polymarket-sports-live-xhr.json (9 个 API 调用)
  - /Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data/polymarket-sports-live-curl.html (server HTML)
  - /Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data/polymarket-sports-live-NEXT_DATA.json (Next.js dehydratedState)
  - /Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data/polymarket-initialState.json (1.6MB Redux store, 解 zlib 后)

---

## 1. 工具环境

| 项 | 值 |
|---|---|
| Playwright | 1.60.0 (Python sync API) |
| Chromium | playwright bundled (匿名 UA: Chrome 120) |
| 抓包目标 | https://polymarket.com/zh/sports/live |
| 登录态 | 匿名 (无 cookie / 无私钥) |
| Locale | zh-CN |
| final_url | https://polymarket.com/zh/sports/live (无重定向) |
| 页面 title | "体育实时预测市场和赔率 2026 \| Polymarket" |

---

## 2. 关键 XHR 请求清单 (按调用顺序)

Playwright 共抓 341 个 requests, 其中 API 调用 9 个 (其余是 JS/CSS/字体/图片).

| # | Method | Host + Path | Query / Body 摘要 |
|---|---|---|---|
| 0 | GET | polymarket.com/api/geoblock/wallet-lists | (无 query) 返回 EVM 黑名单地址数组 |
| 1 | GET | gamma-api.polymarket.com/is-logged-in | 422 "missing auth cookie" (匿名预期) |
| 2 | GET | polymarket.com/api/tags/filtered | `tag=102982&status=active&locale=zh` |
| 3 | GET | clob.polymarket.com/rewards/markets/0xa9db...c3ff | `sponsored=true` (单 condition_id reward 查询) |
| 4 | POST | polymarket.com/api/meta/capi | Facebook conversion API 埋点 (PageView) |
| 5 | POST | clob.polymarket.com/books | body=`[{token_id:...} x 10]` (拿 10 个 outcome orderbook) |
| 6 | POST | clob.polymarket.com/last-trades-prices | body=`[{token_id:...} x 11]` (拿最后成交价) |
| 7 | GET | clob.polymarket.com/rewards/markets/0xdb39...0fdb | `sponsored=true` |
| 8 | POST | clob.polymarket.com/books | body=`[{token_id:...} x 1]` (延迟轮询补漏) |

**关键观察 - 这里没有任何对 `gamma-api.polymarket.com/events` 的 client XHR 调用**.

---

## 3. SSR 真相 (重大发现)

events list **不走 client XHR**, 走 Next.js Server-Side Rendering:

1. 首次 HTML response 内嵌 `<script id="__NEXT_DATA__">` (Next.js pages router).
2. `pageProps.dehydratedState.queries` 有 7 个 React Query 查询 (server prefetched):
   - `['/api/tags', 'filteredTags', '102982', 'active', 'en']` (Top Navbar tags)
   - `['account', 'featureFlags', '']`
   - `['sportsPopularCounts']` (每个 league slug -> 热门 events 数)
   - `['sportsVolumes']` (每 league 24h volume)
   - `['leaguesWithUpcomingGames']` (string array, e.g. `['ahl','atp','bkarg',...]`)
   - `['sportsLatestEventDates']` (每 league 最近 event 日期)
   - `['parentToChildEventIds']`
3. `pageProps.initialState` 是 **urlsafe-base64 + zlib (wbits=15)** 编码的 1.6MB Redux store, decode 后包含:
   - `sportSlug: "live"`
   - `events: {<slug>: <full_event_obj>}` × 58 (字段完全等同 gamma /events)
   - `games: {<slug>: <game_state>}` × 58
   - `sections: { hidden: {}, expanded: {}, live: {<date>:{events:[...]}}, soon, delayed }`
   - `marketsSections: {<event>: {moneyline, spreads, totals, nrfi, btts, firstSetTotals, ...}}`
4. Client hydrate 后, 仅对前几个可见 event 调 `/books` + `/last-trades-prices` + `/rewards/markets` 拿实时 orderbook & sponsorship.

**老李判断**: 浏览器侧不暴露 events list 的获取 endpoint. server-side 用 polymarket 内部 BFF (Backend-for-Frontend, 非公开), BFF 内部组装 sections.live + games + initialState 一次性吐出.

---

## 4. 我们 v3 用 vs 官方前端 client XHR 对照

| 用途 | v3 endpoint | 官方 client XHR | 差异 |
|---|---|---|---|
| events list | gamma `/events?tag_slug=sports&...` | (无 client 调用, SSR 注入) | 我们走公开 API, 官方走私有 BFF |
| orderbook | clob `/book?token_id=` | clob `/books` (POST, 批量) | 官方批量 POST, 我们要升级 |
| 最后成交 | (新) | clob `/last-trades-prices` (POST 批量) | 🆕 新发现 endpoint, 见 §6 |
| reward 配置 | clob `/markets/{cid}` | clob `/rewards/markets/{cid}?sponsored=true` | 🆕 路径不同, 见 §6 |
| tags meta | gamma `/tags` | polymarket.com/api/tags/filtered (BFF 包装) | 不影响我们 |

---

## 5. 是否有 islive / live 真参数 (回答用户)

**直接答用户**: 官方前端 **没有任何 ?islive=true 或 ?live=true client 参数**.

- gamma 公共 API 实测: `?live=true`, `?is_live=true`, `?in_play=true`, `?live_status=in-play`, `?status=live`, `?section=live` 全部 **静默忽略** (返回 limit=2 默认 events).
- `?sport=live` 同样静默忽略 (返回非体育的 default events, 0 重叠).
- 唯一在 client 出现的"live"字面量: `dehydratedState.queries['leaguesWithUpcomingGames']` 是名字; `initialState.sections.live` 是 SSR 结构 — 都不是 query param.

**官方真正的"live"过滤逻辑** 隐藏在 server-side, 大概率结构为 (推断, 不可证):
```
filter:
  - tag.slug == 'sports'
  - event.active == true && event.closed == false && event.archived == false
  - event.enableOrderBook == true
  - 关联 goalserve gameId 状态 == ('in-play' OR start_in_24h OR 'delayed')
  - 按 startDate 排序、再按 sport 分组
sections:
  live: 真 in-play (本次抓包为空, 因为本时刻无 in-play)
  soon: startTime within Nd, gameState='not-started'
  delayed: ETA passed but not started
```

本次抓包 58 个 events 全部 `gameState: not-started, live: false, view.type: live-soon, view.section: soon` (都是即将开赛, 没有真 in-play). 所以 polymarket 的 "live" 页面语义是 **"今日 in-play + 即将开赛 + 推迟" 联合**, 不是"严格只显示进行中".

---

## 6. /sports/live 是否有独立 endpoint (vs /events?tag_slug=)

**没有独立 公开 endpoint**.

- `https://gamma-api.polymarket.com/sports/live` → HTTP 422 (无效路径)
- 唯一 sports-relevant 公开 endpoint 仍是 `gamma /events?tag_slug=sports&...`.
- BFF 私有路径 (推测) 在 `polymarket.com/api/...`, 但只匿名暴露 `/api/tags/filtered` + `/api/geoblock/wallet-lists` + `/api/meta/capi`. events list 不通过 client 出 (走 server 内部 RPC).

**🆕 client 真实在用的 undocumented endpoint** (老李 portal v3 没列):

| Endpoint | Method | 输入 | 输出 |
|---|---|---|---|
| `clob.polymarket.com/books` | POST | body `[{token_id}]` 数组 | orderbooks 数组 (含 market, asset_id, timestamp, hash, bids, asks, min_order_size, tick_size, neg_risk, last_trade_price) |
| `clob.polymarket.com/last-trades-prices` | POST | body `[{token_id}]` 数组 | `[{price, side, token_id}]` 数组 |
| `clob.polymarket.com/rewards/markets/{condition_id}` | GET | `?sponsored=true` | `{data:[{condition_id, question, market_slug, event_slug, image, tokens, rewards_config, rewards_max_spread, rewards_min_size, market_competitiveness}], next_cursor, limit, count}` |
| `gamma-api.polymarket.com/is-logged-in` | GET | (cookie) | `{type:"validation error", error:"missing auth cookie"}` 匿名 422 |
| `polymarket.com/api/tags/filtered` | GET | `?tag=<id>&status=active&locale=<lc>` | tags 数组 (含 activeEventsCount, forceShow, forceHide) |
| `polymarket.com/api/geoblock/wallet-lists` | GET | (无) | `{block:[<addr>...]}` |

**v3 老李没列的**: 全部 6 个.

---

## 7. 给老李 v3.1 升级清单

| # | 待加项 | 优先级 | 说明 |
|---|---|---|---|
| L-01 | clob POST /books 批量 orderbook | P0 | client 实际用这个, 单 GET /book 浪费 RTT |
| L-02 | clob POST /last-trades-prices 批量最后成交价 | P0 | 跟 /books 配对, 一次拿 N 个 token 最后成交 |
| L-03 | clob GET /rewards/markets/{cid}?sponsored=true | P1 | reward_config + market_competitiveness (做市资格用) |
| L-04 | initialState 字段 reference: gameId/eventDate/startTime/seriesSlug/resolvedTeams/eventMetadata/automaticallyActive/pendingDeployment/deploying/negRiskAugmented | P1 | 补齐 gamma event schema, 我们漏的 11 个字段 |
| L-05 | sections 语义 + view.type 枚举 (live-soon / live-now / live-delayed) | P1 | "live" 页面语义清晰化, 不要再被 islive 假参数误导 |
| L-06 | 警告: "live" 过滤不是公开 API 能复刻 | P0 | 我们必须自己用 goalserve game-state 过滤 + 时间过滤组合, 不能假设 gamma 给 |
| L-07 | gamma /is-logged-in 422 schema | P2 | 边缘 endpoint, 但 client 在用 |
| L-08 | /api/tags/filtered 是 polymarket.com (非 gamma) BFF | P2 | 区分 host: gamma vs www-BFF |

---

## 8. 给老胡 W2-EXTRA-01 v3.1 待加项

| # | 字段 / 行为 | 必要性 | 备注 |
|---|---|---|---|
| EXTRA-01a | watch endpoint POST /books schema (clob) | 强需 | client 批量 endpoint, 单 GET 不能替代 |
| EXTRA-01b | watch endpoint POST /last-trades-prices | 强需 | 同上, 配对 |
| EXTRA-01c | watch endpoint GET /rewards/markets/{cid}?sponsored=true | 弱需 | sponsored 标记字段稳定性 |
| EXTRA-01d | gamma event 新字段: gameId, eventDate, startTime, seriesSlug, automaticallyActive, pendingDeployment, deploying, eventMetadata, resolvedTeams, negRiskAugmented, image_raw | 强需 | client 在读, server 在出, 监测 schema 漂移 |
| EXTRA-01e | events.* 是否新增 view/section/live 字段 | 中需 | 当前只 server-side 注入, 万一某天 gamma 公开 |
| EXTRA-01f | Polymarket BFF (polymarket.com/api/*) 是否新开 events 路径 | 强需 | 一旦 BFF 开放公共 events endpoint, 我们立即切 (省掉 tag_slug 全量拉) |
| EXTRA-01g | clob /books POST 字段 `neg_risk:false` 默认值与 negRiskAugmented 关系 | 中需 | neg_risk fix 后的二代字段 |

---

## 9. 老李结论 (一句话给 GM)

**官方前端没用任何"live"参数**. `/sports/live` 的 events list 由 Next.js SSR + 内部 BFF 私有逻辑产生, decode `__NEXT_DATA__.pageProps.initialState` (urlsafe-base64 + zlib) 可见全部数据嵌在 HTML. 公开 gamma API 没有 live 过滤能力, 必须**自建过滤** (我们用 goalserve game state + 时间窗组合等效). client XHR 真正在用的是 clob 批量 endpoint **POST /books / POST /last-trades-prices / GET /rewards/markets/{cid}?sponsored=true** 三个 v3 漏列的接口, 必须并入 v3.1.
