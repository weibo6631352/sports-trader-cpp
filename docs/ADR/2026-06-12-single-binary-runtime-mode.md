# ADR: 单 binary 运行时 mode (废编译期 STCPP_EXEC_MODE)

date: 2026-06-12 | owner: 老雷 (GM) | 决策: 老板「全改吧, 彻底一些」| commit: db651744

## 决策

废除编译期 STCPP_EXEC_MODE 双 binary 架构, 改单 binary 运行时 `--mode paper|live` (默认 paper)。

## 动因 (老板原话「现在虚拟盘和实盘几乎是两份代码」)

- 决策引擎本来就只有一份; 分叉的是构建层 (19 编译分叉 + 12 CMake 条件块 + 2 binary)。
- 实害: 2026-06-12 当天 live 构建三连断 — 1205 测试只护 paper binary, live 盲区漂移。
- 老板原始意图「真钱和虚拟盘就一个参数切换的事」— 编译期双 binary 不是「一个参数」。

## 安全模型 (R-7/R-11 不降级)

| 旧 (编译期) | 新 (运行时) |
|---|---|
| paper 代码不进 live binary (已在 06-12 早间改为 seam) | 同 binary; 装配按 mode |
| 双 binary 选错 = 拿错文件 | --mode 显式 + 默认 paper 安全侧 |
| — | main 入口凭证四件套 fail-fast (实测缺任一秒退) |
| PAPER_MODE env | PAPER_MODE=1 与 --mode live 互斥硬拒 |
| R-7 不可中途切换 | ExecutionContext::Init 单次锁 (双调 abort) 保留 |
| R-11 mode_tag 断言 / WAL kind / 账本文件 | 全部按运行时 mode 选, 语义不变 |
| 开闸 = LIVE_ARMED=1 + LiveOrderGate fail-closed + 老板口令 | 不变 |

## 影响

- build-live/ 目录与 `-DSTCPP_EXEC_MODE` 不复存在; start_live.sh = 同一 binary `--mode live`。
- paper_runtime 保持 paper 专用入口 (固定 Init(Paper) + PAPER_MODE=1 硬校验)。
- LiveExecutorAdapter 归位 execution/ (IOrderExecutor 一接口两后端)。
- 后续命名 (PaperLoop→通用名) 评估为纯 churn, 暂不动 (老板「不增心智负担」); TickOne
  分段重构待实盘稳定后做行为逐位不变提炼。
