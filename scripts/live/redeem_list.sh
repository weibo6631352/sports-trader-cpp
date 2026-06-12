#!/usr/bin/env bash
# 待赎回清单 (实盘日常工具) — owner: 老雷 | 2026-06-13
# 列出钱包上已结算可赎回的仓位 (data-api 只读)。赎回动作在 Polymarket 网页一键完成;
# 自动赎回 (链上 redeemPositions) 是后续开发项。
set -euo pipefail
cd "$(dirname "$0")/../.."
set -a; source .env; set +a
curl -s --max-time 15 "https://data-api.polymarket.com/positions?user=${POLYMARKET_FUNDER_ADDRESS}&redeemable=true&limit=100" | python3 -c "
import json,sys
ps=json.load(sys.stdin)
tot=0.0
print('=== 待赎回 (已结算赢仓) ===')
for p in ps:
    v=float(p.get('currentValue') or 0)
    tot+=v
    print(' %-40.40s  %8.2f USDC' % (p.get('title') or p.get('asset'), v))
print('合计: %.2f USDC (%d 仓)' % (tot, len(ps)))
"
