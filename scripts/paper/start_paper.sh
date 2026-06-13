#!/usr/bin/env bash
# 虚拟盘启动 (paper mode) — owner: 老雷 | 2026-06-13
# paper = 100% 虚拟 / 不碰真钱; 2026-06-13 起 paper 默认就「开火」(enable_paper_fills 强制 true),
#   无需也无视 --enable-fills (「仅观测」对 paper 已去除 — 它天生就是安全模式, 漏 flag 静默空转坑过人)。
# binary 内置 dotenv (trader_server_main → LoadDotEnv): Goalserve/Polymarket 凭证从仓库根 .env 读。
# 用法: bash scripts/paper/start_paper.sh
set -uo pipefail
cd "$(dirname "$0")/../.."

# guard: 已在跑就别重启 (别误杀正在积累数据的盘); 要重启先手动 pkill。
if pgrep -f "trader_server --mode paper" >/dev/null; then
  echo "paper 已在跑 pid=$(pgrep -f 'trader_server --mode paper' | head -1) — 不重启。要重启先: pkill -9 -f 'trader_server --mode paper'"
  exit 0
fi

echo "== build (单 binary, 与 live 共用) =="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -GNinja >/dev/null
ninja -C build trader_server

LOG=/tmp/paper_server.log
setsid ./build/src/stcpp/app/trader_server --mode paper --port 7080 --host 0.0.0.0 \
  > "$LOG" 2>&1 < /dev/null &
sleep 4
pgrep -f "trader_server --mode paper" >/dev/null \
  && echo "paper_server 已启动 (port 7080, 日志 $LOG; paper 永远开火=虚拟成交, 不碰真钱)" \
  || { echo "启动失败 (查 $LOG — Goalserve/PM 凭证缺会报)"; exit 1; }
