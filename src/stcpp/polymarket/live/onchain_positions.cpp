// src/stcpp/polymarket/live/onchain_positions.cpp — 链上持仓读取 (libcurl + data-api REST)
//
// owner: 老雷 (GM) | 2026-06-13
// 手写 JSON 解析 (与 onchain_balance / live_order_submitter 同栈; 项目无 simdjson link)。
//   data-api positions 是【扁平对象数组】(字段全标量), 故 brace-depth + string-aware 切对象即可。
#include "stcpp/polymarket/onchain_positions.hpp"

#include <curl/curl.h>

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace stcpp::polymarket {

namespace {

constexpr char kDataApi[] = "https://data-api.polymarket.com/positions";

std::size_t WriteCb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

// 取 "0x..." 20 字节地址的规范小写 0x40hex; 非法 → 空。
std::string NormAddr(const std::string& a) {
    std::string h = (a.rfind("0x", 0) == 0 || a.rfind("0X", 0) == 0) ? a.substr(2) : a;
    if (h.size() != 40) return {};
    for (char& c : h) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return "0x" + h;
}

// 在 obj 内找 "key": 的值起点 (冒号后第一个非空白)。未找到 → npos。
std::size_t FindValueStart(std::string_view obj, std::string_view key) {
    std::string pat = "\"";
    pat.append(key);
    pat.append("\":");
    const auto k = obj.find(pat);
    if (k == std::string_view::npos) return std::string_view::npos;
    std::size_t i = k + pat.size();
    while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
    return i;
}

// 字符串字段 "key":"value" → value (不处理转义, data-api token/cid/无转义)。
std::string ExtractStr(std::string_view obj, std::string_view key) {
    const auto i = FindValueStart(obj, key);
    if (i == std::string_view::npos || i >= obj.size() || obj[i] != '"') return {};
    const auto end = obj.find('"', i + 1);
    if (end == std::string_view::npos) return {};
    return std::string(obj.substr(i + 1, end - i - 1));
}

// 数字字段 "key":123.45 → double (缺/非数 → 默认值)。
double ExtractNum(std::string_view obj, std::string_view key, double dflt) {
    const auto i = FindValueStart(obj, key);
    if (i == std::string_view::npos || i >= obj.size()) return dflt;
    char* endp = nullptr;
    const std::string tmp(obj.substr(i, 40));  // 截一小段够解一个数
    const double v = std::strtod(tmp.c_str(), &endp);
    return (endp == tmp.c_str()) ? dflt : v;
}

// 布尔字段 "key":true/false → bool。
bool ExtractBool(std::string_view obj, std::string_view key) {
    const auto i = FindValueStart(obj, key);
    if (i == std::string_view::npos) return false;
    return obj.compare(i, 4, "true") == 0;
}

// 把 JSON 数组切成各对象 substring (brace-depth + string-aware; 跳引号内的花括号 + 转义)。
std::vector<std::string_view> SplitObjects(std::string_view body) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    int depth = 0;
    bool in_str = false, esc = false;
    bool have_start = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (in_str) {
            if (esc) {
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '"') {
                in_str = false;
            }
            continue;
        }
        if (c == '"') {
            in_str = true;
        } else if (c == '{') {
            if (depth == 0) {
                start = i;
                have_start = true;
            }
            ++depth;
        } else if (c == '}') {
            if (depth > 0) {
                --depth;
                if (depth == 0 && have_start) {
                    out.push_back(body.substr(start, i - start + 1));
                    have_start = false;
                }
            }
        }
    }
    return out;
}

}  // namespace

std::vector<OnchainPosition> ParseOpenPositions(std::string_view json_body) noexcept {
    std::vector<OnchainPosition> out;
    const auto lb = json_body.find('[');
    if (lb == std::string_view::npos) return out;  // 非数组 → 空
    for (std::string_view obj : SplitObjects(json_body.substr(lb))) {
        if (ExtractBool(obj, "redeemable")) continue;  // 已结算 → 跳 (open 才入对账)
        OnchainPosition p;
        p.token_id = ExtractStr(obj, "asset");
        p.condition_id = ExtractStr(obj, "conditionId");
        p.outcome_index = static_cast<int>(ExtractNum(obj, "outcomeIndex", 0.0));
        p.shares = ExtractNum(obj, "size", 0.0);
        p.avg_price = ExtractNum(obj, "avgPrice", 0.0);
        p.neg_risk = ExtractBool(obj, "negativeRisk");
        // 健全性: token/cid 非空, 股>0, 价∈(0,1)。不合格跳 (脏数据不进真钱账本)。
        if (p.token_id.empty() || p.condition_id.empty()) continue;
        if (!(p.shares > 0.0) || !(p.avg_price > 0.0 && p.avg_price < 1.0)) continue;
        out.push_back(std::move(p));
    }
    return out;
}

std::optional<std::vector<OnchainPosition>> ReadOpenPositions(const std::string& funder_hex,
                                                              std::string& err) noexcept {
    const std::string addr = NormAddr(funder_hex);
    if (addr.empty()) {
        err = "funder 地址格式非法";
        return std::nullopt;
    }
    const std::string url = std::string(kDataApi) + "?user=" + addr + "&limit=500";

    CURL* c = curl_easy_init();
    if (!c) {
        err = "curl init 失败";
        return std::nullopt;
    }
    std::string resp;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Mozilla/5.0");  // 默认 UA 被 data-api 403 (实测纪律)
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_NOPROXY, "*");
    const CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        err = std::string("curl: ") + curl_easy_strerror(rc);
        return std::nullopt;
    }
    if (http < 200 || http >= 300) {
        err = "HTTP " + std::to_string(http);
        return std::nullopt;
    }
    // 响应应为 JSON 数组 (空仓 → "[]")。非 '[' 开头视为异常 (别把错误体当无仓)。
    if (resp.find('[') == std::string::npos) {
        err = "响应非数组: " + resp.substr(0, 160);
        return std::nullopt;
    }
    return ParseOpenPositions(resp);
}

}  // namespace stcpp::polymarket
