// stcpp/net/outbound_buffer.hpp — 出站 JSON 复用 buffer
//
// Owner: 老陈 (A-NET-01, #03)  Last review: 2026-05-29
//
// 红线:
//   zero-alloc — 固定 4096B 栈/成员分配, 无 heap
//   非线程安全 — per-worker 独享一个实例 (R-12: vCPU3 worker, 不进 vCPU0 event loop)
//   reset() O(1) — 仅移游标, 无 memset
//   compact JSON only — 跨洋链路带宽紧, 禁 pretty-print
//
// 容量根据: SignedOrder JSON 实测 < 1KB (老李 endpoint-matrix-v3 §A 字段集)
// 4096B = 4x 余量, 单 stack/member 分配对 L1 友好
//
// 与 WalRecord serialize_into 完全独立:
//   serialize_into(span<byte>) — WAL 内部二进制, concept 已锁, 不动
//   OutboundBuffer::append()   — REST/WSS 出站 JSON, 本文件新增

#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace stcpp::net {

inline constexpr std::size_t kMaxOutboundJson = 4096;

class OutboundBuffer {
public:
    OutboundBuffer() noexcept = default;

    // 禁 copy/move — buffer 语义是 per-worker 独占, 不应传递
    OutboundBuffer(const OutboundBuffer&)            = delete;
    OutboundBuffer& operator=(const OutboundBuffer&) = delete;
    OutboundBuffer(OutboundBuffer&&)                 = delete;
    OutboundBuffer& operator=(OutboundBuffer&&)      = delete;

    // 重置游标 O(1), 无 memset. caller 必须在 reset() 前确保 view() 已不被使用.
    void reset() noexcept { write_pos_ = 0; }

    // 追加字符串. 溢出返 false (caller emit metric + drop).
    [[nodiscard]] bool append(std::string_view sv) noexcept {
        if (write_pos_ + sv.size() > kMaxOutboundJson) return false;
        std::memcpy(buf_.data() + write_pos_, sv.data(), sv.size());
        write_pos_ += sv.size();
        return true;
    }

    // 追加单字符 (JSON 分隔符: '{' '}' '[' ']' ':' ',' '"').
    [[nodiscard]] bool append(char c) noexcept {
        if (write_pos_ >= kMaxOutboundJson) return false;
        buf_[write_pos_++] = static_cast<std::byte>(c);
        return true;
    }

    // 返 string_view, 生命期 = buf_ (reset() 前有效).
    // caller (Http2Client::AsyncPost / IWssTransport::AsyncSendText) 必须在 reset() 前完成 IO.
    [[nodiscard]] std::string_view view() const noexcept {
        return {reinterpret_cast<const char*>(buf_.data()), write_pos_};
    }

    [[nodiscard]] std::size_t size()  const noexcept { return write_pos_; }
    [[nodiscard]] bool        empty() const noexcept { return write_pos_ == 0; }

    // remaining() — 给 serializer 做边界预检用
    [[nodiscard]] std::size_t remaining() const noexcept {
        return kMaxOutboundJson - write_pos_;
    }

private:
    std::array<std::byte, kMaxOutboundJson> buf_{};
    std::size_t write_pos_{0};
};

}  // namespace stcpp::net
