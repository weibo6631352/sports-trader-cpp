# 回测框架 v0.2 (C++ 全栈版)

- Owner: 小蒋 (quant-backtest)
- Date: 2026-05-28
- Supersedes: `xiaojiang-backtest-framework-v0.1.md` (v0.1 保留作 audit trail, 不删)
- 验收人: 老雷 (GM) + 老周 (cpp-chief-architect) + 老韩 (risk-engineer) + 小梁 (financial-expert) + 老高 (code-conventions)
- 关联:
  - `xiaocheng-signal-catalog-v1.md` (小程 12 信号, P0-01 详细实验设计)
  - `xiaoxiao-kelly-slippage-model-v1.md` (小肖 Kelly + slippage v1, §6 回测验证方案)
  - `laohan-riskmanager-design-v0.2.md` (老韩 RM v0.2)
  - `laozhou-architecture-v0.2.md` (老周 §D-04 回测/实盘共用 feature pipeline)
  - `xiaosong-test-replay-framework-v0.1.md` (小宋 replay 协议 + MessagePack framed)
  - `xiaodeng-ml-roadmap-data-needs-v1.md` (小邓 ML, 训练 Python → ONNX → C++ 推理)
  - `laogao-code-conventions-v1.md` (老高 R-11 单 binary + flag)
  - 配套同 Wave 6: `xiaojiang-paper-trading-engine-v0.2-cpp.md` (本人, paper engine v0.2)
- Wave: 6
- 状态: v0.2 RFC, 待会签 (替代 v0.1)

---

## 0. v0.1 → v0.2 决策记录 (老雷 GM Sign-off, 2026-05-28)

### 0.1 用户原话 cite

> "我们离不开 rust 和 python 吗, 我不太希望 rust 作为项目生产环境的一部分."
> — 用户, 2026-05-28

### 0.2 GM 决策

GM 老雷 2026-05-28 终极对齐 (与用户口头确认):

**回测框架 + paper engine 全 C++. 不允许 pyo3 binding 进生产.**

Python 仅限于:
1. **ML 离线训练** (sklearn / LightGBM, 导 ONNX, C++ 推理)
2. **Jupyter notebook 一次性数据探索** (小程 / 小董 ad-hoc, 不入回测 run)
3. **M4.5 gate 脚本** (`tools/m4_5_gate/run_gate_check.py`, cron job, 非 production hot path, 标注 "tool-level only")

回测 + paper engine 是长跑项目主体, **必须 C++**.

### 0.3 v0.1 与 v0.2 差异表

| 维度 | v0.1 (Python + pyo3) | v0.2 (C++ 全栈) | 影响 |
|---|---|---|---|
| 数据帧 | polars (Python) | **Apache Arrow C++** (zero-copy IPC, ColumnarBatch) | 性能 +20-50%, 与生产 feature pipeline 同 Arrow 表示 |
| SQL ad-hoc | duckdb (Python) | **DuckDB C++ API** (libduckdb 链接, in-process) | 同源, ad-hoc 体验保留 |
| feature pipeline | C++ .so + pyo3 binding | **直接 C++ link** (无 binding 层) | 零 IPC 开销, BR-1 红线天然落地 |
| RiskManager | C++ .so + pyo3 binding | **直接 C++ link** (RiskGateway 是同 binary 函数调用) | 同上 |
| walk-forward / purged k-fold | Python (scipy / sklearn) | **C++ 自研 + Arrow compute** | 工作量 +2 周 |
| DSR / PBO | Python (statsmodels) | **C++ 自研 (Boost.Math 调用 Φ / Φ⁻¹)** | 工作量 +1 周 |
| 报告生成 | matplotlib + jinja2 (Python HTML) | **C++ + Apache ECharts / vega-lite JSON 模板** (HTML out via static asset) | 工作量 +1 周 |
| 阈值扫描 | Python joblib | **C++ std::execution::par + TBB** | 性能 +5-10x |
| 信号 contract | YAML + Python expr eval | **YAML + 编译期 C++ DSL** (老高 §5 conventions) | 安全性提升 |
| ML 训练 | Python (保留) | Python (保留, 导 ONNX) | 不变 |
| C++ 推理 | n/a | **ONNX Runtime C++ + Treelite (LightGBM)** | 新增 |
| Notebook 探索 | Jupyter (保留) | Jupyter (保留, 一次性) | 不变 |
| M4.5 gate | Python | Python (保留, tool-level) | 不变, 但标注 non-production |
| 总工作量 | T+5 周 (第一份 P0-01 报告 2026-07-02) | T+7 周 (第一份 P0-01 报告 **2026-07-16**) | **延期 2 周** |

### 0.4 v0.2 收益清算

**收益:**
1. **BR-1 红线天然落地** — 回测 / paper / live 三层是同一 C++ 函数调用, 不存在 binding 漂移
2. **零 IPC 开销** — feature pipeline / RM / slippage model 全是同进程调用 (v0.1 pyo3 每次 binding cross 5-20μs)
3. **paper engine 节省 ~6 周** — 不用维护 Python wrapper, 直接复用 C++ feature / RM (姊妹文档 v0.2)
4. **生产纯净** — 部署机不装 Python runtime, 攻击面 + 镜像体积 (小郭安全 + 老吴 ops 加成)
5. **数据帧统一** — Arrow C++ 是 v0.2 feature pipeline 输入输出格式, 回测无需在 polars / Arrow 之间序列化

**成本:**
1. **+4 周 C++ 重写工作量** (walk-forward + 防过拟合 + 报告), 但 paper engine 省 ~6 周, **净省 ~2 周**
2. **延期 2 周** (因学习 + 测试缓冲), 第一份 P0-01 报告 7/2 → 7/16
3. **C++ stats lib 不如 Python 富** — DSR / PBO 用 Boost.Math, 没现成 sklearn / statsmodels 一行调用, 自写 ~200 行

### 0.5 还保留 Python 的地方 (明示, 防漂移)

| 用途 | 文件 / 模块 | 性质 | 红线 |
|---|---|---|---|
| ML 训练 | `ml/training/*.py` (小邓 owner) | 离线一次性 | 必须导 ONNX, 不入运行时 |
| Notebook 探索 | `notebooks/*.ipynb` (小程 / 小董 / 我 ad-hoc) | 一次性 | 探索结论必须落 C++ 代码, notebook 不进 git main 受 CI 监管 |
| M4.5 gate | `tools/m4_5_gate/run_gate_check.py` (我 + 老韩) | tool-level cron | 不在 hot path, 不调任何 C++ 内部 lib, 仅读 audit log + parquet 报告产 JSON |
| 数据探索 SQL | `tools/duckdb_cli/*.sql` (我 + 小董) | 一次性 | 不入 production binary |

**红线:** 任何"Python 进 production hot path" PR → 自动拒 (老郭 § CI 扫 banner).

---

## 1. 设计目标 + 红线 (v0.2 加固版)

### 1.1 目标 (v0.1 + 全 C++ 加固)

| # | 目标 | 度量 | v0.1 vs v0.2 |
|---|---|---|---|
| B1 | 回测结果可复现 (确定性) | 同一 fixture + 同一 code SHA + 同一 config → 同一 PnL 序列 (浮点容差 < 1e-9) | 不变 |
| B2 | 防过拟合 | OOS Sharpe / IS Sharpe ≥ 0.6; deflated Sharpe ≥ 1.0 | 不变 |
| B3 | 与实盘共用 feature pipeline (D-04) | **直接 C++ 函数调用, 无 binding 层** | v0.1 pyo3 binding → v0.2 同进程函数 |
| B4 | 信号 ↔ 回测 解耦 | 信号通过 contract (YAML) 描述, 回测引擎不感知信号内部 | 不变 |
| B5 | 报告自动化 | `stcpp_backtest report --run-id <id>` 一键出 HTML + metrics.json | v0.1 Python CLI → v0.2 C++ CLI |
| B6 | walk-forward + purged k-fold | C++ 实现, 严格 IS/OOS 切分 | v0.1 Python → v0.2 C++ |
| B7 | 与 paper 报告同模板 | M4.5 gate 自动对账时同表 | 不变 |
| **B8 (新)** | **零 Python 依赖于 production binary** | `ldd stcpp_backtest \| grep -v python` 必空 | v0.2 新增 |
| **B9 (新)** | **Arrow C++ 是回测唯一数据帧** | 所有 in-memory dataset = `arrow::RecordBatch` | v0.2 新增 |

### 1.2 红线 (不允许妥协)

| # | 红线 | 来源 |
|---|---|---|
| BR-1 | **回测 / paper / 实盘三层共用 feature pipeline (C++ 直接 link)** | D-04 + GM Wave 6 + 老高 R-11 |
| BR-2 | **回测必跑 RiskManager** (RM 是同 C++ 函数调用, 不许 mock 规则) | 老韩 RM v0.2 G1 |
| BR-3 | **OOS 数据严禁参与任何阈值调参** (含 hyperparameter search) | 行业共识 |
| BR-4 | **任何 Python 出现在 production binary → 拒** | GM 2026-05-28 决策 + CLAUDE.md §12 |
| BR-5 | **回测 slippage 模型必须用小肖 v1 C++ 同一份** | 小肖 §6 |
| BR-6 | **数据 schema 走小段 / 小余 owner**, 我只消费 (Parquet → Arrow C++) | 班底分工 |
| BR-7 | **任何"研究专用 hack" PR → 拒** | 老周备书 |
| **BR-8 (新)** | **walk-forward / DSR / PBO / 报告生成全部 C++** | GM 2026-05-28 |
| **BR-9 (新)** | **ONNX 模型推理走 C++ ONNX Runtime, 不允许 Python 推理** | 小邓 v0.2 + GM 2026-05-28 |

### 1.3 不在本文档范围

- 信号研究本身 → 小程 / 小梁 / 老彭
- 数据 ETL / 清洗 → 小田 (data-etl) / 小余 (历史数据)
- Parquet schema 定义 → 小段 / 小余 / 老王
- Kelly + slippage 实现 → 小肖
- 实盘 / paper 引擎执行 → 姊妹文档 `xiaojiang-paper-trading-engine-v0.2-cpp.md`
- ML 训练 → 小邓 (Python sklearn / LightGBM, 导 ONNX)
- ONNX 模型导出 → 小邓 + 老高 (导出契约)

---

## 2. 技术栈 (v0.2 全 C++)

### 2.1 v0.2 选型表

```
C++ 23 (老何 cpp-version-selection v1: -std=c++23 + -fno-exceptions for hot path)
├── Apache Arrow C++ 17.0+    主数据帧 (zero-copy IPC, ColumnarBatch, dataset API)
├── DuckDB C++ API (libduckdb) ad-hoc SQL + Parquet 读 (in-process, 静态 link)
├── Boost.Math 1.84+          统计 (normal_distribution Φ / Φ⁻¹, t 分布, chi-square)
├── Eigen 3.4+                线性代数 (Sharpe / drawdown 向量计算 + 小邓某些 feature 共用)
├── Intel TBB 2021+           参数扫描并行 (std::execution::par 后端)
├── ONNX Runtime 1.18+        分类模型推理 (P0-02 score_model 等)
├── Treelite (header-only)    LightGBM / XGBoost native 推理 (比 ONNX 快 2-3x for tree models)
├── nlohmann/json 3.11+       metrics.json 输出
├── yaml-cpp 0.8+             信号 contract YAML
├── fmtlib + spdlog           log
└── Catch2 v3                 单元测试
```

**为什么 Arrow C++ 而非 polars C++ (yes, polars 有 C++ 实验性 binding):**
- Arrow C++ 是工业标准, Polymarket / Goalserve 数据落 Parquet 原生支持
- 与小田 ETL / 小余 历史数据 owner schema 零摩擦 (Parquet metadata 直读)
- polars C++ 仍是 Rust core + C wrapper, 与 GM "不允许 Rust 进生产" 红线冲突
- Arrow compute kernels (filter / aggregate / take) 单核性能与 polars 同级, multi-core 用 TBB 调度

**为什么 DuckDB C++ API 而非自研 SQL 引擎:**
- DuckDB 是 MIT 协议 C++ 库, 单文件 amalgamation 静态 link, 不引外部进程
- 直接读 Parquet (Arrow / Parquet 互通), ad-hoc 体验保留
- 我们要的是 read-only ad-hoc SQL, 不需要 OLTP / OLAP 全套
- 自研 SQL 引擎工作量 > 12 周, 不值

**为什么不用 vectorbt / backtrader (Python 现成框架):**
- 不在选型范围 (Python 禁入生产)
- 我们要的是"调 RM + 信号 contract + slippage 模型"的薄壳, 现成框架反而要切除一半再用

**为什么不全自研 (DuckDB 也撤掉):**
- 自研 Parquet reader + SQL 优化器: 工作量 +8 周, 不在 M4.5 时间预算内
- DuckDB C++ API 是 in-process, 不引入 OS 服务依赖, 已是"最小外部依赖" balance 点

### 2.2 选型对比 (vs 全自研)

| 维度 | DuckDB C++ API + Arrow C++ (v0.2 选型) | 全自研 (Arrow + 自写 SQL + 自写 Parquet reader) |
|---|---|---|
| 工作量 | T+7 周 (含 4 周 C++ 重写) | T+15+ 周 (+8 周自写 SQL/Parquet) |
| 外部依赖 | Arrow / DuckDB / Boost.Math / TBB | 仅 Arrow + Boost.Math + TBB |
| 二进制体积 | DuckDB amalgamation ~30MB | <10MB |
| 维护成本 | 跟 upstream, 偶尔需 vendor patch | 完全自有, 但 bug 自吃 |
| ad-hoc SQL | 直接 DuckDB CLI 或 C++ API | 自写, 至少 2 季实现满 SQL |
| Parquet 兼容性 | DuckDB / Arrow 同源, 与小田 / 小余 零摩擦 | 自写, 需对齐 spec, 易出 schema 解析 bug |
| 风险 | DuckDB 1.0+ 已稳定 (2024 GA) | 自研稳定需 2-3 月调试 |
| **选择** | **v0.2 推荐** | v3+ 远景, 当前不做 |

老周 (chief-architect) + 老高 (code-conventions) 会签了选型 (W1 决议).

### 2.3 模块图 (v0.2)

```
+----------------------------------------------------------------------+
|                  stcpp_backtest CLI (C++ binary)                     |
|  $ stcpp_backtest run --config configs/signals/p0_01_pinnacle.yaml   |
+----------------------------------------------------------------------+
              |
              ▼
+----------------------------------------------------------------------+
|              backtest::engine::Driver (C++)                          |
|  ┌────────────────────────────────────────────────────────────────┐ |
|  │  arrow::dataset 加载 Parquet → arrow::RecordBatch              │ |
|  │           ↓                                                      │ |
|  │  DataAccessGuard (C++ template, compile-time phase enforce)    │ |
|  │           ↓                                                      │ |
|  │  features::compute_batch(batch)  ← 同一份 C++ feature lib      │ |
|  │           ↓                                                      │ |
|  │  signal::contract::eval(batch) → OrderIntent stream             │ |
|  │           ↓                                                      │ |
|  │  risk::RiskGateway::evaluate(intent) ← 同一份 C++ RM           │ |
|  │           ↓                                                      │ |
|  │  signer::BacktestSigner::sign_and_submit(audit_id, intent)     │ |
|  │           ↓                                                      │ |
|  │  slippage::SlippageModel::estimate(...)  ← 同一份小肖 C++ lib  │ |
|  │           ↓                                                      │ |
|  │  ledger::BacktestLedger::apply_fill → PnL accrual              │ |
|  │           ↓                                                      │ |
|  │  metrics::collect (Sharpe / DD / hit / DSR / PBO ...)           │ |
|  └────────────────────────────────────────────────────────────────┘ |
|              ↓                                                        |
|     reports/run_id/report.html + metrics.json + trades.parquet       |
+----------------------------------------------------------------------+
              |
              ▼ (ad-hoc 查询)
+----------------------------------------------------------------------+
|              DuckDB C++ API (in-process, libduckdb 静态 link)        |
|  conn.Query("SELECT ... FROM 'runs/p0_01/trades.parquet' WHERE ...") |
+----------------------------------------------------------------------+
```

**关键点:**
- 所有箭头都是 **C++ 函数调用** (无 IPC, 无 binding)
- `arrow::RecordBatch` 是统一数据帧, feature pipeline / RM / slippage 全接同一类型
- DuckDB 只是 ad-hoc 工具, 不在回测 hot path 上

### 2.4 与 C++ feature pipeline 共享 (BR-1 红线天然落地)

**v0.1 痛点 (已撤):** pyo3 binding 层有 5-20μs cross 开销, 每个 backtest run 跑 50M 行可能累积 ~10min binding 开销; 同时 binding signature 漂移风险高.

**v0.2 方案 (老周 architecture v0.2 §D-04 共享 lib):**

```
+-----------------------------------+
|   libstcpp_features.a (C++)       |  ← 共享静态库
|   - compute_polymarket_mid()      |
|   - compute_book_depth_24h()      |
|   - compute_pinnacle_novig_fair() |
|   - compute_kickoff_age()         |
|   - ...                            |
+-----------+-----------------------+
            |
            |  link 进所有 binary:
            ▼
+-------------------+ +-------------------+ +-------------------+
| stcpp_trader_live | | stcpp_trader_paper| | stcpp_backtest    |
| (老周 §A.2)       | | (姊妹文档 §A.2)   | | (本文 §2.3)       |
+-------------------+ +-------------------+ +-------------------+
```

- **同一 `.a` 静态库**, 同一 ABI, 同一 ABI 签名
- CI 在每个 binary 上跑 `nm | sha256sum` 校验 feature 函数符号一致
- 任何"研究专用 feature 复刻"被 CI 拒 (BR-7 落地)

**v0.1 临时 Python 复刻 (已撤):** 不需要了. C++ feature lib 由小田 (data-etl) + 老周 在 Sprint-2 (M+2) 出, 我直接用.

### 2.5 与小宋 Replay 框架的关系 (v0.2 更紧密)

| 维度 | 小宋 replay (v0.1) | 小蒋 backtest (v0.2) |
|---|---|---|
| 主用途 | 行为复现 + 回归测试 + 事故复现 | PnL 模拟 + 参数扫描 + 信号验证 |
| 输入 | EventRecorder 录的事件流 (MessagePack framed) | Parquet 历史数据 (小余 / 小段) |
| 时间轴 | 严格事件驱动 (按 monotonic_ns) | 事件驱动 (复用小宋 C++ ReplayDriver) |
| 范围 | 全系统 (含 RM / signer / exec) | 同上, 但 signer 替身为 BacktestSigner |
| 跑频 | nightly (replay smoke + full) | on-demand + 信号变更后跑 |
| 语言 | C++ (小宋 v0.1 已是 C++) | C++ (v0.2, 与小宋同语言) |

**v0.2 改进:**
- 我直接 link 小宋的 `libstcpp_replay.a`, 复用 `ReplayDriver` 事件分发底座
- Parquet → MessagePack frame 的 adapter 我写 (C++, 一次性工具)
- 回测的 "Golden" 是已知 PnL baseline, 复用 GoldenLogAssertion 验回归

---

## 3. Walk-Forward 设计 (C++ 实现)

### 3.1 总体思路

走 **rolling walk-forward + purged + embargo** 三件套 (与 v0.1 设计一致, 实现语言换 C++).

```
时间轴:
  IS-1  | Embargo | OOS-1 | ... 滚动 ...
        IS-2  | Embargo | OOS-2 |
              IS-3  | Embargo | OOS-3 |
```

### 3.2 IS / OOS 切分 (按 sport / 信号区分)

(沿用 v0.1 §3.2, 时间窗 + 数据集划分不变)

**P0-01 (Pinnacle no-vig, pregame 6h):**
- IS: 2024-10-22 ~ 2025-02-15
- Embargo: 2025-02-15 ~ 2025-02-22
- OOS: 2025-02-22 ~ 2025-04-13
- 季后赛二段 OOS: 2025-04-13 ~ 2025-06-15

### 3.3 防止偷看 OOS 的代码层 enforce (C++ 编译期 + 运行期)

**v0.2 升级: C++ template + concept 编译期 phase 锁定**

```cpp
// src/backtest/guard.h

enum class Phase { InSample, OutOfSample, Frozen };

template <Phase P>
class DataAccessGuard {
public:
    DataAccessGuard(arrow::TimestampScalar start, arrow::TimestampScalar end)
        : start_(start), end_(end) {}

    // 仅在 IS phase 编译时允许参数调
    std::shared_ptr<arrow::RecordBatch> get_data(
        arrow::TimestampScalar req_start,
        arrow::TimestampScalar req_end) const {
        if (req_start < start_ || req_end > end_) {
            throw OOSLeakageError(/* ... */);
        }
        return load_(req_start, req_end);
    }

    // 编译期约束: 仅 IS phase 可调 freeze_params
    template <Phase Q = P>
    typename std::enable_if<Q == Phase::InSample, void>::type
    freeze_params(const ParamSet& params) {
        frozen_ = params;
    }

    // OOS phase 调 set_params → 编译失败 (concept 拦截)
    // 这是 v0.2 比 v0.1 强的地方: 不是运行期 raise, 是编译期拦截
};
```

**运行期防御 (CI 检查):**
- `tools/ci/oos_leakage_check.py` (注意: 这是 dev tool, 不入 production, 可保留 Python)
- 用 libclang AST 扫所有 backtest 子模块: 任何直接读 Parquet (绕过 guard) → 拒 merge
- @老吴 帮我加到 CI pipeline

---

## 4. 过拟合检测 (C++ 实现)

### 4.1 多重比较校正 (Bonferroni)

**实现:** C++ 函数 `stats::bonferroni_adjust(p_values, n_experiments) -> std::vector<double>`.

- 任何阈值扫描 (e.g. P0-01 的 3¢ vs 4¢ vs 5¢) 必须报告:
  - 单次 p-value
  - Bonferroni 校正后 p-value (= p × N_experiments)
  - 校正后仍 < 0.01 才算 IS 通过

```cpp
// src/backtest/stats/bonferroni.cc
std::vector<double> bonferroni_adjust(
    const std::vector<double>& p_values, std::size_t n_experiments) {
    std::vector<double> out;
    out.reserve(p_values.size());
    for (double p : p_values) {
        out.push_back(std::min(1.0, p * n_experiments));
    }
    return out;
}
```

### 4.2 Deflated Sharpe Ratio (López de Prado 2014) — C++ 实现

公式 (沿用 v0.1 §4.2):

$$DSR = Z(\hat{SR}) - \frac{\sqrt{1 - \gamma}}{\sqrt{T - 1}} \cdot \left( \sigma_{SR} \cdot Z^{-1}(1 - 1/N) \right) \cdot \sqrt{T}$$

**C++ 实现 (Boost.Math):**

```cpp
// src/backtest/stats/deflated_sharpe.cc
#include <boost/math/distributions/normal.hpp>

double deflated_sharpe(
    double sr_hat,       // 观测 Sharpe
    double sigma_sr,     // Sharpe 时序 std
    double gamma,        // Sharpe 时序 skewness
    std::size_t T,       // trade 数
    std::size_t N) {     // 试过的策略数
    boost::math::normal_distribution<> phi;
    double z_sr = sr_hat;
    double phi_inv = boost::math::quantile(phi, 1.0 - 1.0 / N);
    double correction = std::sqrt(1.0 - gamma) / std::sqrt(T - 1)
                        * sigma_sr * phi_inv * std::sqrt(T);
    return z_sr - correction;
}
```

**MVP 上线门槛 (与小程 §3.6 + v0.1 一致):** DSR > 1.0

### 4.3 Nested Cross-Validation (C++)

防"调阈值偷看 OOS":

```cpp
// src/backtest/cv/nested_cv.cc
struct OuterSplit { /* IS range, OOS range */ };
struct InnerFold  { /* train, val ranges */ };

for (const auto& outer : walk_forward_splits) {
    ParamSet best_params;
    double best_inner_score = -std::numeric_limits<double>::infinity();
    
    for (const auto& inner : cv_splits(outer.is, 5 /* n_folds */)) {
        for (const auto& params : param_grid) {
            double score = evaluate(params, inner.train, inner.val);
            if (score > best_inner_score) {
                best_inner_score = score;
                best_params = params;
            }
        }
    }
    
    // freeze, OOS 评分
    DataAccessGuard<Phase::Frozen> guard(outer.oos.start, outer.oos.end);
    double oos_score = evaluate_frozen(best_params, guard);
    report(outer, best_params, oos_score, best_inner_score);
}
```

### 4.4 Purged + Embargo (C++)

- 按 `game_id` 分组, 同一场只能进 1 个 fold (`arrow::compute::Hash` 分组)
- 邻近时间 (< embargo) 的 fold 也弃
- Embargo 默认 7 天

### 4.5 PBO (Probability of Backtest Overfitting, Bailey & López de Prado 2017)

v0.1 没列, v0.2 补上 (因 C++ 实现门槛低, 顺便落):

```cpp
// src/backtest/stats/pbo.cc
double probability_of_backtest_overfitting(
    const std::vector<std::vector<double>>& strategy_returns,
    std::size_t S);  // S 是 cross-validation 切分数

// PBO < 0.5 → 信号有真 alpha 概率高
// PBO > 0.5 → 怀疑过拟合
```

**门槛:** PBO < 0.3 (小梁 financial-expert 同意, 6/12 会签)

---

## 5. 数据 Schema 需求 (与 v0.1 一致, 沿用)

(完全沿用 v0.1 §5, 因 Parquet schema 由小余 / 小段 owner, 我消费. Arrow C++ 直接读, 无须改 schema.)

**与 v0.1 唯一差异:** 我现在用 `arrow::dataset::FileSystemDataset` 读 Parquet (C++), 不再走 `pyarrow.dataset` (Python).

**Schema 版本校验 (C++):**

```cpp
auto schema_version = batch->schema()->metadata()->Get("schema_version");
if (schema_version != EXPECTED_VERSION) {
    throw SchemaMismatchError(/* ... */);
}
```

---

## 6. 回测引擎技术架构 (v0.2 C++)

### 6.1 模块图 (见 §2.3)

### 6.2 信号 Contract (与小程会签, YAML 不变)

YAML 与 v0.1 一致, 但解析端改 C++:

```cpp
// src/backtest/contract/loader.cc
SignalContract load_contract(const std::filesystem::path& yaml_path) {
    auto root = YAML::LoadFile(yaml_path.string());
    SignalContract c;
    c.signal_id = root["signal_id"].as<std::string>();
    c.version   = root["version"].as<std::string>();
    // features.required, trigger, entry, exit, metrics 全部 C++ 解析
    return c;
}
```

**trigger expression 改 C++ DSL:**

v0.1 用 `python_expr` 字段 (Python eval), v0.2 不允许 Python eval. 改 C++ DSL:

```yaml
# configs/signals/p0_01_pinnacle_novig.yaml
trigger:
  cpp_expr: |
    abs(p_pm - p_pinn_novig) >= 0.03
    && (kickoff_in_hours >= 0.5 && kickoff_in_hours <= 6.0)
    && depth_24h >= 50000
    && last_update_age_s <= 300
    && !news.lineup_flag
```

C++ 端用 ExprTk (header-only, BSL-1.0) 解析编译期 expression. 解析失败 → load 时报错, 不到回测期才挂.

### 6.3 与 RM 的接口 (BR-2)

v0.1 通过 pyo3 binding 调 RM, v0.2 直接 C++ 函数调用:

```cpp
// 同进程, 同 binary, 无 IPC
risk::RiskGateway rm(load_config("configs/rm/backtest_config.toml"));
rm.set_clock(virtual_clock);  // 注入虚拟时钟 (复用小宋 VirtualClock)

for (auto& [ts, intent] : intent_stream) {
    virtual_clock.advance_to(ts);
    auto decision = rm.evaluate(intent);
    if (decision.decision == RiskDecision::APPROVED) {
        // 走 BacktestSigner + SlippageModel
    } else if (decision.decision == RiskDecision::DEFERRED) {
        // 入 retry queue
    } else {
        rejected_counter[decision.reject_code]++;
    }
}
```

**红线约束 (与 v0.1 一致):**
- RM 配置 `backtest_config.toml` 与 `prod_config.toml` 仅差 capacity 类参数, 红线参数 (HARD cap / KELLY_FRACTION / STALE) 全部一致
- 任何"backtest-only RM bypass" → 红线

### 6.4 BacktestSigner (C++)

```cpp
// src/exec/signer/backtest_signer.cc
class BacktestSigner {
public:
    Fill sign_and_submit(
        AuditId audit_id, const OrderIntent& intent,
        const RiskDecision& decision) {
        // 复用小肖 slippage 模型 (BR-5)
        auto slip = slippage_model_.estimate({
            .order_size_usdc = decision.approved_size_usdc,
            .quote_price = intent.price,
            .book_depth_l1_usdc = intent.book_depth_l1_usdc,
            .time_since_quote_ms = clock_.now_ms() - intent.book_snapshot_ts_ms,
            .tick_size = intent.tick_size,
        });
        
        // Bernoulli 抽样 fill
        bool actually_filled = rng_() < slip.expected_fill_rate;
        if (!actually_filled) {
            return Fill{audit_id, FillStatus::UNFILLED, /* ... */};
        }
        return Fill{
            .audit_id = audit_id,
            .status = FillStatus::FILLED,
            .fill_price = slip.expected_fill_price,
            .fill_size = decision.approved_size_usdc,
            .fill_ts_ns = clock_.now_ns() + estimate_e2e_latency_ns(),
        };
    }
private:
    slippage::SlippageModel& slippage_model_;  // 引用, 共享 C++ lib
    VirtualClock& clock_;
    std::mt19937_64 rng_;
};
```

### 6.5 PnL Accrual (C++)

```cpp
// src/backtest/ledger.cc
class BacktestLedger {
public:
    void apply_fill(const Fill& fill, const SettleEvent& settle) {
        // Polymarket 二元: 赢 → (1 - fill_price) * size, 输 → -fill_price * size
    }
    
    std::unordered_map<MarketId, double> mark_to_market(TimestampScalar ts) const {
        // 持仓未平时, 用当时 mid 估值
    }
};
```

---

## 7. 报告生成 (v0.2 C++)

### 7.1 报告结构 (与 v0.1 + paper 同模板)

```
report/
├── 01_summary.html            # PnL curve + 累计 trade count (vega-lite JSON 嵌入)
├── 02_sharpe.html             # rolling 30d Sharpe
├── 03_drawdown.html
├── 04_hit_rate.html
├── 05_alpha_decay.html
├── 06_slippage_diag.html      # predicted vs actual slippage
├── 07_clv.html
├── 08_reject_breakdown.html
├── metrics.json
├── trades.parquet             # 全部 trade 明细, 供 ad-hoc DuckDB 查询
└── config_snapshot.yaml
```

**实现 (C++):**
- vega-lite JSON 模板存 `templates/*.json.tmpl`, 用 `inja` (header-only Jinja-like for C++) 渲染
- C++ 计算指标 → 灌进 JSON → 输出 HTML (含 vega-lite CDN 引用)
- 报告生成时间从 v0.1 ~10s/run 降到 v0.2 ~1s/run (无 Python 启动开销 + 直接计算)

### 7.2 报告 CLI

```bash
# 跑回测
stcpp_backtest run \
  --config configs/signals/p0_01_pinnacle_novig.yaml \
  --data-root data/ \
  --is-start 2024-10-22 --is-end 2025-02-15 \
  --oos-start 2025-02-22 --oos-end 2025-04-13 \
  --output runs/p0_01_run_20260716/

# 出报告
stcpp_backtest report --run-dir runs/p0_01_run_20260716/

# 对比两个 run (e.g. backtest vs paper)
stcpp_backtest compare \
  --baseline runs/p0_01_run_20260716/ \
  --candidate runs/p0_01_paper_w1/ \
  --output reports/comparison.html
```

`compare` 命令是 M4.5 gate (Python tool-level) 的输入源.

### 7.3 ad-hoc 查询 (DuckDB C++ API)

```cpp
// tools/ad_hoc/query_pnl.cc (C++ 一次性脚本)
duckdb::DuckDB db(nullptr);
duckdb::Connection conn(db);
auto result = conn.Query(
    "SELECT date_trunc('day', fill_ts) AS d, sum(pnl_realized_usdc) "
    "FROM 'runs/p0_01_run_20260716/trades.parquet' "
    "WHERE pnl_realized_usdc IS NOT NULL "
    "GROUP BY d ORDER BY d");
result->Print();
```

或用 DuckDB CLI 直接 (Notebook 一次性探索 OK):

```bash
duckdb -c "SELECT ... FROM 'runs/.../trades.parquet'"
```

---

## 8. P0-01 Pinnacle no-vig 第一份回测计划 (v0.2 时间表)

### 8.1 项目总览 (与 v0.1 一致, 时间窗不变)

| 项 | 值 |
|---|---|
| 信号 | SIG-P0-01 pinnacle-novig-revert |
| 数据 | NBA 2024-10-22 ~ 2025-04-13 |
| IS 窗 | 2024-10-22 ~ 2025-02-15 (~600 场) |
| OOS 窗 | 2025-02-22 ~ 2025-04-13 (~280 场) |
| 二段 OOS (季后赛) | 2025-04-13 ~ 2025-06-15 |
| 目标 trade 数 | IS ≥ 500, OOS ≥ 150, 二段 OOS ≥ 80 |
| 计算耗时估算 | 单次 run ~ **3min** (C++ vs v0.1 Python 15min, 提速 5x) |
| 参数扫描组合 | 27 (3 阈值 × 3 depth × 3 Kelly fraction), Bonferroni N=27 |

### 8.2 关键阻塞 (gating)

| # | 阻塞 | Owner | 死线 | v0.1 vs v0.2 |
|---|---|---|---|---|
| G1 | Pinnacle 历史数据 6 月 | @老李 + @老彭 + @小程 OQ-2 | 2026-06-12 | 不变 |
| G2 | Polymarket book snapshots 6 月 Parquet | @小余 | 2026-06-19 | 不变 |
| G3 | Goalserve pregame + lineup | @小段 | 2026-06-19 | 不变 |
| **G4 (v0.2)** | **C++ feature_pipeline lib (libstcpp_features.a)** | @老周 + @小田 | **2026-06-26** | v0.1 pyo3 binding → v0.2 直接 C++ link |
| **G5 (v0.2)** | **C++ RiskGateway (in-process, 不再 binding)** | @老韩 + 老周 | **2026-06-26** | 同上 |
| G6 | slippage model v1 (C++) | @小肖 | 2026-06-19 | 不变 (小肖 v1 本来就 C++) |
| G7 | 信号 contract YAML (P0-01) 锁版 + C++ DSL 校验 | @小程 + 我 | 2026-06-12 | DSL 改 cpp_expr |
| **G8 (v0.2 新)** | **Arrow C++ + DuckDB C++ 依赖入 vendored / system** | @老吴 (devops) | **2026-06-12** | 新增, 但 brew install duckdb / brew install apache-arrow 即可 |
| **G9 (v0.2 新)** | **回测 C++ 框架 skeleton (driver / guard / ledger)** | 我 | **2026-07-09** | 新增, 用掉 +4 周 |

### 8.3 时间表 (T = 2026-05-28 today)

| 周 | 里程碑 | v0.1 vs v0.2 |
|---|---|---|
| W1 (5/28 - 6/4) | 框架 RFC + C++ 选型会签 | v0.1 framework skeleton → v0.2 选型 + RFC |
| W2 (6/5 - 6/11) | Arrow C++ + DuckDB 接入 + Parquet loader skeleton | v0.1 用 Python polars, v0.2 用 Arrow C++ |
| W3 (6/12 - 6/18) | Pinnacle 数据接入; 单元测试 no-vig 公式 (C++) | 不变, 实现语言换 |
| W4 (6/19 - 6/25) | DataAccessGuard (C++ template) + Polymarket book Parquet 接入 | v0.1 Python guard → v0.2 C++ template |
| W5 (6/26 - 7/2) | feature lib + RiskGateway + slippage 接通; IS dry-run | 工作量 +1 周 (vs v0.1) |
| W6 (7/3 - 7/9) | walk-forward + DSR + PBO + Bonferroni (C++ 实现) | v0.2 新写 4 周 中的第 1 周 |
| W7 (7/10 - 7/16) | 报告生成 (vega-lite + inja) + 跑完 IS 正式 + OOS 第一轮 | v0.2 新写 4 周 中的第 2 周 |

**P0-01 第一份完整回测报告: 2026-07-16 (W7 末)**, 含 IS + 第一轮 OOS. (v0.1 是 7/2, v0.2 延期 2 周)

后续 (W8) 含参数扫 + 季后赛 regime shift, 报告 v2 = 7/23.

### 8.4 失败模式 (preregistered, 沿用 v0.1)

(不变, 见 v0.1 §8.4)

### 8.5 报告交付物 (沿用 v0.1)

(不变, 见 v0.1 §8.5, 仅格式从 Python HTML → C++-generated HTML)

---

## 9. 与他人接力 (v0.2 更新)

### 9.1 我向上游要的

| 物件 | Owner | 死线 | v0.1 vs v0.2 |
|---|---|---|---|
| 信号 contract YAML (P0-01, P0-02) + cpp_expr 字段 | 小程 | 6/12 / 6/26 | DSL 字段名改 |
| Pinnacle no-vig 公式 + C++ 单元测试 | 小程 | 6/12 | 实现换 C++ |
| score_model (P0-02, LightGBM → ONNX) | 小邓 (训练) + 我 (C++ 推理用 Treelite) | 7/9 | v0.2 新增 ONNX 路径 |
| Polymarket book Parquet | 小余 | 6/19 | 不变 |
| Pinnacle 历史 | 老李 / 老彭 | 6/12 | 不变 |
| Goalserve livescore + pregame Parquet | 小段 | 6/19 | 不变 |
| **libstcpp_features.a (C++ feature lib)** | 老周 / 小田 | 6/26 | **v0.1 是 .so + pyo3, v0.2 是 .a 直接 link** |
| **libstcpp_risk.a (C++ RM)** | 老韩 / 老周 | 6/26 | 同上 |
| Slippage model v1 (C++) | 小肖 | 6/19 | 不变 |
| ReplayDriver C++ (复用小宋) | 小宋 | 6/30 | 不变 |
| **ONNX 模型导出契约 + C++ ONNX Runtime 集成示例** | 小邓 / 老高 | **7/2** | **v0.2 新增** |

### 9.2 我向下游交付的

| 物件 | 用途 | 接收 |
|---|---|---|
| backtest framework v0.2 (本文) | 设计契约 | 全队 review |
| backtest CLI + 框架代码 (C++) | 跑回测 | 自己 + 小程 / 小董 / 老木 |
| P0-01 第一份回测报告 v1 (2026-07-16) | 阈值确认 + 信号验收 | 小梁 + 小程 + 老雷 |
| P0-02 回测报告 (T+8w, 2026-07-23) | 同上 | 同上 |
| backtest baseline (供 M4.5 gate 对比) | paper PnL 比对基准 | M4.5 gate (姊妹文档) |

### 9.3 不耻下问 (v0.2 新增咨询)

| 问题 | 找谁 | 进度 |
|---|---|---|
| ONNX 模型 C++ 推理性能 (LightGBM tree, 单 batch < 50μs?) | @小邓 (ML) | 已问, 等小邓 prototype |
| ONNX 模型导出契约 (input dtype / shape / batch size) | @小邓 + @老高 | W2 会签 |
| Arrow C++ ColumnarBatch 与 RecordBatch 性能特征 (filter / aggregate 哪个更适合 walk-forward 切窗) | @老周 | W1 会签 |
| DuckDB C++ API 静态 link 大小 + 启动开销 | @老吴 (devops) | W1 测了 |
| ExprTk vs 编译期 SignalDSL (是否值得自写) | @老高 | W2 决议 |

---

## 10. 风险点 + 开放问题

### 10.1 已知风险 (v0.2 重排)

| # | 风险 | 严重度 | 缓解 |
|---|---|---|---|
| BR-1 | **C++ 重写工作量超预算 → 延期 > 2 周** | **高** | W1 立刻起 skeleton, 每周 mid-week checkpoint; 必要时砍 PBO 留 v0.3 |
| BR-2 | Pinnacle 历史数据卡死 → P0-01 推迟 | 高 | 三路径并行 (老李 API / The Odds API 付费 / 老彭 CSV); 6/12 至少一条 work |
| BR-3 | C++ feature lib 接口与小肖 / 小程 漂移 | 高 | W2 与老周定 ABI; CI 跑 ABI 检查 (`nm` 比对) |
| BR-4 | OOS leakage (代码绕开 guard) | 中 | C++ template + concept 编译期拦截 + libclang AST 扫 |
| BR-5 | slippage model 在历史数据上无 ground truth fill | 中 | 用 Polymarket 历史 trade tape 反推, 与 v0.1 一致 |
| BR-6 | Polymarket 流动性历史变化 (24 vs 25 vs 26 不同) | 中 | 报告含 quarter-by-quarter Sharpe, 检测 capacity drift |
| BR-7 | C++ 报告生成 (vega-lite + inja) 模板渲染体验差 | 低 | inja 已成熟 (>10k stars), vega-lite 模板可调; 必要时 fallback 到 std::format + html |
| BR-8 | ONNX Runtime 静态 link 体积 ~40MB | 低 | 接受, 与 DuckDB amalgamation 同级; runtime 加载耗时 < 20ms |
| **BR-9 (新, Top 1)** | **C++ stats lib 不如 Python 富, DSR / PBO 计算自写易出 bug** | **高** | 与小邓 review 公式, 跑 cross-check (Python sklearn 跑 ground truth, C++ 比对, 容差 1e-6) |

### 10.2 开放问题 (v0.2 更新)

| # | 问题 | 找谁 | 截止 |
|---|---|---|---|
| BQ-1 | libstcpp_features.a ABI 稳定性 (跨 Sprint 变更怎么管) | @老周 / @小田 | 6/12 |
| BQ-2 | RM backtest_config.toml 是否需要单独审批 | @老韩 | 6/12 |
| BQ-3 | slippage model 历史 fit 用什么 holdout | @小肖 | 6/19 |
| BQ-4 | 二段 OOS (季后赛) 是否进 M4.5 gate | @老雷 + @小梁 | 6/26 |
| BQ-5 | walk-forward 滚动 (M+6 后) 启动节奏 | @老雷 | M+6 |
| **BQ-6 (新)** | **ONNX 模型 C++ 推理 vs Treelite 选哪个 (tree model 时)** | @小邓 + @老高 | 7/2 |
| **BQ-7 (新)** | **C++ ExprTk vs 编译期 DSL, 信号 trigger 写法终选** | @老高 | 6/19 |
| **BQ-8 (新)** | **C++ vega-lite 模板 vs C++ matplotlib-cpp 报告生成路径终选** | @老高 + @小郑 (D1 dashboard) | 6/26 |
| **BQ-9 (新)** | **回测期间 ML 模型从 Python 训练 → ONNX 导出 → C++ 推理的 round-trip 验证流程** | @小邓 + 我 | 7/9 |

### 10.3 v0.2 不做, 留 v0.3+

- 多账户回测
- 实时 walk-forward (streaming)
- Bayesian hyperparameter search
- counterfactual analysis ("如果 size 翻倍会怎样")
- option-style payoff 回测 (体育 prop 类信号未启用)
- v0.2 自研 SQL 引擎 (DuckDB 现在够用)

---

## 11. v0.2 工作量延期评估 (给老雷)

### 11.1 工作量差分

| 模块 | v0.1 (Python) | v0.2 (C++) | 增量 |
|---|---|---|---|
| 框架 skeleton + Parquet loader | 1 周 | 2 周 (Arrow C++ + DuckDB 起步) | +1 周 |
| DataAccessGuard | 0.5 周 (Python class) | 1 周 (C++ template + concept) | +0.5 周 |
| walk-forward + purged k-fold | 0.5 周 (sklearn 现成) | 1.5 周 (自写) | +1 周 |
| DSR + PBO + Bonferroni | 0.5 周 (statsmodels) | 1 周 (Boost.Math 自写) | +0.5 周 |
| 报告生成 (HTML) | 0.5 周 (matplotlib + jinja) | 1.5 周 (vega-lite + inja) | +1 周 |
| 信号 contract eval | 0.3 周 (Python eval) | 0.5 周 (ExprTk) | +0.2 周 |
| **总计** | **3.3 周** | **7.5 周** | **+4.2 周** |

**但同时 paper engine 省 ~6 周** (无 Python wrapper 维护): 见姊妹文档 v0.2.

**净影响:** 整体项目延期 **~2 周** (回测延期 2 周, paper 提前 2 周, 抵消后约平).

### 11.2 时间表关键节点重排

| v0.1 节点 | v0.2 节点 | 延期 |
|---|---|---|
| P0-01 第一份回测报告: 7/2 | **7/16** | +2 周 |
| paper engine 落代码: 7 月 (从 backtest 7/2 后启动) | **7 月底-8 月初** (从 backtest 7/16 后启动) | +2 周 |
| paper 正式 D1: 8/15 | **8/29** | +2 周 |
| M4.5 gate 第一次判定: 8/29 | **9/12** | +2 周 |
| live 切换 (PASS 后): 9 月起 | **9 月底 - 10 月初** | +2 周 |

**用户高优:** "虚拟盘稳定盈利后才跑实盘" — 不变. v0.2 多花 2 周买 C++ 生产纯净 + 长期维护简化.

---

## 附录 A — Arrow C++ + DuckDB C++ API 上手清单

### A.1 依赖安装 (老吴 G8)

```bash
# macOS
brew install apache-arrow duckdb boost tbb onnxruntime

# Linux
apt-get install libarrow-dev libduckdb-dev libboost-math-dev libtbb-dev libonnxruntime-dev
```

### A.2 最小回测 driver 示例 (C++)

```cpp
// src/backtest/driver_skeleton.cc
#include <arrow/api.h>
#include <arrow/dataset/api.h>
#include <duckdb.hpp>
#include <stcpp/features/api.h>      // 同进程 C++ feature lib
#include <stcpp/risk/gateway.h>      // 同进程 C++ RM
#include <stcpp/slippage/model.h>    // 同进程 C++ slippage

int main() {
    // 1. 加载 Parquet → arrow::RecordBatch
    auto dataset = arrow::dataset::FileSystemDataset::Make(/* Parquet path */);
    auto scanner = dataset->NewScan().Finish().ValueOrDie()->Finish().ValueOrDie();
    
    // 2. for each batch, run feature pipeline + signal + RM + signer
    for (auto batch : scanner->ScanBatches()) {
        auto features = stcpp::features::compute_batch(batch);
        auto intents = stcpp::signal::eval(features, contract);
        for (auto& intent : intents) {
            auto decision = rm.evaluate(intent);
            if (decision.is_approved()) {
                auto fill = signer.sign_and_submit(intent, decision);
                ledger.apply(fill);
            }
        }
    }
    
    // 3. 报告生成
    auto metrics = stcpp::metrics::collect(ledger);
    stcpp::report::write_html(metrics, "runs/.../report.html");
    return 0;
}
```

### A.3 DuckDB ad-hoc 查询样例 (一次性 tool)

```cpp
// tools/ad_hoc/query.cc
duckdb::DuckDB db(nullptr);
duckdb::Connection conn(db);
auto r = conn.Query(R"sql(
    SELECT 
        date_trunc('day', to_timestamp(fill_ts_ns / 1e9)) AS d,
        sum(pnl_realized_usdc) AS pnl,
        count(*) AS n
    FROM 'runs/p0_01/trades.parquet'
    WHERE status = 'FILLED'
    GROUP BY d
    ORDER BY d
)sql");
r->Print();
```

---

## 附录 B — Python 保留清单 (再次明示)

| 文件 / 模块 | 用途 | 性质 | 不会进 production |
|---|---|---|---|
| `ml/training/*.py` | LightGBM / sklearn 训练 | 离线 一次性 | 导 ONNX, runtime 不依赖 |
| `notebooks/exploration/*.ipynb` | 数据探索 (小程 / 小董 / 我) | 一次性 | 不入 main / CI 监管 |
| `tools/m4_5_gate/run_gate_check.py` | M4.5 gate 判定 | cron job | tool-level, 读 audit log + parquet, 出 JSON |
| `tools/ci/oos_leakage_check.py` | CI AST 扫 | CI tool | 不入 binary |
| `tools/duckdb_cli/*.sql` | SQL 探索 | 一次性 | 不入 binary |

**红线 (BR-4):** 上述以外, 任何 `*.py` 出现在 `src/` 或 `runtime/` → CI 拒.

---

## 附录 C — 报告 metrics.json schema (与 v0.1 一致)

(与 v0.1 附录 C 完全一致, 因 schema 是 contract, 不因实现语言变. C++ 用 nlohmann/json 序列化.)

---

**END v0.2.** 等 6/12 选型会签 + 6/26 C++ feature lib + RM 接通, bump v0.3 跑 P0-01 IS dry-run.

— 小蒋 (quant-backtest), 2026-05-28
