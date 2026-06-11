#pragma once
// stcpp/data/odds_feed_parser.hpp — Goalserve getodds (跨庄家赔率) XML 解析器
//
// Owner: 老雷 (GM) | last_review: 2026-06-02
//
// 目的 (特征审计 bm_slots 缺口配套):
//   解析 www.goalserve.com/getfeed/{key}/getodds/soccer?cat={X}_10 的多庄家赛果赔率,
//   喂 FeatureStoreGameRow.bm_slots → g_bm_devig_p_yes(#5)/g_bm_overround_avg(#6)/
//   g_valid_bm_count(#7)/x_devig_minus_mid(#16) 特征 (此前 bm_slots 全库无写入 → 恒死)。
//   也是跨庄家 sharp fair 锚 (比单源 bet365 inplay fair 更稳健的共识)。
//
// XML 层级 (实测 2026-06-02, 服务器 HTTPS):
//   <scores sport="basketball" ts="1780372892">                # ts = unix 秒
//     <category name=".." id="..">
//       <match date=".." time=".." status="Not Started" id="1031017">
//         <localteam name="San Antonio Spurs" id="1190"/>
//         <visitorteam name=".." id=".."/>
//         <odds ts="1780372621">                               # ts = unix 秒 (R-20 data_source)
//           <type value="Match Winner|Home/Away|3Way Result" id="N">
//             <bookmaker name="bet365" id="16" stop="False">
//               <odd name="Home|Draw|Away" value="1.44"/>      # decimal odds
//
// 注意:
//   - HTTP 500 但 body 有效 (Goalserve 怪癖) → fetcher 必须读 body, 不当失败 (调用方负责)。
//   - 赛果盘名按运动不同 (soccer="Match Winner" 3-way; basket="Home/Away"/"3Way Result")。
//     用 result-market name allowlist 选第一个匹配的 <type>。
//   - 庄家 id → slot 索引经 kBookmakerIds ABI (8 家); 不在表内的庄家忽略。
//   - 纯函数, 无 I/O; fail-soft: 缺字段/坏值 → 该腿 present=false, 不造假。
//   - participant 按 name (Home/Draw/Away) 区分, 非位置 (Goalserve 不保证序)。

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stcpp/data/data_contract.hpp"      // kNumBookmakers / kBookmakerIds
#include "stcpp/data/goalserve_client.hpp"   // GoalserveSport
#include "stcpp/data/goalserve_record.hpp"   // FourTs / DataSourceTsOrigin

namespace stcpp::data::goalserve {

// 单庄家赛果三腿 decimal 赔率 (home/draw/away; draw=0 表示 2-way 无平局腿)。
struct BookResultOdds {
    double home = 0.0;
    double draw = 0.0;  // 0 = 该盘无平局腿 (2-way 运动)
    double away = 0.0;
    bool   present = false;  // true = 至少 home+away 就位且 > 1.0
};

// 一场比赛的跨庄家赛果赔率 (中性: 未按 PM YES/NO 定向; 定向在 trading_loop 按 yes_is_home)。
struct MatchResultOdds {
    std::string  match_id;
    std::string  home_team;
    std::string  away_team;
    GoalserveSport sport = GoalserveSport::Soccer;
    std::int64_t odds_ts_ns = 0;  // <odds ts> 优先, 回退 <scores ts> (R-20 data_source)
    std::array<BookResultOdds, kNumBookmakers> books{};

    [[nodiscard]] int valid_book_count() const noexcept {
        int n = 0;
        for (const auto& b : books)
            if (b.present)
                ++n;
        return n;
    }
};

namespace odds_feed_detail {

// 取属性值: 在 [tag_begin, tag_end) 内找 attr="..." 的值 (string_view, 无拷贝)。
[[nodiscard]] inline std::string_view AttrValue(std::string_view block, std::string_view attr) noexcept {
    // attr 形如 name=" / id=" / value=" / ts="
    std::string needle;
    needle.reserve(attr.size() + 2);
    needle.append(attr);
    needle.append("=\"");
    const auto k = block.find(needle);
    if (k == std::string_view::npos)
        return {};
    const auto vs = k + needle.size();
    const auto ve = block.find('"', vs);
    if (ve == std::string_view::npos)
        return {};
    return block.substr(vs, ve - vs);
}

[[nodiscard]] inline double ParseDouble(std::string_view sv) noexcept {
    double v = 0.0;
    // 跳过前导空白
    std::size_t i = 0;
    while (i < sv.size() && (sv[i] == ' ' || sv[i] == '\t'))
        ++i;
    const auto r = std::from_chars(sv.data() + i, sv.data() + sv.size(), v);
    if (r.ec != std::errc{})
        return 0.0;
    return v;
}

[[nodiscard]] inline std::int64_t ParseUnixSecToNs(std::string_view sv) noexcept {
    std::int64_t s = 0;
    const auto r = std::from_chars(sv.data(), sv.data() + sv.size(), s);
    if (r.ec != std::errc{} || s <= 0)
        return 0;
    return s * 1'000'000'000LL;
}

// 赛果盘选择 — 运动感知优先级。返回 name 的优先级 (越小越优先, -1=非赛果盘)。
//   soccer/3-way 运动: 偏好含平局的 "Match Winner"/"1X2"。
//   2-way 运动 (basket/amfootball/hockey/baseball/tennis/volleyball): 偏好 "Home/Away"/"Moneyline",
//     **避开 "3Way Result"** (篮球等的 3Way 含 regulation 平局腿, 非 PM moneyline 语义)。
//   未知名 → -1 (跳过)。
[[nodiscard]] inline int ResultMarketPriority(std::string_view name, GoalserveSport sport) noexcept {
    const bool three_way = (sport == GoalserveSport::Soccer);
    if (three_way) {
        if (name == "Match Winner") return 0;
        if (name == "1X2") return 1;
        if (name == "Match Result") return 2;
        if (name == "Result") return 3;
        if (name == "3Way Result") return 4;
        return -1;
    }
    // 2-way 运动: 优先纯两向赛果盘
    if (name == "Home/Away") return 0;
    if (name == "Moneyline") return 1;
    if (name == "Winner") return 2;
    if (name == "To Win") return 3;
    if (name == "Match Winner") return 4;  // 部分 2-way 运动也叫 Match Winner
    // 注意: 不收 "3Way Result" (2-way 运动的 3Way 含平局腿, 语义错)
    return -1;
}

// bookmaker id → kBookmakerIds slot 索引; 不在表内 → -1。
[[nodiscard]] inline int SlotForBookmakerId(std::int32_t bm_id) noexcept {
    for (std::size_t i = 0; i < kBookmakerIds.size(); ++i)
        if (kBookmakerIds[i] == bm_id)
            return static_cast<int>(i);
    return -1;
}

}  // namespace odds_feed_detail

// ---------------------------------------------------------------------------
// ParseGetOddsXml — getodds XML body → 每场 MatchResultOdds。
//   ingestion_ts_ns: fetcher 收到 body 的本地刻 (R-20 ingestion; data_source 用 feed ts)。
//   fail-soft: 任何坏块跳过, 不抛, 返回能解析出的部分。
// ---------------------------------------------------------------------------
[[nodiscard]] inline std::vector<MatchResultOdds> ParseGetOddsXml(
    std::string_view xml, GoalserveSport sport, std::int64_t ingestion_ts_ns) {
    using namespace odds_feed_detail;
    std::vector<MatchResultOdds> out;

    // <scores ts="..."> 作 feed 级 data_source 回退
    std::int64_t scores_ts_ns = 0;
    if (const auto sk = xml.find("<scores"); sk != std::string_view::npos) {
        const auto se = xml.find('>', sk);
        if (se != std::string_view::npos)
            scores_ts_ns = ParseUnixSecToNs(AttrValue(xml.substr(sk, se - sk), "ts"));
    }

    std::size_t pos = 0;
    while (true) {
        const auto ms = xml.find("<match", pos);
        if (ms == std::string_view::npos)
            break;
        // match 块 = [ms, 下一个 <match 或 末尾) — 用 </match> 收边更准
        auto me = xml.find("</match>", ms);
        const auto next_match = xml.find("<match", ms + 6);
        if (me == std::string_view::npos)
            me = (next_match == std::string_view::npos) ? xml.size() : next_match;
        else
            me += 8;  // 含 </match>
        const std::string_view block = xml.substr(ms, me - ms);
        pos = me;

        // <match ...> 头 (到第一个 '>')
        const auto match_hdr_end = block.find('>');
        if (match_hdr_end == std::string_view::npos)
            continue;
        const std::string_view match_hdr = block.substr(0, match_hdr_end);

        MatchResultOdds rec;
        rec.sport = sport;
        rec.match_id = std::string(AttrValue(match_hdr, "id"));
        if (rec.match_id.empty())
            continue;

        // localteam / visitorteam name
        if (const auto lk = block.find("<localteam"); lk != std::string_view::npos) {
            const auto le = block.find("/>", lk);
            if (le != std::string_view::npos)
                rec.home_team = std::string(AttrValue(block.substr(lk, le - lk), "name"));
        }
        // 客队标签跨运动不一致: basket="<awayteam>", soccer="<visitorteam>" — 两者都试。
        {
            auto vk = block.find("<awayteam");
            if (vk == std::string_view::npos)
                vk = block.find("<visitorteam");
            if (vk != std::string_view::npos) {
                const auto ve = block.find("/>", vk);
                if (ve != std::string_view::npos)
                    rec.away_team = std::string(AttrValue(block.substr(vk, ve - vk), "name"));
            }
        }

        // <odds ts="...">
        const auto ok = block.find("<odds");
        if (ok == std::string_view::npos)
            continue;
        const auto odds_hdr_end = block.find('>', ok);
        if (odds_hdr_end != std::string_view::npos) {
            const std::int64_t ots =
                ParseUnixSecToNs(AttrValue(block.substr(ok, odds_hdr_end - ok), "ts"));
            rec.odds_ts_ns = (ots > 0) ? ots : scores_ts_ns;
        }
        if (rec.odds_ts_ns <= 0)
            rec.odds_ts_ns = scores_ts_ns;

        // 选赛果盘 <type>: 扫所有 type, 按运动感知优先级取**最优**的 (非第一个匹配)。
        //   type 块 = [hdr_end+1, 下一个 <type 或 </odds>)。
        std::size_t tp = ok;
        std::size_t result_type_begin = std::string_view::npos, result_type_end = std::string_view::npos;
        int best_priority = 1 << 30;
        while (true) {
            const auto ts_pos = block.find("<type", tp);
            if (ts_pos == std::string_view::npos)
                break;
            const auto ts_hdr_end = block.find('>', ts_pos);
            if (ts_hdr_end == std::string_view::npos)
                break;
            const std::string_view type_hdr = block.substr(ts_pos, ts_hdr_end - ts_pos);
            const std::string_view tname = AttrValue(type_hdr, "value");
            const int prio = ResultMarketPriority(tname, sport);
            if (prio >= 0 && prio < best_priority) {
                best_priority = prio;
                result_type_begin = ts_hdr_end + 1;
                const auto nt = block.find("<type", ts_hdr_end);
                const auto oc = block.find("</odds>", ts_hdr_end);
                result_type_end = std::min(nt == std::string_view::npos ? block.size() : nt,
                                           oc == std::string_view::npos ? block.size() : oc);
            }
            tp = ts_hdr_end + 1;
        }
        if (result_type_begin == std::string_view::npos)
            continue;  // 该场无赛果盘 → 跳过 (不造假)

        const std::string_view tblock = block.substr(result_type_begin, result_type_end - result_type_begin);

        // 遍历 <bookmaker id="X"> ... 内的 <odd name=value>
        std::size_t bp = 0;
        while (true) {
            const auto bk = tblock.find("<bookmaker", bp);
            if (bk == std::string_view::npos)
                break;
            const auto bk_hdr_end = tblock.find('>', bk);
            if (bk_hdr_end == std::string_view::npos)
                break;
            const std::string_view bm_hdr = tblock.substr(bk, bk_hdr_end - bk);
            // bookmaker 块 = [hdr_end, </bookmaker> 或 下一个 <bookmaker)
            auto be = tblock.find("</bookmaker>", bk_hdr_end);
            const auto next_bm = tblock.find("<bookmaker", bk_hdr_end);
            const std::size_t bm_inner_end =
                std::min(be == std::string_view::npos ? tblock.size() : be,
                         next_bm == std::string_view::npos ? tblock.size() : next_bm);
            const std::string_view bm_inner = tblock.substr(bk_hdr_end + 1, bm_inner_end - bk_hdr_end - 1);
            bp = bm_inner_end;

            std::int32_t bm_id = 0;
            const auto idsv = AttrValue(bm_hdr, "id");
            std::from_chars(idsv.data(), idsv.data() + idsv.size(), bm_id);
            const int slot = SlotForBookmakerId(bm_id);
            if (slot < 0)
                continue;  // 不在 8 家 ABI 内
            // stop="True" → 该庄家暂停, 跳过 (fail-closed)
            if (AttrValue(bm_hdr, "stop") == "True")
                continue;

            BookResultOdds bro;
            // 遍历 <odd name=value>
            std::size_t op = 0;
            while (true) {
                const auto odk = bm_inner.find("<odd", op);
                if (odk == std::string_view::npos)
                    break;
                const auto od_end = bm_inner.find("/>", odk);
                const auto od_end2 = bm_inner.find('>', odk);
                const std::size_t ohe = (od_end != std::string_view::npos) ? od_end : od_end2;
                if (ohe == std::string_view::npos)
                    break;
                const std::string_view odd_hdr = bm_inner.substr(odk, ohe - odk);
                const std::string_view oname = AttrValue(odd_hdr, "name");
                const double oval = ParseDouble(AttrValue(odd_hdr, "value"));
                if (oname == "Home")
                    bro.home = oval;
                else if (oname == "Away")
                    bro.away = oval;
                else if (oname == "Draw")
                    bro.draw = oval;
                op = ohe + 1;
            }
            bro.present = (bro.home > 1.0 && bro.away > 1.0);
            if (bro.present)
                rec.books[static_cast<std::size_t>(slot)] = bro;
        }

        if (rec.valid_book_count() > 0)
            out.push_back(std::move(rec));
    }

    (void)ingestion_ts_ns;  // 调用方填 FourTs.ingestion; 解析器只产 data_source(odds_ts)
    return out;
}

// ---------------------------------------------------------------------------
// ParseInplayMappingXml — inplay-mapping XML → (pregame_match_id → inplay_match_id)。
//   桥接 getodds (pregame 空间) ↔ 系统 join key (inplay_match_id)。Agent C 实测格式:
//   <mappings sport=".."><match pregame_match_id="1030519" inplay_match_id="195604499" .../>
//   纯函数, fail-soft。
// ---------------------------------------------------------------------------
[[nodiscard]] inline std::vector<std::pair<std::string, std::string>> ParseInplayMappingXml(
    std::string_view xml) {
    using namespace odds_feed_detail;
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t pos = 0;
    while (true) {
        const auto ms = xml.find("<match", pos);
        if (ms == std::string_view::npos)
            break;
        const auto me = xml.find('>', ms);
        if (me == std::string_view::npos)
            break;
        const std::string_view hdr = xml.substr(ms, me - ms);
        pos = me + 1;
        const std::string_view pre = AttrValue(hdr, "pregame_match_id");
        const std::string_view inp = AttrValue(hdr, "inplay_match_id");
        if (!pre.empty() && !inp.empty())
            out.emplace_back(std::string(pre), std::string(inp));
    }
    return out;
}

}  // namespace stcpp::data::goalserve
