// src/stcpp/polymarket/live/live_order_submitter.cpp — 生产实盘下单器 (LIVE only)
//
// Owner: GM (老雷) 2026-05-31。固化自 experiments/laosun-laoli-live-order (实盘验证)。
#include "stcpp/polymarket/live/live_order_submitter.hpp"

#include "stcpp/crypto/eip712_v2.hpp"
#include "stcpp/crypto/secp256k1_signer.hpp"
#include "stcpp/polymarket/clob_wire.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include <curl/curl.h>

namespace stcpp::polymarket {

namespace {

std::string ToHex(const std::uint8_t* b, std::size_t n, bool prefix = true) {
    static const char* h = "0123456789abcdef";
    std::string s = prefix ? "0x" : "";
    for (std::size_t i = 0; i < n; ++i) {
        s += h[b[i] >> 4];
        s += h[b[i] & 0xf];
    }
    return s;
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool HexToBytes(const std::string& hex, std::uint8_t* out, std::size_t n) {
    const char* p = hex.c_str();
    if (hex.size() >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (std::strlen(p) != n * 2) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const int hi = HexVal(p[i * 2]), lo = HexVal(p[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::uint64_t RandomSalt() {
    std::uint64_t salt = 0;
    const int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (::read(fd, &salt, sizeof(salt)) != static_cast<ssize_t>(sizeof(salt))) salt = 0;
        ::close(fd);
    }
    return salt >> 1;  // < 2^63, 正十进制
}

// 极简 JSON 字段提取 (CLOB 响应已知形态; 不引 simdjson 避免重依赖)。
std::string ExtractStr(const std::string& json, const std::string& key) {
    const std::string pat = "\"" + key + "\":\"";
    const auto pos = json.find(pat);
    if (pos == std::string::npos) return {};
    const auto start = pos + pat.size();
    const auto end = json.find('"', start);
    if (end == std::string::npos) return {};
    return json.substr(start, end - start);
}

bool ExtractBool(const std::string& json, const std::string& key) {
    const auto pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) return false;
    return json.find("true", pos) == pos + key.size() + 3;
}

std::size_t WriteCb(char* p, std::size_t sz, std::size_t nm, void* ud) {
    static_cast<std::string*>(ud)->append(p, sz * nm);
    return sz * nm;
}

}  // namespace

bool LiveCredentials::FromEnv(LiveCredentials& out, std::string& err) {
    struct E {
        const char* name;
        std::string* dst;
    };
    const char* pk = std::getenv("WALLET_PRIVATE_KEY");
    if (!pk) { err = "缺 WALLET_PRIVATE_KEY"; return false; }
    if (!HexToBytes(pk, out.private_key.data(), 32)) { err = "WALLET_PRIVATE_KEY 格式错(应 0x+64hex)"; return false; }
    const E fields[] = {
        {"POLYMARKET_FUNDER_ADDRESS", &out.funder_address},
        {"POLYMARKET_API_KEY", &out.api_key},
        {"POLYMARKET_API_SECRET", &out.api_secret},
        {"POLYMARKET_API_PASSPHRASE", &out.api_passphrase},
    };
    for (const auto& f : fields) {
        const char* v = std::getenv(f.name);
        if (!v || !*v) { err = std::string("缺 ") + f.name; return false; }
        *f.dst = v;
    }
    return true;
}

LiveOrderSubmitter::LiveOrderSubmitter(LiveCredentials creds, std::string endpoint)
    : creds_(std::move(creds)), endpoint_(std::move(endpoint)) {
    crypto::Bytes32 key{};
    std::memcpy(key.data(), creds_.private_key.data(), 32);
    crypto::Address eoa{};
    if (crypto::DeriveAddress(key, eoa)) {
        std::memcpy(eoa_.data(), eoa.data(), 20);
        eoa_lc_ = ToHex(eoa_.data(), 20);
        ready_ = true;
    }
}

LiveOrderSubmitter::~LiveOrderSubmitter() {
    if (curl_) curl_easy_cleanup(static_cast<CURL*>(curl_));  // 释放持久 keep-alive handle
}

LiveOrderResult LiveOrderSubmitter::Submit(const LiveOrderRequest& req) noexcept {
    LiveOrderResult r;
    if (!ready_) { r.error = "submitter not ready (私钥无效)"; return r; }

    // ---- 1. 构造 V2 order + digest ----
    crypto::OrderV2 ord;
    ord.salt = crypto::U256FromU64(RandomSalt());
    if (!crypto::AddressFromHex(creds_.funder_address, ord.maker)) { r.error = "funder 地址格式错"; return r; }
    std::memcpy(ord.signer.data(), eoa_.data(), 20);
    if (!crypto::U256FromDecimal(req.token_id, ord.token_id)) { r.error = "token_id 非法"; return r; }
    ord.maker_amount = req.maker_amount;
    ord.taker_amount = req.taker_amount;
    ord.side = req.is_buy ? 0 : 1;
    ord.signature_type = static_cast<std::uint8_t>(req.signature_type);
    const auto now_s = static_cast<std::uint64_t>(::time(nullptr));
    ord.timestamp_ms = now_s * 1000ULL;
    // metadata/builder 默认 0

    const auto domain = crypto::CtfExchangeV2Domain(req.neg_risk);
    const crypto::Bytes32 digest = crypto::ComputeOrderV2Digest(ord, domain);

    // ---- 2. 签名 + recover 自检 (P1-5) ----
    crypto::Bytes32 key{};
    std::memcpy(key.data(), creds_.private_key.data(), 32);
    crypto::Signature65 sig{};
    if (!crypto::SignDigest(digest, key, sig)) { r.error = "签名失败"; return r; }
    crypto::Address recovered{};
    if (!crypto::RecoverAddress(digest, sig, recovered) ||
        std::memcmp(recovered.data(), eoa_.data(), 20) != 0) {
        r.error = "recover 自检失败 (签名参数错)";
        return r;
    }

    // ---- 3. wire body ----
    OrderV2Wire w;
    w.salt = 0;  // 见下: salt 须与签名一致, 用 U256→u64 还原
    // salt 在 ord.salt(Bytes32); 还原低 64 位 (RandomSalt 保证 < 2^63 → 全在低 64 位)
    {
        std::uint64_t s = 0;
        for (int i = 0; i < 8; ++i) s = (s << 8) | ord.salt[24 + static_cast<std::size_t>(i)];
        w.salt = s;
    }
    w.maker = creds_.funder_address;
    w.signer = eoa_lc_;
    w.token_id = req.token_id;
    w.maker_amount = req.maker_amount;
    w.taker_amount = req.taker_amount;
    w.is_buy = req.is_buy;
    w.signature_type = req.signature_type;
    w.timestamp_ms = ord.timestamp_ms;
    w.signature = ToHex(sig.data(), 65);
    w.owner = creds_.api_key;
    w.order_type = req.order_type;
    const std::string body = BuildOrderV2Body(w);

    // ---- 4. L2 HMAC ----
    const std::string ts = std::to_string(now_s);
    const std::string poly_sig = ComputeL2Signature(creds_.api_secret, ts, "POST", "/order", body);

    // ---- 5. POST ----
    // 持久 handle 复用 (2026-06-13): 首次 init, 之后跨 Submit 复用 → libcurl 连接缓存保持 TLS 暖
    //   (冷 ~31ms → 暖 ~14ms)。curl_easy_reset 清选项但【保留 live 连接/DNS/会话缓存】(libcurl 文档保证)
    //   → keep-alive 生效。单线程串行 (loop_thread_), handle 无需加锁。析构 cleanup。
    CURL* c = static_cast<CURL*>(curl_);
    if (!c) {
        c = curl_easy_init();
        curl_ = c;
    }
    if (!c) { r.error = "curl init 失败"; return r; }
    curl_easy_reset(c);  // 重置选项, 保留连接池 → 暖复用
    std::string resp;
    curl_slist* hdr = nullptr;
    hdr = curl_slist_append(hdr, "Content-Type: application/json");
    hdr = curl_slist_append(hdr, ("POLY_ADDRESS: " + eoa_lc_).c_str());
    hdr = curl_slist_append(hdr, ("POLY_SIGNATURE: " + poly_sig).c_str());
    hdr = curl_slist_append(hdr, ("POLY_TIMESTAMP: " + ts).c_str());
    hdr = curl_slist_append(hdr, ("POLY_API_KEY: " + creds_.api_key).c_str());
    hdr = curl_slist_append(hdr, ("POLY_PASSPHRASE: " + creds_.api_passphrase).c_str());
    const std::string url = endpoint_ + "/order";
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    // 超时收紧 (2026-06-12 性能审计): 下单往返实测 warm ~14ms / 冷 ~31ms; 旧 8s = 560× 冗余且白卡决策线程。
    //   total 3s (>3s 没回应基本卡死/失败, FOK 更该早放弃) + connect 2s (连接阶段单独上限) →
    //   worst-case 决策线程阻塞 8s→3s。NOSIGNAL: 多线程 daemon 里 curl 超时禁用 SIGALRM (线程安全, best practice)。
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    const CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdr);
    // 不 cleanup c — 持久复用 (析构时释放)。连接留在 libcurl 缓存供下单复用 (keep-alive)。

    r.http_status = static_cast<int>(http);
    r.raw_response = resp;
    if (rc != CURLE_OK) { r.error = std::string("curl: ") + curl_easy_strerror(rc); return r; }

    // ---- 6. 解析响应 ----
    r.order_id = ExtractStr(resp, "orderID");
    r.status = ExtractStr(resp, "status");
    {  // transactionsHashes 是数组: "transactionsHashes":["0x..."]
        const auto pos = resp.find("\"transactionsHashes\":[\"");
        if (pos != std::string::npos) {
            const auto start = pos + std::strlen("\"transactionsHashes\":[\"");
            const auto end = resp.find('"', start);
            if (end != std::string::npos) r.transaction_hash = resp.substr(start, end - start);
        }
    }
    {  // 回执实际成交量 (老韩: 以 CLOB 回执为唯一真相, 非请求量)
        const std::string mk = ExtractStr(resp, "makingAmount");
        const std::string tk = ExtractStr(resp, "takingAmount");
        if (!mk.empty()) r.making_amount = std::strtod(mk.c_str(), nullptr);
        if (!tk.empty()) r.taking_amount = std::strtod(tk.c_str(), nullptr);
    }
    const std::string errmsg = ExtractStr(resp, "errorMsg");
    const std::string err = ExtractStr(resp, "error");
    r.error = !errmsg.empty() ? errmsg : err;
    r.success = (http >= 200 && http < 300) && ExtractBool(resp, "success") && r.error.empty();
    return r;
}

}  // namespace stcpp::polymarket
