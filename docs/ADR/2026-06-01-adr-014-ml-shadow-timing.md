# ADR-014: ML shadow inference 时机 — C M2 (8/6) 后 shadow, M4.5 后 active

- **ID:** ADR-014
- **Date:** 2026-06-01 (W5 末)
- **Status:** Accepted (GM 老雷拍板, Wave 27 投票)
- **投票**: A 2 (小梁/老胡) / B 2 (老韩/小余) / C 2 (老周/老郭) — 三分裂

## 决议

- W6 ~ M2 (6/7-8/6): paper 数据进 paper_mldata.wal + 老彭 de-vig 校准 (ADR-008)
- M2 后 (8/6 ~ M4.5): de-vig 锁定后 (小梁 P1 触发 24h ack), shadow inference 启动
- M4.5 (2027-05) 后: 评估 ML active 进生产 (老钱 + 老韩 + 老雷 三方决议, ML-R6 解锁)

## 三分裂理由

- A 严守 M4.5 (小梁/老胡): de-vig 漂移 → noise / acceptance v1 121 条绑定
- B W6 shadow (老韩/小余): 数据越早越好, ML-R 红线一字不改
- C M2 折中 (老周/老郭): W6 没可推理模型, M2 后阶段 1 LightGBM baseline 就绪

GM 答复: 采纳 C 折中
- 小梁 P1 smell #1: "de-vig W6-M4.5 漂移 = noise" — 必须 de-vig 锁定 (M2) 后 shadow
- 老韩 B 视角: shadow 数据越早越好 — M2 比 M4.5 提前 9 个月足够
- 老胡 A 视角: ML gate 绑定 M4.5 — shadow 不影响 active gate (ML-R3 不阻塞 if-else)
- 三方主张兼顾, C 折中合理

## ML-R 红线 (一字不改)

- ML-R1: ML 不进生产 (active inference M4.5 后)
- ML-R2: 仅离线训练 (W6+ ONNX export)
- ML-R3: 不阻塞 if-else 路径 (shadow 旁路)
- ML-R4: 不替代风控 (RM 21 enum 静态)
- ML-R5: 4 ts R-20 全程透传
- **ML-R6 (新立)**: shadow → active 解锁需 老钱 + 老韩 + 老雷 三方决议

## BLAKE3 audit chain 配套 (W6 Wave 29, 老韩 Smell #2)

> **背景**: 老韩 W5 末 Smell #2 要求 BLAKE3 stub 禁进 live build,
> W5-B-02 (真 BLAKE3) + W5-B-03 (R-7 联调) 必须作为 M1-A06 acceptance gate 前置条件.

**决议 (W6 Wave 29)**:

- paper build + live build 均注入 `-DBLAKE3_REAL=1`
- `BLAKE3_STUB` 宏仅允许在测试 mock 场景下手动定义 (不由 CMake 注入)
- CI grep enforce:
  ```
  # 老高 PR v1.2 必加
  grep -r "BLAKE3_STUB" src/ include/ CMakeLists.txt → 如出现 → build FAIL
  ```
- `AuditEmitterPool` (老周 Smell #A) + BLAKE3_REAL 组合 = W5-B-02/B-03 闭环
- M1-A06 acceptance gate: `test_blake3_audit` T3 (chain verify) + T5 (build-time switch) 全通过

## W6 Owner

小邓 W6 paper 数据收集 (hook 已就位 W4)
M2 后: 小邓 + 小蒋 shadow inference 接入
老唐 W6 Wave 29: BLAKE3 真实现 + AuditEmitterPool (M1-A06 前置)
