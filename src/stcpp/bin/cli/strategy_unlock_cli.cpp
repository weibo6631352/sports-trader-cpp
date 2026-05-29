// src/stcpp/bin/cli/strategy_unlock_cli.cpp — stcpp-strategy-unlock CLI (老沈, W6 Wave 29)
//
// 命令: stcpp-strategy-unlock --strategy-id=X --trigger-audit-id=Y
//        --laohan-sig=B64 --laotang-sig=B64 --laolei-sig=B64
//        --exec-mode=paper|live [--emergency-override]
//
// 公钥 env: RM_UNLOCK_PUBKEY_{LAOHAN,LAOTANG,LAOLEI}  (base64 Ed25519 32B)
// 时间注入: RM_UNLOCK_TS_NS (测试用; 生产用 system_clock)
//
// 红线: R-1 set_state() API / R-2 共享 binary / R-7 exec-mode 隔离 / R-11 WAL 隔离
//       私钥不入日志不入 git

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include <sodium.h>

#include "stcpp/cli/three_signature.hpp"

namespace {

struct CliArgs {
    std::string strategy_id;
    std::string trigger_audit_id_hex;  // 32 hex chars = 16B
    std::string laohan_sig_b64;
    std::string laotang_sig_b64;
    std::string laolei_sig_b64;
    std::string exec_mode;
    bool        emergency_override{false};
};

std::string parse_val(std::string_view arg, std::string_view prefix) {
    return (arg.substr(0, prefix.size()) == prefix)
        ? std::string(arg.substr(prefix.size())) : std::string{};
}

bool parse_args(int argc, char** argv, CliArgs& out) {
    for (int i = 1; i < argc; ++i) {
        std::string_view a{argv[i]};
        if (a == "--emergency-override") { out.emergency_override = true; continue; }
        auto try_parse = [&](std::string_view pfx, std::string& dst) {
            auto v = parse_val(a, pfx);
            if (!v.empty()) { dst = v; return true; }
            return false;
        };
        if (try_parse("--strategy-id=",       out.strategy_id))         continue;
        if (try_parse("--trigger-audit-id=",  out.trigger_audit_id_hex)) continue;
        if (try_parse("--laohan-sig=",         out.laohan_sig_b64))      continue;
        if (try_parse("--laotang-sig=",        out.laotang_sig_b64))     continue;
        if (try_parse("--laolei-sig=",         out.laolei_sig_b64))      continue;
        if (try_parse("--exec-mode=",          out.exec_mode))            continue;
        std::cerr << "[ERROR] unknown arg: " << a << "\n";
        return false;
    }
    return true;
}

bool validate(CliArgs const& a) {
    if (a.strategy_id.empty())           { std::cerr << "[ERROR] --strategy-id required\n";        return false; }
    if (a.trigger_audit_id_hex.size()!=32){ std::cerr << "[ERROR] --trigger-audit-id must be 32 hex chars\n"; return false; }
    if (a.exec_mode!="paper"&&a.exec_mode!="live"){ std::cerr<<"[ERROR] --exec-mode=paper|live\n"; return false; }
    if (!a.emergency_override) {
        if (a.laohan_sig_b64.empty())  { std::cerr<<"[ERROR] --laohan-sig required\n";  return false; }
        if (a.laotang_sig_b64.empty()) { std::cerr<<"[ERROR] --laotang-sig required\n"; return false; }
    }
    if (a.laolei_sig_b64.empty()) { std::cerr<<"[ERROR] --laolei-sig required\n"; return false; }
    return true;
}

bool load_pubkey(const char* env_name,
                  std::array<std::uint8_t, stcpp::cli::kEd25519PubKeyBytes>& out) {
    const char* val = std::getenv(env_name);
    if (!val || !val[0]) { std::cerr<<"[ERROR] env "<<env_name<<" not set\n"; return false; }
    if (!stcpp::cli::ThreeSignatureVerifier::decode_base64_pubkey(val, out)) {
        std::cerr<<"[ERROR] "<<env_name<<" invalid base64 pubkey\n"; return false;
    }
    return true;
}

std::string hex16(std::array<std::uint8_t,16> const& b) {
    static const char* kH="0123456789abcdef";
    std::string s; s.reserve(32);
    for (auto x:b) { s+=kH[(x>>4)&0xF]; s+=kH[x&0xF]; }
    return s;
}

// Stub audit_id (seq-differentiated; production: RiskGateway::next_audit_id + randombytes)
std::array<std::uint8_t,16> make_audit_id(std::int64_t ts_ns, std::uint8_t seq) noexcept {
    std::array<std::uint8_t,16> id{};
    std::int64_t ms = ts_ns/1'000'000;
    for (int i=7;i>=0;--i) { id[static_cast<std::size_t>(i)]=static_cast<std::uint8_t>(ms&0xFF); ms>>=8; }
    id[8]=seq; id[9]=static_cast<std::uint8_t>(ts_ns&0xFF);
    return id;
}

// W6 stub IPC (W7 接 unix domain socket RPC)
void emit_ipc(std::string_view mode,
               std::array<std::uint8_t,16> const& a1,
               std::array<std::uint8_t,16> const& a2,
               std::array<std::uint8_t,16> const& a3,
               bool emergency) {
    std::cout<<"[IPC-STUB] {\"cmd\":\"set_state\",\"state\":\"RUNNING\",\"exec_mode\":\""
             <<mode<<"\",\"emergency\":"<<(emergency?"true":"false")
             <<",\"audit_ids\":[\""<<hex16(a1)<<"\",\""<<hex16(a2)<<"\",\""<<hex16(a3)<<"\"]}\n"
             <<"[INFO] RM HALTED -> RUNNING (exec_mode="<<mode<<")\n"
             <<"[INFO] 28-day observation window started. Any STRATEGY_DECAYED = immediate re-trigger.\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: stcpp-strategy-unlock --strategy-id=X --trigger-audit-id=Y"
                     " --laohan-sig=B64 --laotang-sig=B64 --laolei-sig=B64"
                     " --exec-mode=paper|live [--emergency-override]\n"
                     "Pubkeys from env: RM_UNLOCK_PUBKEY_{LAOHAN,LAOTANG,LAOLEI}\n";
        return 1;
    }
    if (sodium_init() < 0) { std::cerr<<"[FATAL] sodium_init() failed\n"; return 1; }

    CliArgs args;
    if (!parse_args(argc, argv, args) || !validate(args)) return 1;

    stcpp::cli::SignerInput laohan_in{}, laotang_in{}, laolei_in{};
    if (!args.emergency_override) {
        if (!load_pubkey("RM_UNLOCK_PUBKEY_LAOHAN",  laohan_in.pubkey))  return 1;
        if (!load_pubkey("RM_UNLOCK_PUBKEY_LAOTANG", laotang_in.pubkey)) return 1;
    }
    if (!load_pubkey("RM_UNLOCK_PUBKEY_LAOLEI", laolei_in.pubkey)) return 1;

    stcpp::cli::ThreeSignatureVerifier verifier;
    if (!args.emergency_override) {
        if (!verifier.decode_base64_sig(args.laohan_sig_b64,  laohan_in.signature))
            { std::cerr<<"[ERROR] --laohan-sig bad base64 (need 64B)\n";  return 1; }
        if (!verifier.decode_base64_sig(args.laotang_sig_b64, laotang_in.signature))
            { std::cerr<<"[ERROR] --laotang-sig bad base64 (need 64B)\n"; return 1; }
    }
    if (!verifier.decode_base64_sig(args.laolei_sig_b64, laolei_in.signature))
        { std::cerr<<"[ERROR] --laolei-sig bad base64 (need 64B)\n"; return 1; }

    // now_ns: allow test injection
    std::int64_t now_ns = 0;
    const char* ts_env = std::getenv("RM_UNLOCK_TS_NS");
    if (ts_env && ts_env[0]) {
        now_ns = std::stoll(ts_env);
    } else {
        now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();  // UTC epoch_ns
    }

    std::string payload = verifier.build_payload(
        args.strategy_id, args.trigger_audit_id_hex, now_ns);
    laohan_in.sig_timestamp_ns  = now_ns;
    laotang_in.sig_timestamp_ns = now_ns;
    laolei_in.sig_timestamp_ns  = now_ns;

    stcpp::cli::ThreeSignatureInput three;
    three.laohan            = laohan_in;
    three.laotang           = laotang_in;
    three.laolei            = laolei_in;
    three.payload           = payload;
    three.emergency_override = args.emergency_override;

    auto result = verifier.verify(three, now_ns);
    if (result != stcpp::cli::ThreeSigResult::OK) {
        std::cerr<<"[REJECTED] Sig verify FAILED: "<<stcpp::cli::ToString(result)
                 <<"\n[REJECTED] RM state NOT changed. STRATEGY_DECAYED remains.\n";
        return 1;
    }

    auto aid1 = make_audit_id(now_ns, 0x01);  // laohan  Unlock AET#11
    auto aid2 = make_audit_id(now_ns, 0x02);  // laotang Unlock AET#11
    auto aid3 = make_audit_id(now_ns, 0x03);  // laolei  Unlock AET#11

    if (args.emergency_override) {
        std::cerr<<"[WARN] EMERGENCY OVERRIDE. W7 retro + 72h post-mortem mandatory (SOP §7).\n";
        std::cout<<"[AUDIT] Unlock(EMERGENCY) laolei audit_id="<<hex16(aid3)<<"\n";
    } else {
        std::cout<<"[AUDIT] Unlock laohan  audit_id="<<hex16(aid1)<<"\n";
        std::cout<<"[AUDIT] Unlock laotang audit_id="<<hex16(aid2)<<"\n";
        std::cout<<"[AUDIT] Unlock laolei  audit_id="<<hex16(aid3)<<"\n";
    }

    emit_ipc(args.exec_mode, aid1, aid2, aid3, args.emergency_override);
    std::cout<<"[OK] strategy_id="<<args.strategy_id<<" exec_mode="<<args.exec_mode<<"\n";
    return 0;
}
