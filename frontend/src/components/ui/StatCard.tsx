/**
 * StatCard — Material Card 风格 KPI 卡片 (v7)
 * 用于 Ops 页系统健康 / 订阅数 / 吞吐等单值 KPI
 * owner: 小苏  last_review: 2026-05-29
 *
 * v7: 换用 SUID Card + CardContent + Typography
 */

import { JSX, Show } from 'solid-js';
import Card from '@suid/material/Card';
import CardContent from '@suid/material/CardContent';
import Typography from '@suid/material/Typography';

export type StatColor = 'default' | 'green' | 'red' | 'yellow';

interface StatCardProps {
  label: string;
  value: JSX.Element;
  unit?: string;
  sub?: JSX.Element;
  color?: StatColor;
  title?: string;
  pollHint?: string;
}

const COLOR_MAP: Record<StatColor, string> = {
  default: 'text.primary',
  green:   '#4caf50',
  red:     '#f44336',
  yellow:  '#ff9800',
};

export function StatCard(props: StatCardProps) {
  const valColor = () => COLOR_MAP[props.color ?? 'default'];

  return (
    <Card
      variant="outlined"
      title={props.title}
      sx={{ bgcolor: 'background.paper', height: '100%' }}
    >
      <CardContent sx={{ py: '10px !important', px: '12px !important' }}>
        {/* 标签行 */}
        <Typography
          variant="caption"
          sx={{ display: 'block', color: 'text.secondary', fontWeight: 600, letterSpacing: '0.05em', textTransform: 'uppercase', lineHeight: 1.3 }}
        >
          {props.label}
          <Show when={props.pollHint}>
            <span class="poll-hint" style={{ 'margin-left': '5px' }}>{props.pollHint}</span>
          </Show>
        </Typography>

        {/* 主值 */}
        <Typography
          sx={{
            fontFamily: 'monospace',
            fontSize: '20px',
            fontWeight: 700,
            lineHeight: 1.2,
            mt: 0.5,
            color: valColor(),
            whiteSpace: 'nowrap',
            overflow: 'hidden',
            textOverflow: 'ellipsis',
          }}
        >
          {props.value}
          <Show when={props.unit}>
            <Typography component="span" sx={{ fontSize: '11px', fontWeight: 400, color: 'text.secondary', ml: 0.5 }}>
              {props.unit}
            </Typography>
          </Show>
        </Typography>

        {/* 副文字 */}
        <Show when={props.sub}>
          <Typography variant="caption" sx={{ display: 'block', color: 'text.secondary', fontFamily: 'monospace', mt: 0.25 }}>
            {props.sub}
          </Typography>
        </Show>
      </CardContent>
    </Card>
  );
}
