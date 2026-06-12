// src/stcpp/polymarket/live/onchain_balance.cpp — 链上 pUSD 余额读取 (libcurl + Polygon RPC)
//
// owner: 老雷 (GM) | 2026-06-12
#include "stcpp/polymarket/onchain_balance.hpp"

#include <curl/curl.h>

#include <cctype>
#include <cstdint>
#include <string>

namespace stcpp::polymarket {

namespace {

constexpr char kPusdContract[] = "0xc011a7e12a19f7b1f670d46f03b03f3342e82dfb";
constexpr char kDefaultRpc[] = "https://polygon-bor-rpc.publicnode.com";

std::size_t WriteCb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

// 取 "0x..." 20 字节地址的 40 hex(去 0x, 小写); 非法 → 空。
std::string AddrHex40(const std::string& a) {
    std::string h = (a.rfind("0x", 0) == 0 || a.rfind("0X", 0) == 0) ? a.substr(2) : a;
    if (h.size() != 40) return {};
    for (char& c : h) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return h;
}

// 从 RPC JSON 抠 "result":"0x...." 的 hex(无 0x)。失败 → 空。
std::string ExtractResultHex(const std::string& resp) {
    const auto k = resp.find("\"result\"");
    if (k == std::string::npos) return {};
    const auto q1 = resp.find('"', k + 8);            // result 值开引号
    if (q1 == std::string::npos) return {};
    const auto q2 = resp.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    std::string v = resp.substr(q1 + 1, q2 - q1 - 1);  // 0x....
    if (v.rfind("0x", 0) == 0 || v.rfind("0X", 0) == 0) v = v.substr(2);
    return v;
}

}  // namespace

std::optional<double> ReadPusdBalanceUsd(const std::string& funder_hex, const std::string& rpc_url,
                                         std::string& err) noexcept {
    const std::string addr = AddrHex40(funder_hex);
    if (addr.empty()) {
        err = "funder 地址格式非法";
        return std::nullopt;
    }
    const std::string url = rpc_url.empty() ? kDefaultRpc : rpc_url;

    // eth_call data = balanceOf selector(0x70a08231) + 地址左填 0 至 32 字节(64 hex)。
    const std::string data = "0x70a08231" + std::string(24, '0') + addr;
    const std::string body =
        std::string("{\"jsonrpc\":\"2.0\",\"method\":\"eth_call\",\"params\":[{\"to\":\"") + kPusdContract +
        "\",\"data\":\"" + data + "\"},\"latest\"],\"id\":1}";

    CURL* c = curl_easy_init();
    if (!c) {
        err = "curl init 失败";
        return std::nullopt;
    }
    std::string resp;
    curl_slist* hdr = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(c, CURLOPT_NOPROXY, "*");  // 不走代理 (与 inplay 同纪律)
    const CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        err = std::string("curl: ") + curl_easy_strerror(rc);
        return std::nullopt;
    }
    if (http < 200 || http >= 300) {
        err = "RPC HTTP " + std::to_string(http);
        return std::nullopt;
    }
    if (resp.find("\"error\"") != std::string::npos) {
        err = "RPC error: " + resp.substr(0, 160);
        return std::nullopt;
    }
    const std::string hex = ExtractResultHex(resp);
    if (hex.empty() || hex.size() > 64) {
        err = "RPC result 解析失败: " + resp.substr(0, 160);
        return std::nullopt;
    }
    // hex(uint256) → 余额 micro。pUSD 真实余额远 < 2^64, 取低 64 位足够 (高位若非 0 = 异常)。
    std::uint64_t micro = 0;
    for (char ch : hex) {
        const int d = std::isdigit(static_cast<unsigned char>(ch))
                          ? ch - '0'
                          : std::tolower(static_cast<unsigned char>(ch)) - 'a' + 10;
        if (d < 0 || d > 15) {
            err = "hex 非法";
            return std::nullopt;
        }
        micro = micro * 16 + static_cast<std::uint64_t>(d);
    }
    return static_cast<double>(micro) / 1'000'000.0;
}

}  // namespace stcpp::polymarket
