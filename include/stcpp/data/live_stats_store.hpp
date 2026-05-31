// include/stcpp/data/live_stats_store.hpp — live_stats 快照 store + 跨 feed join key
//
// Owner: 老雷 (GM) — live_stats 采集 hop 补齐
// last_review: 2026-05-31
//
// join_key(league_id, home, away) → LiveStatsFields 的 in-mem 快照 store。
//   poller 线程 Publish, daemon refresh 线程 Get/GetSnapshot。范式照抄 SettlementStore:
//   shared_ptr<const map> + mutex 短锁 swap (Apple libc++ atomic<shared_ptr> 不全)。
//   R-12: 锁持有 = shared_ptr 拷贝 (~5ns), hash lookup 锁外。
//
// join key 在生产侧 (CommentariesParser) 与消费侧 (paper_loop es) 共用 MakeLiveStatsJoinKey,
//   保证两端构造一致 (同源 Goalserve 队名; normalize = trim+lower 抗大小写/空白)。
#pragma once

#include <cctype>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "stcpp/data/live_stats_parser.hpp"  // LiveStatsFields

namespace stcpp::data::livescore {

// NormalizeTeam — trim 两端空白 + 转小写 (Goalserve 同源队名一致, normalize 仅作安全裕度)。
[[nodiscard]] inline std::string NormalizeTeam(std::string_view s) noexcept {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::string out;
    out.reserve(e - b);
    for (std::size_t i = b; i < e; ++i) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
    }
    return out;
}

// MakeLiveStatsJoinKey — 跨 feed 联结键 (生产/消费两侧必须用同一函数)。
//   格式: "<league_id>|<home_norm>|<away_norm>"。league_id 不 normalize (纯数字)。
[[nodiscard]] inline std::string MakeLiveStatsJoinKey(std::string_view league_id, std::string_view home,
                                                      std::string_view away) noexcept {
    std::string k;
    k.reserve(league_id.size() + home.size() + away.size() + 2);
    k.append(league_id.data(), league_id.size());
    k.push_back('|');
    k += NormalizeTeam(home);
    k.push_back('|');
    k += NormalizeTeam(away);
    return k;
}

using LiveStatsMap = std::unordered_map<std::string, LiveStatsFields>;

class LiveStatsStore {
public:
    LiveStatsStore() = default;
    LiveStatsStore(const LiveStatsStore&) = delete;
    LiveStatsStore& operator=(const LiveStatsStore&) = delete;
    LiveStatsStore(LiveStatsStore&&) = delete;
    LiveStatsStore& operator=(LiveStatsStore&&) = delete;
    ~LiveStatsStore() = default;

    void Publish(std::shared_ptr<const LiveStatsMap> next) noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        front_ = std::move(next);
    }

    [[nodiscard]] std::optional<LiveStatsFields> Get(const std::string& join_key) const noexcept {
        std::shared_ptr<const LiveStatsMap> snap;
        {
            std::lock_guard<std::mutex> lk(mu_);
            snap = front_;
        }
        if (!snap) return std::nullopt;
        auto it = snap->find(join_key);
        if (it == snap->end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::shared_ptr<const LiveStatsMap> GetSnapshot() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return front_;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return front_ ? front_->size() : 0;
    }

private:
    mutable std::mutex mu_;
    std::shared_ptr<const LiveStatsMap> front_;
};

}  // namespace stcpp::data::livescore
