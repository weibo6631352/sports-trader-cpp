/**
 * DepthBar — 订单簿深度条
 * size/maxSize → width%, 买单绿色左对齐, 卖单红色右对齐
 * owner: 小苏  last_review: 2026-05-29
 */

interface DepthBarProps {
  size: number;
  maxSize: number;
  side: 'bid' | 'ask';
  /** 是否显示 (如无数据时隐藏) */
  show?: boolean;
}

export function DepthBar(props: DepthBarProps) {
  const pct = () => {
    const max = props.maxSize;
    if (!max || max <= 0) return '0%';
    const ratio = Math.min(props.size / max, 1);
    return `${(ratio * 100).toFixed(1)}%`;
  };

  const fillCls = () =>
    props.side === 'bid' ? 'depth-bar-fill-bid' : 'depth-bar-fill-ask';

  return (
    <div class="depth-bar-wrap">
      <div class={fillCls()} style={{ width: pct() }} />
    </div>
  );
}
