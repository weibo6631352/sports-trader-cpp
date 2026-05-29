// stcpp/infra/wal/position_record.hpp — PositionRecord POD v0.1
//
// 落:
//   老韩 W5 Smell-3 → P0 升级 (崩溃重启 circuit breaker 状态归零 + M4.5 gate 数据不完整)
//   GM Wave 27 拍板: W6-A-position-wal 新增 P0
//   laozhou-architecture-v0.6-e2e.md §3.1 (VirtualFill 4 ts 透传)
//   laowang-wal-framework-cpp-interface-v1.md §6 (WalRecord concept)
//   laotang-audit-schema-v1.1.md §2.x (4 ts offset + audit_id)
//
// 红线:
//   R-20  4 ts 全程携带: fill_event_ts ≤ fill_ds_ts ≤ fill_ingestion_ts ≤ fill_as_of_ts
//         时间戳优先用数据源自带, 禁本地 now() 替代上游 ts
//   R-11  WalKind::Position, paper/live 路径参数化: paper=/var/lib/stcpp/paper/position*.wal
//         live=/var/lib/stcpp/live/position*.wal  (PositionLedger caller 负责选路)
//   ABI   字段顺序/大小锁定 (变更须老韩 + 老周 review + ADR)
//
// 不耻下问:
//   BLAKE3 prev_hash / payload_hash @老唐 — 在 payload 层, 本 struct 不含
//   CRC32C 由 WalWriter framework 算, 本 struct 不含
//   VirtualFill 接入 @小蒋 (paper engine)
//   ML hook 读取 @小邓
//
// 命名约定:
//   时间戳字段名尾加 _ns (含单位), 与 WalRecord concept 要求的方法名区分:
//     字段: fill_event_ts_ns / fill_ds_ts_ns / fill_ingestion_ts_ns / fill_as_of_ts_ns
//     方法: event_ts_ns() / data_source_ts_ns() / ingestion_ts_ns() / as_of_ts_ns()
//
// 注: market_id 用 32B 字符串 (Polymarket market address 32B hex 常见, null-padded).
//     outcome 1B: 0=YES, 1=NO (与 VirtualOrder.side 对齐).
//     所有 USDC 金额单位 = USDC * 1e6 (micro-USDC, int64).
//     exposure_pct 单位 = basis points (0..10000).
//     position_delta: 正=买入/增仓, 负=卖出/减仓.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace stcpp::infra::wal {

// ---------------------------------------------------------------------------
// PositionRecord — WAL payload POD (ABI 锁定, 变更走 ADR)
//
// 字段布局 (#pragma pack(push,1), 无隐式 padding, 精确 152B):
//   offset   0:  market_id[32]           32B  Polymarket market address (null-padded)
//   offset  32:  outcome                  1B  0=YES 1=NO
//   offset  33:  pad0_[7]                 7B  显式 padding (对齐 int64 到 offset 40)
//   offset  40:  position_delta           8B  本次变动量 (USDC*1e6, +买/-卖)
//   offset  48:  position_total           8B  累计净持仓 (USDC*1e6)
//   offset  56:  realized_pnl             8B  已实现 PnL (USDC*1e6)
//   offset  64:  unrealized_pnl           8B  未实现 PnL (USDC*1e6, 估值)
//   offset  72:  entry_avg_price_micro    8B  平均买入价 * 1e6 ([0..1000000])
//   offset  80:  bankroll_total           8B  总资金 (USDC*1e6)
//   offset  88:  consec_loss_count        4B  连续亏损笔数 (RM circuit breaker 输入)
//   offset  92:  exposure_pct             4B  敞口占 bankroll (basis points 0..10000)
//   offset  96:  fill_event_ts_ns         8B  R-20: fill/settle 事件时间 (上游 ts)
//   offset 104:  fill_ds_ts_ns            8B  R-20: 数据源发出时间
//   offset 112:  fill_ingestion_ts_ns     8B  R-20: 本地接收时间 (MONOTONIC_RAW)
//   offset 120:  fill_as_of_ts_ns         8B  R-20: 决策快照时间 (REALTIME)
//   offset 128:  audit_id_[16]           16B  ULID (caller 填, 与 WAL header audit_id 一致)
//   offset 144:  crc32c                   4B  payload CRC32C (framework 算, replay 校验)
//   offset 148:  pad1_[4]                 4B  trailing padding (总 152B)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct PositionRecord {
    // -- 市场标识 ---------------------------------------------------------
    std::array<char, 32> market_id{};     //   0..32
    std::uint8_t outcome{0};              //  32..33  0=YES 1=NO
    std::array<std::uint8_t, 7> pad0_{};  //  33..40  显式 padding

    // -- 仓位核心 (int64, USDC * 1e6) ------------------------------------
    std::int64_t position_delta{0};         //  40..48  本次变动 (+买 -卖)
    std::int64_t position_total{0};         //  48..56  累计净持仓
    std::int64_t realized_pnl{0};           //  56..64  已实现 PnL
    std::int64_t unrealized_pnl{0};         //  64..72  未实现 PnL
    std::int64_t entry_avg_price_micro{0};  //  72..80  平均买入价 * 1e6

    // -- RM circuit breaker 状态 (老韩 P0 #3 核心) ----------------------
    std::int64_t bankroll_total{0};     //  80..88  总资金 (USDC * 1e6)
    std::int32_t consec_loss_count{0};  //  88..92  连续亏损笔数
    std::int32_t exposure_pct{0};       //  92..96  敞口占比 (basis points)

    // -- R-20: 4 时间戳 (ns) ---------------------------------------------
    // 命名含 fill_ 前缀, 与 WalRecord concept 方法名区分 (防字段/方法同名歧义)
    std::int64_t fill_event_ts_ns{0};      //  96..104 fill/settle 上游 ts
    std::int64_t fill_ds_ts_ns{0};         // 104..112 数据源 ts
    std::int64_t fill_ingestion_ts_ns{0};  // 112..120 本地接收 ts
    std::int64_t fill_as_of_ts_ns{0};      // 120..128 决策快照 ts

    // -- 审计标识 + 完整性 -----------------------------------------------
    std::array<std::uint8_t, 16> audit_id_{};  // 128..144 ULID
    std::uint32_t crc32c{0};                   // 144..148 CRC32C (framework 算)
    std::array<std::uint8_t, 4> pad1_{};       // 148..152 trailing padding

    // -----------------------------------------------------------------------
    // WalRecord concept 满足接口 (laowang-wal-framework-cpp-interface-v1.md §6)
    //   方法名必须与 wal_writer.hpp concept 要求一致 (不含 fill_ 前缀)
    // -----------------------------------------------------------------------

    [[nodiscard]] std::int64_t event_ts_ns() const noexcept { return fill_event_ts_ns; }
    [[nodiscard]] std::int64_t data_source_ts_ns() const noexcept { return fill_ds_ts_ns; }
    [[nodiscard]] std::int64_t ingestion_ts_ns() const noexcept { return fill_ingestion_ts_ns; }
    [[nodiscard]] std::int64_t as_of_ts_ns() const noexcept { return fill_as_of_ts_ns; }

    [[nodiscard]] std::array<std::uint8_t, 16> audit_id() const noexcept { return audit_id_; }

    // serialize_into: 写整个 struct 到 out (包含 crc32c 字段, 但此时 crc32c=0;
    // 调用方在 WalWriter::Append 内由 framework 计算后回填).
    // 返回写入字节数.
    [[nodiscard]] std::size_t serialize_into(std::span<std::byte> out) const noexcept {
        constexpr std::size_t sz = sizeof(PositionRecord);
        if (out.size() < sz)
            return 0;
        std::memcpy(out.data(), this, sz);
        return sz;
    }

    [[nodiscard]] static constexpr std::size_t max_serialized_size() noexcept {
        return sizeof(PositionRecord);
    }
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// ABI 硬校验 (变更须 ADR + 老韩/老周 review + 下游 M4.5/ML hook/replay 同步)
// ---------------------------------------------------------------------------

static_assert(sizeof(PositionRecord) == 152, "PositionRecord 必须 152B — ABI 锁定");
static_assert(std::is_trivially_copyable_v<PositionRecord>,
              "PositionRecord 必须 trivially copyable (POD 落盘 + memcpy replay)");
static_assert(std::is_standard_layout_v<PositionRecord>,
              "PositionRecord 必须 standard layout (offsetof 安全)");

// 字段偏移硬校验
static_assert(offsetof(PositionRecord, market_id) == 0);
static_assert(offsetof(PositionRecord, outcome) == 32);
static_assert(offsetof(PositionRecord, position_delta) == 40);
static_assert(offsetof(PositionRecord, position_total) == 48);
static_assert(offsetof(PositionRecord, realized_pnl) == 56);
static_assert(offsetof(PositionRecord, unrealized_pnl) == 64);
static_assert(offsetof(PositionRecord, entry_avg_price_micro) == 72);
static_assert(offsetof(PositionRecord, bankroll_total) == 80);
static_assert(offsetof(PositionRecord, consec_loss_count) == 88);
static_assert(offsetof(PositionRecord, exposure_pct) == 92);
static_assert(offsetof(PositionRecord, fill_event_ts_ns) == 96);
static_assert(offsetof(PositionRecord, fill_ds_ts_ns) == 104);
static_assert(offsetof(PositionRecord, fill_ingestion_ts_ns) == 112);
static_assert(offsetof(PositionRecord, fill_as_of_ts_ns) == 120);
static_assert(offsetof(PositionRecord, audit_id_) == 128);
static_assert(offsetof(PositionRecord, crc32c) == 144);

}  // namespace stcpp::infra::wal
