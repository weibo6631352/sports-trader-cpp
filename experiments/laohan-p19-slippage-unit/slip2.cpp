#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
constexpr double BETA = 0.3, RHO_MAX = 3.0, FLOOR = 0.50, KAPPA = 1.0, THALF = 30000.0;
struct R {
    double rho, fill;
    int slip_bps;
    const char* rej;
};
R calc(double size, double depth, double price, double tick, double dt_ms) {
    double rho = size / depth, s_stale = 1.0 - std::exp(-dt_ms / THALF), pi = 1.0 - std::exp(-BETA * rho);
    double pf, fill;
    const char* rej = "Ok";
    if (rho > RHO_MAX)
        return {rho, 0, 0, "EXCEED_BOOK_DEPTH"};
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
    int sb = (int)(slip + (slip >= 0 ? 0.5 : -0.5));
    if (fill < FLOOR)
        rej = "LOW_FILL_RATE";
    else if (std::abs((double)sb) > 200)
        rej = "EXCESSIVE_SLIPPAGE";
    return {rho, fill, sb, rej};
}
int main() {
    printf("=== search EXCESSIVE_SLIPPAGE pure (fill>=0.5 & slip>200) ===\n");
    for (double depth : {400.0, 2000.0}) {
        for (double price : {0.02, 0.05, 0.10}) {
            for (double sizeWh = 10; sizeWh <= depth * 3; sizeWh += 5) {
                R r = calc(sizeWh, depth, price, 0.01, 200);
                if (std::strcmp(r.rej, "EXCESSIVE_SLIPPAGE") == 0) {
                    printf("FOUND: size=%.0f depth=%.0f price=%.2f -> rho=%.3f fill=%.3f slip=%d\n", sizeWh,
                           depth, price, r.rho, r.fill, r.slip_bps);
                    break;
                }
            }
        }
    }
    printf("=== R18 EXCEED with depth=250: min size whole for rho>3 ===\n");
    R d = calc(800, 250, 0.5, 0.01, 200);
    printf("size800 depth250: rho=%.2f -> %s\n", d.rho, d.rej);
    printf(
        "=== R16 LOW_FILL: keep stale 40s, default size. fix size=0.001 -> fill=0.2636<0.5 still LOW ===\n");
    R s = calc(0.001, 1100, 0.5, 0.01, 40000);
    printf("size0.001 depth1100 stale40s: fill=%.4f -> %s\n", s.fill, s.rej);
    return 0;
}
