// include/stcpp/domain/micro_pusd.hpp — MicroPUSD 强类型单位 (方案 B 单位契约, B1 c1)
//
// Owner: 老雷 (GM) — 实施老周 spec (docs/RESEARCH/laozhou-microusd-unit-contract-spec-v1.md §1)
// last_review: 2026-05-30
//
// 背景: size_usdc → size_pUSD_micro (ADR v0.6 rename, micro=1e-6) 后, RM 的 caps/bankroll
//   仍按 whole pUSD 设 → risk_gateway.cpp 拿 size_pUSD_micro 直接比 cap(=10) → 差 1e6 量级
//   → 任何单恒拒 (A2 解封才暴露)。根因是「字段名带 micro 但裸 int64 无类型护栏」。本类型用
//   编译期把「micro pUSD」钉死, 杜绝跨边界漏乘 ×1e6 (踩铁律#3 = 实盘静默烧钱)。
//
// 设计 (老周 §1):
//   - 单字段 int64; standard-layout; sizeof==8; ABI 零变 (WAL/VirtualFill 布局不动)
//   - explicit 构造 (禁裸 int64 隐式灌入)
//   - 禁 MicroPUSD*MicroPUSD (pUSD² 无意义); 除同型返 double 比例
//   - to_pusd()/from_pusd() 显式跨 double 算法边界
//   - _pusd / _upusd UDL 给测试 + cap 字面量
//
// 红线: 铁律#3 (单位口径回测/实盘一致); R-20 不涉 (无 ts)。
//
// c1 (本提交): 仅类型 + 单测, 暂不接入 RM / 订单结构 / 仓位账本 (留待 c2-c5)。
#pragma once

#include <cmath>
#include <compare>
#include <cstdint>
#include <type_traits>

namespace stcpp::domain {

struct MicroPUSD {
    std::int64_t v{0};

    constexpr MicroPUSD() noexcept = default;
    explicit constexpr MicroPUSD(std::int64_t micro) noexcept : v(micro) {}

    // ---- 跨 double pUSD 算法边界 (唯一合法转换通道) ----
    [[nodiscard]] constexpr double to_pusd() const noexcept { return static_cast<double>(v) / 1'000'000.0; }
    // 从 double pUSD 转回 micro; round-to-nearest (截断会系统性低估 exposure).
    // 注: std::llround 非 constexpr (C++20), 故 from_pusd 非 constexpr (运行期入口, 无妨).
    [[nodiscard]] static MicroPUSD from_pusd(double pusd) noexcept {
        return MicroPUSD{static_cast<std::int64_t>(std::llround(pusd * 1'000'000.0))};
    }
    // 直接给 micro 整数 (上游已是 micro 时, 比 from_pusd 省一次乘).
    [[nodiscard]] static constexpr MicroPUSD from_micro(std::int64_t micro) noexcept {
        return MicroPUSD{micro};
    }

    // ---- 同型加减 (exposure 累加) ----
    constexpr MicroPUSD& operator+=(MicroPUSD o) noexcept {
        v += o.v;
        return *this;
    }
    constexpr MicroPUSD& operator-=(MicroPUSD o) noexcept {
        v -= o.v;
        return *this;
    }

    // ---- 整数倍 / 整数均摊 (n 倍下单 / 均摊) ----
    constexpr MicroPUSD& operator*=(std::int64_t k) noexcept {
        v *= k;
        return *this;
    }
    constexpr MicroPUSD& operator/=(std::int64_t k) noexcept {
        v /= k;
        return *this;
    }

    // ---- 比较 (4 cap 比较点; 三路) ----
    friend constexpr auto operator<=>(MicroPUSD, MicroPUSD) noexcept = default;
    friend constexpr bool operator==(MicroPUSD, MicroPUSD) noexcept = default;
};

// 同型加减 (返新值)
[[nodiscard]] constexpr MicroPUSD operator+(MicroPUSD a, MicroPUSD b) noexcept {
    return MicroPUSD{a.v + b.v};
}
[[nodiscard]] constexpr MicroPUSD operator-(MicroPUSD a, MicroPUSD b) noexcept {
    return MicroPUSD{a.v - b.v};
}
[[nodiscard]] constexpr MicroPUSD operator-(MicroPUSD a) noexcept {
    return MicroPUSD{-a.v};
}

// 整数倍 (左右对称) / 整数均摊
[[nodiscard]] constexpr MicroPUSD operator*(MicroPUSD a, std::int64_t k) noexcept {
    return MicroPUSD{a.v * k};
}
[[nodiscard]] constexpr MicroPUSD operator*(std::int64_t k, MicroPUSD a) noexcept {
    return MicroPUSD{a.v * k};
}
[[nodiscard]] constexpr MicroPUSD operator/(MicroPUSD a, std::int64_t k) noexcept {
    return MicroPUSD{a.v / k};
}

// 同型相除 → 无量纲比例 double (DD% / 利用率)
[[nodiscard]] constexpr double operator/(MicroPUSD a, MicroPUSD b) noexcept {
    return static_cast<double>(a.v) / static_cast<double>(b.v);
}

// !! 故意不声明 operator*(MicroPUSD, MicroPUSD): pUSD² 无量纲意义, 误用 → 编译错 !!

// ---- _pusd UDL (测试/cap 字面量: 10_pusd == from_pusd(10.0)) ----
[[nodiscard]] constexpr MicroPUSD operator""_pusd(long double pusd) noexcept {
    return MicroPUSD{static_cast<std::int64_t>(pusd * 1'000'000.0L + (pusd >= 0 ? 0.5L : -0.5L))};
}
[[nodiscard]] constexpr MicroPUSD operator""_pusd(unsigned long long pusd) noexcept {
    return MicroPUSD{static_cast<std::int64_t>(pusd) * 1'000'000};
}
// micro 直读 UDL (cap 阈值以 micro 写时用): 1_upusd == from_micro(1)
[[nodiscard]] constexpr MicroPUSD operator""_upusd(unsigned long long micro) noexcept {
    return MicroPUSD{static_cast<std::int64_t>(micro)};
}

// ---- A1 (老郭钳-1): double pUSD → int64 micro 的【唯一】转换入口 ----
//   VirtualFill.fill_size_usdc / PositionLedger 等存 micro 的 int64 字段, double→micro 一律走此 helper,
//   禁散落手写 (int64)(x*1e6) / llround(x*1e6) (负值 round 方向会错; 单点便于 CI grep 守护)。
//   复用 from_pusd 的 round-to-nearest (llround) 语义 (c1 已审)。
[[nodiscard]] inline std::int64_t to_micro_pusd(double pusd) noexcept {
    return MicroPUSD::from_pusd(pusd).v;
}

// ---- ABI / layout 保证 (编译期, 老周 §6.1; ADR-027 lock v1.8 不破) ----
static_assert(sizeof(MicroPUSD) == sizeof(std::int64_t), "MicroPUSD must be 8 bytes (ABI lock)");
static_assert(alignof(MicroPUSD) == alignof(std::int64_t), "MicroPUSD align == int64 (ABI lock)");
static_assert(std::is_standard_layout_v<MicroPUSD>, "MicroPUSD must be standard-layout (WAL/VirtualFill)");
static_assert(std::is_trivially_copyable_v<MicroPUSD>, "MicroPUSD must be trivially copyable (memcpy WAL)");

}  // namespace stcpp::domain
