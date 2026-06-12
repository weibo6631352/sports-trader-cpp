# CLOB EIP-712 Oracle 对拍 (2026-06-13)

老板「不要和旧版 SDK 对标,有没有更好的方法」的答案。

## 为什么不对标 SDK
装的 py_clob_client 0.34.6 / py_order_utils 0.3.2 是【经典 12 字段旧格式】
(salt/maker/signer/taker/tokenId/makerAmount/takerAmount/expiration/nonce/feeRateBps/side/sigType),
而 PM 线上交易所(我们 $1 单已成交)用的是【11 字段 v2 新格式】(去 taker/expiration/nonce/feeRateBps,
加 timestamp/metadata/builder,domain version="2")。拿旧 SDK 当标尺会把我们正确的单误报为错。

## 更好的方法:三层守,彻底不碰 PM SDK 版本
| 层 | 守什么 | oracle | 在哪 |
|---|---|---|---|
| ① 数学 | C++ keccak/ABI编码/domain separator 算对没 | 通用 EIP-712 标准库 eth_account(喂我们自己的 11 字段 type)— 版本无关永不过时 | 本目录 eip712_oracle.py |
| ② 回归 | 我们别手滑改坏编码 | 金标准向量进 ctest | tests/unit/test_eip712_v2.cpp::OracleVectorsMatchGenericEip712 |
| ③ 协议 | 11 字段是否 PM 当前真要的 | 线上 CLOB 本身(arm 后 $1 canary,被拒=改协议=停) | arm 时人工 |

## 验证
eip712_oracle.py 对【实盘成交过的 ReferenceOrder】算出的 digest 与 test_eip712_v2.cpp
锁定的实盘值 0x56ea6e…f507b **逐字节一致** → oracle 方法被链上现实背书。

## 重新生成金标准 (PM 若真改了 type 时)
  .venv/bin/python experiments/laolei-clob-oracle-diff/eip712_oracle.py
输出粘进 test_eip712_v2.cpp 的 vecs[]。一次性预研,不进生产(生产是 C++ ComputeOrderV2Digest)。
