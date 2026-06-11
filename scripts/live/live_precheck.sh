#!/usr/bin/env bash
# 实盘开闸前预检 (只读, 零风险) — owner: 老雷 | 2026-06-12
# 用法: 在服务器仓库根目录跑 bash scripts/live/live_precheck.sh
set -u
cd "$(dirname "$0")/../.." || exit 1
source .env 2>/dev/null
ADDR=${POLYMARKET_FUNDER_ADDRESS:-}
echo "== 实盘预检 $(date -u +%FT%TZ) =="
fail=0
# 1. 凭证齐备
for v in POLYMARKET_API_KEY POLYMARKET_API_SECRET POLYMARKET_API_PASSPHRASE POLYMARKET_FUNDER_ADDRESS; do
  if [ -z "${!v:-}" ]; then echo "✗ $v 缺失"; fail=1; else echo "✓ $v"; fi
done
# 2. 钱包价值 (data-api 只读)
VAL=$(curl -s -m10 -H "User-Agent: Mozilla/5.0" "https://data-api.polymarket.com/value?user=$ADDR" | python3 -c "import json,sys;print(json.load(sys.stdin)[0].get('value',0))" 2>/dev/null)
echo "钱包价值: \$${VAL:-?} $([ "${VAL%.*}" = "0" ] && echo '✗ 未入金 (开闸前置)' || echo '✓')"
# 3. 链上遗留持仓 (开闸前须对账)
NPOS=$(curl -s -m10 -H "User-Agent: Mozilla/5.0" "https://data-api.polymarket.com/positions?user=$ADDR&limit=50" | python3 -c "import json,sys;d=json.load(sys.stdin);print(len(d) if isinstance(d,list) else 0)" 2>/dev/null)
echo "链上持仓: ${NPOS:-?} 个 $([ "${NPOS:-0}" -gt 0 ] && echo '⚠ 遗留仓需对账 (2026-06-03 试单)' )"
# 4. CLOB 可达性
curl -s -m8 -o /dev/null -w "CLOB 可达: %{http_code} (%{time_total}s)\n" "https://clob.polymarket.com/ok" 2>/dev/null
# 5. 编译模式 (live binary 必须 kCompiledMode=Live; paper binary 永不带闸)
echo "(提醒: 开闸 = 老韩 RM + 小白安全会签 → LiveOrderGate.Arm(); 默认 disarmed fail-closed)"
exit $fail
