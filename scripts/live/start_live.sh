#!/usr/bin/env bash
# 实盘启动 (单参数切换的「那个参数」= --mode live) — owner: 老雷 | 2026-06-12
# 2026-06-12 老板「东西都在 .env 中了, 程序直接读就好了」: binary 内置 dotenv
#   (trader_server_main → LoadDotEnv), 凭证/LIVE_ARMED/STCPP_LIVE_INTENT_OK 全从仓库根
#   .env 读, 本脚本不再 source/export 任何仪式变量。私钥落 .env 的风险由限额钱包封顶。
# 前置: ① 老板明确同意开闸 (LIVE_ARMED=1 写在 .env) ② 钱包已入金
# 用法: bash scripts/live/start_live.sh
set -euo pipefail
cd "$(dirname "$0")/../.."

echo "== 实盘预检 =="
bash scripts/live/live_precheck.sh || { echo "预检未过, 拒绝启动"; exit 1; }
echo "== build (单 binary, 与 paper 共用) =="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -GNinja >/dev/null
ninja -C build trader_server
# pkill -x 精确按进程名杀 (-f 模式串会误杀含同字样的 shell 自身, 实测踩过)
pkill -x trader_server 2>/dev/null || true
sleep 2; pkill -9 -x trader_server 2>/dev/null || true
setsid ./build/src/stcpp/app/trader_server --mode live --host 0.0.0.0 --port 7080 --enable-fills \
  > /tmp/live_server.log 2>&1 < /dev/null &
sleep 4
pgrep -x trader_server >/dev/null && echo "live_server 已启动 (port 7080, 日志 /tmp/live_server.log; armed 状态看日志 ARMED 行)" \
  || { echo "启动失败 (查 /tmp/live_server.log — 缺凭证会 fail-fast)"; exit 1; }
