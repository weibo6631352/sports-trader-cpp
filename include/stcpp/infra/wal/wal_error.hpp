// stcpp/infra/wal/wal_error.hpp — WalError enum + Result alias
//
// 落: laowang-wal-framework-cpp-interface-v1.md §4
// 注: 本仓库不引入 tl::expected (无第三方依赖约束). 用 std::expected (C++23) 时机未到,
//     先用轻量 WalResult<T> = struct { bool ok; T value; WalError error; } 风格.
//     真上线时切 std::expected, API 不变.

#pragma once

#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>

namespace stcpp::infra::wal {

enum class WalError : std::uint8_t {
    Ok = 0,
    PitViolation = 1,        // → caller REJECT(INVALID_INTENT.sub=TS_*)
    Backpressure = 2,        // → caller REJECT(AUDIT_WAL_BACKPRESSURE)  老韩 v0.3 #18
    FsyncFailed = 3,         // → SAFE_MODE
    PathPrefixMismatch = 4,  // Open() abort, 不返回 (R-11)
    VersionMismatch = 5,
    CrcMismatch = 6,
    SeqGap = 7,
    UlidNonMonotonic = 8,
    MidCorruption = 9,
    TailTruncated = 10,  // warn, replay 可继续
    DiskFull = 11,
    Io = 12,
};

[[nodiscard]] constexpr std::string_view ToString(WalError e) noexcept {
    switch (e) {
        case WalError::Ok:
            return "Ok";
        case WalError::PitViolation:
            return "PitViolation";
        case WalError::Backpressure:
            return "Backpressure";
        case WalError::FsyncFailed:
            return "FsyncFailed";
        case WalError::PathPrefixMismatch:
            return "PathPrefixMismatch";
        case WalError::VersionMismatch:
            return "VersionMismatch";
        case WalError::CrcMismatch:
            return "CrcMismatch";
        case WalError::SeqGap:
            return "SeqGap";
        case WalError::UlidNonMonotonic:
            return "UlidNonMonotonic";
        case WalError::MidCorruption:
            return "MidCorruption";
        case WalError::TailTruncated:
            return "TailTruncated";
        case WalError::DiskFull:
            return "DiskFull";
        case WalError::Io:
            return "Io";
    }
    return "unknown";
}

// 轻量 Result (C++20 兼容, 等 C++23 std::expected 切). API 与 tl::expected 风格一致.
template <typename T>
class WalResult {
public:
    constexpr WalResult(T v) noexcept : value_(std::move(v)) {}  // NOLINT
    constexpr WalResult(WalError e) noexcept : value_(e) {}      // NOLINT

    [[nodiscard]] constexpr bool has_value() const noexcept { return std::holds_alternative<T>(value_); }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr const T& value() const& noexcept { return std::get<T>(value_); }
    [[nodiscard]] constexpr T&& value() && noexcept { return std::get<T>(std::move(value_)); }

    [[nodiscard]] constexpr WalError error() const noexcept {
        return has_value() ? WalError::Ok : std::get<WalError>(value_);
    }

private:
    std::variant<T, WalError> value_;
};

// void 特化 (Append 仅返 seq u64, FlushUntil 返 void)
template <>
class WalResult<void> {
public:
    constexpr WalResult() noexcept : err_(WalError::Ok) {}
    constexpr WalResult(WalError e) noexcept : err_(e) {}  // NOLINT
    [[nodiscard]] constexpr bool has_value() const noexcept { return err_ == WalError::Ok; }
    constexpr explicit operator bool() const noexcept { return has_value(); }
    [[nodiscard]] constexpr WalError error() const noexcept { return err_; }

private:
    WalError err_;
};

}  // namespace stcpp::infra::wal
