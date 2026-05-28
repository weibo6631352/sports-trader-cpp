#!/usr/bin/env bash
# laowu-bandwidth-probe.sh — Wave 15 网络诊断必备数据采集
# Owner: 老吴 (linux-sre-devops)
# 用法: bash laowu-bandwidth-probe.sh [section]
#   sections: proxy | goalserve | gzip | concurrency | all
# 凭证: 从 .env 读取, 输出中已 redact

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Load .env
if [ -f "$REPO_ROOT/.env" ]; then
    set -a; source "$REPO_ROOT/.env"; set +a
fi

PROXY="${GOALSERVE_PROXY:-http://127.0.0.1:7890}"
KEY="${GOALSERVE_API_KEY:-}"
TS="$(date '+%Y%m%dT%H%M%S')"
OUT_DIR="$SCRIPT_DIR"

# curl 通用格式: speed in bytes/s
WFMT='code=%{http_code} dl=%{size_download} ul=%{size_upload} t_conn=%{time_connect} t_app=%{time_appconnect} t_ttfb=%{time_starttransfer} t_total=%{time_total} sp_dl=%{speed_download}\n'

redact() { sed -E "s|/${KEY}/|/<REDACTED>/|g; s|${KEY}|<REDACTED>|g"; }

# ---------------------------------------------------------------
# Section 1: 代理本身吞吐 (上下行)
# 思路: 走代理拉 cachefly / cloudflare speedtest endpoint 大 payload
# 多 size + 多次 sample 算 p50/p95
# ---------------------------------------------------------------
probe_proxy_throughput() {
    local out="$OUT_DIR/laowu-bandwidth-proxy-throughput.csv"
    echo "ts,target,size_label,n,code,size_download,time_total,speed_download_bps,mbps" > "$out"

    # cloudflare speedtest (1MB / 10MB / 100MB), 上行用 /__up endpoint
    local targets=(
        "https://speed.cloudflare.com/__down?bytes=10000000|cf-10MB"
        "https://speed.cloudflare.com/__down?bytes=100000000|cf-100MB"
        "https://cachefly.cachefly.net/10mb.test|cachefly-10MB"
        "https://cachefly.cachefly.net/100mb.test|cachefly-100MB"
    )

    for entry in "${targets[@]}"; do
        local url="${entry%|*}"
        local label="${entry#*|}"
        for i in 1 2 3 4 5; do
            local line
            line=$(curl -sS -o /dev/null --max-time 90 \
                -x "$PROXY" \
                -w "$WFMT" \
                "$url" 2>&1 | tr '\n' ' ')
            local code dl t sp
            code=$(echo "$line" | sed -n 's/.*code=\([0-9]*\).*/\1/p')
            dl=$(echo "$line" | sed -n 's/.*dl=\([0-9]*\).*/\1/p')
            t=$(echo "$line" | sed -n 's/.*t_total=\([0-9.]*\).*/\1/p')
            sp=$(echo "$line" | sed -n 's/.*sp_dl=\([0-9]*\).*/\1/p')
            local mbps="0"
            [ -n "$sp" ] && mbps=$(awk -v s="$sp" 'BEGIN{printf "%.2f", s*8/1000000}')
            echo "$(date '+%FT%T'),$url,$label,$i,$code,$dl,$t,$sp,$mbps" >> "$out"
            echo "  [$label #$i] code=$code dl=$dl t=${t}s mbps=$mbps"
        done
    done
    echo "[proxy_throughput] -> $out"
}

# ---------------------------------------------------------------
# Section 2: 通过代理对 Goalserve 三域名稳态吞吐
# 三域: www.goalserve.com, inplay.goalserve.com, oddsfeed.goalserve.com
# 用最大 payload endpoint
# ---------------------------------------------------------------
probe_goalserve_throughput() {
    local out="$OUT_DIR/laowu-bandwidth-goalserve-throughput.csv"
    echo "ts,host,endpoint,mode,n,code,size_dl,time_total,speed_dl_bps,kbps,gzip" > "$out"

    # 测试矩阵: host + endpoint (尽量用大 payload), mode=proxy/direct
    # 注: inplay.goalserve.com / oddsfeed.goalserve.com 需 IP 白名单, direct 大概率失败 — 我们如实记录
    local targets=(
        "www.goalserve.com|/getfeed/${KEY}/bsktbl/nba-shedule|nba-schedule-large"
        "www.goalserve.com|/getfeed/${KEY}/hockey/nhl-shedule|nhl-schedule-large"
        "www.goalserve.com|/getfeed/${KEY}/racing/uk|racing-uk"
        "www.goalserve.com|/getfeed/${KEY}/bsktbl/inplay?json=1|bsktbl-inplay-small"
        "inplay.goalserve.com|/inplay/soccer_10?key=${KEY}|inplay-soccer"
        "oddsfeed.goalserve.com|/getfeed/${KEY}/soccernew/inplay|oddsfeed-inplay"
    )

    for entry in "${targets[@]}"; do
        local host="${entry%%|*}"
        local rest="${entry#*|}"
        local path="${rest%|*}"
        local label="${rest##*|}"
        local url="https://${host}${path}"
        for mode in proxy direct; do
            for i in 1 2 3 4 5; do
                local opts=(-sS -o /dev/null --max-time 30 --compressed -w "$WFMT")
                if [ "$mode" = "proxy" ]; then opts+=(-x "$PROXY"); fi
                local line
                line=$(curl "${opts[@]}" "$url" 2>&1 | tr '\n' ' ')
                local code dl t sp
                code=$(echo "$line" | sed -n 's/.*code=\([0-9]*\).*/\1/p')
                dl=$(echo "$line" | sed -n 's/.*dl=\([0-9]*\).*/\1/p')
                t=$(echo "$line" | sed -n 's/.*t_total=\([0-9.]*\).*/\1/p')
                sp=$(echo "$line" | sed -n 's/.*sp_dl=\([0-9]*\).*/\1/p')
                local kbps="0"
                [ -n "$sp" ] && kbps=$(awk -v s="$sp" 'BEGIN{printf "%.2f", s*8/1000}')
                echo "$(date '+%FT%T'),$host,$label,$mode,$i,$code,$dl,$t,$sp,$kbps,compressed" >> "$out"
                echo "  [$host/$label/$mode #$i] code=$code dl=$dl t=${t}s kbps=$kbps"
            done
        done
    done
    echo "[goalserve_throughput] -> $out"
}

# ---------------------------------------------------------------
# Section 3: gzip 压缩比 (raw vs --compressed)
# 同 endpoint 两次拉, 一次 --compressed 一次不开
# ---------------------------------------------------------------
probe_gzip_ratio() {
    local out="$OUT_DIR/laowu-bandwidth-gzip-ratio.csv"
    echo "ts,host,endpoint,gz_off_bytes,gz_on_bytes,ratio_pct,saved_pct" > "$out"

    local endpoints=(
        "www.goalserve.com|/getfeed/${KEY}/bsktbl/nba-shedule|nba-schedule"
        "www.goalserve.com|/getfeed/${KEY}/hockey/nhl-shedule|nhl-schedule"
        "www.goalserve.com|/getfeed/${KEY}/soccer/home|soccer-home"
        "www.goalserve.com|/getfeed/${KEY}/bsktbl/inplay?json=1|bsktbl-inplay"
        "inplay.goalserve.com|/inplay/soccer_10?key=${KEY}|inplay-soccer-stream"
    )

    for entry in "${endpoints[@]}"; do
        local host="${entry%%|*}"
        local rest="${entry#*|}"
        local path="${rest%|*}"
        local label="${rest##*|}"
        local url="https://${host}${path}"

        # gzip OFF (不发 Accept-Encoding, 服务端可能仍 chunked)
        local off
        off=$(curl -sS -o /dev/null -x "$PROXY" --max-time 30 \
              -H "Accept-Encoding: identity" \
              -w "%{size_download}" "$url" 2>&1)
        # gzip ON
        local on
        on=$(curl -sS -o /dev/null -x "$PROXY" --max-time 30 --compressed \
             -w "%{size_download}" "$url" 2>&1)
        local ratio="0" saved="0"
        if [ "${off:-0}" -gt 0 ] && [ "${on:-0}" -gt 0 ]; then
            ratio=$(awk -v a="$on" -v b="$off" 'BEGIN{printf "%.1f", (a/b)*100}')
            saved=$(awk -v a="$on" -v b="$off" 'BEGIN{printf "%.1f", (1-a/b)*100}')
        fi
        echo "$(date '+%FT%T'),$host,$label,$off,$on,$ratio,$saved" >> "$out"
        echo "  [$host/$label] off=$off on=$on ratio=${ratio}% saved=${saved}%"
    done
    echo "[gzip_ratio] -> $out"
}

# ---------------------------------------------------------------
# Section 4: 并发上限 + 限流触发点 (1/2/4/6/8/10 并发 × 20 请求)
# 端点: www.goalserve.com/bsktbl/inplay (轻量) 测 RPS limit
# ---------------------------------------------------------------
probe_concurrency() {
    local out="$OUT_DIR/laowu-bandwidth-concurrency.csv"
    echo "concurrency,total_req,success,timeout,err_other,wall_time_s,effective_rps,p50_ms,p95_ms,max_ms" > "$out"

    local url="https://www.goalserve.com/getfeed/${KEY}/bsktbl/inplay?json=1"
    local total=20

    for c in 1 2 4 6 8 10; do
        local tmpdir
        tmpdir=$(mktemp -d)
        local t0
        t0=$(awk 'BEGIN{srand(); printf "%d", systime()}')
        local start_ms
        start_ms=$(python3 -c 'import time;print(int(time.time()*1000))')

        # 并发: 用 xargs -P
        seq 1 "$total" | xargs -n1 -P"$c" -I{} bash -c '
            i="$1"; url="$2"; proxy="$3"; out="$4"
            t=$(curl -sS -o /dev/null --max-time 15 -x "$proxy" \
                -w "%{http_code} %{time_total}\n" "$url" 2>&1)
            echo "$t" > "$out/req_$i.txt"
        ' _ {} "$url" "$PROXY" "$tmpdir"

        local end_ms
        end_ms=$(python3 -c 'import time;print(int(time.time()*1000))')
        local wall_ms=$((end_ms - start_ms))
        local wall_s
        wall_s=$(awk -v w="$wall_ms" 'BEGIN{printf "%.3f", w/1000}')

        # 统计
        local succ=0 tout=0 oerr=0
        local times=()
        for f in "$tmpdir"/req_*.txt; do
            local line code t
            line=$(cat "$f")
            code=$(echo "$line" | awk '{print $1}')
            t=$(echo "$line" | awk '{print $2}')
            case "$code" in
                200) succ=$((succ+1)); times+=("$t");;
                000) tout=$((tout+1));;
                *)   oerr=$((oerr+1));;
            esac
        done

        # 排序算 p50/p95/max
        local sorted
        sorted=$(printf '%s\n' "${times[@]:-0}" | sort -n)
        local n=${#times[@]}
        local p50_ms="0" p95_ms="0" max_ms="0"
        if [ "$n" -gt 0 ]; then
            local p50_idx=$(( (n+1)/2 ))
            local p95_idx=$(( (n*95+99)/100 ))
            [ "$p95_idx" -gt "$n" ] && p95_idx=$n
            local p50_s p95_s max_s
            p50_s=$(echo "$sorted" | sed -n "${p50_idx}p")
            p95_s=$(echo "$sorted" | sed -n "${p95_idx}p")
            max_s=$(echo "$sorted" | tail -1)
            p50_ms=$(awk -v x="$p50_s" 'BEGIN{printf "%.0f", x*1000}')
            p95_ms=$(awk -v x="$p95_s" 'BEGIN{printf "%.0f", x*1000}')
            max_ms=$(awk -v x="$max_s" 'BEGIN{printf "%.0f", x*1000}')
        fi
        local rps
        rps=$(awk -v t="$total" -v w="$wall_s" 'BEGIN{if(w>0) printf "%.2f", t/w; else print "0"}')

        echo "$c,$total,$succ,$tout,$oerr,$wall_s,$rps,$p50_ms,$p95_ms,$max_ms" >> "$out"
        echo "  [concurrency=$c] succ=$succ to=$tout err=$oerr wall=${wall_s}s rps=$rps p50=${p50_ms}ms p95=${p95_ms}ms"
        rm -rf "$tmpdir"
        # 限流缓冲, 不要把账号打死
        sleep 3
    done
    echo "[concurrency] -> $out"
}

# ---------------------------------------------------------------
# main
# ---------------------------------------------------------------
SEC="${1:-all}"
case "$SEC" in
    proxy)        probe_proxy_throughput;;
    goalserve)    probe_goalserve_throughput;;
    gzip)         probe_gzip_ratio;;
    concurrency)  probe_concurrency;;
    all)
        probe_proxy_throughput
        probe_goalserve_throughput
        probe_gzip_ratio
        probe_concurrency
        ;;
    *) echo "unknown section: $SEC"; exit 1;;
esac
