---
name: inplay-goalserve-has-odds
description: inplay.goalserve.com/inplay-soccer.gz 有实时赔率(value_eu/bet365单源); v2.1 的 NO_ODDS 是 www 节点 base feed, 别混淆
metadata:
  type: reference
---

`inplay.goalserve.com/inplay-soccer.gz`(EU Sofia 节点)**带实时赔率** —— `events.<id>.odds.<market_id>.participants.<长pid>.{name:"Home"/"Draw"/"Away", value_eu, suspend}`,bet365 单源,1s refresh。market_id `"1"`=1X2全场 / `"27"`=1X2上半场(w8 §4.3 字典)。实测样本见 `docs/RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md` §3.2。

**别被 v2.1 的 NO_ODDS 误导:** `xiaoduan-goalserve-odds-by-sport-v2.1.md` 的 "NO_ODDS / 五大运动无赔率" 结论是针对 **www.goalserve.com 节点的 base feed**(`soccernew/home` 只含比分)——那是**另一个 endpoint**,不是 inplay odds 源。老板 2026-05-31 明确纠正过我这个混淆。

**当前为什么没数据流:** inplay 白名单(IP,403)未开 + 抓取时多在赛季外(无在赛比赛),**不是 key 没 odds plan**。白名单一开 + 有在赛比赛即流入。

**解析:** `ParseInplayOddsDevig`([[paperdaemon-app-layer]] 链路的一环)按 name 匹配 Home/Draw/Away(非位置,Goalserve 不保证序),market key 锚 `"<id>":`(防 `"suspend":"1"` 误匹配),暂停腿剔除 fail-closed。**注:解析器是按 w8 文档样本写的,真 odds 数据到手后仍需用第一份真样本复核字段路径。**

**Why:** 我曾据一份不全的 www 节点归档样本告诉老板"赔率没接通、需升级 odds plan",是错的。
**How to apply:** 提"直播源赔率/inplay odds"时,源是 inplay.goalserve.com(有 odds),缺口是白名单+赛季,不是 odds plan。
