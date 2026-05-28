# ADR-013: 跨洋部署 — C M4.5 前单点 us-east-1, M5+ 多点评估

- **ID:** ADR-013
- **Date:** 2026-06-01 (W5 末)
- **Status:** Accepted (GM 老雷拍板, **Wave 27 投票 6/6 全共识**)
- **投票**: C 6/6 ✓ 全共识

## 决议

- W6 ~ M4.5: **us-east-1 单点** (主节点就近 PM)
- M5+ live 后 (bankroll > $20K, 小梁 financial threshold): **评估 multi-region active/active**
- active/active 真正阻塞是 nonce 分布式协调 (老郭/老韩 一致), 非 SRE 资源

## 实施

- W6: 老吴 us-east-1 维持, 不动
- M5+ 前: 老叶 + 老韩 nonce 分布式 spec
- M5+ live 后: 老吴 multi-region 部署 (HC-10 SRE 入职后, Q4 2026)

## 反对方理由 (申辩记录)

**无** (6/6 全共识)

## W6 Owner

老吴 (SRE) — 维持 us-east-1
M5+ 前: 老叶 + 老韩 nonce 协调 spec
