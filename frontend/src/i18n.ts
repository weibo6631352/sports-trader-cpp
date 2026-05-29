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
  basketball:  '篮球',
  soccer:      '足球',
  football:    '橄榄球',
  baseball:    '棒球',
  tennis:      '网球',
  hockey:      '冰球',
  ice_hockey:  '冰球',
  nhl:         '冰球',
  nba:         '篮球',
  nfl:         '橄榄球',
  mlb:         '棒球',
  fifa:        '足球',
};

/**
 * 从 slug 或 neg_risk_market_id 关键词推断运动类型中文名。
 * 用于后端 sport 字段为空时的 fallback (如 outright/futures 市场)。
 */
export function inferSportFromSlug(slug: string | null | undefined): string {
  if (!slug) return '';
  const s = slug.toLowerCase();
  if (s.includes('nhl') || s.includes('stanley-cup') || s.includes('hockey')) return '冰球';
  if (s.includes('nba') || s.includes('nba-finals') || s.includes('basketball')) return '篮球';
  if (s.includes('nfl') || s.includes('super-bowl') || s.includes('football')) return '橄榄球';
  if (s.includes('mlb') || s.includes('world-series') || s.includes('baseball')) return '棒球';
  if (s.includes('fifa') || s.includes('world-cup') || s.includes('soccer') || s.includes('champions-league')) return '足球';
  if (s.includes('tennis') || s.includes('wimbledon') || s.includes('us-open')) return '网球';
  return '';
}

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
  INVALID_INTENT:              '意图格式非法',
  ADVISORY_ONLY:               '仅顾问模式·不下单',
  MODEL_NOT_CALIBRATED:        '模型未校准',
  RISK_LIMIT_EXCEEDED:         '风控额度超限',
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
