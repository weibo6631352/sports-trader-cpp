// stcpp/infra/wal/wal_kind.hpp — 5 类 WAL 路径白名单 (R-11 物理隔离 + ML-R1)
//
// 落: laowang-wal-framework-v0.2.md §3 (4 WAL 矩阵)
//     laowang-wal-framework-cpp-interface-v1.md §2
//     小冯 W6 Wave 29: 新增 IngestRaw = 4 (DS-01 解锁)
// 红线 R-11: 5 类 WAL 物理路径隔离, Open() 期硬校验 path prefix → fail = abort.
// 红线 R-7:  IngestRaw 区分 paper/live: paper.ingest_raw.wal / live.ingest_raw.wal

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace stcpp::infra::wal {

enum class WalKind : std::uint8_t {
    RiskAudit = 0,
    Position = 1,
    PaperAudit = 2,
    ShadowAudit = 3,
    IngestRaw = 4,  // 小冯 W6 DS-01: Goalserve/PM WSS 原始帧落盘, 解锁 ML 训练源数据
};

inline constexpr std::size_t kWalKindCount = 5;

// 5 类 WAL 路径白名单 (R-11). 老吴 deploy 已建 systemd group + 0750 目录.
// IngestRaw 路径根 = /var/lib/stcpp/ingest/ (paper/live 子目录由 build-time 宏决定).
inline constexpr std::array<std::pair<WalKind, std::string_view>, kWalKindCount> kPathRoots{{
    {WalKind::RiskAudit, "/var/lib/stcpp/audit/"},
    {WalKind::Position, "/var/lib/stcpp/exec/"},
    {WalKind::PaperAudit, "/var/lib/stcpp/paper/"},
    {WalKind::ShadowAudit, "/var/lib/stcpp/shadow/"},
    {WalKind::IngestRaw, "/var/lib/stcpp/ingest/"},
}};

[[nodiscard]] constexpr std::string_view PathRootOf(WalKind k) noexcept {
    for (const auto& [kind, path] : kPathRoots) {
        if (kind == k)
            return path;
    }
    return "";
}

[[nodiscard]] constexpr std::string_view ToString(WalKind k) noexcept {
    switch (k) {
        case WalKind::RiskAudit:
            return "risk_audit";
        case WalKind::Position:
            return "position";
        case WalKind::PaperAudit:
            return "paper_audit";
        case WalKind::ShadowAudit:
            return "shadow_audit";
        case WalKind::IngestRaw:
            return "ingest_raw";
    }
    return "unknown";
}

}  // namespace stcpp::infra::wal
