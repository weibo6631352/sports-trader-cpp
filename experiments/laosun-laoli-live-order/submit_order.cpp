// submit_order.cpp — Polymarket CLOB V2 实盘下单 (Phase 2: 签名 + L2 HMAC + POST)
//
// 链路: EIP-712 V2 签名 (Phase 1 已验) → 拼 wire body → L2 HMAC-SHA256 鉴权头
//       → libcurl POST https://clob.polymarket.com/order
//
// 默认: FOK BUY 5 shares @ limit (argv 可覆盖), 真金白银小额测试单 (用户目标: 达成交易)。
// 红线: 私钥/api_secret/passphrase/POLY_SIGNATURE 任何片段绝不 log。
//
// 用法: ./submit_order <tokenId> <makerAmount_micro> <takerAmount_micro> [DRYRUN]
//   DRYRUN: 只签名+拼 body+算 HMAC, 不真正 POST (打印将发送的内容, redact 敏感头)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include <curl/curl.h>
#include <openssl/hmac.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include "keccak256.h"

namespace {

bool hex2bytes(const char* hex, std::uint8_t* out, std::size_t outlen) {
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
    if (std::strlen(hex) != outlen * 2) return false;
    for (std::size_t i = 0; i < outlen; ++i) {
        unsigned b;
        if (std::sscanf(hex + i * 2, "%2x", &b) != 1) return false;
        out[i] = static_cast<std::uint8_t>(b);
    }
    return true;
}
std::string bytes2hex(const std::uint8_t* b, std::size_t n, bool prefix = true) {
    static const char* h = "0123456789abcdef";
    std::string s = prefix ? "0x" : "";
    for (std::size_t i = 0; i < n; ++i) { s += h[b[i] >> 4]; s += h[b[i] & 0xf]; }
    return s;
}
void u64_to_32(std::uint64_t v, std::uint8_t out[32]) {
    std::memset(out, 0, 32);
    for (int i = 0; i < 8; ++i) out[31 - i] = static_cast<std::uint8_t>(v >> (8 * i));
}
void addr_to_32(const std::uint8_t a[20], std::uint8_t out[32]) { std::memset(out, 0, 32); std::memcpy(out + 12, a, 20); }
bool dec_to_32(const char* dec, std::uint8_t out[32]) {
    std::memset(out, 0, 32);
    for (const char* p = dec; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        unsigned carry = static_cast<unsigned>(*p - '0');
        for (int i = 31; i >= 0; --i) { unsigned x = static_cast<unsigned>(out[i]) * 10 + carry; out[i] = static_cast<std::uint8_t>(x & 0xff); carry = x >> 8; }
        if (carry) return false;
    }
    return true;
}

// base64url 解码 (含 padding) — 用于 api_secret
std::string b64url_decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-') return 62;
        if (c == '_') return 63;
        return -1;
    };
    std::string out;
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((buf >> bits) & 0xff)); }
    }
    return out;
}
// base64url 编码 (保留 = padding, 不 strip — 老李 HMAC R2)
std::string b64url_encode(const std::uint8_t* d, std::size_t n) {
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    for (std::size_t i = 0; i < n; i += 3) {
        unsigned b0 = d[i], b1 = (i + 1 < n) ? d[i + 1] : 0, b2 = (i + 2 < n) ? d[i + 2] : 0;
        out.push_back(A[b0 >> 2]);
        out.push_back(A[((b0 & 3) << 4) | (b1 >> 4)]);
        out.push_back((i + 1 < n) ? A[((b1 & 15) << 2) | (b2 >> 6)] : '=');
        out.push_back((i + 2 < n) ? A[b2 & 63] : '=');
    }
    return out;
}

const char* env_or_die(const char* k) {
    const char* v = std::getenv(k);
    if (!v) { std::printf("❌ 缺环境变量 %s (source .env)\n", k); std::exit(1); }
    return v;
}

size_t write_cb(char* p, size_t sz, size_t nm, void* ud) {
    static_cast<std::string*>(ud)->append(p, sz * nm);
    return sz * nm;
}

}  // namespace

int main(int argc, char** argv) {
    // keccak self-test (小白 P1-4)
    { std::uint8_t o[32]; kc::keccak256(nullptr, 0, o);
      if (bytes2hex(o, 32) != "0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470") { std::printf("❌ keccak self-test FAIL\n"); return 1; } }

    const char* tokenId   = (argc > 1) ? argv[1] : "49372751764221434001147103326275206782320924549228867963338278477320771811813";
    std::uint64_t makerAmt = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 90000ULL;   // $0.09 max (limit 0.018)
    std::uint64_t takerAmt = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 5000000ULL;  // 5 shares
    bool dryrun = (argc > 4) && std::string(argv[4]) == "DRYRUN";
    // SIDE: 0=BUY 1=SELL (env)。SELL: makerAmount=卖出 shares(micro), takerAmount=收到 USDC(micro)
    const char* side_env = std::getenv("SIDE");
    const std::uint64_t side = side_env ? std::strtoull(side_env, nullptr, 10) : 0;

    // 份额输入便利层 (对齐官方 py-clob-client-v2 get_order_amounts):
    //   SHARES=<份额> PRICE=<价> [TICK=0.001] → 自动算 makerAmount/takerAmount, 免手算 micro。
    //   BUY : takerAmount=份额, makerAmount=份额×价 ;  SELL: makerAmount=份额, takerAmount=份额×价
    if (const char* sh = std::getenv("SHARES")) {
        const char* pr = std::getenv("PRICE");
        if (!pr) { std::printf("❌ SHARES 需配 PRICE (如 PRICE=0.02)\n"); return 1; }
        double shares = std::strtod(sh, nullptr);
        double price = std::strtod(pr, nullptr);
        double tick = std::getenv("TICK") ? std::strtod(std::getenv("TICK"), nullptr) : 0.001;
        price = std::round(price / tick) * tick;            // 价取整到 tick
        double sz = std::floor(shares * 100.0) / 100.0;     // 份额下取整到 2 位小数
        double usdc = sz * price;
        if (side == 0) {  // BUY: taker=份额, maker=USDC
            takerAmt = static_cast<std::uint64_t>(std::llround(sz * 1e6));
            makerAmt = static_cast<std::uint64_t>(std::llround(usdc * 1e6));
        } else {          // SELL: maker=份额, taker=USDC
            makerAmt = static_cast<std::uint64_t>(std::llround(sz * 1e6));
            takerAmt = static_cast<std::uint64_t>(std::llround(usdc * 1e6));
        }
    }
    // signatureType: 0=EOA 1=POLY_PROXY 2=POLY_GNOSIS_SAFE (env SIG_TYPE 覆盖, 默认 1)
    const char* st_env = std::getenv("SIG_TYPE");
    const std::uint64_t sigType = st_env ? std::strtoull(st_env, nullptr, 10) : 1;
    // feeRateBps: 必须 == 市场 /fee-rate base_fee (env FEE), 否则 order_version_mismatch
    const char* fee_env = std::getenv("FEE");
    const std::uint64_t feeBps = fee_env ? std::strtoull(fee_env, nullptr, 10) : 0;

    const char* pk_hex     = env_or_die("WALLET_PRIVATE_KEY");
    const char* funder     = env_or_die("POLYMARKET_FUNDER_ADDRESS");
    const char* api_key    = env_or_die("POLYMARKET_API_KEY");
    const char* api_secret = env_or_die("POLYMARKET_API_SECRET");
    const char* api_pass   = env_or_die("POLYMARKET_API_PASSPHRASE");

    std::uint8_t funder20[20], pk[32];
    if (!hex2bytes(funder, funder20, 20) || !hex2bytes(pk_hex, pk, 32)) { std::printf("❌ funder/私钥格式错\n"); return 1; }

    std::uint64_t now_s  = static_cast<std::uint64_t>(::time(nullptr));
    std::uint64_t now_ms = now_s * 1000ULL;
    if (const char* tse = std::getenv("TS_MS")) now_ms = std::strtoull(tse, nullptr, 10);  // 比对用固定 ts

    // salt (env SALT 覆盖, 用于与参考实现 digest 比对)
    std::uint64_t salt = 0;
    if (const char* se = std::getenv("SALT")) {
        salt = std::strtoull(se, nullptr, 10);
    } else {
        int fd = ::open("/dev/urandom", O_RDONLY); if (fd < 0 || ::read(fd, &salt, 8) != 8) { std::printf("❌ urandom\n"); return 1; } ::close(fd); salt >>= 1;
    }

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    std::uint8_t eoa20[20];
    { secp256k1_pubkey pub;
      if (!secp256k1_ec_pubkey_create(ctx, &pub, pk)) { std::memset(pk, 0, 32); std::printf("❌ 私钥无效\n"); return 1; }
      std::uint8_t pub65[65]; std::size_t pl = 65; secp256k1_ec_pubkey_serialize(ctx, pub65, &pl, &pub, SECP256K1_EC_UNCOMPRESSED);
      std::uint8_t h[32]; kc::keccak256(pub65 + 1, 64, h); std::memcpy(eoa20, h + 12, 20); }

    // ---- structHash (V2 11 字段, 权威源 py-clob-client-v2 exchange_order_builder_v2) ----
    // CTF Exchange V2 (2026-04 切换), 去 taker/nonce/feeRateBps, 加 timestamp/metadata/builder
    static const char* OT = "Order(uint256 salt,address maker,address signer,uint256 tokenId,uint256 makerAmount,uint256 takerAmount,uint8 side,uint8 signatureType,uint256 timestamp,bytes32 metadata,bytes32 builder)";
    std::uint8_t th[32]; kc::keccak256(reinterpret_cast<const std::uint8_t*>(OT), std::strlen(OT), th);
    std::uint8_t buf[32 * 12];
    std::memcpy(buf, th, 32);
    u64_to_32(salt, buf + 32 * 1);
    addr_to_32(funder20, buf + 32 * 2);  // maker = funder (proxy/Safe)
    addr_to_32(eoa20, buf + 32 * 3);     // signer = EOA
    if (!dec_to_32(tokenId, buf + 32 * 4)) { std::memset(pk, 0, 32); std::printf("❌ tokenId\n"); return 1; }
    u64_to_32(makerAmt, buf + 32 * 5);
    u64_to_32(takerAmt, buf + 32 * 6);
    u64_to_32(side, buf + 32 * 7);       // side = 0 (BUY)
    u64_to_32(sigType, buf + 32 * 8);    // signatureType
    u64_to_32(now_ms, buf + 32 * 9);     // timestamp (ms, V2 替代 nonce)
    std::memset(buf + 32 * 10, 0, 32);   // metadata = bytes32(0)
    std::memset(buf + 32 * 11, 0, 32);   // builder  = bytes32(0)
    std::uint8_t sh[32]; kc::keccak256(buf, sizeof(buf), sh);

    // ---- domainSeparator (name="Polymarket CTF Exchange" version="2" exchange_v2 0xE111...) ----
    static const char* DT = "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)";
    std::uint8_t db[32 * 5];
    kc::keccak256(reinterpret_cast<const std::uint8_t*>(DT), std::strlen(DT), db);
    kc::keccak256(reinterpret_cast<const std::uint8_t*>("Polymarket CTF Exchange"), 23, db + 32);
    kc::keccak256(reinterpret_cast<const std::uint8_t*>("2"), 1, db + 64);
    u64_to_32(137, db + 96);
    const char* exch = std::getenv("EXCHANGE");  // 默认 V2 normal exchange (py-clob-client-v2 config)
    std::uint8_t v20[20]; hex2bytes(exch ? exch : "0xE111180000d2663C0091e4f400237545B87B996B", v20, 20); addr_to_32(v20, db + 128);
    std::uint8_t ds[32]; kc::keccak256(db, sizeof(db), ds);

    // ---- digest + sign ----
    std::uint8_t pre[66]; pre[0] = 0x19; pre[1] = 0x01; std::memcpy(pre + 2, ds, 32); std::memcpy(pre + 34, sh, 32);
    std::uint8_t digest[32]; kc::keccak256(pre, 66, digest);
    if (std::getenv("PRINT_DIGEST")) std::printf("DIGEST=%s salt=%llu\n", bytes2hex(digest, 32).c_str(), (unsigned long long)salt);
    secp256k1_ecdsa_recoverable_signature rsig;
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &rsig, digest, pk, nullptr, nullptr)) { std::memset(pk, 0, 32); std::printf("❌ sign\n"); return 1; }
    std::memset(pk, 0, 32);  // 私钥用完即清
    std::uint8_t sig64[64]; int recid = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, sig64, &recid, &rsig);
    std::uint8_t sig65[65]; std::memcpy(sig65, sig64, 64); sig65[64] = static_cast<std::uint8_t>(recid + 27);
    // P1-5 recover 自检
    { secp256k1_pubkey rp; if (!secp256k1_ecdsa_recover(ctx, &rp, &rsig, digest)) { std::printf("❌ recover\n"); return 1; }
      std::uint8_t p65[65]; std::size_t pl = 65; secp256k1_ec_pubkey_serialize(ctx, p65, &pl, &rp, SECP256K1_EC_UNCOMPRESSED);
      std::uint8_t h[32], r20[20]; kc::keccak256(p65 + 1, 64, h); std::memcpy(r20, h + 12, 20);
      if (std::memcmp(r20, eoa20, 20) != 0) { std::printf("❌ recover != EOA\n"); return 1; } }
    secp256k1_context_destroy(ctx);

    // ---- wire body (byte-exact, 用于 HMAC + POST) ----
    // 权威格式: py-clob-client-v2 order_data_v2.order_to_json_v2
    //   {"order":{...含 signature...},"owner":api_key,"orderType":"FOK","deferExec":false,"postOnly":false}
    std::string maker = bytes2hex(funder20, 20), signer = bytes2hex(eoa20, 20), sig = bytes2hex(sig65, 65);
    std::string z32 = "0x0000000000000000000000000000000000000000000000000000000000000000";  // metadata/builder
    std::string body = std::string("{\"order\":{")
        + "\"salt\":" + std::to_string(salt) + ","            // salt: JSON number
        + "\"maker\":\"" + maker + "\","
        + "\"signer\":\"" + signer + "\","
        + "\"tokenId\":\"" + tokenId + "\","
        + "\"makerAmount\":\"" + std::to_string(makerAmt) + "\","
        + "\"takerAmount\":\"" + std::to_string(takerAmt) + "\","
        + std::string("\"side\":\"") + (side == 0 ? "BUY" : "SELL") + "\","
        + "\"expiration\":\"0\","
        + "\"signatureType\":" + std::to_string(sigType) + ","  // JSON number
        + "\"timestamp\":\"" + std::to_string(now_ms) + "\","
        + "\"metadata\":\"" + z32 + "\","
        + "\"builder\":\"" + z32 + "\","
        + "\"signature\":\"" + sig + "\"},"
        + "\"owner\":\"" + api_key + "\","
        + "\"orderType\":\"FOK\","
        + "\"deferExec\":false,"
        + "\"postOnly\":false}";

    // ---- L2 HMAC-SHA256 (老李 §2.2) ----
    std::string ts = std::to_string(now_s);
    std::string base = ts + "POST" + "/order" + body;
    std::string secret_bytes = b64url_decode(api_secret);
    unsigned char mac[32]; unsigned int maclen = 32;
    HMAC(EVP_sha256(), secret_bytes.data(), static_cast<int>(secret_bytes.size()),
         reinterpret_cast<const unsigned char*>(base.data()), base.size(), mac, &maclen);
    std::string poly_sig = b64url_encode(mac, maclen);

    std::string addr_lc = signer;  // POLY_ADDRESS = signer EOA (api key owner), 非 funder
    std::printf("=== 下单参数 ===\n");
    std::printf("market token: %s\n", tokenId);
    std::printf("side=%s  makerAmount=%llu micro($%.4f)  takerAmount=%llu micro($%.4f)\n",
                side == 0 ? "BUY" : "SELL",
                (unsigned long long)makerAmt, makerAmt / 1e6, (unsigned long long)takerAmt, takerAmt / 1e6);
    std::printf("orderType=FOK  sigType=1  POLY_ADDRESS=%s\n", addr_lc.c_str());
    std::printf("body (%zu bytes): %s\n", body.size(), body.c_str());

    if (dryrun) { std::printf("\n[DRYRUN] 不 POST。HMAC/签名已算 (POLY_SIGNATURE redacted)。\n"); return 0; }

    // ---- POST ----
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* c = curl_easy_init();
    std::string resp;
    struct curl_slist* hdr = nullptr;
    hdr = curl_slist_append(hdr, "Content-Type: application/json");
    hdr = curl_slist_append(hdr, ("POLY_ADDRESS: " + addr_lc).c_str());
    hdr = curl_slist_append(hdr, ("POLY_SIGNATURE: " + poly_sig).c_str());
    hdr = curl_slist_append(hdr, ("POLY_TIMESTAMP: " + ts).c_str());
    hdr = curl_slist_append(hdr, (std::string("POLY_API_KEY: ") + api_key).c_str());
    hdr = curl_slist_append(hdr, (std::string("POLY_PASSPHRASE: ") + api_pass).c_str());
    curl_easy_setopt(c, CURLOPT_URL, "https://clob.polymarket.com/order");
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 8L);
    CURLcode rc = curl_easy_perform(c);
    long http = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    std::printf("\n=== CLOB 响应 ===\nHTTP %ld  curl=%s\n%s\n", http, curl_easy_strerror(rc), resp.c_str());
    curl_slist_free_all(hdr); curl_easy_cleanup(c); curl_global_cleanup();
    return (rc == CURLE_OK && http >= 200 && http < 300) ? 0 : 2;
}
