# FLB 累积回测 (一次性预研, 2026-06-13, 跑完归档)

Phase 3 门: FLB 一盘一击 $25 → 深度累积到 $25 是否「不劣化」。
数据: PM data-api /trades (成交印记) + 我们 166 settled (CLOB /markets/<cid> tokens[].winner 权威 outcome)。
跑法: 服务器 (PM 访问快) `python3 flb_accum_bt.py quotes.jsonl.settlements.jsonl`。
结论: 薄簿占 69% (一次性 FOK 整单杀=错过) / 漂移≈0 / 入场捕获 2.25× → 累积放行。
⚠ 绝对胜率虚高 100% = 采样偏差, 不采信; edge 锚 paper 71%/+3.3%u。
生产实现: src/stcpp/engine/trading_loop.cpp ProcessFlbTrigger (C++)。本脚本仅离线验证。
