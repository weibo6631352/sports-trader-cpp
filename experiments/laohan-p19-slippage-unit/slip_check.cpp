#include <cmath>
#include <cstdint>
#include <cstdio>
constexpr double BETA = 0.3, RHO_MAX = 3.0, FLOOR = 0.50, KAPPA = 1.0, THALF = 30000.0;
void calc(const char* name, double size, double depth, double price, double tick, double dt_ms) {
    double rho = size / depth;
    double s_stale = 1.0 - std::exp(-dt_ms / THALF);
    double pi = 1.0 - std::exp(-BETA * rho);
    double pf, fill;
    const char* rej = "Ok";
    if (rho > RHO_MAX) {
        printf("%-28s rho=%.6f -> EXCEED_BOOK_DEPTH\n", name, rho);
        return;
    }
    if (rho <= 1.0) {
        pf = price + tick * rho * 0.5;
        fill = 1.0 - pi - s_stale;
    } else {
        pf = price + tick * (0.5 + (rho - 1.0) * KAPPA);
        fill = (1.0 / rho) * (1.0 - pi) * (1.0 - s_stale);
    }
    if (fill < 0)
        fill = 0;
    if (fill > 1)
        fill = 1;
    double slip = (pf - price) / price * 10000.0;
    int slip_bps = (int)(slip + (slip >= 0 ? 0.5 : -0.5));
    if (fill < FLOOR)
        rej = "LOW_FILL_RATE";
    else if (std::abs((double)slip_bps) > 200)
        rej = "EXCESSIVE_SLIPPAGE";
    printf("%-28s rho=%.6f fill=%.4f slip_bps=%d s_stale=%.4f -> %s\n", name, rho, fill, slip_bps, s_stale,
           rej);
}
int main() {
    // dt: make_ok default book_snapshot = now-200ms -> dt~200ms. R16 sets 40s stale.
    printf("=== BUG behavior (size = raw micro count as whole) ===\n");
    calc("R16 LOW_FILL bug", 1000, 1100, 0.5, 0.01, 40000);
    calc("R17 EXCESSIVE bug", 1000, 400, 0.5, 0.01, 200);
    calc("R18 EXCEED_DEPTH bug", 1000, 250, 0.5, 0.01, 200);
    calc("R13 EDGE_NEG(slip) bug", 1000, 800, 0.5, 0.01, 200);
    printf("\n=== FIXED behavior (size_micro=1000 -> 0.001 whole pUSD) ===\n");
    calc("R16 LOW_FILL fix", 0.001, 1100, 0.5, 0.01, 40000);
    calc("R17 EXCESSIVE fix", 0.001, 400, 0.5, 0.01, 200);
    calc("R18 EXCEED_DEPTH fix", 0.001, 250, 0.5, 0.01, 200);
    calc("R13 EDGE_NEG fix", 0.001, 800, 0.5, 0.01, 200);
    printf("\n=== FIXED: what size_micro recreates each reject (depth as in test) ===\n");
    // to recreate EXCEED_BOOK_DEPTH need rho>3 with depth=250 -> size>750 whole = 750e6 micro
    calc("R18 recreate size=1e9mic", 1000.0, 250, 0.5, 0.01, 200);  // 1000 whole vs 250
    // LOW_FILL with stale 40s depth 1100: need rho moderate. try size=600 whole
    calc("R16 recreate size=600wh", 600, 1100, 0.5, 0.01, 40000);
    calc("R16 recreate size=900wh", 900, 1100, 0.5, 0.01, 40000);
    // EXCESSIVE pure: need slip>200bps but fill>=0.5. depth 400. try size around 1000-1100 whole
    calc("R17 recreate size=1100wh", 1100, 400, 0.5, 0.01, 200);
    calc("R17 recreate size=1180wh", 1180, 400, 0.5, 0.01, 200);
    return 0;
}
