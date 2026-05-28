# ADR-011: paper/live binary 策略 — A 共享 binary + C 内部 transport 分离

- **ID:** ADR-011
- **Date:** 2026-06-01 (W5 末)
- **Status:** Accepted (GM 老雷拍板, Wave 27 投票)
- **投票**: A 4 (老韩/小余/小梁/老胡) / C 2 (老周/老郭)
- **关联**: R-2 红线 (CLAUDE.md §8), 小梁 retro A-15

## 决议

**主决议: A 共享 binary** (维持 R-2 金融红线)
**实施细化: C 内部 transport 分离** (transport 层 paper/live 物理隔离, core 共享)

## 实施

- 同一 binary, CMake build-time mode switch (现状 R-7 已实施)
- 内部 RM + Signal core 共享 (统计可解释性, M4.5 G7 paired t-test)
- signer/matcher transport 层物理隔离 (CMake if-else, 已实施: polymarket/signer/execution 3 模块)
- 老周 W6 v0.7 §8 启动期 11 步 + transport 分离 spec

## 反对方理由 (申辩记录)

- 老周 C: "#ifdef PAPER_MODE 是技术债积累, transport 分离干净"
- 老郭 C: "R-2 价值在防 RM + Signal 分叉, 不在强行合并 transport"

GM 答复: 已采纳 transport 分离作为实施细化, 兼顾两方.

## W6 Owner

老周 (架构) + 老韩 (R-2 守护)
