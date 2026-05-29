/**
 * i18n.ts — 中文映射表 (5 张, 集中管理)
 * owner: 小苏
 * last_review: 2026-05-29
 */

export const STATUS_ZH: Record<string, string> = {
  inplay:   '进行中',
  halftime: '中场',
  final:    '完场',
  pregame:  '赛前',
};

export const SPORT_ZH: Record<string, string> = {
  basketball: '篮球',
  soccer:     '足球',
  football:   '橄榄球',
  baseball:   '棒球',
  tennis:     '网球',
  hockey:     '冰球',
};

export const MARKET_TYPE_ZH: Record<string, string> = {
  ml:     '胜负盘',
  total:  '大小盘',
  spread: '让分盘',
  h1:     '上半场',
  h2:     '下半场',
  q1:     '第一节',
  q2:     '第二节',
  q3:     '第三节',
  q4:     '第四节',
  series: '系列赛',
  prop:   '特殊盘',
};

export const WSS_STATE_ZH: Record<string, string> = {
  CONNECTED:    '已连接',
  DISCONNECTED: '已断开',
  unknown:      '未知',
};

export const REJECT_REASON_ZH: Record<string, string> = {
  MAX_POSITION_EXCEEDED:       '超过最大持仓',
  MARKET_NOT_ACCEPTING_ORDERS: '市场暂不接单',
  KELLY_FRACTION_CAP:          'Kelly 仓位上限',
};

export const SIDE_ZH: Record<string, string> = { BUY: '买', SELL: '卖' };

/** 从 condition_id 尾缀推断盘口类型中文名 */
export function inferMarketTypeZh(conditionId: string): string {
  if (!conditionId) return '—';
  const parts = conditionId.split('-');
  const last = parts[parts.length - 1];
  return MARKET_TYPE_ZH[last] ?? last.toUpperCase();
}

/** 从 market tokens 推断完整盘口标签 (含线值) */
export function inferMarketLabel(conditionId: string, market: { tokens?: Array<{ outcome?: string }> } | null): string {
  const typeZh = inferMarketTypeZh(conditionId);
  if (!market?.tokens || market.tokens.length === 0) return typeZh;

  const firstOutcome = market.tokens[0]?.outcome ?? '';
  if (conditionId.includes('-total')) {
    const m = firstOutcome.match(/[\d.]+/);
    if (m) return `${typeZh} ${m[0]}`;
  }
  if (conditionId.includes('-spread')) {
    const m = firstOutcome.match(/[+-][\d.]+/);
    if (m) return `${typeZh} ${m[0]}`;
  }
  return typeZh;
}
