// include/stcpp/data/commentaries_parser.hpp — Goalserve commentaries Feed live_stats 提取
//
// Owner: 老雷 (GM) — live_stats 采集 hop 补齐 (5 个 g_*_diff 特征端到端)
// last_review: 2026-05-31
//
// 背景 (数据源已查文档, 非想象):
//   docs/GOALSERVER/soccer-data-feed.md §6 "实时比赛统计 (Live Game Stats Feed)":
//     endpoint: www.goalserve.com/getfeed/{KEY}/commentaries/{league_id}.xml  (每 30s, 顶级联赛)
//     match 信封 (§8 line 502): <match status=".." static_id=".." fix_id=".." id="..">
//     队名 (soccernew 同源约定): <localteam name=".."> / <visitorteam name="..">
//     live_stats (§5 line 300): <live_stats value="ICorner=home:0,away:2|IAttacks=home:33,away:56|.."/>
//   KV 叶子格式 = 真实捕获 (test_live_stats_parser kRealFeedFixture); 解析复用 LiveStatsParser::Parse。
//
// join key (跨 feed): inplay-soccer.gz 活路径只带 inplay_match_id(134xxxxxxx) + 队名 + league_id,
//   不带 static_id (cross-source-mapping §1.2: inplay_match_id 无直接 static_id 对应)。
//   commentaries id(7位) ≠ inplay id(134xxx) → 走 (league_id + 队名) exact join (同源 Goalserve 队名一致)。
//   static_id 仍捕获, 未来 inplay→static_id 映射落地后可切直连 (见 cross-source-mapping W9)。
//
// 设计 (无 XML 库, 全仓 JSON; 属性级提取对嵌套深度健壮):
//   逐 <match 切 span → span 内首个 <localteam name=> / <visitorteam name=> / <live_stats value=>。
//   失配/缺字段 → 该 match valid=false (fail-safe, 下游不 join → 特征保持 sentinel, 绝不造假数据)。
//   R-12: 纯 CPU string 解析, 无 IO/无锁, 可在采集线程调用。R-20: 本头不碰时间戳。
//
// 白名单未开放: commentaries endpoint 需 odds/live plan + IP 白名单。代码就位零网络依赖,
//   白名单一开 fetcher 返回真 XML 即流入。属性名 (value/name/static_id) 待首个真捕获复核。
#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "stcpp/data/live_stats_store.hpp"  // MakeLiveStatsJoinKey (生产/消费两侧共用)
#include "stcpp/data/live_stats_parser.hpp"  // LiveStatsParser::Parse + LiveStatsFields

namespace stcpp::data::livescore {

// 一个 commentaries <match> 的 live_stats 提取结果。
struct CommentariesMatchStats {
    std::string join_key;     // MakeLiveStatsJoinKey(league_id, home, away) — 与活路径 es 对齐
    std::string static_id;    // 跨 feed 映射锚 (未来直连; 现 team-name join)
    std::string home_team;    // <localteam name="..">
    std::string away_team;    // <visitorteam name="..">
    LiveStatsFields stats{};  // <live_stats value="KV"> 解析 (无效字段 = -1)
    bool valid{false};        // 有 live_stats + 双队名 (可 join)
};

class CommentariesParser {
public:
    // Parse — commentaries/{league}.xml body → 每 <match> 的 live_stats。
    //   league_id: 调用方按请求的联赛传入 (信封内 match 不一定重复 league)。
    //   不抛异常; 缺字段的 match valid=false 跳过。
    [[nodiscard]] static std::vector<CommentariesMatchStats> Parse(std::string_view xml,
                                                                   std::string_view league_id) noexcept {
        std::vector<CommentariesMatchStats> out;
        std::size_t pos = 0;
        while (true) {
            const std::size_t m = xml.find("<match", pos);
            if (m == std::string_view::npos) break;
            // span = [m, 下一个 <match 或 EOF)
            std::size_t next = xml.find("<match", m + 6);
            const std::size_t span_end = (next == std::string_view::npos) ? xml.size() : next;
            const std::string_view span = xml.substr(m, span_end - m);
            pos = (next == std::string_view::npos) ? xml.size() : next;

            CommentariesMatchStats rec;
            rec.static_id = std::string(TagAttr(span, "<match", "static_id"));
            rec.home_team = std::string(TagAttr(span, "<localteam", "name"));
            rec.away_team = std::string(TagAttr(span, "<visitorteam", "name"));
            const std::string_view ls_value = TagAttr(span, "<live_stats", "value");

            if (rec.home_team.empty() || rec.away_team.empty() || ls_value.empty()) {
                continue;  // 无 join 料 / 无 live_stats → 跳过 (fail-safe)
            }
            rec.stats = LiveStatsParser::Parse(ls_value);
            if (!rec.stats.any_valid()) continue;  // KV 全空 → 无意义, 跳过
            rec.join_key = MakeLiveStatsJoinKey(league_id, rec.home_team, rec.away_team);
            rec.valid = true;
            out.push_back(std::move(rec));
        }
        return out;
    }

    // ParseInto — 便利: 直接产 join_key → LiveStatsFields 累积进 acc (poller 用)。
    static void ParseInto(LiveStatsMap& acc, std::string_view xml,
                          std::string_view league_id) noexcept {
        for (auto& r : Parse(xml, league_id)) {
            if (r.valid) acc[r.join_key] = r.stats;
        }
    }

    // ParseSoccernewLiveInto — soccernew/live body (一次含所有直播联赛) → acc。
    //   结构: <category id="X"> 包裹多个 <match><live_stats value="KV"/></match>。一个 doc 多联赛 →
    //   league_id 须逐 match 从最近的 <category id> 取 (非单一参数)。league_id 空间 = Goalserve
    //   联赛 id (与 inplay es.league_id 同源, join_key 两端一致)。线性扫描跟踪当前 category。
    //   (2026-06-02 特征审计 #19-23: 替换失效的 per-league commentaries — 真实 live_stats 在 soccernew/live。)
    static void ParseSoccernewLiveInto(LiveStatsMap& acc, std::string_view xml) noexcept {
        std::string current_league;
        std::size_t pos = 0;
        while (pos < xml.size()) {
            const std::size_t cat = xml.find("<category", pos);
            const std::size_t mat = xml.find("<match", pos);
            if (mat == std::string_view::npos)
                break;  // 无更多 match
            if (cat != std::string_view::npos && cat < mat) {
                // 下一个 match 前先遇到 category → 更新当前 league
                const std::size_t gt = xml.find('>', cat);
                if (gt == std::string_view::npos)
                    break;
                current_league = std::string(TagAttr(xml.substr(cat, gt - cat + 1), "<category", "id"));
                pos = gt + 1;
                continue;
            }
            // 处理 match: span = [mat, 下一个 <match 或 EOF)
            const std::size_t next = xml.find("<match", mat + 6);
            const std::size_t span_end = (next == std::string_view::npos) ? xml.size() : next;
            const std::string_view span = xml.substr(mat, span_end - mat);
            pos = span_end;

            const std::string_view home = TagAttr(span, "<localteam", "name");
            const std::string_view away = TagAttr(span, "<visitorteam", "name");
            const std::string_view ls_value = TagAttr(span, "<live_stats", "value");
            if (home.empty() || away.empty() || ls_value.empty() || current_league.empty())
                continue;  // 缺 join 料 → 跳过 (fail-safe, 绝不造假)
            LiveStatsFields stats = LiveStatsParser::Parse(ls_value);
            if (!stats.any_valid())
                continue;
            acc[MakeLiveStatsJoinKey(current_league, home, away)] = stats;
        }
    }

private:
    // TagAttr — span 内首个 `tag` 起始标签里的 `attr="..."` 值 (无引号 KV, value 内无 '"'/' >')。
    //   tag 形如 "<localteam"; attr 形如 "name"。找不到返回空 view。
    [[nodiscard]] static std::string_view TagAttr(std::string_view span, std::string_view tag,
                                                  std::string_view attr) noexcept {
        const std::size_t t = span.find(tag);
        if (t == std::string_view::npos) return {};
        // 该 tag 的结束 '>' (含自闭合 '/>')
        const std::size_t gt = span.find('>', t);
        const std::size_t tag_end = (gt == std::string_view::npos) ? span.size() : gt;
        const std::string_view tag_span = span.substr(t, tag_end - t);
        // 查 attr=" — 必须单词边界 (attr 前是空白/'<', 防 "id" 误匹配 "gid"/"static_id" 的子串)。
        //   (2026-06-02 bug: <category gid="" id="X"> 中 find("id=\"") 命中 gid 的 id → 返回空 league。)
        std::string needle = std::string(attr);
        needle += "=\"";
        std::size_t search_from = 0;
        while (true) {
            const std::size_t a = tag_span.find(needle, search_from);
            if (a == std::string_view::npos) return {};
            const bool boundary =
                (a == 0) || tag_span[a - 1] == ' ' || tag_span[a - 1] == '\t' || tag_span[a - 1] == '<';
            if (boundary) {
                const std::size_t vstart = a + needle.size();
                const std::size_t vend = tag_span.find('"', vstart);
                if (vend == std::string_view::npos) return {};
                return tag_span.substr(vstart, vend - vstart);
            }
            search_from = a + 1;  // 跳过非边界匹配 (如 gid 的 id), 找下一个
        }
    }
};

}  // namespace stcpp::data::livescore
