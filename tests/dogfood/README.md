# tests/dogfood/ — Paper Runtime Dogfood 测试框架

- **Owner:** 小宫 (dogfood-evaluator, E 产品业务保障部)
- **Last review:** 2026-05-29
- **目的:** W11 paper runtime 上线前预热 — 端到端 dogfood 检查单 + bash/curl harness

## 文件清单

| 文件 | 用途 |
|---|---|
| `README.md` | 本文件，框架说明 |
| `checklist-paper-runtime.md` | 端到端 dogfood 检查单 (6 阶段，真实使用者视角) |
| `harness/run_dogfood.sh` | 主 harness：启动 stub API + 跑全套 curl 检查 |
| `harness/probe_obs_api.sh` | 逐 endpoint 探针 (对 stub 或真实 paper runtime 均可跑) |
| `harness/stub_api_server.py` | 本地 stub HTTP server (tool-level Python，仅 dogfood 用) |
| `harness/gate_verdict.sh` | GM-PAPER-G 六维 dogfood 验证维度输出脚本 |

## 快速跑通 (本地 stub 验证)

```bash
# 步骤 1: 启动 stub server (端口 19091)
python3 tests/dogfood/harness/stub_api_server.py --port 19091 &
STUB_PID=$!

# 步骤 2: 跑全套 dogfood 探针
BASE_URL=http://localhost:19091 tests/dogfood/harness/run_dogfood.sh

# 步骤 3: 清理
kill $STUB_PID
```

## 关联文档

- `docs/RESEARCH/xiaozheng-observability-endpoints-v1.md` (观测 API 定义)
- `docs/RESEARCH/xiaojiang-paper-trading-engine-v0.2-cpp.md` (paper engine 架构)
- `docs/OKR/laolei-2026-drive-directive-paper-profit-v1.md` (GM-PAPER-G 门禁)
- `docs/RESEARCH/xiaoying-acceptance-spec-v2.md` (M1 38 条 + GM-PAPER-G 八条 AC)
