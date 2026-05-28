// stcpp/infra/wal/wal_kind.hpp — 4 类 WAL 路径白名单 (R-11 物理隔离 + ML-R1)
//
// 落: laowang-wal-framework-v0.2.md §3 (4 WAL 矩阵)
//     laowang-wal-framework-cpp-interface-v1.md §2
// 红线 R-11: 4 类 WAL 物理路径隔离, Open() 期硬校验 path prefix → fail = abort.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace stcpp::infra::wal {

enum class WalKind : std::uint8_t {
    RiskAudit    = 0,
    Position     = 1,
    PaperAudit   = 2,
    ShadowAudit  = 3,
};

inline constexpr std::size_t kWalKindCount = 4;

// 4 类 WAL 路径白名单 (R-11). 老吴 deploy 已建 systemd group + 0750 目录.
inline constexpr std::array<std::pair<WalKind, std::string_view>, kWalKindCount>
    kPathRoots{{
        {WalKind::RiskAudit,    "/var/lib/stcpp/audit/"},
        {WalKind::Position,     "/var/lib/stcpp/exec/"},
        {WalKind::PaperAudit,   "/var/lib/stcpp/paper/"},
        {WalKind::ShadowAudit,  "/var/lib/stcpp/shadow/"},
    }};

[[nodiscard]] constexpr std::string_view PathRootOf(WalKind k) noexcept {
    return kPathRoots[static_cast<std::size_t>(k)].second;
}

[[nodiscard]] constexpr std::string_view ToString(WalKind k) noexcept {
    switch (k) {
        case WalKind::RiskAudit:   return "risk_audit";
        case WalKind::Position:    return "position";
        case WalKind::PaperAudit:  return "paper_audit";
        case WalKind::ShadowAudit: return "shadow_audit";
    }
    return "unknown";
}

}  // namespace stcpp::infra::wal
