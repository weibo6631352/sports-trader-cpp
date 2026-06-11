// experiments/laolei-phase-align/sim.cpp — inplay 接口相位贴合实验 (老雷, 2026-06-05)
//
// 目的: 本地模拟 Goalserve inplay feed(每 ~interval 发一版)+ 我们 1/s 轮询 + 相位对齐算法,
//   测【抓取命中延迟 L = 我方拿到时刻 − feed版本发布时刻】的分布, 验证相位锁是否生效 + 快速迭代参数。
//   原样复刻 src/stcpp/data/inplay_feed_thread.cpp 的相位算法 (EMA + phase_corr 双向伺服 + aim + sleep),
//   故结论直接代表生产逻辑。跑完归档。
//
// 编译: c++ -O2 -std=c++20 sim.cpp -o sim && ./sim
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <random>
#include <cmath>

// ---- 复刻生产参数 (inplay_feed_thread.hpp / .cpp) ----
int64_t kPollFloor = 1005;             // poll_interval_ms = min_fetch_interval_ms (限速地板; 实验可改)
constexpr int64_t kMargin    = 150;    // phase_margin_ms
constexpr int64_t kMaxNudge  = 350;    // phase_max_nudge_ms
constexpr int64_t kSafety    = 30;     // 留 30ms 余量 (clamp hi = margin - safety)
int64_t kCorrTarget= 250;              // 伺服目标 (L→此值; 实验可改, 老板「取最小值命中为基准」→ 试压到 fetch 底)

struct PhaseState {
    int64_t prev_updated = 0;
    int64_t ema = 2000;
    int64_t corr = 0;
};

// 复刻: 收到新版本时更新 EMA + 双向 phase_corr 伺服
void on_new_version(PhaseState& s, int64_t updated_ts, int64_t now_rt) {
    int64_t L = now_rt - updated_ts;
    if (s.prev_updated > 0) {
        int64_t gap = updated_ts - s.prev_updated;
        if (gap >= 500 && gap <= 8000) s.ema = (s.ema * 7 + gap * 3) / 10;
    }
    if (L >= 0 && L < s.ema * 3 / 2) {
        s.corr += (L - kCorrTarget) * 3 / 10;       // 比例增益 0.3, 双向
        int64_t lo = -kMaxNudge, hi = kMargin - kSafety;
        if (s.corr < lo) s.corr = lo;
        if (s.corr > hi) s.corr = hi;
    }
    s.prev_updated = updated_ts;
}

// 复刻: 算下一次 sleep (瞄准下一版更新后; 够近才瞄, 否则 floor)
int64_t next_sleep(const PhaseState& s, int64_t now_rt) {
    if (s.prev_updated <= 0 || s.ema < 500) return kPollFloor;
    int64_t base = s.prev_updated + kMargin - s.corr;
    int64_t need = now_rt + kPollFloor - base;
    int64_t k = (need <= s.ema) ? 1 : (need + s.ema - 1) / s.ema;
    int64_t target = base + k * s.ema;
    int64_t w = target - now_rt;
    if (w <= kPollFloor + kMaxNudge) return w;
    return kPollFloor;
}

struct Result { double min, p50, p90, max, mean; int misses; int catches; };

Result run(int64_t interval, int64_t jitter_ms, int64_t fetch_ms, int n_versions, uint32_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int64_t> jit(-jitter_ms, jitter_ms);
    // feed 发版时刻 publish[k] = k*interval + jitter
    std::vector<int64_t> publish;
    int64_t t = 0;
    for (int k = 0; k < n_versions + 5; ++k) { publish.push_back(t); t += interval + jit(rng); }

    auto latest_version_at = [&](int64_t when) -> int {  // 返回 ≤when 的最新版本 index
        int idx = -1;
        for (int k = 0; k < (int)publish.size(); ++k) { if (publish[k] <= when) idx = k; else break; }
        return idx;
    };

    PhaseState s;
    int64_t now = 0;
    int last_caught = -1;
    std::vector<int64_t> catch_latencies;  // L = now(fetch完成) − updated_ts, 每次抓到新版本
    int misses = 0;
    std::uniform_int_distribution<int64_t> fjit(0, fetch_ms / 2);  // fetch 时间抖动

    // 跑到覆盖 n_versions
    int guard = 0;
    while (last_caught < n_versions && guard++ < n_versions * 10) {
        int64_t sleep_ms = next_sleep(s, now);
        now += std::max(sleep_ms, kPollFloor);  // 顶层 token-bucket 强制限速地板 (生产真实行为)
        now += fetch_ms + fjit(rng);  // fetch 往返
        int v = latest_version_at(now);
        if (v > last_caught) {
            if (last_caught >= 0 && v - last_caught > 1) misses += (v - last_caught - 1);  // 跨过的版本=漏
            int64_t updated_ts = publish[v];
            int64_t L = now - updated_ts;
            catch_latencies.push_back(L);
            on_new_version(s, updated_ts, now);
            last_caught = v;
        }
        // 若没抓到新版本(dup), 不更新 state (复刻: 只在新版本时更新)
    }
    Result r{}; r.catches = catch_latencies.size(); r.misses = misses;
    if (!catch_latencies.empty()) {
        std::sort(catch_latencies.begin(), catch_latencies.end());
        auto pc = [&](double p){ return catch_latencies[std::min((size_t)(p*catch_latencies.size()), catch_latencies.size()-1)]; };
        r.min = catch_latencies.front(); r.max = catch_latencies.back();
        r.p50 = pc(0.5); r.p90 = pc(0.9);
        double sum = 0; for (auto x : catch_latencies) sum += x; r.mean = sum / catch_latencies.size();
    }
    return r;
}

int main() {
    printf("=== inplay 相位贴合实验 (复刻生产算法; poll_floor=%lld margin=%lld nudge=%lld) ===\n",
           (long long)kPollFloor, (long long)kMargin, (long long)kMaxNudge);
    printf("命中延迟 L = 我方拿到时刻 − feed版本发布时刻 (ms). fetch往返=94ms.\n\n");
    struct Cfg { const char* name; int64_t interval; int64_t jitter; };
    Cfg cfgs[] = {
        {"basket 2.04s 稳", 2040, 20},
        {"hockey 2.01s 稳", 2010, 15},
        {"esports 2.00s 稳", 2000, 10},
        {"amfootball 1.64s 快", 1640, 60},
        {"soccer 1.93s",   1930, 80},
        {"tennis 2.33s 变", 2330, 200},
    };
    printf("%-20s | %-5s %-5s %-5s %-5s %-5s | 漏版 命中数\n", "场景(feed间隔)", "min", "p50", "p90", "max", "mean");
    printf("------------------------------------------------------------------------\n");
    for (auto& c : cfgs) {
        // 多 seed 平均, 避免单次运气
        double mn=1e9,p50=0,p90=0,mx=0,me=0; int miss=0,cat=0;
        for (uint32_t seed = 1; seed <= 8; ++seed) {
            Result r = run(c.interval, c.jitter, 94, 120, seed);
            mn=std::min(mn,r.min); p50+=r.p50; p90+=r.p90; mx=std::max(mx,r.max); me+=r.mean; miss+=r.misses; cat+=r.catches;
        }
        printf("%-20s | %5.0f %5.0f %5.0f %5.0f %5.0f | %4d %5d\n",
               c.name, mn, p50/8, p90/8, mx, me/8, miss, cat);
    }
    printf("\n解读: p50/mean ~500ms 且 max~1000 = 没贴住相位 (随机相位, =半个轮询周期)。\n");

    // ---- 关键实验: 轮询频率 vs 命中延迟 (证明只有提频能锁相位) ----
    printf("\n=== 轮询频率 vs 命中延迟 (basket 2.04s feed; 证明锁相位的唯一杠杆=提频) ===\n");
    printf("%-22s | %-5s %-5s %-5s %-5s | 速率 漏版\n", "poll 间隔(限速地板)", "min", "p50", "p90", "max");
    printf("------------------------------------------------------------------\n");
    int64_t floors[] = {1005, 1020, 800, 670, 503, 350};  // 1020≈basket 2040/2 (整除锁, 0漂移, ~0.98/s 不破限)
    for (int64_t f : floors) {
        kPollFloor = f;
        double mn=1e9,p50=0,p90=0,mx=0; int miss=0;
        for (uint32_t seed=1; seed<=8; ++seed) {
            Result r = run(2040, 20, 94, 120, seed);
            mn=std::min(mn,r.min); p50+=r.p50; p90+=r.p90; mx=std::max(mx,r.max); miss+=r.misses;
        }
        printf("%4lldms (%.2f req/s)       | %5.0f %5.0f %5.0f %5.0f | %.2f/s %4d\n",
               (long long)f, 1000.0/f, mn, p50/8, p90/8, mx, 1000.0/f, miss);
    }
    kPollFloor = 1005;
    // ---- 实验: 伺服目标 vs 命中 (老板「取最小值命中为基准」→ 目标压到 fetch 底能否拉低典型命中) ----
    printf("\n=== 伺服目标 vs 命中 (basket 2.04s, 1/s 限速; 目标压到 fetch 底~100ms 看典型命中能否贴最小值) ===\n");
    printf("%-14s | %-5s %-5s %-5s %-5s | 漏版\n", "伺服目标", "min", "p50", "p90", "max");
    printf("--------------------------------------------------\n");
    int64_t targets[] = {250, 150, 100, 50};
    for (int64_t tg : targets) {
        kCorrTarget = tg;
        double mn=1e9,p50=0,p90=0,mx=0; int miss=0;
        for (uint32_t seed=1; seed<=8; ++seed) {
            Result r = run(2040, 20, 94, 120, seed);
            mn=std::min(mn,r.min); p50+=r.p50; p90+=r.p90; mx=std::max(mx,r.max); miss+=r.misses;
        }
        printf("%4lldms        | %5.0f %5.0f %5.0f %5.0f | %4d\n", (long long)tg, mn, p50/8, p90/8, mx, miss);
    }
    kCorrTarget = 250;
    printf("\n结论: 1005ms(限速,1/s) → p50~500/max~1000 锁不住; 提频(地板变小)→ 命中延迟成比例压低。\n");
    printf("  贴住相位(命中稳定~300ms)需 poll≤~500ms(2/s), 但破 Goalserve 1req/s 限速 → 429 封禁险。\n");
    printf("  ⇒ 1/s 限速下命中延迟物理下限 ~半周期; 这不是算法 bug, 是限速 vs feed 节奏的结构限制。\n");
    return 0;
}
