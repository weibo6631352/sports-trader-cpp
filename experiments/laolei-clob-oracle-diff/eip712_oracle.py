#!/usr/bin/env python3
# CLOB EIP-712 oracle (一次性预研, 跑完归档): 用【通用 EIP-712 标准库 eth_account】对我们自己定义的
#   11 字段 v2 Order type 算出权威 digest, 作为 C++ 生产函数 ComputeOrderV2Digest 的标准答案。
# 不依赖任何 Polymarket SDK 版本 — 只用 EIP-712 标准本身, 永不过时 (老板「不要和旧版对标」)。
# 用法: .venv/bin/python eip712_oracle.py  → 打印每个固定向量的 digest (粘进 C++ 回归测试)。
from eth_account.messages import encode_typed_data
from eth_utils import keccak

# 我们生产代码 (src/stcpp/crypto/eip712_v2.cpp) 的 11 字段 type + domain (逐字对齐)。
ORDER_TYPE = [
    {"name": "salt", "type": "uint256"},
    {"name": "maker", "type": "address"},
    {"name": "signer", "type": "address"},
    {"name": "tokenId", "type": "uint256"},
    {"name": "makerAmount", "type": "uint256"},
    {"name": "takerAmount", "type": "uint256"},
    {"name": "side", "type": "uint8"},
    {"name": "signatureType", "type": "uint8"},
    {"name": "timestamp", "type": "uint256"},
    {"name": "metadata", "type": "bytes32"},
    {"name": "builder", "type": "bytes32"},
]
EXCHANGE = "0xE111180000d2663C0091e4f400237545B87B996B"          # neg_risk=false
NEG_RISK_EXCHANGE = "0xe2222d279d744050d28e00520010520000310F59"  # neg_risk=true

def digest(v):
    domain = {"name": "Polymarket CTF Exchange", "version": "2", "chainId": 137,
              "verifyingContract": NEG_RISK_EXCHANGE if v["neg_risk"] else EXCHANGE}
    msg = {
        "salt": v["salt"], "maker": v["maker"], "signer": v["signer"],
        "tokenId": int(v["tokenId"]), "makerAmount": v["makerAmount"],
        "takerAmount": v["takerAmount"], "side": v["side"], "signatureType": v["sigType"],
        "timestamp": v["timestamp"], "metadata": bytes(32), "builder": bytes(32),
    }
    full = {"types": {"EIP712Domain": [
                {"name": "name", "type": "string"}, {"name": "version", "type": "string"},
                {"name": "chainId", "type": "uint256"}, {"name": "verifyingContract", "type": "address"}],
            "Order": ORDER_TYPE},
            "primaryType": "Order", "domain": domain, "message": msg}
    sm = encode_typed_data(full_message=full)
    # EIP-712 digest = keccak(0x19 0x01 || domainSeparator(header) || structHash(body))
    return "0x" + keccak(b"\x19\x01" + sm.header + sm.body).hex()

VECTORS = [
    {"name": "V1_buy",        "salt": 1000000, "maker": "0x1111111111111111111111111111111111111111",
     "signer": "0x2222222222222222222222222222222222222222", "tokenId": "100",
     "makerAmount": 1000000, "takerAmount": 2000000, "side": 0, "sigType": 1,
     "timestamp": 1700000000000, "neg_risk": False},
    {"name": "V2_sell_negrisk","salt": 999999999, "maker": "0xabcdef0123456789abcdef0123456789abcdef01",
     "signer": "0x00000000000000000000000000000000deadbeef", "tokenId": "100",
     "makerAmount": 5530000, "takerAmount": 1070000, "side": 1, "sigType": 2,
     "timestamp": 1748476800000, "neg_risk": True},
    {"name": "V3_big_token",  "salt": 42, "maker": "0x78dE3c8264C546Fffed8D9A1396cddEf7c8686BE",
     "signer": "0xe98BAA12D2EE4be2A68577F47CBD986d9f4576e6",
     "tokenId": "71321045679252212594626385532706912750332728571942532289631379312455583992563",
     "makerAmount": 1000000, "takerAmount": 1010101, "side": 0, "sigType": 1,
     "timestamp": 1748476800000, "neg_risk": False},
]

for v in VECTORS:
    print(f'{{"{v["name"]}", {v["salt"]}ULL, "{v["maker"]}", "{v["signer"]}", "{v["tokenId"]}", '
          f'{v["makerAmount"]}ULL, {v["takerAmount"]}ULL, {v["side"]}, {v["sigType"]}, '
          f'{v["timestamp"]}ULL, {"true" if v["neg_risk"] else "false"}, "{digest(v)}"}},')
