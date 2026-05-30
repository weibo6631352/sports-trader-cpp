// src/stcpp/app/paper_runtime_main.cpp — headless paper 常驻进程入口 (systemd)
//
// Owner: 老雷 (GM) — PaperDaemon 重构 (老郭 §A.1): systemd stcpp-paper.service 期待的
//   /opt/stcpp/bin/paper_runtime. 复用 stcpp::app::PaperDaemon (RunMode::Headless, 无 HTTP).
//
// last_review: 2026-05-30
//
// 角色: RunMode::Headless —— 无 HTTP 观测端, stderr → journald (老吴部署对齐 §3).
//   全天候跑 PaperLoop + ML 采集; 观测看板由独立 stcpp_paper_server (按需起) 提供.
//
// R-11 硬 gate (老韩 R-11 审计 G1-G4): main() 第一件事跑 mode 一致性校验.
//   mode 唯一真相源 = build-time (kCompiledMode); PAPER_MODE env 仅做冗余交叉校验.
//   任一不一致 → abort (绝不允许 env 运行期切 live).
//
// ToS: 仅 paper 虚拟成交 (VirtualFill), 不向 Polymarket CLOB 下单.

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "stcpp/app/paper_daemon.hpp"
#include "stcpp/execution/execution_mode.hpp"

namespace {

std::atomic<stcpp::app::PaperDaemon*> g_daemon{nullptr};

void handle_signal(int /*sig*/) {
    if (auto* d = g_daemon.load(std::memory_order_acquire)) {
        d->RequestStop();
    }
}

void print_usage() {
    std::printf(
        "usage: paper_runtime [--verbose] [--no-record-ml] [--ml-path PATH]\n"
        "  --verbose        extra WSS/parser debug logging\n"
        "  --no-record-ml   disable ML training data capture\n"
        "  --ml-path PATH   ML capture output (default data/ml_capture/quotes.jsonl)\n"
        "\n"
        "RunMode::Headless — paper trading daemon, no HTTP (stderr -> journald).\n"
        "  Requires PAPER_MODE=1 env (R-11 hard gate).\n"
        "  live book: gamma /events -> CLOB WSS; live score: Goalserve inplay.\n"
        "  ToS: VirtualFill only, no CLOB order placement.\n");
}

// ---------------------------------------------------------------------------
// R-11 硬 gate (老韩 G1-G4): build-time mode = 真相源, PAPER_MODE env = 冗余交叉校验.
//   G1 编译期: 本 binary 只许 paper mode 编译 (static_assert).
//   G2 运行期: STCPP_EXEC_MODE_STR 与 kCompiledMode 一致.
//   G3 运行期: PAPER_MODE env 必须 == "1" (缺失/!=1 → abort).
//   G4 运行期: ExecutionContext::Init(Paper) 单次 (双调内部 abort).
// ---------------------------------------------------------------------------
void enforce_paper_mode_gates() {
    using stcpp::execution::ExecutionContext;
    using stcpp::execution::ExecutionMode;

    // G1: 编译期锁 —— paper_runtime 绝不在 live/backtest binary 中编译.
    static_assert(stcpp::execution::kCompiledMode == ExecutionMode::Paper,
                  "paper_runtime 只许 paper mode 编译 (R-11/R-7); 检查 -DSTCPP_EXEC_MODE=paper");

    // G2: build-time 字符串与 kCompiledMode 一致 (双注入源交叉校验).
    if (std::strcmp(STCPP_EXEC_MODE_STR, "paper") != 0) {
        std::fprintf(stderr,
                     "[paper_runtime] FATAL (R-11 G2): STCPP_EXEC_MODE_STR=\"%s\" != \"paper\". "
                     "build-time mode 不一致, abort.\n",
                     STCPP_EXEC_MODE_STR);
        std::abort();
    }

    // G3: PAPER_MODE env 硬校验 (systemd unit Environment=PAPER_MODE=1; 缺失 → abort).
    const char* paper_env = std::getenv("PAPER_MODE");
    if (paper_env == nullptr || std::strcmp(paper_env, "1") != 0) {
        std::fprintf(stderr,
                     "[paper_runtime] FATAL (R-11 G3): PAPER_MODE env %s. "
                     "headless paper 常驻进程必须显式 PAPER_MODE=1 (systemd unit 已设); abort.\n",
                     paper_env == nullptr ? "未设置" : "!= 1");
        std::abort();
    }

    // G4: ExecutionContext 单次 Init (R-7: 双调 → 内部 abort).
    ExecutionContext::Init(ExecutionMode::Paper);

    std::printf("[paper_runtime] R-11 gates PASS: build-time=paper, PAPER_MODE=1, ExecutionContext=paper\n");
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    // --help/-h 在 gate 前响应 (纯信息, 不起 daemon, 不碰账本 → 不需 R-11 gate).
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    enforce_paper_mode_gates();  // R-11: 起 daemon 前第一件事

    stcpp::app::PaperDaemonConfig cfg;
    cfg.mode = stcpp::app::RunMode::Headless;
    cfg.exec_mode = stcpp::debug_api::ExecMode::Paper;  // G2 已校验

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--verbose" || a == "-v") {
            cfg.verbose = true;
        } else if (a == "--no-record-ml") {
            cfg.record_ml = false;
        } else if (a == "--ml-path" && i + 1 < argc) {
            cfg.ml_path = argv[++i];
        } else {
            std::fprintf(stderr, "[paper_runtime] unknown arg: %s (try --help)\n", a.c_str());
            return 2;
        }
    }

    // SIGHUP: logrotate postrotate 不断链路 (老吴部署对齐 §3); 忽略即可 (stderr fd 不重开).
    std::signal(SIGHUP, SIG_IGN);

    stcpp::app::PaperDaemon daemon(std::move(cfg));
    g_daemon.store(&daemon, std::memory_order_release);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const stcpp::app::BuildResult br = daemon.Build();
    if (!br.ok) {
        std::fprintf(stderr, "[paper_runtime] FATAL: PaperDaemon::Build 失败: %s\n", br.error.c_str());
        return 1;
    }
    std::printf("[paper_runtime] Build OK: markets=%zu tokens=%zu; 进入 headless 常驻\n", br.market_count,
                br.token_count);
    std::fflush(stdout);

    const int rc = daemon.Run();
    g_daemon.store(nullptr, std::memory_order_release);
    std::printf("[paper_runtime] 已停止\n");
    return rc;
}
