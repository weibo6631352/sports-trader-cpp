# Rust 工程栈 v1 (GM 钦定首选验证语言)

- Owner: 老张 (rust-advisor)
- Date: 2026-05-28
- 验收人: 老雷 (GM) + 老周 (chief-architect) + 老高 (code-quality)
- 关联:
  - `laozhang-rust-signer-crates-v1.md` (signer 专项 crate 选型)
  - `CLAUDE.md` §10 / §12.3 (语言纪律 + Rust 工程栈定义)
  - `laojiang-latency-budget-v1.md` (热路径 budget)
  - `xiaoshi-data-structures-selection-v1.md` (lock-free 原语共识)
  - `xiaozheng-observability-v0.1.md` (Prometheus / OTel 集成)
  - `laogao-code-conventions-v1.md` (PR 红线 — 本文新增 R-Rust 段)
  - `laochen-network-bench-v1.md` (网络 bench, 给老陈迁移指南)
  - `xiaoduan-goalserve-api-spec-v1.md` (Goalserve, 给小段迁移指南)
  - `xiaojiang-backtest-framework-v0.1.md` (回测, 给小蒋迁移指南)
  - `xiaoliang-market-structure-v1.md` (市场微结构, 给小袁迁移指南)
- 用户钦定背景: 2026-05-28 GM 指令 — "即使做技术验证或测试, 尽量不要用 Python, 尽量用效率贴近 C++ 的 Rust. 我们是 C++ 项目, 多线程 / 数据流转 / 内存管理 / 大数据量高频并行处理, 框架性能这块一定要注意."
- 协作: signer 细节 @老孙, C++ 端边界 @老周, 性能基准 @老姜, 并发原语 @小石, observability 集成 @小郑, CI/CD @老吴 + @老高

---

## 0. TL;DR (老雷 5 分钟版)

**Rust 在本项目的定位**: 性能贴近 C++ 的二号工程语言, 用于 **"看得见的真实性能 + 真实并发 + 真实内存"** 的预研 / 测试 / 跨进程组件. 不替代 C++ 热路径, 不蔓延进 trader 主进程.

**1. Cargo workspace 总仓**: 一个独立的 `sports-trader-rs/` 仓 (与 C++ 主仓 `sports-trader-cpp/` 平行), workspace `resolver = "2"`, edition 2024, MSRV 1.83+. 子 crate 按功能拆 (`signer/api-client/bench/etl/...`), 不与 C++ 共享 build system.

**2. 3 个最关键 crate**:
- **`tokio`** (1.40+, 异步 IO 运行时) — API client / WSS / 跨进程通信的事实标准. 但严禁混进 signer (供应链最小化, 见老张 v1)
- **`criterion`** (0.5+, 统计严谨 benchmark) — 所有性能基准的官方门面. PR 必跑, 回归 > 5% 阻断
- **`polars`** (0.43+, 列式 DataFrame) — 替代 pandas 做离线 ETL / 回测分析. 同等内存 5-50x pandas, Rust 原生不丢精度

**3. 与 C++ 的边界 (起步阶段)**:
- **不混编** (不上 cbindgen / cxx-rs FFI, 直到老周明确给出"性能必须 Rust 写但要嵌 C++ 进程"的场景)
- **跨进程通信**: UDS + bincode (内部) / protobuf (公共 schema) / 共享内存 (大对象, 老王 WAL 同款)
- **公共 schema** 走 `.proto` / `.fbs` 文件, C++ / Rust 各自 codegen

**4. 三个"用户高优"承诺 (写进红线)**:
- **多线程纪律**: tokio (IO) vs rayon (CPU) vs std thread + crossbeam (lock-free queue) 三选一明确, 不允许混用. false sharing `#[repr(align(64))]` 强制
- **内存管理**: 零拷贝 `bytes::Bytes` + `zerocopy` + arena `bumpalo` 三件套. jemalloc / mimalloc 选 mimalloc (更小 RSS)
- **大数据量高频并行**: `futures::Stream` 背压 + `tokio::sync::mpsc(bounded)` 强制有界 + `polars` lazy frame 流式处理. 禁止 `collect::<Vec<_>>()` 在不知大小的流上

**5. 红线 (R-Rust-1 .. R-Rust-8)**: 见 §12. 热路径无临时分配 / 不 unwrap / unsafe 必带 SAFETY / panic 等于事故.

**6. CI/CD**: `cargo fmt --check && cargo clippy -D warnings && cargo test && cargo deny check && cargo audit && cargo bench --no-run` 全跑, 与老吴 GitHub Actions 联签. Benchmark 回归 > 5% 阻断 (与老姜 latency budget 对齐).

---

## 1. Cargo workspace 结构

### 1.1 仓库布局 (与 C++ 主仓平行)

```
~/code/
├── sports-trader-cpp/          # C++ 主仓 (热路径 + 生产)
│   ├── src/                    # C++ 源
│   ├── include/stcpp/
│   ├── tools/                  # 可调用 Rust 编译产物的 helper script
│   ├── third_party/proto/      # 公共 schema (.proto / .fbs)
│   └── docs/
│
└── sports-trader-rs/           # Rust 工程仓 (本文档负责)
    ├── Cargo.toml              # workspace root
    ├── Cargo.lock              # git track
    ├── rust-toolchain.toml     # 锁 toolchain 1.83.0+
    ├── deny.toml               # cargo-deny
    ├── .cargo/config.toml      # rustflags / target-cpu=native (仅 bench)
    ├── supply-chain/           # cargo-vet
    ├── crates/
    │   ├── stcpp-common/       # 公共类型 (订单 / 市场 / 错误), 无 IO
    │   ├── stcpp-proto/        # protobuf / flatbuffers codegen (与 C++ 端共享)
    │   ├── stcpp-api-client/   # Polymarket / Goalserve / Polygon RPC REST + WSS
    │   ├── stcpp-signer/       # 已存在 (老孙, 独立部署进程; 见 laozhang-rust-signer-crates-v1)
    │   ├── stcpp-nonce-mgr/    # 老叶 nonce 管理器 (独立进程)
    │   ├── stcpp-etl/          # ETL / parquet writer / polars lazy 流
    │   ├── stcpp-bench/        # 性能基准集合 (criterion + iai)
    │   ├── stcpp-loadgen/      # 压测发包 (k6 等价物, Rust 原生)
    │   ├── stcpp-replay/       # 历史 tick replay 工具
    │   └── stcpp-ipc/          # 共享内存 / UDS 跨进程客户端 (C++ 也通过 SHM ABI 互通)
    ├── vendor/                 # cargo vendor 锁源 (release)
    └── target/                 # build out (gitignore)
```

### 1.2 workspace root `Cargo.toml`

```toml
[workspace]
resolver = "2"
members = [
    "crates/stcpp-common",
    "crates/stcpp-proto",
    "crates/stcpp-api-client",
    "crates/stcpp-signer",
    "crates/stcpp-nonce-mgr",
    "crates/stcpp-etl",
    "crates/stcpp-bench",
    "crates/stcpp-loadgen",
    "crates/stcpp-replay",
    "crates/stcpp-ipc",
]

# signer 是独立部署进程, 不出现在 default-members, 防止 etl 改动触发 signer 重 build
default-members = [
    "crates/stcpp-common",
    "crates/stcpp-api-client",
    "crates/stcpp-etl",
    "crates/stcpp-bench",
]

[workspace.package]
version = "0.1.0"
edition = "2024"
rust-version = "1.83"
authors = ["sports-trader team"]
license = "Proprietary"
publish = false

[workspace.lints.rust]
unsafe_code = "warn"
missing_docs = "warn"
unused_must_use = "deny"
rust_2024_compatibility = "warn"

[workspace.lints.clippy]
unwrap_used = "deny"
expect_used = "warn"
panic = "deny"
todo = "warn"
indexing_slicing = "warn"
integer_arithmetic = "warn"
mod_module_files = "deny"
allow_attributes = "warn"
let_underscore_must_use = "deny"

[profile.release]
opt-level = 3
lto = "fat"
codegen-units = 1
strip = false           # 保留 symbol 便于 perf / audit
panic = "abort"         # 与 C++ 端 fail-fast 风格对齐

[profile.bench]
inherits = "release"
debug = "line-tables-only"   # criterion + perf 需要

# 临时调试用: cargo build --profile dev-opt
[profile.dev-opt]
inherits = "dev"
opt-level = 2

[workspace.dependencies]
# 详见 §2 各场景表
tokio = { version = "=1.40.0", default-features = false }
reqwest = { version = "=0.12.7", default-features = false, features = ["rustls-tls", "http2", "gzip", "brotli", "json", "stream"] }
tokio-tungstenite = { version = "=0.23.1", default-features = false, features = ["rustls-tls-webpki-roots"] }
rustls = { version = "=0.23.12", default-features = false, features = ["std", "tls12", "aws_lc_rs"] }
futures-util = { version = "=0.3.30", default-features = false, features = ["std"] }
serde = { version = "=1.0.210", default-features = false, features = ["derive", "std"] }
serde_json = { version = "=1.0.128", default-features = false, features = ["std"] }
simd-json = { version = "=0.13.10", default-features = false, features = ["serde_impl", "known-key"] }
bincode = { version = "=2.0.0-rc.3" }
rmp-serde = { version = "=1.3.0" }
governor = { version = "=0.6.3", default-features = false, features = ["std"] }
backoff = { version = "=0.4.0", default-features = false, features = ["tokio"] }
polars = { version = "=0.43.1", default-features = false, features = ["lazy", "parquet", "csv", "json", "streaming", "dtype-full"] }
arrow2 = { version = "=0.18.0", default-features = false }
parquet2 = { version = "=0.17.2", default-features = false }
zerocopy = { version = "=0.8.5", default-features = false, features = ["derive"] }
bytes = { version = "=1.7.2", default-features = false }
crossbeam = { version = "=0.8.4", default-features = false, features = ["std"] }
crossbeam-channel = { version = "=0.5.13" }
crossbeam-queue = { version = "=0.3.11" }
crossbeam-utils = { version = "=0.8.20" }
rayon = { version = "=1.10.0" }
parking_lot = { version = "=0.12.3" }
core_affinity = { version = "=0.8.1" }
bumpalo = { version = "=3.16.0", features = ["collections", "boxed"] }
typed-arena = { version = "=2.0.2" }
memmap2 = { version = "=0.9.5" }
nalgebra = { version = "=0.33.0", default-features = false, features = ["std"] }
statrs = { version = "=0.17.1" }
tracing = { version = "=0.1.40", default-features = false, features = ["std", "attributes"] }
tracing-subscriber = { version = "=0.3.18", default-features = false, features = ["std", "fmt", "json", "registry", "env-filter"] }
prometheus-client = { version = "=0.22.3" }
clap = { version = "=4.5.18", features = ["derive", "env"] }
anyhow = { version = "=1.0.86" }
thiserror = { version = "=1.0.63" }
proptest = { version = "=1.5.0" }
fake = { version = "=2.9.2", features = ["derive"] }
criterion = { version = "=0.5.1", default-features = false, features = ["cargo_bench_support", "html_reports"] }
iai = { version = "=0.1.1" }
mimalloc = { version = "=0.1.43", default-features = false }
nix = { version = "=0.29.0", default-features = false, features = ["mman", "socket", "user", "process", "sched"] }
shared_memory = { version = "=0.12.4" }
```

### 1.3 子 crate 拆分原则 (写进 ADR)

| 原则 | 理由 |
|---|---|
| **每 crate 单一职责** (signer 不碰 api-client, etl 不碰 ipc) | 失败 blast radius 小; 独立 review; 编译缓存命中率高 |
| **crate 边界即 thread 边界**: 一个 crate 内部用同一种并发模型 (全 tokio 或全 rayon 或全 std thread), 不混 | 心智成本; 类型系统冲突 (`Send` / `!Send` 边界); 调试痛苦 |
| **`stcpp-common` 无 IO 依赖** (不依赖 tokio / reqwest / std::fs) | 单元测试不需 sandbox; 可被任意 crate 引入不污染 |
| **`stcpp-proto` 单独 crate** 而非每个 crate 自己 codegen | 共享 schema 单一来源; 与 C++ 端 protoc 输出对齐 |
| **`stcpp-signer` / `stcpp-nonce-mgr` 是独立 binary**, 编译产物部署为系统服务 | 独立攻击面; signer 已与 trader 进程隔离 (老孙 v3) |
| **bench / loadgen / replay 是工具 crate**, 不进生产部署 | dev-dependencies 可以重 (criterion, fake), 不污染 prod build |

### 1.4 与 C++ 主仓的边界

| 边界 | 决策 | 理由 |
|---|---|---|
| **不共享 build system** | C++ 走 vcpkg + CMake; Rust 走 Cargo. 不上 corrosion / cmake-rs / cxx-rs | 起步阶段, 维护一份 build chain 已经够痛; FFI 出错风险高 |
| **不在 C++ 进程里嵌 Rust DLL** | 不上 cbindgen / cxx-rs (Wave 8 不开此场景) | 现阶段 Rust 都是独立进程: signer / nonce-mgr / etl 工具 / bench. 嵌 DLL 引入 panic 跨边界 / 内存模型不一致风险 |
| **跨进程通信: 优先 UDS + 二进制 framing** | bincode (内部) / protobuf (公共 schema) / FlatBuffers (零拷贝大对象) | UDS 单机延迟 < 50us, 满足 signer < 5ms 预算; protobuf 是 C++ / Rust 都成熟支持的 |
| **共享内存: 用 `shared_memory` crate** (Rust 端) ↔ `boost::interprocess` (C++ 端) | 老王 WAL framework 也用 SHM; 协议层走 FlatBuffers 零拷贝 schema |
| **公共 schema 文件位置**: `sports-trader-cpp/third_party/proto/*.proto`, Rust workspace 通过 git submodule 引入 | 单一 SSOT, 改一处两边同步; 防 schema 漂移 |
| **Rust crate 不写 C++ ABI 兼容代码** | C++ ABI 不稳定; cbindgen 输出的 header 容易和 stcpp 命名约定冲突 (老高 §1) |

**何时允许破例 (上 FFI)**:
- 当 C++ 热路径出现一个**性能验证后 Rust 显著更优** (≥ 30%) **且无法独立进程化** (单笔延迟预算 < UDS RTT 50us) 的场景, 才考虑 cxx-rs. 现阶段无此场景.
- 决策权 @老周 + @老郭 (架构评审).

---

## 2. 核心 crate 选型 (按场景)

> 每个 crate 推荐附: 性能数据 / 已知 issue / 知名采用. 锁版本到 patch level, 升级走 PR + 老张 review.

### 2.1 API client (Polymarket / Goalserve / Polygon RPC)

| Crate | 版本 | 用途 | 性能 / 备注 | 知名采用 |
|---|---|---|---|---|
| **`reqwest`** | `=0.12.7` | HTTP/1.1 + HTTP/2 客户端 | 30k req/s 单线程 (本地 loopback); 默认 hyper + rustls; 不开 native-tls | aws-sdk-rust, alloy, half of Rust web ecosystem |
| **`tokio`** | `=1.40.0` | async runtime | 业界事实标准; 100M+ downloads; 维护活跃 | 几乎所有 async crate |
| **`serde_json`** | `=1.0.128` | JSON serde (慢路径, 解析配置) | ~250 MB/s parse; 老牌稳定 | 几乎所有 Rust JSON |
| **`simd-json`** | `=0.13.10` | JSON 高速 parse (热路径) | **~1.2 GB/s parse**, 比 simdjson C++ 还快一点 (SIMD intrinsics; AVX2 / NEON) | TiKV, Vector |
| **`governor`** | `=0.6.3` | rate limit (GCRA 算法) | 单线程 50ns/decision; 与 Goalserve hot endpoint 限流配 | Cloudflare 部分内部工具 |
| **`backoff`** | `=0.4.0` | 指数退避重试 | tokio 集成; jitter 内置 | aws-sdk-rust (类似) |

**用法骨架** (与 `xiaoduan-goalserve-api-spec-v1` 对齐):

```rust
use governor::{Quota, RateLimiter};
use std::num::NonZeroU32;

let limiter = RateLimiter::direct(Quota::per_second(NonZeroU32::new(10).unwrap()));
let client = reqwest::ClientBuilder::new()
    .pool_max_idle_per_host(8)
    .pool_idle_timeout(std::time::Duration::from_secs(60))
    .http2_keep_alive_interval(std::time::Duration::from_secs(20))
    .http2_keep_alive_while_idle(true)
    .tcp_nodelay(true)
    .timeout(std::time::Duration::from_secs(5))
    .use_rustls_tls()
    .build()?;

backoff::future::retry(backoff::ExponentialBackoff::default(), || async {
    limiter.until_ready().await;
    let bytes = client.get(url).send().await?.bytes().await?;
    // 热路径用 simd-json
    let mut buf = bytes.to_vec();
    let parsed: GameOdds = simd_json::serde::from_slice(&mut buf)
        .map_err(|e| backoff::Error::permanent(e))?;
    Ok(parsed)
}).await
```

**禁用**:
- `reqwest` 的 `default-tls` (会拉 native-tls). 必须 `rustls-tls`
- `serde_json` 在热路径解析 ≥ 1 KB JSON. ≥ 1KB 走 `simd-json`
- `tokio::spawn_blocking` 在 IO 路径 (除非确实是 CPU bound)

### 2.2 WSS (Polymarket clob WSS + Goalserve inplay WSS)

| Crate | 版本 | 性能 | 备注 |
|---|---|---|---|
| **`tokio-tungstenite`** | `=0.23.1` | ~200k msg/s 单连接 (本地) | rustls backend; 与 reqwest 共享 TLS 配置 |
| **`futures-util`** | `=0.3.30` | — | `StreamExt::next` / `forward` / `try_for_each` 不可少 |

骨架 (与老陈 WSS 实测 + 小段 Goalserve WSS 对齐):

```rust
use futures_util::{StreamExt, SinkExt};
use tokio_tungstenite::{connect_async, tungstenite::Message};

let (ws, _) = connect_async(url).await?;
let (mut tx, mut rx) = ws.split();

// 关键: 心跳 task 独立 — 不放主 stream loop, 防止背压挂死
tokio::spawn(async move {
    let mut interval = tokio::time::interval(std::time::Duration::from_secs(20));
    loop {
        interval.tick().await;
        if tx.send(Message::Ping(vec![])).await.is_err() { break; }
    }
});

while let Some(msg) = rx.next().await {
    match msg? {
        Message::Binary(b) => handle_binary(b),
        Message::Text(s)   => handle_text(s),
        Message::Ping(_) | Message::Pong(_) => continue,
        Message::Close(_) => break,
        _ => continue,
    }
}
```

**已知 issue**:
- `tokio-tungstenite` 0.23 与 `rustls` 0.23 配套, 升级要锁版本一致
- WSS 自动 ping/pong 不可靠, 业务层必须自己定 heartbeat (上面例子) — 与老陈 `wss_reconnect.py` 经验一致

### 2.3 性能 benchmark (核心栈)

| Crate | 用途 | 何时用 | 输出 |
|---|---|---|---|
| **`criterion`** | 统计严谨 micro/macro bench | 默认全部 bench | HTML 报告 + p50/p95/p99 + 回归对比 |
| **`iai`** | cachegrind 精准 (instructions / L1/L2/L3 / branches) | 平台无关回归 (CI 跨机) | 单 baseline 数字 |
| **`hyperfine`** (CLI) | 跨进程 CLI 时间 | end-to-end binary 启动 / batch 跑 | 多次 warmup + p99 |
| **`pprof-rs`** | 火焰图 | 性能分析 (不进 CI) | SVG flamegraph |

**criterion 模板** (与老姜 latency budget §1 对齐):

```rust
// crates/stcpp-bench/benches/wss_parse.rs
use criterion::{criterion_group, criterion_main, Criterion, BenchmarkId, Throughput, black_box};
use stcpp_api_client::polymarket::parse_book_update;

fn bench_parse(c: &mut Criterion) {
    let fixtures = load_fixtures("data/polymarket_book_updates.jsonl");
    let mut group = c.benchmark_group("polymarket/book_update_parse");
    group.throughput(Throughput::Bytes(fixtures[0].len() as u64));
    // 老姜 latency budget: parse p99 < 50us
    group.significance_level(0.01).sample_size(500);

    for (i, payload) in fixtures.iter().enumerate().take(8) {
        group.bench_with_input(BenchmarkId::from_parameter(i), payload, |b, p| {
            let mut scratch = Vec::with_capacity(p.len() + 64);
            b.iter(|| {
                scratch.clear();
                scratch.extend_from_slice(p);
                black_box(parse_book_update(&mut scratch).unwrap())
            });
        });
    }
    group.finish();
}

criterion_group!(benches, bench_parse);
criterion_main!(benches);
```

**iai 模板** (CI 回归门禁用):

```rust
// crates/stcpp-bench/benches/parse_iai.rs
use iai::black_box;

fn parse_book() {
    let payload = include_bytes!("../../data/sample_book.json").to_vec();
    let mut buf = payload.clone();
    let _ = black_box(simd_json::serde::from_slice::<serde_json::Value>(&mut buf));
}

iai::main!(parse_book);
```

**CI 回归门禁标准** (与老姜对齐):
- criterion `--save-baseline main`, PR 跑 `--baseline main`
- 任何 bench 平均值 **回归 > 5%** → CI 阻断
- iai 任何指标 (instructions / branches / cache) **回归 > 2%** → CI 阻断 (iai 比 criterion 稳)
- 新增 bench: 必须附 p50 / p99 / mean / stddev 表

### 2.4 并发 (重点章节, §4 详)

| Crate | 用途 | 选型理由 |
|---|---|---|
| **`tokio`** | 异步 IO bound (HTTP / WSS / IPC) | 业界标准, multi-thread runtime, work-stealing |
| **`rayon`** | CPU bound (parquet 解码 / 矩阵运算 / batch parse) | data parallel, `par_iter()` 一行换并行 |
| **`crossbeam-channel`** | std thread 间消息传递 (有锁但 fast path 自旋) | 比 `std::sync::mpsc` 性能高 3-5x; SPSC / SPMC / MPMC 同 API |
| **`crossbeam-queue`** | lock-free queue (`ArrayQueue` SPMC, `SegQueue` unbounded MPMC) | 与 C++ 端 rigtorp/moodycamel 语义对齐 (小石 v1) |
| **`parking_lot`** | `Mutex` / `RwLock` 替代 std (快 2-3x, 无 poisoning) | 业界共识替代 std::sync |
| **`core_affinity`** | CPU pinning | 与 C++ 端 thread pinning 一致 |

### 2.5 数据 / DataFrame / 列式存储

| Crate | 用途 | 性能 vs Python | 备注 |
|---|---|---|---|
| **`polars`** | 列式 DataFrame (lazy + streaming) | **5-50x pandas**, 内存 1/4 ~ 1/10 | Rust 原生; SQL-like; 与 pyo3 互通 (回测 notebook 可读) |
| **`parquet2`** + `arrow2` | Parquet 读写 | 比 pyarrow 同等或快; 零依赖 | polars 内部用; 也可独立用做 ETL pipeline |
| **`zerocopy`** | 零拷贝 byte ↔ struct 转换 | 编译期保证 layout (`FromBytes` / `AsBytes`) | 替代 `bytemuck`; 更严 derive 检查 |
| **`bytes`** | 引用计数 byte buffer (`Bytes`, `BytesMut`) | 网络协议 / WSS frame 零拷贝必备 | tokio 生态默认 |
| **`memmap2`** | mmap 大文件 (历史 tick replay, parquet 直读) | 比 fread 快 ~3x 顺序; 比 read 快 ~10x 随机 | 老王 WAL 可同款 |
| **`bincode`** | 紧凑二进制 serde (内部 IPC) | 比 JSON 5-10x 快, 体积 1/5 | 不暴露给外部, 仅 Rust 进程间 |
| **`rmp-serde`** | MessagePack | signer IPC (老孙) 同款; 跨语言 (C++ 也有 lib) | — |

**polars 性能基准** (官方 + 我们样本):
- 1 GB CSV → DataFrame: pandas 28s / polars 1.4s (~20x)
- groupby + agg 100M rows: pandas 14s / polars 0.4s (~35x)
- lazy + streaming 10 GB parquet 不爆内存 (pandas 直接 OOM)

**给小蒋 (回测) 的迁移指南见 §13.3**.

### 2.6 加密 (已老张 signer v1 选型, 此处仅引用)

详见 `laozhang-rust-signer-crates-v1.md`:
- ECDSA: **`k256`** (RustCrypto, audit clean)
- Keccak: **`sha3`**
- TLS: **`rustls`** + `aws-lc-rs` backend
- 零拷贝清零: **`zeroize`** + **`secrecy`**
- 常时比较: **`subtle`**
- 供应链审计: **`cargo-vet`** (主) + `cargo-audit` + `cargo-deny`

签名相关变更必须 @老孙 + @老沈 联签.

### 2.7 数值 / 统计 (替代 numpy / scipy 离线探索)

| Crate | 用途 | vs Python | 备注 |
|---|---|---|---|
| **`nalgebra`** | 线性代数 (Vec / Mat / 分解) | 单线程慢些, SIMD 后接近; 但 zero-cost 集成 | 替代 numpy 在编译型环境 |
| **`statrs`** | 概率分布 / 假设检验 (类 scipy.stats) | 大致持平 | t-test / KS / 分布 PDF/CDF/quantile |
| **`ndarray`** | n 维数组 (类 numpy) | 与 nalgebra 互补; BLAS backend 可接 | 大批量数值优先 ndarray + BLAS |
| `argmin` (可选) | 数值优化 | — | Kelly 求解 / MLE; 小肖可能用 |

**给小袁 (市场微结构) 的迁移指南见 §13.5**.

### 2.8 日志 / 观测 / metric

| Crate | 用途 | 备注 |
|---|---|---|
| **`tracing`** | 结构化日志 facade | 必须 (与 signer + observability 联签) |
| **`tracing-subscriber`** | 收集器 + JSON formatter | 输出到 Loki / stdout |
| **`prometheus-client`** | Prometheus metric (Rust 官方) | 与小郑 `prometheus-cpp` 同协议; OpenMetrics 标准 |
| **`opentelemetry`** + `opentelemetry-otlp` | OTel trace export 到 Tempo | 与小郑 OTel SDK 选型对齐 |

**与 observability 集成铁律** (与小郑 §3 对齐):
- metric 命名遵循小郑 §5 约定 (`stcpp_<layer>_<metric>_<unit>`)
- trace span 标签遵循 OTel 语义约定 (`http.method`, `net.peer.name`, ...)
- log 输出 JSON, 字段名与小郑 Loki label schema 一致 (`level`, `target`, `request_id`, ...)

### 2.9 CLI / 错误

| Crate | 用途 |
|---|---|
| **`clap`** | CLI 参数解析, `derive` macro 一行定义 |
| **`anyhow`** | 顶层 main / bin 错误 (只用在 main, 不污染 lib) |
| **`thiserror`** | 库内部自定义 error enum (派生 `Error` trait) |

**铁律**:
- 库 crate (`stcpp-*`) **只用 `thiserror`**, 暴露具名 enum
- binary crate (`stcpp-bench`, `stcpp-loadgen`) 顶层 `main` 才允许 `anyhow::Result<()>`
- 中间层禁止 `Box<dyn Error>` (错误吞噬)

### 2.10 测试

| Crate | 用途 | 何时用 |
|---|---|---|
| **`proptest`** | property-based test (随机生成输入, shrink 反例) | 解析器 / 状态机 / 数值 invariant |
| **`fake`** | fixture / mock data (Faker for Rust) | API client 测试 fixture |
| `rstest` (可选) | parametrized test | — |
| `wiremock` (可选) | HTTP mock server | API client integration test |

**proptest 例**:

```rust
proptest! {
    #[test]
    fn parse_then_reserialize_is_idempotent(
        bid_price in 0.0001f64..1.0,
        bid_size in 1u64..1_000_000,
    ) {
        let original = BookLevel { price: bid_price, size: bid_size };
        let bytes = serialize(&original);
        let back = parse(&bytes).unwrap();
        prop_assert!((back.price - original.price).abs() < 1e-9);
        prop_assert_eq!(back.size, original.size);
    }
}
```

---

## 3. 性能基准模板 (criterion + 给 in-flight agents 的 starter)

### 3.1 总原则

| 原则 | 怎么做 |
|---|---|
| **每个 PR 必跑 bench** | CI 跑 `cargo bench --no-run` 编译; 关键 bench 在 nightly 跑实测 |
| **回归门禁** | criterion baseline diff, 平均 > 5% 回归阻断 |
| **fixture 入 git** | 真实样本 (老陈 / 小段已采) 进 `data/`, 单元测试 / bench 共享 |
| **JIT / 缓存预热** | criterion 默认 100 warmup; 自定义高方差路径加 `warm_up_time(Duration::from_secs(3))` |
| **结果归档** | `target/criterion/*` 在 release 时归档到 `docs/RESEARCH/data/bench/{date}-{crate}/` |

### 3.2 starter 给老陈 (network bench, 替换 wss_bench.py)

`crates/stcpp-bench/benches/wss_e2e_latency.rs`:

```rust
use criterion::{criterion_group, criterion_main, Criterion};
use std::time::{Duration, Instant};
use tokio::runtime::Runtime;
use futures_util::StreamExt;

fn bench_polymarket_wss_first_msg(c: &mut Criterion) {
    let rt = Runtime::new().unwrap();
    let mut group = c.benchmark_group("wss/polymarket_first_message");
    group.sample_size(30);          // 重连成本高, 不能 100 次
    group.measurement_time(Duration::from_secs(60));

    group.bench_function("connect_to_first_book_update", |b| {
        b.to_async(&rt).iter_custom(|iters| async move {
            let mut total = Duration::ZERO;
            for _ in 0..iters {
                let start = Instant::now();
                let (ws, _) = tokio_tungstenite::connect_async(
                    "wss://ws-subscriptions-clob.polymarket.com/ws/market"
                ).await.unwrap();
                let (_, mut rx) = ws.split();
                let _first = rx.next().await;   // 第一帧 (subscribe ack or push)
                total += start.elapsed();
            }
            total
        });
    });
    group.finish();
}

criterion_group!(benches, bench_polymarket_wss_first_msg);
criterion_main!(benches);
```

**老陈, 用这个替换 Python `wss_bench.py`. Python 不能给真实的并发 / 重连压力 (GIL + asyncio quirks). Rust 这个跑 100 次重连串行, 你能拿到稳定的 p99**.

### 3.3 starter 给小袁 (微观结构 / orderbook 压力)

`crates/stcpp-bench/benches/orderbook_apply.rs`:

```rust
use criterion::{criterion_group, criterion_main, Criterion, BatchSize, Throughput};
use stcpp_common::OrderBook;
use stcpp_replay::load_book_diffs;

fn bench_apply(c: &mut Criterion) {
    let diffs = load_book_diffs("data/nba_2025_01_15_2h.bin");  // 2h 真实 diff
    let mut group = c.benchmark_group("microstructure/orderbook_apply");
    group.throughput(Throughput::Elements(diffs.len() as u64));
    group.bench_function("apply_2h_nba", |b| {
        b.iter_batched(
            || OrderBook::new(1024),                    // setup, 不计时
            |mut book| {
                for d in &diffs {
                    book.apply(d);                       // hot loop
                }
                book
            },
            BatchSize::LargeInput,
        );
    });
    group.finish();
}

criterion_group!(benches, bench_apply);
criterion_main!(benches);
```

**小袁, 你的市场微结构分析 (top-of-book persistence / spread stickiness / 大单 imprint), 在 Rust + polars 上跑 1 个月数据应该 < 30 秒, 比 pandas 块 30x. 出图用 polars → DataFrame → pyo3 转 pandas (临时), 长期 plotters 直接出 SVG**.

### 3.4 starter 给小蒋 (回测引擎)

`crates/stcpp-bench/benches/backtest_replay.rs`:

```rust
use criterion::{criterion_group, criterion_main, Criterion};
use stcpp_replay::ReplayEngine;
use stcpp_common::strategies::PinnacleNoVig;

fn bench_replay(c: &mut Criterion) {
    let mut group = c.benchmark_group("backtest/pinnacle_no_vig");
    group.sample_size(10);
    group.measurement_time(std::time::Duration::from_secs(120));

    group.bench_function("nba_2025_q1_replay", |b| {
        b.iter(|| {
            let mut engine = ReplayEngine::from_parquet("data/nba_2025_q1.parquet").unwrap();
            let mut strat = PinnacleNoVig::default();
            engine.run(&mut strat).unwrap();
            strat.metrics()
        });
    });
    group.finish();
}

criterion_group!(benches, bench_replay);
criterion_main!(benches);
```

**小蒋, 你 v0.1 是 Python + DuckDB. GM 钦定后, 我建议:
- **回测引擎核心 (replay loop, RM 模拟, signal apply) 必须 Rust** — 这是性能瓶颈, Python 跑 1 个月 NBA 数据要 1 小时+, Rust < 5 分钟
- **特征 / 标签计算用 polars** (替代 pandas) — lazy frame 一行 SQL-like 出 alpha decay 曲线
- **报告生成 (HTML / 图) 还可以保留 Python notebook** — 这是离线展示, 不是性能路径**
- 详细迁移路径见 §13.4

### 3.5 starter 给小段 (Goalserve API 限流测试)

`crates/stcpp-loadgen/src/bin/goalserve_burst.rs` (不是 bench 是 loadgen):

```rust
use clap::Parser;
use governor::{Quota, RateLimiter};
use std::num::NonZeroU32;
use std::sync::Arc;
use std::time::Instant;

#[derive(Parser)]
struct Args {
    #[arg(long, default_value = "10")]
    rps: u32,
    #[arg(long, default_value = "300")]
    duration_secs: u64,
    #[arg(long)]
    endpoint: String,
}

#[tokio::main(flavor = "multi_thread", worker_threads = 4)]
async fn main() -> anyhow::Result<()> {
    let args = Args::parse();
    let limiter = Arc::new(RateLimiter::direct(
        Quota::per_second(NonZeroU32::new(args.rps).unwrap())
    ));
    let client = Arc::new(reqwest::Client::builder()
        .use_rustls_tls()
        .tcp_nodelay(true)
        .build()?);

    let start = Instant::now();
    let deadline = start + std::time::Duration::from_secs(args.duration_secs);
    let mut latencies = Vec::with_capacity(args.rps as usize * args.duration_secs as usize);

    while Instant::now() < deadline {
        limiter.until_ready().await;
        let c = client.clone();
        let url = args.endpoint.clone();
        let t = tokio::spawn(async move {
            let t0 = Instant::now();
            let r = c.get(&url).send().await;
            (t0.elapsed(), r.map(|x| x.status().as_u16()).ok())
        });
        latencies.push(t);
    }

    // 收集 + 输出 p50/p95/p99
    let results: Vec<_> = futures_util::future::join_all(latencies).await;
    let mut ts: Vec<u128> = results.iter().filter_map(|r| r.as_ref().ok())
        .map(|(d, _)| d.as_micros()).collect();
    ts.sort();
    let p = |q: f64| ts[(ts.len() as f64 * q) as usize];
    println!("p50={}us p95={}us p99={}us total={}", p(0.5), p(0.95), p(0.99), ts.len());
    Ok(())
}
```

**小段, 把 Goalserve 限流测试从 `bench_helpers.sh` 迁到这个. Rust 能给真实并发 + 真实 backpressure. 跑 `cargo run --release --bin goalserve_burst -- --rps 20 --duration-secs 600 --endpoint <url>`, 输出可信的 p99**.

---

## 4. 多线程纪律 (用户高优!)

### 4.1 总决策表

| 场景 | 用什么 | 不用什么 | 原因 |
|---|---|---|---|
| HTTP / WSS / IPC IO | **`tokio`** (multi-thread) | rayon, std::thread | 异步 IO 必须 reactor; std thread 一连接一线程在 1k+ 连接下崩 |
| CPU bound 数据并行 (parquet decode, batch parse, 矩阵) | **`rayon`** (`par_iter`) | tokio::spawn_blocking 大量并发 | rayon 有 work-stealing 调度, CPU 利用率更稳; spawn_blocking 池有限 (默认 512) |
| Lock-free queue (替代 channel) | **`crossbeam-queue::ArrayQueue` / `SegQueue`** | std::sync::mpsc, tokio::sync::mpsc (CPU bound 场景) | crossbeam 性能 5-10x std, MPMC native, 接近 rigtorp 性能 |
| 线程间消息 (std thread) | **`crossbeam-channel`** | std::sync::mpsc | std mpsc 单 receiver 锁竞争慢 |
| async task 间消息 | **`tokio::sync::mpsc`** (bounded) | unbounded! 见 §6 背压 | bounded 才能背压 |
| 共享只读状态 | **`Arc<T>`** 或 **`arc_swap::ArcSwap`** | `Arc<RwLock<T>>` 在读多写极少场景 | RwLock 写锁阻塞读, ArcSwap 是 lock-free 全替换 |
| 共享可变状态 (小区段) | **`parking_lot::Mutex`** | `std::sync::Mutex` | parking_lot 快 2-3x, 无 poisoning, 体积小 |
| 共享读多写少 | **`parking_lot::RwLock`** 或 **`arc_swap`** | std::sync::RwLock | 同上 |
| Atomic 单字段 | **`std::sync::atomic`** 或 **`crossbeam-utils::CachePadded`** | Mutex<bool> 这种荒唐写法 | atomic 是 lock-free 唯一正解 |

### 4.2 tokio runtime 配置 (与老姜 budget 对齐)

```rust
// API client / WSS 主进程
#[tokio::main(flavor = "multi_thread", worker_threads = 4)]
async fn main() -> anyhow::Result<()> {
    // ...
}

// 或者手动:
let rt = tokio::runtime::Builder::new_multi_thread()
    .worker_threads(4)
    .max_blocking_threads(32)
    .thread_name("stcpp-rt")
    .enable_all()
    .on_thread_start(|| {
        // CPU pinning (与 C++ 端同款)
        let core_ids = core_affinity::get_core_ids().unwrap();
        let i = NEXT_CORE.fetch_add(1, Ordering::Relaxed) % core_ids.len();
        core_affinity::set_for_current(core_ids[i]);
    })
    .build()?;
```

**worker_threads 取值**: API client 路径 4 (IO bound, 数量小于 core 数防 cache 抖动); ETL 路径用 rayon 默认 (= core 数); signer 单线程同步 (不上 tokio).

### 4.3 rayon 用法

```rust
use rayon::prelude::*;

// 1M parquet rows, 计算 feature
let features: Vec<Feature> = rows
    .par_iter()
    .map(|row| compute_feature(row))
    .collect();

// 自定义线程池 (隔离 ETL 不干扰 API client)
let pool = rayon::ThreadPoolBuilder::new()
    .num_threads(4)
    .thread_name(|i| format!("etl-{}", i))
    .build()?;
pool.install(|| {
    rows.par_iter().map(...).collect()
});
```

### 4.4 lock-free 原则 (与小石 v1 对齐)

| 原则 | Rust 实现 |
|---|---|
| 队列容量 2 的幂 | `crossbeam_queue::ArrayQueue::new(1024)` (1024 = 2^10) |
| Cache line padding | `crossbeam_utils::CachePadded<T>` 或 `#[repr(align(64))]` (Apple Silicon 128) |
| `is_lock_free` 编译期 assert | `const _: () = assert!(AtomicU64::is_lock_free());` (实际上 const 不行, 用 `#[test]` 兜底) |
| 生产 / 消费 idx 分 cache line | `CachePadded<AtomicUsize>` head + tail |
| 不要 spin-loop 无 backoff | `crossbeam_utils::Backoff::snooze()` |

**`Arc<Mutex<T>>` vs `Arc<RwLock<T>>` vs `Atomic*` 决策树**:

```
共享数据是单原子值 (u64/ptr/enum 4B)?
  ├─ 是 → Atomic* (Relaxed/Acquire/Release 按需)
  └─ 否 ↓
读 vs 写比例?
  ├─ 写多 (≥ 10% 写) → parking_lot::Mutex
  ├─ 读多 (≤ 1% 写, 小数据) → arc_swap::ArcSwap<T>
  └─ 读多 (≤ 10% 写, 大数据) → parking_lot::RwLock
关键路径 ≤ 100ns?
  └─ 全替换为 lock-free 数据结构 (crossbeam) 或重新设计 (SPSC 队列)
```

### 4.5 false sharing 防范

```rust
use crossbeam_utils::CachePadded;

struct WorkerCounters {
    in_flight: CachePadded<AtomicU64>,
    completed: CachePadded<AtomicU64>,
    errors:    CachePadded<AtomicU64>,
}
```

**或手动**:
```rust
#[repr(align(64))]   // Linux/x86: 64; Apple Silicon: 128 (用 cfg 区分)
struct Padded<T>(pub T);
```

**CI 验证**: 用 `perf c2c` 或 `cargo-show-asm` 看变量布局, 季度审 hot struct.

### 4.6 NUMA / CPU pinning

`core_affinity = "=0.8.1"`. 跨平台 (Linux + macOS + Windows). 用于 API client / ETL worker:

```rust
use core_affinity::CoreId;

let cores = core_affinity::get_core_ids().unwrap();
// 我们的 trader 是 4 core 部署, 留 core 0 给 OS / kernel
let workers = &cores[1..];

for (i, &core) in workers.iter().enumerate() {
    std::thread::Builder::new()
        .name(format!("etl-{}", i))
        .spawn(move || {
            core_affinity::set_for_current(core);
            run_worker();
        })?;
}
```

NUMA 跨节点: 当前部署单 socket, 不必 numactl. 上多 socket 时引入 `hwloc` crate 评估.

---

## 5. 内存管理 (用户高优!)

### 5.1 零拷贝栈

| 工具 | 场景 | 例 |
|---|---|---|
| **`bytes::Bytes`** | 引用计数 byte buffer, 切片 / clone 不复制 | WSS frame; HTTP body |
| **`bytes::BytesMut`** | 可写 byte buffer, 写满转 `Bytes` 零拷贝 | 协议组帧 |
| **`zerocopy::FromBytes` / `AsBytes`** | 编译期验证 struct ↔ &[u8] 安全转换 | 网络协议 fixed-size header, parquet row decode |
| **`memmap2::Mmap`** | mmap 大文件直读 | 历史 tick replay, parquet 直读 |
| **`std::io::Cursor<&[u8]>`** | 切片当作 reader | serde decode 不分配 |

**bytes 例 (WSS 帧零拷贝转给 worker 队列)**:

```rust
use bytes::Bytes;

while let Some(msg) = ws_rx.next().await {
    match msg? {
        Message::Binary(vec) => {
            let frame: Bytes = vec.into();   // Vec<u8> → Bytes, 零拷贝 (move)
            queue.send(frame).await?;        // clone Bytes 也是零拷贝 (ref count)
        }
        _ => continue,
    }
}
```

**zerocopy 例 (协议 header 解析)**:

```rust
use zerocopy::{FromBytes, AsBytes, Unaligned};

#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
struct WireHeader {
    magic: [u8; 4],
    version: u8,
    msg_type: u8,
    length: U16Be,
}

let header = WireHeader::ref_from_prefix(&buf[..]).unwrap();
let payload = &buf[std::mem::size_of::<WireHeader>()..];
// 整个 parse 没分配, 没拷贝
```

### 5.2 Arena allocator

| Crate | 用途 | 何时用 |
|---|---|---|
| **`bumpalo`** | bump arena (一次分配, 整体释放) | 单次请求生命周期内的 N 个小对象 (parse 出的 1k+ 节点); 用完整 reset |
| **`typed-arena`** | typed bump arena (类型同质) | AST 节点; trade tree node |

**例 (parser 一次性 arena)**:

```rust
use bumpalo::Bump;

fn parse_book_update(payload: &[u8]) -> ParsedBook<'_> {
    let arena = Bump::with_capacity(8 * 1024);   // 8K 一次到位
    let bids: Vec<&'_ Level> = parse_levels_into(&arena, &payload[..]);
    let asks = parse_levels_into(&arena, ...);
    ParsedBook { bids, asks, _arena: arena }      // 整 book 用完丢, 不一个个 drop
}
```

**性能**: 100 个小对象, malloc 路径 ~4-6 us, bumpalo ~50 ns (~100x).

### 5.3 `Box<[u8]>` vs `Vec<u8>` vs `Bytes` 取舍

| 类型 | 大小 | 何时用 |
|---|---|---|
| `[u8; N]` (栈) | 24 字节 ptr 都没有 | N 已知且 < 4 KB |
| `Box<[u8; N]>` (堆) | 8 字节 ptr | N 大 + 已知, 不需要 grow |
| `Box<[u8]>` (堆) | 16 字节 (ptr + len) | runtime 大小, 不需要 grow, 比 Vec 省 8B 没 capacity 字段 |
| `Vec<u8>` | 24 字节 (ptr + len + cap) | 需要 push / extend / 不知大小 |
| `Bytes` | 32 字节 (内部 inline + ref counted) | 跨边界传 (worker → ipc), 不可变 |
| `BytesMut` | 32 字节 | 协议组帧, 写满转 `Bytes` |

**规则**:
- 热路径接收 / 解析完成后: `Vec<u8>` → `Bytes` (`.freeze()` 或 `.into()`)
- 内部 const-size buffer (固定 1500 bytes MTU 等): `Box<[u8; 1500]>`
- 不知大小的 parse 输出: `Box<[T]>` (用 `Vec::into_boxed_slice()`)

### 5.4 全局分配器

| 分配器 | 性能 | RSS | 适用 |
|---|---|---|---|
| `jemalloc` (`tikv-jemallocator`) | 高 throughput | 高 (碎片化) | 大量小对象, 多线程 |
| **`mimalloc`** (`mimalloc` crate) | 高 throughput + 低延迟 | **低** (紧凑) | 默认推荐 |
| `snmalloc` (`snmalloc-rs`) | 极高 throughput | 中 | 实验性, 大对象多 |
| 默认 (system / glibc) | 中等 | 中等 | 仅 dev / 不在意时 |

**选 mimalloc**: Microsoft 出品, MIT 开源, 性能不输 jemalloc 但 RSS 低 30%-50% (跨洋部署内存贵), 跨平台稳.

```rust
// crates/stcpp-api-client/src/main.rs (binary)
#[global_allocator]
static GLOBAL: mimalloc::MiMalloc = mimalloc::MiMalloc;
```

**禁用**: 不用 `dhat` 之类做生产分配 profiling (太慢). 用 `bytehound` 或 `heaptrack`.

---

## 6. 大数据量高频并行 (用户高优!)

### 6.1 数据流式处理 (Stream + bounded mpsc)

**铁律 (R-Rust-7)**: 不知道大小的 input 流, **必须用 `Stream` + bounded channel + backpressure**, 严禁 `collect::<Vec<_>>()`.

```rust
use futures_util::StreamExt;
use tokio::sync::mpsc;

// 24h Goalserve inplay 数据流, 不知道总量 (可能 1M+ events)
let (tx, mut rx) = mpsc::channel::<RawEvent>(1024);  // bounded! 满了背压上游

// 上游: WSS receive
tokio::spawn(async move {
    let mut ws = connect_wss().await?;
    while let Some(msg) = ws.next().await {
        // tx.send 在 channel 满时会 await, 自动 backpressure 到 WSS read
        if tx.send(parse_event(msg?)).await.is_err() { break; }
    }
    Ok::<_, anyhow::Error>(())
});

// 下游: parser + polars 流式
let mut batch = Vec::with_capacity(512);
while let Some(event) = rx.recv().await {
    batch.push(event);
    if batch.len() >= 512 {
        // 批量写 parquet (减少 fsync 调用, 与老王 WAL batch fsync 思路一致)
        write_parquet_batch(&batch)?;
        batch.clear();
    }
}
```

### 6.2 批量化 (batch parse / batch fsync / batch write)

| 操作 | 单次成本 | batch 512 后 | 加速 |
|---|---|---|---|
| `Parquet write_row()` (单行) | ~80 us | batch 512: ~2 ms / 512 = 4 us/行 | 20x |
| `fsync()` | ~3 ms | batch 512: 3 ms / 512 = 6 us/行 | 500x |
| simd-json parse 单 doc | 15 us | batch parse 多 doc (复用 tape): 8 us | 2x |
| WSS frame decode | 2 us | batch 100 帧到 `Vec<Bytes>` 再 parse: 1.5 us | 1.3x |

**实现**: 业务路径必须有 batch 概念, 不允许 "解析一条 / 写一条 / fsync 一次" 的实现.

### 6.3 背压 (backpressure) 策略

| 策略 | 何时 | Rust 实现 |
|---|---|---|
| **bounded channel** (满则上游 await) | 大多数 IO pipeline | `tokio::sync::mpsc::channel(N)` |
| **drop oldest** (满则丢旧) | 实时 metric, 不能 backlog | 自写 ring buffer + atomic counter |
| **drop newest** (满则丢新) | 不关心末尾 | 同上 |
| **降级** (退化到采样) | 不能丢但 hot path 不能堵 | sentry-style sampling |

**严禁**:
- `mpsc::unbounded_channel()` (内存爆炸入口)
- `tokio::spawn` 在 hot loop 无 join handle (task 失控泄漏)
- `Vec::extend` 在不知 input 大小的 stream 上 (OOM)

### 6.4 polars lazy + streaming (大数据集 ETL)

```rust
use polars::prelude::*;

// 10 GB 历史 tick parquet, 单机 16 GB 内存
let q = LazyFrame::scan_parquet("history/nba_2025_*.parquet", ScanArgsParquet::default())?
    .filter(col("event_type").eq(lit("trade")))
    .group_by([col("game_id"), col("market_id")])
    .agg([
        col("price").mean().alias("avg_price"),
        col("size").sum().alias("total_volume"),
        col("ts_ns").max().sub(col("ts_ns").min()).alias("session_ns"),
    ])
    .sort("total_volume", SortOptions::default().with_order_descending(true));

// streaming = true: polars 不一次性 load, 分块流式跑
let df = q.with_streaming(true).collect()?;
df.write_parquet(Path::new("out.parquet"), ParquetWriteOptions::default(), None)?;
```

**给小蒋 (回测)**: 这套 lazy + streaming 是你 v0.1 → v0.2 升级的核心, 把 DuckDB 步骤替换. polars 直接出 DataFrame, 不需要落 DuckDB.

### 6.5 SIMD 数据并行

| 工具 | 稳定性 | 用法 |
|---|---|---|
| `std::simd` (nightly) | nightly only | 实验, 不进生产 |
| **`wide`** | stable | `f64x4`, `u32x8`; 自动 fallback 标量; 跨平台 |
| 手写 `core::arch::x86_64` | stable, unsafe | 性能极限, 必须配 cfg + 标量 fallback |

**例** (price ladder vectorize, 与老姜 §1 阶段 6 对齐):

```rust
use wide::f64x4;

fn ladder_apply_offset(prices: &mut [f64], offset: f64) {
    let v_off = f64x4::splat(offset);
    let chunks = prices.chunks_exact_mut(4);
    let rem = chunks.remainder();
    for c in chunks {
        let v = f64x4::from(<[f64; 4]>::try_from(&c[..]).unwrap());
        let v2 = v + v_off;
        c.copy_from_slice(&v2.to_array());
    }
    for x in rem { *x += offset; }
}
```

---

## 7. 框架性能纪律

### 7.1 编译选项

`.cargo/config.toml`:

```toml
[build]
# 默认 dev / release 不开 native CPU (二进制要跨机部署); 仅 bench 开
rustflags = ["-D", "warnings"]

# bench / 本地优化 build:
# cargo build --release --target x86_64-unknown-linux-gnu --config 'build.rustflags=["-C","target-cpu=native"]'
```

| flag | 默认 | bench 开启 | 生产 |
|---|---|---|---|
| `opt-level = 3` | release ✓ | ✓ | ✓ |
| `lto = "fat"` | (本 workspace 已配) | ✓ | ✓ |
| `codegen-units = 1` | (本 workspace 已配) | ✓ | ✓ |
| `target-cpu=native` | ✗ (不跨机) | ✓ | 按 target 编 (`x86-64-v3` 一般够) |
| `panic = "abort"` | (本 workspace 已配) | ✓ | ✓ |
| `overflow-checks` | release: off | ON (catch bug) | ON |
| `debug = "line-tables-only"` | bench profile 已配 | ✓ | ✗ (生产 strip) |

### 7.2 PGO (Profile-Guided Optimization)

**何时上**: 当 bench 已经做到极限 (criterion p99 收敛), 还需要再压 5-15% 时.

**怎么做**:
```bash
# 1. Instrument build
RUSTFLAGS="-Cprofile-generate=/tmp/pgo-data" cargo build --release --bin stcpp-api-client
# 2. 跑真实 workload (replay 1h 真实 tick)
./target/release/stcpp-api-client --replay data/nba_2025_01_15_1h.bin
# 3. Merge profile
llvm-profdata merge -o /tmp/pgo.profdata /tmp/pgo-data
# 4. Use build
RUSTFLAGS="-Cprofile-use=/tmp/pgo.profdata" cargo build --release --bin stcpp-api-client
```

**典型增益**: 5-15% (热路径分支重定向); 用过 PGO 的项目: rustc 自身, ClickHouse.

**门禁**: PGO build 必须跑 full regression bench, 防止 profile 偏差导致回归.

### 7.3 `unsafe` 红线

| 谁能写 | 评审 | 必须 |
|---|---|---|
| 老张 / 老孙 / 小石 / 老周 (4 人) | 双人 review (写者 + 另 1 人) | `// SAFETY:` 注释 + 列举所有 invariant |

**模板**:
```rust
// SAFETY:
// - `ptr` 来自上一行 `Vec::as_ptr()`, 非空且对齐 align_of::<u64>()
// - `len` <= `vec.len()` (上面 if 已校验)
// - `'a` 生命周期不超过 vec, vec 在本函数作用域内不释放
// - 不会被其他线程并发写, 因为 vec 是 &mut self 独占
unsafe { std::slice::from_raw_parts(ptr, len) }
```

**禁止 unsafe 场景**:
- 单纯避开 borrow checker → 重构成 safe
- 调用 C API → 包装成 safe wrapper, 不允许 unsafe 散布

### 7.4 "热路径无分配" 铁律

热路径定义 (与老姜 budget 对齐): WSS message handler / IPC server handler / 信号 compute / 风控 check.

| 不允许 | 允许 |
|---|---|
| `Box::new(x)` | 预分配 `Vec<T>` 复用 |
| `String::new()` / `format!()` | `&str` slicing; `write!` 到预分配 buf |
| `Vec::new()` 后 push | `Vec::with_capacity(N)` 一次到位 |
| `vec![0u8; N]` | 复用 `BytesMut::with_capacity(N).resize(N, 0)` |
| `to_string()` 错误处理 | enum error type (`thiserror`) |
| 闭包 captures heap (`Box<dyn Fn>`) | 静态 dispatch / impl Trait |

**CI 验证**: 关键 bench `criterion --measurement-time 30s` 跑后, `dhat` 或自定义 GlobalAlloc counter 校验 "0 allocation in hot loop". 见 §11.

---

## 8. 与 C++ 的 FFI / IPC 边界

### 8.1 决策: 起步阶段不混编

| 选项 | 决策 | 理由 |
|---|---|---|
| **cbindgen** (Rust → C header) | 不上 | 当前没有 "Rust lib 嵌 C++ 进程" 场景 |
| **cxx-rs** (Rust ↔ C++ 双向) | 不上 | 同上 |
| **bindgen** (C/C++ → Rust 绑定) | 不上 | 只有签名相关用到的小量 C 库 (如 aws-lc-rs); 已 audit 过 |
| **独立进程 + UDS / TCP** | **采用** | 起步阶段最简单, 失败 blast radius 小 |
| **独立进程 + SHM** | 采用 (大对象) | 老王 WAL 同款; ETL 数据落到 SHM 给 trader 读 |

### 8.2 跨进程通信选型

| 通信 | wire format | 何时 |
|---|---|---|
| **UDS + bincode** | bincode 2.0 | 内部 Rust ↔ Rust (signer ↔ nonce-mgr) |
| **UDS + rmp-serde** | MessagePack | signer 已用 (老孙 v3) |
| **UDS + protobuf** | protobuf 3 | C++ ↔ Rust 跨语言 (trader ↔ nonce-mgr) |
| **TCP + protobuf** | protobuf 3 + length-prefix | 跨机 (未来 obs 节点 ↔ trader); 现阶段不需要 |
| **SHM + FlatBuffers** | FlatBuffers 零拷贝 | 大对象 (orderbook snapshot / tick replay) |
| **SHM + raw struct (zerocopy)** | C ABI struct (`#[repr(C)]`) | 已知 layout 不变, 极致性能 |

### 8.3 SHM 用法 (Rust 端, 与 C++ boost::interprocess 互通)

```rust
use shared_memory::{ShmemConf, Shmem};
use zerocopy::{FromBytes, AsBytes};

#[derive(FromBytes, AsBytes)]
#[repr(C)]
struct OrderbookSnapshot {
    seq: u64,
    bid_levels: [Level; 32],
    ask_levels: [Level; 32],
    // ...
}

let shm = ShmemConf::new()
    .size(4096)
    .os_id("stcpp_orderbook_nba_001")  // C++ 端用同名 boost::interprocess::shared_memory_object
    .open()?;
let snap = OrderbookSnapshot::ref_from(unsafe { shm.as_slice() }).unwrap();
// 读取 snap.bid_levels...
```

**与 C++ 端的约定**:
- `#[repr(C)]` Rust struct **必须** 与 C++ 端 struct 字节对应 (字段顺序 / 大小 / 对齐)
- schema 改动: 双方同步, 改 SHM key 名带版本号 (`stcpp_orderbook_v3_nba_001`)
- 不允许在 SHM 里放含指针 / `String` / `Vec` 的类型 (内存地址不跨进程)

---

## 9. Cargo.toml 模板 (给老孙 + 其他子 crate)

### 9.1 子 crate 模板 (`crates/stcpp-api-client/Cargo.toml`)

```toml
[package]
name = "stcpp-api-client"
version.workspace = true
edition.workspace = true
rust-version.workspace = true
license.workspace = true
publish.workspace = true

[lints]
workspace = true

[features]
default = []
goalserve = []
polymarket = []
polygon-rpc = []

[dependencies]
stcpp-common = { path = "../stcpp-common" }
tokio = { workspace = true, features = ["macros", "rt-multi-thread", "net", "sync", "time", "io-util"] }
reqwest.workspace = true
tokio-tungstenite.workspace = true
futures-util.workspace = true
serde.workspace = true
serde_json.workspace = true
simd-json.workspace = true
bytes.workspace = true
governor.workspace = true
backoff.workspace = true
tracing.workspace = true
thiserror.workspace = true
prometheus-client.workspace = true

[dev-dependencies]
criterion.workspace = true
proptest.workspace = true
fake.workspace = true
tokio = { workspace = true, features = ["test-util"] }

[[bench]]
name = "polymarket_parse"
harness = false  # criterion 自己有 harness

[[bench]]
name = "wss_first_message"
harness = false
```

**铁律**:
- 子 crate **不重写 dependency 版本号**, 用 `workspace = true` (单一来源)
- features 默认空, 用户显式开 (`--features goalserve,polymarket`)
- `dev-dependencies` 不污染生产 build
- 多 bench 拆成多个 `[[bench]]`, 每个独立

### 9.2 `rust-toolchain.toml`

```toml
[toolchain]
channel = "1.83.0"
components = ["rustc", "cargo", "rust-std", "clippy", "rustfmt"]
targets = ["x86_64-unknown-linux-gnu", "aarch64-apple-darwin"]
profile = "minimal"
```

### 9.3 `deny.toml` (workspace 级, 与 signer 共享一份)

```toml
[graph]
targets = [
    { triple = "x86_64-unknown-linux-gnu" },
    { triple = "aarch64-apple-darwin" },
]

[advisories]
db-urls = ["https://github.com/rustsec/advisory-db"]
yanked = "deny"

[licenses]
allow = ["MIT", "Apache-2.0", "Apache-2.0 WITH LLVM-exception", "BSD-2-Clause", "BSD-3-Clause", "ISC", "Unicode-DFS-2016", "Unicode-3.0", "MPL-2.0", "Zlib", "CC0-1.0"]
confidence-threshold = 0.93

[bans]
multiple-versions = "warn"   # workspace 中 polars / arrow 多版本难免, warn 即可
wildcards = "deny"
deny = [
    { name = "openssl" },
    { name = "openssl-sys" },
    { name = "native-tls" },
]

[sources]
unknown-registry = "deny"
unknown-git = "deny"
allow-registry = ["https://github.com/rust-lang/crates.io-index"]
```

---

## 10. 与 C++ 重复语义场景

### 10.1 同一数据结构 (orderbook) C++ + Rust 各一份

**问题**: 小石定的 C++ orderbook (SoA + AVX2), Rust 端 bench / replay 也需要一个 orderbook 抽象. 怎么对齐?

**方案**:
- **公共 schema 走 FlatBuffers** (`book_v1.fbs`), C++ / Rust 各自 codegen
- **wire / serde 层** 由 schema 强约束
- **内存 layout (热路径) 各自独立**: C++ 用 SoA + AVX2, Rust bench 用同样 SoA 但 wide crate vectorize
- **测试夹具共享**: 同一份 JSONL 真实数据, 两边各跑出 orderbook 结果, 跨语言对比 (golden test)

**目录布局**:
```
sports-trader-cpp/third_party/proto/book_v1.fbs   # 单一 SSOT
sports-trader-cpp/include/stcpp/orderbook.hpp     # C++ 端实现 (小石)
sports-trader-rs/crates/stcpp-common/src/book.rs  # Rust 端实现 (老张 / 小石联签)
```

**golden test** (CI 跑):
- 同 fixture `nba_2025_01_15_1h.jsonl`
- C++ 端跑出 final state JSON
- Rust 端跑出 final state JSON
- diff: 必须字节级一致 (浮点用 fixed-point 表示)

### 10.2 同 API client (C++ 生产 + Rust benchmark)

**问题**: 老李定的 Polymarket C++ client 用 nlohmann/json + cpr; 老陈 bench / 小段 loadgen 在 Rust. 两者哪个权威?

**答**:
- **C++ 是生产** (热路径走 nlohmann/json / simdjson)
- **Rust 是测试/bench/loadgen** (用 simd-json, 拿真实 RPS 数据)
- **两者必须用同一份 fixture**: `docs/RESEARCH/data/laochen-network-bench-*.csv` 等
- **两者结果对比走 ADR**: 当 Rust bench 跑出 latency p99 X, C++ 跑出 Y, X / Y > 1.3 = "Rust 反而更快" 需 @老周 评审是否切换

### 10.3 公共 schema 单一来源

| schema 类型 | 文件 | 谁定 |
|---|---|---|
| 订单 / 交易 / 市场 | `.proto` (gRPC 也用) | @老周 + @老李 |
| 大对象 / 零拷贝 | `.fbs` (FlatBuffers) | @老王 (WAL 也用) |
| audit log | JSON schema | @老唐 |
| metric 命名 | YAML | @小郑 |

**位置**: `sports-trader-cpp/third_party/proto/` (git submodule into Rust workspace).

**改动流程**: 修改 schema 必须 PR, 影响双方, owner 双签 (C++ 端 + Rust 端).

---

## 11. CI/CD 规范

### 11.1 全套 check (PR 必跑, 与老吴 GitHub Actions 集成)

```yaml
# .github/workflows/rust.yml (新建)
name: Rust CI
on: [pull_request, push]
jobs:
  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@1.83.0
        with:
          components: rustfmt, clippy
      - run: cargo fmt --all -- --check
      - run: cargo clippy --workspace --all-targets --all-features -- -D warnings

  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@1.83.0
      - run: cargo test --workspace --all-features

  supply-chain:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@1.83.0
      - run: cargo install cargo-deny cargo-audit cargo-vet --locked
      - run: cargo deny check
      - run: cargo audit --deny warnings
      - run: cargo vet --locked

  bench-compile:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@1.83.0
      - run: cargo bench --workspace --no-run

  bench-regression:
    runs-on: [self-hosted, dedicated-bench]   # 独立机, 避免 GH runner 抖动
    if: github.event_name == 'pull_request'
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@1.83.0
      - run: cargo bench --bench wss_parse -- --save-baseline pr-${{ github.event.number }}
      - run: cargo bench --bench wss_parse -- --baseline main --baseline-lenient
      # criterion 自动报告 regression > 5% 阻断
```

### 11.2 与老高 PR 模板联签

老高 `laogao-code-conventions-v1.md` §7 PR 模板增加段落 (Rust PR 适用):

```markdown
## Rust 工程检查 (Rust PR 必填)
- [ ] `cargo fmt --check` 过
- [ ] `cargo clippy -D warnings` 过
- [ ] `cargo test --workspace` 全过
- [ ] `cargo deny check` 过
- [ ] 新增 unsafe 块: 已附 `SAFETY:` 注释 + 双人评审 (写者 + 另 1 人)
- [ ] 热路径新增分配: 已通过 `dhat` 或自定义 GlobalAlloc 验证 (0 alloc in hot loop)
- [ ] criterion bench: 回归 < 5% (附 `target/criterion/report/index.html` 截图或链接)
- [ ] 新增 direct dependency: 已记录 ADR + 通过 `cargo vet`
```

### 11.3 release flow

1. PR merge to main
2. CI 跑全套
3. 手动触发 release workflow → `cargo vendor --locked` → 离线 sandbox build → minisign 签名 → 归档 binary + SBOM (`cargo cyclonedx`)
4. 部署 by @老吴

---

## 12. Rust 工程红线 (给老高补 R-Rust 段)

> 老高 `laogao-code-conventions-v1.md` §9 红线表已覆盖 C++. 本节为 Rust 等价红线, 编号 R-Rust-1 .. R-Rust-8, 老高 v0.2 合并.

| ID | 红线 | 谁来挡 | 违反处理 |
|---|---|---|---|
| **R-Rust-1** | **热路径禁止临时分配** (Box::new / String::new / Vec::new / format! / to_string) — 必须复用预分配 buffer | 老高 (PR) + 老姜 (bench 回归) | PR block; 必须改成 reuse buffer |
| **R-Rust-2** | **生产代码禁止 `unwrap()` / `expect()`** — 必须用 `?` + `thiserror` 自定义 error; `expect()` 仅在 main 启动期允许 | 老高 + clippy `unwrap_used = deny` | clippy CI 自动 block |
| **R-Rust-3** | **panic 等于 incident** — `panic!()` / `todo!()` / `unimplemented!()` / 未 match arm → 立刻 P1; 默认 `panic = "abort"` | 老韩 (RM) + 老胡 (incident) | post-mortem 必走 |
| **R-Rust-4** | **`unsafe` 块必带 `// SAFETY:`** 注释列举 invariant, 双人评审 (作者 + 另 1 人; reviewer 必须是老张/老孙/小石/老周之一) | 老张 + 老孙 + 小石 + 老周 | PR block |
| **R-Rust-5** | **新增 direct dependency 必走 ADR** — 写明用途 / 替代方案 / 性能 / audit 状态, `cargo vet` 通过 | 老张 + 老沈 (security) | PR block |
| **R-Rust-6** | **禁止 `mpsc::unbounded_channel`** — 所有 channel / queue 必须有界, 否则等同于 OOM 入口 | 老高 + 小石 | clippy 自定义 lint (后续装) + 人工审 |
| **R-Rust-7** | **stream 处理禁止 `collect::<Vec<_>>()` 不知大小输入** — 大数据必走 `futures::Stream` + bounded channel + batch | 老高 + 老张 | 人工审 + 大数据 OOM 是 P0 |
| **R-Rust-8** | **跨进程 schema 改动必双签** (C++ owner + Rust owner) — schema 文件改动同步 review C++ / Rust 两边 codegen | 老周 + 老张 | PR block |

### 12.1 给老高 v0.2 的具体融合建议

老高 `laogao-code-conventions-v1.md` 增段:
- §1 命名: ASCII 全集 — Rust 端同; clippy `non_ascii_idents = warn` 强制
- §5 性能反模式: 增加 Rust 等价表 (链接本文 §7)
- §9 红线: 增加 R-Rust-1 .. R-Rust-8 (本节)
- §7 PR 模板: 增加 Rust 段 (本文 §11.2)

---

## 13. 给 in-flight agents 的迁移建议

### 13.1 老陈 (network-bench, `laochen-network-bench-v1`)

**当前**: Python `wss_bench.py` + `wss_reconnect.py` + bash `bench_helpers.sh`.

**问题**:
- Python `websockets` lib 在 1k+ msg/s 下 GIL 抖动, p99 数据不可信
- asyncio 跨进程并发受限
- 没法测真实 backpressure (Python 无界 queue 容易掩盖问题)

**迁移路径** (4 周):
- 第 1 周: 把 `wss_bench.py` 翻译成 Rust (`crates/stcpp-bench/benches/wss_first_message.rs`, 模板见 §3.2)
- 第 2 周: `wss_reconnect.py` → `crates/stcpp-loadgen/src/bin/wss_reconnect.rs`, 跑 100 次重连出 p99
- 第 3 周: bash `bench_helpers.sh` (REST 限流 + 延迟测) → `crates/stcpp-loadgen/src/bin/rest_burst.rs`
- 第 4 周: 输出 v2 报告, 用 Rust 数据替换 Python 数据, 老姜验收

**保留 Python**: 一次性 ad-hoc 探查 (一次跑完不再用的) 可保留. 长跑 / CI / 回归 全切 Rust.

### 13.2 小段 (Goalserve API, `xiaoduan-goalserve-api-spec-v1`)

**当前**: `latency-runs-20260528.csv` 走 bash + curl + jq.

**迁移**:
- 限流 burst test: 用 §3.5 starter (`stcpp-loadgen/goalserve_burst`)
- 字段对照 / schema 验证: Rust `serde_json::Value` + proptest 反向测试
- 长跑 cron (老吴的 24h 探针): 改成 Rust binary 系统服务

**保留 bash**: ad-hoc curl + jq 一次性查 OK; 周期任务必走 Rust.

### 13.3 小蒋 (回测引擎, `xiaojiang-backtest-framework-v0.1`)

**当前**: Python + DuckDB + pandas, 已声明 v1 用 Python "MVP 阶段先跑通".

**GM 钦定后的调整建议** (给小蒋, 老张+小梁+老韩联签):
| 模块 | v0.1 (Python) | v0.2 (Rust 转化) | 优先级 |
|---|---|---|---|
| **数据加载 (parquet / duckdb)** | duckdb-python | **polars Rust lazy + streaming** | 高 (10-50x) |
| **feature pipeline** | C++ binding (D-04) | C++ binding (D-04 不变) | — |
| **replay loop (核心)** | Python loop | **Rust** (`stcpp-replay` crate) | 高 (50-100x) |
| **RiskManager 模拟** | C++ binding | C++ binding (D-04 不变) | — |
| **signal apply (策略 contract)** | Python | **Rust trait + dyn dispatch** (与 C++ 端策略对齐) | 中 |
| **walk-forward + purged k-fold 切分** | Python | **Rust** (statrs + nalgebra) | 中 |
| **PnL / Sharpe / drawdown / deflated Sharpe 统计** | scipy | **statrs** | 中 |
| **HTML / 图报告生成** | jinja2 + matplotlib | **保留 Python notebook** (不是性能路径) | 低 (保留) |

**关键收益**: 1 个月 NBA 数据回测 Python 60+ 分钟 → Rust 5 分钟内, P0-01 第一份回测可以 T+5 周提前到 T+3 周.

**迁移路径**: v0.1 跑完先验证逻辑, v0.2 (T+8 周) 把 replay loop + polars 迁 Rust.

### 13.4 小袁 (市场微结构, `xiaoliang-market-structure-v1`)

(注: 文档名 xiaoliang 但负责人是小袁, 与 AGENT.md 对齐)

**当前**: 大量 [推断] / [待验] 等历史数据填充. 出图 / 统计部分预计 pandas / matplotlib.

**迁移建议**:
- 历史 tick 解析 / aggregation: Rust + polars (1 小时 → 30 秒)
- 微结构指标计算 (spread persistence / top-of-book stickiness / 大单 imprint): Rust `stcpp-bench/microstructure_metrics`
- 出图: 保留 Python (matplotlib) 一次性, 或上 `plotters` crate (SVG)
- 报告里的数字必须从 Rust pipeline 跑出, 不接受 pandas 数字 (浮点 / NaN 行为不一致)

### 13.5 全员适用的 "Python 转 Rust" 思维转换

| Python 模式 | Rust 等价 |
|---|---|
| `pd.read_parquet(path)` | `LazyFrame::scan_parquet(path, ...)?.collect()?` |
| `df.groupby().agg().sort()` | `df.lazy().group_by().agg().sort()` 同样链式 |
| `requests.get(url).json()` | `reqwest::get(url).await?.json::<T>().await?` |
| `asyncio.gather(*tasks)` | `futures::future::join_all(tasks).await` |
| `time.perf_counter()` | `std::time::Instant::now() / .elapsed()` |
| `with open(path) as f` | `std::fs::File::open(path)?` 或 `BufReader::new(file)` |
| `pickle.dump / load` | `bincode::serde::encode_to_vec / decode_from_slice` |
| `dict[k]` 找不到 KeyError | `HashMap::get(k)` 返 `Option<&V>`, 必处理 |
| `try: ... except Exception` | `match result { Ok(_) => ..., Err(e) => ... }` |
| `numpy.array(...).mean()` | `nalgebra::DVector::from_vec(...).mean()` |

---

## 14. 开放问题 (待 sign-off)

| ID | 问题 | 待 | 优先级 |
|---|---|---|---|
| ZE-Q1 | edition 2024 (Rust 1.83+) 是否锁死? 还是 2021 起步? | @老张 + @老高 决策 (本文已建议 2024) | 高 |
| ZE-Q2 | `tokio` 进 etl / api-client 是否 OK, signer 完全隔离? | @老孙 (signer 不引 tokio 的纪律保持) | 高 |
| ZE-Q3 | polars 在 trader 主进程是否接入? 还是仅 etl/replay 工具? | @老周 决策 (本文倾向: 仅工具, 不进 trader hot path) | 高 |
| ZE-Q4 | benchmark 专用机 (`self-hosted, dedicated-bench`) 是否物理隔离? | @老吴 部署 | 中 |
| ZE-Q5 | mimalloc 还是 jemalloc? 实测我们 workload 才能定 | @老姜 跑对比 bench | 中 |
| ZE-Q6 | FlatBuffers 还是 Cap'n Proto 用于 SHM 大对象? 老王 WAL 选哪个? | @老王 决策 (本文倾向 FlatBuffers, 与 Rust crate `flatbuffers` 成熟度配套) | 中 |
| ZE-Q7 | cxx-rs 在什么具体场景下允许? 是否要预先写一个 trial spike? | @老周 + @老郭 | 中 |
| ZE-Q8 | 给老高 v0.2 R-Rust 红线段是否还需补充? (R-Rust-9 等待提议) | @老高 v0.2 review | 低 |
| ZE-Q9 | 小蒋 v0.2 Rust 迁移启动时间? (本文建议 T+8 周, 在 v0.1 验证完逻辑后) | @小蒋 + @小梁 | 低 |
| ZE-Q10 | Rust workspace 仓 (`sports-trader-rs`) 是否单仓 vs `sports-trader-cpp` 的 monorepo? | @老雷 + @老吴 (CI/build) | 低 |

---

## 15. 决策声明

老张 (rust-advisor) sign-off v1:

- **workspace**: 独立仓 `sports-trader-rs/`, resolver=2, edition=2024, MSRV 1.83
- **3 核心 crate**: `tokio` + `criterion` + `polars` (按重要度)
- **3 用户高优承诺 (有量化)**:
  - 多线程纪律: tokio (IO) / rayon (CPU) / crossbeam (lock-free), `#[repr(align(64))]` 防 false sharing
  - 内存管理: bytes + zerocopy + bumpalo + mimalloc, 热路径 0 alloc
  - 大数据并行: futures Stream + bounded mpsc + polars streaming, 严禁 unbounded / collect
- **FFI**: 起步不上 cbindgen / cxx-rs, 走独立进程 + UDS / SHM
- **CI**: cargo fmt / clippy / test / deny / audit / vet 全跑, criterion 回归 > 5% 阻断
- **红线 R-Rust-1 .. R-Rust-8**: 入老高 v0.2

待 @老雷 (GM, 钦定方向) + @老周 (架构 boundary) + @老高 (代码规范集成) 共同 sign-off 后转 ACTIVE.

下一步:
- 立刻给老陈 / 小段 / 小蒋 / 小袁 发迁移指南 (§13)
- T+2 周: Rust workspace 仓 `sports-trader-rs/` 落地骨架 (空 crate + CI 跑通)
- T+4 周: 老陈 + 小段 完成 Python → Rust 第一波迁移, 出 v2 bench 报告
- T+8 周: 小蒋 v0.2 Rust 启动
