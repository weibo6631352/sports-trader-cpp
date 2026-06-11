#!/usr/bin/env bash
# 实盘启动 (单参数切换的「那个参数」) — owner: 老雷 | 2026-06-12
# 前置: ① 老板明确同意开闸 ② bash scripts/live/live_precheck.sh 全绿 ③ 钱包已入金
# 用法: LIVE_ARMED=1 bash scripts/live/start_live.sh   (不带 LIVE_ARMED 则 disarmed 干跑)
set -euo pipefail
cd "$(dirname "$0")/../.."
echo "== 实盘预检 =="
bash scripts/live/live_precheck.sh || { echo "预检未过, 拒绝启动"; exit 1; }
echo "== live build (独立 build-live/, 不碰 paper build/) =="
cmake -S . -B build-live -DSTCPP_EXEC_MODE=live -DCMAKE_BUILD_TYPE=Release -GNinja >/dev/null
ninja -C build-live paper_server
pkill -f "build-live/.*paper_server" 2>/dev/null || true
export STCPP_LIVE_INTENT_OK=1
export PAPER_MODE=0
ARMED=${LIVE_ARMED:-0}
echo "== 启动 live (LIVE_ARMED=$ARMED; gate $([ "$ARMED" = 1 ] && echo '⚠开闸' || echo 'disarmed 干跑')) =="
setsid env LIVE_ARMED=$ARMED ./build-live/src/stcpp/app/paper_server --host 0.0.0.0 --port 7090 --enable-fills \
  > /tmp/live_server.log 2>&1 < /dev/null &
sleep 3
pgrep -f "build-live/.*paper_server" >/dev/null && echo "live_server 已启动 (port 7090, 日志 /tmp/live_server.log)" || { echo "启动失败"; exit 1; }
