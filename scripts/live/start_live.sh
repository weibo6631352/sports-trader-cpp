#!/usr/bin/env bash
# 实盘启动 (单参数切换的「那个参数」= --mode live) — owner: 老雷 | 2026-06-12
# 2026-06-12 架构合理化: 单 binary 运行时 mode。与 paper 同一个 build/trader_server,
#   差异只在 --mode live + 凭证 + LIVE_ARMED。build-live/ 双构建已废。
# 前置: ① 老板明确同意开闸 ② bash scripts/live/live_precheck.sh 全绿 ③ 钱包已入金
# 用法: LIVE_ARMED=1 bash scripts/live/start_live.sh   (不带 LIVE_ARMED 则 disarmed 干跑)
set -euo pipefail
cd "$(dirname "$0")/../.."
echo "== 实盘预检 =="
bash scripts/live/live_precheck.sh || { echo "预检未过, 拒绝启动"; exit 1; }
echo "== build (单 binary, 与 paper 共用) =="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -GNinja >/dev/null
ninja -C build trader_server
pkill -f "trader_server --mode live" 2>/dev/null || true
export STCPP_LIVE_INTENT_OK=1
unset PAPER_MODE   # R-11: PAPER_MODE=1 与 --mode live 互斥 (main 入口硬拒)
ARMED=${LIVE_ARMED:-0}
echo "== 启动 live (LIVE_ARMED=$ARMED; gate $([ "$ARMED" = 1 ] && echo \'⚠开闸\' || echo \'disarmed 干跑\')) =="
setsid env LIVE_ARMED=$ARMED ./build/src/stcpp/app/trader_server --mode live --host 0.0.0.0 --port 7090 --enable-fills \
  > /tmp/live_server.log 2>&1 < /dev/null &
sleep 3
pgrep -f "trader_server --mode live" >/dev/null && echo "live_server 已启动 (port 7090, 日志 /tmp/live_server.log)" || { echo "启动失败 (查 /tmp/live_server.log — 缺凭证会 fail-fast)"; exit 1; }
