#!/usr/bin/env bash
# 老李 v2 — Polymarket 全 endpoint 复用率矩阵实测脚本
# 用法: bash laoli-polymarket-matrix-v2-probe.sh
#
# 输出: 同目录下 laoli-polymarket-matrix-v2-<stamp>.txt + 中间 JSON 落 /tmp/laoli_v2_<stamp>/
#
# 安全:
#  - 凭证从 .env 读, 文件名 / log 全程 redact
#  - 不下任何真实订单 (GET 优先, derive-api-key 无副作用)
#  - 跨洋链路慢, 用 GNU parallel / xargs -P 限制 ≤ 5 并发避限流

set -u

PROJECT_ROOT="/Users/wangweibo/code/sports-trader-cpp"
ENV_FILE="${PROJECT_ROOT}/.env"
OUT_DIR="${PROJECT_ROOT}/docs/RESEARCH/data"
STAMP=$(date +%Y%m%d-%H%M%S)
RAW_DIR="/tmp/laoli_v2_${STAMP}"
mkdir -p "$RAW_DIR"
REPORT="${OUT_DIR}/laoli-polymarket-matrix-v2-${STAMP}.txt"

if [ ! -f "$ENV_FILE" ]; then echo "[err] .env missing"; exit 1; fi
set -a; source "$ENV_FILE"; set +a

UA="curl/8.4.0 sports-trader-cpp-laoli-v2"
GAMMA="https://gamma-api.polymarket.com"
CLOB="https://clob.polymarket.com"
DATA="https://data-api.polymarket.com"

redact() {
  sed \
    -e "s|${WALLET_PRIVATE_KEY:-_NA_}|<PK_REDACTED>|g" \
    -e "s|${POLYMARKET_API_KEY:-_NA_}|<APIKEY_REDACTED>|g" \
    -e "s|${POLYMARKET_API_SECRET:-_NA_}|<SECRET_REDACTED>|g" \
    -e "s|${POLYMARKET_API_PASSPHRASE:-_NA_}|<PASS_REDACTED>|g" \
    -e "s|${GOALSERVE_API_KEY:-_NA_}|<GS_REDACTED>|g"
}

log() { echo "$@" | redact | tee -a "$REPORT"; }

echo "# laoli polymarket matrix v2 — ${STAMP}" > "$REPORT"
echo "# raw: ${RAW_DIR}" >> "$REPORT"
echo "" >> "$REPORT"

#############################################
# 0. clock & basic
#############################################
log "===== 0. CLOB /time + 基础探活 ====="
t_srv=$(curl -sS -A "$UA" "${CLOB}/time" 2>/dev/null)
t_local=$(date +%s)
log "  clob_time=${t_srv}  local=${t_local}  delta=$((t_local - t_srv))s"

#############################################
# A. gamma — 全方位
#############################################
log ""
log "===== A. gamma API 全覆盖 ====="

# A.1 /sports
log ""
log "--- A.1 GET /sports ---"
f="${RAW_DIR}/gamma-sports.json"
t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${GAMMA}/sports")
read tt code sz <<< "$t"
n=$(jq 'length' "$f" 2>/dev/null)
log "  http=${code} sports=${n} bytes=${sz} t=${tt}s"
log "  sport sample: $(jq -r '.[0:5][] | .label // .name // .id' "$f" 2>/dev/null | tr '\n' ',' | head -c 200)"

# A.2 /tags
log ""
log "--- A.2 GET /tags?limit=500 ---"
f="${RAW_DIR}/gamma-tags.json"
t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${GAMMA}/tags?limit=500")
read tt code sz <<< "$t"
n=$(jq 'length' "$f" 2>/dev/null)
log "  http=${code} tags=${n} bytes=${sz} t=${tt}s"
# 抽出体育相关 slug
sport_tags=$(jq -r '.[] | select(.slug // "" | test("nba|mlb|nfl|nhl|soccer|tennis|mma|golf|ncaa|epl|liga|bundesliga|champions|formula|cricket|esport|boxing|wnba|mls|ufc|f1|nascar"; "i")) | .slug' "$f" 2>/dev/null | sort -u)
log "  体育相关 tag_slug 命中:"
echo "$sport_tags" | sed 's/^/    /' >> "$REPORT"

# A.3 /series
log ""
log "--- A.3 GET /series?limit=500 ---"
f="${RAW_DIR}/gamma-series.json"
t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${GAMMA}/series?limit=500")
read tt code sz <<< "$t"
n=$(jq 'length' "$f" 2>/dev/null)
log "  http=${code} series=${n} bytes=${sz} t=${tt}s"
# 试 ?sport= 过滤
f="${RAW_DIR}/gamma-series-by-sport.json"
t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${GAMMA}/series?sport=nba")
read tt code sz <<< "$t"
n=$(jq 'length' "$f" 2>/dev/null)
log "  ?sport=nba  http=${code} returned=${n} bytes=${sz} t=${tt}s"

# A.4 /events 按 tag_slug 全 sport 跑
log ""
log "--- A.4 GET /events?tag_slug=X (全 sport 复用率) ---"

# 老李 v1 列出 + 用户 v2 补全
TAG_SLUGS=(nba mlb nfl nhl soccer tennis mma golf ncaa ncaa-basketball ncaa-football mls epl la-liga bundesliga champions-league formula-1 cricket esports boxing wnba ufc nascar)

# header line
printf '%-22s %8s %8s %8s %10s %8s\n' "tag_slug" "http" "events" "markets" "tokens" "size_KB" >> "$REPORT"
printf '%-22s %8s %8s %8s %10s %8s\n' "tag_slug" "http" "events" "markets" "tokens" "size_KB"

for slug in "${TAG_SLUGS[@]}"; do
  f="${RAW_DIR}/gamma-events-${slug}.json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' \
    "${GAMMA}/events?tag_slug=${slug}&closed=false&limit=200&order=startDate&ascending=true" 2>/dev/null)
  read tt code sz <<< "$t"
  events=$(jq 'length' "$f" 2>/dev/null || echo 0)
  markets=$(jq '[.[] | (.markets // []) | length] | add // 0' "$f" 2>/dev/null)
  tokens=$(jq '[.[] | (.markets // [])[] | (.clobTokenIds // "[]" | fromjson | length)] | add // 0' "$f" 2>/dev/null)
  sz_kb=$(awk -v s="$sz" 'BEGIN{printf "%.1f", s/1024}')
  line=$(printf '%-22s %8s %8s %8s %10s %8s' "$slug" "$code" "$events" "$markets" "$tokens" "$sz_kb")
  echo "  $line"
  echo "  $line" >> "$REPORT"
done

# A.5 /events 探活 (active flag / closed flag 组合)
log ""
log "--- A.5 /events 不同过滤组合行为 ---"
for combo in "closed=false&active=true" "closed=true&active=false&limit=10" "active=true&limit=10"; do
  f="${RAW_DIR}/gamma-events-$(echo $combo | md5 | head -c 8).json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' \
    "${GAMMA}/events?${combo}" 2>/dev/null)
  read tt code sz <<< "$t"
  n=$(jq 'length' "$f" 2>/dev/null || echo 0)
  log "  ?${combo}  http=${code} events=${n} bytes=${sz} t=${tt}s"
done

# A.6 /events/{id} per-item 字段 vs listing 字段一致性 (v1 验过 5 个, v2 再验 3 个不同 sport)
log ""
log "--- A.6 /events/{id} per-item 跨 sport 抽样 ---"
for slug in nba mlb soccer; do
  src="${RAW_DIR}/gamma-events-${slug}.json"
  [ ! -s "$src" ] && continue
  eid=$(jq -r '.[0].id' "$src" 2>/dev/null)
  [ "$eid" = "null" ] && continue
  f="${RAW_DIR}/gamma-event-${eid}.json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${GAMMA}/events/${eid}")
  read tt code sz <<< "$t"
  mk=$(jq '(.markets // []) | length' "$f" 2>/dev/null)
  listing_mk=$(jq --arg id "$eid" '.[] | select((.id|tostring) == $id) | (.markets // []) | length' "$src" 2>/dev/null)
  log "  ${slug} event=${eid}  per_item_markets=${mk}  listing_markets=${listing_mk}  bytes=${sz} t=${tt}s  match=$([ "$mk" = "$listing_mk" ] && echo YES || echo NO)"
done

# A.7 /markets/{id} (gamma 视角) — v1 只验过 1 个 NBA, v2 多验
log ""
log "--- A.7 GET /markets/{id} (gamma) ---"
# 取一个 NBA market id
nba_mid=$(jq -r '.[0].markets[0].id' "${RAW_DIR}/gamma-events-nba.json" 2>/dev/null)
if [ -n "$nba_mid" ] && [ "$nba_mid" != "null" ]; then
  f="${RAW_DIR}/gamma-market-${nba_mid}.json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${GAMMA}/markets/${nba_mid}")
  read tt code sz <<< "$t"
  log "  nba market_id=${nba_mid}  http=${code} bytes=${sz} t=${tt}s"
  # 比对字段
  listing_keys=$(jq -r '.[0].markets[0] | keys | sort | join(",")' "${RAW_DIR}/gamma-events-nba.json" 2>/dev/null | head -c 800)
  single_keys=$(jq -r 'keys | sort | join(",")' "$f" 2>/dev/null | head -c 800)
  log "  listing.markets[0] keys: ${listing_keys}"
  log "  single market keys     : ${single_keys}"
fi

#############################################
# B. CLOB REST 全覆盖
#############################################
log ""
log "===== B. CLOB REST 全覆盖 ====="

# 先从 NBA 拿一个有效 token_id + condition_id
NBA_F="${RAW_DIR}/gamma-events-nba.json"
TOK=$(jq -r '.[].markets // [] | .[] | select(.clobTokenIds and .acceptingOrders == true) | (.clobTokenIds | fromjson | .[0])' "$NBA_F" 2>/dev/null | head -1)
CID=$(jq -r '.[].markets // [] | .[] | select(.clobTokenIds and .acceptingOrders == true) | .conditionId' "$NBA_F" 2>/dev/null | head -1)
[ -z "$TOK" ] && TOK=$(jq -r '.[].markets // [] | .[] | (.clobTokenIds // "[]" | fromjson | .[0])' "$NBA_F" 2>/dev/null | head -1)
[ -z "$CID" ] && CID=$(jq -r '.[].markets // [] | .[] | .conditionId' "$NBA_F" 2>/dev/null | head -1)
log "  样本 token_id=${TOK:0:16}...  conditionId=${CID:0:16}..."

# B.1 /markets 分页 + cursor
log ""
log "--- B.1 GET /markets 分页 ---"
f="${RAW_DIR}/clob-markets-p1.json"
t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${CLOB}/markets?next_cursor=MA==")
read tt code sz <<< "$t"
n=$(jq '.data | length' "$f" 2>/dev/null)
nxt=$(jq -r '.next_cursor' "$f" 2>/dev/null)
limit=$(jq '.limit // null' "$f" 2>/dev/null)
total=$(jq '.count // null' "$f" 2>/dev/null)
log "  page1: http=${code} data=${n} limit=${limit} count_total=${total} next_cursor=${nxt} bytes=${sz} t=${tt}s"
# 翻第二页
f2="${RAW_DIR}/clob-markets-p2.json"
t=$(curl -sS -A "$UA" -o "$f2" -w '%{time_total} %{http_code} %{size_download}' "${CLOB}/markets?next_cursor=${nxt}")
read tt code sz <<< "$t"
n2=$(jq '.data | length' "$f2" 2>/dev/null)
log "  page2 cursor=${nxt}: http=${code} data=${n2} bytes=${sz} t=${tt}s"

# B.2 /sampling-markets vs /sampling-simplified-markets
log ""
log "--- B.2 sampling-markets vs sampling-simplified-markets ---"
for ep in sampling-markets sampling-simplified-markets; do
  f="${RAW_DIR}/clob-${ep}.json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${CLOB}/${ep}?next_cursor=MA==")
  read tt code sz <<< "$t"
  n=$(jq '.data | length' "$f" 2>/dev/null)
  sample_keys=$(jq -r '.data[0] | keys | sort | join(",")' "$f" 2>/dev/null | head -c 300)
  log "  /${ep}: http=${code} data=${n} bytes=${sz} t=${tt}s"
  log "    sample[0] keys: ${sample_keys}"
done

# B.3 /markets/{cid} clob 视角
log ""
log "--- B.3 GET /markets/{condition_id} (clob 视角) ---"
f="${RAW_DIR}/clob-market-by-cid.json"
t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${CLOB}/markets/${CID}")
read tt code sz <<< "$t"
keys=$(jq -r 'keys | sort | join(",")' "$f" 2>/dev/null | head -c 800)
log "  http=${code} bytes=${sz} t=${tt}s"
log "  keys: ${keys}"

# B.4 /book 单
log ""
log "--- B.4 GET /book?token_id= ---"
f="${RAW_DIR}/clob-book.json"
t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${CLOB}/book?token_id=${TOK}")
read tt code sz <<< "$t"
bids=$(jq '.bids | length' "$f" 2>/dev/null)
asks=$(jq '.asks | length' "$f" 2>/dev/null)
log "  http=${code} bids=${bids} asks=${asks} bytes=${sz} t=${tt}s"

# B.5 /books 批量 — 边界已测, v2 再压 250 / 500 / 501
log ""
log "--- B.5 POST /books batch (501 / 500 边界确认) ---"
# 重用 NBA token 池
TOKEN_FILE="${RAW_DIR}/tokens.txt"
jq -r '.[].markets // [] | .[] | (.clobTokenIds // "[]" | fromjson | .[])' \
  "$NBA_F" "${RAW_DIR}/gamma-events-mlb.json" 2>/dev/null | sort -u > "$TOKEN_FILE"
ntok=$(wc -l < "$TOKEN_FILE" | tr -d ' ')
log "  可用 token 池: ${ntok}"
for batch in 1 10 100 500 501; do
  if [ "$ntok" -lt "$batch" ]; then continue; fi
  body=$(head -${batch} "$TOKEN_FILE" | awk '{printf "{\"token_id\":\"%s\"},", $1}' | sed 's/,$//')
  body="[${body}]"
  f="${RAW_DIR}/clob-books-b${batch}.json"
  t=$(curl -sS -A "$UA" -X POST -H "Content-Type: application/json" \
    -o "$f" -w '%{time_total} %{http_code} %{size_download}' \
    --max-time 30 -d "$body" "${CLOB}/books" 2>/dev/null)
  read tt code sz <<< "$t"
  ret=$(jq 'length' "$f" 2>/dev/null || echo "?")
  log "  batch=${batch} http=${code} returned=${ret} bytes=${sz} t=${tt}s"
done

# B.6 /price /midpoint /spread
log ""
log "--- B.6 /price /midpoint /spread ---"
for ep in "price?token_id=${TOK}&side=BUY" "price?token_id=${TOK}&side=SELL" "midpoint?token_id=${TOK}" "spread?token_id=${TOK}"; do
  short=$(echo "$ep" | cut -d'?' -f1)
  side=""
  if echo "$ep" | grep -q "side="; then side=$(echo "$ep" | sed 's/.*side=//'); fi
  f="${RAW_DIR}/clob-${short}-${side}.json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${CLOB}/${ep}")
  read tt code sz <<< "$t"
  v=$(cat "$f")
  log "  /${short}${side:+ side=$side}: http=${code} bytes=${sz} t=${tt}s  body=${v:0:100}"
done

# B.7 /tick-size /neg-risk
log ""
log "--- B.7 /tick-size /neg-risk ---"
for ep in "tick-size?token_id=${TOK}" "neg-risk?token_id=${TOK}"; do
  short=$(echo "$ep" | cut -d'?' -f1)
  f="${RAW_DIR}/clob-${short}.json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${CLOB}/${ep}")
  read tt code sz <<< "$t"
  v=$(cat "$f")
  log "  /${short}: http=${code} bytes=${sz} t=${tt}s  body=${v}"
done

# B.8 /prices-history
log ""
log "--- B.8 /prices-history (interval / fidelity / startTs+endTs) ---"
for q in \
  "market=${TOK}&interval=1d&fidelity=60" \
  "market=${TOK}&interval=1h&fidelity=1" \
  "market=${TOK}&interval=1w&fidelity=1440" \
  "market=${TOK}&startTs=$((t_local - 3600))&endTs=${t_local}&fidelity=1"; do
  f="${RAW_DIR}/clob-history-$(echo $q | md5 | head -c 8).json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${CLOB}/prices-history?${q}")
  read tt code sz <<< "$t"
  pts=$(jq '.history | length' "$f" 2>/dev/null || echo 0)
  q_redact=$(echo "$q" | sed 's/market=[0-9]*/market=<TOK>/')
  log "  ${q_redact}: http=${code} pts=${pts} bytes=${sz} t=${tt}s"
done

# B.9 /trades 公开 (按 market filter, 无 user) — 看是否公开
log ""
log "--- B.9 /trades 公开 filter ---"
# 尝试公开调用
for q in "market=${CID}&limit=10" "asset_id=${TOK}&limit=10"; do
  f="${RAW_DIR}/clob-trades-pub-$(echo $q | md5 | head -c 8).json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${CLOB}/trades?${q}" 2>/dev/null)
  read tt code sz <<< "$t"
  q_red=$(echo "$q" | sed -e 's/market=[a-f0-9x]*/market=<CID>/' -e 's/asset_id=[0-9]*/asset_id=<TOK>/')
  log "  /trades?${q_red}: http=${code} bytes=${sz} t=${tt}s"
done

#############################################
# B+. CLOB 私有 endpoint (L2 HMAC)
#############################################
log ""
log "===== B+. CLOB 私有 endpoint (L2 HMAC) ====="

# 用 python 写 helper, 生成 L2 header
HELPER_PY="${RAW_DIR}/l2_helper.py"
cat > "$HELPER_PY" <<'PY'
"""L2 HMAC signer + L1 EIP-712 — laoli v2."""
import os, sys, hmac, hashlib, base64, time, json, urllib.request, urllib.parse, urllib.error

API_KEY = os.environ['POLYMARKET_API_KEY']
API_SECRET = os.environ['POLYMARKET_API_SECRET']
API_PASS = os.environ['POLYMARKET_API_PASSPHRASE']
WALLET_PK = os.environ['WALLET_PRIVATE_KEY']
FUNDER = os.environ['POLYMARKET_FUNDER_ADDRESS']
CLOB = "https://clob.polymarket.com"
UA = "curl/8.4.0 sports-trader-cpp-laoli-v2"

# signer EOA 从 private key 推
from eth_account import Account
acct = Account.from_key(WALLET_PK)
SIGNER = acct.address

def l2_sign(method, path_with_qs, body=""):
    ts = str(int(time.time()))
    msg = ts + method.upper() + path_with_qs + body
    secret_bytes = base64.urlsafe_b64decode(API_SECRET + "=" * ((4 - len(API_SECRET) % 4) % 4))
    sig = hmac.new(secret_bytes, msg.encode(), hashlib.sha256).digest()
    sig_b64 = base64.urlsafe_b64encode(sig).rstrip(b"=").decode()
    return {
        "POLY_ADDRESS": SIGNER,
        "POLY_API_KEY": API_KEY,
        "POLY_PASSPHRASE": API_PASS,
        "POLY_SIGNATURE": sig_b64,
        "POLY_TIMESTAMP": ts,
        "User-Agent": UA,
    }

def call(method, path_with_qs, body=None):
    headers = l2_sign(method, path_with_qs, body or "")
    url = CLOB + path_with_qs
    data = body.encode() if body else None
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    if body: req.add_header("Content-Type", "application/json")
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            raw = r.read()
            return {
                "http": r.status,
                "t_s": round(time.time()-t0, 3),
                "bytes": len(raw),
                "body": raw.decode(errors='replace')[:500],
            }
    except urllib.error.HTTPError as e:
        return {"http": e.code, "t_s": round(time.time()-t0, 3), "bytes": 0, "body": e.read().decode(errors='replace')[:500]}
    except Exception as e:
        return {"http": -1, "t_s": round(time.time()-t0, 3), "bytes": 0, "body": str(e)[:500]}

def main():
    op = sys.argv[1]
    if op == "auth-api-keys":
        print(json.dumps(call("GET", "/auth/api-keys")))
    elif op == "data-trades":
        # 我方成交 — 没传 user 走当前 funder
        print(json.dumps(call("GET", "/trades?limit=10")))
    elif op == "data-orders":
        print(json.dumps(call("GET", "/data/orders")))
    elif op == "balance-allowance":
        # signature_type=2 (proxy 1-of-1)
        for st in [0,1,2]:
            r = call("GET", f"/balance-allowance?param_type=COLLATERAL&signature_type={st}")
            print(json.dumps({"signature_type":st, **r}))
    elif op == "order-by-id-noop":
        # 假 order id, 期望 404, 验证鉴权路径
        print(json.dumps(call("GET", "/data/order/0xdead")))
    elif op == "derive-api-key":
        # L1 EIP-712: ClobAuth
        from eth_account.messages import encode_typed_data
        ts = str(int(time.time()))
        typed = {
            "types":{
                "EIP712Domain":[
                    {"name":"name","type":"string"},
                    {"name":"version","type":"string"},
                    {"name":"chainId","type":"uint256"}
                ],
                "ClobAuth":[
                    {"name":"address","type":"address"},
                    {"name":"timestamp","type":"string"},
                    {"name":"nonce","type":"uint256"},
                    {"name":"message","type":"string"}
                ]
            },
            "domain":{"name":"ClobAuthDomain","version":"1","chainId":137},
            "primaryType":"ClobAuth",
            "message":{
                "address": SIGNER,
                "timestamp": ts,
                "nonce": 0,
                "message": "This message attests that I control the given wallet"
            }
        }
        em = encode_typed_data(full_message=typed)
        sig = acct.sign_message(em).signature.hex()
        if not sig.startswith("0x"): sig = "0x" + sig
        headers = {
            "POLY_ADDRESS": SIGNER,
            "POLY_SIGNATURE": sig,
            "POLY_TIMESTAMP": ts,
            "POLY_NONCE": "0",
            "User-Agent": UA,
        }
        url = CLOB + "/auth/derive-api-key"
        req = urllib.request.Request(url, headers=headers, method="GET")
        t0 = time.time()
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                raw = r.read()
                # 隐去 secret
                body = raw.decode(errors='replace')[:500]
                # mask
                try:
                    obj = json.loads(raw)
                    masked = {k: (v if k not in ("secret","passphrase","apiKey") else f"<{k.upper()}_REDACTED>") for k,v in obj.items()}
                    body = json.dumps(masked)
                except: pass
                print(json.dumps({"http":r.status,"t_s":round(time.time()-t0,3),"bytes":len(raw),"body":body}))
        except urllib.error.HTTPError as e:
            print(json.dumps({"http":e.code,"t_s":round(time.time()-t0,3),"bytes":0,"body":e.read().decode(errors='replace')[:500]}))
    elif op == "signer":
        print(SIGNER)

if __name__ == "__main__":
    main()
PY

PYBIN="${PROJECT_ROOT}/.venv/bin/python"

# B+.1 /auth/api-keys
log ""
log "--- B+.1 GET /auth/api-keys ---"
SIGNER=$("$PYBIN" "$HELPER_PY" signer 2>/dev/null)
log "  signer EOA = ${SIGNER}"
out=$("$PYBIN" "$HELPER_PY" auth-api-keys 2>&1)
log "  $(echo $out | redact)"

# B+.2 /auth/derive-api-key  (L1 EIP-712)
log ""
log "--- B+.2 GET /auth/derive-api-key (L1) ---"
out=$("$PYBIN" "$HELPER_PY" derive-api-key 2>&1)
log "  $(echo $out | redact)"

# B+.3 /trades (我方)
log ""
log "--- B+.3 GET /trades (我方, L2) ---"
out=$("$PYBIN" "$HELPER_PY" data-trades 2>&1)
log "  $(echo $out | redact)"

# B+.4 /data/orders
log ""
log "--- B+.4 GET /data/orders (我方活跃单, L2) ---"
out=$("$PYBIN" "$HELPER_PY" data-orders 2>&1)
log "  $(echo $out | redact)"

# B+.5 /balance-allowance (跑 0/1/2 三个 signature_type)
log ""
log "--- B+.5 GET /balance-allowance (signature_type sweep) ---"
out=$("$PYBIN" "$HELPER_PY" balance-allowance 2>&1)
echo "$out" | redact | while read line; do log "  $line"; done

# B+.6 /data/order/{noop}  — 验证鉴权路径
log ""
log "--- B+.6 GET /data/order/<fake>  鉴权探活 ---"
out=$("$PYBIN" "$HELPER_PY" order-by-id-noop 2>&1)
log "  $(echo $out | redact)"

#############################################
# C. data API 全覆盖
#############################################
log ""
log "===== C. data-api 全覆盖 ====="

FUNDER="${POLYMARKET_FUNDER_ADDRESS}"

# C.1 /positions  含 redeemable / mergeable
log ""
log "--- C.1 GET /positions ---"
for filt in "" "&sortBy=CURRENT&sortDirection=DESC" "&redeemable=true" "&mergeable=true" "&sizeThreshold=0.1"; do
  f="${RAW_DIR}/data-positions$(echo $filt | md5 | head -c 6).json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${DATA}/positions?user=${FUNDER}${filt}")
  read tt code sz <<< "$t"
  n=$(jq 'length' "$f" 2>/dev/null || echo 0)
  log "  ?user=<F>${filt}: http=${code} returned=${n} bytes=${sz} t=${tt}s"
done

# C.2 /value
log ""
log "--- C.2 GET /value ---"
f="${RAW_DIR}/data-value.json"
t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${DATA}/value?user=${FUNDER}")
read tt code sz <<< "$t"
v=$(cat "$f")
log "  http=${code} bytes=${sz} t=${tt}s body=${v}"

# C.3 /trades 公开 by user
log ""
log "--- C.3 GET /trades by user ---"
for q in "limit=10" "limit=100" "side=BUY&limit=10" "takerOnly=true&limit=10"; do
  f="${RAW_DIR}/data-trades-$(echo $q | md5 | head -c 6).json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${DATA}/trades?user=${FUNDER}&${q}")
  read tt code sz <<< "$t"
  n=$(jq 'length' "$f" 2>/dev/null || echo 0)
  log "  ?${q}: http=${code} returned=${n} bytes=${sz} t=${tt}s"
done

# C.4 /activity 全 type
log ""
log "--- C.4 GET /activity 跨 type ---"
for typ in "" "TRADE" "REDEEM" "MERGE" "SPLIT" "REWARD" "DEPOSIT" "WITHDRAWAL" "CONVERT" "SPLIT_REWARDS"; do
  qs="user=${FUNDER}&limit=50"
  [ -n "$typ" ] && qs="${qs}&type=${typ}"
  f="${RAW_DIR}/data-activity-${typ:-all}.json"
  t=$(curl -sS -A "$UA" -o "$f" -w '%{time_total} %{http_code} %{size_download}' "${DATA}/activity?${qs}")
  read tt code sz <<< "$t"
  n=$(jq 'length' "$f" 2>/dev/null || echo 0)
  # 找出 unique type 取值
  uniq_t=$(jq -r '[.[].type] | unique | join(",")' "$f" 2>/dev/null | head -c 200)
  log "  type=${typ:-<none>}: http=${code} returned=${n} bytes=${sz} t=${tt}s  observed_types=${uniq_t}"
done

# C.5 探未文档化 endpoint
log ""
log "--- C.5 data-api 未文档 endpoint 探活 ---"
for ep in "/holdings?user=${FUNDER}" "/pnl?user=${FUNDER}" "/user/${FUNDER}" "/leaderboard?limit=5" "/markets" "/series" "/events"; do
  code=$(curl -sS -A "$UA" -o /dev/null -w '%{http_code}' "${DATA}${ep}")
  ep_redact=$(echo "$ep" | sed "s|${FUNDER}|<FUNDER>|g")
  log "  ${ep_redact}: http=${code}"
done

#############################################
# D. WebSocket — market + user channel
#############################################
log ""
log "===== D. WebSocket 全 channel ====="

# D.1 /ws/market 多 token 压力 100 / 300 / 500
log ""
log "--- D.1 /ws/market 单连接多 token ---"
head -500 "$TOKEN_FILE" > "${RAW_DIR}/wss-tokens-500.txt"

WSSMARKET_PY="${RAW_DIR}/wss_market.py"
cat > "$WSSMARKET_PY" <<'PY'
import asyncio, json, sys, time
import websockets

async def main():
    n = int(sys.argv[1])
    tokens = [l.strip() for l in open(sys.argv[2]) if l.strip()][:n]
    url = "wss://ws-subscriptions-clob.polymarket.com/ws/market"
    t0 = time.time()
    try:
        async with websockets.connect(url, open_timeout=15) as ws:
            t_open = time.time() - t0
            await ws.send(json.dumps({"type":"Market","assets_ids":tokens}))
            msgs = 0; bytes_in = 0; first = None
            unique = set()
            ev_types = {}
            try:
                while time.time() - t0 < 20:
                    m = await asyncio.wait_for(ws.recv(), timeout=4)
                    if first is None: first = time.time() - t0
                    msgs += 1; bytes_in += len(m)
                    try:
                        d = json.loads(m)
                        items = d if isinstance(d, list) else [d]
                        for it in items:
                            if isinstance(it, dict):
                                if 'asset_id' in it: unique.add(it['asset_id'])
                                ev = it.get('event_type','?')
                                ev_types[ev] = ev_types.get(ev,0)+1
                    except: pass
            except asyncio.TimeoutError: pass
            print(json.dumps({
                "subscribed": n, "open_s": round(t_open,3),
                "first_msg_s": round(first or -1,3), "duration_s": round(time.time()-t0,3),
                "msgs": msgs, "bytes": bytes_in, "unique_assets": len(unique),
                "event_types": ev_types,
            }))
    except Exception as e:
        print(json.dumps({"error":str(e)[:200],"subscribed":n}))

asyncio.run(main())
PY

for n in 1 50 100 300 500; do
  out=$("$PYBIN" "$WSSMARKET_PY" "$n" "${RAW_DIR}/wss-tokens-500.txt" 2>&1 | tail -1)
  log "  N=${n}: ${out}"
done

# D.2 /ws/user 鉴权 — 用 conditionId
log ""
log "--- D.2 /ws/user 鉴权 + 多 condition_id ---"
# 取多个 condition_id
jq -r '.[].markets // [] | .[] | .conditionId' "$NBA_F" 2>/dev/null | head -20 > "${RAW_DIR}/wss-cids.txt"

WSSUSER_PY="${RAW_DIR}/wss_user.py"
cat > "$WSSUSER_PY" <<'PY'
import asyncio, json, sys, time, os
import websockets

async def main():
    cids = [l.strip() for l in open(sys.argv[1]) if l.strip()]
    url = "wss://ws-subscriptions-clob.polymarket.com/ws/user"
    payload = {
        "type":"User",
        "auth":{
            "apiKey": os.environ['POLYMARKET_API_KEY'],
            "secret": os.environ['POLYMARKET_API_SECRET'],
            "passphrase": os.environ['POLYMARKET_API_PASSPHRASE'],
        },
        "markets": cids,
    }
    t0 = time.time()
    try:
        async with websockets.connect(url, open_timeout=15) as ws:
            t_open = time.time() - t0
            await ws.send(json.dumps(payload))
            msgs = 0; bytes_in = 0; first = None
            try:
                while time.time() - t0 < 10:
                    m = await asyncio.wait_for(ws.recv(), timeout=3)
                    if first is None: first = time.time() - t0
                    msgs += 1; bytes_in += len(m)
            except asyncio.TimeoutError: pass
            print(json.dumps({
                "subscribed_cids": len(cids),
                "open_s": round(t_open,3),
                "first_msg_s": round(first or -1,3),
                "msgs": msgs, "bytes": bytes_in,
                "note": "silent=healthy (no order events)"
            }))
    except Exception as e:
        print(json.dumps({"error":str(e)[:200]}))

asyncio.run(main())
PY
out=$("$PYBIN" "$WSSUSER_PY" "${RAW_DIR}/wss-cids.txt" 2>&1 | tail -1)
log "  user channel: ${out}"

#############################################
# E. 限流压测 — 公开 endpoint 20 并发
#############################################
log ""
log "===== E. 限流压测 ====="
log "--- E.1 gamma /events 20 并发 ---"
seq 1 20 | xargs -P 20 -I{} curl -sS -A "$UA" -o /dev/null -w '%{http_code} %{time_total}\n' \
  "${GAMMA}/events?tag_slug=nba&closed=false&limit=5" 2>/dev/null | sort | uniq -c | tee -a "$REPORT"

log "--- E.2 clob /price 30 并发 ---"
seq 1 30 | xargs -P 30 -I{} curl -sS -A "$UA" -o /dev/null -w '%{http_code} %{time_total}\n' \
  "${CLOB}/price?token_id=${TOK}&side=BUY" 2>/dev/null | sort | uniq -c | tee -a "$REPORT"

log "--- E.3 data /positions 20 并发 ---"
seq 1 20 | xargs -P 20 -I{} curl -sS -A "$UA" -o /dev/null -w '%{http_code} %{time_total}\n' \
  "${DATA}/positions?user=${FUNDER}" 2>/dev/null | sort | uniq -c | tee -a "$REPORT"

#############################################
# Summary
#############################################
log ""
log "===== 汇总 ====="
log "raw: ${RAW_DIR}"
log "report: ${REPORT}"
echo "DONE: ${REPORT}"
