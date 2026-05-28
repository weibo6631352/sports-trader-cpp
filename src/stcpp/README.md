# `src/stcpp/` — 实现层

与 `include/stcpp/` 子目录一一对应。Header-only 模块（如 `numerical/`）此处可能没 `.cpp` 文件。

## 红线提醒

- **R-11 paper 不污染真账本**：CMake target 物理隔离 `stcpp_paper_signer` vs `stcpp_live_signer`，paper signer 不出现在 live binary（链接期 enforce）
- **R-12 WebSocket 不阻塞**：vCPU0 reactor 禁同步 REST，所有出站走 vCPU3 worker pool
- **R-20 4 时间戳**：每个跨模块数据流必须携带 4 ts 完整链路

## 入口

- `src/stcpp/bin/main.cpp` — Live mode 主进程（M5+ 启用）
- `src/stcpp/bin/paper.cpp` — Paper mode 主进程（M4 D1 启用）
- `src/stcpp/bin/backtest.cpp` — Backtest 主进程（M2+ 启用）

每个 build-time `STCPP_EXEC_MODE` 只编译一个 binary（R-7）。
