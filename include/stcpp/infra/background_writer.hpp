// include/stcpp/infra/background_writer.hpp — 异步缓冲落盘器 (2026-06-13, 老板「独立线程异步写 + 缓冲写不高频读写磁盘」)
//
// 决策线程 enqueue (纳秒, 不阻塞), 独立 writer 线程【批量缓冲】写盘 → 把磁盘 IO 与尾延迟和决策环隔离。
//   - AppendLine: 追加一行到文件; writer 攒 per-file 缓冲, 满阈值 OR 定时才 flush → 不高频读写磁盘 (秒级批量)。
//   - 有界队列, 满即丢弃 + 计数 (绝不阻塞生产者; journal 容忍丢失, 内存账本才是真值源)。
//   - 持久 FILE* (开一次复用, 省 fopen/fclose churn)。析构: 停 + drain + flush + close + join。
//   - 用途: 纯写出 sink (fills journal) — 不参与热路径一致性 (不像下单/账本, 故异步安全)。
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace stcpp::infra {

class BackgroundWriter {
public:
    BackgroundWriter() : th_([this] { Run_(); }) {}
    ~BackgroundWriter() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_one();
        if (th_.joinable()) th_.join();
    }
    BackgroundWriter(const BackgroundWriter&) = delete;
    BackgroundWriter& operator=(const BackgroundWriter&) = delete;

    // 决策线程调用: 入队一行 (移动 path+line)。队列满 → 丢弃 + dropped_++。纳秒级, 绝不阻塞。
    void AppendLine(std::string path, std::string line) noexcept {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (q_.size() >= kMaxQueue) {  // 满 → 丢 (不阻塞决策环, 这正是异步的全部意义)
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            q_.push_back(Item{std::move(path), std::move(line)});
            notify = true;
        }
        if (notify) cv_.notify_one();
    }

    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    struct Item {
        std::string path;
        std::string line;
    };
    struct Sink {
        std::FILE* fp{nullptr};
        std::string buf;  // 累积缓冲 (满阈值/定时才落盘)
    };

    static constexpr std::size_t kMaxQueue = 8192;          // 有界 (满即丢; 8192×~640B ≈ 5MB 上限)
    static constexpr std::size_t kFlushBytes = 64 * 1024;   // 缓冲阈值 → 批量写一次
    static constexpr int kFlushIntervalMs = 1000;           // 定时 flush 上限 (秒级落盘, 不高频读写磁盘)

    void Run_() {
        using clock = std::chrono::steady_clock;
        auto last_flush = clock::now();
        for (;;) {
            std::deque<Item> batch;
            bool stopping = false;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait_for(lk, std::chrono::milliseconds(kFlushIntervalMs),
                             [this] { return stop_ || !q_.empty(); });
                batch.swap(q_);  // 一次性取走整批 (writer 持锁极短)
                stopping = stop_;
            }
            for (auto& it : batch) sinks_[it.path].buf += it.line;  // 入 per-file 缓冲 (内存)
            const bool time_up = clock::now() - last_flush >= std::chrono::milliseconds(kFlushIntervalMs);
            for (auto& [path, sink] : sinks_) {
                if (stopping || time_up || sink.buf.size() >= kFlushBytes) FlushSink_(path, sink);
            }
            if (time_up || stopping) last_flush = clock::now();
            if (stopping) break;  // stop: 本轮已 drain+flush batch, 退出
        }
        for (auto& [path, sink] : sinks_) {  // 终关: 兜底 flush + 关 handle
            FlushSink_(path, sink);
            if (sink.fp != nullptr) std::fclose(sink.fp);
        }
    }

    void FlushSink_(const std::string& path, Sink& sink) {
        if (sink.buf.empty()) return;
        if (sink.fp == nullptr) sink.fp = std::fopen(path.c_str(), "a");  // 持久 handle, 开一次
        if (sink.fp == nullptr) { sink.buf.clear(); return; }  // 打不开 (目录缺等) → 丢缓冲, 不卡死
        std::fwrite(sink.buf.data(), 1, sink.buf.size(), sink.fp);
        std::fflush(sink.fp);  // 批量一次 write() 到 OS page cache (非 fsync; OS writeback 落盘)
        sink.buf.clear();
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Item> q_;
    bool stop_{false};
    std::atomic<std::uint64_t> dropped_{0};
    std::unordered_map<std::string, Sink> sinks_;  // 仅 writer 线程访问 (无需锁)
    std::thread th_;  // 末声明: 其他成员先就绪, 再启 writer 线程
};

}  // namespace stcpp::infra
