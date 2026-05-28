# `tests/` — 测试体系（小宋 v0.2 + cpp-skeleton v1）

| 子目录 | 类型 | 频率 | 时间预算 |
|---|---|---|---|
| `unit/` | gtest 单测 | 每个 PR | ≤ 8min |
| `sim/` | 模拟器（mock CLOB/WSS/RPC） | 每个 PR + post-merge | ≤ 30min |
| `replay/` | replay driver（与老王 WAL framework adapter） | nightly | ≤ 3h |
| `chaos/` | fault injection（13 类 + R-12 4 场景） | nightly + weekly | ≤ 14h |
| `bench/` | Google Benchmark perf 回归 | nightly | ≤ 30min |
| `ci_grep/` | CI grep 红线检查（R-20 PIT + 老李 HMAC bug 4） | 每个 PR | ≤ 1min |

## RM 21 reject enum 覆盖矩阵（小宋 v0.2）

每个 reject enum 必须 1 unit + 1 sim + 1 replay/chaos 三层覆盖。CI 脚本 `tools/ci/risk_enum_coverage.py` 拦截。

## 覆盖率门禁

- `risk/` + `signer/` + `nonce/`：≥ 90% line / 85% branch
- 整体：≥ 70%
- PR 改动关联文件覆盖率回退 → block

## R-12 4 测试场景（小宋 v0.2 §3）

- **S-1**：注入 REST 5s 慢响应，WebSocket event loop tick 保持 < 50us
- **S-2**：1000 strategy 同时要同一 market data，实际只发 1 个 REST（single-flight）
- **S-3**：WebSocket 断线 + 5 markets，REST 兜底只发 1 个 batched 调用
- **S-4**：vCPU0 11 connection burst 不阻塞（老周 §17.1.1）

## 老李 HMAC bug 4 永久 CI grep（小宋 v0.2 §4）

- `urlsafe_b64encode().rstrip(b"=")` → reject
- `param_type` → reject（应 `asset_type`）
- `sigType=2` → reject（应 `=1`）
- HMAC path 含 querystring → reject
