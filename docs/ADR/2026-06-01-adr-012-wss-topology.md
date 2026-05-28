# ADR-012: WSS 拓扑 — E 相位方案 (paper 1×8 不动, M4.5 后升 4-5×2)

- **ID:** ADR-012
- **Date:** 2026-06-01 (W5 末)
- **Status:** Accepted (GM 老雷拍板, Wave 27 投票)
- **投票**: A 1 / B 2 / C 2 / E 1 (极度分歧, GM 拍板 E)

## 决议

- paper 阶段 (W6 ~ M4.5): **1 conn × 8 sub 不动** (小冯 v0.1 1044 行 + 8/8 测试)
- M4.5 后到 M5+: **升 4-5 conn × 2 sub** (老李 v3 设计)
- 老韩/小梁 2×4 折中方案 paper 阶段不实施

## 实施

- W6: 小冯 1×8 维持, 不动
- M2-M4.5: 小冯 + 老李 准备 4-5 conn upgrade design (conn overhead 跨洋实测)
- M4.5 后: 实际升级

## 反对方理由 (申辩记录)

- 老周 B: "v0.6 C 无跨洋实测数据, 老李 v3 4-5×2 是生产标准"
- 小余 B: "1×8 单 loop 故障域太大"
- 老韩/小梁 C: "故障域 vs 管理复杂度 Pareto 点"
- 老胡 A: "8 ASK 未 ack 不叠加变量"

GM 答复:
- paper 阶段优先级是 SPSC 5 queue 落地 (Smell 老周 P0), 不是 WSS 拓扑
- 4-5 conn 跨洋实测数据由小冯 + 老李 M2 后准备
- 老胡 PM 视角"不叠加变量"在 paper 阶段合理

## W6 Owner

小冯 (WSS subscriber) — 维持 1×8 不动
M2 后: 小冯 + 老李 4-5 conn upgrade spec
