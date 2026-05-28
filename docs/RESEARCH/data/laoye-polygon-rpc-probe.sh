#!/usr/bin/env bash
# 老叶 — Polygon RPC 全 endpoint 复用率矩阵 v1 实证脚本
# 用法: bash laoye-polygon-rpc-probe.sh
#
# 输出: 同目录下 laoye-polygon-rpc-probe-YYYYMMDD-HHMMSS.txt
#       JSON dump 落 /tmp/laoye_polygon_*
#
# 约束:
#  - 不发交易, 只读
#  - 每 endpoint × vendor 调用 ≤ 10 次
#  - 不打满限流; vendor 之间间隔 200 ms
#  - 不写凭证 (本脚本只用公开 endpoint, 无 key)

set -u
PROJECT_ROOT="/Users/wangweibo/code/sports-trader-cpp"
OUT_DIR="${PROJECT_ROOT}/docs/RESEARCH/data"
STAMP=$(date +%Y%m%d-%H%M%S)
RAW_DIR="/tmp/laoye_polygon_${STAMP}"
mkdir -p "$RAW_DIR"
REPORT="${OUT_DIR}/laoye-polygon-rpc-probe-${STAMP}.txt"

UA="curl/8.4.0 sports-trader-cpp-laoye-probe"

# 公共 endpoint (无 key, 全免费 / demo)
# 三 vendor 中:
#  - Alchemy demo 是社区共享 key, 只用来感知响应特征, 不靠它做 SLA
#  - dRPC public 是真正的免费层
#  - Ankr public 多次 401 (老陈实测 30 次全 401), 改用 polygon-rpc.com 官方公共池
RPC_ALCHEMY="https://polygon-mainnet.g.alchemy.com/v2/demo"
RPC_QUICKNODE_PUBLIC="https://docs-demo.polygon-mainnet.quiknode.pro/"  # QuickNode 公开 demo
RPC_DRPC="https://polygon.drpc.org"
RPC_PUBLIC="https://polygon-rpc.com"           # 官方公共池 (作对照)
RPC_LLAMA="https://polygon.llamarpc.com"       # 备用 (公开)

# Polymarket 链上合约 (见 laoli-polymarket-api-spec-v1.md)
CTF_EXCHANGE="0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E"
NEG_RISK_CTF_EXCHANGE="0xC5d563A36AE78145C45a50134d48A1215220f80a"
USDC_E_POLYGON="0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174"
CTF_CONDITIONAL_TOKENS="0x4D97DCd97eC945f40cF65F87097ACe5EA0476045"

echo "# 老叶 Polygon RPC probe ${STAMP}" > "$REPORT"
echo "# project: sports-trader-cpp" >> "$REPORT"
echo "# constraint: read-only, <=10 calls/endpoint" >> "$REPORT"
echo "" >> "$REPORT"

log() { echo "$@" | tee -a "$REPORT"; }
hr()  { log "----------------------------------------------------------------"; }

# rpc_call <vendor_url> <json_body> <n_iter>
# emit: per-call total_s + http_code + size; final stats min/median/max + size; redact url
rpc_call() {
  local url="$1"; local body="$2"; local n="$3"; local label="$4"
  local f="${RAW_DIR}/$(echo "${label}" | tr ' /' '__').last.json"
  local times=()
  local sizes=()
  local codes=()
  local i=0
  while [ $i -lt $n ]; do
    i=$((i+1))
    out=$(curl -sS -A "$UA" -o "$f" -w '%{http_code} %{time_total} %{size_download}' \
           -H 'content-type: application/json' \
           -X POST -d "$body" "$url" 2>/dev/null)
    code=$(echo "$out" | awk '{print $1}')
    t=$(echo "$out" | awk '{print $2}')
    s=$(echo "$out" | awk '{print $3}')
    codes+=("$code"); times+=("$t"); sizes+=("$s")
    sleep 0.2
  done
  # python 计 median (避免 bash 浮点 / sort 跨平台问题)
  median=$(python3 -c "import sys; a=sorted(map(float,sys.argv[1:])); n=len(a); print(f'{a[n//2]:.3f}')" "${times[@]}")
  min=$(python3 -c "import sys; print(f'{min(map(float,sys.argv[1:])):.3f}')" "${times[@]}")
  max=$(python3 -c "import sys; print(f'{max(map(float,sys.argv[1:])):.3f}')" "${times[@]}")
  size_med=$(python3 -c "import sys; a=sorted(map(int,sys.argv[1:])); print(a[len(a)//2])" "${sizes[@]}")
  ok=$(python3 -c "import sys; print(sum(1 for c in sys.argv[1:] if c=='200'))" "${codes[@]}")
  log "  ${label}  n=${n}  ok=${ok}/${n}  t_s=min/med/max=${min}/${median}/${max}  size=${size_med}B"
}

# Polymarket 合约 eth_call: balanceOf(address) — selector 0x70a08231
# 用本节脚本不发交易, 只 view function
PROBE_ADDR="0x0000000000000000000000000000000000000000"  # zero address, 任何 view 都能跑
PROBE_ADDR_PADDED="000000000000000000000000${PROBE_ADDR:2}"

# 已 mined 老块 (创世块附近 1) 作为 immutable 测试目标
# Polygon block 1 (2020 年起源) 一定 mined, 用 0x1
OLD_BLOCK_HEX="0x1"
# 一个 famous Polymarket 历史 tx (从 polygonscan 公共信息: 大量 USDC.e transfer)
# 此处用 USDC.e 部署 tx 做参照
USDC_DEPLOY_TX="0x4ea202c10fb6ef67df9faaff5b9e72625f9f5a1c87b6f24d04dcd5dcfa9e0a07"

hr
log "0. 测试 vendor / endpoint 清单"
log "   Alchemy demo:     ${RPC_ALCHEMY}"
log "   QuickNode demo:   ${RPC_QUICKNODE_PUBLIC}"
log "   dRPC public:      ${RPC_DRPC}"
log "   Polygon public:   ${RPC_PUBLIC}"
log "   LlamaRPC:         ${RPC_LLAMA}"
hr

# 各 vendor 跑一遍 baseline: chainId + blockNumber
for vendor in "alchemy:${RPC_ALCHEMY}" "quicknode:${RPC_QUICKNODE_PUBLIC}" "drpc:${RPC_DRPC}" "public:${RPC_PUBLIC}" "llama:${RPC_LLAMA}"; do
  name="${vendor%%:*}"
  url="${vendor#*:}"
  log ""
  log "=========== Vendor: ${name} ==========="

  # 1. eth_chainId (cacheable forever)
  rpc_call "$url" '{"jsonrpc":"2.0","id":1,"method":"eth_chainId","params":[]}' 5 "[${name}] eth_chainId"

  # 2. eth_blockNumber (latest, no cache)
  rpc_call "$url" '{"jsonrpc":"2.0","id":1,"method":"eth_blockNumber","params":[]}' 10 "[${name}] eth_blockNumber"

  # 3. eth_gasPrice (legacy, latest)
  rpc_call "$url" '{"jsonrpc":"2.0","id":1,"method":"eth_gasPrice","params":[]}' 5 "[${name}] eth_gasPrice"

  # 4. eth_maxPriorityFeePerGas (EIP-1559)
  rpc_call "$url" '{"jsonrpc":"2.0","id":1,"method":"eth_maxPriorityFeePerGas","params":[]}' 5 "[${name}] eth_maxPriorityFeePerGas"

  # 5. eth_feeHistory (1 call covers N blocks of baseFee+priority)
  #    blockCount=20, newestBlock=latest, rewardPercentiles=[25,50,75]
  rpc_call "$url" '{"jsonrpc":"2.0","id":1,"method":"eth_feeHistory","params":["0x14","latest",[25,50,75]]}' 5 "[${name}] eth_feeHistory(20blocks)"

  # 6. eth_getBalance (USDC.e contract address — 任何地址都行, 这里用零地址)
  rpc_call "$url" "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBalance\",\"params\":[\"${PROBE_ADDR}\",\"latest\"]}" 5 "[${name}] eth_getBalance"

  # 7. eth_getTransactionCount (nonce 查询)
  rpc_call "$url" "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getTransactionCount\",\"params\":[\"${PROBE_ADDR}\",\"latest\"]}" 5 "[${name}] eth_getTransactionCount"

  # 8. eth_getCode (USDC.e bytecode — bulky one-time)
  rpc_call "$url" "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getCode\",\"params\":[\"${USDC_E_POLYGON}\",\"latest\"]}" 3 "[${name}] eth_getCode(USDC.e)"

  # 9. eth_getStorageAt (USDC.e slot 0)
  rpc_call "$url" "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getStorageAt\",\"params\":[\"${USDC_E_POLYGON}\",\"0x0\",\"latest\"]}" 3 "[${name}] eth_getStorageAt"

  # 10. eth_call (USDC.e balanceOf 零地址)
  rpc_call "$url" "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_call\",\"params\":[{\"to\":\"${USDC_E_POLYGON}\",\"data\":\"0x70a08231${PROBE_ADDR_PADDED}\"},\"latest\"]}" 5 "[${name}] eth_call(balanceOf)"

  # 11. eth_estimateGas (USDC.e transfer 0 to zero — view-safe)
  #     selector 0xa9059cbb transfer(address,uint256)
  rpc_call "$url" "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_estimateGas\",\"params\":[{\"to\":\"${USDC_E_POLYGON}\",\"data\":\"0xa9059cbb${PROBE_ADDR_PADDED}0000000000000000000000000000000000000000000000000000000000000000\"}]}" 3 "[${name}] eth_estimateGas(transfer)"

  # 12. eth_getBlockByNumber latest, full tx — bulk 经典
  rpc_call "$url" '{"jsonrpc":"2.0","id":1,"method":"eth_getBlockByNumber","params":["latest",true]}' 3 "[${name}] eth_getBlockByNumber(latest,full)"

  # 13. eth_getBlockByNumber latest, hash-only (size 对比)
  rpc_call "$url" '{"jsonrpc":"2.0","id":1,"method":"eth_getBlockByNumber","params":["latest",false]}' 3 "[${name}] eth_getBlockByNumber(latest,hashonly)"

  # 14. eth_getBlockByNumber 旧块 (immutable, archive-ish)
  rpc_call "$url" "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBlockByNumber\",\"params\":[\"${OLD_BLOCK_HEX}\",false]}" 3 "[${name}] eth_getBlockByNumber(old,hashonly)"

  # 15. eth_getLogs (CTF Exchange Trade event topic, 单块, 复用率经典)
  #     CTFExchange OrderFilled event signature keccak256:
  #     event OrderFilled(bytes32 orderHash, address indexed maker, address indexed taker, uint256 makerAssetId, uint256 takerAssetId, uint256 makerAmountFilled, uint256 takerAmountFilled, uint256 fee)
  #     用 fromBlock=latest-10 toBlock=latest 不指定 topic, 演示 1 次 RPC 拉 N event
  rpc_call "$url" "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getLogs\",\"params\":[{\"address\":\"${CTF_EXCHANGE}\",\"fromBlock\":\"latest\",\"toBlock\":\"latest\"}]}" 3 "[${name}] eth_getLogs(CTFExchange@latest)"

  # 16. JSON-RPC batch (single HTTP, 多个 call): blockNumber + gasPrice + chainId
  rpc_call "$url" '[{"jsonrpc":"2.0","id":1,"method":"eth_blockNumber","params":[]},{"jsonrpc":"2.0","id":2,"method":"eth_gasPrice","params":[]},{"jsonrpc":"2.0","id":3,"method":"eth_chainId","params":[]}]' 3 "[${name}] batch(3 calls)"

done

# ============================================================
# Z. JSON-RPC batch 上限实测 (核心: 找各 vendor batch 软上限)
# ============================================================
log ""
hr
log "Z. JSON-RPC batch 上限实测"
hr

build_batch() {
  local n="$1"
  python3 -c "
import json, sys
n=int(sys.argv[1])
calls=[{'jsonrpc':'2.0','id':i,'method':'eth_blockNumber','params':[]} for i in range(n)]
print(json.dumps(calls))
" "$n"
}

probe_batch() {
  local url="$1"; local name="$2"; local n="$3"
  local body=$(build_batch "$n")
  local f="${RAW_DIR}/batch_${name}_${n}.json"
  out=$(curl -sS -A "$UA" -o "$f" -w '%{http_code} %{time_total} %{size_download}' \
         -H 'content-type: application/json' \
         --max-time 30 \
         -X POST -d "$body" "$url" 2>/dev/null)
  code=$(echo "$out" | awk '{print $1}')
  t=$(echo "$out" | awk '{print $2}')
  s=$(echo "$out" | awk '{print $3}')
  # 检查返回是否仍是 batch array (有些 vendor 拒绝时返回 1 个 error object)
  got=$(python3 -c "
import json,sys
try:
  d=json.load(open(sys.argv[1]))
  if isinstance(d,list): print(len(d))
  else: print('-1')
except Exception:
  print('-2')
" "$f" 2>/dev/null)
  log "  [${name}] batch_size=${n}  http=${code}  t_s=${t}  size=${s}B  returned=${got}"
}

for vendor in "alchemy:${RPC_ALCHEMY}" "quicknode:${RPC_QUICKNODE_PUBLIC}" "drpc:${RPC_DRPC}" "public:${RPC_PUBLIC}"; do
  name="${vendor%%:*}"
  url="${vendor#*:}"
  log ""
  log "--- batch probe: ${name} ---"
  for n in 1 10 50 100 200 500 1000; do
    probe_batch "$url" "$name" "$n"
    sleep 0.3
  done
done

# ============================================================
# Y. WebSocket subscribe 复用 (用 python websockets, 1 连接多 subscription)
# ============================================================
log ""
hr
log "Y. WebSocket subscription 复用 (单连接订阅 newHeads + logs)"
hr

# WSS demo endpoint (Alchemy demo 限制实测看看; dRPC 公共 WSS 存在)
WSS_DRPC="wss://polygon.drpc.org"
# Alchemy demo wss
WSS_ALCHEMY="wss://polygon-mainnet.g.alchemy.com/v2/demo"

cat > "${RAW_DIR}/wss_probe.py" <<'PYEOF'
#!/usr/bin/env python3
# WebSocket subscription 复用率探测
# 在 1 个连接上同时订阅 newHeads + logs (CTFExchange) + newPendingTransactions
# 观察 30s 内每个 subscription 推送数 + 推送间隔
import asyncio, json, time, sys, os

try:
    import websockets
except ImportError:
    print("websockets not installed; skip")
    sys.exit(0)

CTF_EXCHANGE = "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E"

async def probe(url, name, dur=20):
    print(f"[{name}] connecting {url}")
    t0 = time.time()
    try:
        async with websockets.connect(url, open_timeout=10, ping_interval=15, ping_timeout=10, max_size=2**24) as ws:
            t_handshake = (time.time() - t0) * 1000
            print(f"[{name}] handshake_ms={t_handshake:.0f}")

            # 订阅 newHeads
            await ws.send(json.dumps({"jsonrpc":"2.0","id":1,"method":"eth_subscribe","params":["newHeads"]}))
            r1 = json.loads(await asyncio.wait_for(ws.recv(), timeout=10))
            sub_heads = r1.get("result"); print(f"[{name}] sub_newHeads={sub_heads}")

            # 订阅 logs on CTF Exchange (复用王: 1 sub 覆盖整个合约所有 event)
            await ws.send(json.dumps({"jsonrpc":"2.0","id":2,"method":"eth_subscribe","params":["logs",{"address":CTF_EXCHANGE}]}))
            r2 = json.loads(await asyncio.wait_for(ws.recv(), timeout=10))
            sub_logs = r2.get("result"); print(f"[{name}] sub_logs(CTFExchange)={sub_logs}")

            # 尝试订阅 newPendingTransactions (有些 vendor 限制)
            try:
                await ws.send(json.dumps({"jsonrpc":"2.0","id":3,"method":"eth_subscribe","params":["newPendingTransactions"]}))
                r3 = json.loads(await asyncio.wait_for(ws.recv(), timeout=5))
                sub_pending = r3.get("result") or r3.get("error")
                print(f"[{name}] sub_pending={sub_pending}")
            except Exception as e:
                print(f"[{name}] sub_pending err: {e}")
                sub_pending = None

            # 收 dur 秒消息, 按 subscription 分类计数
            counts = {sub_heads:0, sub_logs:0}
            if isinstance(sub_pending, str): counts[sub_pending] = 0
            intervals = {sub_heads:[], sub_logs:[]}
            if isinstance(sub_pending, str): intervals[sub_pending] = []
            last_ts = {k:None for k in counts}
            sizes = {k:[] for k in counts}

            t_start = time.time()
            try:
                while time.time() - t_start < dur:
                    try:
                        msg = await asyncio.wait_for(ws.recv(), timeout=dur)
                    except asyncio.TimeoutError:
                        break
                    try:
                        d = json.loads(msg)
                    except:
                        continue
                    sub = d.get("params",{}).get("subscription")
                    if sub in counts:
                        counts[sub] += 1
                        now = time.time()
                        if last_ts[sub] is not None:
                            intervals[sub].append(now - last_ts[sub])
                        last_ts[sub] = now
                        sizes[sub].append(len(msg))
            except websockets.exceptions.ConnectionClosed as e:
                print(f"[{name}] conn closed mid-window: {e}")

            # 退订
            for sub in counts:
                try:
                    await ws.send(json.dumps({"jsonrpc":"2.0","id":99,"method":"eth_unsubscribe","params":[sub]}))
                except: pass

            print(f"[{name}] === results over {dur}s ===")
            print(f"[{name}] newHeads:   count={counts.get(sub_heads,0)}  avg_interval_s={sum(intervals[sub_heads])/max(len(intervals[sub_heads]),1):.2f}  avg_msg_B={sum(sizes[sub_heads])/max(len(sizes[sub_heads]),1):.0f}")
            print(f"[{name}] logs:       count={counts.get(sub_logs,0)}  avg_msg_B={sum(sizes[sub_logs])/max(len(sizes[sub_logs]),1):.0f}")
            if isinstance(sub_pending, str):
                cp = counts.get(sub_pending,0)
                print(f"[{name}] pendingTx:  count={cp}")
    except Exception as e:
        print(f"[{name}] error: {e}")

async def main():
    targets = [
        ("drpc", os.environ.get("WSS_DRPC", "wss://polygon.drpc.org")),
        ("alchemy_demo", os.environ.get("WSS_ALCHEMY", "wss://polygon-mainnet.g.alchemy.com/v2/demo")),
    ]
    for name, url in targets:
        await probe(url, name, dur=20)
        print()

if __name__ == "__main__":
    asyncio.run(main())
PYEOF

WSS_DRPC="$WSS_DRPC" WSS_ALCHEMY="$WSS_ALCHEMY" python3 "${RAW_DIR}/wss_probe.py" 2>&1 | tee -a "$REPORT"

# ============================================================
# X. Polygon 特有 RPC (bor_*)
# ============================================================
log ""
hr
log "X. Polygon 特有 RPC: bor_getCurrentValidators / bor_getCurrentProposer / eth_getRootHash"
hr

for vendor in "alchemy:${RPC_ALCHEMY}" "quicknode:${RPC_QUICKNODE_PUBLIC}" "drpc:${RPC_DRPC}" "public:${RPC_PUBLIC}"; do
  name="${vendor%%:*}"
  url="${vendor#*:}"
  rpc_call "$url" '{"jsonrpc":"2.0","id":1,"method":"bor_getCurrentValidators","params":[]}' 3 "[${name}] bor_getCurrentValidators"
  rpc_call "$url" '{"jsonrpc":"2.0","id":1,"method":"bor_getCurrentProposer","params":[]}' 3 "[${name}] bor_getCurrentProposer"
  # eth_getRootHash 需要起止块 hex
  rpc_call "$url" '{"jsonrpc":"2.0","id":1,"method":"eth_getRootHash","params":[1000,1255]}' 2 "[${name}] eth_getRootHash"
done

# Heimdall REST (与 Bor RPC 不同, 是 cosmos-sdk REST)
log ""
log "Heimdall public API:"
for path in "/clerk/event-record/list" "/staking/validator-set" "/checkpoints/latest"; do
  url="https://heimdall-api.polygon.technology${path}"
  f="${RAW_DIR}/heimdall_$(echo $path | tr / _).json"
  out=$(curl -sS -A "$UA" -o "$f" -w '%{http_code} %{time_total} %{size_download}' --max-time 15 "$url" 2>/dev/null)
  code=$(echo "$out" | awk '{print $1}')
  t=$(echo "$out" | awk '{print $2}')
  s=$(echo "$out" | awk '{print $3}')
  log "  GET ${url}  http=${code}  t_s=${t}  size=${s}B"
  sleep 0.3
done

log ""
log "DONE."
echo ""
echo "Report: $REPORT"
echo "Raw:    $RAW_DIR"
