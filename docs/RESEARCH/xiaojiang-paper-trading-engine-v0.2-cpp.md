# Paper Trading 引擎 v0.2 (C++ 全栈版)

- Owner: 小蒋 (quant-backtest)
- Date: 2026-05-28
- Supersedes: `xiaojiang-paper-trading-engine-v0.1.md` (v0.1 保留作 audit trail, 不删)
- 验收人: 老雷 (GM) + 老周 (cpp-chief-architect) + 老韩 (risk-engineer) + 老沈 (security) + 老孙 (signer) + 小梁 (financial-expert) + 老高 (code-conventions)
- 关联:
  - 姊妹文档: `xiaojiang-backtest-framework-v0.2-cpp.md` (本人, backtest v0.2)
  - `xiaocheng-signal-catalog-v1.md` (小程 12 信号)
  - `xiaoxiao-kelly-slippage-model-v1.md` (小肖 Kelly + slippage v1, C++)
  - `laohan-riskmanager-design-v0.2.md` (老韩 RM v0.2, paper 必跑 RM)
  - `laozhou-architecture-v0.2.md` (老周架构 v0.2 §14 SAFE_MODE; paper signer 是唯一替身)
  - `laosun-key-management-v3.md` (老孙 signer v3 C++ + SecureBuffer)
  - `xiaoyuan-microstructure-v1.md` (小袁 Wave 6; paper fill 模拟依赖)
  - `xiaosong-test-replay-framework-v0.1.md` (小宋 replay 协议)
  - `laogao-code-conventions-v1.md` (老高 R-11: paper / live / backtest 单 binary + flag)
- Wave: 6
- 用户高优指令 (2026-05-28): "支持虚拟盘测试, 开始不投入真实资金. 虚拟盘稳定盈利后才跑实盘."
- 状态: v0.2 RFC, 待会签 (替代 v0.1)

---

## 0. v0.1 → v0.2 决策记录 (老雷 GM Sign-off, 2026-05-28)

### 0.1 用户原话 cite

> "我们离不开 rust 和 python 吗, 我不太希望 rust 作为项目生产环境的一部分."
> — 用户, 2026-05-28

### 0.2 GM 决策

GM 老雷 2026-05-28 终极对齐:

**paper engine + backtest 全 C++. 不允许 pyo3 binding 进生产.**

- paper / live / backtest 三 mode **全 C++ 同 binary, 不同 flag** (与老高 R-11 + 老周 §14 + GM Sign-off 一致)
- PaperSigner C++ 实现 (与老孙 v3 C++ signer 同语言, 共享 SecureBuffer)
- 虚拟成交模型 (VirtualMatcher) C++ (复用小肖 Kelly slippage C++ 实现)
- 仅 M4.5 gate 脚本 (`tools/m4_5_gate/run_gate_check.py`) 保留 Python (tool-level, cron job, 非 production hot path)

### 0.3 v0.1 vs v0.2 差异表

| 维度 | v0.1 | v0.2 | 影响 |
|---|---|---|---|
| paper 主进程 | C++ (本已是) | **C++ (不变, 但去掉所有 pyo3 想法)** | 无 |
| backtest 集成方式 | Python 调 C++ via pyo3 | **C++ 直接函数调用 (in-process)** | 大幅简化 (姊妹文档 v0.2) |
| PaperSigner | C++ (本已是) | **C++ (共享老孙 v3 SecureBuffer + signer infra)** | 加强复用 |
| VirtualMatcher | C++ (本已是) | **C++ (复用小肖 SlippageModel + 小袁 microstructure C++)** | 不变 |
| PaperLedger WAL | C++ (本已是) | **C++ (复用老王 WAL framework v0.1)** | 加强复用 |
| signer backend 切换 | CMake 编译期 (3 binary) | **CMake 编译期 (3 binary) + 老高 R-11 单 binary multi-mode 评估** | 见 §3.2 |
| M4.5 gate 脚本 | Python | **Python (保留, tool-level only)** | 不变 |
| backtest baseline 自动比对 | Python script (daily cron) | **stcpp_backtest C++ CLI + Python gate 协同** | gate 仅读 metrics.json, 不调 C++ lib |
| 总工作量 | 既定 | **减 ~6 周** (无 Python wrapper 维护) | paper 提前 |
| paper D1 | 2026-08-15 | **2026-08-29** (整体 +2 周, 因 backtest +2 周) | 见姊妹文档延期分析 |

### 0.4 v0.2 收益清算

**收益:**
1. **PR-1 红线天然落地** — paper / live 二者是同 binary 不同 mode flag, 与 backtest 也共享 95%+ 代码
2. **PaperSigner 与 RealSigner 共享 SecureBuffer** — 老孙 v3 的 hardened crypto 缓冲区, paper 也走同款内存安全 (即便不签真 tx, 模拟过程也别留私钥残留风险路径)
3. **VirtualMatcher 与 backtest BacktestSigner 共享 SlippageModel** — 小肖一份代码, 三处复用 (backtest / paper / live 的 slippage 预算)
4. **省 ~6 周 Python wrapper 维护** — 不再为 paper 写 pyo3 接口, 不再为每个 C++ 接口变更同步 Python binding
5. **生产部署纯净** — 部署机不装 Python runtime, paper / live 同 systemd unit 同 binary

**成本:**
1. **整体延期 ~2 周** (因 backtest 延期 2 周拖累, 详见姊妹文档 §11)
2. **CMake 三 binary target 维护** (live / paper / backtest 各一个 main), 但这是 PR-7 红线必须

### 0.5 还保留 Python 的地方

| 用途 | 文件 / 模块 | 性质 | 红线 |
|---|---|---|---|
| M4.5 gate 判定 | `tools/m4_5_gate/run_gate_check.py` | cron job, tool-level | 仅读 audit log + metrics.json + paper run dir 产 verdict JSON; 不调任何 C++ 内部 lib; **不在 hot path** |
| dashboard 数据预聚 | `tools/m4_5_gate/agg_for_dashboard.py` | cron, tool-level | 仅读 parquet 出 JSON 给小郑 D1 dashboard |
| ML 训练 | `ml/training/*.py` (小邓 owner) | 离线一次性 | 导 ONNX, runtime C++ 推理 |
| Notebook 探索 | `notebooks/*.ipynb` | 一次性 | 不入 main / production |

**红线 (PR-8 新增):** 任何 Python 出现在:
- `src/` (production source)
- `runtime/` (deployed binary 同目录)
- `systemd unit ExecStart` 调用链
→ **CI 拒**.

---

## 1. 设计目标 + 红线 (v0.2 加固版)

### 1.1 目标 (v0.1 + 全 C++ 落地)

| # | 目标 | 度量 | v0.1 vs v0.2 |
|---|---|---|---|
| P1 | paper trading 用同一份生产代码 (BR-1) | binary 完全一致 (md5sum 相同), 仅启动 flag `--mode=paper` 差异 | **v0.2 真正落地**, 因为 backtest 也 C++ 了, 三 mode 共代码 |
| P2 | paper 与 live 行为不可区分 (除 fill backend) | RM / 信号 / feature pipeline / order intent / audit log 字段全同 | 不变 |
| P3 | paper fill 模拟尽量贴近真实 | predicted vs actual fill slippage RMSE < 30bps | 不变 |
| P4 | paper PnL 可与 backtest baseline 自动对比 | 同一份 C++ 报告模板 (姊妹文档 §7.1) | v0.1 Python report → v0.2 C++ report |
| P5 | M4.5 gate 自动判定脚本 | Python 脚本, 读两周数据 → 出 PASS/FAIL + 理由 | 不变 (但明示 tool-level) |
| P6 | 在线率 > 99.5% (M4.5 门禁) | 同实盘观测口径 | 不变 |
| P7 | paper mode 任何时候可立即切回 backtest 或 live | 启动 flag 切换, 无需重 build | 不变 |
| **P8 (新)** | **零 Python 依赖于 paper / live binary** | `ldd stcpp_trader_paper \| grep -v python` 必空 | v0.2 新增 |
| **P9 (新)** | **PaperSigner 复用老孙 v3 SecureBuffer + signer infra** | C++ 代码共享 ≥ 70%, 仅 backend swap | v0.2 新增 |

### 1.2 红线 (不允许妥协, GM Wave 6 + 老雷 2026-05-28 加固)

| # | 红线 | 来源 |
|---|---|---|
| PR-1 | **paper 不是另一套代码, 是同一份 C++ binary 的 mode flag (或 CMake target 隔离)** | GM Wave 6 + D-04 + 用户高优 + 老高 R-11 |
| PR-2 | **paper 必跑 RiskManager 同一份 (含同款 STALE / cap / Kelly 阈值)** | 老韩 RM v0.2 G1 + GM W-2 |
| PR-3 | **paper 数据源是真实生产数据源** (实时 Goalserve + 实时 Polymarket WSS) | 用户高优 |
| PR-4 | **paper signer 替身在编译期决定**, 不是运行期切 | 老沈 TB-B 红线 |
| PR-5 | **paper 报告与 live 报告同模板 (C++ 生成)** | 老雷 + 用户高优 |
| PR-6 | **不达 M4.5 gate 不切实盘** | GM Wave 6 + 用户高优 |
| PR-7 | **PaperSigner 严禁存在于 live binary** (CMake / 链接器隔离 + CI nm 校验) | 老沈 + 老周 §2.4 三层防御 |
| **PR-8 (新)** | **零 Python 在 paper / live binary 部署路径** | GM 2026-05-28 |
| **PR-9 (新)** | **PaperSigner 共享老孙 v3 SecureBuffer (即使不签真 tx)** | 老沈 + 老孙 (security-in-depth) |

### 1.3 不在本文档范围

- backtest framework → 姊妹文档 v0.2
- 信号实现 → 小程
- RM 内部 → 老韩
- signer 实现 → 老孙 (live 真 signer v3)
- 真实链上交互 → 老叶 (RPC) / 老李 (Polymarket CLOB)
- 微观结构模拟参数标定 → 小袁 Wave 6 (C++ lib)
- ML 训练 → 小邓 (Python sklearn / LightGBM, 导 ONNX)

---

## 2. 与 backtest / live 的差异矩阵 (v0.2 收紧)

### 2.1 三向对比表 (v0.2)

| 维度 | backtest (v0.2) | paper (v0.2) | live |
|---|---|---|---|
| 主进程语言 | **C++ (v0.2 改)** | C++ | C++ |
| 启动 flag | `--mode=backtest` | `--mode=paper` | `--mode=live` |
| 时间轴 | 历史时间 (virtual clock) | 实时 wall clock | 实时 wall clock |
| 数据来源 | Parquet 历史 (小余 / 小段) | Goalserve / Polymarket WSS 真实流 | 同 paper |
| feature pipeline | **同一份 libstcpp_features.a (C++ link)** | 同 | 同 |
| 信号 | 同一份 contract (小程) | 同 | 同 |
| RiskManager | **同一份 libstcpp_risk.a (C++ link)**, backtest_config.toml | 同一份, paper_config.toml | 同一份, prod_config.toml |
| signer 入口 | C++ 函数调用 BacktestSigner (in-process) | C++ IPC 到 stcpp_signer_paper 子进程 | C++ IPC 到 stcpp_signer_live 子进程 |
| signer 后端 | 直接产虚拟 fill (无 IPC) | 子进程内 VirtualMatcher (有 IPC) | 子进程内真 EIP-712 + RPC (老孙 v3) |
| signer SecureBuffer | n/a (in-process, 无私钥) | **共享老孙 v3 SecureBuffer** (PR-9) | 同 paper, 真私钥 |
| fill 来源 | C++ SlippageModel 按 size + book 模拟 | C++ VirtualMatcher + 实时 book + Bernoulli | Polymarket 真实成交回报 |
| PnL | paper PnL (历史 settle) | paper PnL (实时 settle) | 真实 PnL |
| audit log | 写 backtest run dir | 写 prod-style audit WAL (老王 lib) | 同 paper |
| 监控 | 离线 | Prometheus / Grafana / 报警 (小郑 D1) | 同 paper |
| in-flight 失败处理 | 跑批不影响 | SAFE_MODE / HALTED 同 live | 同 paper |
| signer 子进程 crash | 不存在 | trader SAFE_MODE + 重连 | 同 paper |
| 在线率要求 | n/a | > 99.5% (M4.5 门禁) | > 99.9% |
| ML 推理 (P0-02 score_model) | **C++ ONNX Runtime / Treelite** (in-process, 同 binary) | 同 | 同 |

### 2.2 "完全相同" 的部分 (v0.2 扩展)

PR-1 红线落地 (v0.2 比 v0.1 更彻底, 因为 backtest 也是 C++):

```
src/
├── infra/                    # 全部三 mode 共享 (v0.1 已是)
├── data/                     # 全部 (含 ingest / book / feature / heartbeat / clock)
├── strategy/                 # 全部 (含信号 / 定价 / 对冲)
├── risk/                     # 全部 (RiskManager + RiskGateway)
├── exec/
│   ├── state_machine/        # 全部
│   ├── nonce/                # 全部
│   ├── fill/                 # 全部 (fill ingestion)
│   └── recon/                # 全部
├── ml/                       # **v0.2 新增**: ONNX Runtime / Treelite C++ 推理
└── ledger/                   # 抽象 + paper provider (backtest 复用 paper provider)
```

### 2.3 "刻意不同" 的部分 (v0.2 重排, ≤ 3 处, ≤ 200 行代码差异)

| # | 位置 | 差异 | 实现 |
|---|---|---|---|
| D-1 | signer 子进程的 backend | live = RealSigner / paper = PaperSigner / backtest = (in-process) BacktestSigner | CMake 编译期分支, 3 个 signer binary + 1 backtest in-process |
| D-2 | RM 配置文件 | paper_config 与 prod_config 极小差异: ledger provider + bankroll | TOML 文件差异, RM 代码不变 |
| D-3 | ledger 真值源 | live = LiveLedgerProvider (链上) / paper = PaperLedgerProvider / backtest = BacktestLedgerProvider (in-mem) | LedgerProvider C++ 抽象 (§5) |

### 2.4 "完全不能跨越" 的红线 (CMake / 链接器隔离, PR-7 加固)

- **PaperSigner / BacktestSigner / RealSigner 互不入对方 binary**:
  - 三个 CMake target: `stcpp_signer_live` / `stcpp_signer_paper` / 不需要 backtest signer 单独 binary (in-process)
  - link 阶段, `RealSigner` 只链入 live target; PaperSigner 只链入 paper; BacktestSigner 只链入 backtest target
  - CI 静态扫:
    ```
    nm stcpp_signer_live | grep -E "Paper|Backtest" && exit 1
    nm stcpp_signer_paper | grep -E "RealSigner|chain_rpc_submit|eip712_real" && exit 1
    nm stcpp_trader_backtest | grep -E "RealSigner|PaperSigner|chain_rpc" && exit 1
    ```
  - 任何 PR 让符号泄漏 → CI 拒
- **共享 .a 反过来 OK**: `libstcpp_features.a` / `libstcpp_risk.a` / `libstcpp_slippage.a` / `libstcpp_ml_inference.a` 三个 target 共享 (BR-1 落地)
- **共享 SecureBuffer (PR-9)**: 老孙 v3 的 `libstcpp_securebuffer.a` 链入所有 signer target, 即便 paper 不签真 tx, 内存 hygiene 一致

---

## 3. 架构 (v0.2 paper mode 是 C++ binary 的 flag)

### 3.1 模块图 (v0.2)

```
                          +-------------+
                          | Goalserve   |  (生产数据源, 真实实时)
                          | inplay/live |
                          +------+------+
                                 |
                                 ▼
+-------------+          +-------------+         +-------------+
| Polymarket  |          |   L2 DATA   |         | Polymarket  |
| gamma/clob  | ───────► |  C++ ingest | ◄────── | WSS market/ |
| REST snap   |          | book builder|         |   user      |
+-------------+          +------+------+         +-------------+
                                |
                                ▼
                          +-------------+
                          | L3 STRATEGY |  (C++, 小程信号, 同一份)
                          | + ML 推理   |  (C++ ONNX Runtime for P0-02)
                          +------+------+
                                 |
                                 ▼ OrderIntent (paper 与 live 字段全同)
                          +-------------+
                          | L4 RISK     |  (C++ RM, 同一份 libstcpp_risk.a)
                          | RiskGateway |
                          +------+------+
                                 |
                                 ▼ RiskDecision (含 audit_id)
                          +-------------+
                          | L5 EXEC     |  (C++ state machine, 同一份)
                          | order SM    |
                          +------+------+
                                 |
                       audit_id + intent
                                 |
                                 ▼ (IPC 经过 TB-B 边界, C++ IPC 协议)
                  +----------------------------+
                  | signer 子进程 (mode 差异)  |
                  |  paper: stcpp_signer_paper |  (C++)
                  |  live:  stcpp_signer_live  |  (C++)
                  +----------+-----------------+
                             |
                             ▼ (paper) 模拟 fill              live: 链上 tx + 回执
                  +----------------------------+
                  |  paper: VirtualMatcher     |  (C++, 复用小肖 SlippageModel)
                  |   (§4 微观结构)            |
                  +----------+-----------------+
                             |
                             ▼ (paper) Fill (虚拟)
                  +----------------------------+
                  |  L5 fill ingestion (C++, 同一份) |
                  |  → ledger update           |
                  +----------------------------+
                             |
                             ▼
                  +----------------------------+
                  |  L1 metrics / audit / log  |
                  |  (C++, 与 live 同一份)      |
                  +----------------------------+
                             |
                             ▼ (异步, cron)
                  +----------------------------+
                  | tools/m4_5_gate/           |  ← **唯一 Python**
                  |   run_gate_check.py        |  (tool-level, 读 audit log + metrics.json)
                  |   出 verdict JSON          |
                  +----------------------------+
```

### 3.2 signer 子进程 backend 切换实现 (v0.2 复审)

**v0.1 评估了方案 A (编译期, 3 binary) vs 方案 B (启动期注入, 1 binary)**, 推荐了 A.

**v0.2 重新评估** (因 GM "全 C++ 单 binary multi-mode" + 老高 R-11):

| 方案 | 优势 | 劣势 | v0.2 决议 |
|---|---|---|---|
| **A (编译期 3 binary)** | PR-7 红线最强 (符号物理隔离); nm 一扫便知; 老沈最爱 | 部署 3 个 binary, systemd unit / packaging 略繁琐 | **推荐 (与 v0.1 一致)** |
| **B (启动期注入, 1 binary)** | 部署单一 binary; 老高 R-11 "单 binary multi-mode" 字面契合 | live binary 中包含 PaperSigner 代码 → 攻击面增大; 老沈不喜欢; nm 隔离失效 | **拒 (PR-7 红线优先)** |

**最终决议:** 走方案 A. 老高 R-11 "单 binary multi-mode" 仅适用于 **trader 主进程** (live / paper / backtest 三 mode 用同一 `stcpp_trader` binary + `--mode` flag), 但 **signer 子进程**走 CMake target 隔离 (live / paper 两个独立 signer binary). backtest 不 fork signer, 用 in-process BacktestSigner.

老沈 + 老周 + 老高 三方 6/12 会签.

```cmake
# CMakeLists.txt 节选 (v0.2)
# trader 主进程: 单 binary, 三 mode 通过 flag
add_executable(stcpp_trader src/trader/trader_main.cc)
target_link_libraries(stcpp_trader PRIVATE
    stcpp_infra stcpp_data stcpp_strategy stcpp_risk stcpp_exec
    stcpp_features stcpp_slippage stcpp_ml_inference
    stcpp_backtest_signer  # backtest mode 用 (in-process)
)

# signer 子进程: live / paper 二个独立 binary
add_executable(stcpp_signer_live src/exec/signer/signer_main.cc)
target_link_libraries(stcpp_signer_live PRIVATE
    stcpp_real_signer stcpp_securebuffer stcpp_infra stcpp_crypto)

add_executable(stcpp_signer_paper src/exec/signer/signer_main.cc)
target_link_libraries(stcpp_signer_paper PRIVATE
    stcpp_paper_signer stcpp_securebuffer  # PR-9, 共享 SecureBuffer
    stcpp_virtual_matcher stcpp_slippage stcpp_infra)

# CI 校验
add_test(NAME signer_isolation
         COMMAND bash -c "
           nm stcpp_signer_live | grep -E 'paper_signer|virtual_matcher' && exit 1
           nm stcpp_signer_paper | grep -E 'chain_rpc_submit|eip712_real' && exit 1
           nm stcpp_trader | grep -E 'RealSigner|PaperSigner_full' && exit 1
           exit 0")
```

trader 主进程通过启动 flag 决定 fork 哪个 signer:
- `--mode=live` → fork stcpp_signer_live
- `--mode=paper` → fork stcpp_signer_paper
- `--mode=backtest` → 不 fork, in-process BacktestSigner (`stcpp_backtest_signer` 库)

### 3.3 IPC 协议 (paper 与 live 完全一致, v0.1 不变)

(沿用 v0.1 §3.3, IPC 协议为 C++ struct, 用老周 v0.2 §11 定义的 framed binary).

### 3.4 fill 回报路径 (v0.2 不变)

(沿用 v0.1 §3.4)

---

## 4. 虚拟成交模拟 (C++ 实现, v0.2)

### 4.1 总体思路 (与小袁会签)

(与 v0.1 §4.1 一致, 实现语言换 C++)

### 4.2 TAKER fill 模拟 (C++ 实现, 复用小肖 SlippageModel)

```cpp
// src/exec/signer/paper_signer/virtual_matcher.cc
Fill VirtualMatcher::simulate_taker_fill(
    const SignRequest& intent,
    const BookSnapshot& current_book,
    const slippage::SlippageEstimate& slip) {
    
    // 1. 校验 quote_price 与 current_book 一致性 (容差: < 200ms)
    if (std::abs(intent.limit_price - current_book.best_ask) > 2.0 * intent.tick_size) {
        return Fill{intent.audit_id, FillStatus::UNFILLED, "QUOTE_STALE"};
    }
    
    // 2. 按 slippage model 已算的 expected_fill_price + fill_rate
    bool actually_filled = std::generate_canonical<double, 32>(rng_) < slip.expected_fill_rate;
    if (!actually_filled) {
        return Fill{intent.audit_id, FillStatus::UNFILLED, "WITHDRAWN_OR_RACE_LOST"};
    }
    
    // 3. fill price 用 expected_fill_price (vwap)
    // 4. fill latency: E2E 实测分布 (老姜 latency-budget §11) 加 noise
    int64_t e2e_lat_ns = latency_sampler_.sample();
    
    return Fill{
        .audit_id = intent.audit_id,
        .status = FillStatus::FILLED,
        .fill_price = slip.expected_fill_price,
        .fill_size = intent.size_usdc * slip.expected_fill_rate,
        .fill_ts_ns = clock_.now_ns() + e2e_lat_ns,
    };
}
```

**关键点 (v0.2 重申):**
- 复用 C++ `libstcpp_slippage.a` (小肖 v1), 不再造一份 (BR-5 红线)
- 实时 book (从 L2 DATA 拉 C++ struct, 无序列化开销)
- E2E latency 抽样 (与老姜 latency-budget C++ 实现联动)

### 4.3 maker fill 模拟 (待小袁 microstructure C++)

```cpp
// src/exec/signer/paper_signer/virtual_matcher.cc
Fill VirtualMatcher::simulate_maker_fill(
    const SignRequest& intent,
    const BookSnapshot& current_book) {
    
    auto p_fill = micro_model_.fill_probability(
        intent.limit_price, current_book, /* time_window_s = */ 60);
    
    if (std::generate_canonical<double, 32>(rng_) < p_fill) {
        auto fill_time = micro_model_.sample_fill_time(/* ... */);
        return Fill{
            .audit_id = intent.audit_id,
            .status = FillStatus::FILLED,
            .fill_price = intent.limit_price,  // maker 拿 limit price (无 slippage)
            .fill_ts_ns = clock_.now_ns() + fill_time * 1'000'000'000LL,
        };
    } else {
        // 60s 后转 taker (小程 §3.1 fallback)
        return simulate_taker_fill(/* intent_now_taker */, current_book, /* slip */);
    }
}
```

**对小袁的需求 (@小袁 Wave 6 C++ 输出):**
- `libstcpp_microstructure.a` 提供 `FillProbabilityModel` (C++ class)
- 接口: `double fill_probability(double limit, const BookSnapshot& book, int window_s)`
- 标定数据: Polymarket 历史 trade tape (小余) + book snapshots → C++ 反推 maker fill rate

**v0.2 paper 启动时占位:** 小袁模型未到位前用极简版:
```cpp
double placeholder_fill_prob(double limit, double mid, double spread) {
    return std::max(0.0, 1.0 - 5.0 * std::abs(limit - mid) / spread);
}
```

### 4.4 取消单 + 撤单模拟 (v0.1 不变)

(沿用 v0.1 §4.4)

### 4.5 部分成交 (v0.1 不变)

(沿用 v0.1 §4.5)

---

## 5. RiskManager 在 paper 模式 (PR-2 落地, v0.2)

### 5.1 paper_config.toml vs prod_config.toml (v0.1 不变)

(沿用 v0.1 §5.1)

**v0.2 新增检查 (PR-2 落实):**
- 启动期 self-check (C++): paper_config 加载时 hash 红线字段, 与 prod_config 比对, 不一致 = abort
- `config_redline_hasher` 是 C++ 独立工具, CI 跑 (`stcpp_check_redline paper_config.toml prod_config.toml`)

### 5.2 LedgerProvider 抽象 (v0.2 C++)

(沿用 v0.1 §5.2, C++ 类层次清晰):

```cpp
// include/stcpp/ledger/provider.h
class LedgerProvider {
public:
    virtual ~LedgerProvider() = default;
    virtual Positions current_positions() const = 0;
    virtual double bankroll_usdc() const = 0;
    virtual int64_t last_recon_sync_ts_ns() const = 0;
    virtual void apply_fill(const Fill& f) = 0;
};

class LiveLedgerProvider : public LedgerProvider {
    // 接 polygon RPC + polymarket data-api, 同步链上余额
};

class PaperLedgerProvider : public LedgerProvider {
    // 内置虚拟 ledger, apply_fill 直接更新内存 + 持久化到 paper_ledger.wal (老王 WAL framework v0.1)
    // last_recon_sync_ts_ns 总是 now() (永远 fresh, 因为是内置)
};

class BacktestLedgerProvider : public LedgerProvider {
    // 纯内存, 历史 settle 事件驱动
    // (backtest 复用 paper 的 ledger 逻辑, 仅 WAL 不持久化)
};
```

**RM 不感知 paper vs live**: 通过 DI 注入 provider 指针 (`std::unique_ptr<LedgerProvider>`), RM 调用 virtual function.

### 5.3 paper 是否触发 stale halt? (PR-3 落实, v0.1 不变)

**是.** (沿用 v0.1 §5.3)

### 5.4 paper 中的 SAFE_MODE / 崩溃恢复 (v0.2 强化)

(沿用 v0.1 §5.4, 但 WAL 实现明确为 C++):
- `paper_ledger.wal` 用老王 v0.1 WAL framework (C++)
- fsync + CRC32C, 与 nonce WAL 同款保证
- 崩溃恢复 = C++ WAL replay → 重建虚拟 positions + bankroll

---

## 6. 数据流详解 (v0.1 不变, 沿用)

(完全沿用 v0.1 §6, 时序图 + 真假表 + settle 时机均不变. v0.2 唯一差异: 所有"Python script" → "C++ CLI" 或 "tool-level Python")

### 6.4 与 backtest baseline 联动 (v0.2 改)

paper 跑 2 周, 同时拿当周历史数据跑 backtest (C++ CLI):

```bash
# 每日自动化 (cron, root@trader-prod)
# 跑 backtest (C++ CLI, in 3min vs v0.1 Python 15min)
stcpp_backtest run \
  --config configs/signals/p0_01_pinnacle_novig.yaml \
  --is-start "$(date -d '7 days ago')" \
  --is-end "$(date -d '1 day ago')" \
  --output runs/baseline_$(date +%Y%m%d)/

# 与 paper 对比 (C++ CLI)
stcpp_backtest compare \
  --baseline runs/baseline_$(date +%Y%m%d)/ \
  --candidate /var/log/stcpp/paper/$(date +%Y%m%d)/ \
  --output reports/paper_vs_baseline_$(date +%Y%m%d).html

# M4.5 gate 判定 (Python tool, 仅读 JSON, 出 verdict)
python tools/m4_5_gate/run_gate_check.py \
  --paper-run-dir /var/log/stcpp/paper/ \
  --baseline-backtest runs/baseline_$(date +%Y%m%d)/ \
  --output reports/m4_5_gate_$(date +%Y%m%d).json
```

**关键变化:** v0.1 中 `python -m backtest run` 现在是 `stcpp_backtest run` (C++ CLI). gate 脚本本身保留 Python.

---

## 7. M4.5 Gate 自动判定脚本 (Python, tool-level, v0.1 不变本质)

### 7.1 门禁条件 (沿用 v0.1 §7.1, GM Wave 6 + 用户高优)

| # | 条件 | 度量 | 阈值 | 拒绝行为 |
|---|---|---|---|---|
| G-A | 连续运行时长 ≥ 14 天 | `(last_ts - first_ts) > 14 * 86400` | 14d | FAIL_DURATION |
| G-B | 累计 paper PnL > 0 | `sum(daily_pnl)` | > 0 USDC | FAIL_PNL |
| G-C | 日 Sharpe > 1.0 | `mean(daily_pnl_pct) / std(daily_pnl_pct) * sqrt(252)` | > 1.0 | FAIL_SHARPE |
| G-D | 风控失效次数 = 0 | 见 v0.1 §7.2 | 0 | FAIL_RISK |
| G-E | 在线率 > 99.5% | `uptime / total` | > 0.995 | FAIL_UPTIME |
| G-F | OOS / IS Sharpe ratio > 0.6 (vs backtest baseline) | paper / baseline | > 0.6 | FAIL_OOS_DECAY |

### 7.2 "风控失效" 严格定义 (v0.1 不变, 沿用)

(沿用 v0.1 §7.2)

### 7.3 自动判定脚本 (Python, 明示 tool-level)

```bash
# 脚本路径 (tool-level Python only, not production)
tools/m4_5_gate/run_gate_check.py

# 头部注释 (强制声明, CI 校验)
"""
TOOL-LEVEL PYTHON ONLY — NOT PRODUCTION HOT PATH

This script reads paper run artifacts (audit WAL, metrics.json, parquet)
and produces a gate verdict JSON. It runs as a cron job, separate from
the production stcpp_trader binary.

DO NOT IMPORT ANY C++ EXTENSION HERE. DO NOT CALL ANY C++ LIB DIRECTLY.

Allowed deps:
- pandas / pyarrow (read parquet)
- json / pathlib (stdlib)
- numpy (basic stats)

Forbidden:
- Any pyo3 / pybind11 / cffi binding
- Any subprocess call to production binary (use file-based artifacts only)
"""
```

实现 (沿用 v0.1 §7.3 伪代码逻辑, 完整 Python 函数, 仅读 file artifacts):

```python
def run_m45_gate(args) -> GateDecision:
    paper = load_paper_run(args.paper_run_dir)  # 读 audit WAL + metrics.json
    baseline = load_backtest(args.baseline_backtest)  # 读 C++ 出的 metrics.json

    checks = {}
    # G-A duration
    duration_days = (paper.last_ts - paper.first_ts) / 86400
    checks["G-A_DURATION"] = Check(passed=duration_days >= 14, value=duration_days, ...)
    
    # G-B PnL
    total_pnl = paper.daily_pnl.sum()
    checks["G-B_PNL"] = Check(passed=total_pnl > 0, value=total_pnl, ...)
    
    # G-C Sharpe
    sharpe = paper.daily_pnl_pct.mean() / paper.daily_pnl_pct.std() * np.sqrt(252)
    checks["G-C_SHARPE"] = Check(passed=sharpe > 1.0, value=sharpe, ...)
    
    # G-D Risk failure (Python AST scan over audit log + signer log)
    risk_failures = scan_risk_failures(paper.audit_log, paper.signer_log)
    checks["G-D_RISK"] = Check(passed=len(risk_failures) == 0, ...)
    
    # G-E Uptime
    uptime_ratio = paper.uptime_seconds / paper.total_seconds
    checks["G-E_UPTIME"] = Check(passed=uptime_ratio > 0.995, ...)
    
    # G-F OOS decay (paper vs C++ backtest baseline)
    paper_sharpe = sharpe
    baseline_sharpe = baseline.sharpe_oos
    decay_ratio = paper_sharpe / baseline_sharpe if baseline_sharpe > 0 else 0
    checks["G-F_OOS_DECAY"] = Check(passed=decay_ratio > 0.6, ...)
    
    all_pass = all(c.passed for c in checks.values())
    verdict = "PASS_FOR_LIVE" if all_pass else "HOLD_OR_REWORK"
    
    return GateDecision(verdict=verdict, checks=checks, ratified_at=datetime.utcnow())
```

**输出: dashboard 显示 (小郑 D1 联动)**

gate JSON 落 `/var/lib/stcpp/m4_5_gate/latest.json`, 小郑 dashboard (Grafana) panel 直接读 → 显示 6 个 check 通过状态.

### 7.4 输出格式 (v0.1 不变)

(沿用 v0.1 §7.4 JSON 格式)

### 7.5 Gate 之后 (v0.1 不变)

(沿用 v0.1 §7.5)

---

## 8. 报告 (与 backtest 报告同模板, v0.2 C++ 生成)

### 8.1 模板复用 (v0.2)

paper 报告**逐图逐表与 backtest 报告同**(姊妹文档 v0.2 §7.1), 生成方式从 Python matplotlib → C++ vega-lite + inja.

```
report/
├── 01_summary.html            # PnL curve, paper + baseline 双线对比 (vega-lite)
├── 02_sharpe.html
├── 03_drawdown.html
├── 04_hit_rate.html
├── 05_alpha_decay.html
├── 06_slippage_diag.html      # paper predicted vs actual (实时反馈)
├── 07_clv.html
├── 08_reject_breakdown.html
├── 09_uptime_chart.html       # paper 特有
├── 10_risk_failure_log.txt    # paper 特有
├── metrics.json               # C++ nlohmann/json 输出
└── m4_5_gate_decision.json    # Python gate 脚本输出, 引用 metrics.json
```

### 8.2 paper 特有 metric (v0.1 不变)

(沿用 v0.1 §8.2, JSON schema 不变)

### 8.3 对账 dashboard (与小郑联动, v0.2)

(沿用 v0.1 §8.3, 但实时数据从 C++ binary 的 Prometheus exporter, paper baseline 从 `stcpp_backtest` C++ CLI 出的 metrics.json)

---

## 9. 时间表 (v0.2 整体 +2 周)

### 9.1 总体 (用户高优 M4.5 卡死)

| 月份 | 里程碑 | v0.1 vs v0.2 |
|---|---|---|
| **W1 (5/28 - 6/4)** (现在) | 本文 v0.2 RFC + backtest v0.2 RFC, 等会签 | v0.2 重写 |
| 6 月 (Sprint-1 末) | C++ feature lib + RM lib + slippage v1 各就位 | 不变 |
| **7/16** | **backtest framework P0-01 第一份回测报告** | v0.1 是 7/2, v0.2 延 2 周 |
| 7 月底 - 8 月 | paper trading engine 落代码 (PaperSigner + VirtualMatcher + PaperLedger), 全 C++ | 不变 |
| 8 月 | paper engine 联调 + dry-run (1 周不计 gate) | 不变 |
| **8/29 起** | **paper trading 正式 2 周连续运行** | v0.1 是 8/15, v0.2 延 2 周 |
| **9/12** | **M4.5 gate 第一次判定** | v0.1 是 8/29, v0.2 延 2 周 |
| 9 月底起 | gate PASS → live; gate FAIL → 调整后再跑 2 周 | v0.1 是 9 月, v0.2 延 ~2 周 |

### 9.2 关键里程碑 (M4.5)

| 时间 | 事件 | 负责 | v0.1 vs v0.2 |
|---|---|---|---|
| 6/12 | Pinnacle 数据 + 信号 contract 锁版 (含 cpp_expr) | 老李 / 老彭 / 小程 | DSL 换 cpp_expr |
| 6/19 | Polymarket / Goalserve Parquet schema | 小余 / 小段 | 不变 |
| 6/26 | libstcpp_features.a + libstcpp_risk.a (C++) | 老周 / 小田 / 老韩 | **v0.1 是 pyo3, v0.2 是 .a** |
| 6/26 | slippage model v1 + 单测 | 小肖 | 不变 (本就 C++) |
| **7/16** | **backtest 第一份报告 (P0-01)** | 我 | v0.1 是 7/2, v0.2 延 2 周 |
| 7/29 | PaperSigner / VirtualMatcher skeleton (C++) | 我 + 小袁 | 不变 (v0.1 也是 C++) |
| 8/14 | paper engine 联调通 + 小宋 chaos 验过 | 我 + 小宋 | v0.1 是 7/31, v0.2 延 2 周 |
| 8/22 | dry-run 1 周 (不算 gate) | 我 | v0.1 是 8/8, v0.2 延 2 周 |
| **8/29** | **paper trading 正式 D1** | 全队 | v0.1 是 8/15, v0.2 延 2 周 |
| **9/12** | **paper D14, M4.5 gate 判定** | 我 + 老雷 + 老韩 | v0.1 是 8/29, v0.2 延 2 周 |

### 9.3 风险路径 (gate 失败处置, v0.1 不变)

(沿用 v0.1 §9.3)

---

## 10. 风险点 + 开放问题 (v0.2 重排)

### 10.1 已知风险

| # | 风险 | 严重度 | 缓解 |
|---|---|---|---|
| PR-1 | VirtualMatcher 与真实 fill 偏差大 → paper Sharpe 与 live 实际差异大 | 高 | 与小袁 Wave 6 C++ 模型紧密 calibrate; live 起步首周影子并跑比对 |
| PR-2 | paper 数据源真实, 但市场冲击虚拟 (我们没真下单, 不会影响 book) | 高 | M4.5 通过后 live 起步用 PER_ORDER_CAP_SOFT × 0.5 缩水首周 |
| PR-3 | settle 等真比赛 4h → paper 单笔 trade 周期长, 14 天可能不够 trade 数 | 中 | 14 天目标 trade 数 40-60, 必要时延 21 天 |
| PR-4 | paper 跑期间生产数据源中断 → uptime 受真 infra 拖累 | 中 | M4.5 阈值 99.5% 已给 0.5% 余量 |
| PR-5 | PaperSigner 子进程 crash → paper 失真 | 中 | SAFE_MODE 同 live |
| PR-6 | paper 期间 alpha 真在 decay → 切实盘翻车 | 中 | 监控 paper vs backtest baseline 比值, ≤ 0.6 即拒 |
| PR-7 | virtual ledger WAL 损坏 → paper PnL 丢失 | 低 | 老王 v0.1 WAL framework fsync + CRC32C |
| PR-8 | M4.5 gate 自动脚本逻辑 bug → 误判 PASS | 低 | 脚本 PR 双人 review; 第一次判定前手工对账 |
| **PR-9 (新, Top 1)** | **C++ VirtualMatcher 重写期间, paper engine 联调时间不够** | **高** | W7 起 (backtest 完成后) paper engine 全力, 8/14 联调 deadline 是死线; 必要时启用 placeholder microstructure model 跑 dry-run |
| **PR-10 (新)** | **CMake 三 binary target 维护复杂度** | 中 | CI 自动化 (nm 校验 + ABI 检查), 老吴 packaging 自动化, 双人 review CMake 变更 |
| **PR-11 (新)** | **PaperLedger WAL 与老王 v0.1 framework 接口不稳定 (老王 v0.1 还在 RFC)** | 中 | 7 月 paper engine skel 时 fallback 用 raw `O_APPEND` + fsync (临时), 等老王 v0.1 稳定再切 |

### 10.2 开放问题 (v0.2 更新)

| # | 问题 | 找谁 | 截止 |
|---|---|---|---|
| PQ-1 | PaperSigner / RealSigner CMake 编译期分支已选 A (PR-7) | (已闭) | 6/12 |
| PQ-2 | VirtualMatcher maker 路径 placeholder 是否够 paper dry-run | @小袁 + 我 | 7/29 |
| PQ-3 | M4.5 G-F 阈值 0.6 是否合适, 或区分 NBA/NFL | @老雷 + @小梁 | 8/22 |
| PQ-4 | paper 期间是否允许人工干预 | @老雷 | 7/15 |
| PQ-5 | 多策略 (P0-01 + P0-02 同时跑 paper) 还是单策略 | @小梁 + @老雷 | 7/31 |
| PQ-6 | virtual bankroll 初始值 ($20K? $50K?) | @老雷 + @小梁 | 7/15 |
| PQ-7 | paper 期间 PR 是否允许 deploy | @老吴 + @老韩 | 7/15 |
| **PQ-8 (新)** | **PaperSigner SecureBuffer 共享老孙 v3 接口稳定性** | @老孙 + @老沈 | 6/26 |
| **PQ-9 (新)** | **M4.5 gate Python 脚本是否走 PyEnv vs system Python (主机部署)** | @老吴 | 7/15 |
| **PQ-10 (新)** | **ML 推理 (P0-02 score_model) C++ ONNX vs Treelite 在 paper 期间稳定性 SLA** | @小邓 + 我 | 7/29 |

### 10.3 v0.2 不做, 留 v0.3+

(沿用 v0.1 §10.3)

---

## 附录 A — 共享 / 独有 模块清单 (PR-1 v0.2 落地清晰版)

### A.1 三 mode 共享 (C++ binary 一致或共享 .a)

```
src/
├── infra/                  # 全部, 链入所有 binary
├── data/                   # 全部 (含 ingest / book / feature / heartbeat / clock)
├── strategy/               # 全部 (含信号 / 定价 / 对冲)
├── risk/                   # 全部 (RiskManager + RiskGateway)
├── exec/
│   ├── state_machine/      # 全部
│   ├── nonce/              # 全部
│   ├── fill/               # 全部 (fill ingestion)
│   └── recon/              # 全部
├── ml/                     # ONNX Runtime / Treelite 推理
└── ledger/
    └── provider.h          # 抽象接口
```

### A.2 mode-specific (CMake 编译期分流)

```
src/exec/signer/
├── signer_main.cc                # 入口 (paper / live 共享)
├── real_signer/                  # only in stcpp_signer_live target
│   ├── eip712_real.cc            (老孙 v3)
│   └── chain_rpc_submit.cc       (老叶)
├── paper_signer/                 # only in stcpp_signer_paper target
│   ├── paper_signer.cc           (PR-9: 共享 SecureBuffer)
│   └── virtual_matcher.cc        (本人, 复用小肖 SlippageModel)
└── backtest_signer/              # 链入 stcpp_trader (backtest mode in-process)
    └── backtest_signer.cc        (本人)

src/ledger/
├── ledger_provider.h             # 抽象 (共享)
├── live_ledger.cc                # only live (链入 stcpp_trader live mode)
├── paper_ledger.cc               # only paper (链入 stcpp_trader paper mode + backtest mode)
└── backtest_ledger.cc            # only backtest (in-mem, 不持久化)
```

### A.3 CI 隔离校验 (v0.2)

```bash
# CI 跑
nm stcpp_signer_live   | grep -E "paper_signer|virtual_matcher|backtest_signer" && exit 1
nm stcpp_signer_paper  | grep -E "real_signer|eip712_real|chain_rpc_submit"      && exit 1

# trader 主进程: live mode 不能有 paper / backtest 的 ledger 实现
nm stcpp_trader | grep -E "PaperLedgerProvider::|BacktestLedgerProvider::" | \
  awk '/--mode=live/' && exit 1  # (实际用 binary section + symbol table 检查)
```

### A.4 Python 完全隔离 (PR-8)

```bash
# CI: production binary 不依赖 Python
ldd stcpp_trader | grep -i python && exit 1
ldd stcpp_signer_live | grep -i python && exit 1
ldd stcpp_signer_paper | grep -i python && exit 1
ldd stcpp_backtest | grep -i python && exit 1

# CI: production source 不含 .py
find src/ -name "*.py" | head -1 | grep . && exit 1
find runtime/ -name "*.py" | head -1 | grep . && exit 1
```

---

## 附录 B — paper 启动命令 (v0.2)

```bash
# paper 模式启动 (生产路径, 与 v0.1 一致)
/usr/local/bin/stcpp_trader \
  --mode=paper \
  --rm-config=/etc/stcpp/paper_config.toml \
  --signer-binary=/usr/local/bin/stcpp_signer_paper \
  --paper-ledger-wal=/var/lib/stcpp/paper_ledger.wal \
  --audit-wal=/var/log/stcpp/audit.wal \
  --metrics-port=9090

# 同款 systemd unit, mode 字段差异
[Unit]
Description=stcpp paper trader
After=network.target

[Service]
ExecStart=/usr/local/bin/stcpp_trader --mode=paper ...
Restart=on-failure
RestartSec=5s
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

---

## 附录 C — paper 与 live 启动 flag diff (人审版, v0.1 不变)

(沿用 v0.1 附录 C)

---

## 附录 D — Python 保留清单 (再次明示)

| 文件 | 用途 | 性质 | 不进 production binary |
|---|---|---|---|
| `tools/m4_5_gate/run_gate_check.py` | M4.5 gate 判定, cron | tool-level | 仅读 file artifacts |
| `tools/m4_5_gate/agg_for_dashboard.py` | dashboard 数据预聚 | cron tool | 仅读 parquet 出 JSON |
| `tools/m4_5_gate/risk_failure_detector.py` | §7.2 风控失效扫 | tool | 仅读 audit log |
| `tools/m4_5_gate/__main__.py` | gate 总入口 | wrapper | 不调 C++ |
| `notebooks/*.ipynb` | 数据探索 | 一次性 | 不入 main |
| `ml/training/*.py` | LightGBM 训练 | 离线 | 导 ONNX |

**红线 (PR-8):** 上述以外, 任何 `*.py` 在 deployed binary path 或 systemd unit ExecStart 调用链 → CI 拒.

---

## 附录 E — Top 1 风险 (给老雷 + 老周看)

**PR-9 (新, Top 1): C++ VirtualMatcher 重写期间, paper engine 联调时间不够**

**为什么 Top 1:**
- backtest 延期 2 周 (因 C++ 重写工作量) → 7/16 才出第一份报告
- paper engine 必须 backtest 完成后开始联调 (因为依赖 backtest baseline 自动比对)
- paper D1 8/29, paper engine skeleton 7/29 → 实际只剩 ~1 个月联调窗口
- VirtualMatcher 依赖小袁 microstructure C++ lib (Wave 6 在跑, 可能晚到)
- 如果 VirtualMatcher 联调延期, paper D1 推迟, M4.5 gate 9/12 也推迟

**缓解 (已布):**
1. **8/14 联调 deadline 是死线**, 不允许再延
2. **小袁 microstructure 模型不到位时**, paper 用 placeholder fill_prob 跑 dry-run (4.3 已写), 不阻塞流程
3. **影响识别**: 周一站会 (与小程 + 小肖 + 小袁) 每周检查联调进度
4. **回退方案**: 若 PR-9 失败, M4.5 paper D1 推迟至 9/12, gate 推迟至 9/26 (再加 2 周)

**Top 2-3 (次之):**
- Top 2: PR-1 (VirtualMatcher 与真实 fill 偏差大) — 长期风险, 短期无法消除, 缓解靠 live 影子并跑首周
- Top 3: PR-10 (CMake 三 binary target 维护复杂度) — 工程风险, 缓解靠 CI 自动化 + 老吴 packaging

---

**END v0.2.** 等 PaperSigner/VirtualMatcher C++ 设计与小袁 + 老沈 + 老孙 会签, bump v0.3.

— 小蒋 (quant-backtest), 2026-05-28
