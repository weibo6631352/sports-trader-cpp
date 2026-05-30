// derive_api_key.cpp — L1 ClobAuth EIP-712 派生 api 凭证 (F-11)
//
// 权威源: py-clob-client-v2 signing/eip712.py + signing/model.py
//   domain: ClobAuthDomain / version "1" / chainId 137 (无 verifyingContract)
//   struct: ClobAuth(address address,string timestamp,uint256 nonce,string message)
//   message = "This message attests that I control the given wallet", nonce=0
// 用私钥(EOA)签 → GET /auth/derive-api-key (POLY_ADDRESS/SIGNATURE/TIMESTAMP/NONCE)
//   → 返回 {apiKey, secret, passphrase} (该 EOA owner 的凭证, 幂等)
//
// 红线: 私钥/secret/passphrase 不全文 log (secret/passphrase 只打印前 6 字符比对)。
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <curl/curl.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include "keccak256.h"

namespace {
bool hex2bytes(const char* hex, std::uint8_t* out, std::size_t n) {
    if (hex[0]=='0'&&(hex[1]=='x'||hex[1]=='X')) hex+=2;
    if (std::strlen(hex)!=n*2) return false;
    for (std::size_t i=0;i<n;++i){ unsigned b; if(std::sscanf(hex+i*2,"%2x",&b)!=1) return false; out[i]=(std::uint8_t)b; }
    return true;
}
std::string b2h(const std::uint8_t* b,std::size_t n,bool px=true){ static const char* h="0123456789abcdef"; std::string s=px?"0x":""; for(std::size_t i=0;i<n;++i){s+=h[b[i]>>4];s+=h[b[i]&0xf];} return s; }
void u256(std::uint64_t v,std::uint8_t o[32]){ std::memset(o,0,32); for(int i=0;i<8;++i)o[31-i]=(std::uint8_t)(v>>(8*i)); }
void addr32(const std::uint8_t a[20],std::uint8_t o[32]){ std::memset(o,0,32); std::memcpy(o+12,a,20); }
void kstr(const char* s,std::uint8_t o[32]){ kc::keccak256((const std::uint8_t*)s,std::strlen(s),o); }
size_t wcb(char* p,size_t s,size_t n,void* u){ ((std::string*)u)->append(p,s*n); return s*n; }
}

int main(){
    const char* pk_hex=std::getenv("WALLET_PRIVATE_KEY");
    if(!pk_hex){ std::printf("❌ 缺 WALLET_PRIVATE_KEY\n"); return 1; }
    std::uint8_t pk[32];
    if(!hex2bytes(pk_hex,pk,32)){ std::printf("❌ 私钥格式\n"); return 1; }

    secp256k1_context* ctx=secp256k1_context_create(SECP256K1_CONTEXT_SIGN|SECP256K1_CONTEXT_VERIFY);
    std::uint8_t eoa[20];
    { secp256k1_pubkey pub; if(!secp256k1_ec_pubkey_create(ctx,&pub,pk)){std::memset(pk,0,32);std::printf("❌ 私钥无效\n");return 1;}
      std::uint8_t p65[65]; std::size_t pl=65; secp256k1_ec_pubkey_serialize(ctx,p65,&pl,&pub,SECP256K1_EC_UNCOMPRESSED);
      std::uint8_t h[32]; kc::keccak256(p65+1,64,h); std::memcpy(eoa,h+12,20); }
    std::string eoa_lc=b2h(eoa,20);
    std::uint64_t ts=(std::uint64_t)::time(nullptr);
    std::string ts_s=std::to_string(ts);

    // domainSeparator (无 verifyingContract)
    std::uint8_t db[32*4];
    kstr("EIP712Domain(string name,string version,uint256 chainId)",db);
    kstr("ClobAuthDomain",db+32);
    kstr("1",db+64);
    u256(137,db+96);
    std::uint8_t ds[32]; kc::keccak256(db,sizeof(db),ds);

    // structHash: ClobAuth(address,string timestamp,uint256 nonce,string message)
    std::uint8_t sb[32*5];
    kstr("ClobAuth(address address,string timestamp,uint256 nonce,string message)",sb);
    addr32(eoa,sb+32);
    kstr(ts_s.c_str(),sb+64);                                   // string timestamp → keccak
    u256(0,sb+96);                                              // nonce = 0
    kstr("This message attests that I control the given wallet",sb+128);  // string message → keccak
    std::uint8_t sh[32]; kc::keccak256(sb,sizeof(sb),sh);

    // digest + sign
    std::uint8_t pre[66]; pre[0]=0x19; pre[1]=0x01; std::memcpy(pre+2,ds,32); std::memcpy(pre+34,sh,32);
    std::uint8_t dig[32]; kc::keccak256(pre,66,dig);
    secp256k1_ecdsa_recoverable_signature rs;
    secp256k1_ecdsa_sign_recoverable(ctx,&rs,dig,pk,nullptr,nullptr);
    std::memset(pk,0,32);
    std::uint8_t s64[64]; int rid=0; secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx,s64,&rid,&rs);
    std::uint8_t s65[65]; std::memcpy(s65,s64,64); s65[64]=(std::uint8_t)(rid+27);
    std::string l1sig=b2h(s65,65);
    secp256k1_context_destroy(ctx);

    std::printf("EOA: %s\n", eoa_lc.c_str());

    // GET /auth/derive-api-key
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* c=curl_easy_init(); std::string resp; struct curl_slist* hd=nullptr;
    hd=curl_slist_append(hd,("POLY_ADDRESS: "+eoa_lc).c_str());
    hd=curl_slist_append(hd,("POLY_SIGNATURE: "+l1sig).c_str());
    hd=curl_slist_append(hd,("POLY_TIMESTAMP: "+ts_s).c_str());
    hd=curl_slist_append(hd,"POLY_NONCE: 0");
    curl_easy_setopt(c,CURLOPT_URL,"https://clob.polymarket.com/auth/derive-api-key");
    curl_easy_setopt(c,CURLOPT_HTTPHEADER,hd);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,wcb);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&resp);
    curl_easy_setopt(c,CURLOPT_TIMEOUT,8L);
    CURLcode rc=curl_easy_perform(c);
    long http=0; curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);
    std::printf("HTTP %ld curl=%s\nraw: %s\n", http, curl_easy_strerror(rc), resp.c_str());
    curl_slist_free_all(hd); curl_easy_cleanup(c); curl_global_cleanup();
    return (http>=200&&http<300)?0:2;
}
